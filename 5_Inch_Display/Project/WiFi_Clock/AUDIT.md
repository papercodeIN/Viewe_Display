# WiFi_Clock — Audit & Production-Readiness Report

Date: 2026-11-03
Scope: full audit of the PlatformIO project for the VIEWE UEDX80480050E-WB-A 5" ESP32-S3 clock.

## 1. Errors fixed (build-breaking)

| # | Issue | Fix |
|---|-------|-----|
| 1 | `src/main.cpp` used `NTPClient timeClient` / `WiFiUDP ntpClient` but their includes had been removed in the working tree → `pio run` failed with `'NTPClient' does not name a type` (4 errors). | Replaced the one-shot NTPClient polling with lwIP SNTP via `configTzTime(TIMEZONE_OFFSET, "pool.ntp.org", "time.nist.gov")`. Continuous background sync (auto re-sync ≈ hourly), correct DST handling from the POSIX TZ string, one dependency fewer. |

## 2. Dead code / dead imports removed

- `#include "esp_flash.h"` — never used.
- `NTPClient@3.2.1` in `platformio.ini` — no longer used by any code.
- Globals `url_openweather`, `url_lat_lon`, `lat_lon_url` — URL builders now return `String` locally.
- Flags `ntp_time_valid`, `ntp_sync_pending`, `ntp_sync_started_ms` and all `timeClient.begin()/isTimeSet()/update()` calls (including a duplicated `begin()` in `serviceWiFiConnection`).
- Dead `_ui_screen_change(&ui_ScreenClock, …)` inside `handleAlarmConfirmation` — the generated `ui_event_ButtonConfirm` already switches screens.
- Dangling `lv_obj_set_user_data(item, (void*)WiFi.SSID(i).c_str())` — pointer into a temporary `String`, and never read.
- Stale `board_dir = ${PROJECT_DIR}/boards` in `platformio.ini` (not a valid env option; PlatformIO's default `boards/` lookup already works).
- Junk file `src/idf_component.yml.orig` (byte-identical backup) — deleted.
- Duplicate/contradictory `Serial.println` state spam in `handlePasswordValidation`.

## 3. Robustness / correctness fixes

- **`assert(board->begin())`** compiled out under release builds → replaced with a real check: log + `ESP.restart()`.
- **NVS init**: `nvs_flash_init()` failure now erases the partition and retries (standard ESP-IDF pattern) instead of just logging.
- **Auto-reconnect after Wi-Fi drop**: previously a drop left the clock offline forever. Now the last working credentials are remembered and retried every 30 s (`WIFI_RECONNECT_INTERVAL_MS`); the on-screen "Disconnect Wi-Fi" action disables auto-reconnect and wipes saved credentials from NVS.
- **Wi-Fi scan lifecycle**: the spinner now always resolves — hidden when the scan fails, times out, or finds 0 networks ("No Wi-Fi networks found" message shown). A scan started on the Wi-Fi list screen is abandoned when the user navigates away instead of building the list on whichever screen happens to be active (`lv_scr_act()` guard; the list is now parented to `ui_ScreenWifiList` explicitly).
- **Dangling pointers**: `selected_wifi_name` is reset when the list is rebuilt and the keyboard handler bails out if no network is selected; the textarea password is copied into a static buffer before the textarea is cleared.
- **Alarm rollers**: `lv_roller_get_selected_str` was called with the wrong buffer sizes for minute/second (`sizeof(hours_rol)`); `alarm_time` is now built with `snprintf`.
- **Weather JSON**: `deserializeJson` errors are checked; missing fields keep the previous values instead of blanking the UI; `http.begin()` results checked; geocoding guarded by `WiFi.status()`.
- **City names with spaces** (e.g. "New York") now work: query parameters are percent-encoded (`urlEncode`).
- **LVGL thread safety**: Wi-Fi icon visibility is now always read/modified under the LVGL lock (was an unlocked `lv_obj_has_flag()` read from the loop task).
- **Alarm dialog**: `lv_msgbox` now passes `NULL` buttons (previously an invisible empty-text button); text updated to match.
- **Temperature/weather labels**: `snprintf` + `.c_str()` instead of `sprintf`/`String` varargs.

## 4. Supply-chain / reproducibility

- `ESP32_Display_Panel` was floating on git `main` (installed copy was v1.0.5-dev, commit 92b790e). Pinned to the latest stable release **`#v1.0.4`**.
- `esp_panel_drivers_conf.h` synced to the v1.0.4 template (only delta was the file-version stamp + one default-off line) — removes the `#warning "esp_panel_drivers_conf.h version is outdated"` spam.

## 5. Housekeeping

- `lib/ui/src/screens/ui_ScreenPassord.c` → **`ui_ScreenPassword.c`** (filename typo; upstream fixed the same name in v0.1.6; `filelist.txt`/`CMakeLists.txt` already referenced the correct name).
- `filelist.txt` + `CMakeLists.txt`: added the missing `fonts/ui_font_Clock96.c` entry.
- `lib/ui/src/ui.h`: declared `extern lv_obj_t *ui_LabelTimeAmPm;` (defined in `main.cpp`, used by the clock screen).
- README: documented SNTP servers, hourly re-sync, and 30 s auto-reconnect behavior.

## 6. On-device issues found after first flash (fixed same day)

| # | Issue | Root cause | Fix |
|---|-------|-----------|-----|
| 1 | Screen glitching constantly | Unconditional LVGL updates: Wi-Fi icon flag was written every 500 ms and the AM/PM label text/flag every second. `lv_obj_clear_flag()`/`lv_label_set_text()` invalidate **even when nothing changes**, and in LVGL full-refresh mode every invalidation repaints the whole 800×480 frame through PSRAM (~1.5 MB per pass). The old code guarded these calls; the audit rewrite had dropped the guards. | All one-second timers (time/AM-PM, date, weekday, weather, temperature) and the Wi-Fi icon now write **only when the value actually changed**. Idle redraw load drops to exactly the 1 Hz clock update. |
| 2 | Date shows `DDMMMYYYY`, wanted `DD-MMM-YYYY` | Format string `%02d%s%04d` | `%02d-%s-%04d`, buffer `DATE_DISPLAY_LEN` 10 → 16 (hyphen verified present in `ui_font_FontNumber28bp4`) |

## 7. Build environment notes (this machine)

- Two PlatformIO installations share `~/.platformio`: VS Code uses `C:\Users\Parrot\.platformio\penv\Scripts\platformio.exe`; the shell PATH also exposes a bundled PlatformIO on Python 3.13. **Never run two `pio` builds at once**: every run re-installs `framework-arduinoespressif32` from a URL (no integrity hash), so a concurrent build sees the package mid-extraction and fails (missing `NetworkClient.h` / `FRAMEWORK_DIR = None` / bootloader errors). A concurrent-build collision caused the intermittent failures during this audit.
- `esptool` was missing from the penv (the `idf_tools.py installation failed (rc=1)` warning seen on every run; the pioarduino `tool-esptoolpy` zip ships no packaging metadata). Fixed with `python -m pip install esptool` **inside `C:\Users\Parrot\.platformio\penv`** (esptool 5.3.0) — required for `bootloader.bin` regeneration after any clean build.

## 8. Known remaining (benign) warnings

- `initialization discards 'const' qualifier` (`static char *TAG`) and enum|enum deprecations inside the **third-party** libraries (ESP32_IO_Expander, LVGL 8 style) — upstream issues, not project code.

## 9. Not changed (intentionally)

- Nothing is committed — all changes are in the working tree for your review (`git diff`).
- The alarm is one-shot (lost on reboot) — persisting it would need an off-toggle in the UI; flagged as a possible follow-up.
- `src/idf_component.yml` kept (harmless; enables future ESP-IDF builds).
- Git history was not rewritten; no secrets are present in tracked files (`secrets.h` is git-ignored; the two `[redacted]`-looking lines in `main.cpp` are really `= NULL` and `= WEATHER_API_KEY`).
- Display pipeline kept on `LVGL_PORT_AVOID_TEARING_MODE == 1` (LCD double buffer + LVGL full refresh) as chosen in commit fd18b62. If any glitching remains after this fix, the next levers are a larger RGB bounce buffer (`configRGB_BounceBufferSize`) or mode 3 (direct mode).

## 10. Verification

- `pio run` with the VS Code PlatformIO: **SUCCESS (85 s)**, firmware + `firmware.factory.bin` produced (`tmp_audit` build logs removed after the session). Remaining warnings are third-party only.
- On-device (user flash): Wi-Fi connect + SNTP time + 12/24 h toggle verified working; glitching and date format fixes pending user re-flash of the new build.
