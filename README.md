# T-Print

Custom firmware for the **LilyGO T-Embed CC1101** — a pocket remote that monitors and controls **Klipper (Moonraker)**, **Bambu Lab (LAN MQTT)**, and **Prusa (PrusaLink)** printers.

Icons in the setup UI and web flasher come from [Iconify](https://icon-sets.iconify.design/) (`mdi` set).

## Features

- Encoder + 1.9" display UI for status, pause / resume / cancel / e-stop
- **On-device setup webapp** (SoftAP + LAN) for Wi‑Fi and printer onboarding
- Multi-printer profiles stored in NVS
- **Browser flasher** (`flasher/`) via ESP Web Tools
- Optional camera snapshots (Klipper webcam URL, PrusaLink snap, Bambu A1/P1 LAN JPEG)

## Device controls

| Input | Action |
|-------|--------|
| **Turn** encoder | Move selection / change detail page (1/3 → 2/3 → 3/3) |
| **Short click** encoder | Open printer / open Actions / confirm |
| **Long-press** encoder (~0.7s) | **Back** (detail → fleet list, actions → detail) |
| Actions → **Back** | Same as long-press from the Actions screen |

Hint on the status page: `Click actions · hold back`.

## Quick start (web flasher)

1. Build firmware (below) so `flasher/*.bin` is populated.
2. Serve the flasher folder over HTTP (Web Serial requires a secure context or localhost):

```bash
python -m http.server 8080 --directory flasher
```

3. Open http://localhost:8080 — click **Install firmware**.
4. Connect to Wi‑Fi **T-Print**, open http://192.168.4.1, join your home network, then reopen setup from the IP shown on the device.

Hosted flasher: https://techjeeper.github.io/t-print/

## Build

```bash
python -m pip install platformio
python scripts/fetch_iconify.py
python -m platformio run
```

Flash via USB:

```bash
python -m platformio run -t upload
```

## Setup webapp API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/status` | Device / Wi‑Fi status |
| POST | `/api/wifi/scan` | Start async Wi‑Fi scan |
| GET | `/api/wifi/scan` | Poll scan status / results |
| PUT | `/api/wifi` | Start join `{ssid,password}` |
| GET | `/api/wifi` | Poll join status / LAN IP |
| GET/POST | `/api/printers` | List / add |
| PUT/DELETE | `/api/printers/:id` | Update / remove |
| POST | `/api/printers/:id/test` | Connectivity test |
| POST | `/api/test-printer` | Test unsaved profile |
| POST | `/api/discover` | LAN printer discovery |
| PUT | `/api/device` | Name / brightness |
| POST | `/api/factory-reset` | Wipe + reboot |

After joining LAN: `http://t-print.local` (mDNS) or the device IP.

## Hardware

LilyGO T-Embed CC1101 (ESP32-S3, 16MB flash, 8MB PSRAM, ST7789 170×320, rotary encoder). CC1101 Sub‑GHz is unused for printer control.

## License

MIT
