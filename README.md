<img align="right" src="https://visitor-badge.laobi.icu/badge?page_id=papercodeIN.Viewe_Display" height="20" />

<h1 align="center">🌟 VIEWE Display 🌟</h1>

<p align="center">
  <em>Projects for the VIEWE 5-inch 800x480 ESP32-S3 smart display — Bluetooth Stream Deck and more</em>
</p>

<p align="center">
  <a href="https://www.youtube.com/@Fusion_Automate"><img src="https://img.shields.io/badge/YouTube-FusionAutomate-%23FF0000?style=for-the-badge&logo=youtube" alt="YouTube"></a>
  <a href="https://www.instagram.com/fusion_automate"><img src="https://img.shields.io/badge/Instagram-fusion__automate-%23E4405F?style=for-the-badge&logo=instagram" alt="Instagram"></a>
  <a href="https://x.com/fusion_automate"><img src="https://img.shields.io/badge/X-fusion__automate-000000?style=for-the-badge&logo=x" alt="X (Twitter)"></a>
  <a href="https://t.me/fusionautomate"><img src="https://img.shields.io/badge/Telegram-%40fusionautomate-%2326A5E4?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://fusionautomate.in/"><img src="https://img.shields.io/badge/Website-fusionautomate.in-blueviolet?style=for-the-badge" alt="Website"></a>
</p>

---

## About

This repository collects firmware, examples, and project notes for VIEWE (VIEWESMART) ESP32 smart display boards used in Fusion Automate builds: the **5-inch** 800×480 ESP32-S3 smart display, and a round display coming soon.

Project readiness varies. Some folders are complete PlatformIO projects; others may be Arduino sketches that need board-specific libraries and configuration. Check each project README for requirements, wiring, tested status, and build instructions before flashing.

## Projects

### 5 Inch Display — ESP32-S3 with 800×480 RGB touch display ([product](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/) · [official GitHub](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display) · [Display Wiki](https://www.display-wiki.com/))

See the [5-inch board page](<5_Inch_Display/README.md>) for setup notes and project status.

| Project | Description |
|---|---|
| [Bluetooth Stream Deck](<5_Inch_Display/Project/Bluetooth_Stream_Deck/README.md>) | Touchscreen shortcut panel that sends BLE HID media keys and keyboard shortcuts to the paired computer; port of the CrowPanel 7" build, hardware-tested and working |
| [Digital Photo Frame](<5_Inch_Display/Project/Digital_Photo_Frame/README.md>) | Wi-Fi photo frame with a web dashboard — upload photos, slideshow with transitions, SD-card storage, OTA; port of the CrowPanel 7" build |
| [ESP32 NAS](<5_Inch_Display/Project/ESP32_NAS/README.md>) | Wi-Fi NAS file server on the onboard microSD slot — browser file manager (upload/download/delete), WiFi setup portal with AP fallback, OTA; port of the CrowPanel 2.8" build |
| [Wireless Second Screen](<5_Inch_Display/Project/2nd_Screen/README.md>) | PC screen mirroring to the 5" ESP32-S3 display over Wi-Fi (MJPEG/TCP), with touch-to-mouse input backchannel and cursor-following zoom/magnification |

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

---

## 📞 Contact

| 📞 Method | 🔗 Details |
|-----------|-----------|
| Call / WhatsApp | [+91-9974477759](https://wa.me/919974477759) |
| Telegram | [@fusionautomate](https://t.me/fusionautomate) |
| Email | [eng.innovativ@gmail.com](mailto:eng.innovativ@gmail.com) |

## 💖 Support My Work

<p align="center">
  <a href="https://buymeacoffee.com/pylin" target="_blank">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" style="height: 50px; width: 180px;">
  </a>
</p>

<p align="center">
  ⭐ Star this repo if it helped you · 🔔 <a href="https://github.com/papercodeIN">Follow Fusion Automate</a>
</p>
