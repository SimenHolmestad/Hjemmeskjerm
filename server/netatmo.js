import fs from "node:fs";
import path from "node:path";
import { loadEnv } from "vite";

const API = "https://api.netatmo.com";
const STATE_FILE = ".netatmo.json";

const HISTORY_HOURS = 3;
const GRID_STEP = 300; // Netatmo måler hvert femte minutt
const POINTS = (HISTORY_HOURS * 3600) / GRID_STEP;
const TYPES = ["Temperature", "Humidity", "CO2"];
const METRICS = ["temperature", "humidity", "co2"];

const CACHE_MS = 4 * 60 * 1000; // like under måleintervallet, så vi aldri hopper over en måling
const MIN_UPSTREAM_MS = 60 * 1000; // gulv mot en nettleser som laster på nytt i loop
const SERVE_STALE_MS = 2 * 60 * 60 * 1000;
const AUTH_BACKOFF_MS = 10 * 60 * 1000;
const TIMEOUT_MS = 10 * 1000;

let pending = null;
let authFailedAt = 0;
let cache = null;
let lastUpstream = 0;

function readState(root) {
  try {
    return JSON.parse(fs.readFileSync(path.join(root, STATE_FILE), "utf8"));
  } catch {
    return {}; // mangler eller er korrupt – da bootstrapper vi fra .env.local igjen
  }
}

function writeState(root, state) {
  const file = path.join(root, STATE_FILE);
  fs.writeFileSync(`${file}.tmp`, JSON.stringify(state, null, 2));
  fs.renameSync(`${file}.tmp`, file); // atomisk, så en drept prosess aldri etterlater en halv fil
}

// Et token som er limt inn i .env.local etter bootstrap skal vinne over det lagrede.
function pickRefreshToken(state, env) {
  if (env.NETATMO_REFRESH_TOKEN && env.NETATMO_REFRESH_TOKEN !== state.seed) {
    return env.NETATMO_REFRESH_TOKEN;
  }
  return state.refresh_token || env.NETATMO_REFRESH_TOKEN;
}

async function refresh(root, env) {
  const state = readState(root); // les på nytt: en annen prosess kan nettopp ha rotert tokenet
  const refreshToken = pickRefreshToken(state, env);
  if (!refreshToken || !env.NETATMO_CLIENT_ID || !env.NETATMO_CLIENT_SECRET) {
    throw new Error("mangler NETATMO_CLIENT_ID/CLIENT_SECRET/REFRESH_TOKEN i .env.local");
  }

  const response = await fetch(`${API}/oauth2/token`, {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      grant_type: "refresh_token",
      refresh_token: refreshToken,
      client_id: env.NETATMO_CLIENT_ID,
      client_secret: env.NETATMO_CLIENT_SECRET,
    }),
    signal: AbortSignal.timeout(TIMEOUT_MS),
  });
  const json = await response.json().catch(() => ({}));

  if (!response.ok) {
    if (json.error === "invalid_grant") {
      authFailedAt = Date.now();
      console.error(
        "[netatmo] invalid_grant – refresh-tokenet er ugyldig.\n" +
          "  Lag et nytt på dev.netatmo.com (scopes: read_homecoach read_station),\n" +
          "  og lim det inn i .env.local som NETATMO_REFRESH_TOKEN. Serveren restarter selv.",
      );
    }
    throw new Error(`token: ${json.error || `HTTP ${response.status}`}`);
  }

  // Refresh-tokenet roterer og det gamle blir ugyldig, så det nye må lagres med en gang.
  writeState(root, {
    ...state,
    seed: env.NETATMO_REFRESH_TOKEN,
    refresh_token: json.refresh_token,
    access_token: json.access_token,
    expires_at: Date.now() + json.expires_in * 1000,
    rotated_at: new Date().toISOString(),
  });
  authFailedAt = 0;
  return json.access_token;
}

async function accessToken(root, env) {
  const state = readState(root);
  if (state.access_token && Date.now() < state.expires_at - 2 * 60 * 1000) {
    return state.access_token;
  }
  // Ett felles kall om flere forespørsler kommer samtidig – to parallelle
  // refresh-kall ville ugyldiggjort hverandres token.
  pending ??= refresh(root, env).finally(() => {
    pending = null;
  });
  return pending;
}

async function get(token, endpoint, params) {
  const response = await fetch(`${API}${endpoint}?${new URLSearchParams(params)}`, {
    headers: { authorization: `Bearer ${token}` },
    signal: AbortSignal.timeout(TIMEOUT_MS),
  });
  const json = await response.json().catch(() => ({}));
  if (!response.ok) {
    const error = new Error(json.error?.message || `HTTP ${response.status}`);
    error.code = json.error?.code;
    throw error;
  }
  return json;
}

// Vi vet ikke om enheten er en Healthy Home Coach eller en værstasjon, så vi prøver begge.
async function detect(token, env) {
  for (const [kind, endpoint] of [
    ["homecoach", "/api/gethomecoachsdata"],
    ["station", "/api/getstationsdata"],
  ]) {
    const json = await get(token, endpoint, {}).catch(() => null); // manglende scope gir 403
    const devices = json?.body?.devices ?? [];
    const candidates = devices.flatMap((device) => [
      { device_id: device._id, module_id: null, data: device.dashboard_data },
      ...(device.modules ?? []).map((module) => ({
        device_id: device._id,
        module_id: module._id,
        data: module.dashboard_data,
      })),
    ]);

    if (env.NETATMO_MODULE_ID) {
      const pinned = candidates.find(
        (candidate) =>
          candidate.module_id === env.NETATMO_MODULE_ID ||
          candidate.device_id === env.NETATMO_MODULE_ID,
      );
      if (pinned) return { kind, device_id: pinned.device_id, module_id: pinned.module_id };
      continue;
    }

    // CO2 måles bare innendørs, så det er den beste markøren for riktig modul.
    const indoor = candidates
      .filter((candidate) => typeof candidate.data?.CO2 === "number")
      .sort((a, b) => b.data.time_utc - a.data.time_utc);
    const pick =
      indoor[0] ?? candidates.find((candidate) => typeof candidate.data?.Temperature === "number");
    if (pick) return { kind, device_id: pick.device_id, module_id: pick.module_id };
  }
  throw new Error("fant ingen Netatmo-enhet med innendørsdata");
}

// optimize=false er dokumentert både som et objekt med tidsstempel som nøkkel og som en
// liste med blokker (én per sammenhengende periode). Vi tar imot begge formene.
function toSamples(body) {
  if (Array.isArray(body)) {
    return body.flatMap((chunk) => {
      if (!chunk || !Array.isArray(chunk.value)) {
        throw new Error("uventet blokk i getmeasure-svaret");
      }
      const step = chunk.step_time ?? 0; // en blokk med ett punkt kan mangle step_time
      return chunk.value.map((row, index) => ({ t: chunk.beg_time + index * step, row }));
    });
  }
  if (body && typeof body === "object") {
    return Object.entries(body).map(([t, row]) => ({ t: Number(t), row }));
  }
  throw new Error("uventet getmeasure-svar");
}

function toPayload(samples, begin) {
  const history = Object.fromEntries(METRICS.map((metric) => [metric, Array(POINTS).fill(null)]));
  const now = Object.fromEntries(METRICS.map((metric) => [metric, null]));
  let updated = 0;

  for (const { t, row } of [...samples].sort((a, b) => a.t - b.t)) {
    if (!Array.isArray(row)) continue;
    const index = Math.round((t - begin) / GRID_STEP);
    METRICS.forEach((metric, i) => {
      if (typeof row[i] !== "number") return;
      if (index >= 0 && index < POINTS) history[metric][index] = row[i];
      now[metric] = row[i]; // sortert stigende, så siste verdi blir stående
    });
    updated = Math.max(updated, t);
  }

  return {
    updated,
    // getmeasure har ingen reachable-flagg, så vi utleder det av alderen på siste måling.
    stale: updated === 0 || Date.now() / 1000 - updated > 20 * 60,
    now,
    history,
  };
}

async function load(root, env) {
  const token = await accessToken(root, env);
  let state = readState(root);

  if (!state.device) {
    state = { ...readState(root), device: await detect(token, env) };
    writeState(root, state);
  }

  const begin = Math.floor(Date.now() / 1000) - HISTORY_HOURS * 3600;
  const params = {
    device_id: state.device.device_id,
    scale: "max",
    type: TYPES.join(","),
    date_begin: begin,
    optimize: "false",
    real_time: "true",
  };
  if (state.device.module_id) params.module_id = state.device.module_id;

  let json;
  try {
    json = await get(token, "/api/getmeasure", params);
  } catch (error) {
    if (error.code !== 21) throw error; // 21 = device_not_found
    writeState(root, { ...readState(root), device: undefined });
    return load(root, env);
  }

  if (env.NETATMO_DEBUG) console.dir(json, { depth: 4 });
  return toPayload(toSamples(json.body), begin);
}

function send(res, status, body) {
  res.statusCode = status;
  res.setHeader("content-type", "application/json");
  res.setHeader("cache-control", "no-store");
  res.end(JSON.stringify(body));
}

async function handle(res, root, env) {
  const now = Date.now();
  const fresh = cache && now - cache.at < CACHE_MS;

  if (!fresh && now - lastUpstream > MIN_UPSTREAM_MS && now - authFailedAt > AUTH_BACKOFF_MS) {
    lastUpstream = now;
    try {
      cache = { at: now, payload: await load(root, env) };
    } catch (error) {
      console.error("[netatmo]", error.message);
    }
  }

  // Vis heller gamle tall enn ingenting mens ruteren starter på nytt – men merk dem som gamle.
  if (cache && now - cache.at < SERVE_STALE_MS) {
    send(res, 200, { ...cache.payload, stale: cache.payload.stale || now - cache.at > CACHE_MS });
    return;
  }
  send(res, 503, { error: "ingen data fra Netatmo" });
}

export default function netatmo() {
  const install = (server) => {
    const root = server.config.root;
    // NETATMO_* ligger utenfor VITE_-prefikset, og havner derfor aldri i nettleserbundlen.
    const env = loadEnv(server.config.mode, root, "NETATMO");
    server.middlewares.use("/api/netatmo", (_req, res) => {
      handle(res, root, env).catch((error) => {
        console.error("[netatmo]", error.message);
        send(res, 503, { error: error.message });
      });
    });
  };

  return { name: "netatmo", configureServer: install, configurePreviewServer: install };
}
