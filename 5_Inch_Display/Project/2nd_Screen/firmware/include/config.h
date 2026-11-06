#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// Display hardware pinout (VIEWE UEDX80480050E-WB 5-inch)
// ============================================================
// Hardware-tested configuration, shared with the Digital Photo Frame and
// Bluetooth Stream Deck projects in this repository: ST7262 RGB panel on a
// 16-bit RGB565 bus, vendor timings (HPW 1, HBP 42, HFP 20 / VPW 10, VBP 12,
// VFP 4) and a 15 MHz PCLK.
//
// The PCLK is deliberately low: Arduino_GFX has no DMA bounce buffers, so the
// scanout DMA reads the PSRAM framebuffer directly and the vendor's 20 MHz
// rate outruns sustainable PSRAM bandwidth — the panel drifts and glitches.
// 15 MHz is the proven-stable rate for this exact stack (same library, same
// ST7262 panel, same OPI-PSRAM ESP32-S3).
#define TFT_BL              2
#define TFT_BL_CHANNEL      0         // LEDC channel driving the backlight
#define TFT_BL_FREQ_HZ      20000     // PWM frequency, above audible range
#define RGB_PCLK_HZ         15000000
#define SCREEN_WIDTH        800
#define SCREEN_HEIGHT       480

// ============================================================
// GT911 capacitive touch (I2C)
// ============================================================
// Pins per VIEWE's official board documentation (UEDX80480050E ESP32 repo):
// SDA = GPIO19, SCL = GPIO20, INT = GPIO18, RST = GPIO38.
// NOTE: GPIO19/20 are also the native USB D-/D+ pins — that is why the build
// disables USB-CDC (ARDUINO_USB_CDC_ON_BOOT=0) and flashes over the CH340C
// UART bridge instead.
#define TP_SDA              19
#define TP_SCL              20
#define TP_INT              18
#define TP_RST              38
#define TP_I2C_FREQ_HZ      400000

// Touch orientation fixes. GT911 usually reports coordinates already matched
// to the panel's native landscape orientation; if touch lands mirrored or
// rotated on your unit (two touch-cable variants exist per the vendor repo),
// flip these at compile time — no re-flash of anything else needed.
#define TOUCH_SWAP_XY       0
#define TOUCH_FLIP_X        0
#define TOUCH_FLIP_Y        0

// ============================================================
// WiFi Configuration
// ============================================================
// Credentials live in include/secrets.h (gitignored). Copy
// include/secrets.h.example to include/secrets.h and fill them in.
#define WIFI_HOSTNAME       "secondscreen"
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_RETRY_INTERVAL_MS  15000

#if __has_include("secrets.h")
#include "secrets.h"
#define HAS_DEFAULT_CREDENTIALS 1
#else
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define HAS_DEFAULT_CREDENTIALS 0
#endif

// ============================================================
// Mirror Service
// ============================================================
#define MIRROR_TCP_PORT     5566
#define MDNS_INSTANCE       "VIEWE Second Screen"
#define MDNS_HOSTNAME       "secondscreen"   // reachable as secondscreen.local

// ============================================================
// Wire Protocol (2ndScreen protocol v1, mirrored by pc-client/mirror.py)
// ============================================================
// Every packet starts with a 4-byte ASCII magic. All integers little-endian.
//
//   PC -> ESP32:
//     "MJP1" u16 seq u16 flags u32 jpegLen                 (12-byte header)
//            u8 jpegLen bytes of baseline-JPEG payload     (800x480, RGB)
//     "PING"                                               (keepalive, >= 1/5s)
//     "BYE!"                                               (clean disconnect)
//   ESP32 -> PC:
//     "RDYT" u16 width u16 height u16 protoVer u16 caps u32 maxFrameBytes
//          caps bit0 = touch backchannel available         (16-byte hello)
//     "PONG"
//     "TOCH" u8 nPts then nPts * { u8 id, u8 action, i16 x, i16 y }
//          action: 1 = down, 2 = move, 3 = up              (up = released)
//     "BUSY"                                               (second client rejected)
//
// JPEG payload rules: baseline (non-progressive) JPEG, exactly SCREEN_WIDTH x
// SCREEN_HEIGHT (the PC bakes the letterbox into the image). Progressive
// JPEGs are rejected by TJpgDec and logged; the connection stays up.
#define PROTO_VER           1
#define MAX_JPEG_BYTES      (384 * 1024)
#define MAX_TOUCH_POINTS    5
#define CLIENT_IDLE_TIMEOUT_MS 15000     // no valid packet for 15s -> close
#define STATS_INTERVAL_MS   5000
#define FRAME_READ_TIMEOUT_MS 6000       // mid-frame stall before disconnect
#define CURSOR_SIZE         7            // pointer-dot overlay: NxN px (black border, white fill)
#define CURSOR_BACKUP_W     (CURSOR_SIZE + 2)
#define CURSOR_BACKUP_H     (CURSOR_SIZE + 2)

// ============================================================
// Watchdog Timer
// ============================================================
#define WDT_TIMEOUT_S       8        // log-only watchdog (never panics/reboots)

// ============================================================
// Logging Levels (compile-time filtering)
// ============================================================
// 0 = None, 1 = Error, 2 = Warn, 3 = Info, 4 = Debug
#define LOG_LEVEL           3

#define LOG_ERROR(...)      do { if (LOG_LEVEL >= 1) Serial.printf("[ERR] " __VA_ARGS__); } while(0)
#define LOG_WARN(...)       do { if (LOG_LEVEL >= 2) Serial.printf("[WRN] " __VA_ARGS__); } while(0)
#define LOG_INFO(...)       do { if (LOG_LEVEL >= 3) Serial.printf("[INF] " __VA_ARGS__); } while(0)
#define LOG_DEBUG(...)      do { if (LOG_LEVEL >= 4) Serial.printf("[DBG] " __VA_ARGS__); } while(0)

#endif // CONFIG_H
