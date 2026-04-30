# PocketMind Firmware
## Pisces Moon OS — ESP32-S3 AI Companion Hardware

**Copyright (C) 2026 Eric Becker / Fluid Fortune**
**SPDX-License-Identifier: AGPL-3.0-or-later**

A branch of Pisces Moon OS for purpose-built ESP32-S3 AI companion devices.

---

## What This Is

PocketMind is a Pisces Moon OS port for dedicated AI companion hardware. Where the T-Deck Plus is a general-purpose field intelligence device, PocketMind targets are optimized for AI interaction specifically — voice input, AI response display, and Lighthouse integration.

PocketMind devices connect to The Lighthouse over WiFi and use your existing AI subscriptions (Claude Max, Gemini Advanced, etc.) at zero additional cost. No API tokens. No extra subscription. Full model with Projects and memory intact.

---

## Hardware Targets

### Waveshare ESP32-S3-Touch-LCD-4B (Active — Confirmed Booting)
- ST7701 480×480 RGB parallel display
- GT911 capacitive touch
- AXP2101 PMU, PCF85063 RTC, QMI8658 IMU
- 16MB Flash, 8MB PSRAM
- WiFiManager captive portal
- Keyboard + touch UI

### Waveshare ESP32-S3-Touch-LCD-2.8 / "HoneySod" (In Development)
- ST7789 SPI 240×320 display
- CST328 capacitive touch
- PCM5101 audio decoder + onboard MIC + speaker
- MicroSD, battery charging
- Voice-first PTT interface

### LilyGO T-Deck Plus (Stub — See Pisces Moon OS)
- Full implementation lives in the main Pisces Moon OS repo
- Stub target here for multi-target build compatibility

---

## Build

Requires PlatformIO.

```bash
# Waveshare 4B
pio run -e waveshare4b_pocketmind --target upload

# HoneySod
pio run -e honeysod_pocketmind --target upload

# T-Deck (stub)
pio run -e tdeck_pocketmind --target upload
```

---

## First Boot

1. Flash firmware
2. Device boots with BIOS-style boot screen
3. WiFiManager captive portal launches automatically
4. Connect phone to `PocketMind-Setup` / password `pocketmind`
5. Open `192.168.4.1` and enter your network credentials
6. Device connects and launches Lighthouse terminal

---

## Lighthouse Integration

PocketMind connects to a running Lighthouse instance over HTTP:

```cpp
#define LIGHTHOUSE_HOST  "192.168.1.100"  // Your Lighthouse server
#define LIGHTHOUSE_PORT  8000
```

Set these in `platformio.ini` build flags or `lighthouse_client.h`.

The Lighthouse server runs on any Linux machine (Debian XFCE recommended).
See: https://github.com/FluidFortune/the-lighthouse

---

## Architecture Notes

**SPI Bus Treaty** — all targets implement the Treaty for shared bus arbitration between display, SD card, and other SPI peripherals. See Pisces Moon OS documentation for full Treaty specification.

**Ghost Engine** — background intelligence collection runs on Core 0. UI and Lighthouse communication run on Core 1. The device is always collecting.

---

## Project Status

**Alpha — in active development.**

| Target | Boot | Display | Touch | WiFi | Lighthouse |
|--------|------|---------|-------|------|------------|
| Waveshare 4B | ✅ | ✅ | 🔧 calibrating | ✅ | 🔧 in progress |
| HoneySod | 🔧 | 🔧 | 🔧 | 🔧 | 🔧 |
| T-Deck Plus | ✅ | ✅ | ✅ | ✅ | ✅ |

---

## License

AGPL-3.0-or-later. All improvements come back to the community.
See CLA.md — same CLA as Pisces Moon OS parent project.

---

## Part of Pisces Moon OS

- **Pisces Moon OS** — https://github.com/FluidFortune/pisces-moon-os
- **The Lighthouse** — https://github.com/FluidFortune/the-lighthouse
- **PocketMind Firmware** — https://github.com/FluidFortune/pocketmind-firmware
- **Fluid Fortune** — https://fluidfortune.com
