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

Unit-filene i [`systemd/`](../systemd) er skrevet for `/home/simen/Hjemmeskjerm` og brukeren
`simen`. Endre `User=` og stiene om det er annerledes hos deg. systemd arver ikke PATH-en fra
skallet ditt, så alle programmer må oppgis med full sti. Ligger ikke node på `/usr/bin/node`
(sjekk med `which node`), må `ExecStart=` i `hjemmeskjerm-web.service` peke dit den ligger.
Er node installert med nvm, ligger den under `~/.nvm` og forsvinner når du bytter versjon –
da er det enklere å installere node fra apt eller NodeSource.

```sh
sudo cp ~/Hjemmeskjerm/systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hjemmeskjerm-web hjemmeskjerm-eink
journalctl -u hjemmeskjerm-eink -f
```

## Oppdatere koden

[`deploy.py`](../deploy.py) i rota gjør hele runden: stopper e-paper-løkka, kopierer koden
over, bygger driveren og nettsida på Pi-en, og starter tjenestene igjen.

```sh
./deploy.py
PI_HOST=pi@192.168.1.42 PI_DEST=/home/pi/Hjemmeskjerm ./deploy.py
```

`PI_HOST`, `PI_DEST` og `PI_PNPM` overstyrer maskin, sti og hvor pnpm ligger. pnpm oppgis med
full sti av nøyaktig samme grunn som unit-fila oppgir node med full sti: et ssh-kall får bare
`/usr/local/bin:/usr/bin:/bin:/usr/games`. pnpm legger seg selv til PATH nederst i `.bashrc`,
men `.bashrc` returnerer på vaktposten for ikke-interaktive skall lenge før den kommer dit.
Derfor virker `pnpm` når du logger inn og skriver den, og finnes ikke når skriptet kaller den –
heller ikke gjennom et login-skall. `command -v pnpm` i et interaktivt skall på Pi-en gir stien
å sette.

Løkka stoppes først med vilje. `pnpm build` tømmer `dist/`, og i det vinduet svarer vite
preview med en 404 som ellers ville blitt tatt skjermbilde av og malt på veggen. Feiler et
steg, blir løkka stående stoppet: skjermen fryser på det forrige bildet, og du retter opp og
kjører på nytt.

Bare filer som ligger i git blir kopiert, så `node_modules`, `.venv`, `driver/build` og
`epaper` på Pi-en røres ikke – lista kan ikke komme i utakt med `.gitignore`. Unntaket er
`webpage/.env.local`, som er gitignorert, men som Netatmo-oppsettet trenger på Pi-en; den
sendes for seg.

`webpage/.netatmo.json` sendes derimot aldri. Netatmo roterer refresh-tokenet og gjør det
forrige ugyldig, så Pi-en eier sin egen fil. Kopierer du din over, kaster du tokenet som
virker, og får `invalid_grant` til du bootstrapper på nytt.

`eink.toml` ligger i git og blir overskrevet. Justerer du `vcom`, `gamma` eller `concurrent`
mot det ekte panelet, må verdiene tilbake til repoet før neste deploy.

Skriptet bygger med standardverdien for `SPI_DIVIDER`, altså 16. Flytter du skjermen til en
Pi 4B, må `make SPI_DIVIDER=32` kjøres for hånd – se nedenfor.

### Sudoers for deploy

Skriptet starter og stopper tjenestene over ssh, og ssh gir ingen terminal å taste passord i.
Brukeren må derfor få kjøre akkurat disse kommandoene uten passord. Bruk `visudo`, som nekter
å lagre en fil med syntaksfeil – en ødelagt fil under `/etc/sudoers.d/` gjør at `sudo` slutter
å virke i det hele tatt:

```sh
sudo visudo -f /etc/sudoers.d/hjemmeskjerm
```

```
simen ALL=(root) NOPASSWD: /usr/bin/systemctl start hjemmeskjerm-web, \
  /usr/bin/systemctl stop hjemmeskjerm-web, \
  /usr/bin/systemctl restart hjemmeskjerm-web, \
  /usr/bin/systemctl start hjemmeskjerm-eink, \
  /usr/bin/systemctl stop hjemmeskjerm-eink, \
  /usr/bin/systemctl restart hjemmeskjerm-eink, \
  /usr/bin/systemctl daemon-reload, \
  /usr/bin/cp /home/simen/Hjemmeskjerm/systemd/hjemmeskjerm-web.service /etc/systemd/system/hjemmeskjerm-web.service, \
  /usr/bin/cp /home/simen/Hjemmeskjerm/systemd/hjemmeskjerm-eink.service /etc/systemd/system/hjemmeskjerm-eink.service
```

sudo sammenligner hele kommandolinja tegn for tegn. `systemctl restart hjemmeskjerm-web` og
`systemctl restart hjemmeskjerm-web.service` er to forskjellige strenger, og bare den første
står her. Stien må være den `command -v systemctl` gir på Pi-en, og stiene i `cp`-linjene må
stemme med `PI_DEST`.

Skriptet kopierer unit-filene bare når de faktisk er endret, og kjører `daemon-reload` etterpå.

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
| `--max-rects <n>` | Hvor mange rektangler skjermen deles i |
| `--concurrent <n>` | Hvor mange som tegnes i samme slengen |
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

Blir det flere rektangler enn panelet tegner på én gang, slås de nærmeste sammen to og to til
det er få nok igjen – det paret som koster minst i unødvendig tegnet areal først. Det er
viktig at det er *naboer*
som slås sammen: tar man i stedet den omsluttende boksen rundt alt, blir en endring øverst og
en nederst til en oppdatering av hele skjermen. Dekker rektanglene til slutt mer enn halve
skjermen, tegnes alt likevel. Til slutt forenes rektangler som overlapper hverandre: en
omsluttende boks kan legge seg over et rektangel som ikke var med i sammenslåinga, og da ville
det samme området blitt tegnet to ganger.

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

**Rektanglene tegnes samtidig.** Hvert rektangel koster en hel bølgeform, og bølgeformen er
også det synlige blinket – tegner man dem etter hverandre, blir ni rektangler til ni blink som
ruller over skjermen. IT8951 har flere LUT-motorer og kan tegne flere områder på én gang, så
`epaper` laster inn alle rektanglene først og fyrer så av alle `DPY_BUF_AREA`-kommandoene uten
å vente imellom. Da koster de til sammen én bølgeform og gir ett blink.

Det er ikke opplagt at det virker: `EPD_IT8951_WriteCommand` kaller `ReadBusy()` foran hver
kommando, og HRDY holdes lav mens panelet oppdaterer, så man skulle tro at neste kommando
uansett måtte vente. Målt på vårt panel med ni rektangler gikk det likevel fra omtrent sju
sekunder til omtrent tre, og av de tre er det meste oppstart. Ni bølgeformer ble til én.

Hvor mange som fyres av før vi venter igjen står i `concurrent`. Tallet finnes fordi antallet
LUT-motorer ikke står i noe vi kan lese. Registerkartet peker mot seksten – `LUT0`-registrene
ligger med 0x40 i steg, og `LUTAFSR` er status for alle sammen – men så mange tåler ikke vårt
panel: over åtte blir deler av skjermen rotete. Åtte gir rene rektangler.

`max_rects` er hvor mange rektangler diffen får dele skjermen i. Er den større enn
`concurrent`, går de i flere porsjoner, og **hver porsjon er ett blink**. Det er en reell
avveining: flere rektangler betyr mindre areal som tegnes opp, men flere blink. Ett blink over
et større område er ikke opplagt verre enn to blink over et mindre – her må man se på veggen.

| `max_rects` | Porsjoner ved `concurrent = 8` | Areal som tegnes |
|---|---|---|
| 8 | 1 | ~11 % |
| 16 | 2 | ~4 % |
| 24 | 3 | ~3 % |

Tallene er snitt over tilfeldige endringsmønstre, ikke over ekte tavler, så de sier mest om
formen på kurven: gevinsten flater ut fort.

Begge settes i [`eink.toml`](eink.toml) og sendes videre som `--concurrent` og `--max-rects`,
så de kan endres uten å bygge om. Utelates de, gjelder standardverdiene i `src/main.c` og
`src/diff.h`. Blir deler av skjermen rotete, eller blir rektangler stående uoppdaterte, er
`concurrent` for høy for panelet. Da er hurtiglageret i tillegg blitt feil, siden det mener
skjermen er riktig tegnet, og `--full` retter det opp.

Registerlesing duger ikke til å finne grensa – `EPD_IT8951_ReadReg` går selv gjennom
`WriteCommand` → `ReadBusy`, så et forsøk på å lese `LUTAFSR` underveis ville serialisert
nettopp det man prøver å måle. Klokka og skjermen er de eneste brukbare instrumentene.

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
