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

Ta stegene i rekkefølge. Hvert av dem utelukker én feilkilde, og de er lagt opp slik at
ingenting krever at det forrige steget var perfekt.

**1. Bygg med BCM og sjekk at panelet svarer.** BCM er den varianten som allerede er kjent å
virke på denne maskinvaren, så den brukes til å få bildet riktig én gang.

```sh
cd ~/Hjemmeskjerm/eink/driver && make LIB=BCM

sudo ./epaper info     # leser bare enhetsinfo, rører ikke panelet
sudo ./epaper clear    # skal bli hvitt
```

**`sudo` er ikke valgfritt med BCM.** bcm2835 trenger `/dev/mem` for SPI. Uten root faller
`bcm2835_init()` tilbake til `/dev/gpiomem` og returnerer *suksess*, men lar
SPI-registerpekeren stå som NULL – og da segfaulter `bcm2835_spi_begin()` rett etterpå. Vi
sjekker for root på forhånd og sier fra i stedet, men det er verdt å vite hvorfor.

**2. Lag et bilde, og vis det.** `--no-display` skriver bare BMP-fila og rører ikke panelet,
så dette steget trenger verken root eller skjerm. Nettsida må kjøre i et eget skall:

```sh
cd ~/Hjemmeskjerm/webpage && pnpm preview --port 4173
```

og så, i det første skallet:

```sh
cd ~/Hjemmeskjerm/eink/render && .venv/bin/python render.py --once --no-display -v
cd ../driver && sudo ./epaper display ../frame.bmp
```

Nå skal nettsida stå på skjermen. Er den **speilvendt**, er det pakkingen i
`driver/src/pack.c` som står feil vei. Står den **opp ned**, bytt `panel.rotate` i
`eink.toml` mellom 90 og 270.

**3. Få `render.py` til å nå panelet.** `render.py` kjører som vanlig bruker – blant annet
fordi chromium nekter å kjøre som root uten `--no-sandbox` – mens et BCM-bygg krever root.
Det er to veier ut av det.

*Enten* bygg med LGPIO, som ikke trenger root i det hele tatt:

```sh
cd ~/Hjemmeskjerm/eink/driver && make clean && make
./epaper info          # nå uten sudo
```

*eller* behold BCM-bygget og la `render.py` kalle det gjennom `sudo`. Sett
`use_sudo = true` under `[paths]` i `eink.toml`, og gi brukeren lov til å kjøre akkurat det
ene programmet uten passord:

```sh
echo "simen ALL=(root) NOPASSWD: /home/simen/Hjemmeskjerm/eink/driver/epaper" \
  | sudo tee /etc/sudoers.d/epaper
sudo chmod 440 /etc/sudoers.d/epaper
sudo -n ~/Hjemmeskjerm/eink/driver/epaper info    # skal virke uten passord
```

Da kjører fortsatt bare selve paneloppdateringa som root, ikke nettleseren.

Uansett vei:

```sh
cd ~/Hjemmeskjerm/eink/render && .venv/bin/python render.py --once -v
```

Svarer `epaper info` med `panelet rapporterte 0x0` under LGPIO, kom oppsettet opp, men
panelet svarer ikke. `./epaper info -v` dumper da de rå bytene fra panelet: bare nuller
betyr at ingenting svarer, søppel betyr at SPI går og at noe annet er galt. Se
[Når LGPIO ikke svarer](#når-lgpio-ikke-svarer) nedenfor. Det er ingen hast med å løse
det – BCM-veien over virker, og `use_sudo` gjør at tjenesten kan bruke den.

`epaper info` skal uansett backend svare med `panel_w=1872`, `panel_h=1404` og en
LUT-versjon. Feiler den, er det SPI/GPIO som er problemet, ikke bildekoden.

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

## Når LGPIO ikke svarer

Symptomet er at `epaper info` kommer gjennom oppsettet, men rapporterer `0x0`. På denne
maskina er følgende allerede utelukket: riktig gpiochip (`gpiodetect` viser `gpiochip0
[pinctrl-bcm2835]`), ingen som holder GPIO 8 (`gpioinfo` viser ingen `consumer=` på linje 8),
og SPI-frekvensen (helt ned til 1 MHz gir samme resultat).

Det som gjenstår å prøve, i rekkefølge:

1. **`dtoverlay=spi0-0cs`.** LGPIO-veien ble lagt til av Waveshare for Pi 5, og deres egen
   readme sier at man da skal kommentere ut `dtparam=spi=on` og legge inn `spi0-0cs` i
   stedet. Den overlayen gir spi0 null chip-select-linjer, så SPI-kontrolleren slutter å
   røre CE0 i det hele tatt, og driveren står fritt til å styre GPIO 8 selv.

   ```sh
   sudo nano /boot/firmware/config.txt   # kommenter ut dtparam=spi=on, legg til:
   # dtoverlay=spi0-0cs
   sudo reboot
   ```

2. **Sammenlikn mux-tilstanden** mellom en BCM-kjøring og en LGPIO-kjøring:

   ```sh
   pinctrl get 7-11        # eller: raspi-gpio get 7-11
   ```

   Under BCM skal 9, 10 og 11 være ALT0 og 8 være OUTPUT.

3. **BUSY-pinnen.** `./epaper info -v` skriver også hva GPIO 24 leser. Står den fast på 0
   mens panelet har strøm, kommer ikke GPIO-lesingene fram heller, og da er det ikke et
   rent SPI-problem.

## SPI-frekvens

Waveshare hardkodet 12,5 MHz i LGPIO-veien, mens BCM-veien kjører på 250 MHz / 32 = 7,8 MHz
på en Pi 3. Det er altså ikke samme fart på de to backendene, og et panel som er fornøyd med
den ene kan tie helt stille på den andre. Vi bruker 7,8 MHz som standard, siden det er farten
panelet er verifisert på her.

Får du `panelet rapporterte 0x0`, prøv deg nedover:

```sh
for hz in 7812500 4000000 2000000 1000000; do
  make clean >/dev/null && make SPI_HZ=$hz >/dev/null && echo "--- $hz Hz ---" && ./epaper info
done
```

Første frekvens som gir `panel_w=1872` er svaret. Sett den som standard ved å endre `SPI_HZ`
i `Makefile`. Går ingen av dem, er det ikke farten som er problemet – se GPIO 8 nedenfor.

`-v` viser hvilken gpiochip som ble åpnet og hvilken frekvens SPI kjører på:

```sh
./epaper info -v
```

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
