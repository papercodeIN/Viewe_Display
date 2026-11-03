# WiFi Clock — VIEWE UEDX80480050E 5" Display

A PlatformIO project (VS Code) for the **VIEWE UEDX80480050E-WB-A** 5-inch ESP32-S3
touch display. The UI is built natively for the panel's **800×480** landscape
resolution with LVGL 8.4.

## Features

- **NTP clock** — large 96 px clock face, 12/24-hour toggle (persisted), date and weekday
- **Wi-Fi manager** — scan networks, pick one, type the password on an on-screen keyboard, non-blocking connect/reconnect with automatic retry
- **Weather** — key-free by default via [Open-Meteo](https://open-meteo.com) (no signup, no API key); automatically switches to OpenWeatherMap if `WEATHER_API_KEY` is set in `secrets.h`
- **Alarm** — hour/minute/second rollers with confirmation
- **Settings** — Wi-Fi setup, alarm, disconnect
- **Persistent config** — Wi-Fi credentials and clock format stored in NVS
- **Secrets kept out of Git** — `secrets.h` is ignored, `secrets.h.example` is committed

## Hardware

| Item | Value |
|------|-------|
| Board | VIEWE UEDX80480050E-WB-A |
| MCU | ESP32-S3, 8 MB PSRAM, 16 MB flash |
| Display | 5" IPS, 800×480, RGB, ST7262E43-G4 |
| Touch | GT911 capacitive |

## Requirements

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- No API keys needed — just set your city in `secrets.h` for weather

## Setup

### 1. Configure secrets

Copy the template and fill in your values:

```bash
cp secrets.h.example secrets.h
```

```c
#define WEATHER_API_KEY   ""          /* optional: set to use OpenWeatherMap */
#define WEATHER_CITY      "Mumbai"    /* city for the weather display        */
#define TIMEZONE_OFFSET   "IST-5:30"   /* India Standard Time, UTC+5:30 */
#define WIFI_SSID         "MyHomeWiFi"
#define WIFI_PASSWORD     "my-secret-password"
```

Without `WEATHER_API_KEY` the clock fetches weather from
[Open-Meteo](https://open-meteo.com) — a free, key-less service (plain HTTP,
non-commercial use). With a key it uses OpenWeatherMap instead. Leave
`WEATHER_CITY` empty to disable weather entirely.

**Weather shows `--` or `No city set`?** That means no data was fetched:

1. `No city set` on the clock face means exactly what it says — set
   `WEATHER_CITY` in `secrets.h` and re-flash.
2. After changing `secrets.h`, re-build and re-upload; the value is compiled
   into the firmware.
3. If a city is set but the display still shows `--`, check the serial
   monitor: every weather step is logged (`Retrying weather setup`,
   `lat/lon`, `Weather: ...`, HTTP error codes). A failed setup (e.g. DNS not
   ready right after connecting) is retried automatically every ~5 minutes.

`WIFI_SSID` / `WIFI_PASSWORD` are the default network. They are used when the
board has no credentials saved yet, or when the network saved in NVS cannot be
reached, so the clock connects on first boot without any on-screen typing.
Leave them empty to rely purely on the on-screen Wi-Fi setup. Credentials
entered on the screen are stored in NVS and take priority over the secrets
values.

`TIMEZONE_OFFSET` is a POSIX TZ string: the number is what you add to local
time to reach UTC, so an eastern zone needs a minus sign. India is
`"IST-5:30"`. Note that the clock is hardcoded to this zone — there is no
timezone picker on the device.

`secrets.h` is git-ignored — never commit it.

### 2. Build / upload / monitor

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial log at 115200 baud
```

In VS Code: open the project folder, then use the PlatformIO toolbar
(**Build**, **Upload**, **Monitor**).

> The first build downloads the ESP32 toolchain and the pinned Arduino core
> 3.3.9, which takes a while. Later builds are incremental.

### Toolchain notes (read if the build fails)

- **Isolated toolchain.** `platformio.ini` sets `packages_dir = .pio/packages`,
  so this project keeps its own Arduino core 3.3.9, LVGL and toolchain copies
  under `.pio/packages` instead of the shared `~/.platformio/packages`. Other
  projects or platform versions on the same machine cannot clobber them. The
  first build downloads several GB into that folder; `.pio/` is git-ignored.

- **Do not build from Git Bash / MSYS2.** Espressif's `idf_tools.py` refuses
  MSYS environments ("MSys/Mingw is not supported"), which leaves the esptool
  package half-installed and breaks the build with
  `ModuleNotFoundError: No module named 'esptool'`. Build from **cmd,
  PowerShell or VS Code** instead. From Git Bash you can clear the variable
  explicitly:

  ```bash
  cmd //c "set MSYSTEM=&& %USERPROFILE%\.platformio\penv\Scripts\python.exe -m platformio run"
  ```

- If a global-store build ever fails with `TypeError: ... 'NoneType'` inside
  the Espressif platform scripts (older checkouts without the
  `packages_dir` pin), remove the clobbered shared packages and rebuild —
  `platformio.ini` pins the correct core and re-downloads it:

  ```bash
  rm -rf ~/.platformio/packages/framework-arduinoespressif32
  rm -rf ~/.platformio/packages/tool-esptoolpy
  pio run
  ```

## Project layout

```
WiFi_Clock/
├── platformio.ini                  # PlatformIO environment + dependencies
├── boards/
│   └── BOARD_VIEWE_UEDX80480050E_WB_A.json
├── src/
│   ├── main.cpp                    # application logic (Wi-Fi, NTP, weather, alarm)
│   └── lvgl_v8_port.cpp/.h         # LVGL display/touch port
├── lib/
│   └── ui/                         # SquareLine UI library
│       ├── ui.h
│       └── src/
│           ├── ui.c                # screen + event glue
│           ├── screens/            # 800×480 screen layouts
│           ├── fonts/              # generated LVGL fonts (incl. Clock96)
│           └── images/             # generated LVGL image assets
├── lv_conf.h                       # LVGL 8.4 configuration
├── lvgl_v8_port.cpp/.h             # LVGL display/touch port
├── esp_panel_*_conf.h              # display panel / board configuration
└── secrets.h / secrets.h.example   # credentials (secrets.h is ignored)
```

## Display configuration

The board preset is enabled in `esp_panel_board_supported_conf.h`:

```c
#define ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED       (1)
#define BOARD_VIEWE_UEDX80480050E_WB_A
```

No other board changes are required.

## Notes

- **Time is synced with SNTP** (`pool.ntp.org`, `time.nist.gov` as fallback)
  using the timezone from `secrets.h`. System time is set automatically once
  Wi-Fi connects and is re-synchronized about once an hour afterwards. If the
  clock loses Wi-Fi, it retries the last working credentials every 30 seconds.
- **Timezone is fixed to India Standard Time** (`IST-5:30` in `secrets.h`).
  Change `TIMEZONE_OFFSET` there if you move the clock to another zone.
- The RGB panel runs with **LVGL direct mode + LCD double buffering**
  (`LVGL_PORT_AVOID_TEARING_MODE == 3` in `src/lvgl_v8_port.h`). This is the
  recommended mode for RGB panels: LVGL renders straight into the LCD frame
  buffers and only changed dirty regions are redrawn, so the per-second clock
  update does not repaint the whole 800×480 panel (no visible 1 Hz flash).
  `setup()` therefore calls `configFrameBufferNumber()` and
  `configRGB_BounceBufferSize()` before `board->begin()`.
- LVGL is pinned to **v8.4.0**. The UI is written for the LVGL 8 API; do not
  upgrade to v9 without porting the screens and the `lvgl_v8_port` layer.
- The 96 px clock font (`lib/ui/src/fonts/ui_font_Clock96.c`) was generated with
  `lv_font_conv` from Montserrat, digits and colon only, to keep flash usage low.
