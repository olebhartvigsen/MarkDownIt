#!/usr/bin/env python3
import argparse, os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 630
TEAL = (20, 184, 166)
TEAL_DARK = (13, 148, 136)
WHITE = (255, 255, 255)

def load_font(path, size):
    return ImageFont.truetype(path, size)

def draw_card(font_reg, font_bold, out):
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / H
        c = tuple(int(TEAL[i] + (TEAL_DARK[i] - TEAL[i]) * t) for i in range(3))
        d.line([(0, y), (W, y)], fill=c)
    d.ellipse([80, 60, 260, 240], fill=WHITE)
    d.text((110, 110), "M↓", font=font_bold, fill=TEAL_DARK)
    d.text((320, 120), "MarkDownIt", font=font_bold, fill=WHITE)
    d.text((320, 220), "A native Windows Markdown editor.", font=font_reg, fill=WHITE)
    d.text((320, 420), "No Electron. No .NET. No browser.", font=font_reg, fill=(224, 255, 250))
    d.text((320, 510), "github.com/olebhartvigsen/MarkDownIt", font=font_reg, fill=(174, 224, 220))
    img.save(out, "PNG")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts-dir", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    fr = load_font(os.path.join(a.fonts_dir, "DejaVuSans.ttf"), 40)
    fb = load_font(os.path.join(a.fonts_dir, "DejaVuSans-Bold.ttf"), 96)
    draw_card(fr, fb, a.out)
    print("wrote", a.out)
