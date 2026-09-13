# Hjemmeskjerm

Status-skjerm til å ha i gangen hjemme. Skjermen viser en Entur-tavle, Netatmo-tall, antall besøkende på Tøyenbadet og YR-data. Mye av koden er laget med Claude.

![Screenshot av hjemmeskjerm](docs/images/example_screenshot.png)

# Hardware

Jeg bruker følgende:
- [10.3 tommer e-paper skjerm med driver](https://www.waveshare.com/10.3inch-e-Paper-HAT.htm) (1404×1872 piksler)
- [Raspberry PI 3](https://www.raspberrypi.com/products/raspberry-pi-3-model-b/)

# Oppsett

Foreløpig kjører serveren bare lokalt som en dev-server.

```sh
pnpm install
pnpm dev --host
```

Tavla- og YR-url-ene ligger i `.env`. Hvis du kopierer disse inn i `.env.local` og bytter ut verdiene vil det som står i `.env.local` gjelde.

# Entur

Entur-dataen kommer fra en Entur-tavle rendret i en Iframe. Se [tavla.entur.no](https://tavla.entur.no).

# YR
YR-dataen hentes ut som en widget, se [developer.yr.no/doc/guides/available-widgets](https://developer.yr.no/doc/guides/available-widgets).

# Tøyenbadet

Tall på hvor mange som er på Tøyenbadet ligger åpent tilgjengelig på denne siden: [web.datanova.com/OslobadeneERPTicketMonitor](https://web.datanova.com/OslobadeneERPTicketMonitor).

## Netatmo

Netatmo-dataen hentes via Netatmo sine API-er. Se [dev.netatmo.com/](https://dev.netatmo.com). Netatmo krever noe mer oppsett fordi det her er snakk om privat data (fra egen Netatmo) og man må legge inn sine egne secrets.

# Hvordan sette opp Netatmo

1. Logg inn på [dev.netatmo.com](https://dev.netatmo.com) og lag en app under **My Apps**. Navn og URL spiller ingen rolle.
2. Sett **redirect URI** på appsiden til `http://localhost:5199/`.
3. Fyll inn `.env.local` i prosjektmappa med `client id` og `client secret` fra
   appsiden. Fila er gitignorert og skal aldri committes.

   ```sh
   NETATMO_CLIENT_ID=...
   NETATMO_CLIENT_SECRET=...
   ```

4. Kjør `pnpm netatmo-token`. Den skriver ut en adresse du åpner i nettleseren og godkjenner, tar imot svaret fra Netatmo, og lagrer refresh-tokenet i `.env.local`.

Enheten velges automatisk: oppsettet leter etter modulen som rapporterer CO2, siden det bare måles innendørs. Har du flere moduler, kan du overstyre valget med `NETATMO_MODULE_ID=<mac-adresse>` i `.env.local`.
