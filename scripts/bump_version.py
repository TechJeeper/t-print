#!/usr/bin/env python3
"""Bump or set the project semver in VERSION and sync platformio.ini PP_VERSION."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
PLATFORMIO = ROOT / "platformio.ini"


def read_version() -> str:
    text = VERSION_FILE.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", text):
        raise SystemExit(f"Invalid VERSION contents: {text!r}")
    return text


def write_version(ver: str) -> None:
    VERSION_FILE.write_text(ver + "\n", encoding="utf-8")
    ini = PLATFORMIO.read_text(encoding="utf-8")
    updated, n = re.subn(
        r'(-DPP_VERSION=\\")(\d+\.\d+\.\d+)(\\")',
        rf"\g<1>{ver}\g<3>",
        ini,
    )
    if n != 1:
        raise SystemExit("Could not update -DPP_VERSION in platformio.ini")
    PLATFORMIO.write_text(updated, encoding="utf-8")
    print(f"version -> {ver}")


def bump(part: str) -> str:
    major, minor, patch = (int(x) for x in read_version().split("."))
    if part == "major":
        major, minor, patch = major + 1, 0, 0
    elif part == "minor":
        minor, patch = minor + 1, 0
    else:
        patch += 1
    return f"{major}.{minor}.{patch}"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--major", action="store_true")
    g.add_argument("--minor", action="store_true")
    g.add_argument("--patch", action="store_true", default=False)
    g.add_argument("--set", dest="set_ver", metavar="X.Y.Z")
    args = ap.parse_args()

    if args.set_ver:
        if not re.fullmatch(r"\d+\.\d+\.\d+", args.set_ver):
            raise SystemExit("--set requires MAJOR.MINOR.PATCH")
        ver = args.set_ver
    elif args.major:
        ver = bump("major")
    elif args.minor:
        ver = bump("minor")
    else:
        ver = bump("patch")

    write_version(ver)


if __name__ == "__main__":
    main()
