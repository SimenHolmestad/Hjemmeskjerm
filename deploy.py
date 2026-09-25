#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PI_HOST = os.environ.get("PI_HOST", "simen@10.0.0.45")
PI_DEST = os.environ.get("PI_DEST", "/home/simen/Hjemmeskjerm")

PI_PNPM = os.environ.get("PI_PNPM", "/home/simen/.local/share/pnpm/bin/pnpm")

ROOT = Path(__file__).resolve().parent


def run_command_locally(*command: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=ROOT, check=check)


def run_ssh_command(*command: str, check: bool = True) -> subprocess.CompletedProcess:
    return run_command_locally("ssh", PI_HOST, *command, check=check)


def copy_code_to_raspberry_pi() -> None:
    git_filenames = subprocess.run( ["git", "ls-files"], cwd=ROOT, check=True, capture_output=True, text=True).stdout  # Kun filer i git

    with tempfile.NamedTemporaryFile("w", suffix=".txt") as list_file:
        list_file.write(git_filenames)
        list_file.flush()
        run_command_locally(
            "rsync", "-a", f"--files-from={list_file.name}", "./", f"{PI_HOST}:{PI_DEST}/"
        )

    # This gitignored file should be copied
    run_command_locally("rsync", "-a", "webpage/.env.local", f"{PI_HOST}:{PI_DEST}/webpage/.env.local")


def update_systemd_unit_files() -> None:
    """Kopierer unit-filene bare når de faktisk er endret."""
    changed = False
    for unit_file in sorted((ROOT / "systemd").glob("*.service")):
        source = f"{PI_DEST}/systemd/{unit_file.name}"
        target = f"/etc/systemd/system/{unit_file.name}"
        if run_ssh_command("cmp", "-s", source, target, check=False).returncode == 0:
            continue
        print(f"    {unit_file.name} er endret")
        run_ssh_command("sudo", "cp", source, target)
        changed = True

    if changed:
        run_ssh_command("sudo", "systemctl", "daemon-reload")


def build_web() -> None:
    if run_ssh_command("test", "-x", PI_PNPM, check=False).returncode != 0:
        sys.exit(f"fant ikke pnpm på {PI_PNPM} – sett PI_PNPM til riktig sti")

    run_ssh_command(
        f"cd {PI_DEST}/webpage && {PI_PNPM} install --frozen-lockfile && {PI_PNPM} build"
    )


def build_eink_renderer() -> None:
    run_ssh_command(f"cd {PI_DEST}/eink/driver && make")
    run_ssh_command(f"cd {PI_DEST}/eink/render && .venv/bin/pip install --quiet -r requirements.txt")


def check_status() -> None:
    time.sleep(5)
    status = run_ssh_command(
        "systemctl", "is-active", "hjemmeskjerm-web", "hjemmeskjerm-eink", check=False
    )
    print("==> Output fra journalctl på hjemmeskjerm-eink")
    run_ssh_command("journalctl", "-u", "hjemmeskjerm-eink", "-n", "15", "--no-pager")
    print("==> Output fra journalctl på hjemmeskjerm-web")
    run_ssh_command("journalctl", "-u", "hjemmeskjerm-web", "-n", "15", "--no-pager")
    if status.returncode != 0:
        sys.exit("en av tjenestene kjører ikke – se loggen over")


def full_deploy():
    print("==> Stopper render-prosessen på E-ink-skjermen")
    run_ssh_command("sudo", "systemctl", "stop", "hjemmeskjerm-eink")

    print("==> Clearer E-ink-skjermen")
    run_ssh_command("sudo", "~/Hjemmeskjerm/eink/driver/epaper", "clear")

    print("==> Kopierer koden til Raspbery PI")
    copy_code_to_raspberry_pi()

    print("==> Oppdaterer Unit-filene til Systemd")
    update_systemd_unit_files()

    print("==> Bygger nettsiden (pnpm)")
    build_web()

    print("==> Bygger eink-render-kode (C og Python)")
    build_eink_renderer()

    print("==> (re)Starter systemd-tjenestene")
    run_ssh_command("sudo", "systemctl", "restart", "hjemmeskjerm-web")
    run_ssh_command("sudo", "systemctl", "start", "hjemmeskjerm-eink")

    print("==> Sjekker status på systemd-tjenestene")
    check_status()


def deploy_web_only():
    print("==> Kopierer koden til Raspbery PI")
    copy_code_to_raspberry_pi()

    print("==> Bygger nettsiden (pnpm)")
    build_web()

    print("==> Restarter hjemmeskjerm-web (systemd-tjenesten som kjører nettsiden)")
    run_ssh_command("sudo", "systemctl", "restart", "hjemmeskjerm-web")


def main() -> None:
    parser = argparse.ArgumentParser(description="Deployer hjemmeskjerm til Raspberry Pi-en.")
    parser.add_argument("--web-only", action="store_true",
                        help="bygg og restart bare nettsida")
    args = parser.parse_args()

    # Gitignorert, men Netatmo-oppsettet på Pi-en trenger den.
    if not (ROOT / "webpage" / ".env.local").exists():
        sys.exit("fant ikke webpage/.env.local – se webpage/README.md om Netatmo")

    if args.web_only:
        deploy_web_only()
    else:
        full_deploy()


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(f"feilet: {' '.join(error.cmd)}")
