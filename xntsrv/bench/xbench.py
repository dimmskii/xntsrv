#!/usr/bin/env python3
"""
xbench - how quickly an X server answers, seen from an X client.

Speaks the X protocol directly (Python's standard library only), so it runs
in any guest with nothing installed, and measures the things that make a
remote X display feel quick or slow:

  connect   opening a connection, up to the server's setup reply
  roundtrip one request and its reply
  pair      two requests sent separately, and their two replies; shows small
            messages being held back on the way (Nagle's algorithm)
  popup     creating and mapping a small window, as a menu does, until its
            MapNotify and its Expose arrive.  Briefly shows a white box near
            the top-left corner of the screen.
  putimage  sending full-HD images to an off-screen pixmap, as programs that
            draw in software (or render with the GPU in the guest) do
  burst     filling a full-HD pixmap 50 and then 400 times, sent in one go.
            Per fill, the two should cost the same; if the larger batch costs
            more, the server idles between its time slices.

  xbench.py [--display=D] [--tests=connect,roundtrip,pair,popup,putimage,burst]

D is an X display name (":0", "host:0") or a raw target ("unix:PATH",
"tcp:HOST:PORT"); the default is $DISPLAY.  The authentication cookie is
read from $XAUTHORITY or ~/.Xauthority.  Times are in milliseconds.

SPDX-License-Identifier: MIT
"""
import argparse
import os
import socket
import statistics
import struct
import sys
import time

ALL_TESTS = ["connect", "roundtrip", "pair", "popup", "putimage", "burst"]
NOTES = []  # lines printed after the table


def target_for(display):
    if display.startswith(("unix:", "tcp:")):
        return display
    host, _, rest = display.rpartition(":")
    number = int(rest.split(".")[0])
    if host in ("", "unix"):
        return "unix:/tmp/.X11-unix/X%d" % number
    return "tcp:%s:%d" % (host, 6000 + number)


def read_cookie():
    path = os.environ.get("XAUTHORITY") or os.path.expanduser("~/.Xauthority")
    try:
        data = open(path, "rb").read()
    except OSError:
        return b""
    i = 0
    while i + 2 <= len(data):
        i += 2  # family
        fields = []
        for _ in range(4):  # address, display number, auth name, auth data
            n = struct.unpack(">H", data[i:i + 2])[0]
            fields.append(data[i + 2:i + 2 + n])
            i += 2 + n
        if fields[2] == b"MIT-MAGIC-COOKIE-1":
            return fields[3]
    return b""


def pad(b):
    return b + b"\0" * (-len(b) % 4)


class Connection:
    def __init__(self, target, cookie):
        if target.startswith("unix:"):
            self.sock = socket.socket(socket.AF_UNIX)
            self.sock.connect(target[5:])
        else:
            host, port = target[4:].rsplit(":", 1)
            # As a normal client: libxcb sends each request at once too.
            self.sock = socket.create_connection((host, int(port)))
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        name = b"MIT-MAGIC-COOKIE-1" if cookie else b""
        self.sock.sendall(struct.pack("<BxHHHHxx", ord("l"), 11, 0, len(name), len(cookie)) + pad(name) + pad(cookie))
        head = self.recv(8)
        body = self.recv(struct.unpack("<H", head[6:8])[0] * 4)
        if head[0] != 1:
            sys.exit("xbench: the server refused the connection: %s" % body[: head[1]].decode(errors="replace"))
        self.setup_size = 8 + len(body)
        self.next_id = struct.unpack("<I", body[4:8])[0]
        vendor = struct.unpack("<H", body[16:18])[0]
        formats = body[21]
        screen = 32 + (vendor + 3) // 4 * 4 + 8 * formats
        self.root = struct.unpack("<I", body[screen:screen + 4])[0]

    def new_id(self):
        self.next_id += 1
        return self.next_id

    def recv(self, n):
        out = b""
        while len(out) < n:
            chunk = self.sock.recv(n - len(out))
            if not chunk:
                sys.exit("xbench: the server closed the connection")
            out += chunk
        return out

    def send(self, data):
        self.sock.sendall(data)

    def event(self):
        """The next event or reply (32 bytes; replies' extra data is not used here)."""
        e = self.recv(32)
        if e[0] == 0:
            sys.exit("xbench: X error %d for request %d" % (e[1], e[10]))
        return e

    def sync(self):
        self.send(struct.pack("<BxH", 43, 1))  # GetInputFocus
        while self.event()[0] != 1:
            pass

    def close(self):
        self.sock.close()


def ms(seconds):
    return seconds * 1000


def summary(values):
    values = sorted(values)
    return "median %7.2f   p90 %7.2f   max %7.2f" % (
        statistics.median(values), values[int(len(values) * 0.9)], values[-1])


def test_connect(target, cookie):
    times = []
    for _ in range(20):
        t = time.perf_counter()
        Connection(target, cookie).close()
        times.append(ms(time.perf_counter() - t))
    return [("connect", times)]


def test_roundtrip(c):
    times = []
    for _ in range(200):
        t = time.perf_counter()
        c.sync()
        times.append(ms(time.perf_counter() - t))
    return [("roundtrip", times)]


def test_pair(c):
    times = []
    request = struct.pack("<BxH", 43, 1)
    for _ in range(50):
        t = time.perf_counter()
        c.send(request)
        c.send(request)
        c.recv(64)
        times.append(ms(time.perf_counter() - t))
    return [("pair", times)]


def test_popup(c):
    mapped, exposed = [], []
    for _ in range(15):
        wid = c.new_id()
        # CreateWindow 150x200 at 40,40: white, override-redirect (as menus
        # are), reporting Exposure and StructureNotify.
        values = struct.pack("<III", 0xFFFFFF, 1, 0x8000 | 0x20000)
        create = struct.pack("<BBHIIhhHHHHII", 1, 0, 8 + 3, wid, c.root, 40, 40, 150, 200, 0, 1, 0,
                             0x2 | 0x200 | 0x800) + values
        t = time.perf_counter()
        c.send(create + struct.pack("<BxHI", 8, 2, wid))  # MapWindow
        seen = {}
        while 19 not in seen or 12 not in seen:  # MapNotify, Expose
            seen.setdefault(c.event()[0] & 0x7F, time.perf_counter())
        mapped.append(ms(seen[19] - t))
        exposed.append(ms(seen[12] - t))
        c.send(struct.pack("<BxHI", 4, 2, wid))  # DestroyWindow
        c.sync()
        time.sleep(0.03)
    return [("popup: MapNotify", mapped), ("popup: Expose", exposed)]


def test_putimage(c):
    width, height, frames = 1920, 1080, 10
    pixmap, gc = c.new_id(), c.new_id()
    c.send(struct.pack("<BBHIIHH", 53, 24, 4, pixmap, c.root, width, height))  # CreatePixmap
    c.send(struct.pack("<BxHIII", 55, 4, gc, pixmap, 0))  # CreateGC
    rows = (65535 * 4 - 24) // (width * 4)  # largest PutImage without BIG-REQUESTS
    line = bytes(range(256)) * (width * 4 // 256)

    def frame():
        y = 0
        while y < height:
            n = min(rows, height - y)
            data = line * n
            c.send(struct.pack("<BBHIIHHhhBBxx", 72, 2, 6 + len(data) // 4, pixmap, gc, width, n, 0, y, 0, 24) + data)
            y += n
        c.sync()

    frame()
    times = []
    for _ in range(frames):
        t = time.perf_counter()
        frame()
        times.append(ms(time.perf_counter() - t))
    c.send(struct.pack("<BxHI", 60, 2, gc) + struct.pack("<BxHI", 54, 2, pixmap))  # FreeGC, FreePixmap
    c.sync()
    mb = width * height * 4 / 1e6
    NOTES.append("putimage: %.1f full-HD frames a second, %.0f MB/s" % (1000 / statistics.median(times),
                                                                        mb * 1000 / statistics.median(times)))
    return [("putimage: one frame", times)]


def test_burst(c):
    width, height = 1920, 1080
    pixmap, gc = c.new_id(), c.new_id()
    c.send(struct.pack("<BBHIIHH", 53, 24, 4, pixmap, c.root, width, height))  # CreatePixmap
    c.send(struct.pack("<BxHIII", 55, 4, gc, pixmap, 0))  # CreateGC
    fill = struct.pack("<BxHII", 70, 5, pixmap, gc) + struct.pack("<hhHH", 0, 0, width, height)  # PolyFillRectangle
    c.sync()
    results = []
    for count in (50, 400):
        per_fill = []
        for _ in range(5):
            t = time.perf_counter()
            c.send(fill * count)
            c.sync()
            per_fill.append(ms(time.perf_counter() - t) / count)
        results.append(("burst: %d, per fill" % count, per_fill))
    c.send(struct.pack("<BxHI", 60, 2, gc) + struct.pack("<BxHI", 54, 2, pixmap))  # FreeGC, FreePixmap
    c.sync()
    small, large = statistics.median(results[0][1]), statistics.median(results[1][1])
    NOTES.append("burst: a long batch costs %.1f times as much per fill as a short one%s" % (
        large / small, " (the server idles between time slices)" if large > 1.5 * small else ""))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0].strip())
    parser.add_argument("--display", default=os.environ.get("DISPLAY", ""))
    parser.add_argument("--tests", default=",".join(ALL_TESTS))
    args = parser.parse_args()
    if not args.display:
        sys.exit("xbench: no display; set DISPLAY or pass --display")
    tests = args.tests.split(",")
    for name in tests:
        if name not in ALL_TESTS:
            sys.exit("xbench: unknown test %r; the tests are %s" % (name, ", ".join(ALL_TESTS)))

    target = target_for(args.display)
    cookie = read_cookie()
    c = Connection(target, cookie)
    print("xbench: %s (setup reply %d bytes); times in milliseconds" % (target, c.setup_size))
    results = []
    for name in tests:
        if name == "connect":
            results += test_connect(target, cookie)
        else:
            results += globals()["test_" + name](c)
    for label, values in results:
        print("  %-20s %s" % (label, summary(values)))
    for note in NOTES:
        print("  " + note)


if __name__ == "__main__":
    main()
