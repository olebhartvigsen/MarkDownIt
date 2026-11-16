#!/usr/bin/env python3
"""Downscale the doc-logo 4x masters from render-doc.mjs to the final
MSIX asset set used by the file type association Logo.

Outputs installer/msix/assets/DocLogo44*.png: the plain targetsize set
plus the altform-unplated and altform-lightunplated flavors (the same
layout emit_variants in finalize.py uses for Square44x44Logo), because
Explorer and the "Open with" list resolve a FileTypeAssociation Logo
through the same qualified resource lookup as the app tile icon.
"""
from PIL import Image
import os
import shutil

root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
src = os.path.join(root, "tools", "msix-icons", "out-4x-doc")
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


for t in (16, 20, 24, 30, 32, 36, 40, 44, 48, 56, 60, 64, 72, 80, 96, 256):
    emit_variants(f"DocLogo44.targetsize-{t}", t, t)
write("DocLogo44", 44, 44)

files = sorted(f for f in os.listdir(dst) if f.startswith("DocLogo44"))
for f in files:
    im = Image.open(os.path.join(dst, f))
    assert im.mode == "RGBA" and im.width > 0 and im.height > 0, f
print(f"{len(files)} doc logo assets written to {dst}")
