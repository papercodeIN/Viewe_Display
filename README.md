# VIEWE 5" ESP32-S3 Display (UEDX80480050E-WB)

Projects and examples for the VIEWE 5-inch ESP32-S3 smart display — 800 × 480 IPS panel with capacitive touch, Wi-Fi and Bluetooth 5 LE.

![Hardware](https://img.shields.io/badge/hardware-VIEWE_5inch_ESP32--S3-2F4F7F)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3_240MHz-E7352C?logo=espressif&logoColor=white)
![UI](https://img.shields.io/badge/UI-LVGL-32A3CE)
![Wireless](https://img.shields.io/badge/wireless-Wi--Fi_&_BLE_5-0082FC?logo=bluetooth&logoColor=white)

## Projects

Standalone PlatformIO projects, each with its own build guide:

| Project | Description |
|---|---|
| [Bluetooth Stream Deck](<Project/Bluetooth_Stream_Deck/README.md>) | BLE HID keyboard deck — touch tiles send shortcuts and media keys straight to the paired computer. Port of the CrowPanel 7" build, in development |

## Manufacturer resources

- Product page: [ESP32 5 Inch 800×480 RGB IPS TFT Display Touch Screen Arduino LVGL](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/)
- Official GitHub repository — datasheets, schematics, 2D drawings, examples and factory firmware: [VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display)
- Display Wiki — open display technology docs and tutorials: [display-wiki.com](https://www.display-wiki.com/)
- VIEWE official website: [viewedisplay.com](https://viewedisplay.com/)
- Sales / support: [info@viewedisplay.com](mailto:info@viewedisplay.com) · Wiki support: [viewedisplay@outlook.com](mailto:viewedisplay@outlook.com) · WhatsApp: [+852 6676 2053](https://wa.me/85266762053)

## VIEWE on social media

[![VIEWE YouTube](https://img.shields.io/badge/YouTube-%40viewedisplay-FF0000?logo=youtube&logoColor=white)](https://www.youtube.com/@viewedisplay)
[![VIEWE Facebook](https://img.shields.io/badge/Facebook-VIEWE_Display-1877F2?logo=facebook&logoColor=white)](https://www.facebook.com/profile.php?id=61556567317171)
[![VIEWE LinkedIn](https://img.shields.io/badge/LinkedIn-VIEWE_Technology-0A66C2?logo=linkedin&logoColor=white)](https://www.linkedin.com/company/viewe-technology-co-ltd/)
[![VIEWE TikTok](https://img.shields.io/badge/TikTok-%40viewedisplay-000000?logo=tiktok&logoColor=white)](https://www.tiktok.com/@www.viewedisplay.com)

## Component versions

| Environment | esp32 core | LVGL | Notes |
|---|---|---|---|
| Arduino IDE (manufacturer setup) | 3.0.0+ | 8.4.0 | With `ESP32_Display_Panel` 1.0.3+ by Espressif; enable `BOARD_VIEWE_UEDX80480050E_WB_A` in `esp_panel_board_supported_conf.h` |
| PlatformIO (projects above) | pinned per project | pinned per project | See each project's `platformio.ini` |

The manufacturer also ships ESP-IDF (v5.3+) examples with LVGL v9 in the official repository.

## Video walkthroughs

Build and demo videos on the [Fusion Automate YouTube channel](https://www.youtube.com/channel/UCKKhdFV0q8CV5vWUDfiDfTw):

- Videos for this board are in the works. Meanwhile, the sibling build for the Elecrow CrowPanel 7" — the base for the Bluetooth Stream Deck port — is in the [CrowPanel ESP32 7" playlist](https://www.youtube.com/playlist?list=PLxrSjjYyzaaImkFRbGMiRHKIos24P5MyG) and the [Elecrow repository](https://github.com/papercodeIN/Elecrow).

## Support

If these projects help you, consider supporting the channel:

<a href="https://buymeacoffee.com/pylin" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="36"></a>
&nbsp;&nbsp;
<a href="https://www.youtube.com/channel/UCKKhdFV0q8CV5vWUDfiDfTw" target="_blank"><img src="https://img.shields.io/badge/SUBSCRIBE%20ON%20YOUTUBE-FF0000?style=for-the-badge&logo=youtube&logoColor=white" alt="Subscribe on YouTube" height="36"></a>

## Contact

| Method | Details |
|---|---|
| Call / WhatsApp | [+91-9974477759](https://wa.me/919974477759) |
| Telegram | [@fusionautomate](https://t.me/fusionautomate) |
| Instagram | [@fusion_automate](https://www.instagram.com/fusion_automate) |
| Email | [eng.innovativ@gmail.com](mailto:eng.innovativ@gmail.com) |
