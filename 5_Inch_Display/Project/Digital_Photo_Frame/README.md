<div align="center">

<img src="https://visitor-badge.laobi.icu/badge?page_id=papercodeIN.Viewe_Display.5_Inch_Display.Digital_Photo_Frame" alt="Project visitors" height="20" />

<p align="center">
  <strong>Support</strong><br>
  If these projects help you, connect with us on social media:
</p>


[![YouTube](https://img.shields.io/badge/YouTube-Fusion_Automate-FF0000?logo=youtube&logoColor=white)](https://www.youtube.com/channel/UCKKhdFV0q8CV5vWUDfiDfTw)
[![Telegram](https://img.shields.io/badge/Telegram-%40fusionautomate-26A5E4?logo=telegram&logoColor=white)](https://t.me/fusionautomate)
[![Instagram](https://img.shields.io/badge/Instagram-%40fusion__automate-E4405F?logo=instagram&logoColor=white)](https://www.instagram.com/fusion_automate)
[![WhatsApp](https://img.shields.io/badge/WhatsApp-%2B91--9974477759-25D366?logo=whatsapp&logoColor=white)](https://wa.me/919974477759)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy_Me_a_Coffee-support-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/pylin)
</div>

# Digital Photo Frame

A Wi-Fi digital photo frame for the VIEWE UEDX80480050E-WB 5-inch ESP32-S3 display (800x480, IPS RGB). The firmware serves a web dashboard on the local network: upload JPEG/PNG/BMP photos from a phone or laptop, arrange the slideshow, pick transitions, and control playback remotely. Photos are stored on the microSD card and shown as a full-screen slideshow with animated transitions.

> [!IMPORTANT]
> **Not tested on the display yet.** This firmware has never been flashed to a physical VIEWE 5-inch panel. Verify display output, SD mounting, and uploads on your board when you first run it.

**Status — ported; PlatformIO build passes.** This is a port of the working [CrowPanel 7" Digital Photo Frame](https://github.com/papercodeIN/Elecrow/tree/main/CrowPanel%20-%20ESP32%20HMI%20Display%20-%207%20Inch/Project/Digital_Photo_Frame); the web dashboard, slideshow engine, transitions, upload pipeline, and settings are unchanged. The display configuration is the hardware-tested VIEWE 5-inch setup from the [Bluetooth Stream Deck](<../Bluetooth_Stream_Deck/README.md>) project in this repository (same panel recipe: ST7262 RGB bus, 15 MHz PCLK, PSRAM framebuffer), and the SD pin map was verified against the official [VIEWE schematic](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display).

## What You Need

- VIEWE UEDX80480050E-WB 5-inch ESP32-S3 (N16R8: 16 MB flash + 8 MB PSRAM)
- A FAT32-formatted microSD card (photos live in a `/photos` folder on it)
- USB-C data cable connected to the CH340C port for flashing
- PlatformIO Core or the PlatformIO IDE extension for VS Code
- A phone or computer on the same network for the web dashboard

Ported from and tested against:

- PlatformIO `espressif32@6.9.0`
- GFX Library for Arduino `1.4.7` (hardware-tested on this panel)
- TJpg_Decoder `1.1.0` (baseline JPEG)
- pngle `1.1.0` (PNG)
- ESPAsyncWebServer `3.6.0` / AsyncTCP `3.3.2`

## Features

- 800x480 full-screen slideshow from the SD card's `/photos` folder
- Web dashboard served by the frame: upload, gallery, delete, delete-all
- 12 slideshow transitions (wipe, blinds, curtain, box, checkerboard, fade) plus Auto mode that picks a random effect per slide
- Playback controls: next, previous, pause, show a specific photo
- Settings persisted in NVS: slide interval (1–3600 s), sequential/shuffle order, fit mode (letterbox / crop-fill / auto), transition, brightness
- Wi-Fi provisioning: connect to the `PhotoFrame-Setup` access point on first boot, or set default credentials in `include/secrets.h`
- OTA firmware updates over Wi-Fi (the 16 MB flash carries two OTA app slots)
- SD hot-plug: the card is polled every second and the slideshow recovers on reinsert
- Uploads are buffered in PSRAM, then written to the SD card in one shot

## SD Card Setup

The microSD slot sits on SPI and was verified against the official VIEWE schematic (TF section): **CS GPIO10, MOSI GPIO11, SCK GPIO12, MISO GPIO13** — the board already provides 10K pull-ups on the bus.

1. Format the card FAT32.
2. Create a `photos` folder in the root (the firmware also creates `/photos` if missing).
3. Copy JPEG/PNG/BMP files onto it — or just upload everything from the dashboard later.

Progressive JPEGs cannot be decoded by the on-device decoder; the dashboard's auto-convert normalizes phone/WhatsApp photos to baseline JPEG before uploading.

## Build and Flash

This is a PlatformIO project — environment `viewe_5`, with a vendored board definition in `boards/` for the 16 MB flash / OPI PSRAM variant.

### Option 1: PlatformIO Core

Build from the project folder:

```sh
python -m platformio run -e viewe_5
```

Upload to the display, replacing `COM5` with the CH340C port:

```sh
python -m platformio run -e viewe_5 -t upload --upload-port COM5
```

Or run `./flash.ps1`; it builds the firmware and asks which COM port to use.

### Option 2: VS Code

1. Install the PlatformIO IDE extension.
2. Open this folder in VS Code.
3. Use **PlatformIO > Build** to compile, **PlatformIO > Upload** to flash.

### First Boot

1. Insert the SD card and power the display.
2. Optionally copy `include/secrets.h.example` to `include/secrets.h` and set `DEFAULT_SSID`/`DEFAULT_PASS` before building to skip the setup portal. (This file holds credentials and is git-ignored.)
3. Without saved credentials the frame starts the `PhotoFrame-Setup` access point; connect and open `http://192.168.4.1` (the screen shows the address).
4. On your own Wi-Fi, open the IP shown on the frame's screen to reach the dashboard.

### If Upload Fails

Close serial monitors and other programs using the COM port, then hold the **BOOT** button while connecting or resetting and upload again.

Use the CH340/UART port, not the native USB port. The native USB pins are shared with the touch controller on this display. The serial monitor uses **115200** baud.

## How It Works

The firmware drives the 800x480 RGB panel through an Arduino_GFX `Arduino_RGB_Display` PSRAM framebuffer at 15 MHz PCLK (see the [board README](<../../README.md>) for why the PCLK is lowered), decodes photos from the SD card with TJpg_Decoder (JPEG), pngle (PNG), and a native streaming BMP decoder, and serves the dashboard with ESPAsyncWebServer. A mutex guards the SPI bus between the slideshow, the SD health poll, and upload writes. Settings and Wi-Fi credentials persist in NVS.

Touch input is not part of the photo frame — control happens through the web dashboard, exactly as in the CrowPanel 7" original.

## Project Structure

```text
src/main.cpp         Slideshow engine, Wi-Fi/AP portal, OTA, web server routes
include/config.h     SD/panel/backlight pins, slideshow defaults, timings
include/display_5inch.h  VIEWE 5-inch Arduino_GFX panel config + compat helpers
include/image_decoder.h  JPEG/PNG/BMP decoders with fit modes
include/index_html.h     Embedded web dashboard UI
include/secrets.h.example Optional default Wi-Fi credentials template
boards/              ESP32-S3 board definition for the 16 MB flash / OPI PSRAM variant
flash.ps1            Windows build-and-upload helper
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| No COM port | Try another USB-C cable that supports data; connect to the CH340C port. |
| Upload fails | Close serial monitors using the port, then hold **BOOT** while retrying. |
| Blank screen | Confirm the board definition uses 16 MB flash / OPI PSRAM settings. |
| Screen glitches / drifts constantly | The scanout clock is tuned to 15 MHz because Arduino_GFX has no DMA bounce buffers (the vendor's 20 MHz ESP-IDF config assumes them). If it ever recurs, lower `RGB_PCLK_HZ` in `include/config.h` (e.g. `12000000`) and reflash. |
| SD card not detected | Check the card is FAT32; reseat it. The frame retries mounting every second and shows the retry count on screen. |
| Photo shows "Unsupported" | The file is a progressive JPEG or corrupted — re-upload it through the dashboard with auto-convert enabled. |
| Dashboard unreachable | On first boot join the `PhotoFrame-Setup` AP; otherwise check the frame's screen for the current IP, or use the router's client list (`esp32-photoframe` hostname). |

## Safety and Security

- The dashboard and all APIs are unauthenticated on the local network — like the original, this is designed for a trusted home LAN.
- The optional `PhotoFrame-Setup` access point is open while the frame waits for Wi-Fi credentials.
- OTA updates are unauthenticated unless you set `OTA_PASSWORD` in `include/secrets.h`.
- Uploaded filenames are sanitized server-side (path traversal, hidden files, and reserved names are rejected).

## Notes

- The board macro for the manufacturer's library stack is `BOARD_VIEWE_UEDX80480050E_WB_A` in `esp_panel_board_supported_conf.h` — this project drives the panel directly with Arduino_GFX instead and does not need it.
- Arduino IDE users should select the ESP32S3 Dev Module with 16 MB flash, OPI PSRAM, and USB CDC On Boot disabled — see the [board README](<../../README.md>) for the full reference configuration.
- The 7-inch CrowPanel original keeps its own project folder in the [Elecrow repository](https://github.com/papercodeIN/Elecrow), including a production-readiness audit of the shared firmware logic.
- Vendor socials and full contact details are in the [board README](<../../README.md>).
