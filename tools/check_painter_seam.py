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

It counts every Windows painter file, not only `src/renderer.cpp`. The diagram
siblings were always separate translation units, so counting one file let 127
names hide behind a baseline of 42 while four painters were deleted. The total
across the files is gated as well, because that total is the number the plan is
trying to reach zero. Only the renderer shell is left, so the file count and
the total are the same number now.

Usage:
  python3 tools/check_painter_seam.py           # gate: fail above the baseline
  python3 tools/check_painter_seam.py --report  # print the numbers, always 0
"""

import os
import re
import sys

# Platform graphics names, as spelled in the Windows painters. Anything that
# couples a paint call to Direct2D or DirectWrite.
WINDOWS_RAW = re.compile(
    r"\b(?:ID2D1[A-Za-z0-9_]*|IDWrite[A-Za-z0-9_]*|D3D11[A-Za-z0-9_]*)\b")

# The mac painter must stay at zero of its own platform types.
MAC_RAW = re.compile(r"\b(?:CG[A-Za-z]*Ref|CT[A-Za-z]*Ref|NSGraphicsContext)\b")

# Measured on 2026-11-26. Every decrement is a method or a file that moved onto
# platform::RenderTarget, platform::TextEngine or platform::TextRangeSource;
# write the new numbers here when that lands.
#
# 111 -> 85 in renderer.cpp: the search and marker highlight pass became
# layout::PaintMatchHighlights and layout::PaintMarkerHighlights, the geometry
# moved behind platform::TextRangeSource (platform::DWriteRangeSource on
# Windows), and the four paint paths stopped creating brushes of their own.
#
# 85 -> 77: Renderer::DrawTable and the two highlight helpers stopped taking
# ID2D1RenderTarget*; the washes, the hairlines, the selection fills and the
# cell clip go through platform::RenderTarget, and the table no longer builds a
# brush of its own. What is left of its DirectWrite use is the cell layout the
# caret cache keeps and the row height a recordless call measures.
#
# 77 -> 68: Renderer::DrawCodeBlock stopped taking ID2D1RenderTarget* and its
# seven brushes: the card, the selection wash and the glyphs go through
# platform::RenderTarget, the card's lines are the walk's own record, and the
# json token colours ride the shared runs instead of drawing effects. What is
# left of its DirectWrite use is the one layout the caret cache keeps and the
# search fills hit test against.
#
# 68 -> 42: Renderer::DrawMermaidBlock stopped taking ID2D1RenderTarget*, the
# last function that did, and the arrow head helper beside it went with it. The
# bands, the node shapes, the arrow heads and the dotted edges are seam
# primitives now, the labels are seam blocks through DrawLayout, and the one
# DirectWrite use left is the factory GetMermaidLayout needs to measure the
# diagram the layout pass also reads.
#
# The ratio of file to total, on 2026-11-26 after the shared mermaid painter
# took the pie and the state diagram: renderer.cpp 42, renderer_sequence.cpp
# 35, renderer_gantt.cpp 16. Total 93 of an original 127.
#
# The last two siblings then left by decision: the shared painter is
# authoritative where the two Windows bodies disagreed with it. Sequence kept
# the shared painter's 12 mermaid unit autonumber digits and its palette
# activation stroke (#666666); gantt kept its bold section titles. Both
# diagrams paint through platform::PaintMermaidDiagram now and their Windows
# files are gone, so the shell is all that is counted: 42 of an original 127.
WINDOWS_BASELINE = 42
WINDOWS_TOTAL_BASELINE = 42

PAINTERS = [
    ("src/renderer.cpp", WINDOWS_RAW, WINDOWS_BASELINE, "windows painter"),
    ("src/macos/renderer_mac.cpp", MAC_RAW, 0, "mac painter"),
]

# Files that count towards the Windows total. Kept separate from PAINTERS so a
# deleted painter shows up as a fall in the total rather than as a read error.
WINDOWS_FILES = ("src/renderer.cpp",)


def count(pattern, text):
    return len(pattern.findall(text))


def verdict(found, baseline):
    """'ok' at or below the baseline, 'over' above it."""
    return "ok" if found <= baseline else "over"


def main(argv):
    report_only = "--report" in argv
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    failed = False
    total = 0
    for path, pattern, baseline, label in PAINTERS:
        full = os.path.join(repo, path)
        try:
            text = open(full, encoding="utf-8").read()
        except OSError as exc:
            print("painter seam: cannot read %s (%s)" % (path, exc))
            return 1
        found = count(pattern, text)
        if path in WINDOWS_FILES:
            total += found
        state = verdict(found, baseline)
        print("painter seam: %s %d platform graphics names (baseline %d) [%s]"
              % (label, found, baseline, state))
        if state == "over":
            failed = True
            print("::error::%s names %d platform graphics types, baseline is %d."
                  % (path, found, baseline))
            print("Paint through platform::RenderTarget the way renderer_mac.cpp"
                  " does, or move the work into shared code.")
        elif found < baseline and pattern is WINDOWS_RAW:
            print("  the count went down: set the baseline for %s to %d in %s"
                  % (path, found, os.path.basename(__file__)))
    state = verdict(total, WINDOWS_TOTAL_BASELINE)
    print("painter seam: windows painters %d names in %d files (baseline %d)"
          " [%s]" % (total, len(WINDOWS_FILES), WINDOWS_TOTAL_BASELINE, state))
    if state == "over":
        failed = True
        print("::error::the Windows painters name %d platform graphics types in"
              " total, baseline is %d." % (total, WINDOWS_TOTAL_BASELINE))
    elif total < WINDOWS_TOTAL_BASELINE:
        print("  the total went down: set WINDOWS_TOTAL_BASELINE to %d in %s"
              % (total, os.path.basename(__file__)))
    if report_only:
        return 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
