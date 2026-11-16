#!/usr/bin/env python3
"""Checks a cursor theme written by xntsrv-appearance --write-cursors=DIR.

Reads the files the way libXcursor does and checks that they are sane:
enough pointers, valid headers, plausible sizes, something visible, and a hot
spot inside the picture.  Usage: check-xcursor.py DIR
"""
import os
import struct
import sys


def images(path):
    data = open(path, "rb").read()
    magic, header, version, count = struct.unpack_from("<4sIII", data, 0)
    assert magic == b"Xcur", "bad magic"
    assert count >= 1, "no images"
    out = []
    for i in range(count):
        kind, nominal, position = struct.unpack_from("<III", data, header + 12 * i)
        assert kind == 0xFFFD0002, "not an image entry"
        size, kind2, nominal2, ver, width, height, xhot, yhot, delay = struct.unpack_from("<9I", data, position)
        assert size == 36 and kind2 == kind and nominal2 == nominal and ver == 1, "bad image header"
        assert 8 <= width <= 512 and 8 <= height <= 512, "implausible size %dx%d" % (width, height)
        assert xhot < width and yhot < height, "hot spot outside the picture"
        pixels = struct.unpack_from("<%dI" % (width * height), data, position + 36)
        out.append((nominal, width, height, xhot, yhot, delay, pixels))
    return out


def main():
    cursors = os.path.join(sys.argv[1], "cursors")
    names = sorted(os.listdir(cursors))
    assert len(names) >= 40, "only %d cursor files" % len(names)
    for required in ("left_ptr", "default", "xterm", "text", "watch", "hand2", "pointer", "crosshair",
                     "sb_h_double_arrow", "sb_v_double_arrow", "fleur", "not-allowed"):
        assert required in names, "missing " + required
    assert os.path.exists(os.path.join(sys.argv[1], "index.theme")), "no index.theme"

    animated = 0
    for name in names:
        try:
            frames = images(os.path.join(cursors, name))
        except AssertionError as error:
            sys.exit("%s: %s" % (name, error))
        opaque = sum(1 for p in frames[0][6] if p >> 24 == 255)
        visible = sum(1 for p in frames[0][6] if p >> 24)
        assert visible >= 10, "%s: almost nothing visible" % name
        # Premultiplied: no colour channel may exceed the pixel's opacity.
        for p in frames[0][6]:
            a = p >> 24
            assert ((p >> 16) & 255) <= a and ((p >> 8) & 255) <= a and (p & 255) <= a, "%s: not premultiplied" % name
        if len(frames) > 1:
            animated += 1
            assert all(f[5] > 0 for f in frames), "%s: animation without delays" % name
        if name == "left_ptr":
            nominal, width, height, xhot, yhot = frames[0][:5]
            assert xhot < width // 3 and yhot < height // 3, "arrow's hot spot is not at its tip"
            print("left_ptr: %dx%d, hot spot %d,%d, %d visible pixels, %d opaque" % (width, height, xhot, yhot, visible, opaque))
    print("%d cursor files checked, %d animated" % (len(names), animated))


if __name__ == "__main__":
    main()
