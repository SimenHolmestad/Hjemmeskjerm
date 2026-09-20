# Vendret Waveshare-kode

Disse filene kommer fra Waveshare sitt IT8951-eksempelrepo og er ikke skrevet av oss.
De beholder sine originale MIT-headere og sine engelske kommentarer.

- **Opphav:** <https://github.com/waveshareteam/IT8951-ePaper>
- **Commit:** `86406933d8f22af9fd3f2152b4958610c054b9a8` (2024-01-23)
- **Hentet fra:** `Raspberry/lib/`

## Hvilke filer

| Fil | Original sti |
|---|---|
| `EPD_IT8951.c` / `.h` | `Raspberry/lib/e-Paper/` |
| `DEV_Config.c` / `.h` | `Raspberry/lib/Config/` |
| `Debug.h` | `Raspberry/lib/Config/` |

Resten av `Raspberry/` er utelatt med vilje. `GUI_Paint.c` og `GUI_BMPfile.c` er erstattet av
våre egne `src/bmp.c` og `src/pack.c`; fontene, `example.c` og `examples/main.c` brukes ikke.
`RPI_gpiod.c` og `dev_hardware_SPI.c` trengs bare for `LIB=GPIOD`, som vi ikke støtter.

Utvalget er frittstående: `EPD_IT8951.c` og `DEV_Config.c` kaller bare `Debug`, `DEV_*` og
libc. De rører verken `GUI_Paint`, `isColor`, `Four_Byte_Align` eller globalene som lå i
`examples/main.c`.

## Våre endringer

Alle endringer er merket med `/* hjemmeskjerm: ... */` i koden, slik at en diff mot upstream
er lett å lese. Filene ble committet uendret først, så endringene ligger i egen commit.

### `EPD_IT8951.c`
- `EPD_IT8951_ReadBusy()` — timeout. Upstream spinner i en naken `while` uten timeout og uten
  å sove; en frakoblet skjerm henger prosessen for alltid på 100 % CPU.
- `EPD_IT8951_WaitForDisplayReady()` — timeout, og `static` fjernet så vi kan kalle den etter
  en oppdatering. Upstream kaller den bare *før* skriving, så `Clear_Refresh` og
  `4bp_Refresh` returnerer mens panelet fortsatt oppdaterer.
- `EPD_IT8951_Clear_Refresh()` — `malloc` sjekkes for NULL.

### `DEV_Config.c`
- Død `#elif USE_WIRINGPI_LIB`-gren fjernet. Den lå etter `#ifdef BCM`, `USE_WIRINGPI_LIB`
  defineres aldri av noen Makefile, og den refererte wiringPi-symboler vi ikke lenker mot.
- `lgSpiOpen()` og `lgGpioClaim*()` — returverdier sjekkes. Særlig viktig for GPIO 8 (CS):
  kjernen kan allerede eie den pinnen via `cs-gpios` i device tree, og da ble feilen
  tidligere slukt i stillhet slik at CS-styringen bare var en no-op.

### `Debug.h`
- `Debug()` skriver til `stderr` i stedet for `stdout`, og er stille med mindre
  `Debug_Enabled` er satt. `epaper info` skriver `key=value` til stdout, og den må ikke
  blandes med driverens logging.

### `EPD_IT8951.h`
- Inkluderingssti flatet ut (`"../Config/DEV_Config.h"` → `"DEV_Config.h"`).
- Prototype for `EPD_IT8951_WaitForDisplayReady()`.

### `epd_host.h`
Ny fil, ikke fra Waveshare. Den ene krok-headeren vendret kode inkluderer, slik at vi slipper
å la driveren inkludere noe fra `src/`.
