# E-paper-visning

Viser [nettsida](../webpage) på en Waveshare 10,3" IT8951 e-paper-skjerm koblet til en
Raspberry Pi 3. Alt kjører på Pi-en:

```
systemd: vite preview            → http://localhost:4173
systemd: render/render.py        → løkke, hvert 5. minutt
           ├── chromium tar skjermbilde 1404×1872
           ├── Pillow: roter 90°, gråtone, 16 nivåer → frame.bmp (1872×1404)
           └── driver/epaper display frame.bmp
                 └── SPI/GPIO → IT8951 → panelet
```

`driver/` er et lite C-program som bare kan to ting: vise en BMP og tømme skjermen.
`render/` er Python-løkka som lager BMP-en og kaller det. All konfigurasjon ligger i
[`eink.toml`](eink.toml).

## Oppsett på Pi-en

Forutsetter 64-bits Raspberry Pi OS og at SPI er skrudd på (`sudo raspi-config` →
Interface Options → SPI).

```sh
sudo apt install liblgpio-dev chromium python3-venv
sudo usermod -aG gpio,spi $USER     # logg ut og inn etterpå

cd ~/Hjemmeskjerm/eink/driver && make
cd ../render && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt

cd ../../webpage && pnpm install && pnpm build
```

Sett `vcom` i `eink.toml` til verdien som står på klistremerket på flexkabelen til
panelet. Feil VCOM gir et utvasket eller altfor mørkt bilde, ikke en feilmelding.

## Første gangs test

Ta stegene i rekkefølge – hvert av dem utelukker en feilkilde.

Bygg med `LIB=BCM` først. Det er den varianten som allerede er kjent å virke på denne
maskinvaren, så får du bildet riktig én gang før du bytter til LGPIO.

```sh
cd ~/Hjemmeskjerm/eink/driver && make LIB=BCM

sudo ./epaper info     # leser bare enhetsinfo, rører ikke panelet
sudo ./epaper clear    # skal bli hvitt
```

Nettsida må kjøre før `render.py` har noe å ta bilde av. I et eget skall:

```sh
cd ~/Hjemmeskjerm/webpage && pnpm preview --port 4173
```

og så, i det første:

```sh
cd ~/Hjemmeskjerm/eink/render && .venv/bin/python render.py --once -v
```

(`render.py` sjekker at noen svarer på porten før den starter chromium, så du får en
forståelig feilmelding og ikke et kræsj hvis du glemmer det.)

**`sudo` er ikke valgfritt når man bygger med BCM.** bcm2835 trenger `/dev/mem` for SPI.
Uten root faller `bcm2835_init()` tilbake til `/dev/gpiomem` og returnerer *suksess*, men
lar SPI-registerpekeren stå som NULL – og da segfaulter `bcm2835_spi_begin()` rett etterpå.
Vi sjekker for root på forhånd og sier fra i stedet, men det er verdt å vite hvorfor.

Bygg med LGPIO når bildet står riktig, så slipper du root:

```sh
make clean && make
./epaper info
```

`epaper info` skal svare med `panel_w=1872`, `panel_h=1404` og en LUT-versjon. Feiler
den, er det SPI/GPIO som er problemet, ikke bildekoden.

Er bildet **speilvendt**, er det pakkingen i `driver/src/pack.c` som står feil vei. Står det
**opp ned**, bytt `panel.rotate` i `eink.toml` mellom 90 og 270.

## Kjør som tjeneste

Unit-filene i [`systemd/`](systemd) er skrevet for `/home/simen/Hjemmeskjerm` og brukeren
`simen`. Endre `User=` og stiene om det er annerledes hos deg.

```sh
sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hjemmeskjerm-web hjemmeskjerm-eink
journalctl -u hjemmeskjerm-eink -f
```

Etter en `git pull` som endrer nettsida: `cd webpage && pnpm build && sudo systemctl restart
hjemmeskjerm-web`. Endrer den C-koden: `cd eink/driver && make && sudo systemctl restart
hjemmeskjerm-eink`.

## `epaper`

| Kommando | Gjør |
|---|---|
| `epaper display <fil.bmp>` | Viser bildet med GC16 (16 gråtoner, full oppdatering) |
| `epaper clear` | Gjør skjermen hvit |
| `epaper clear --init` | Som over, men med INIT-bølgeform – skrubber bort ghosting |
| `epaper info` | Skriver `key=value` om panelet til stdout |

Flagg: `-v` slår på driverlogg til stderr, `--packed` bruker blokkskriving over SPI.
`EPAPER_VCOM` (volt, f.eks. `-1.14`) overstyrer den innkompilerte standardverdien;
`render.py` setter den selv fra `eink.toml`.

Avslutningskoder: `0` ok, `1` feil bruk, `2` feil med BMP-fila, `3` hardware eller
tidsavbrudd, `4` ugyldig konfigurasjon, `130` avbrutt.

## Bygging og testing

```sh
make            # LGPIO (standard)
make LIB=BCM    # bcm2835 i stedet – krever sudo ved kjøring
make test       # tester pakkingen, trenger ingen skjerm
make check      # syntakssjekker alt uten å lenke, virker også på en Mac
```

`make test` og `make check` kjører fint på en utviklingsmaskin uten e-paper. Det samme gjør
`render.py --once --no-display`, som skriver `frame.bmp` uten å røre panelet – nyttig for å
se hva som faktisk fanges opp før det havner på veggen.

## Ting som er verdt å vite

**SPI-overføringen går byte for byte.** `DEV_SPI_WriteByte` i den vendrete driveren gjør ett
kall per byte, og en full ramme er 1,3 MB. Under LGPIO betyr det ett ioctl per byte, og en
oppdatering kan ta i størrelsesorden 20–100 sekunder. Det spiller liten rolle når skjermen
uansett bare oppdateres hvert femte minutt, men det er den soleklart største mulige
opptimaliseringen her: får man pakkebufferet skrevet med én `lgSpiWrite` (eller i biter på
noen kB), havner overføringen godt under sekundet. Mål med `time ./epaper display ...` før du
eventuelt gjør noe med det.

**LGPIO og GPIO 8.** Driveren styrer CS (GPIO 8) som en vanlig utgang, men kjernen kan
allerede eie den pinnen gjennom `cs-gpios` i device tree. Skjer det, feiler
`lgGpioClaimOutput`, og uten sjekken vi har lagt inn ville CS-skrivingene stille blitt
no-ops – panelet får søppel uten at noe sier fra. Får du den feilen: legg
`dtoverlay=spi0-0cs` i `/boot/firmware/config.txt` og start på nytt, eller bygg med
`make LIB=BCM`, som går utenom kjernen via `/dev/mem` (men da kreves root).

**Ghosting.** Hver 20. runde kjøres `clear --init` først. Juster med `init_clear_every` i
`eink.toml`, eller sett den til 0 for å skru det av.

**Ei tapt runde er usynlig.** E-paper holder på bildet uten strøm, så når en runde feiler
logger `render.py` det og beholder det forrige bildet. Etter tre feil på rad startes
nettleseren på nytt.

## Vendret kode

`driver/vendor/it8951/` er Waveshare sin driver, MIT-lisensiert. Se
[`VENDOR.md`](driver/vendor/it8951/VENDOR.md) for nøyaktig hvor den kommer fra og hva vi har
endret. Endringene er merket med `/* hjemmeskjerm: */` i koden.
