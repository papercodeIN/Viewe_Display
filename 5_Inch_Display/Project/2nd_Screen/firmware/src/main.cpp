// ============================================================
// VIEWE UEDX80480050E-WB 5" ESP32-S3 — Wireless Second Screen
// ============================================================
// Receives MJPEG frames over TCP on the local network, decodes them with
// TJpg_Decoder, blits them straight into the RGB panel's PSRAM framebuffer,
// and streams GT911 touch events back to the PC.
//
// Protocol (must stay in sync with pc-client/mirror.py — see config.h):
//   PC -> ESP32 : "MJP1" frames, "PING", "BYE!", "CARS" cursor
//   ESP32 -> PC : "RDYT" hello, "PONG", "TOCH" touch, "BUSY" (second client)
//
// Hardware configuration is shared with the Digital Photo Frame project in
// this repository (ST7262 RGB panel @ 15 MHz PCLK, 16-bit RGB565 bus, GT911
// touch on GPIO19/20) — see include/config.h and include/display_5inch.h.
//
// Threading model: everything runs in the Arduino loop task (core 1). The
// Wi-Fi/lwIP stack lives on core 0. TJpg_Decoder and Arduino_GFX are both
// single-threaded globals, so a single consumer thread avoids any locking.

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "display_5inch.h"
#include <TJpg_Decoder.h>

#define FIRMWARE_VERSION "1.0.0"

LGFX tft;

// ---------- Panel / decode buffers (all in PSRAM) ----------
static uint8_t*  jpegBuf   = nullptr;  // incoming JPEG payload
static uint16_t* shadowBuf = nullptr;  // last decoded frame, cursor-free copy

// ---------- Network state ----------
static WiFiServer server(MIRROR_TCP_PORT);
static WiFiClient client;
static bool mdnsUp = false;
static uint32_t lastSeenMs = 0;        // last valid packet from the client

// ---------- Stats ----------
static uint32_t framesDecoded = 0;
static uint32_t bytesTotal = 0;
static uint32_t framesWindow = 0;      // frames within the stats window
static float    decodeMsAvg = 0.0f;
static uint32_t statsLastMs = 0;

// ---------- Cursor overlay ----------
static int16_t curX = 0, curY = 0;
static bool curVisible = false;
// True once at least one frame has decoded this session, i.e. the shadow
// buffer holds real pixel data. Guards the erase path: erasing from an
// uninitialized shadow would paint garbage on the idle screen.
static bool shadowValid = false;
#define COLOR_CURSOR_BG   0x0000       // black border
#define COLOR_CURSOR_FILL 0xFFFF       // white fill

// ---------- Wire header accumulator (shared across packets) ----------
static uint8_t hdr[12];
static size_t  hdrGot = 0;

// ---------- GT911 touch ----------
static bool    gt911Ok = false;
static uint8_t gtAddr = 0x5D;
static uint8_t gtErrStreak = 0;
struct TouchPoint { uint8_t id; int16_t x, y; };
static TouchPoint prevPts[MAX_TOUCH_POINTS];
static uint8_t nPrevPts = 0;

// ============================================================
// Decode path: TJpg_Decoder callback -> framebuffer + shadow copy
// ============================================================

// Copy one decoded block into the cursor-free shadow frame.
static void shadowCopy(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!shadowBuf || x < 0 || y < 0) return;
    int16_t maxR = (y + h > SCREEN_HEIGHT) ? SCREEN_HEIGHT - y : h;
    uint16_t maxC = (x + w > SCREEN_WIDTH) ? SCREEN_WIDTH - x : w;
    for (uint16_t r = 0; r < maxR; r++) {
        memcpy(shadowBuf + (uint32_t)(y + r) * SCREEN_WIDTH + x,
               bitmap + (uint32_t)r * w, maxC * 2);
    }
}

// TJpg_Decoder hands over MCU-sized blocks (up to 16 px wide). Blit them
// straight into the panel framebuffer and keep the shadow copy in sync.
// Native byte order both ways (setSwapBytes(false) convention — the same
// one the photo frame uses; swapping here scrambles colors).
static bool decodeBlit(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || (x + w) <= 0 || (y + h) <= 0) return 1;

    if (x >= 0 && (x + w) <= SCREEN_WIDTH && y >= 0 && (y + h) <= SCREEN_HEIGHT) {
        tft.pushImage(x, y, w, h, bitmap);
        shadowCopy(x, y, w, h, bitmap);
        return 1;
    }

    // Partial clipping at the panel edges (only when the JPEG is larger
    // than the panel — the PC client normally sends exactly 800x480).
    int16_t clipX = x < 0 ? -x : 0;
    int16_t clipY = y < 0 ? -y : 0;
    uint16_t drawW = (x + w > SCREEN_WIDTH) ? (SCREEN_WIDTH - x) : (w - clipX);
    uint16_t drawH = (y + h > SCREEN_HEIGHT) ? (SCREEN_HEIGHT - y) : (h - clipY);
    if (drawW == 0 || drawH == 0) return 1;

    for (uint16_t r = 0; r < drawH; r++) {
        uint16_t* src = bitmap + (uint32_t)(clipY + r) * w + clipX;
        tft.pushImage(x + clipX, y + clipY + r, drawW, 1, src);
        if (shadowBuf) {
            memcpy(shadowBuf + (uint32_t)(y + clipY + r) * SCREEN_WIDTH + x + clipX,
                   src, drawW * 2);
        }
    }
    return 1;
}

// Restore a panel region from the shadow frame (used to erase the cursor
// without relying on framebuffer read-back, which Arduino_GFX does not
// guarantee for RGB panels).
static void shadowRestore(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if (!shadowBuf || !shadowValid) return;
    int16_t x0 = max((int16_t)0, x), y0 = max((int16_t)0, y);
    int16_t x1 = min((int16_t)SCREEN_WIDTH, (int16_t)(x + w));
    int16_t y1 = min((int16_t)SCREEN_HEIGHT, (int16_t)(y + h));
    if (x1 <= x0 || y1 <= y0) return;
    for (int16_t r = y0; r < y1; r++) {
        tft.pushImage(x0, r, x1 - x0, 1, shadowBuf + (uint32_t)r * SCREEN_WIDTH + x0);
    }
}

// ============================================================
// Cursor overlay (pointer dot; position comes from the PC via "CARS")
// ============================================================
static void cursorErase() {
    if (curVisible) shadowRestore(curX - 1, curY - 1, CURSOR_BACKUP_W, CURSOR_BACKUP_H);
    curVisible = false;
}

static void cursorDraw() {
    if (!curVisible) return;
    tft.drawRect(curX, curY, CURSOR_SIZE, CURSOR_SIZE, COLOR_CURSOR_BG);
    tft.fillRect(curX + 1, curY + 1, CURSOR_SIZE - 2, CURSOR_SIZE - 2, COLOR_CURSOR_FILL);
    tft.drawPixel(curX + CURSOR_SIZE / 2, curY + CURSOR_SIZE / 2, COLOR_CURSOR_BG);
}

static void cursorSet(int16_t x, int16_t y, bool visible) {
    cursorErase();
    curX = x; curY = y;
    curVisible = visible;
    cursorDraw();
}

// ============================================================
// Screens (idle / status)
// ============================================================
static void drawIdleScreen(const char* line3) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("VIEWE Second Screen", SCREEN_WIDTH / 2, 120, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawCentreString(WiFi.localIP().toString(), SCREEN_WIDTH / 2, 190, 4);
    tft.drawCentreString(line3, SCREEN_WIDTH / 2, 260, 2);
    tft.drawCentreString("secondscreen.local  port " + String(MIRROR_TCP_PORT),
                         SCREEN_WIDTH / 2, 300, 2);
}

static void drawBootScreen(const char* status) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("VIEWE Second Screen", SCREEN_WIDTH / 2, 160, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawCentreString(status, SCREEN_WIDTH / 2, 240, 2);
    tft.drawCentreString("v" FIRMWARE_VERSION, SCREEN_WIDTH / 2, 280, 2);
}

// ============================================================
// GT911 capacitive touch — raw I2C driver (no external library)
// ============================================================
static bool gt911Write(uint16_t reg, const uint8_t* data, size_t len) {
    Wire.beginTransmission(gtAddr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    for (size_t i = 0; i < len; i++) Wire.write(data[i]);
    return Wire.endTransmission(true) == 0;
}

static bool gt911Write1(uint16_t reg, uint8_t val) { return gt911Write(reg, &val, 1); }

static bool gt911Read(uint16_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(gtAddr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;   // repeated start
    if (Wire.requestFrom((uint16_t)gtAddr, (size_t)len, (bool)true) != (size_t)len) return false;
    for (size_t i = 0; i < len; i++) data[i] = Wire.read();
    return true;
}

static void gt911Init() {
    pinMode(TP_RST, OUTPUT);
    pinMode(TP_INT, OUTPUT);
    digitalWrite(TP_INT, HIGH);
    digitalWrite(TP_RST, HIGH);
    delay(50);
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(10);
    pinMode(TP_INT, INPUT);        // release INT; GT911 latched its I2C address
    delay(100);

    Wire.begin((int)TP_SDA, (int)TP_SCL, (uint32_t)TP_I2C_FREQ_HZ);
    Wire.setTimeOut(50);

    // The INT level during reset selects 0x5D vs 0x14 (and the two touch-cable
    // variants differ) — probe both by reading the product ID at 0x8140.
    const uint8_t addrs[2] = {0x5D, 0x14};
    for (int i = 0; i < 2; i++) {
        uint8_t id[4] = {0};
        gtAddr = addrs[i];
        if (gt911Read(0x8140, id, 4) && id[0] == '9' && id[1] == '1' && id[2] == '1') {
            gt911Ok = true;
            LOG_INFO("[TOUCH] GT911 found at 0x%02X (product '%c%c%c')\n",
                     gtAddr, id[0], id[1], id[2]);
            return;
        }
    }
    gt911Ok = false;
    LOG_WARN("[TOUCH] GT911 not responding — touch backchannel disabled\n");
}

// Forward declaration (network section below)
static void netSendTouch(const TouchPoint* pts, uint8_t n, const uint8_t* actions);

// Poll the GT911 and emit down/move/up diffs to the PC.
static void gt911Poll() {
    if (!gt911Ok) return;

    static uint32_t lastTouchPoll = 0;
    uint32_t now = millis();
    if (now - lastTouchPoll < 25) return;   // poll at ~40 Hz (every 25ms) to prevent I2C bus flooding
    lastTouchPoll = now;

    uint8_t st = 0;
    if (!gt911Read(0x814E, &st, 1)) {
        if (++gtErrStreak > 100) {
            LOG_WARN("[TOUCH] I2C stuck, re-initialising GT911\n");
            gt911Init();
            gtErrStreak = 0;
        }
        return;
    }
    gtErrStreak = 0;
    if (!(st & 0x80)) return;                       // buffer not ready

    uint8_t n = st & 0x0F;
    TouchPoint cur[MAX_TOUCH_POINTS];
    uint8_t nCur = 0;
    uint8_t actions[MAX_TOUCH_POINTS];

    if (n > MAX_TOUCH_POINTS) n = MAX_TOUCH_POINTS;
    if (n > 0) {
        uint8_t buf[MAX_TOUCH_POINTS * 8];
        if (!gt911Read(0x8150, buf, (size_t)n * 8)) {
            gt911Write1(0x814E, 0x00);
            return;
        }
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t* p = buf + (size_t)i * 8;
            int16_t x = p[1] | ((p[2] & 0x0F) << 8);
            int16_t y = p[3] | ((p[4] & 0x0F) << 8);
#if TOUCH_SWAP_XY
            int16_t t = x; x = y; y = t;
#endif
#if TOUCH_FLIP_X
            x = SCREEN_WIDTH - 1 - x;
#endif
#if TOUCH_FLIP_Y
            y = SCREEN_HEIGHT - 1 - y;
#endif
            cur[nCur].id = p[0];
            cur[nCur].x = x;
            cur[nCur].y = y;
            nCur++;
        }
    }

    // Build the diff packet: down for new ids, move for continuing ids,
    // up for ids that disappeared since the previous poll.
    TouchPoint out[MAX_TOUCH_POINTS];
    uint8_t nOut = 0;
    for (uint8_t i = 0; i < nCur; i++) {
        bool known = false;
        for (uint8_t j = 0; j < nPrevPts; j++) {
            if (prevPts[j].id == cur[i].id) { known = true; break; }
        }
        if (nOut < MAX_TOUCH_POINTS) { out[nOut] = cur[i]; actions[nOut] = known ? 2 : 1; nOut++; }
    }
    for (uint8_t j = 0; j < nPrevPts && nOut < MAX_TOUCH_POINTS; j++) {
        bool still = false;
        for (uint8_t i = 0; i < nCur; i++) {
            if (cur[i].id == prevPts[j].id) { still = true; break; }
        }
        if (!still) { out[nOut] = prevPts[j]; actions[nOut] = 3; nOut++; }
    }
    if (nOut > 0) netSendTouch(out, nOut, actions);

    memcpy(prevPts, cur, sizeof(TouchPoint) * nCur);
    nPrevPts = nCur;
    gt911Write1(0x814E, 0x00);                      // clear buffer-status flag
}

// ============================================================
// Networking
// ============================================================
static void wifiConfig() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.setSleep(false);            // low latency matters more than power here
}

static bool wifiConnect(const char* statusLine) {
    drawBootScreen(statusLine);
    LOG_INFO("[NET] Connecting to Wi-Fi '%s'", DEFAULT_SSID);
    if (DEFAULT_PASS[0]) WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
    else                 WiFi.begin(DEFAULT_SSID);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println();
            return false;
        }
        delay(250);
        Serial.print('.');
    }
    Serial.println();
    LOG_INFO("[NET] Wi-Fi connected, IP: %s  RSSI: %d dBm\n",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

static bool mdnsStart() {
    MDNS.end();
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("secondscreen", "tcp", MIRROR_TCP_PORT);
        mdnsUp = true;
        LOG_INFO("[NET] mDNS: %s.local, service _secondscreen._tcp:%d\n",
                 MDNS_HOSTNAME, MIRROR_TCP_PORT);
        return true;
    }
    mdnsUp = false;
    LOG_WARN("[NET] mDNS start failed (discovery falls back to the IP)\n");
    return false;
}

static void sendHello() {
    uint8_t pkt[16];
    memcpy(pkt, "RDYT", 4);
    pkt[4]  = SCREEN_WIDTH & 0xFF;         pkt[5]  = SCREEN_WIDTH >> 8;
    pkt[6]  = SCREEN_HEIGHT & 0xFF;        pkt[7]  = SCREEN_HEIGHT >> 8;
    pkt[8]  = PROTO_VER & 0xFF;            pkt[9]  = PROTO_VER >> 8;
    pkt[10] = (gt911Ok ? 0x01 : 0x00) & 0xFF;  pkt[11] = 0;   // caps
    pkt[12] = (uint8_t)(MAX_JPEG_BYTES & 0xFF);
    pkt[13] = (uint8_t)((MAX_JPEG_BYTES >> 8) & 0xFF);
    pkt[14] = (uint8_t)((MAX_JPEG_BYTES >> 16) & 0xFF);
    pkt[15] = (uint8_t)((MAX_JPEG_BYTES >> 24) & 0xFF);
    client.write(pkt, sizeof(pkt));
}

static void netSendTouch(const TouchPoint* pts, uint8_t n, const uint8_t* actions) {
    if (!client || !client.connected() || !gt911Ok || n == 0) return;
    uint8_t pkt[5 + MAX_TOUCH_POINTS * 6];
    memcpy(pkt, "TOCH", 4);
    pkt[4] = n;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t* p = pkt + 5 + (size_t)i * 6;
        p[0] = pts[i].id;
        p[1] = actions[i];
        p[2] = (uint8_t)(pts[i].x & 0xFF);
        p[3] = (uint8_t)((pts[i].x >> 8) & 0xFF);
        p[4] = (uint8_t)(pts[i].y & 0xFF);
        p[5] = (uint8_t)((pts[i].y >> 8) & 0xFF);
    }
    client.write(pkt, 5 + (size_t)n * 6);
}

// Read exactly n bytes with an overall deadline (frame payloads). Feeds the
// watchdog in passing so a slow client can never trip the 8 s TWDT.
static bool readExact(WiFiClient& c, uint8_t* buf, size_t n, uint32_t timeoutMs) {
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < n) {
        // Try to drain any bytes already buffered by lwIP before checking
        // connected(): after an orderly TCP FIN, connected() can become false
        // even though the final payload bytes are still waiting in the RX buffer.
        int r = c.read(buf + got, n - got);
        if (r > 0) { got += (size_t)r; continue; }
        if (!c.connected()) return false;
        if (millis() - t0 > timeoutMs) return false;
        delayMicroseconds(500);
    }
    return true;
}

static void disconnectClient(const char* why) {
    if (why) LOG_INFO("[NET] Client disconnected: %s\n", why);
    cursorErase();
    if (client) client.stop();
    hdrGot = 0;                 // never leak partial header bytes to the next client
    shadowValid = false;        // shadow content is stale once the stream ends
    lastSeenMs = millis();
    framesWindow = 0;
    drawIdleScreen("Waiting for PC - run the mirror client");
}

static void handleOneFrame(uint32_t len) {
    // The 12-byte header (magic + seq + flags + len) was consumed by the
    // caller; the JPEG payload follows immediately. Validate hard:
    if (len == 0 || len > MAX_JPEG_BYTES) {
        LOG_WARN("[NET] Rejected frame, bad length %u\n", (unsigned)len);
        disconnectClient("bad frame length");
        return;
    }
    if (!readExact(client, jpegBuf, len, FRAME_READ_TIMEOUT_MS)) {
        disconnectClient("payload read failed");
        return;
    }

    uint32_t t0 = micros();
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(decodeBlit);
    JRESULT res = TJpgDec.drawJpg(0, 0, jpegBuf, len);
    uint32_t dtUs = micros() - t0;

    if (res != JDR_OK) {
        LOG_WARN("[JPEG] Decode failed (code=%d) - %u bytes\n", (int)res, (unsigned)len);
    } else {
        framesDecoded++;
        framesWindow++;
        bytesTotal += len + 12;
        float ms = dtUs / 1000.0f;
        decodeMsAvg = (decodeMsAvg <= 0.0f) ? ms : decodeMsAvg * 0.9f + ms * 0.1f;
        shadowValid = true;                     // shadow now holds real pixels
        if (curVisible) cursorDraw();           // decoded frame wiped the sprite
    }
    lastSeenMs = millis();
}

static void handleClient(uint32_t now) {
    // Always service pending connections first, so a second client while one
    // is connected gets an explicit "BUSY" instead of hanging in the backlog.
    WiFiClient incoming = server.available();
    if (incoming) {
        if (client && client.connected()) {
            incoming.write((const uint8_t*)"BUSY", 4);
            incoming.stop();
            LOG_INFO("[NET] Second client rejected (busy)\n");
        } else {
            incoming.setNoDelay(true);
            client = incoming;
            LOG_INFO("[NET] Client connected from %s\n",
                     client.remoteIP().toString().c_str());
            sendHello();
            framesDecoded = 0;
            framesWindow = 0;
            bytesTotal = 0;
            decodeMsAvg = 0.0f;
            hdrGot = 0;
            lastSeenMs = now;
        }
    }

    if (!client || !client.connected()) return;

    // Accumulate the 4-byte magic first.
    int r;
    while (hdrGot < 4 && (r = client.read(hdr + hdrGot, 4 - hdrGot)) > 0) {
        hdrGot += (size_t)r;
    }

    if (hdrGot < 4) {
        if (now - lastSeenMs > CLIENT_IDLE_TIMEOUT_MS) disconnectClient("idle timeout");
        return;
    }

    if (memcmp(hdr, "PING", 4) == 0) {
        client.write((const uint8_t*)"PONG", 4);
        lastSeenMs = now;
        hdrGot = 0;
        return;
    }
    if (memcmp(hdr, "BYE!", 4) == 0) {
        hdrGot = 0;
        disconnectClient("client said bye");
        return;
    }
    if (memcmp(hdr, "CARS", 4) == 0) {
        // CARS needs 5 more bytes: visible(1), x(i16), y(i16).
        uint8_t rest[5];
        if (!readExact(client, rest, 5, FRAME_READ_TIMEOUT_MS)) {
            disconnectClient("cursor read failed");
            return;
        }
        int16_t x = (int16_t)((uint16_t)rest[1] | ((uint16_t)rest[2] << 8));
        int16_t y = (int16_t)((uint16_t)rest[3] | ((uint16_t)rest[4] << 8));
        cursorSet(x, y, rest[0] != 0);
        lastSeenMs = millis();
        hdrGot = 0;
        return;
    }
    if (memcmp(hdr, "MJP1", 4) == 0) {
        // MJP1 needs 8 more bytes: seq(2), flags(2), len(4, LE).
        uint8_t rest[8];
        if (!readExact(client, rest, 8, FRAME_READ_TIMEOUT_MS)) {
            disconnectClient("frame header read failed");
            return;
        }
        uint32_t len = (uint32_t)rest[4] | ((uint32_t)rest[5] << 8) |
                       ((uint32_t)rest[6] << 16) | ((uint32_t)rest[7] << 24);
        hdrGot = 0;
        handleOneFrame(len);
        return;
    }

    LOG_WARN("[NET] Unknown packet magic %02X %02X %02X %02X - closing\n",
             hdr[0], hdr[1], hdr[2], hdr[3]);
    hdrGot = 0;
    disconnectClient("protocol desync");
}

static void printStats(uint32_t now) {
    if (now - statsLastMs < STATS_INTERVAL_MS) return;
    statsLastMs = now;
    float fps = framesWindow * (1000.0f / STATS_INTERVAL_MS);
    float mbps = bytesTotal * 8.0f / STATS_INTERVAL_MS / 1000.0f;
    framesWindow = 0;
    if (client && client.connected()) {
        LOG_INFO("[STAT] fps=%.1f  decode=%.1fms  %.0f kbps  total=%u\n",
                 fps, decodeMsAvg, mbps, (unsigned)framesDecoded);
    } else if (WiFi.status() != WL_CONNECTED) {
        LOG_INFO("[NET] Wi-Fi down, waiting to retry\n");
    }
}

// ============================================================
// Setup / Loop
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n==============================================");
    Serial.printf("  VIEWE 5\" Wireless Second Screen  v%s\n", FIRMWARE_VERSION);
    Serial.println("==============================================");

    // Log-only watchdog: deinit first (the Arduino core may have installed a
    // panic-enabled TWDT), then re-arm it so a busy transfer can never reboot.
    esp_task_wdt_deinit();
    esp_task_wdt_init(WDT_TIMEOUT_S, false);
    esp_task_wdt_add(NULL);

    tft.begin();
    tft.setRotation(0);
    tft.setSwapBytes(false);
    tft.fillScreen(TFT_BLACK);
    tft.setBrightness(255);

#if HAS_DEFAULT_CREDENTIALS
    drawBootScreen("Connecting Wi-Fi...");
    wifiConfig();
    if (!wifiConnect(DEFAULT_SSID[0] ? "Connecting Wi-Fi..." : "No SSID in secrets.h!")) {
        drawBootScreen("Wi-Fi failed - retrying every 15s");
    }
    if (WiFi.status() == WL_CONNECTED) {
        mdnsStart();
        server.begin();
        server.setNoDelay(true);
        drawIdleScreen("Waiting for PC - run the mirror client");
    } else {
        drawIdleScreen("Wi-Fi failed - retrying");
    }
#else
    drawBootScreen("No Wi-Fi credentials!");
    LOG_ERROR("[NET] Copy include/secrets.h.example to include/secrets.h and fill in Wi-Fi credentials\n");
#endif

    gt911Init();
    if (!jpegBuf) jpegBuf = (uint8_t*)ps_malloc(MAX_JPEG_BYTES);
    if (!shadowBuf) shadowBuf = (uint16_t*)ps_malloc((uint32_t)SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    if (!jpegBuf || !shadowBuf) {
        LOG_ERROR("[MEM] PSRAM allocation failed (jpeg=%p shadow=%p)\n", jpegBuf, shadowBuf);
        drawBootScreen("PSRAM allocation failed!");
        while (true) delay(1000);
    }
    LOG_INFO("[SYSTEM] Ready: jpeg %u KB, shadow %u KB in PSRAM\n",
             (unsigned)(MAX_JPEG_BYTES / 1024),
             (unsigned)(SCREEN_WIDTH * SCREEN_HEIGHT * 2 / 1024));
    lastSeenMs = millis();
    statsLastMs = millis();
}

void loop() {
    uint32_t now = millis();

    static uint32_t lastWdtFeed = 0;
    if (now - lastWdtFeed >= 1000) {
        lastWdtFeed = now;
        esp_task_wdt_reset();
    }

    // Keep Wi-Fi alive; re-arm server + mDNS after a reconnect. The retry is
    // a non-blocking state machine so GT911 polling never stalls (and the
    // watchdog is always fed on schedule).
    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t lastRetry = 0;
        static bool waiting = false;
        static uint32_t waitStart = 0;
        static bool shownIdle = false;

        if (client && client.connected()) disconnectClient("Wi-Fi lost");
        if (!shownIdle) { drawIdleScreen("Wi-Fi reconnecting..."); shownIdle = true; }

        if (!waiting) {
            if (now - lastRetry > WIFI_RETRY_INTERVAL_MS) {
                lastRetry = now;
                WiFi.disconnect();
                wifiConfig();
                if (DEFAULT_PASS[0]) WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
                else                 WiFi.begin(DEFAULT_SSID);
                waiting = true;
                waitStart = now;
                LOG_INFO("[NET] Wi-Fi retry...\n");
            }
        } else if (WiFi.status() == WL_CONNECTED) {
            waiting = false;
            LOG_INFO("[NET] Wi-Fi back, IP: %s\n", WiFi.localIP().toString().c_str());
            mdnsStart();
            server.end();
            server.begin();
            server.setNoDelay(true);
            drawIdleScreen("Waiting for PC - run the mirror client");
            shownIdle = false;
            lastSeenMs = millis();
        } else if (now - waitStart > 12000) {
            // Attempt timed out; WiFi.begin stays pending, so drop it and
            // wait for the next retry interval.
            waiting = false;
            WiFi.disconnect();
        }
    } else {
        handleClient(now);
    }

    gt911Poll();
    printStats(now);
}
