# E-paper-visning

Viser [nettsida](../webpage) på en Waveshare 10,3" IT8951 e-paper-skjerm koblet til en
Raspberry Pi 3. Alt kjører på Pi-en:

```
systemd: vite preview            → http://localhost:4173
systemd: render/render.py        → løkke, hvert 30. sekund
           ├── chromium tar skjermbilde 1404×1872
           ├── Pillow: roter 90°, gråtone, 16 nivåer → frame.bmp (1872×1404)
           └── driver/epaper display frame.bmp
                 ├── sammenligner med forrige ramme, finner rektanglene
                 └── SPI/GPIO → IT8951 → panelet, ett rektangel om gangen
```

`driver/` er et lite C-program som bare kan to ting: vise en BMP og tømme skjermen.
`render/` er Python-løkka som lager BMP-en og kaller det. All konfigurasjon ligger i
[`eink.toml`](eink.toml).

## Oppsett på Pi-en

Forutsetter 64-bits Raspberry Pi OS og at SPI er skrudd på (`sudo raspi-config` →
Interface Options → SPI).

```sh
sudo apt install chromium python3-venv
```

Driveren bruker `bcm2835`, som ikke ligger i apt og må bygges fra kilde:

```sh
cd /tmp
curl -O http://www.airspayce.com/mikem/bcm2835/bcm2835-1.75.tar.gz
tar xzf bcm2835-1.75.tar.gz && cd bcm2835-1.75
./configure && make && sudo make install
```

Så selve prosjektet:

```sh
cd ~/Hjemmeskjerm/eink/driver && make
cd ../render && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt

cd ../../webpage && pnpm install && pnpm build
```

Sett `vcom` i `eink.toml` til verdien som står på klistremerket på flexkabelen til
panelet. Feil VCOM gir et utvasket eller altfor mørkt bilde, ikke en feilmelding.

## Første gangs test

`epaper` må kjøre som root: `bcm2835` snakker med SPI gjennom `/dev/mem`. Programmet sier
fra hvis du glemmer det, i stedet for å krasje.

```sh
cd ~/Hjemmeskjerm/eink/driver

sudo ./epaper info     # leser bare enhetsinfo, rører ikke panelet
sudo ./epaper clear    # skal bli hvitt
```

`epaper info` skal svare med `panel_w=1872`, `panel_h=1404` og en LUT-versjon. Feiler den,
er det SPI/GPIO som er problemet, ikke bildekoden. `-v` gir driverlogg og en rådump av det
panelet svarte.

Så et ekte bilde. `--no-display` skriver bare BMP-fila og rører ikke panelet, så det steget
trenger ikke root. Nettsida må kjøre i et eget skall:

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

## La render.py nå panelet

`render.py` kjører som vanlig bruker – chromium nekter å kjøre som root uten
`--no-sandbox` – mens `epaper` krever root. Derfor kaller `render.py` `epaper` gjennom
`sudo` (`use_sudo` i `eink.toml`, på som standard), og brukeren får lov til å kjøre akkurat
det ene programmet uten passord:

```sh
echo "simen ALL=(root) NOPASSWD: /home/simen/Hjemmeskjerm/eink/driver/epaper" \
  | sudo tee /etc/sudoers.d/epaper
sudo chmod 440 /etc/sudoers.d/epaper
sudo -n ~/Hjemmeskjerm/eink/driver/epaper info    # skal virke uten passord
```

Da er det bare paneloppdateringa som kjører privilegert, ikke nettleseren.

```sh
cd ~/Hjemmeskjerm/eink/render && .venv/bin/python render.py --once -v
```

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
| `epaper display <fil.bmp>` | Viser bildet. Bare rektanglene som har endret seg siden sist blir tegnet |
| `epaper clear` | Gjør skjermen hvit |
| `epaper clear --mode init` | Som over, men med INIT-bølgeform – skrubber bort ghosting |
| `epaper info` | Skriver `key=value` om panelet til stdout |

| Flagg | Gjør |
|---|---|
| `--mode <bølgeform>` | `init`, `du`, `gc16` (standard), `gl16`, `glr16`, `gld16`, `a2`, `du4`, eller et tall 0–7 |
| `--full` | Tegn hele skjermen, ikke bare det som har endret seg |
| `--no-cache` | Ikke bruk hurtiglageret: tegn alt, og la det stå tomt etterpå |
| `-v` | Driverlogg til stderr, og hvilke rektangler som tegnes |
| `--no-packed` | Skriv pikseldataene ett ord om gangen i stedet for i blokker |

`EPAPER_VCOM` (volt, f.eks. `-1.14`) overstyrer den innkompilerte standardverdien;
`render.py` setter den selv fra `eink.toml`.

Avslutningskoder: `0` ok, `1` feil bruk, `2` feil med BMP-fila, `3` hardware eller
tidsavbrudd, `4` ugyldig konfigurasjon, `130` avbrutt.

## Bygging og testing

```sh
make            # bygg
make test       # tester pakkingen, trenger ingen skjerm
make check      # syntakssjekker alt uten å lenke, virker også på en Mac

make SPI_DIVIDER=32   # for en Pi 4B, se nedenfor
```

`make test` og `make check` kjører fint på en utviklingsmaskin uten e-paper. Det samme gjør
`render.py --once --no-display`, som skriver `frame.bmp` uten å røre panelet – nyttig for å
se hva som faktisk fanges opp før det havner på veggen.

## Ting som er verdt å vite

**SPI-overføringen går i blokker.** Waveshares vei skriver pikseldataene ett 16-bits ord om
gangen, og hvert ord er to `bcm2835_spi_transfer`-kall. Det kallet er ikke en enkel
registerskriving: det tømmer FIFO-en, setter TA=1, skriver én byte, venter på DONE og setter
TA=0 igjen. Klokka står altså stille mellom hver byte.

Vi går i stedet gjennom `DEV_SPI_WriteBytes` → `bcm2835_spi_writenb`, som holder FIFO-en
fôret så klokka løper sammenhengende. Dataene deles i blokker på 512 ord; hver blokk er en
helt vanlig dataskriving med CS og preamble, og BUSY leses foran hver av dem – samme
disiplin som den ordvise veien, bare en blokk om gangen i stedet for et ord.

`--no-packed` finnes bare som vei ut om blokkskrivinga skulle vise seg å krangle med panelet.
Går noe galt der, ser det ut som en skjerm som bare delvis tegnes opp.

**SPI-klokka hører til brettet.** Delefaktoren settes med `make SPI_DIVIDER=...` og er 16,
som er verdien for Pi 3/3B/3B+. Bytter du til en Pi 4B, må den til 32: delefaktoren deler
core clock, Pi 4 har dobbelt core clock av Pi 3, og
[Waveshares wiki](https://www.waveshare.com/wiki/10.3inch_e-Paper_HAT) advarer om at 16 på en
Pi 4B gir «transmission errors». De to verdiene sikter altså mot samme bussfart.

For høy fart viser seg som at `epaper info` leser nuller eller tull, eller som et forvrengt
bilde. Sjekk `info` etter en endring – `-v` skriver ut hvilken delefaktor bygget har. Vær
oppmerksom på at overføringsfeil kan være sporadiske, så en enkelt god ramme er ikke bevis;
la `render.py` gå noen runder.

**Bare det som endrer seg blir tegnet.** GC16 driver hver piksel gjennom svart før den
lander, så en oppdatering av hele panelet er den blinkingen man ser. `epaper` husker derfor
ramma panelet sist fikk, finner rektanglene som skiller seg, og tegner bare dem. Resten av
skjermen røres ikke og blinker ikke. Har ingenting endret seg, blir panelet stående helt i
fred.

Sammenligningen gjøres på det *pakkede* bufferet `pack.c` lager, ikke på BMP-en. Det bufferet
er allerede i panelets koordinater, allerede speilet og allerede kvantisert til de 16 nivåene
panelet viser, og én byte er nøyaktig to piksler. Da kommer rektanglene ut ferdig speilet og
på bytegrenser, og `render.py` slipper å vite noe om det hele. Rutenettet er 16 piksler: 4 er
det IT8951 krever av `Area_X` og `Area_W` i 4bpp, og 1872 går opp i 16. Logikken ligger i
`src/diff.c` og testes av `make test`.

Blir det flere enn tolv rektangler, slås de nærmeste sammen to og to til det er tolv igjen –
det paret som koster minst i unødvendig tegnet areal først. Det er viktig at det er *naboer*
som slås sammen: tar man i stedet den omsluttende boksen rundt alt, blir en endring øverst og
en nederst til en oppdatering av hele skjermen. Dekker rektanglene til slutt mer enn halve
skjermen, tegnes alt likevel – én full oppdatering er da billigere enn tolv.

`epaper display` skriver `rects=` og `area_pct=` til stdout, og `render.py` logger det hver
runde. Blinker skjermen mer enn ventet, er det de to tallene man skal se på. `-v` lister hvert
enkelt rektangel.

Forrige ramme ligger i `/var/lib/epaper/prev.4bpp`, med `/tmp` som reserve. Den skrives først
når hele oppdateringen har gått gjennom, så en avbrutt kjøring etterlater ingen fil, og neste
runde tegner alt på nytt. `--full` tvinger fram det samme når noe ser rart ut.

**Bølgeform.** `mode` i `eink.toml`. `gc16` er standard og gir alle 16 gråtoner; det er de
endrede rektanglene som blinker kort. `a2` blinker ikke i det hele tatt, men kan bare svart og
hvitt, så gråtonene i YR-grafen og vær-ikonene forsvinner i feltene som oppdateres. Waveshare
dokumenterer bare `init`, `gc16` og `a2` for denne skjermen, men modenummeret går rett videre
til firmwarens LUT, så `gl16` og `du` er verdt å prøve mot panelet:

```sh
sudo ./epaper display ../frame.bmp --full --mode gl16
```

En bølgeform firmwaren ikke har gir ingen feilmelding, så dette må ses på: forvent enten et
uendret panel eller et forvrengt bilde.

**Ghosting.** Hver 60. runde – en halvtime – kjøres `clear --mode init` først. Juster med
`init_clear_every` i `eink.toml`, eller sett den til 0 for å skru det av. Bruker du `a2` for
alvor, er det denne knappen du skal se på: A2 er en relativ bølgeform og etterlater mer
ghosting enn GC16.

**Ei tapt runde er usynlig.** E-paper holder på bildet uten strøm, så når en runde feiler
logger `render.py` det og beholder det forrige bildet. Etter tre feil på rad startes
nettleseren på nytt.

## Vendret kode

`driver/vendor/it8951/` er Waveshare sin driver, MIT-lisensiert. Se
[`VENDOR.md`](driver/vendor/it8951/VENDOR.md) for nøyaktig hvor den kommer fra og hva vi har
endret. Endringene er merket med `/* hjemmeskjerm: */` i koden.
