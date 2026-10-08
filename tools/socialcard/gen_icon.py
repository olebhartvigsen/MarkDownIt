#!/usr/bin/env python3
import argparse, os
from PIL import Image, ImageDraw, ImageFont

TEAL = (20, 184, 166)
TEAL_DARK = (13, 148, 136)
WHITE = (255, 255, 255)
MONO = "M↓"

def draw_monogram(size, bg, fg, out):
    img = Image.new("RGB", (size, size), bg)
    d = ImageDraw.Draw(img)
    m = size * 0.08
    d.ellipse([m, m, size - m, size - m], fill=fg)
    f = ImageFont.truetype(FONT_BOLD, int(size * 0.42))
    tw = d.textlength(MONO, font=f)
    x = (size - tw) / 2 - size * 0.02
    y = size * 0.30
    d.text((x, y), MONO, font=f, fill=bg)
    img.save(out, "PNG")

def draw_logo(size, out):
    """
    "MarkDownIt site logo: rounded-square tile, teal to dark-teal
    "gradient, white monogram, subtle inner border.
    """
    r = int(size * 0.18)
    img = Image.new("RGB", (size, size), WHITE)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, size, size], radius=r, fill=TEAL)
    for y in range(size):
        t = y / size
        c = tuple(int(TEAL[i] + (TEAL_DARK[i] - TEAL[i]) * t) for i in range(3))
        d.line([(r, y), (size, y)], fill=c)
    d.ellipse([size * 0.12, size * 0.12, size * 0.88, size * 0.88], fill=WHITE)
    f = ImageFont.truetype(FONT_BOLD, int(size * 0.34))
    tw = d.textlength(MONO, font=f)
    x = (size - tw) / 2 - size * 0.015
    y = size * 0.335
    d.text((x, y), MONO, font=f, fill=TEAL_DARK)
    img.save(out, "PNG")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts-dir", required=True)
    ap.add_argument("--site", action="store_true")
    a = ap.parse_args()
    FONT_BOLD = os.path.join(a.fonts_dir, "DejaVuSans-Bold.ttf")
    globals()["FONT_BOLD"] = FONT_BOLD
    if a.site:
        draw_logo(512, "preview-markdownit-logo.png")
    else:
        draw_monogram(512, WHITE, TEAL, "preview-app.png")
    print("wrote previews")
