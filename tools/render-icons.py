"""Render Tabler SVG icons in src/icons to 16/32/64 px PNGs.

Pure-Python rasterizer: svgpathtools samples each SVG subpath densely,
PIL draws the polylines as thick round-capped, round-joined strokes at 4x
the target size, then LANCZOS-downscales to the target size. Matches the
repo convention: 24x24 Tabler geometry, stroke #333333, width 2, round
caps and joins, transparent background.
"""
import os
import re
from svgpathtools import parse_path
from PIL import Image, ImageDraw

ICON_DIR = "/workspace/MarkDownIt/src/icons"
GLYPHS = [
    "find", "replace", "new",
    "table", "table-add-row", "table-add-row-above", "table-remove-row",
    "table-add-column", "table-add-column-left", "table-remove-column",
    "table-remove",
]
STROKE = (0x33, 0x33, 0x33, 255)
SS = 4            # supersample factor
STROKE_W = 2.0    # svg stroke-width in 24-unit space


def sample_points(sub, k):
    n = max(8, min(4000, int(sub.length() * k / 1.5) + 1))
    return [(p.real * k, p.imag * k)
            for p in (sub.point(t / (n - 1)) for t in range(n))]


def draw_path(dr, dstr, k):
    width = max(1, round(STROKE_W * k))
    r = STROKE_W * k / 2.0
    for sub in parse_path(dstr).continuous_subpaths():
        pts = sample_points(sub, k)
        dr.line(pts, fill=STROKE, width=width, joint="curve")
        for x, y in (pts[0], pts[-1]):
            dr.ellipse([x - r, y - r, x + r, y + r], fill=STROKE)


for glyph in GLYPHS:
    svg_path = os.path.join(ICON_DIR, glyph + ".svg")
    text = open(svg_path, encoding="utf-8").read()
    ds = re.findall(r'<path d="([^"]+)"\s*/>', text)
    if not ds:
        raise SystemExit(f"no paths found in {svg_path}")
    for size in (16, 32, 64):
        S = size * SS
        k = S / 24.0
        img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        dr = ImageDraw.Draw(img)
        for dstr in ds:
            draw_path(dr, dstr, k)
        out = img.resize((size, size), Image.LANCZOS)
        out_path = os.path.join(ICON_DIR, f"{glyph}_{size}.png")
        out.save(out_path)
        print(f"{glyph}_{size}.png", os.path.getsize(out_path), "bytes")
print("done")