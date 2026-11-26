#!/usr/bin/env python3
"""Ratchet the Windows painter onto the platform seams.

The port-alignment plan, section 2.2, ends with one shared painter. The mac
renderer is already there: `src/macos/renderer_mac.cpp` names no Core Graphics
or Core Text type, it draws through `platform::RenderTarget`. The Windows
renderer is not: it still names Direct2D and DirectWrite types, and every one
of those is a place where its painting cannot be shared with the mac.

This check counts those names and fails when the count grows. The number may
only go down; lower it in the same commit that moves a method onto the seam.
A rise is a regression against the architecture, not a refactor.

Usage:
  python3 tools/check_painter_seam.py           # gate: fail above the baseline
  python3 tools/check_painter_seam.py --report  # print the numbers, always 0
"""

import os
import re
import sys

# Platform graphics names, as spelled in the Windows painter. Anything that
# couples a paint call to Direct2D or DirectWrite.
WINDOWS_RAW = re.compile(
    r"\b(?:ID2D1[A-Za-z0-9_]*|IDWrite[A-Za-z0-9_]*|D3D11[A-Za-z0-9_]*)\b")

# The mac painter must stay at zero of its own platform types.
MAC_RAW = re.compile(r"\b(?:CG[A-Za-z]*Ref|CT[A-Za-z]*Ref|NSGraphicsContext)\b")

# Measured on 2026-11-26 in src/renderer.cpp. Every decrement is a method that
# moved onto platform::RenderTarget, platform::TextEngine or
# platform::TextRangeSource; write the new number here when that lands.
#
# 111 -> 85: the search and marker highlight pass became
# layout::PaintMatchHighlights and layout::PaintMarkerHighlights, the geometry
# moved behind platform::TextRangeSource (platform::DWriteRangeSource on
# Windows), and the four paint paths stopped creating brushes of their own.
#
# 85 -> 77: Renderer::DrawTable and the two highlight helpers stopped taking
# ID2D1RenderTarget*; the washes, the hairlines, the selection fills and the
# cell clip go through platform::RenderTarget, and the table no longer builds a
# brush of its own. What is left of its DirectWrite use is the cell layout the
# caret cache keeps and the row height a recordless call measures.
WINDOWS_BASELINE = 77

PAINTERS = [
    ("src/renderer.cpp", WINDOWS_RAW, WINDOWS_BASELINE, "windows painter"),
    ("src/macos/renderer_mac.cpp", MAC_RAW, 0, "mac painter"),
]


def count(pattern, text):
    return len(pattern.findall(text))


def verdict(found, baseline):
    """'ok' at or below the baseline, 'over' above it."""
    return "ok" if found <= baseline else "over"


def main(argv):
    report_only = "--report" in argv
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    failed = False
    for path, pattern, baseline, label in PAINTERS:
        full = os.path.join(repo, path)
        try:
            text = open(full, encoding="utf-8").read()
        except OSError as exc:
            print("painter seam: cannot read %s (%s)" % (path, exc))
            return 1
        found = count(pattern, text)
        state = verdict(found, baseline)
        print("painter seam: %s %d platform graphics names (baseline %d) [%s]"
              % (label, found, baseline, state))
        if state == "over":
            failed = True
            print("::error::%s names %d platform graphics types, baseline is %d."
                  % (path, found, baseline))
            print("Paint through platform::RenderTarget the way renderer_mac.cpp"
                  " does, or move the work into shared code.")
        elif found < baseline and path == "src/renderer.cpp":
            print("  the count went down: set WINDOWS_BASELINE to %d in %s"
                  % (found, os.path.basename(__file__)))
    if report_only:
        return 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
