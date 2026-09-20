#!/usr/bin/env python3
"""Tar skjermbilde av hjemmeskjerm-sida og viser det på e-paper-skjermen.

Kjører som én langtlevende prosess. Nettleseren og sida holdes åpne mellom
rundene, så det er bare den aller første runden som betaler ventinga på
iframe-ene. Sida oppdaterer sine egne tall selv.

    python render.py                    løkka, slik systemd kjører den
    python render.py --once             én runde, så avslutt
    python render.py --once --no-display  skriv bare BMP-fila (trenger ingen skjerm)
"""

import argparse
import io
import logging
import os
import signal
import socket
import subprocess
import sys
import time
import tomllib
import urllib.parse
from pathlib import Path

from PIL import Image
from playwright.sync_api import sync_playwright

log = logging.getLogger("render")

# Settes av signalhåndtereren. Løkka sjekker den mellom rundene.
_stopp = False


def _be_om_stopp(signum, _frame):
    global _stopp
    _stopp = True
    log.info("fikk signal %s, avslutter etter denne runden", signum)


class Config:
    """eink.toml med fornuftige standardverdier for alt."""

    def __init__(self, sti: Path):
        with sti.open("rb") as fp:
            raw = tomllib.load(fp)
        rot = sti.parent

        side = raw.get("page", {})
        self.url = side.get("url", "http://localhost:4173/")
        self.side_bredde = int(side.get("width", 1404))
        self.side_hoyde = int(side.get("height", 1872))
        self.settle = float(side.get("settle_seconds", 10))
        self.reload_minutter = float(side.get("reload_minutes", 60))

        panel = raw.get("panel", {})
        self.panel_bredde = int(panel.get("width", 1872))
        self.panel_hoyde = int(panel.get("height", 1404))
        self.vcom = float(panel.get("vcom", -1.14))
        self.rotasjon = int(panel.get("rotate", 90))
        if self.rotasjon not in (90, 270):
            raise ValueError(f"panel.rotate må være 90 eller 270, ikke {self.rotasjon}")

        bilde = raw.get("image", {})
        self.gamma = float(bilde.get("gamma", 1.0))
        self.kontrast = float(bilde.get("contrast", 1.0))
        if self.gamma <= 0:
            raise ValueError("image.gamma må være større enn 0")

        lokke = raw.get("loop", {})
        self.intervall = float(lokke.get("interval_seconds", 300))
        self.init_clear_hver = int(lokke.get("init_clear_every", 20))

        # Brukes bare til å gi en nyttig feilmelding når serveren er nede.
        self.webpage_dir = (rot.parent / "webpage").resolve()

        stier = raw.get("paths", {})
        self.epaper = (rot / stier.get("epaper", "driver/epaper")).resolve()
        # epaper krever root: bcm2835 trenger /dev/mem for SPI. render.py
        # kjører som vanlig bruker – chromium nekter å kjøre som root uten
        # --no-sandbox – så selve epaper-kallet går gjennom sudo.
        self.bruk_sudo = bool(stier.get("use_sudo", True))
        self.frame = (rot / stier.get("frame", "frame.bmp")).resolve()
        self.timeout = float(stier.get("timeout_seconds", 180))

        # Chromium-binæret. På Raspberry Pi OS installeres det med apt, og da
        # slipper vi Playwright sin egen nedlasting, som ikke er bygget for
        # Raspberry Pi OS på arm64.
        self.chromium = os.environ.get("EINK_CHROMIUM") or _finn_chromium()


def _finn_chromium() -> str | None:
    for sti in ("/usr/bin/chromium", "/usr/bin/chromium-browser"):
        if Path(sti).exists():
            return sti
    return None   # la Playwright bruke sin egen


def bygg_lut(gamma: float, kontrast: float) -> list[int]:
    """Én 256-oppslagstabell som gjør kontrast, gamma og kvantisering samtidig.

    Kvantiseringen til 16 nivåer (& 0xF0) gjør at BMP-fila på disk ser ut
    nøyaktig som det panelet kommer til å vise. Det er verdt en del når man
    feilsøker uten å stå foran skjermen.
    """
    lut = []
    for i in range(256):
        v = i / 255.0
        v = (v - 0.5) * kontrast + 0.5
        v = min(1.0, max(0.0, v))
        v = v ** (1.0 / gamma)
        b = int(round(v * 255.0))
        lut.append(min(255, max(0, b)) & 0xF0)
    return lut


def lag_bilde(png: bytes, cfg: Config, lut: list[int]) -> Image.Image:
    """PNG fra nettleseren -> gråtonebilde i panelets format."""
    bilde = Image.open(io.BytesIO(png)).convert("L")

    dreining = Image.Transpose.ROTATE_90 if cfg.rotasjon == 90 else Image.Transpose.ROTATE_270
    bilde = bilde.transpose(dreining)

    if bilde.size != (cfg.panel_bredde, cfg.panel_hoyde):
        log.warning(
            "bildet ble %dx%d, panelet er %dx%d – skalerer",
            bilde.width, bilde.height, cfg.panel_bredde, cfg.panel_hoyde,
        )
        bilde = bilde.resize((cfg.panel_bredde, cfg.panel_hoyde), Image.LANCZOS)

    return bilde.point(lut)


def kall_epaper(cfg: Config, *args: str) -> str:
    """Kjører epaper-programmet og returnerer det det skrev.

    Kaster RuntimeError ved feil, med programmets egen utskrift i meldinga –
    epaper forklarer stort sett selv hva som er galt, og den forklaringa er
    til liten nytte hvis den blir liggende i en pipe ingen leser.
    """
    kommando = [str(cfg.epaper), *args]
    if cfg.bruk_sudo:
        # -n: feil heller enn å bli stående og vente på et passord ingen ser.
        kommando = ["sudo", "-n", *kommando]
    miljo = {**os.environ, "EPAPER_VCOM": f"{cfg.vcom}"}
    log.debug("kjører %s", " ".join(kommando))
    start = time.monotonic()
    res = subprocess.run(
        kommando, env=miljo, timeout=cfg.timeout,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    utdata = (res.stdout or "").strip()

    if res.returncode != 0:
        if res.returncode < 0:
            sig = -res.returncode
            navn = signal.Signals(sig).name if sig in signal.Signals.__members__.values() else sig
            grunn = f"ble drept av signal {navn}"
        else:
            grunn = f"avsluttet med kode {res.returncode}"

        deler = [f"epaper {args[0]} {grunn}"]
        if utdata:
            deler.append(utdata.rstrip("."))
        # Den desidert vanligste årsaken: epaper krever root (bcm2835 bruker
        # /dev/mem), mens render.py kjører som vanlig bruker. Eldre bygg
        # segfaulter i stedet for å si fra.
        if res.returncode in (-signal.SIGSEGV, 3):
            if cfg.bruk_sudo:
                deler.append(
                    f"Virker `sudo -n {cfg.epaper} info` fra denne brukeren? "
                    "Se sudoers-oppsettet i eink/README.md"
                )
            else:
                deler.append(
                    "epaper krever root. Sett use_sudo = true i eink.toml"
                )
        raise RuntimeError(". ".join(deler))

    log.debug("%s tok %.1f s", args[0], time.monotonic() - start)
    return utdata


def vent_på_server(url: str, budsjett: float) -> bool:
    """Venter til noen svarer på verten og porten i url. True hvis de gjør det.

    Sjekken gjøres før chromium startes – å starte nettleseren tar snaue 20
    sekunder på en Pi 3, og det er bortkastet hvis det ikke er noe å hente.

    Budsjettet er kort ved --once (da står du og venter på svar) og langt i
    løkkemodus, der systemd kan ha startet oss før vite rekker å lytte.
    After= i unit-fila sier bare når prosessen ble startet, ikke når den er
    klar til å ta imot.
    """
    deler = urllib.parse.urlsplit(url)
    vert = deler.hostname or "localhost"
    port = deler.port or (443 if deler.scheme == "https" else 80)

    frist = time.monotonic() + budsjett
    sagt_fra = False
    while not _stopp:
        try:
            with socket.create_connection((vert, port), timeout=2):
                return True
        except OSError:
            pass
        if time.monotonic() >= frist:
            return False
        if not sagt_fra and budsjett > 10:
            log.info("ingen svarer på %s:%d ennå – venter", vert, port)
            sagt_fra = True
        time.sleep(1)
    return False


class Nettleser:
    """Holder chromium og sida i live mellom rundene."""

    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._pw = None
        self._browser = None
        self.page = None
        self.lastet = 0.0

    def start(self) -> None:
        cfg = self.cfg
        self._pw = sync_playwright().start()
        opp = {
            "headless": True,
            # /dev/shm er liten på en Pi, og et viewport på 2,6 megapiksler
            # er ikke lite. Uten denne kan chromium kræsje under rendringen.
            "args": ["--disable-dev-shm-usage", "--disable-gpu"],
        }
        if cfg.chromium:
            opp["executable_path"] = cfg.chromium
        log.info("starter chromium (%s)", cfg.chromium or "playwright sin egen")
        self._browser = self._pw.chromium.launch(**opp)
        self.page = self._browser.new_page(
            viewport={"width": cfg.side_bredde, "height": cfg.side_hoyde},
            device_scale_factor=1,
        )
        self.last()

    def last(self) -> None:
        log.info("laster %s", self.cfg.url)
        self.page.goto(self.cfg.url, wait_until="load", timeout=60_000)
        # Iframe-ene er kryssdomene; vi får ikke vite når de er ferdige.
        log.info("venter %.0f s på at iframe-ene skal bli ferdige", self.cfg.settle)
        time.sleep(self.cfg.settle)
        self.lastet = time.monotonic()

    def last_om_nodvendig(self) -> None:
        if self.cfg.reload_minutter <= 0:
            return
        if time.monotonic() - self.lastet >= self.cfg.reload_minutter * 60:
            self.last()

    def skjermbilde(self) -> bytes:
        return self.page.screenshot(type="png")

    def stopp(self) -> None:
        for lukk in (
            lambda: self._browser and self._browser.close(),
            lambda: self._pw and self._pw.stop(),
        ):
            try:
                lukk()
            except Exception:
                log.debug("feil under nedstenging av nettleser", exc_info=True)
        self._browser = self._pw = self.page = None


def en_runde(nettleser: Nettleser, cfg: Config, lut: list[int],
             runde: int, vis: bool) -> None:
    nettleser.last_om_nodvendig()

    png = nettleser.skjermbilde()
    bilde = lag_bilde(png, cfg, lut)
    # Pillow skriver "L"-bilder som 8-bits palett-BMP, bunn-opp og
    # ukomprimert. Det er nøyaktig formatet src/bmp.c krever.
    bilde.save(cfg.frame, "BMP")
    log.info("skrev %s (%dx%d)", cfg.frame, bilde.width, bilde.height)

    if not vis:
        return

    if cfg.init_clear_hver > 0 and runde % cfg.init_clear_hver == 0:
        log.info("full INIT-klaring for å skrubbe bort ghosting")
        kall_epaper(cfg, "clear", "--init")

    kall_epaper(cfg, "display", str(cfg.frame))


def main() -> int:
    her = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(description="Viser hjemmeskjerm-sida på e-paper.")
    p.add_argument("--config", type=Path, default=her.parent / "eink.toml")
    p.add_argument("--once", action="store_true", help="kjør én runde og avslutt")
    p.add_argument("--no-display", action="store_true",
                   help="skriv bare BMP-fila, ikke kall epaper (for testing uten skjerm)")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s",
        datefmt="%H:%M:%S",
    )

    cfg = Config(args.config)
    vis = not args.no_display
    if vis and not cfg.epaper.exists():
        log.error("fant ikke %s – har du kjørt make i eink/driver?", cfg.epaper)
        return 1

    signal.signal(signal.SIGTERM, _be_om_stopp)
    signal.signal(signal.SIGINT, _be_om_stopp)

    # Kort budsjett ved --once, langt i løkkemodus: der kan systemd ha startet
    # oss før vite rekker å lytte.
    if not vent_på_server(cfg.url, 5.0 if args.once else 180.0):
        port = urllib.parse.urlsplit(cfg.url).port or 80
        log.error("ingen svarer på %s", cfg.url)
        log.error("Nettsida må kjøre først. Start den med:")
        log.error("    cd %s && pnpm preview --port %d", cfg.webpage_dir, port)
        log.error("eller, hvis tjenesten er satt opp:")
        log.error("    sudo systemctl start hjemmeskjerm-web")
        return 1

    if vis:
        # info rører ikke panelet, men går gjennom hele SPI/GPIO-oppsettet.
        # Feiler den, er det ingen vits i å rendre først og oppdage det etterpå.
        try:
            log.debug("panel: %s", kall_epaper(cfg, "info").replace("\n", " "))
        except Exception as e:
            log.error("%s", e)
            return 1

    lut = bygg_lut(cfg.gamma, cfg.kontrast)
    nettleser = Nettleser(cfg)
    try:
        nettleser.start()
    except Exception as e:
        log.error("klarte ikke starte nettleseren: %s", e)
        log.debug("detaljer", exc_info=True)
        nettleser.stopp()
        return 1

    runde = 0
    feil_på_rad = 0
    try:
        while not _stopp:
            runde += 1
            try:
                en_runde(nettleser, cfg, lut, runde, vis)
                feil_på_rad = 0
            except Exception as e:
                feil_på_rad += 1
                # E-paper holder på bildet sitt uten strøm, så en tapt runde
                # er usynlig. Bedre en litt gammel skjerm enn en feilmelding
                # på veggen.
                log.error("runde %d feilet (%d på rad): %s", runde, feil_på_rad, e)
                log.debug("detaljer", exc_info=True)
                if feil_på_rad >= 3:
                    log.warning("starter nettleseren på nytt")
                    nettleser.stopp()
                    try:
                        nettleser.start()
                        feil_på_rad = 0
                    except Exception:
                        log.exception("klarte ikke starte nettleseren på nytt")
                        return 1

            if args.once or _stopp:
                break
            _sov(cfg.intervall)
    finally:
        nettleser.stopp()

    return 0


def _sov(sekunder: float) -> None:
    """Sover, men våkner med en gang vi er bedt om å stoppe."""
    slutt = time.monotonic() + sekunder
    while not _stopp and time.monotonic() < slutt:
        time.sleep(min(1.0, slutt - time.monotonic()))


if __name__ == "__main__":
    sys.exit(main())
