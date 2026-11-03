#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// VIEWE UEDX80480050E-WB 5" ESP32-S3 Display - Pin Definitions
// Sources: vendor schematic (Schematic/5.0.png in the VIEWE
// UEDX80480050ESP32-5inch-Touch-Display repo) and the
// hardware-tested Bluetooth_Stream_Deck port in this repo.
// ============================================================

// --- Display: 800x480 RGB parallel panel (ST7262) ---
// Driven by Arduino_GFX (Arduino_ESP32RGBPanel), not TFT_eSPI:
// TFT_eSPI has no support for RGB parallel panels.
//
// PCLK runs at 15 MHz on purpose (see src/main.cpp for why).
#define SCREEN_W        800
#define SCREEN_H        480

// --- Backlight ---
// IO2-LCD-BL-EN drives the LP3310B6F backlight boost converter.
#define TFT_BL          2
#define BL_PWM_CHANNEL  0
#define BL_PWM_FREQ_HZ  20000
#define BL_PWM_RES_BITS 8

// --- microSD card slot (TF1, SPI on the FSPI bus) ---
// From the TF section of the vendor schematic (RTP signals were
// repurposed for the card slot in this hardware revision):
//   TF1 pin 2 (DAT3/CS) -> GPIO10 (10K pull-up to 3V3)
//   TF1 pin 3 (CMD)     -> GPIO11 (10K pull-up to 3V3)
//   TF1 pin 5 (CLK)     -> GPIO12
//   TF1 pin 7 (DATA0)   -> GPIO13
#define SD_CS           10
#define SD_MOSI         11
#define SD_SCK          12
#define SD_MISO         13

// Card clock. 20 MHz matches the vendor's own sd_card_spi example
// default for this slot. If a card refuses to mount, drop this to
// 4000000 - the firmware also auto-downgrades to 4 MHz after 3
// failed remount attempts.
#define SD_SPI_FREQ_HZ          20000000
#define SD_SPI_FALLBACK_FREQ_HZ 4000000
#define SD_FREQ_DOWNGRADE_AFTER 3    // failed remounts before fallback

// --- Capacitive touch (GT911) - NOT used by this firmware ---
// The NAS status screen is display-only (no touch UI), so the
// GT911 stays uninitialised: SDA GPIO19, SCL GPIO20,
// RST GPIO38, INT GPIO18.

// ============================================================
// WiFi Configuration
// ============================================================
// AP mode is open (no password) - user connects to set up home WiFi
#define AP_SSID             "NAS-Setup"
#define WIFI_HOSTNAME       "viewe-nas"
#define MAX_WIFI_FAIL       10   // non-blocking reconnect attempts before AP fallback

// Default WiFi credentials (optional - secrets.h is gitignored)
#if __has_include("secrets.h")
#include "secrets.h"
#define HAS_DEFAULT_CREDENTIALS 1
#else
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define HAS_DEFAULT_CREDENTIALS 0
#endif

// NVS Preferences keys
#define PREF_NAMESPACE  "nas_wifi"
#define PREF_SSID       "ssid"
#define PREF_PASS       "pass"

// ============================================================
// Server Configuration
// ============================================================
#define HTTP_PORT       80

// ============================================================
// Timing (milliseconds)
// ============================================================
#define SD_POLL_INTERVAL        5000
#define WIFI_RETRY_INTERVAL     10000
#define WIFI_CONNECT_TIMEOUT    60000
#define SD_RETRY_ANIM_INTERVAL  500   // SD-fault animation tick (small area, no flicker)
#define UPLOAD_STALL_TIMEOUT    60000 // abandoned upload: release PSRAM buffer

#endif // CONFIG_H
