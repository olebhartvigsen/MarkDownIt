#!/usr/bin/env python3
"""Downscale the 4x masters from render.mjs to the final MSIX asset set.

Outputs installer/msix/assets/ with exactly the layout MSIX expects:
base logos + scale variants + targetsize variants (each targetsize in the
plain, altform-unplated and altform-lightunplated flavors; the unplated
flavors are byte-identical to the plain asset because the artwork already
has a transparent background).
"""
from PIL import Image
import os
import shutil

root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
src = os.path.join(root, "tools", "msix-icons", "out-4x")
dst = os.path.join(root, "installer", "msix", "assets")
os.makedirs(dst, exist_ok=True)


def write(name, w, h):
    im = Image.open(os.path.join(src, name + ".png")).convert("RGBA")
    im = im.resize((w, h), Image.LANCZOS)
    im.save(os.path.join(dst, name + ".png"))


def emit_variants(name, w, h):
    write(name, w, h)
    for suffix in ("_altform-unplated", "_altform-lightunplated"):
        shutil.copyfile(
            os.path.join(dst, name + ".png"),
            os.path.join(dst, name + suffix + ".png"),
        )


# Base logos and scale variants ship as plain assets (matching the reference
# set); only the targetsize set carries the unplated flavors the taskbar
# resolves through resources.pri.
for base, size in [("Logo44", 44), ("Logo71", 71), ("Logo150", 150), ("Logo310", 310)]:
    write(base, size, size)
    for s in (100, 125, 150, 200, 400):
        write(f"{base}.scale-{s}", round(size * s / 100), round(size * s / 100))

for t in (16, 20, 24, 30, 32, 36, 40, 44, 48, 56, 60, 64, 72, 80, 96, 256):
    emit_variants(f"Logo44.targetsize-{t}", t, t)

write("StoreLogo", 50, 50)
write("Wide310x150", 310, 150)
write("SplashScreen", 620, 300)

files = sorted(os.listdir(dst))
for f in files:
    im = Image.open(os.path.join(dst, f))
    assert im.mode == "RGBA" and im.width > 0 and im.height > 0, f
print(f"{len(files)} assets written to {dst}")