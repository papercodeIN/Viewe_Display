#!/usr/bin/env python3
"""
Self-test for the second-screen PC client (no display hardware needed).

Starts a fake device that speaks the wire protocol on localhost, then drives
the real client code against it: protocol packing, letterbox math, the JPEG
encode path, frame sending, keepalive, cursor packets, and touch parsing with
coordinate mapping. A real mss capture is exercised when a display is present.
Exits 0 on success, 1 on failure.
"""

import os
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mirror as M  # noqa: E402

RESULTS = []


def check(name, cond, detail=""):
    RESULTS.append((name, bool(cond)))
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {name}" + (f" — {detail}" if detail and not cond else ""))


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


class FakeDevice(threading.Thread):
    """Speaks the device side of the protocol and records what it receives."""

    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.frames = []
        self.cursors = []
        self.pings = 0
        self.bye = False
        self.toch_to_send = None
        self._stop = threading.Event()

    def run(self):
        self.sock.settimeout(10)
        conn, _ = self.sock.accept()
        conn.settimeout(10)
        conn.sendall(M.build_hello(800, 480, 1, M.MAX_JPEG_BYTES))
        try:
            while not self._stop.is_set():
                magic = recv_exact(conn, 4)
                if magic is None:
                    break
                if magic == M.MAGIC_FRAME:
                    seq, flags, ln = struct.unpack("<HHI", recv_exact(conn, 8))
                    jpeg = recv_exact(conn, ln)
                    self.frames.append((seq, jpeg))
                elif magic == M.MAGIC_PING:
                    self.pings += 1
                    conn.sendall(M.MAGIC_PONG)
                elif magic == M.MAGIC_CURSOR:
                    vis, x, y = struct.unpack("<Bhh", recv_exact(conn, 5))
                    self.cursors.append((vis, x, y))
                elif magic == M.MAGIC_BYE:
                    self.bye = True
                    break
                else:
                    print(f"  [FAKE-DEVICE] unexpected magic {magic!r}")
                    break
                if self.toch_to_send is not None:
                    conn.sendall(self.toch_to_send)
                    self.toch_to_send = None
        except (socket.timeout, OSError):
            pass
        finally:
            conn.close()
            self.sock.close()

    def finish(self):
        self._stop.set()
        try:
            socket.create_connection(("127.0.0.1", self.port), timeout=1).close()
        except OSError:
            pass


def main():
    print("== 2nd Screen PC client self-test ==")

    # ---------- pure protocol / math ----------
    print("\n-- protocol and math --")
    scale, off_x, off_y, out_w, out_h = M.letterbox(1920, 1080)
    check("letterbox 1920x1080 -> 800x450 centered",
          (out_w, out_h, off_x, off_y) == (800, 450, 0, 15),
          f"got {(scale, off_x, off_y, out_w, out_h)}")
    scale, off_x, off_y, out_w, out_h = M.letterbox(1024, 600)
    check("letterbox 1024x600 -> scale<1 fits",
          out_w <= 800 and out_h <= 480 and off_x >= 0 and off_y >= 0,
          f"got {(out_w, out_h, off_x, off_y)}")

    hdr = M.build_frame_header(7, 4321)
    magic, seq, flags, ln = struct.unpack("<4sHHI", hdr)
    check("frame header round-trip", len(hdr) == 12 and magic == b"MJP1"
          and seq == 7 and flags == 0 and ln == 4321)

    hello = M.build_hello(800, 480, 1, M.MAX_JPEG_BYTES)
    w, h, ver, caps, mx = M.parse_hello(hello)
    check("hello round-trip", (w, h, ver, caps, mx) == (800, 480, 1, 1, M.MAX_JPEG_BYTES))

    cur = M.build_cursor(True, 10, 20)
    check("cursor packet layout", cur == struct.pack("<4sBhh", b"CARS", 1, 10, 20)
          and len(cur) == 9)

    touch_pkt = bytes([1]) + struct.pack("<BBhh", 3, M.TOUCH_DOWN, 100, 200)
    pts = M.parse_touch(touch_pkt)
    check("touch packet parse", pts == [(3, M.TOUCH_DOWN, 100, 200)], f"got {pts}")

    # panel<->screen mapping round-trip (1px tolerance from rounding)
    sc = 0.5
    sx, sy = M.panel_to_screen(400, 240, sc, 0, 0, 10, 20)
    px, py = M.screen_to_panel(sx, sy, sc, 0, 0, 10, 20)
    check("panel<->screen mapping round-trip", abs(px - 400) <= 1 and abs(py - 240) <= 1,
          f"got ({px},{py})")

    if sys.platform == "win32":
        try:
            cursor = M.WindowsCursor()
            position = cursor.position()
            check("Windows cursor tracker runtime", position is not None,
                  f"got {position}")
        except Exception as exc:  # noqa: BLE001
            check("Windows cursor tracker runtime", False, repr(exc))

    # ---------- end-to-end against a fake device ----------
    print("\n-- end-to-end against fake device --")
    from PIL import Image
    dev = FakeDevice()
    dev.start()
    time.sleep(0.2)

    injector = M.NullInjector()
    client = M.DeviceClient("127.0.0.1", dev.port, injector=injector)
    caps = client.connect()
    check("connect + hello", client.alive and client.panel == (800, 480)
          and (caps & M.CAPS_TOUCH) != 0)
    client.set_touch_mapping(1.0, 0, 0, 0, 0)

    for i in range(3):
        img = Image.new("RGB", (800, 480), (i * 40, 60, 120))
        jpeg = M.encode_frame(img, 60)
        check_clean = jpeg[:2] == b"\xff\xd8" and jpeg[-2:] == b"\xff\xd9"
        if i == 0:
            check("JPEG starts with SOI / ends with EOI", check_clean)
        client.send_frame(i, jpeg)
        time.sleep(0.05)

    client.send_ping()
    dev.toch_to_send = M.MAGIC_TOUCH + bytes([1]) + struct.pack(
        "<BBhh", 0, M.TOUCH_DOWN, 400, 240)
    time.sleep(0.3)
    client.send_cursor(True, 5, 5)
    time.sleep(0.3)
    client.send_bye()
    time.sleep(0.3)

    check("device received 3 valid frames",
          len(dev.frames) == 3 and all(j[:2] == b"\xff\xd8" and j[-2:] == b"\xff\xd9"
                                       for _, j in dev.frames),
          f"got {len(dev.frames)} frames")
    check("keepalive ping/pong", dev.pings >= 1)
    check("cursor packet received", dev.cursors and dev.cursors[0] == (1, 5, 5),
          f"got {dev.cursors}")
    check("touch event reached injector (mapped 1:1)",
          injector.events and injector.events[0][2] == 400 and injector.events[0][3] == 240,
          f"got {injector.events}")

    # ---------- real capture smoke test (skips when headless) ----------
    print("\n-- real mss capture (optional) --")
    try:
        import argparse
        with M.new_mss() as sct:
            mon_count = len(sct.monitors)
        if mon_count < 2:
            print("  [SKIP] no capturable monitor found")
        else:
            dev2 = FakeDevice()
            dev2.start()
            time.sleep(0.2)
            args = argparse.Namespace(region=None, monitor=1, quality=60,
                                      scale_filter="lanczos", fps=20,
                                      cursor="none", no_touch=True,
                                      zoom=1.0, follow_cursor=False)
            client2 = M.DeviceClient("127.0.0.1", dev2.port)
            client2.connect()
            session = M.MirrorSession(client2, args)
            jpeg, changed = session.grab_and_encode()
            ok = jpeg is not None and jpeg[:2] == b"\xff\xd8" and jpeg[-2:] == b"\xff\xd9"
            check("real screen capture -> valid JPEG", ok,
                  f"jpeg={None if jpeg is None else len(jpeg)}B changed={changed}")
            client2.send_bye()
            dev2.finish()
            time.sleep(0.2)
    except Exception as exc:  # noqa: BLE001
        print(f"  [SKIP] real capture unavailable here: {exc}")

    dev.finish()
    dev.join(timeout=2)

    # ---------- summary ----------
    failed = [n for n, ok in RESULTS if not ok]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} checks passed")
    if failed:
        print("FAILED: " + ", ".join(failed))
        return 1
    print("ALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
