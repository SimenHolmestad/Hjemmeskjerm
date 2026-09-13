import fs from "node:fs";
import http from "node:http";
import crypto from "node:crypto";
import { loadEnv } from "vite";

const PORT = 5199;
const REDIRECT_URI = `http://localhost:${PORT}/`;
const SCOPE = "read_station read_homecoach";

const env = loadEnv("development", process.cwd(), "NETATMO");
if (!env.NETATMO_CLIENT_ID || !env.NETATMO_CLIENT_SECRET) {
  console.error(
    "Mangler NETATMO_CLIENT_ID og NETATMO_CLIENT_SECRET i .env.local.\n" +
    "Begge står på appsiden din på dev.netatmo.com.",
  );
  process.exit(1);
}

const state = crypto.randomUUID();
const authorizeUrl = `https://api.netatmo.com/oauth2/authorize?${new URLSearchParams({
  client_id: env.NETATMO_CLIENT_ID,
  redirect_uri: REDIRECT_URI,
  scope: SCOPE,
  state,
}).toString().replace(/\+/g, "%20")}`;

async function exchange(code) {
  const response = await fetch("https://api.netatmo.com/oauth2/token", {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      grant_type: "authorization_code",
      code,
      client_id: env.NETATMO_CLIENT_ID,
      client_secret: env.NETATMO_CLIENT_SECRET,
      redirect_uri: REDIRECT_URI,
      scope: SCOPE,
    }),
  });
  const json = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(json.error || `HTTP ${response.status}`);
  return json.refresh_token;
}

// Bytter ut NETATMO_REFRESH_TOKEN i .env.local, eller legger den til.
function saveToken(token) {
  const line = `NETATMO_REFRESH_TOKEN=${token}`;
  const current = fs.readFileSync(".env.local", "utf8");
  fs.writeFileSync(
    ".env.local",
    /^NETATMO_REFRESH_TOKEN=.*$/m.test(current)
      ? current.replace(/^NETATMO_REFRESH_TOKEN=.*$/m, line)
      : `${current.replace(/\n*$/, "\n")}${line}\n`,
  );
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, REDIRECT_URI);
  const code = url.searchParams.get("code");
  if (!code) return res.end("Venter på svar fra Netatmo …");

  const reply = (message) => {
    res.setHeader("content-type", "text/plain; charset=utf-8");
    res.end(message);
  };

  try {
    if (url.searchParams.get("state") !== state) throw new Error("state stemmer ikke");
    saveToken(await exchange(code));
    reply("Ferdig. Refresh-tokenet er lagret i .env.local – du kan lukke vinduet.");
    console.log("\n✓ NETATMO_REFRESH_TOKEN lagret i .env.local. Kjør `pnpm dev`.");
  } catch (error) {
    reply(`Klarte ikke hente token: ${error.message}`);
    console.error(`\n✗ ${error.message}`);
  }
  server.close();
});

server.listen(PORT, () => {
  console.log(
    `Registrer ${REDIRECT_URI} som redirect URI på appsiden din på dev.netatmo.com.\n\n` +
    `Åpne så denne adressen i nettleseren og godkjenn:\n\n${authorizeUrl}\n`,
  );
});
