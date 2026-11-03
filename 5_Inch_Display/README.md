<div align="center">

<img src="https://visitor-badge.laobi.icu/badge?page_id=papercodeIN.Viewe_Display.5_Inch_Display" alt="5-inch display page visitors" height="20" />

# VIEWE 5 Inch Display

**ESP32-S3 800×480 smart display: setup notes, projects, and project status**

[Official GitHub](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display) · [Product page](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/) · [Display Wiki](https://www.display-wiki.com/)

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

---

## Overview

This folder contains notes and projects for the VIEWE UEDX80480050E-WB 5-inch ESP32-S3 smart display (800×480 IPS, GT911 capacitive touch, Wi-Fi and Bluetooth 5 LE). Project readiness varies: check the individual project README before building or flashing.

## Hardware reference

| Component | Detail |
|---|---|
| Model | UEDX80480050E-WB-A (WB-B is the newer hardware revision; no software difference) |
| Display | 5" IPS, 800×480, 40-pin RGB 24-bit (RGB565), driver ST7262/ST72611 |
| Touch | Multi-point capacitive (CTP), GT911 over I2C — SDA GPIO19, SCL GPIO20, RST GPIO38 |
| MCU | ESP32-S3-N16R8, dual-core LX7 @ 240 MHz |
| Memory | 520 KB SRAM · 8 MB PSRAM (OPI) · 16 MB Flash |
| Wireless | 2.4 GHz Wi-Fi 802.11 b/g/n, Bluetooth 5 (LE), BLE Mesh |
| USB | Type-C via CH340C — power, flashing, and serial |
| Extras | Micro SD card slot (SPI) — CS GPIO10, MOSI GPIO11, SCK GPIO12, MISO GPIO13 with on-board 10K pull-ups (verified from the official schematic), WS2812B RGB LED, BOOT (GPIO0) / RESET buttons |
| Power | 5 V, 150 mA, −20 ~ 70 ℃ operating |

> The touch controller shares GPIO19/20 with the ESP32-S3's native USB pins, so firmware keeps USB CDC disabled and flashing/serial runs over the CH340C UART port.

## Tested configuration reference

The following versions and Arduino IDE settings are from the manufacturer's documented setup for this board. Library and board-core compatibility can vary; consult the project README if a specific project requires a different configuration.

| Component | Documented version |
|---|---:|
| ESP32 Arduino core | 3.0.0+ |
| ESP32_Display_Panel | 1.0.3+ (installs ESP32_IO_Expander and esp-lib-utils) |
| LVGL | 8.4.0 |

| Arduino IDE setting | Documented value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash size | 16MB (128Mb) |
| Partition scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| PSRAM | OPI PSRAM |
| USB CDC On Boot | Disabled |

Board macro: set `ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED` to `1` and uncomment `#define BOARD_VIEWE_UEDX80480050E_WB_A` in `esp_panel_board_supported_conf.h`.

These are reference settings, not a universal configuration for every project or core version.

## Projects

| Folder | Description | Readiness |
|---|---|---|
| [Bluetooth Stream Deck](<Project/Bluetooth_Stream_Deck/README.md>) | BLE HID shortcut and media-key deck for the paired computer | Hardware-tested and working |
| [Digital Photo Frame](<Project/Digital_Photo_Frame/README.md>) | Web-dashboard photo frame: SD-card slideshow, transitions, uploads, OTA | Ported — PlatformIO build verified; on-hardware check pending |
| [ESP32 NAS](<Project/ESP32_NAS/README.md>) | WiFi NAS file server on the microSD slot: browser file manager, WiFi/AP setup portal, OTA | Ported — PlatformIO build verified; on-hardware check pending |

## Manufacturer resources

- Product page: [ESP32 5 Inch 800×480 RGB IPS TFT Display Touch Screen Arduino LVGL](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/)
- Official GitHub repository — datasheets, schematics, 2D drawings, examples, and factory firmware: [VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display)
- Display Wiki — display technology docs and tutorials: [display-wiki.com](https://www.display-wiki.com/)
- Sales / support: [info@viewedisplay.com](mailto:info@viewedisplay.com) · Wiki support: [viewedisplay@outlook.com](mailto:viewedisplay@outlook.com) · WhatsApp: [+852 6676 2053](https://wa.me/85266762053)

VIEWE on social media: [YouTube](https://www.youtube.com/@viewedisplay) · [Facebook](https://www.facebook.com/profile.php?id=61556567317171) · [LinkedIn](https://www.linkedin.com/company/viewe-technology-co-ltd/) · [TikTok](https://www.tiktok.com/@www.viewedisplay.com)

## Video guides

| Tutorial | Video |
|---|---|
| Bluetooth Stream Deck | Tutorial forthcoming — see the sibling [CrowPanel ESP32 7" playlist](https://www.youtube.com/playlist?list=PLxrSjjYyzaaImkFRbGMiRHKIos24P5MyG) in the meantime |
