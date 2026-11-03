#ifndef DISPLAY_5INCH_H
#define DISPLAY_5INCH_H

// VIEWE UEDX80480050E-WB 5-inch RGB panel backend on Arduino_GFX (GFX Library
// for Arduino). Keep this definition in its own header so image_decoder.h can
// include it before declaring the global display instance.
//
// The panel configuration is the hardware-tested VIEWE 5-inch setup from the
// Bluetooth Stream Deck project in this repository: ST7262 RGB panel on a
// 16-bit RGB565 bus, vendor timings (HPW 1, HBP 42, HFP 20 / VPW 10, VBP 12,
// VFP 4) and a 15 MHz PCLK. The PCLK is deliberately low: Arduino_GFX has no
// DMA bounce buffers, so the scanout DMA reads the PSRAM framebuffer directly
// and the vendor's 20 MHz rate outruns sustainable PSRAM bandwidth — the panel
// drifts and glitches. 15 MHz is the proven-stable rate for this exact stack
// (same library, same ST7262 panel, same OPI-PSRAM ESP32-S3).
//
// Arduino_GFX 1.4.7 exposes neither pushImage/setSwapBytes nor a backlight
// helper, so this class adds the small LovyanGFX-style surface the ported
// photo-frame code was written against:
//   - pushImage() routes to draw16bitBeRGBBitmap() when byte-swapped input is
//     selected (TJpg_Decoder with setSwapBytes(true) emits big-endian pixels)
//     and to draw16bitRGBBitmap() otherwise (native-order pixels).
//   - drawCentreString() reproduces LovyanGFX's centred text; its font-number
//     argument maps to Adafruit-style text scaling (font 1/2/4 -> x1/x2/x4).
//   - setBrightness() drives the backlight LED with LEDC PWM (GPIO 2);
//     Arduino_RGB_Display has no backlight support of its own.

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "config.h"

// TFT_* colour names used by the ported screens. Arduino_GFX ships the same
// RGB565 palette without the TFT_ prefix, so only define what is missing.
#ifndef TFT_BLACK
#define TFT_BLACK        0x0000
#endif
#ifndef TFT_WHITE
#define TFT_WHITE        0xFFFF
#endif
#ifndef TFT_YELLOW
#define TFT_YELLOW       0xFFE0
#endif
#ifndef TFT_GREEN
#define TFT_GREEN        0x07E0
#endif
#ifndef TFT_GREENYELLOW
#define TFT_GREENYELLOW  0xAFE5
#endif
#ifndef TFT_LIGHTGREY
#define TFT_LIGHTGREY    0xD69A
#endif

class LGFX : public Arduino_RGB_Display {
public:
    LGFX() : Arduino_RGB_Display(
        SCREEN_WIDTH, SCREEN_HEIGHT,
        new Arduino_ESP32RGBPanel(
            40, 41, 39, 42,      // DE, VSYNC, HSYNC, PCLK
            45, 48, 47, 21, 14,  // R0-R4 (DATA11-15)
            5, 6, 7, 15, 16, 4,  // G0-G5 (DATA5-10)
            8, 3, 46, 9, 1,      // B0-B4 (DATA0-4)
            0, 20, 1, 42,        // hsync polarity, front porch, pulse width, back porch
            0, 4, 10, 12,        // vsync polarity, front porch, pulse width, back porch
            1, RGB_PCLK_HZ),     // PCLK active on falling edge, 15 MHz (see note above)
        0, true) {}              // no extra rotation, framebuffer in PSRAM, auto cache-flush

    // LovyanGFX-style raw bitmap blit with selectable input byte order.
    void setSwapBytes(bool swap) { _swapBytes = swap; }

    void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *data) {
        if (_swapBytes) {
            draw16bitBeRGBBitmap(x, y, data, w, h);
        } else {
            draw16bitRGBBitmap(x, y, data, w, h);
        }
    }

    // Centred single-line text. `size` mirrors LovyanGFX's font-number argument
    // position and is used as the Adafruit text-size multiplier.
    void drawCentreString(const char *str, int32_t x, int32_t y, uint8_t size) {
        int16_t bx, by;
        uint16_t bw, bh;
        setTextSize(size);
        getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
        setCursor(x - bw / 2, y);
        print(str);
    }

    void drawCentreString(const String &str, int32_t x, int32_t y, uint8_t size) {
        drawCentreString(str.c_str(), x, y, size);
    }

    // Backlight: LEDC PWM on TFT_BL. Initialises the channel on first use so
    // callers can treat setBrightness() like LovyanGFX's built-in helper.
    void setBrightness(uint8_t brightness) {
        static bool blReady = false;
        if (!blReady) {
            ledcSetup(TFT_BL_CHANNEL, TFT_BL_FREQ_HZ, 8);
            ledcAttachPin(TFT_BL, TFT_BL_CHANNEL);
            blReady = true;
        }
        ledcWrite(TFT_BL_CHANNEL, brightness);
    }

private:
    bool _swapBytes = false;
};

#endif // DISPLAY_5INCH_H
