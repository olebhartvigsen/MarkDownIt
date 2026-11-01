#!/usr/bin/env python3
"""Compare the Windows and the macOS QA captures, state by state.

Both apps walk the same named states (MARKDOWNIT_QA_STATES) and
announce their document rectangle, so the smoke on each platform
photographs the same document moments. This tool crops both pictures
to that rectangle and compares what should be identical between the
two builds: the block structure, the rhythm, and the ink.

The platforms use different fonts (Segoe UI and SF Pro), so their wrap
points and exact leading differ a little. The checks below pair the
text rows by index and compare the gap between each pair of rows,
which is a local measure: the fonts' pitch difference stays at 3-5 px
per row no matter how far down the page you look, while a real defect
(a full empty row that should have collapsed, a dialog over the
document, a dead line height, an unreadable page) moves a single gap
or the ink far outside those bounds.

Usage:
  compare_platform_captures.py --macos DIR --windows DIR --out DIR

DIR is a directory holding rects.txt and <state>.png. Exit code 0
when every state passes, 1 otherwise. Writes side-by-side and overlay
pictures per state into OUT for the artifact.
"""
import argparse
import os
import statistics
import sys
from PIL import Image

# The states both walks photograph, in order.
STATES = ["doc", "find", "edit", "outline", "source", "dark", "settings"]

# Tolerances, calibrated on the fonts alone: the row gap differs by up
# to 5 px (the wrap difference moves band counts by up to 2, the first
# row by up to 5 px) and the body pitch by up to 4 px. The real
# defects sit far outside: a missing blank-line strip adds 21-31 px to
# one gap, the line-height bug moved the body pitch by 7 px, and a
# dialog or an unreadable page kills the ink.
TOL_COUNT = 2
TOL_FIRST_ROW = 8
TOL_GAP = 12
TOL_BODY_PITCH = 5
MIN_INK = 0.5  # percent of the pane

# The source view is one monospace buffer, and the two monospace fonts
# wrap it at different words, so its text rows cannot be paired by
# index; it is measured by its pitch instead, with a wider count margin
# for the wrap difference.
WRAP_STATES = {"source"}
WRAP_TOL_COUNT = 4

TRIM_RIGHT = 20  # the scrollbar edge


def parse_rects(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        parts = line.split()
        if len(parts) >= 2:
            nums = [int(v) for v in parts[1].split(",")]
            if len(nums) == 4:
                out[parts[0]] = nums
    return out


def load_pane(png_path, rect):
    img = Image.open(png_path).convert("L")
    x, y, w, h = rect
    x = max(0, min(x, img.width - 1))
    y = max(0, min(y, img.height - 1))
    w = min(w, img.width - x)
    h = min(h, img.height - y)
    return img.crop((x, y, x + max(1, w - TRIM_RIGHT), y + h))


def band_starts(pane):
    """Rows of text: runs of rows with ink, merged across tiny gaps."""
    W, H = pane.size
    data = list(pane.tobytes())
    bg = max(set(data), key=data.count)
    rows = [0] * H
    for i, v in enumerate(data):
        if abs(v - bg) > 55:
            rows[i // W] += 1
    bands = []
    start = None
    for yy, c in enumerate(rows):
        if c and start is None:
            start = yy
        elif not c and start is not None:
            bands.append((start, yy))
            start = None
    if start is not None:
        bands.append((start, H))
    merged = []
    for b in bands:
        if merged and b[0] - merged[-1][1] < 3:
            merged[-1] = (merged[-1][0], b[1])
        else:
            merged.append(b)
    return merged


def ink_percent(pane):
    """Ink either polarity: text on a light page or on a dark one."""
    data = list(pane.tobytes())
    bg = max(set(data), key=data.count)
    off = sum(1 for v in data if abs(v - bg) > 55)
    return 100.0 * off / max(1, len(data))


def compare_state(mac_pane, win_pane, name):
    fails = []
    warns = []

    for side, pane in (("mac", mac_pane), ("win", win_pane)):
        ink = ink_percent(pane)
        tag = "PASS" if ink >= MIN_INK else "FAIL"
        print("  %-8s %-4s %s %.2f%% ink, %d text rows"
              % (name, side, tag, ink, len(band_starts(pane))))
        if ink < MIN_INK:
            fails.append("%s shows almost no readable text (%.2f%% ink)"
                         % (side, ink))

    m_bands = band_starts(mac_pane)
    w_bands = band_starts(win_pane)
    common = min(mac_pane.size[1], win_pane.size[1])
    m_in = [b for b in m_bands if b[0] < common]
    w_in = [b for b in w_bands if b[0] < common]
    if len(m_in) < 2 or len(w_in) < 2:
        fails.append("%s: text missing on one side" % name)
        return fails, warns

    d_first = abs(m_in[0][0] - w_in[0][0])
    if d_first > TOL_FIRST_ROW:
        fails.append("first text row differs by %d px" % d_first)
    elif d_first:
        warns.append("first text row differs by %d px" % d_first)

    wrap_state = name in WRAP_STATES
    count_tol = WRAP_TOL_COUNT if wrap_state else TOL_COUNT
    d_count = abs(len(m_in) - len(w_in))
    if d_count > count_tol:
        fails.append("text row count differs by %d (mac %d, windows %d)"
                     % (d_count, len(m_in), len(w_in)))
    elif d_count:
        warns.append("text row count differs by %d (mac %d, windows %d)"
                     % (d_count, len(m_in), len(w_in)))

    # The gaps between rows, paired by index: row i on one side is row
    # i on the other as long as the wraps agree, and a local gap is
    # immune to any drift accumulated above it. The source view wraps
    # differently by design, so it is measured by pitch instead.
    if not wrap_state:
        pairs = min(len(m_in), len(w_in)) - 1
        worst = 0
        worst_at = -1
        for i in range(pairs):
            mg = m_in[i + 1][0] - m_in[i][1]
            wg = w_in[i + 1][0] - w_in[i][1]
            if abs(mg - wg) > worst:
                worst = abs(mg - wg)
                worst_at = i
            if abs(mg - wg) > TOL_GAP:
                fails.append("gap after row %d differs by %d px (mac %d, "
                             "windows %d)" % (i + 1, abs(mg - wg), mg, wg))
        if worst and not [f for f in fails if "gap after row" in f]:
            warns.append("worst row gap delta %d px (after row %d)"
                         % (worst, worst_at + 1))

    # The body pitch: the median gap across the first few rows, which
    # is pure line height in both documents. On the source view the
    # same measure still holds, over the rows both sides have.
    if len(m_in) > 4 and len(w_in) > 4:
        if wrap_state:
            m_body = statistics.median(
                [m_in[i + 1][0] - m_in[i][1] for i in range(len(m_in) - 1)])
            w_body = statistics.median(
                [w_in[i + 1][0] - w_in[i][1] for i in range(len(w_in) - 1)])
        else:
            m_body = statistics.median(
                [m_in[i + 1][0] - m_in[i][1] for i in range(1, 4)])
            w_body = statistics.median(
                [w_in[i + 1][0] - w_in[i][1] for i in range(1, 4)])
        d_pitch = abs(m_body - w_body)
        if d_pitch > TOL_BODY_PITCH:
            fails.append("body pitch differs by %.0f px (mac %.0f, "
                         "windows %.0f)" % (d_pitch, m_body, w_body))
        elif d_pitch:
            warns.append("body pitch differs by %.0f px" % d_pitch)

    return fails, warns


def write_pictures(mac_pane, win_pane, out_dir, name):
    h = max(mac_pane.size[1], win_pane.size[1])
    w = mac_pane.size[0] + win_pane.size[0] + 8
    side = Image.new("L", (w, h), 255)
    side.paste(mac_pane, (0, 0))
    side.paste(win_pane, (mac_pane.size[0] + 8, 0))
    side.save(os.path.join(out_dir, "%s_side.png" % name))

    common_h = min(mac_pane.size[1], win_pane.size[1])
    ww = max(mac_pane.size[0], win_pane.size[0])
    m_crop = mac_pane.crop((0, 0, mac_pane.size[0],
                            min(common_h, mac_pane.size[1])))
    w_crop = win_pane.crop((0, 0, win_pane.size[0],
                            min(common_h, win_pane.size[1])))
    over = Image.merge("RGB", (
        m_crop.resize((ww, common_h)),
        w_crop.resize((ww, common_h)),
        Image.new("L", (ww, common_h), 0),
    ))
    over.save(os.path.join(out_dir, "%s_overlay.png" % name))


# The chrome landmarks: the surfaces around the document must wear the
# same paint on both platforms. Each block sits in a blank stretch (the
# strip right of the buttons, the pane's empty bottom, the tail of the
# active outline row), far from text and antialiasing; any real palette
# difference sits far outside the tolerance.
CHROME_TOL = 16


def sample_block(img, cx, cy, w=20, h=8, dom=False):
    """The block's average colour, or its dominant colour with dom."""
    x1 = min(img.width, cx + w)
    y1 = min(img.height, cy + h)
    box = img.crop((max(0, cx), max(0, cy), x1, y1))
    raw = box.tobytes()
    data = list(zip(raw[0::3], raw[1::3], raw[2::3]))
    if not data:
        return (0, 0, 0)
    if dom:
        return max(set(data), key=data.count)
    n = len(data)
    return (sum(p[0] for p in data) // n, sum(p[1] for p in data) // n,
            sum(p[2] for p in data) // n)


def chrome_landmarks(state, rect, png_path):
    """The spots to sample for a state, relative to its pane rect."""
    x, y, w, h = rect
    spots = []
    if state in ("doc", "find", "edit"):
        # The strip's blank right of the button row, above the pane.
        spots.append(("strip", 952, y - 30, 20, 8, False))
    if state == "outline":
        # The outline pane is left of the document pane; the rect marks
        # the document, so the pane region is derived from it.
        spots.append(("pane", 20, y + h - 34, 24, 10, False))
        # The active row: the dominant colour of its first text band,
        # which is the fill behind the heading's letters.
        outline = load_pane(png_path, (12, y, x - 36, h))
        # Hairlines at the pane's edge are bands too; a text row is
        # taller.
        bands = [b for b in band_starts(outline) if b[1] - b[0] >= 4]
        if bands:
            y0 = y + bands[0][0]
            y1 = y + bands[0][1] + 2
            spots.append(("row", 14, y0, max(2, x - 42),
                          max(2, y1 - y0), True))
    return spots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--macos", required=True)
    ap.add_argument("--windows", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    mac_rects = parse_rects(os.path.join(args.macos, "rects.txt"))
    win_rects = parse_rects(os.path.join(args.windows, "rects.txt"))

    compared = 0
    failed = 0
    for state in STATES:
        m_png = os.path.join(args.macos, "%s.png" % state)
        w_png = os.path.join(args.windows, "%s.png" % state)
        if state not in mac_rects or state not in win_rects:
            continue
        if not os.path.exists(m_png) or not os.path.exists(w_png):
            continue
        mac_pane = load_pane(m_png, mac_rects[state])
        win_pane = load_pane(w_png, win_rects[state])
        fails, warns = compare_state(mac_pane, win_pane, state)
        if fails:
            failed += 1
            print("  %-8s pair FAIL %s" % (state, "; ".join(fails)))
        elif warns:
            print("  %-8s pair WARN %s" % (state, "; ".join(warns)))
        else:
            print("  %-8s pair PASS" % state)
        write_pictures(mac_pane, win_pane, args.out, state)
        compared += 1

    # The chrome: the surfaces the app draws around the document.
    for state in STATES:
        if state not in mac_rects or state not in win_rects:
            continue
        m_png = os.path.join(args.macos, "%s.png" % state)
        w_png = os.path.join(args.windows, "%s.png" % state)
        if not os.path.exists(m_png) or not os.path.exists(w_png):
            continue
        m_img = Image.open(m_png).convert("RGB")
        w_img = Image.open(w_png).convert("RGB")
        m_spots = chrome_landmarks(state, mac_rects[state], m_png)
        w_spots = chrome_landmarks(state, win_rects[state], w_png)
        for (label, mx, my, mw, mh, dm), (_, wx, wy, ww, wh, dw) in zip(
                m_spots, w_spots):
            mc = sample_block(m_img, mx, my, mw, mh, dm)
            wc = sample_block(w_img, wx, wy, ww, wh, dw)
            delta = max(abs(mc[i] - wc[i]) for i in range(3))
            if delta > CHROME_TOL:
                failed += 1
                print("  %-8s chrome FAIL %s: mac %s vs windows %s "
                      "(delta %d)" % (state, label, mc, wc, delta))
            else:
                print("  %-8s chrome ok   %s: mac %s windows %s"
                      % (state, label, mc, wc))

    print("compared %d states, %d failed" % (compared, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
