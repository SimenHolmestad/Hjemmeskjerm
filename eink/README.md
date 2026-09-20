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
make            # bygg
make test       # tester pakkingen, trenger ingen skjerm
make check      # syntakssjekker alt uten å lenke, virker også på en Mac
```

`make test` og `make check` kjører fint på en utviklingsmaskin uten e-paper. Det samme gjør
`render.py --once --no-display`, som skriver `frame.bmp` uten å røre panelet – nyttig for å
se hva som faktisk fanges opp før det havner på veggen.

## Ting som er verdt å vite

**SPI-overføringen går byte for byte.** `DEV_SPI_WriteByte` i den vendrete driveren gjør ett
kall per byte, og en full ramme er 1,3 MB. Under BCM er hvert kall en registerskriving mot
minnekartet, så det er raskere enn det høres ut som, men det er fortsatt den soleklart
største mulige optimaliseringen her: får man pakkebufferet skrevet i én blokk, havner
overføringen godt under sekundet. Mål med `time sudo ./epaper display ...` før du eventuelt
gjør noe med det.

**Ghosting.** Hver 20. runde kjøres `clear --init` først. Juster med `init_clear_every` i
`eink.toml`, eller sett den til 0 for å skru det av.

**Ei tapt runde er usynlig.** E-paper holder på bildet uten strøm, så når en runde feiler
logger `render.py` det og beholder det forrige bildet. Etter tre feil på rad startes
nettleseren på nytt.

## Vendret kode

`driver/vendor/it8951/` er Waveshare sin driver, MIT-lisensiert. Se
[`VENDOR.md`](driver/vendor/it8951/VENDOR.md) for nøyaktig hvor den kommer fra og hva vi har
endret. Endringene er merket med `/* hjemmeskjerm: */` i koden.
