#!/usr/bin/env python3
"""Compare the Windows and mac launch captures, state by state.

The two platforms photograph the same document in the same named states
(tools/macos-smoke/launch_smoke.sh and tools/windows-smoke/capture.ps1).
Every state announces the rectangle of the document pane in the walk
log; this tool crops both pictures to that pane, scales them to one
width, and compares the layout they show: the number of text bands,
where the large gaps between them sit, and how much ink there is.

Fonts and window chrome differ by design, so the comparison is
structural, not pixel exact. What it catches is the class of defect the
pipeline exists for: a block that went missing or moved on one
platform, a spacing regression that grows a gap by a line height, a
state that stopped drawing. Every state is also checked on its own
side first: a document pane that shows almost no text fails, which is
how a dark mode that draws dark ink on the dark page is caught.

Per pair it writes a side-by-side picture and an overlay (mac ink in
red, windows ink in green, agreement in yellow) into the output
directory, prints a table, and exits 1 when a state fails.
"""
import argparse
import io
import os
import sys

from PIL import Image


def load_rects(path):
    """"<state> x,y,w,h" per line, as the smokes record it."""
    rects = {}
    with io.open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                x, y, w, h = [int(v) for v in parts[1].split(",")]
            except ValueError:
                continue
            rects[parts[0]] = (x, y, w, h)
    return rects


def crop_pane(img, rect, width, trim_right=20):
    """The document pane, normalized to `width` for both platforms."""
    x, y, w, h = rect
    right = min(img.width, x + w - trim_right)
    box = (max(0, x), max(0, y), max(1, right), min(img.height, y + h))
    pane = img.crop(box).convert("L")
    if pane.width < 32 or pane.height < 32:
        raise ValueError("pane rectangle is outside the capture")
    scale = width / float(pane.width)
    return pane.resize((width, max(1, int(round(pane.height * scale)))),
                       Image.LANCZOS)


def scan_bands(pane):
    """Text bands, the gaps between them, and the ink share.

    A band is a run of rows that contain ink. Runs closer than three
    rows merge, which folds anti-aliasing fringes back into their band.
    """
    w, h = pane.size
    data = list(pane.tobytes())
    background = sorted(data)[len(data) // 2]
    rows = [0] * h
    ink_mask = bytearray(w * h)
    for i, v in enumerate(data):
        if abs(v - background) > 55:
            rows[i // w] += 1
            ink_mask[i] = 255

    bands = []
    start = None
    for y, count in enumerate(rows):
        if count > 0 and start is None:
            start = y
        elif count == 0 and start is not None:
            bands.append((start, y))
            start = None
    if start is not None:
        bands.append((start, h))
    merged = []
    for band in bands:
        if merged and band[0] - merged[-1][1] < 3:
            merged[-1] = (merged[-1][0], band[1])
        else:
            merged.append(band)
    gaps = [merged[i + 1][0] - merged[i][1]
            for i in range(len(merged) - 1)]
    ink = sum(rows)
    return merged, gaps, ink, (w, h, ink_mask)


def ink_image(shape, mask):
    w, h, _ = shape
    img = Image.new("L", (w, h))
    img.putdata([255 if b else 0 for b in mask])
    return img


def block_gaps(gaps):
    """The gaps at least 1.55x the median gap: paragraph and block
    spacing rather than line pitch, which fonts move by a pixel or two."""
    if len(gaps) < 3:
        return list(gaps)
    ordered = sorted(gaps)
    median = ordered[len(ordered) // 2]
    if median <= 0:
        median = 1
    return [g for g in gaps if g >= 1.55 * median]


def max_gap(gaps):
    return max(gaps) if gaps else 0


def check_side(name, side, img_path, rect, args):
    """One platform's state on its own: the pane must show readable text.

    Readable text is strong, monochrome ink: a pixel far from the
    background in lightness and close to grey. That separates glyphs
    from the coloured fills (a find highlight, a marker wash), so a
    document drawn in the background colour fails here even when its
    highlights would still form bands. This is how a dark mode that
    paints dark ink on the dark page is caught.
    """
    try:
        pane = crop_pane(Image.open(img_path), rect, args.width)
    except ValueError as exc:
        return "FAIL", "rect outside the capture: %s" % exc
    bands, _gaps, _ink, _shape = scan_bands(pane)

    rgb = Image.open(img_path).convert("RGB")
    x, y, w, h = rect
    box = (max(0, x), max(0, y),
           min(rgb.width, x + w - 20), min(rgb.height, y + h))
    region = rgb.crop(box).resize(pane.size, Image.LANCZOS)
    grey = list(pane.tobytes())
    data = list(region.tobytes())
    background = sorted(grey)[len(grey) // 2]
    strong = 0
    for i, v in enumerate(grey):
        r = data[i * 3]
        b = data[i * 3 + 2]
        if abs(v - background) > 90 and abs(r - b) < 50:
            strong += 1
    share = strong / float(len(grey))
    if len(bands) < 5 or share < 0.0025:
        return "FAIL", ("renders almost no readable text "
                        "(%d bands, strong ink %.2f%%)"
                        % (len(bands), share * 100.0))
    return "PASS", "%d bands, strong ink %.2f%%" % (len(bands), share * 100.0)


def compare_state(name, mac_img, mac_rect, win_img, win_rect, args, out_dir):
    width = args.width
    try:
        mac = crop_pane(mac_img, mac_rect, width)
        win = crop_pane(win_img, win_rect, width)
    except ValueError as exc:
        return "FAIL", "rect outside the capture: %s" % exc, None, None, None

    # Both panes show the document from its top; compare the shared
    # height when a title bar makes one pane a little taller.
    common = min(mac.height, win.height)
    mac = mac.crop((0, 0, width, common))
    win = win.crop((0, 0, width, common))

    mac_bands, mac_gaps, mac_ink, mac_shape = scan_bands(mac)
    win_bands, win_gaps, win_ink, win_shape = scan_bands(win)

    facts = {
        "mac": (len(mac_bands), mac_gaps, mac_ink, common),
        "win": (len(win_bands), win_gaps, win_ink, common),
    }
    failures = []
    warnings = []

    if len(mac_bands) < 5 or len(win_bands) < 5:
        failures.append("almost no text (mac %d bands, windows %d bands)"
                        % (len(mac_bands), len(win_bands)))

    count_delta = abs(len(mac_bands) - len(win_bands))
    if count_delta > args.count_tolerance:
        failures.append("text band count differs by %d (mac %d, windows %d)"
                        % (count_delta, len(mac_bands), len(win_bands)))
    elif count_delta:
        warnings.append("text band count differs by %d "
                        "(mac %d, windows %d)"
                        % (count_delta, len(mac_bands), len(win_bands)))

    if mac_bands and win_bands:
        top_delta = abs(mac_bands[0][0] - win_bands[0][0])
        if top_delta > args.top_tolerance:
            failures.append("first text row differs by %d px" % top_delta)
        elif top_delta:
            warnings.append("first text row differs by %d px" % top_delta)

    mac_block = block_gaps(mac_gaps)
    win_block = block_gaps(win_gaps)
    if count_delta <= args.count_tolerance and len(mac_block) == len(win_block):
        worst = 0
        for mg, wg in zip(mac_block, win_block):
            worst = max(worst, abs(mg - wg))
        if worst > args.gap_tolerance:
            failures.append("a block gap differs by %d px" % worst)
        elif worst:
            warnings.append("worst block gap delta %d px" % worst)
    else:
        # Different block counts break the pairing; fall back to the
        # loudest structural signal, the deepest gap.
        gap_delta = abs(max_gap(mac_gaps) - max_gap(win_gaps))
        if gap_delta > args.gap_tolerance + 8:
            failures.append("deepest gap differs by %d px" % gap_delta)
        warnings.append("block gap counts differ (mac %d, windows %d)"
                        % (len(mac_block), len(win_block)))

    total = float(mac.width * common) * 2.0
    ink_delta = abs(mac_ink - win_ink) / max(1.0, total)
    if ink_delta > 0.06:
        warnings.append("ink share differs by %.1f%%" % (ink_delta * 100.0))

    # Pictures for the eye: side by side, and the two inks overlaid.
    side = Image.new("L", (width * 2 + 8, common), 255)
    side.paste(mac, (0, 0))
    side.paste(win, (width + 8, 0))
    side.save(os.path.join(out_dir, "%s_side.png" % name))
    empty = Image.new("L", (width, common), 0)
    overlay = Image.merge("RGB", (ink_image(mac_shape, mac_shape[2]),
                                  ink_image(win_shape, win_shape[2]),
                                  empty))
    overlay.save(os.path.join(out_dir, "%s_overlay.png" % name))

    status = "FAIL" if failures else ("WARN" if warnings else "PASS")
    detail = "; ".join(failures + warnings) if (failures or warnings) else "ok"
    return status, detail, facts, mac_block, win_block


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--macos", required=True,
                    help="directory with the mac captures and rects.txt")
    ap.add_argument("--windows", required=True,
                    help="directory with the Windows captures and rects.txt")
    ap.add_argument("--out", required=True,
                    help="directory for the report and the diff pictures")
    ap.add_argument("--width", type=int, default=760,
                    help="common width both panes are scaled to")
    ap.add_argument("--count-tolerance", type=int, default=2,
                    help="allowed text band count difference")
    ap.add_argument("--gap-tolerance", type=int, default=12,
                    help="allowed block gap difference in pixels")
    ap.add_argument("--top-tolerance", type=int, default=8,
                    help="allowed first-text-row difference in pixels")
    ap.add_argument("--min-states", type=int, default=3,
                    help="fewest common states the run must provide")
    args = ap.parse_args()

    mac_rects = load_rects(os.path.join(args.macos, "rects.txt"))
    win_rects = load_rects(os.path.join(args.windows, "rects.txt"))
    if not mac_rects or not win_rects:
        print("no rectangles recorded (mac %d, windows %d): the capture "
              "scripts did not run" % (len(mac_rects), len(win_rects)))
        return 1

    all_states = [s for s in ("doc", "find", "edit", "outline", "source",
                              "dark", "settings")
                  if s in mac_rects or s in win_rects]
    common = [s for s in all_states
              if s in mac_rects and s in win_rects]
    if len(common) < args.min_states:
        print("only %d common states (%s): the comparison needs at least %d"
              % (len(common), ", ".join(common), args.min_states))
        return 1

    if not os.path.isdir(args.out):
        os.makedirs(args.out)

    report = []
    failed = 0

    # Every state is checked on its own first, on each side that has a
    # capture: a pane without text is a failure even without a pair.
    for state in all_states:
        for side, root, rects in (("mac", args.macos, mac_rects),
                                  ("win", args.windows, win_rects)):
            if state not in rects:
                continue
            path = os.path.join(root, state + ".png")
            status, detail = check_side(state, side, path, rects[state],
                                        args)
            if status == "FAIL":
                failed += 1
            print("%-8s %-5s %-4s %s" % (state, side, status, detail))
            report.append("%s (%s) on its own: %s - %s"
                          % (state, side, status, detail))

    for state in common:
        mac_img = Image.open(os.path.join(args.macos, state + ".png"))
        win_img = Image.open(os.path.join(args.windows, state + ".png"))
        status, detail, facts, mac_block, win_block = compare_state(
            state, mac_img, mac_rects[state], win_img, win_rects[state],
            args, args.out)
        if status == "FAIL":
            failed += 1
        print("%-8s %-5s %-4s %s" % (state, "pair", status, detail))
        report.append("state %s, pair: %s - %s" % (state, status, detail))
        if facts:
            report.append("  mac:     %d bands, ink %d px, height %d"
                          % (facts["mac"][0], facts["mac"][2], facts["mac"][3]))
            report.append("  windows: %d bands, ink %d px, height %d"
                          % (facts["win"][0], facts["win"][2], facts["win"][3]))
        if mac_block is not None:
            report.append("  mac block gaps:     %s"
                          % ", ".join(str(g) for g in mac_block))
            report.append("  windows block gaps: %s"
                          % ", ".join(str(g) for g in win_block))

    mac_only = [s for s in all_states if s in mac_rects and s not in win_rects]
    win_only = [s for s in all_states if s in win_rects and s not in mac_rects]
    if mac_only:
        report.append("mac-only states, not paired: %s" % ", ".join(mac_only))
    if win_only:
        report.append("windows-only states, not paired: %s"
                      % ", ".join(win_only))

    with io.open(os.path.join(args.out, "report.txt"), "w",
                 encoding="utf-8") as f:
        f.write("\n".join(report) + "\n")

    print("compared %d states in %d pairs, %d failed"
          % (len(all_states), len(common), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
