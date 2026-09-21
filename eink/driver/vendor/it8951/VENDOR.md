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
`RPI_gpiod.c` og `dev_hardware_SPI.c` hører til GPIOD-backenden, som vi ikke bruker.

Utvalget er frittstående: `EPD_IT8951.c` og `DEV_Config.c` kaller bare `Debug`, `DEV_*` og
libc. De rører verken `GUI_Paint`, `isColor`, `Four_Byte_Align` eller globalene som lå i
`examples/main.c`.

## Våre endringer

Alle endringer er merket med `/* hjemmeskjerm: ... */` i koden, slik at en diff mot upstream
er lett å lese. Filene ble committet uendret først, så endringene ligger i egne commits.

### `DEV_Config.c` / `DEV_Config.h`
- **`DEV_SPI_WriteBytes()` er ny.** Blokkskriving over SPI (`bcm2835_spi_writenb`). Se
  kommentaren i `DEV_Config.c` for hvorfor per-byte-veien er så mye dyrere enn den ser ut.
- **Bare BCM-backenden er beholdt.** Upstream har `#ifdef BCM / #elif LGPIO / #elif GPIOD` i
  ti blokker. LGPIO ble lagt til av Waveshare for Pi 5 og fikk aldri panelet til å svare på
  vår Pi 3 – `epaper info` kom gjennom oppsettet, men leste bare nuller, ved alle
  SPI-frekvenser fra 12,5 MHz ned til 1 MHz og med full dupleks – at den feilet like fullt på
  1 MHz er grunnen til at vi tror det var backenden og ikke farten. GPIOD har vi aldri brukt.
  Begge grenene er fjernet, sammen med `EPAPER_SPI_HZ`, `GPIO_Handle` og `SPI_Handle`.
  Fila gikk fra 315 til ~190 linjer, og det er ikke lenger noen `-D<backend>` å huske.
- **SPI-klokka er en byggeknapp.** Upstream har to linjer der den ene er kommentert ut,
  `DIVIDER_16` for Pi 3 og `DIVIDER_32` for Pi 4. Nå settes den med `make SPI_DIVIDER=...`,
  med `_Static_assert` på at den er en toerpotens. Standarden er 16, altså upstreams
  Pi 3-verdi – se README.
- Død `#elif USE_WIRINGPI_LIB`-gren fjernet. Den lå etter `#ifdef BCM`, `USE_WIRINGPI_LIB`
  defineres aldri av noen Makefile, og den refererte wiringPi-symboler vi ikke lenker mot.
- `DEV_Module_Init` sjekker `geteuid()` før `bcm2835_init()`. Uten root faller `bcm2835_init()`
  tilbake til `/dev/gpiomem` og returnerer *suksess*, men lar SPI-registerpekeren stå som
  NULL – og da segfaulter `bcm2835_spi_begin()` rett etterpå. Nå får man en forklaring.
- `DEV_GPIO_Mode` returnerer `int` i stedet for `void`, og `DEV_GPIO_Init` propagerer feilen.

### `EPD_IT8951.c`
- `EPD_IT8951_ReadBusy()` — timeout. Upstream spinner i en naken `while` uten timeout og uten
  å sove; en frakoblet skjerm henger prosessen for alltid på 100 % CPU.
- `EPD_IT8951_WaitForDisplayReady()` — timeout, og `static` fjernet så vi kan kalle den etter
  en oppdatering. Upstream kaller den bare *før* skriving, så `Clear_Refresh` og
  `4bp_Refresh` returnerer mens panelet fortsatt oppdaterer.
- `EPD_IT8951_Clear_Refresh()` — `malloc` sjekkes for NULL.
- `EPD_IT8951_WriteMuitiData()` — skriver i blokker gjennom det nye `DEV_SPI_WriteBytes` i
  stedet for to `DEV_SPI_WriteByte` per ord. Hver blokk er en vanlig dataskriving med CS og
  preamble, så BUSY leses underveis; upstream leste den bare én gang før hele ramma.
- `Source_Buffer_Length` i `HostAreaPackedPixelWrite_1bp/2bp/4bp` er `UDOUBLE` og ikke
  `UWORD`. Bredde ganger høyde sprenger 16 bit, så `Packed_Write` kunne ikke virke på et
  fullskjermsbilde.
- `EPD_IT8951_Clear_Refresh()` har fått `Packed_Write` som parameter. Upstream sendte `false`
  hardkodet videre, så `clear` kunne ikke bruke blokkveien.

### `Debug.h`
- `Debug()` skriver til `stderr` i stedet for `stdout`, og er stille med mindre
  `Debug_Enabled` er satt. `epaper info` skriver `key=value` til stdout, og den må ikke
  blandes med driverens logging.

### `EPD_IT8951.h`
- Inkluderingssti flatet ut (`"../Config/DEV_Config.h"` → `"DEV_Config.h"`).
- Prototype for `EPD_IT8951_WaitForDisplayReady()`. Upstream har den bare inne i en
  utkommentert blokk, samtidig som funksjonen er `static` i `.c`-fila.

### `epd_host.h`
Ny fil, ikke fra Waveshare. Den ene krok-headeren vendret kode inkluderer, slik at vi slipper
å la driveren inkludere noe fra `src/`.
