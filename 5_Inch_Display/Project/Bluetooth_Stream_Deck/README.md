<div align="center">

<img src="https://visitor-badge.laobi.icu/badge?page_id=papercodeIN.Viewe_Display.5_Inch_Display.Bluetooth_Stream_Deck" alt="Project visitors" height="20" />

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

# Bluetooth Stream Deck

A Bluetooth LE HID keyboard Stream Deck for the VIEWE UEDX80480050E-WB 5-inch ESP32-S3 display (800x480, capacitive touch). The touchscreen shows a grid of tiles, and each tap sends a BLE HID media key or keyboard shortcut directly to the paired computer. No WiFi connection or companion server is needed.

**Status — in development.** The firmware is a port of the working [CrowPanel 7" Bluetooth Stream Deck](https://github.com/papercodeIN/Elecrow/tree/main/Project/Bluetooth_Stream_Deck) in the [Elecrow repository](https://github.com/papercodeIN/Elecrow). Both displays share the same recipe — ESP32-S3, 800x480 RGB panel, GT911 capacitive touch — so the port is mostly pin and board configuration. The sections below describe the finished design and the VIEWE-specific hardware details.

## What You Need

- VIEWE UEDX80480050E-WB 5-inch ESP32-S3 (N16R8: 16 MB flash + 8 MB PSRAM) with capacitive touch
- USB-C data cable connected to the CH340C port for flashing
- A Windows, macOS, or Linux computer with Bluetooth
- PlatformIO Core or the PlatformIO IDE extension for VS Code

Ported from and tested against:

- PlatformIO `espressif32@6.9.0`
- LVGL `8.4.0`
- GFX Library for Arduino `1.4.7`
- T-vK/ESP32 BLE Keyboard `0.3.2`

## Features

- 800x480 LVGL interface in landscape orientation
- 4x4 tile grid (16 tiles per page)
- BLE HID media keys and keyboard shortcuts sent straight to the paired computer
- Stable BLE bonding: factory Bluetooth address, bonds persist across reboots
- Touch input through the GT911 controller
- Backlight idle dimming after one minute without touch

## Tile Actions

The default shortcuts use standard keyboard and media-key reports. Some application-specific actions may differ by operating system or active app.

| Tile | Command sent |
|---|---|
| Mute, Play/Pause, Next, Vol Down, Vol Up | Standard media keys |
| Mic | Ctrl+D (app-dependent microphone mute shortcut) |
| Esc | Escape |
| Snip | Windows+Shift+S |
| Copy / Paste | Ctrl+C / Ctrl+V |
| Desktop | Windows+D |
| Terminal | Windows+R, type `wt`, press Enter |
| Lock | Windows+L |
| Start | Windows key |
| Alt+Tab / Ctrl+Tab | Standard keyboard shortcuts |

The mapping lives in `send_action()` in `src/main.cpp`. Commands like Snip, Desktop, Terminal, and Lock use Windows shortcuts; change these mappings if using another operating system. Media keys work where supported by the computer and application.

The screen dims its backlight after one minute without a touch and restores it on the next touch.

## Pairing

The deck uses the ESP32's factory Bluetooth address, which never changes, and it never deletes its pairing keys. Both sides keep the bond, so reconnects after a reboot are automatic and stable.

After flashing, pair once:

1. In Windows **Settings → Bluetooth & devices**, remove **every** **BT Stream Deck** entry (older pairings from other decks can interfere).
2. Restart the display so it advertises again.
3. Choose **Add device**, select **BT Stream Deck**, and complete pairing (Just Works — no passkey prompt).
4. Tap a tile to send its command.

After this one-time cleanup, the pairing survives reboots on both sides. If pairing is ever lost (for example after erasing the flash), remove the device in Windows and pair again.

## Build and Flash

This will be a PlatformIO project. Build and upload steps are identical to the [CrowPanel 7" Bluetooth Stream Deck](https://github.com/papercodeIN/Elecrow/tree/main/Project/Bluetooth_Stream_Deck): open the project folder in VS Code with PlatformIO, build the environment, and upload to the display's CH340/UART port. The final environment name and board definition will be documented here once the port lands.

If upload fails, hold the **BOOT** button while connecting or resetting, then upload again.

## How It Works

The firmware drives the 800x480 RGB panel through an LVGL interface, reads touch positions from the GT911 over I2C, and sends BLE HID keyboard/consumer reports to the paired host over the ESP32-S3 Bluetooth stack. Wi-Fi is unused, so the radio is dedicated to Bluetooth.

The touch controller shares GPIO19/20 with the ESP32-S3's native USB pins. Firmware therefore keeps USB CDC disabled, and flashing/serial output runs over the CH340C UART port at 115200 baud.

## Project Structure

```text
src/main.cpp      Display, touch input, UI, and Bluetooth keyboard actions
platformio.ini    PlatformIO board, build, and library settings
boards/           ESP32-S3 board definition for the 16 MB flash / OPI PSRAM variant
include/          LVGL configuration
tests/            Host-side checks for the tile action mapping
flash.ps1         Windows build-and-upload helper
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| No COM port | Try another USB-C cable that supports data; connect to the CH340C port. |
| Upload fails | Close serial monitors using the port, then hold **BOOT** while retrying. |
| Blank screen | Confirm the board definition uses 16 MB flash / OPI PSRAM settings. |
| Touch stays dead | Check the GT911 wiring notes (SDA GPIO19, SCL GPIO20, RST GPIO38) in the [board README](<../../README.md>). |
| BT Stream Deck does not appear | Confirm the display is powered on, wait a few seconds, then scan again. |
| Connect/disconnect cycling in Windows | Remove every **BT Stream Deck** entry in Windows Bluetooth settings, restart the display, and pair once — stale bond keys from older builds cause this loop. |
| Pairing lost after erasing flash | Erasing the flash deletes the ESP32-side bond; remove the device in Windows and pair again. |

## Safety and Security

- The BLE keyboard uses plain bonding without MITM (Just Works). The display has no passkey screen/keypad, so MITM/passkey pairing is not supported.
- The deck always uses the ESP32's factory public Bluetooth address and never erases its bonds, so pairing is stable across reboots.
- A paired computer can type arbitrary keys through the deck. Only pair with a computer you control, and remove the pairing when the display is not in use.
- This firmware is BLE-only: it does not use WiFi and does not include a companion server.

## Notes

- The board macro for the manufacturer's library stack is `BOARD_VIEWE_UEDX80480050E_WB_A` in `esp_panel_board_supported_conf.h`.
- Arduino IDE users should select the ESP32S3 Dev Module with 16 MB flash, OPI PSRAM, and USB CDC On Boot disabled — see the [board README](<../../README.md>) for the full reference configuration.
- Vendor socials and full contact details are in the [board README](<../../README.md>).
