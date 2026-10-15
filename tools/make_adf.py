#!/usr/bin/env python3
"""Build a bootable FoxysRun.adf with exe2adf.

  python tools/make_adf.py            # build\\sunnyland.exe + data.pak
  python tools/make_adf.py debug      # build-debug\\
  python tools/make_adf.py file       # build-file\\

The floppy boots sunnyland.exe. data.pak sits in the disk root, which is
where the game opens it.
"""
from __future__ import annotations

import io
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_BUILD_DIRS = {
    "release": "build",
    "debug": "build-debug",
    "serial": "build-serial",
    "ace": "build-ace",
    "lite": "build-serial-lite",
    "file": "build-file",
}
_ARG = sys.argv[1].lower() if len(sys.argv) > 1 else "release"
if _ARG not in _BUILD_DIRS:
    raise SystemExit(f"unknown adf mode {_ARG!r}; expected {', '.join(_BUILD_DIRS)}")

MODE = _ARG
BUILD = ROOT / _BUILD_DIRS[MODE]
EXE_SRC = BUILD / "sunnyland.exe"
PAK_SRC = BUILD / "data.pak"
if not PAK_SRC.is_file():
    PAK_SRC = ROOT / "dist" / "data.pak"
STAGE = BUILD / "adf_stage"
OUT_ADF = BUILD / "FoxysRun.adf"
EXE_NAME = "sunnyland.exe"
VOLUME = "FoxysRun"
ADF_NAME = "FoxysRun.adf"

EXE2ADF = ROOT / "tools" / "exe2adf.exe"
EXE2ADF_URL = "https://www.exe2adf.com/downloads/exe2adf-v03e-windows.zip"


def ensure_exe2adf() -> Path:
    if EXE2ADF.is_file():
        return EXE2ADF
    print(f"Downloading {EXE2ADF_URL}")
    req = urllib.request.Request(
        EXE2ADF_URL, headers={"User-Agent": "foxys-run-build-adf"}
    )
    try:
        with urllib.request.urlopen(req) as resp:
            payload = resp.read()
    except OSError as exc:
        raise SystemExit(f"could not download exe2adf: {exc}") from exc
    EXE2ADF.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(payload)) as zf:
        name = next(
            (n for n in zf.namelist() if Path(n).name.lower() == "exe2adf.exe"),
            None,
        )
        if name is None:
            raise SystemExit("exe2adf zip has no exe2adf.exe")
        EXE2ADF.write_bytes(zf.read(name))
    print(f"Saved {EXE2ADF}")
    return EXE2ADF


def stage_payload() -> None:
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)
    shutil.copy2(PAK_SRC, STAGE / "data.pak")
    print(f"Staged data.pak ({PAK_SRC.stat().st_size / 1024:.1f} KB)")
    print(f"  + {EXE_NAME} {EXE_SRC.stat().st_size / 1024:.1f} KB")


def run_exe2adf(exe2adf: Path) -> None:
    if OUT_ADF.exists():
        OUT_ADF.unlink()
    cmd = [
        str(exe2adf),
        "-i",
        EXE_NAME,
        "-l",
        VOLUME,
        "-a",
        ADF_NAME,
        "-d",
        STAGE.name,
        "-k",
    ]
    print("Running:", " ".join(cmd), f"(cwd={BUILD})")
    r = subprocess.run(cmd, cwd=str(BUILD), capture_output=True, text=True)
    sys.stdout.write(r.stdout or "")
    sys.stderr.write(r.stderr or "")
    combined = ((r.stdout or "") + (r.stderr or "")).lower()
    if r.returncode != 0:
        raise SystemExit(f"exe2adf failed ({r.returncode})")
    if "no free space" in combined:
        raise SystemExit("ADF full — the exe and data.pak do not fit one floppy")
    if not OUT_ADF.is_file():
        raise SystemExit(f"exe2adf reported OK but {OUT_ADF} missing")
    print(f"ADF: {OUT_ADF} ({OUT_ADF.stat().st_size / 1024:.1f} KB)")


def main() -> None:
    if not EXE_SRC.is_file():
        raise SystemExit(f"missing {EXE_SRC} — run build.bat first")
    if not PAK_SRC.is_file():
        raise SystemExit(f"missing {PAK_SRC}")
    stage_payload()
    run_exe2adf(ensure_exe2adf())


if __name__ == "__main__":
    main()
