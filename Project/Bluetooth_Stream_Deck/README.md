# Bluetooth Stream Deck

A Bluetooth LE HID keyboard Stream Deck for the VIEWE UEDX80480050E-WB 5-inch ESP32-S3 display (800x480 IPS, capacitive touch). Tap on-screen tiles to send keyboard shortcuts and media keys directly to the paired computer — no WiFi connection or companion server needed.

> **Status — in development.** The firmware is a port of the working [CrowPanel 7" Bluetooth Stream Deck](https://github.com/papercodeIN/Elecrow/tree/main/Project/Bluetooth_Stream_Deck) in the [Elecrow repository](https://github.com/papercodeIN/Elecrow). Both displays share the same recipe — ESP32-S3, 800x480 RGB panel, GT911 capacitive touch — so the port is mostly pin and board configuration. The sections below describe the finished design and the VIEWE-specific hardware details.

## What you need

- VIEWE UEDX80480050E-WB 5-inch ESP32-S3 (N16R8: 16 MB flash + 8 MB PSRAM) with capacitive touch
- USB-C data cable connected to the CH340C port for flashing
- A Windows, macOS, or Linux computer with Bluetooth
- VS Code and the PlatformIO IDE extension

## Hardware notes (VIEWE board)

| Item | Value |
|---|---|
| Panel | 5" IPS, 800x480, 40-pin RGB 24-bit (RGB565), driver ST7262/ST72611 |
| Touch | GT911 capacitive over I2C — SDA GPIO19, SCL GPIO20, RST GPIO38 |
| MCU | ESP32-S3-N16R8, dual-core LX7 @ 240 MHz |
| Flash / PSRAM | 16 MB flash / 8 MB OPI PSRAM |
| USB | Type-C through CH340C — power, flashing and serial |
| Boot / Reset | BOOT (GPIO0) and RESET buttons |

Like the CrowPanel, the touch controller shares GPIO19/20 with the ESP32-S3's native USB pins, so firmware keeps USB CDC disabled and flashing/serial runs over the CH340C UART port. When flashing through the Arduino IDE instead, set **USB CDC On Boot: Disabled** and use the 16 MB flash + OPI PSRAM board options.

## Vendor resources

- Product page: [ESP32 5 Inch 800×480 RGB IPS TFT Display Touch Screen](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/)
- Official GitHub repository — datasheets, schematics, 2D drawings, examples and factory firmware: [VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display)
- Display Wiki — display technology docs and tutorials: [display-wiki.com](https://www.display-wiki.com/)
- Manufacturer libraries (Arduino): `ESP32_Display_Panel` 1.0.3+ by Espressif (installs `ESP32_IO_Expander` and `esp-lib-utils`), LVGL 8.4.0, esp32 core 3.0.0+
- Board macro for the vendor library: `#define BOARD_VIEWE_UEDX80480050E_WB_A` in `esp_panel_board_supported_conf.h`

## Pair with your computer

The deck uses the ESP32's factory Bluetooth address, which never changes, and it never deletes its pairing keys. Both sides keep the bond, so reconnects after a reboot are automatic and stable.

After flashing, pair once:

1. In Windows **Settings → Bluetooth & devices**, remove **every** **BT Stream Deck** entry (older pairings from other decks can interfere).
2. Restart the display so it advertises again.
3. Choose **Add device**, select **BT Stream Deck**, and complete pairing (Just Works — no passkey prompt).
4. Tap a tile to send its command.

If pairing ever gets lost (for example after erasing the flash), remove the device in Windows and pair again.

## Tile actions

The default layout is the same 4x4 tile grid as the CrowPanel build, using standard keyboard and media-key reports. Some application-specific actions may differ by operating system or active app.

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

The mapping lives in `send_action()` in `src/main.cpp`. Commands like Snip, Desktop, Terminal, and Lock use Windows shortcuts; change these mappings if using another operating system. The screen dims its backlight after one minute without a touch and restores it on the next touch.

## Limitations and security

- This firmware is BLE-only: it does not use WiFi and does not include a companion server.
- The BLE keyboard uses plain bonding without MITM (Just Works). The display has no passkey screen/keypad, so MITM/passkey pairing is not supported.
- The deck always uses the ESP32's factory public Bluetooth address and never erases its bonds, so pairing is stable across reboots. Erasing the whole flash deletes the ESP32-side bond; remove the device in Windows and pair again afterwards.
- A paired computer can type arbitrary keys through the deck. Only pair with a computer you control, and remove the pairing when the display is not in use.

## Project files

- `src/main.cpp` — screen, touch input, UI, and Bluetooth keyboard actions *(port in progress)*
- `platformio.ini` — PlatformIO board, build, and library settings
- `boards/` — ESP32-S3 board definition for the 16 MB flash / OPI PSRAM variant
- `include/lv_conf.h` — LVGL configuration
- `tests/` — host-side checks for the tile action mapping
- `flash.ps1` — Windows build-and-upload helper

## Support

If this project helps you, connect with us:

[![YouTube](https://img.shields.io/badge/YouTube-Fusion_Automate-FF0000?logo=youtube&logoColor=white)](https://www.youtube.com/channel/UCKKhdFV0q8CV5vWUDfiDfTw)
[![Telegram](https://img.shields.io/badge/Telegram-%40fusionautomate-26A5E4?logo=telegram&logoColor=white)](https://t.me/fusionautomate)
[![Instagram](https://img.shields.io/badge/Instagram-%40fusion__automate-E4405F?logo=instagram&logoColor=white)](https://www.instagram.com/fusion_automate)
[![WhatsApp](https://img.shields.io/badge/WhatsApp-%2B91--9974477759-25D366?logo=whatsapp&logoColor=white)](https://wa.me/919974477759)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy_Me_a_Coffee-support-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/pylin)

Vendor socials and full contact details are in the [repository README](<../../README.md>).
