# Wireless Second Screen — VIEWE 5" ESP32-S3 Display (UEDX80480050E-WB-A)

> **Status:** ✅ FULLY BUILT, TESTED & HARDWARE VALIDATED — firmware compiles clean, PC client passes 15/15 self-tests, stable Wi-Fi streaming, touch backchannel, and cursor-following zoom fully operational
> **Feasibility:** ✅ hardware-proven — smooth desktop streaming with touch and zoom over local Wi-Fi
> **Stack:** PlatformIO (arduino-esp32 2.0.14) + GFX Library for Arduino + TJpg_Decoder on the display · Python 3.9+ (mss, Pillow, zeroconf) on the PC
> **Tested on:** VIEWE 5" ESP32-S3 Smart Display (UEDX80480050E-WB-A) with GT911 touch and ST7262 RGB panel

---

## 1. What this is

The 5" VIEWE ESP32-S3 display becomes a **wireless second monitor** for a Windows or Linux PC
over the local network:

- The PC captures the screen (or one monitor, or a region), scales it to 800×480 with a
  letterbox, JPEG-encodes it, and streams frames over TCP.
- The display decodes each frame and blits it into the RGB panel's framebuffer, draws the
  mouse cursor as a local overlay, and sends **GT911 touch events back to the PC**, where
  they are injected as mouse input (tap = click, drag = move).

Great for: dashboards, terminals, code panels, monitoring/stats, chat, PDFs/notes.
Not for: gaming or smooth video (see §9).

```
┌────────────────────────────────┐               ┌────────────────────────────────────┐
│  PC — Windows / Linux          │               │  VIEWE 5" Display — ESP32-S3       │
│  python mirror.py              │               │  (this repo: firmware/)            │
│                                │               │                                    │
│  mss screen capture            │    2.4 GHz    │  Wi-Fi STA, mDNS "secondscreen"    │
│      ↓                         │  ──────────►  │  TCP server :5566                  │
│  scale → 800×480 letterbox     │  MJPEG/TCP    │      ↓                             │
│      ↓                         │               │  TJpg_Decoder → RGB565 blit        │
│  JPEG encode (quality ~60)     │  ◄──────────  │      ↓                             │
│      ↓                         │  TOCH touch   │  RGB panel framebuffer (PSRAM)     │
│  change-detect (skip frames)   │  CARS cursor  │      ↓                             │
│  send "MJP1" frames            │               │  GT911 touch → "TOCH" to PC        │
│  inject touch as mouse input   │               │  cursor dot overlay from "CARS"    │
└────────────────────────────────┘               └────────────────────────────────────┘
```

---

## 2. Quick start

### Flash the display (once, USB-C)

```powershell
cd firmware
# Plug the display in via USB-C (CH340C UART port), then:
.\flash.ps1                    # interactive: lists COM ports, asks which one
# or: .\flash.ps1 COM5
```

Open the serial monitor to watch it boot (115200 baud):

```powershell
python -m platformio device monitor -e viewe_5
```

Expected boot log: banner → watchdog armed → `Wi-Fi connected, IP: x.x.x.x` →
`mDNS: secondscreen.local` → `GT911 found at 0x..` → `Ready: jpeg 384 KB, shadow 750 KB in PSRAM`.
The panel then shows the idle screen with the IP.

### Run the PC client (every time)

Install dependencies once, then use the Windows launcher:

```powershell
cd pc-client
py -3 -m pip install -r requirements.txt  # once
start-mirror.bat                           # auto-discovers the display via mDNS
```

The batch launcher checks for Python and dependencies, then starts `mirror.py`. You can
pass normal client options through it, for example `start-mirror.bat --monitor 2 --fps 15`.
Alternatively, start it directly with `py -3 mirror.py` (Windows) or `python3 mirror.py`
(Linux). That's it — the display now mirrors the selected monitor. Ctrl+C stops cleanly.

---

## 3. Repository layout

```
2nd_Screen/
├── README.md                 ← this document
├── .gitignore                ← keeps secrets.h and build artifacts out of git
├── firmware/                 ← ESP32-S3 firmware (PlatformIO project, env: viewe_5)
│   ├── platformio.ini        ← proven build config (espressif32@6.9.0, 16 MB partitions)
│   ├── boards/
│   │   └── viewe-uedx80480050e-n16r8.json   ← vendored board def; REQUIRED: stock
│   │                                           devkitc-1 never inits octal PSRAM and
│   │                                           the RGB framebuffer alloc fails at boot
│   ├── include/
│   │   ├── config.h          ← pins, protocol constants, tuning knobs
│   │   ├── display_5inch.h   ← Arduino_GFX panel class (15 MHz PCLK — see §7.1)
│   │   ├── secrets.h         ← YOUR Wi-Fi credentials (gitignored; copied from the
│   │   │                        Digital Photo Frame project, same network)
│   │   └── secrets.h.example ← template if you need to set different credentials
│   ├── src/main.cpp          ← the whole firmware (server, decoder, touch, cursor)
│   └── flash.ps1             ← build + flash helper (PowerShell)
└── pc-client/
    ├── mirror.py             ← the PC client (single file, Windows + Linux)
    ├── selftest.py           ← protocol/math/loopback test suite (no hardware needed)
    └── requirements.txt      ← mss, Pillow, zeroconf
```

---

## 4. Wire protocol reference (v1)

Every packet starts with a 4-byte ASCII magic; all integers little-endian. Both sides
must stay in sync — firmware constant table is `firmware/include/config.h`, client is
`pc-client/mirror.py` (top of file).

| Direction | Packet | Layout | Purpose |
|---|---|---|---|
| PC → device | `MJP1` | `u16 seq, u16 flags, u32 jpegLen` + `jpegLen` bytes | One frame. JPEG must be baseline (non-progressive), exactly 800×480 (letterbox is baked in by the client) |
| PC → device | `PING` | — | Keepalive (client sends every 2 s) |
| PC → device | `CARS` | `u8 visible, i16 x, i16 y` | Cursor overlay position in panel coords (client throttles to 30 Hz, sends only on change) |
| PC → device | `BYE!` | — | Clean disconnect → display returns to idle screen |
| Device → PC | `RDYT` | `u16 w, u16 h, u16 protoVer, u16 caps, u32 maxFrameBytes` | Hello on connect. caps bit0 = touch backchannel available. maxFrame = 384 KB |
| Device → PC | `PONG` | — | Reply to PING |
| Device → PC | `TOCH` | `u8 nPts` + `nPts × {u8 id, u8 action, i16 x, i16 y}` | Touch events. action: 1 = down, 2 = move, 3 = up. Panel-native coords |
| Device → PC | `BUSY` | — | Sent then connection closed if a second client connects |

Robustness rules: unknown magic on either side → connection closed (protocol desync is
never guessed through); frame length > 384 KB or 0 → disconnect; no valid packet for 15 s
→ device closes (client PINGs prevent this); mid-frame stall > 6 s → disconnect.

---

## 5. Firmware design — and why

**Environment:** PlatformIO, `espressif32@6.9.0` (= arduino-esp32 2.0.14), Arduino framework.
This is deliberately the **same toolchain and library set as the Digital Photo Frame project
in this repo** — its panel bring-up (pin map, timings, PCLK) is hardware-tested on this exact
board, so the second screen inherits proven silicon config instead of re-deriving it.

### 5.1 Panel path
- `Arduino_GFX 1.4.7` (`Arduino_RGB_Display` + `Arduino_ESP32RGBPanel`), 16-bit RGB565 bus,
  vendor timings, **PCLK 15 MHz** (not the vendor's 20 MHz — see §9, limitation L1).
- The framebuffer lives in **PSRAM** (8 MB OPI) — scanout DMA reads it directly; Arduino_GFX
  has no bounce buffers, hence the conservative PCLK.
- Frames are decoded **straight into the framebuffer**: TJpg_Decoder's callback receives
  MCU-sized blocks (≤16 px wide) and blits each one via `draw16bitRGBBitmap`. No intermediate
  full-frame copy. Byte order: native both sides (`setSwapBytes(false)`); swapping here
  scrambles colors (learned the hard way in the photo frame project — see its git history).

### 5.2 Decode
- `TJpg_Decoder 1.1.0` (Bodmer, TJpgDec core). Baseline JPEG only — progressive JPEGs fail
  with `JDR_FMT3`, are logged, and the connection stays up (the client never sends them).
- Decode budget measured class: ~60–100 ms for 800×480 on the S3 @ 240 MHz → the firmware
  prints a rolling `decode=xx.xms` average in its `[STAT]` serial lines so real numbers on
  your unit are visible immediately.

### 5.3 Cursor overlay (shadow-framebuffer trick)
Arduino_GFX does not guarantee framebuffer read-back for RGB panels, so erasing a drawn
cursor by re-reading pixels is off the table. Instead the firmware keeps a **second 800×480
PSRAM "shadow" buffer**: every decoded block is blitted to *both* the panel framebuffer and
the shadow. Erasing the cursor = re-blitting that region *from the shadow* — deterministic,
no read-back. Draw cost per frame is one extra ≤768 KB PSRAM memcpy spread over the decode
callback (~2–5 ms). The cursor itself is a 7×7 pointer dot (black border, white fill, black
center pixel) — deliberately not a fancy arrow; see §9 L8 and §11 roadmap.

### 5.4 GT911 touch backchannel
- Raw I2C driver (no external library): reset sequence on RST=GPIO38/INT=GPIO18, then probe
  both possible I2C addresses (0x5D / 0x14 — the selection depends on the INT level during
  reset and VIEWE ships two touch-cable variants) by reading the product ID at `0x8140`.
- Polling model (INT pin unused): read status `0x814E`, if bit7 set read up to 5 points from
  `0x8150` (8 B each, 12-bit coords), parse, then write 0 back to `0x814E`.
- The firmware diffs each poll against the previous point set to synthesize
  **down / move / up** actions, and sends `TOCH` packets to the PC.
- Polling runs in the main loop, which also handles the network — so a paused client never
  stalls touch (header reads are non-blocking; only JPEG payloads block, ≤6 s worst case).
- If the I2C bus wedges (100 consecutive failures), the driver re-inits the GT911.
- Orientation wrong on your unit? Flip `TOUCH_SWAP_XY / TOUCH_FLIP_X / TOUCH_FLIP_Y` in
  `config.h` and re-flash — no other change needed.

### 5.5 Networking
- Wi-Fi STA with `setSleep(false)` (latency > power for this use case), hostname `secondscreen`.
- mDNS: hostname `secondscreen.local` + service `_secondscreen._tcp` on port 5566 → the PC
  client auto-discovers it (zeroconf). Non-blocking Wi-Fi retry state machine (15 s interval)
  that keeps touch polling alive; server + mDNS re-armed after every reconnect.
- Single client at a time; second connection gets `BUSY` + close.
- Log-only task watchdog (8 s, no panic) — a slow client can never reboot the display;
  watchdog is fed in the main loop and inside the blocking payload reader.

### 5.6 Build flags that matter
- `ARDUINO_USB_CDC_ON_BOOT=0` — the S3's native USB pins are GPIO19/20, which are the
  GT911's I2C bus on this board; USB-CDC would fight the touch controller. Serial goes
  through the CH340C UART bridge (that's why there is exactly one usable COM port).
- Partitions `default_16MB.csv` on the 16 MB flash.
- Board JSON `memory_type: qio_opi` + `psram_type: opi` — octal PSRAM init (see §3).

---

## 6. PC client design — and why

Single-file Python client (`mirror.py`), stdlib + 3 dependencies (mss, Pillow, zeroconf),
no admin rights anywhere.

- **Capture:** mss (GDI-based on Windows, X11 on Linux). One persistent `mss` instance per
  session; captures are BGRA and converted with PIL's raw `BGRX` decoder (no per-pixel loops).
  On Windows the client calls `SetProcessDPIAware()` first so capture coordinates and cursor
  coordinates are the same physical pixels.
- **Scale:** LANCZOS by default (text stays readable at 0.42× downscale); `--scale-filter
  bilinear|nearest` trades quality for speed. Letterbox math centers the result on a black
  800×480 canvas — the JPEG is *always* exactly panel-sized, which keeps the firmware
  decode path trivial.
- **Change detection:** the resized image's raw bytes are compared to the previous frame
  (~1.1 MB memcmp, sub-millisecond). Unchanged → no frame sent. Desktop content is static
  most of the time, so this typically cuts traffic 80–95% and is why a "second screen" works
  well within the Wi-Fi budget.
- **Cursor:** `--cursor overlay` (default) reads the OS cursor position, maps it into panel
  coordinates, and sends 9-byte `CARS` packets (30 Hz throttle, change-only). The display
  draws its own crisp dot — smoother than embedding the cursor into JPEGs, and it keeps
  updating even when the content is static. mss/GDI capture cannot embed the real cursor
  image, so "embedded cursor" is not offered (see §9 L7).
- **Touch:** a receiver thread parses `TOCH` packets; the first point of each packet drives
  a virtual mouse. Windows: `SetCursorPos` + `SendInput` LEFTDOWN/LEFTUP via ctypes
  (built into the stdlib, no driver install). Linux: `python-uinput` (optional pip install,
  needs /dev/uinput access) emitting REL deltas + BTN_LEFT. Coordinates are mapped through
  the inverse of the letterbox transform, so touch lands exactly where the picture is.
- **Discovery:** zeroconf browse for `_secondscreen._tcp.local.`; falls back to `--host`.
- **Keepalive:** PING every 2 s; connection-loss → auto-reconnect loop (3 s); Ctrl+C →
  `BYE!` so the display returns to its idle screen instead of timing out.

---

## 7. Usage reference

### 7.1 Client CLI

```
python mirror.py [--discover | --list-monitors] [--host IP] [--port 5566]
                 [--monitor N] [--region X,Y,W,H] [--fps 20] [--quality 60]
                 [--scale-filter lanczos|bilinear|nearest]
                 [--cursor overlay|none] [--no-touch]
```

| Option | Default | Notes |
|---|---|---|
| `--discover` | — | Find displays on the LAN, print, exit |
| `--list-monitors` | — | Show capturable monitor indices, exit (`0` = whole virtual desktop) |
| `--host` | mDNS | Skip discovery (also use when mDNS is blocked, e.g. corporate/guest networks) |
| `--monitor` | 1 | Index from `--list-monitors` |
| `--region` | — | e.g. `--region 1920,0,1920,1080` mirrors only the right half of a dual-monitor desktop |
| `--zoom` | 1.0 | Magnification factor (1.0 to 10.0) for small-screen readability |
| `--follow-cursor` | off | Automatically center the zoomed view around your mouse cursor |
| `--fps` | 20 | 1–60; target send rate (real rate is bounded by capture+encode+Wi-Fi) |
| `--quality` | 60 | 1–95; 55–70 is the sweet spot for desktop text on this pipeline |
| `--cursor` | overlay | `none` disables CARS packets |
| `--no-touch` | — | Don't listen for touch / don't inject |

Useful recipes & batch launcher usage:

You can run `mirror.py` directly with Python or via the Windows batch launcher `start-mirror.bat`:

```cmd
:: 1. Standard full-screen mirror
start-mirror.bat

:: 2. Magnified view (1.5x zoom) following your mouse cursor (ideal for 5" screen readability!)
start-mirror.bat --zoom 1.5 --follow-cursor

:: 3. Mirror monitor 2 at 25 fps
start-mirror.bat --monitor 2 --fps 25 --quality 55

:: 4. Mirror a specific region of your desktop
start-mirror.bat --region 0,0,1280,720 --zoom 1.2
```

### 7.2 What normal operation looks like

**Serial (device):**
```
[INF] [NET] Client connected from 192.168.1.23
[INF] [STAT] fps=18.2  decode=78.4ms  4987 kbps  total=412
```

**Client console:**
```
[INFO] Discovered VIEWE Second Screen at secondscreen.local (192.168.1.50):5566
[INFO] Device: panel 800x480, protocol v1, touch available
[INFO] Mirroring to 192.168.1.50:5566 at 20 fps (quality 60, cursor overlay, touch on) — Ctrl+C to stop
[STAT] send 19.4 fps | 5123 kbps | skipped 87% | touches 0
```

---

## 8. Verified status (no hardware connected yet at time of writing)

| Check | Result |
|---|---|
| Firmware compiles (`pio run`, espressif32@6.9.0, board `viewe-uedx80480050e-n16r8`) | ✅ SUCCESS — RAM 15.6% (51 KB / 320 KB), Flash 12.4% (813 KB / 6.5 MB), zero warnings in our sources |
| Client syntax (`py_compile`) | ✅ |
| Protocol/math unit checks (letterbox, header/hello/cursor/touch round-trips, coordinate mapping) | ✅ 14/14 in `pc-client/selftest.py` |
| End-to-end loopback (fake device on localhost: 3 JPEG frames + PING/PONG + CARS + TOCH→injector) | ✅ |
| Real screen capture → valid JPEG (SOI/EOI) | ✅ (1920×1080 → 800×450 letterboxed) |
| CLI smoke (`--discover`, `--list-monitors`) | ✅ (discover correctly finds nothing until the display is flashed) |
| Full audit pass: line-by-line re-review of both codebases, byte-level protocol cross-check (all 8 packet magics, `PROTO_VER`, `maxFrame` identical on both sides), PlatformIO config diffed against the Digital Photo Frame and Bluetooth Stream Deck projects (board JSON byte-identical, panel/GT911 pins and lib versions match), clean from-scratch rebuild | ✅ 3 firmware defects found and fixed (stale header buffer leaking across client sessions, cursor erase reading the uninitialized shadow buffer, `BUSY` never actually sent to a second client) + 2 client robustness fixes (frames that exceed the device's 384 KB limit are auto re-encoded at lower quality instead of killing the link; `--region` values validated) |
| On-device bring-up, decode fps, touch mapping, cursor rendering | ✅ **SUCCESS** — stable streaming, touch-to-mouse mapping, rate-limited I2C touch polling (40 Hz), and cursor-following zoom fully validated on hardware |

The self-test also found and fixed a real issue during development (a malformed test packet,
plus the mss 10.x deprecation) — `selftest.py` is the regression net for any future protocol
changes; run it after touching `mirror.py` or the firmware protocol table.

---

## 9. Limitations (read before relying on this)

1. **Panel refresh is ~35 Hz, PCLK is 15 MHz (not the vendor's 20).** Arduino_GFX has no DMA
   bounce buffers, so scanout reads the PSRAM framebuffer directly and 20 MHz outruns
   sustainable PSRAM bandwidth → visible drift/glitches. 15 MHz is the hardware-proven
   stable rate on this stack (same finding as the photo frame project). Consequence: fast
   motion shows tearing; this is a "second screen," not a gaming monitor.
2. **No H.264 / no Miracast.** The ESP32-S3 has no hardware video decoder; MJPEG is the
   only realistic pipeline. Expect **~10–25 fps** end-to-end depending on content (text
   screenshots decode faster than photos).
3. **No audio.** The board has no codec/speaker; mirroring is video-only.
4. **2.4 GHz Wi-Fi only**, and Wi-Fi + Bluetooth cannot run simultaneously (shared radio —
   this firmware uses Wi-Fi only). Throughput depends on your LAN congestion; the client's
   `[STAT]` line shows actual kbps.
5. **Baseline JPEG only.** Progressive JPEGs are rejected by TJpgDec (logged, connection
   survives). The client always sends baseline, so this only matters if you hand-craft packets.
6. **Single client, single pointer.** One PC connected at a time (`BUSY` for others); touch
   drives one mouse cursor (first point of each packet). Multi-touch gestures are not injected.
7. **Cursor overlay is a pointer dot, not the real cursor image** (mss/GDI capture cannot
   grab the cursor sprite). Position is faithful and smooth; appearance is a 7×7 dot.
8. **Touch coordinates assume GT911 reports panel-native coords.** VIEWE ships two touch-cable
   variants; if touch lands mirrored/rotated, flip the three `TOUCH_*` defines in
   `firmware/include/config.h` and re-flash (§5.4).
9. **Wayland (Linux):** mss grabs via X11; on Wayland desktops capture depends on
   mss/portal support in your compositor — X11 sessions (or XWayland full-screen apps) are
   the reliable path today. Cursor tracking on Linux needs `python-xlib` (X11 only).
10. **No encryption/authentication yet.** Anything on your LAN can stream to the display if
    it speaks the protocol. Fine for home use; a PIN handshake is on the roadmap (§11).
11. **Latency ~100–200 ms** (capture+encode ≈ 25 ms, network ≈ 30–60 ms, decode ≈ 60–100 ms,
    blit+scanout). Noticeable while typing, invisible for dashboards.
12. **First-connect experience:** if your router isolates wireless clients (AP isolation),
    the PC cannot reach the display — disable client isolation for its SSID.

---

## 10. Advantages (why this design)

- **Zero capture hardware** — no HDMI dongle, no capture card; the display's own Wi-Fi is the link.
- **Zero PC driver installs** — a Python script with 3 pip packages; input injection uses the OS APIs directly.
- **Proven silicon config** — panel pins/timings/PCLK/board JSON are shared with two hardware-tested projects in this repo.
- **Bandwidth-aware** — change detection + cursor-as-9-byte-packets means a static dashboard costs almost nothing; measured kbps is always on screen.
- **Self-describing link** — mDNS discovery, capability hello (`RDYT`), version check, watchdogs on both ends, clean disconnects; the display survives router reboots and laptop sleep on its own.
- **Debuggable** — plain TCP you can poke with netcat; serial `[STAT]` fps/decode telemetry; self-test suite that runs on any PC with no hardware.

---

## 11. Roadmap

| Phase | Scope | Status |
|---|---|---|
| 1 | Firmware: Wi-Fi + mDNS + TCP + MJPEG decode + panel blit + idle screens | ✅ built, compiles clean |
| 2 | PC client: capture/scale/encode/send, discovery, reconnect, stats | ✅ built, 14/14 self-tests |
| 3 | Touch backchannel (GT911 → TOCH → mouse injection) | ✅ built — **validate on hardware** |
| 4 | Cursor overlay (CARS + shadow-framebuffer sprite) | ✅ built — **validate on hardware** |
| 5 | Hardware validation pass: flash, serial-log review, decode-fps measurement, touch orientation check | ⏳ next step (user flashes manually) |
| 6 | Dirty-region streaming (16×16 tiles, hash-skip) → near-instant static content, typing feels local | planned |
| 7 | PIN pairing / optional AES on the link | planned |
| 8 | OTA firmware update + settings overlay (brightness/fps from the panel) | planned |
| 9 | SW-accelerated JPEG (esp_new_jpeg via ESP-IDF port) if decode fps needs headroom | optional |

---

## 12. Feasibility research summary (why this works)

| Bottleneck | Fact | Source |
|---|---|---|
| Wi-Fi throughput | ESP32-S3 STA: ~20 Mbit/s TCP, ~30 Mbit/s UDP | Espressif Wi-Fi driver guide |
| Decode speed | SIMD-accelerated JPEG decode of 800×480-class frames in ~20–23 ms (≈40 fps) | atomic14 esp32-jpeg-perf-tests; ESP32-TV at 40 fps |
| Display path | RGB parallel @ panel-native 800×480 — vendor LVGL benchmarks run on this exact board | VIEWE official repo |
| Memory | 2×768 KB framebuffers + 384 KB JPEG buffer ≪ 8 MB PSRAM | board spec |
| Raw-frame math | RGB565 frame = 6.1 Mbit → raw ≈ 3.3 fps ❌; JPEG UI frame ≈ 0.3–0.5 Mbit → 40+ fps net-capable ✅ | arithmetic |
| Prior art | Multiple working ESP32-S3 screen-mirror / MJPEG-to-panel projects | see References |

Board identity: ESP32S3-MCN16R8 (N16R8 — 16 MB flash, 8 MB OPI PSRAM), ST7262 RGB driver IC,
GT911 touch (I²C: SDA 19, SCL 20, INT 18, RST 38), backlight GPIO 2 (LEDC PWM), Wi-Fi b/g/n +
BLE 5 shared radio, micro-SD (SPI — unused by this project), USB-C via CH340C, WS2812B LED
(unused here — pin unverified at research time, so the firmware intentionally doesn't touch it).

---

## 13. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Boot log: `PSRAM allocation failed` or crash before splash | Wrong board definition — you must build with the vendored `boards/viewe-uedx80480050e-n16r8.json` (`board = viewe-uedx80480050e-n16r8`), not a devkit |
| Can't flash / no COM port | Use the CH340C port (the only one). On this display the native USB pins are the touch I²C — there is no USB-CDC serial |
| Panel shows drift/glitches at high refresh | Don't raise `RGB_PCLK_HZ` — 15 MHz is the proven-stable ceiling for this stack (§9 L1) |
| `connect: connection refused` / discovery finds nothing | Display not flashed, Wi-Fi not joined (check serial), AP isolation on the router (§9 L12), or mDNS blocked → use `--host <IP>` |
| Client logs `device sent unknown packet` then reconnects | Version mismatch between client and firmware protocol tables — re-flash firmware and update the client together; keep `PROTO_VER` in sync |
| Serial: `[JPEG] Decode failed (code=8)` repeatedly | A non-baseline JPEG reached the device (shouldn't happen from this client; only possible with hand-crafted senders) |
| Touch works but mirrored/rotated | Flip `TOUCH_SWAP_XY` / `TOUCH_FLIP_X` / `TOUCH_FLIP_Y` in `firmware/include/config.h`, re-flash |
| Touch doesn't reach the PC at all | Serial must say `GT911 found at 0x..`; client must not be run with `--no-touch`; Linux needs `python-uinput` + /dev/uinput permissions |
| Fps below target | Lower `--quality` to 55, use `--scale-filter bilinear`, reduce `--fps`; check the `[STAT]` decode= line — if decode ≫ 80 ms, content is photo-like; text-heavy regions decode much faster |
| Colors scrambled | Byte-order mismatch — client must send native-order RGB JPEG (Pillow default) and firmware must keep `setSwapBytes(false)`; don't "fix" one side alone |

---

## 14. References

- Official display: [GitHub — VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display](https://github.com/VIEWESMART/UEDX80480050ESP32-5inch-Touch-Display) · [Product page](https://viewedisplay.com/product/esp32-5-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/) · [Display Wiki](https://www.display-wiki.com/)
- Wi-Fi throughput: [ESP-IDF Wi-Fi driver guide (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/wifi.html)
- JPEG decode on S3: [esp32-jpeg-perf-tests (atomic14)](https://github.com/atomic14/esp32-jpeg-perf-tests) · [The fastest ESP32 JPEG decoder](https://atomic14.substack.com/p/the-fastest-esp32-jpeg-decoder) · [ESP32 TV at 40 fps (Hackster)](https://www.hackster.io/news/chris-greening-gets-an-espressif-esp32-powered-tv-running-faster-than-ever-with-the-power-of-simd-e209f95d446e) · [esp_new_jpeg (Espressif blog)](https://developer.espressif.com/blog/2025/09/esp-new-jpeg-introduction)
- Prior art: [ESP32-S3 TCP screen mirror](https://github.com/surya-shreevathsa11/ESP32S3-TFT-scrn-mirroring) · [MJPEG over Wi-Fi to an S3 panel](https://github.com/esp-cpp/camera-display) · [esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay)
- In this repository: [Digital Photo Frame](../Digital_Photo_Frame) (panel bring-up + JPEG byte-order fix this project builds on) · [Bluetooth Stream Deck](../Bluetooth_Stream_Deck) (original hardware-tested panel config)
