#!/usr/bin/env python3
"""
PC-side mirror client for the VIEWE UEDX80480050E 5" ESP32-S3 Wireless Second
Screen. Captures the screen, scales it to 800x480 (letterboxed), encodes
MJPEG, and streams it over TCP to the display. GT911 touch events from the
display are mapped back onto the captured region and injected as mouse input.

Protocol (must stay in sync with firmware/src/include/config.h):

  PC -> ESP32:
    "MJP1" u16 seq u16 flags u32 jpegLen   + jpegLen bytes (800x480 baseline JPEG)
    "PING"
    "BYE!"
    "CARS" u8 visible i16 x i16 y          (cursor overlay position, panel coords)
  ESP32 -> PC:
    "RDYT" u16 width u16 height u16 protoVer u16 caps u32 maxFrameBytes
    "PONG"
    "TOCH" u8 nPts * { u8 id u8 action i16 x i16 y }   (1=down 2=move 3=up)
    "BUSY"

All integers little-endian.

Usage:
  python mirror.py --discover
  python mirror.py --list-monitors
  python mirror.py                       (auto-discover, mirror monitor 1)
  python mirror.py --host 192.168.1.50 --fps 20 --quality 60

Requires: Python 3.9+; pip install -r requirements.txt
  Windows: nothing extra (input injection is built-in via ctypes)
  Linux:   python-uinput for touch injection (optional), python-xlib for the
           cursor overlay (optional); X11 session recommended, Wayland works
           with pipewire-based desktops that mss/portal can grab (see README).
"""

import argparse
import io
import socket
import struct
import sys
import time
import warnings

PROTOCOL_VERSION = 1
PANEL_W, PANEL_H = 800, 480
DEFAULT_PORT = 5566
MAX_JPEG_BYTES = 384 * 1024

MAGIC_FRAME = b"MJP1"
MAGIC_HELLO = b"RDYT"
MAGIC_TOUCH = b"TOCH"
MAGIC_CURSOR = b"CARS"
MAGIC_PING = b"PING"
MAGIC_PONG = b"PONG"
MAGIC_BYE = b"BYE!"
MAGIC_BUSY = b"BUSY"

CAPS_TOUCH = 0x01

TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP = 1, 2, 3


# ============================================================
# Protocol helpers (pure functions — unit-tested by selftest.py)
# ============================================================

def build_frame_header(seq: int, jpeg_len: int) -> bytes:
    return struct.pack("<4sHHI", MAGIC_FRAME, seq & 0xFFFF, 0, jpeg_len)


def new_mss():
    """mss factory. mss 10 renamed mss.mss -> mss.MSS and deprecated the old
    factory; this wrapper keeps both supported without warning spam."""
    import mss
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        return mss.mss()


def build_cursor(visible: bool, x: int, y: int) -> bytes:
    return struct.pack("<4sBhh", MAGIC_CURSOR, 1 if visible else 0, int(x), int(y))


def build_hello(w: int, h: int, caps: int, max_frame: int) -> bytes:
    """Device-side hello — used by the selftest's fake device."""
    return struct.pack("<4sHHHHI", MAGIC_HELLO, w, h, PROTOCOL_VERSION, caps, max_frame)


def parse_hello(pkt: bytes):
    """Returns (width, height, proto_ver, caps, max_frame_bytes)."""
    if len(pkt) != 16:
        raise ValueError(f"hello packet is {len(pkt)} bytes, expected 16")
    magic, w, h, ver, caps, max_frame = struct.unpack("<4sHHHHI", pkt)
    if magic != MAGIC_HELLO:
        raise ValueError(f"bad hello magic {magic!r}")
    return w, h, ver, caps, max_frame


def parse_touch(pkt: bytes):
    """Parse 'TOCH' body (count byte already included). Returns list of
    (id, action, x, y)."""
    n = pkt[0]
    points = []
    for i in range(n):
        off = 1 + i * 6
        pid, action, x, y = struct.unpack_from("<BBhh", pkt, off)
        points.append((pid, action, x, y))
    return points


def letterbox(src_w: int, src_h: int, dst_w: int = PANEL_W, dst_h: int = PANEL_H):
    """Scale-to-fit math. Returns (scale, off_x, off_y, out_w, out_h)."""
    scale = min(dst_w / src_w, dst_h / src_h)
    out_w = max(1, round(src_w * scale))
    out_h = max(1, round(src_h * scale))
    off_x = (dst_w - out_w) // 2
    off_y = (dst_h - out_h) // 2
    return scale, off_x, off_y, out_w, out_h


def panel_to_screen(px: int, py: int, scale: float, off_x: int, off_y: int, left: int, top: int):
    """Panel coordinate -> desktop pixel (inside the captured region)."""
    return (left + (px - off_x) / scale, top + (py - off_y) / scale)


def screen_to_panel(sx: float, sy: float, scale: float, off_x: int, off_y: int, left: int, top: int):
    """Desktop pixel -> panel coordinate (clamped)."""
    px = round((sx - left) * scale) + off_x
    py = round((sy - top) * scale) + off_y
    px = max(0, min(PANEL_W - 1, px))
    py = max(0, min(PANEL_H - 1, py))
    return px, py


# ============================================================
# Input injection backends
# ============================================================

class NullInjector:
    """Records touches instead of injecting (used on unsupported systems and
    by the selftest)."""
    dry_run = True

    def __init__(self):
        self.events = []

    def touch(self, points, mapping):
        for p in points:
            self.events.append(p)
        return self.events[-3:]


class NullCursor:
    def position(self):
        return None


class WindowsInjector:
    dry_run = False

    def __init__(self):
        import ctypes
        import ctypes.wintypes as wintypes
        self._ct = ctypes
        self._wt = wintypes
        user32 = ctypes.windll.user32
        try:
            user32.SetProcessDPIAware()   # physical pixels, matches mss
        except Exception:
            pass
        self._user32 = user32

        class _MOUSEINPUT(ctypes.Structure):
            _fields_ = [("dx", ctypes.c_long), ("dy", ctypes.c_long),
                        ("mouseData", ctypes.c_ulong), ("dwFlags", ctypes.c_ulong),
                        ("time", ctypes.c_ulong),
                        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]

        class _INPUTUNION(ctypes.Union):
            _fields_ = [("mi", _MOUSEINPUT)]

        class _INPUT(ctypes.Structure):
            _anonymous_ = ("u",)
            _fields_ = [("type", ctypes.c_ulong), ("u", _INPUTUNION)]

        self._INPUT = _INPUT
        self._LEFTDOWN = 0x0002
        self._LEFTUP = 0x0004
        self._button_down = False

    def _click(self, down: bool):
        inp = self._INPUT()
        inp.type = 0                       # INPUT_MOUSE
        inp.mi.dwFlags = self._LEFTDOWN if down else self._LEFTUP
        sent = self._user32.SendInput(1, self._ct.byref(inp), self._ct.sizeof(self._INPUT))
        if sent != 1:
            raise OSError("SendInput rejected (need a real desktop session)")

    def touch(self, points, mapping):
        """mapping = callable(panel_x, panel_y) -> (screen_x, screen_y)"""
        # Only the first point drives the mouse (single-pointer model).
        pid, action, px, py = points[0]
        sx, sy = mapping(px, py)
        sx, sy = int(sx), int(sy)
        self._user32.SetCursorPos(sx, sy)
        if action == TOUCH_DOWN and not self._button_down:
            self._click(True)
            self._button_down = True
        elif action == TOUCH_UP and self._button_down:
            self._click(False)
            self._button_down = False


class WindowsCursor:
    def __init__(self):
        import ctypes
        import ctypes.wintypes as wintypes
        self._ct = ctypes
        self._pt = wintypes.POINT()
        self._user32 = ctypes.windll.user32

    def position(self):
        if self._user32.GetCursorPos(self._ct.byref(self._pt)):
            return float(self._pt.x), float(self._pt.y)
        return None


class LinuxInjector:
    """Mouse injection via /dev/uinput (pip install python-uinput)."""
    dry_run = False

    def __init__(self):
        import uinput
        self._uinput = uinput
        self._dev = uinput.Device([uinput.REL_X, uinput.REL_Y, uinput.BTN_LEFT])
        self._last = None
        self._button_down = False

    def touch(self, points, mapping):
        pid, action, px, py = points[0]
        sx, sy = mapping(px, py)
        if self._last is not None and action == TOUCH_MOVE:
            dx = max(-400, min(400, int(sx - self._last[0])))
            dy = max(-400, min(400, int(sy - self._last[1])))
            if dx:
                self._dev.emit(self._uinput.REL_X, dx)
            if dy:
                self._dev.emit(self._uinput.REL_Y, dy)
        self._last = (sx, sy)
        if action == TOUCH_DOWN and not self._button_down:
            self._dev.emit(self._uinput.BTN_LEFT, 1)
            self._button_down = True
        elif action == TOUCH_UP and self._button_down:
            self._dev.emit(self._uinput.BTN_LEFT, 0)
            self._button_down = False


class LinuxCursor:
    """Cursor position via python-xlib (X11 only)."""

    def __init__(self):
        from Xlib import display
        self._dpy = display.Display()

    def position(self):
        try:
            coord = self._dpy.screen().root.query_pointer()
            return float(coord.root_x), float(coord.root_y)
        except Exception:
            return None


def make_injector(want_touch: bool):
    if not want_touch:
        return None
    if sys.platform == "win32":
        try:
            return WindowsInjector()
        except Exception as exc:
            print(f"[WARN] Touch injection unavailable ({exc}); touch disabled")
            return None
    try:
        return LinuxInjector()
    except Exception as exc:
        print(f"[WARN] Touch injection unavailable ({exc}); "
              f"install python-uinput and /dev/uinput access to enable it")
        return None


def make_cursor_tracker(want_cursor: bool):
    if not want_cursor:
        return None
    try:
        if sys.platform == "win32":
            return WindowsCursor()
        return LinuxCursor()
    except Exception as exc:
        print(f"[WARN] Cursor overlay unavailable ({exc}); "
              f"on Linux install python-xlib (X11) for cursor tracking")
        return None


# ============================================================
# Device connection
# ============================================================

class DeviceClient:
    def __init__(self, host, port, injector=None, cursor_tracker=None,
                 region=None, dry_touch=False):
        self.host = host
        self.port = port
        self.sock = None
        self.alive = False
        self.panel = (PANEL_W, PANEL_H)
        self.caps = 0
        self.max_frame = MAX_JPEG_BYTES
        self.injector = injector
        self.cursor_tracker = cursor_tracker
        self.region = region            # dict with left/top/width/height
        self.dry_touch = dry_touch
        self.touches_seen = 0
        self._run_recv = False
        self._touch_map = None
        self._dry_points = None

    # ---- low-level ----
    def _recv_exact(self, n: int):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    def send_all(self, data: bytes):
        self.sock.sendall(data)

    def send_frame(self, seq: int, jpeg: bytes):
        self.send_all(build_frame_header(seq, len(jpeg)) + jpeg)

    def send_ping(self):
        self.send_all(MAGIC_PING)

    def send_bye(self):
        try:
            self.send_all(MAGIC_BYE)
        except OSError:
            pass

    def send_cursor(self, visible: bool, x: int, y: int):
        self.send_all(build_cursor(visible, x, y))

    # ---- connect ----
    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=10)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        hello = self._recv_exact(16)
        if hello is None:
            raise ConnectionError("no hello from device")
        w, h, ver, caps, max_frame = parse_hello(hello)
        if ver != PROTOCOL_VERSION:
            raise ConnectionError(f"device protocol v{ver}, client v{PROTOCOL_VERSION}")
        self.panel = (w, h)
        self.caps = caps
        self.max_frame = max_frame
        self.alive = True
        self._run_recv = True
        import threading
        threading.Thread(target=self._recv_loop, daemon=True).start()
        return caps

    def close(self):
        self._run_recv = False
        self.alive = False
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    # ---- receive / inject ----
    def set_touch_mapping(self, scale, off_x, off_y, left, top):
        def mapping(px, py):
            return panel_to_screen(px, py, scale, off_x, off_y, left, top)
        self._touch_map = mapping

    def _recv_loop(self):
        try:
            while self._run_recv:
                magic = self._recv_exact(4)
                if magic is None:
                    break
                if magic == MAGIC_PONG:
                    continue
                if magic == MAGIC_TOUCH:
                    hdr = self._recv_exact(1)
                    if hdr is None:
                        break
                    body = self._recv_exact(hdr[0] * 6)
                    if body is None:
                        break
                    points = parse_touch(bytes([hdr[0]]) + body)
                    self.touches_seen += len(points)
                    if self.dry_touch:
                        self._dry_points = points
                    if self.injector is not None and self._touch_map:
                        try:
                            self.injector.touch(points, self._touch_map)
                        except Exception as exc:
                            print(f"[WARN] touch inject failed: {exc}")
                    continue
                print(f"[WARN] device sent unknown packet {magic!r}; reconnecting")
                break
        except OSError:
            pass
        finally:
            self.alive = False


# ============================================================
# mDNS discovery
# ============================================================

def discover(timeout: float = 5.0):
    """Browse for _secondscreen._tcp.local. Returns [(name, host, port, ip)]."""
    try:
        from zeroconf import Zeroconf, ServiceBrowser
    except ImportError:
        print("zeroconf not installed — pip install -r requirements.txt")
        return []

    found = []

    class _Listener:
        def add_service(self, zc, type_, name):
            try:
                info = zc.get_service_info(type_, name)
                if info and info.addresses:
                    ip = socket.inet_ntoa(info.addresses[0])
                    host = (info.server or "").rstrip(".")
                    found.append((name.replace(f".{type_}", ""), host, info.port, ip))
            except Exception:
                pass

        def update_service(self, *a):
            pass

        def remove_service(self, *a):
            pass

    zc = Zeroconf()
    try:
        ServiceBrowser(zc, "_secondscreen._tcp.local.", _Listener())
        time.sleep(timeout)
    finally:
        zc.close()
    return found


def list_monitors():
    with new_mss() as sct:
        for i, mon in enumerate(sct.monitors):
            label = "ALL (virtual desktop)" if i == 0 else f"Monitor {i}"
            print(f"  --monitor {i} : {label}  "
                  f"({mon['width']}x{mon['height']} @ {mon['left']},{mon['top']})")


# ============================================================
# Capture / encode
# ============================================================

def encode_frame(canvas, quality: int) -> bytes:
    buf = io.BytesIO()
    canvas.save(buf, "JPEG", quality=quality)
    return buf.getvalue()


class MirrorSession:
    def __init__(self, dev: DeviceClient, args):
        from PIL import Image
        self.Image = Image
        self.dev = dev
        self.args = args

        self.zoom = max(1.0, float(args.zoom))
        self.follow_cursor = args.follow_cursor
        if self.follow_cursor and not self.dev.cursor_tracker:
            print("[WARN] --follow-cursor requested but cursor tracker is unavailable; "
                  "falling back to center/static region")
            self.follow_cursor = False

        if args.region:
            x, y, w, h = (int(v) for v in args.region.split(","))
            self.region = {"left": x, "top": y, "width": w, "height": h}
        else:
            with new_mss() as sct:
                idx = max(0, min(args.monitor, len(sct.monitors) - 1))
                self.region = dict(sct.monitors[idx])

        src_w, src_h = self.region["width"], self.region["height"]
        self.canvas = Image.new("RGB", (PANEL_W, PANEL_H), (0, 0, 0))
        self.last_small = None
        self.seq = 0

        filters = {"lanczos": Image.LANCZOS, "bilinear": Image.BILINEAR,
                   "nearest": Image.NEAREST}
        self.filter = filters[args.scale_filter]

        print(f"[INFO] Source: {src_w}x{src_h} @ {self.region['left']},{self.region['top']} "
              f"| zoom: {self.zoom}x | follow-cursor: {self.follow_cursor}")

        if self.zoom > 1.0:
            sw = max(100, int(self.region["width"] / self.zoom))
            sh = max(100, int(self.region["height"] / self.zoom))
            sub_l = self.region["left"] + (self.region["width"] - sw) // 2
            sub_t = self.region["top"] + (self.region["height"] - sh) // 2
            self.current_sub_region = {"left": sub_l, "top": sub_t, "width": sw, "height": sh}
        else:
            self.current_sub_region = dict(self.region)

        self._update_touch_mapping(self.current_sub_region["left"],
                                   self.current_sub_region["top"],
                                   self.current_sub_region["width"],
                                   self.current_sub_region["height"])
        self.sct = new_mss()   # one instance, this thread only (reused per frame)

    def _update_touch_mapping(self, sub_l=None, sub_t=None, sub_w=None, sub_h=None):
        if sub_l is None:
            sub_l = self.region["left"]
            sub_t = self.region["top"]
            sub_w = self.region["width"]
            sub_h = self.region["height"]

        def mapping(px, py):
            sx = sub_l + (px / PANEL_W) * sub_w
            sy = sub_t + (py / PANEL_H) * sub_h
            return sx, sy
        self.dev._touch_map = mapping

    def grab_and_encode(self):
        """Returns (jpeg_bytes, changed: bool). One full capture+encode cycle."""
        cap_region = self.region
        if self.zoom > 1.0 or self.follow_cursor:
            sw = max(100, int(self.region["width"] / self.zoom))
            sh = max(100, int(self.region["height"] / self.zoom))
            cx, cy = self.region["left"] + self.region["width"] / 2, self.region["top"] + self.region["height"] / 2
            if self.follow_cursor and self.dev.cursor_tracker:
                pos = self.dev.cursor_tracker.position()
                if pos is not None:
                    cx, cy = pos
            sub_l = int(max(self.region["left"], min(self.region["left"] + self.region["width"] - sw, cx - sw / 2)))
            sub_t = int(max(self.region["top"], min(self.region["top"] + self.region["height"] - sh, cy - sh / 2)))
            cap_region = {"left": sub_l, "top": sub_t, "width": sw, "height": sh}
            self.current_sub_region = cap_region
            self._update_touch_mapping(sub_l, sub_t, sw, sh)

        raw = self.sct.grab(cap_region)
        img = self.Image.frombytes("RGB", (raw.width, raw.height), raw.bgra,
                                   "raw", "BGRX")
        small = img.resize((PANEL_W, PANEL_H), self.filter)

        data = small.tobytes()
        changed = data != self.last_small
        self.last_small = data
        if not changed:
            return None, False

        self.last_canvas = small
        return encode_frame(small, self.args.quality), True

    def encode_at(self, quality: int) -> bytes:
        """Re-encode the last captured frame at a different JPEG quality."""
        return encode_frame(self.last_canvas, quality)

    def send_cursor_update(self, state):
        """Throttled cursor overlay updates. state = dict for bookkeeping."""
        if self.dev.cursor_tracker is None:
            return
        now = time.monotonic()
        if now - state["last_cursor_sent"] < 1.0 / 30:
            return
        pos = self.dev.cursor_tracker.position()
        if pos is None:
            return
        sx, sy = pos
        sub = self.current_sub_region
        inside = (sub["left"] <= sx < sub["left"] + sub["width"] and
                  sub["top"] <= sy < sub["top"] + sub["height"])
        visible = inside
        if visible:
            px = int((sx - sub["left"]) / sub["width"] * PANEL_W)
            py = int((sy - sub["top"]) / sub["height"] * PANEL_H)
            panel_pos = (px, py)
        else:
            panel_pos = (0, 0)

        if visible != state["cursor_visible"] or panel_pos != state["cursor_pos"]:
            self.dev.send_cursor(visible, *panel_pos)
            state["cursor_visible"] = visible
            state["cursor_pos"] = panel_pos
            state["last_cursor_sent"] = now


def run_session(dev: DeviceClient, args):
    session = MirrorSession(dev, args)
    interval = 1.0 / args.fps
    state = {"last_cursor_sent": 0.0, "cursor_visible": None, "cursor_pos": None,
             "oversize_warned": False}
    next_ping = time.monotonic() + 2.0
    stats = {"frames": 0, "skipped": 0, "bytes": 0, "t0": time.monotonic(),
             "last_print": time.monotonic()}

    print(f"[INFO] Mirroring to {dev.host}:{dev.port} at {args.fps} fps "
          f"(quality {args.quality}, cursor {args.cursor}, "
          f"touch {'on' if dev.injector else 'off'}) — Ctrl+C to stop")

    try:
        while True:
            if not dev.alive:
                raise ConnectionError("device closed the connection")
            t0 = time.perf_counter()

            jpeg, changed = session.grab_and_encode()
            if jpeg is not None and len(jpeg) > dev.max_frame:
                # The device rejects frames above maxFrameBytes (384 KB); this
                # can only happen at very high quality on noisy content, so
                # re-encode instead of letting the connection die.
                if not state["oversize_warned"]:
                    print(f"[WARN] {len(jpeg)} B frame exceeds the device limit "
                          f"({dev.max_frame} B) — lowering JPEG quality")
                    state["oversize_warned"] = True
                jpeg = session.encode_at(max(30, args.quality - 20))
                if len(jpeg) > dev.max_frame:
                    jpeg = None          # pathological content: skip this frame
            if jpeg is not None:
                dev.send_frame(session.seq, jpeg)
                session.seq = (session.seq + 1) & 0xFFFF
                stats["frames"] += 1
                stats["bytes"] += len(jpeg) + 12
            else:
                stats["skipped"] += 1

            session.send_cursor_update(state)

            now = time.monotonic()
            if now >= next_ping:
                dev.send_ping()
                next_ping = now + 2.0

            if now - stats["last_print"] >= 2.0:
                elapsed = now - stats["last_print"]
                fps = stats["frames"] / elapsed
                kbps = stats["bytes"] * 8 / elapsed / 1000
                total = stats["frames"] + stats["skipped"]
                skip_pct = 100.0 * stats["skipped"] / total if total else 0
                print(f"[STAT] send {fps:.1f} fps | {kbps:.0f} kbps | "
                      f"skipped {skip_pct:.0f}% | touches {dev.touches_seen}")
                stats["frames"] = 0
                stats["skipped"] = 0
                stats["bytes"] = 0
                stats["last_print"] = now

            elapsed = time.perf_counter() - t0
            if elapsed < interval:
                time.sleep(interval - elapsed)
    finally:
        dev.send_bye()


# ============================================================
# Entry point
# ============================================================

def main():
    ap = argparse.ArgumentParser(
        description="Mirror your PC screen to the VIEWE 5\" ESP32-S3 second screen")
    ap.add_argument("--discover", action="store_true",
                    help="find displays on the LAN and exit")
    ap.add_argument("--list-monitors", action="store_true",
                    help="list capturable monitors and exit")
    ap.add_argument("--host", help="display IP or hostname "
                                   "(default: mDNS auto-discovery)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--monitor", type=int, default=1,
                    help="monitor index from --list-monitors (default 1; "
                         "0 = whole virtual desktop)")
    ap.add_argument("--region", metavar="X,Y,W,H",
                    help="capture a desktop region instead of a monitor")
    ap.add_argument("--fps", type=int, default=20, help="target fps (default 20)")
    ap.add_argument("--quality", type=int, default=60,
                    help="JPEG quality 1-95 (default 60)")
    ap.add_argument("--scale-filter", default="lanczos",
                    choices=["lanczos", "bilinear", "nearest"])
    ap.add_argument("--cursor", default="overlay", choices=["overlay", "none"],
                    help="cursor overlay (drawn by the display, default)")
    ap.add_argument("--no-touch", action="store_true",
                    help="disable the touch backchannel")
    ap.add_argument("--zoom", type=float, default=1.0,
                    help="magnification factor for small-screen readability (default 1.0)")
    ap.add_argument("--follow-cursor", action="store_true",
                    help="automatically center the zoomed view on the mouse cursor")
    args = ap.parse_args()

    if args.discover:
        results = discover()
        if not results:
            print("No displays found. Is the display flashed, on Wi-Fi, "
                  "and on the same network?")
            return 1
        for name, host, port, ip in results:
            print(f"  {name}  ->  {host} ({ip}):{port}")
        return 0

    if args.list_monitors:
        list_monitors()
        return 0

    if args.fps < 1 or args.fps > 60:
        ap.error("--fps must be 1..60")
    if not 1 <= args.quality <= 95:
        ap.error("--quality must be 1..95")
    if args.zoom < 1.0 or args.zoom > 10.0:
        ap.error("--zoom must be between 1.0 and 10.0")
    if args.region:
        try:
            _x, _y, w, h = (int(v) for v in args.region.split(","))
        except ValueError:
            ap.error("--region must be four integers: X,Y,W,H")
        if w <= 0 or h <= 0:
            ap.error("--region width/height must be positive")

    host = args.host
    if not host:
        results = discover(timeout=5.0)
        if not results:
            print("No display found via mDNS. Use --host <IP>, or check that "
                  "the display is flashed, connected to Wi-Fi, and on the "
                  "same network as this PC.")
            return 1
        name, host, port, ip = results[0]
        print(f"[INFO] Discovered {name} at {host} ({ip}):{port}")
        args.port = port

    injector = make_injector(not args.no_touch)
    cursor_tracker = make_cursor_tracker(args.cursor == "overlay" or args.follow_cursor)

    dev = DeviceClient(host, args.port, injector=injector,
                       cursor_tracker=cursor_tracker, region=args.region)
    try:
        dev.connect()
    except OSError as exc:
        print(f"[ERR] Cannot connect to {host}:{args.port} — {exc}")
        return 1
    caps = dev.caps
    print(f"[INFO] Device: panel {dev.panel[0]}x{dev.panel[1]}, "
          f"protocol v{PROTOCOL_VERSION}, "
          f"touch {'available' if caps & CAPS_TOUCH else 'NOT available'}")
    if (caps & CAPS_TOUCH) == 0 and injector is not None and not args.no_touch:
        print("[WARN] Device reports no touch; touch disabled")
        dev.injector = None

    while True:
        try:
            run_session(dev, args)
        except (ConnectionError, OSError) as exc:
            print(f"[WARN] Connection lost ({exc}); retrying in 3s...")
            dev.close()
            time.sleep(3)
            try:
                dev.connect()
                print("[INFO] Reconnected")
            except OSError:
                continue
        except KeyboardInterrupt:
            print("\n[INFO] Stopped")
            dev.send_bye()
            dev.close()
            return 0


if __name__ == "__main__":
    sys.exit(main())
