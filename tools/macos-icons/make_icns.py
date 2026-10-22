#!/usr/bin/env python3
"""Pack the rendered PNGs into a macOS icns file.

The icns container is a list of typed blocks. Each block carries a
four byte type, a four byte length and the image data. The types
follow the naming Apple uses: the two letter prefix says the size and
the number says the pixel size or the scale.

    ic04  16x16      ic05  32x32      ic07  128x128
    ic08  256x256    ic09  512x512    ic10  1024x1024
    ic11  16x16 at 2x (32x32)
    ic12  32x32 at 2x (64x64)
    ic13  128x128 at 2x (256x256)
    ic14  256x256 at 2x (512x512)

Every block here is a PNG, which macOS has accepted since 10.7.
"""
import os
import struct
import sys

# The type for each size that goes into the file.
BLOCKS = [
    ("icp4", 16),
    ("icp5", 32),
    ("icp6", 64),
    ("ic07", 128),
    ("ic08", 256),
    ("ic09", 512),
    ("ic10", 1024),
    ("ic11", 32),
    ("ic12", 64),
    ("ic13", 256),
    ("ic14", 512),
]


def build(png_dir, out_path):
    blocks = []
    for type_name, size in BLOCKS:
        png_path = os.path.join(png_dir, "icon-%d.png" % size)
        if not os.path.exists(png_path):
            print("missing: %s" % png_path)
            sys.exit(1)
        with open(png_path, "rb") as handle:
            data = handle.read()
        blocks.append((type_name, data))

    body = b"".join(
        struct.pack(">4sI", type_name.encode("ascii"), len(data)) + data
        for type_name, data in blocks)

    with open(out_path, "wb") as handle:
        handle.write(b"icns")
        handle.write(struct.pack(">I", len(body) + 8))
        handle.write(body)

    return len(body) + 8


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: make_icns.py <png-dir> <out.icns>")
        sys.exit(2)
    size = build(sys.argv[1], sys.argv[2])
    print("wrote %s (%d bytes)" % (sys.argv[2], size))
