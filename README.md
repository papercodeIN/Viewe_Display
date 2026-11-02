<div align="center">

<img src="https://visitor-badge.laobi.icu/badge?page_id=papercodeIN.Viewe_Display" alt="Repository visitors" height="20" />

# Viewe Display Projects

**Open-source firmware, examples, and build notes for VIEWE development boards**

[![Boards](https://img.shields.io/badge/hardware-VIEWE-2F4F7F)](https://viewedisplay.com/)
[![Framework](https://img.shields.io/badge/framework-Arduino_/_PlatformIO-00979D?logo=arduino)](https://platformio.org/)
[![UI](https://img.shields.io/badge/UI-LVGL-32A3CE)](https://lvgl.io/)
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

## About

This repository collects firmware, examples, and project notes for VIEWE (VIEWESMART) ESP32 smart display boards used in Fusion Automate builds: the **5-inch** 800×480 ESP32-S3 smart display, and a round display coming soon.

Project readiness varies. Some folders are complete PlatformIO projects; others may be Arduino sketches that need board-specific libraries and configuration. Check each project README for requirements, wiring, tested status, and build instructions before flashing.

## Projects

### 5 Inch Display — ESP32-S3 with 800×480 RGB touch display ([product](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/) · [official GitHub](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display) · [Display Wiki](https://www.display-wiki.com/))

See the [5-inch board page](<5_Inch_Display/README.md>) for setup notes and project status.

| Project | Description |
|---|---|
| [Bluetooth Stream Deck](<5_Inch_Display/Project/Bluetooth_Stream_Deck/README.md>) | Touchscreen shortcut panel that sends BLE HID media keys and keyboard shortcuts to the paired computer; port of the CrowPanel 7" build, in development |

### Round display — model to be added

A folder for the round display will be added to this repository once the display model is finalized.

### Board references

| Board | Contents |
|---|---|
| [5 Inch Display](<5_Inch_Display/README.md>) | Board resources, projects, video links, and configuration notes |
| Round display | Coming soon |

## Video walkthroughs

Build and demo videos on the [Fusion Automate YouTube channel](https://www.youtube.com/channel/UCKKhdFV0q8CV5vWUDfiDfTw):

- Videos for this repository are in the works. Meanwhile, the sibling CrowPanel ESP32 7" build — the base for the Bluetooth Stream Deck port — is in the [CrowPanel ESP32 7" playlist](https://www.youtube.com/playlist?list=PLxrSjjYyzaaImkFRbGMiRHKIos24P5MyG).

## Getting started

1. Choose a board or project from the tables above.
2. Read its README for required hardware, dependencies, wiring, and build status.
3. Use Arduino IDE/CLI or PlatformIO as specified by that project; sketch-only examples may need additional setup.
4. Find boards and product details at [VIEWE Display](https://viewedisplay.com/).
