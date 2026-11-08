#!/usr/bin/env python3
"""Fetch Iconify web component + offline mdi icon subset.
Source: https://icon-sets.iconify.design/mdi/"""

from __future__ import annotations

import json
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
DATA.mkdir(exist_ok=True)

ICONIFY_URLS = [
    "https://cdn.jsdelivr.net/npm/iconify-icon@2.1.0/dist/iconify-icon.min.js",
    "https://unpkg.com/iconify-icon@2.1.0/dist/iconify-icon.min.js",
]

ICONS = [
    "printer-3d",
    "printer-3d-nozzle",
    "printer-3d-nozzle-heat",
    "printer-outline",
    "printer-pos",
    "wifi",
    "wifi-lock",
    "wifi-check",
    "wifi-settings",
    "access-point",
    "radar",
    "check-network",
    "cog",
    "plus",
    "pencil",
    "trash-can-outline",
    "connection",
    "content-save",
    "factory",
    "cellphone-cog",
    "usb-port",
    "flash",
    "download",
    "check-circle",
    "alert-circle",
    "bell-ring",
    "bell-off",
    "tune-vertical",
    "close",
    "wifi-off",
    "cable-data",
    "monitor-screenshot",
    "cctv",
    "camera",
]


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "t-print-build/1.0"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


def main() -> None:
    js = None
    for url in ICONIFY_URLS:
        try:
            js = fetch(url)
            print(f"iconify-icon from {url} ({len(js)} bytes)")
            break
        except Exception as e:
            print(f"fail {url}: {e}")
    if not js:
        raise SystemExit("Could not download iconify-icon")
    (DATA / "iconify-icon.min.js").write_bytes(js)

    api = "https://api.iconify.design/mdi.json?icons=" + ",".join(ICONS)
    coll = json.loads(fetch(api).decode("utf-8"))
    coll.setdefault("prefix", "mdi")
    print(f"mdi icons: {len(coll.get('icons', {}))}")

    payload = json.dumps(coll, separators=(",", ":"))
    icons_js = f"""/* Offline Iconify collection from https://icon-sets.iconify.design/mdi/ */
(function(){{
  var collection = {payload};
  function add(){{
    try {{
      if (window.Iconify && Iconify.addCollection) Iconify.addCollection(collection);
    }} catch (e) {{}}
  }}
  add();
  document.addEventListener('DOMContentLoaded', add);
  setTimeout(add, 50);
  setTimeout(add, 500);
}})();
"""
    (DATA / "icons.js").write_text(icons_js, encoding="utf-8")
    print("Wrote data/icons.js")


if __name__ == "__main__":
    main()
