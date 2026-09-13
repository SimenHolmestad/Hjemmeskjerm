const SCREEN_WIDTH = 1404;
const SCREEN_HEIGHT = 1872;

// Skjermen rendres alltid i 1404x1872. På mindre skjermer (f.eks. under
// utvikling) skaleres hele siden ned slik at den får plass i vinduet.
function fitToViewport() {
  const scale = Math.min(
    1,
    window.innerWidth / SCREEN_WIDTH,
    window.innerHeight / SCREEN_HEIGHT,
  );
  document.documentElement.style.setProperty("--fit-scale", scale);
}

fitToViewport();
window.addEventListener("resize", fitToViewport);

const TOYENBADET_URL = "/api/toyenbadet";
const TOYENBADET_INTERVAL = 2 * 60 * 1000; // To minutter

// Tøyenbadet: antall inne nå, samt hvor mange som har gått inn og ut i dag.
async function updateToyenbadet() {
  try {
    const response = await fetch(TOYENBADET_URL);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const { visitorsIn, visitorsOut } = await response.json();

    document.getElementById("toyenbadet-now").textContent =
      visitorsIn - visitorsOut;
    document.getElementById("toyenbadet-in").textContent = visitorsIn;
    document.getElementById("toyenbadet-out").textContent = visitorsOut;
  } catch (error) {
    // Behold forrige tall på skjermen ved feil.
    console.error("Klarte ikke hente besøkstall:", error);
  }
}

updateToyenbadet();
setInterval(updateToyenbadet, TOYENBADET_INTERVAL);

const NETATMO_URL = "/api/netatmo";
const NETATMO_INTERVAL = 5 * 60 * 1000; // samme takt som Netatmo måler
const NETATMO_METRICS = ["temperature", "humidity", "co2"];
const NETATMO_DECIMALS = { temperature: 1, humidity: 0, co2: 0 };
// Uten et gulv på skalaen ser 21,3–21,5 °C ut som en fjellkjede.
const NETATMO_MIN_SPAN = { temperature: 1, humidity: 5, co2: 200 };
const SPARK_WIDTH = 72;
const SPARK_LEADER = 88; // stiplet linje fortsetter forbi grafen og bort til tallet
const SPARK_HEIGHT = 40;
const SPARK_PADDING = 2;

let netatmoFailures = 0;

function formatNetatmo(value, metric) {
  return value === null ? "–" : value.toFixed(NETATMO_DECIMALS[metric]).replace(".", ",");
}

// Regner ut sparkline-path, de stiplede linjene for høyeste og laveste måling,
// og hvor de to tallene skal stå. null er hull i dataene, og bryter linja.
function sparkGeometry(values, minSpan) {
  const numbers = values.filter((value) => value !== null);
  if (numbers.length < 2) return null;

  const high = Math.max(...numbers);
  const low = Math.min(...numbers);
  const span = Math.max(high - low, minSpan);
  const bottom = (high + low) / 2 - span / 2;
  const y = (value) =>
    SPARK_PADDING + (1 - (value - bottom) / span) * (SPARK_HEIGHT - 2 * SPARK_PADDING);

  let line = "";
  let command = "M";
  values.forEach((value, index) => {
    if (value === null) {
      command = "M";
      return;
    }
    const x = (index / (values.length - 1)) * SPARK_WIDTH;
    line += `${command}${x.toFixed(1)} ${y(value).toFixed(1)} `;
    command = "L";
  });

  return {
    line: line.trim(),
    rules: `M0 ${y(high).toFixed(1)}H${SPARK_LEADER}M0 ${y(low).toFixed(1)}H${SPARK_LEADER}`,
    max: { value: high, y: y(high) },
    min: { value: low, y: y(low) },
  };
}

function setNetatmoMetric(metric, value, history) {
  const geometry = sparkGeometry(history, NETATMO_MIN_SPAN[metric]);
  document.getElementById(`netatmo-${metric}`).textContent = formatNetatmo(value, metric);
  document.getElementById(`netatmo-spark-${metric}`).setAttribute("d", geometry?.line ?? "");
  document.getElementById(`netatmo-rule-${metric}`).setAttribute("d", geometry?.rules ?? "");

  // Tallene står på hver sin stiplede linje, så de må plasseres der linja havnet.
  const fallback = { max: SPARK_PADDING, min: SPARK_HEIGHT - SPARK_PADDING };
  for (const edge of ["max", "min"]) {
    const label = document.getElementById(`netatmo-range-${metric}-${edge}`);
    label.textContent = geometry ? formatNetatmo(geometry[edge].value, metric) : "–";
    label.style.top = `${geometry ? geometry[edge].y : fallback[edge]}px`;
  }
}

function clearNetatmo() {
  for (const metric of NETATMO_METRICS) {
    setNetatmoMetric(metric, null, []);
  }
}

// Netatmo: temperatur, luftfuktighet og CO2 med siste tre timer som sparkline.
async function updateNetatmo() {
  try {
    const response = await fetch(NETATMO_URL);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const { now, history, stale } = await response.json();

    // En frossen skjerm ser ut som en skjerm som virker, så vi tømmer tallene
    // i stedet for å vise gamle målinger i det uendelige.
    netatmoFailures = stale ? netatmoFailures + 1 : 0;
    if (netatmoFailures >= 3) {
      clearNetatmo();
      return;
    }

    for (const metric of NETATMO_METRICS) {
      setNetatmoMetric(metric, now[metric], history[metric]);
    }
  } catch (error) {
    console.error("Klarte ikke hente inneklima:", error);
    if (++netatmoFailures >= 3) clearNetatmo();
  }
}

updateNetatmo();
setInterval(updateNetatmo, NETATMO_INTERVAL);
