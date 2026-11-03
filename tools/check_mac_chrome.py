#!/usr/bin/env python3
"""Check that the mac window wears native chrome.

The Windows window runs a ribbon. The mac window follows the macOS
Human Interface Guidelines instead: one standard toolbar in the title
bar, system symbols without decoration, a standard title bar with the
document name, and no custom window chrome. The guidelines warn that
replicating a non-system appearance makes an app feel broken, and the
old hand-painted strip did exactly that. See
references/macos-native-chrome.md for the decisions.

This check keeps the design from drifting back:

- the ribbon strip is gone, classes and files;
- the window carries no titlebar accessories;
- the toolbar builds the deliberate item set, and only that set;
- the toolbar draws system material alone: system symbols and no
  colours of its own;
- the mac bundle no longer ships the raster icon set.

Usage: python3 tools/check_mac_chrome.py
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAC = os.path.join(REPO, "src", "macos")
MAIN_MM = os.path.join(MAC, "main_mac.mm")
TOOLBAR_MM = os.path.join(MAC, "toolbar_mac.mm")
CMAKE = os.path.join(REPO, "CMakeLists.txt")

# The toolbar's deliberate set, in order. Everything else lives in the
# menu bar.
EXPECTED = ["MdSave", "MdShare", "MdUndo", "MdRedo", "MdFind",
            "MdHighlight", "MdEditMode", "MdOutline"]


def fail(message):
    print("FAIL: %s" % message)
    sys.exit(1)


def main():
    # The strip is gone for good.
    for name in ("ribbon_mac.mm", "ribbon_mac.h"):
        if os.path.exists(os.path.join(MAC, name)):
            fail("%s is back; the mac window wears a toolbar, see "
                 "references/macos-native-chrome.md" % name)
    with open(MAIN_MM, encoding="utf-8") as fh:
        main_mm = fh.read()
    for dead in ("MdRibbonView", "MdQuickAccessView", "MdRibbonHost",
                 "NSTitlebarAccessoryViewController"):
        if dead in main_mm:
            fail("main_mac.mm still carries %s" % dead)

    # The toolbar builds the deliberate set, in order.
    with open(TOOLBAR_MM, encoding="utf-8") as fh:
        toolbar = fh.read()
    identifiers = re.findall(r'kId[A-Za-z]+ = @"([^"]+)";', toolbar)
    if identifiers != EXPECTED:
        fail("the toolbar identifiers are %s, want %s"
             % (identifiers, EXPECTED))

    # The toolbar draws system material alone.
    if "imageWithSystemSymbolName" not in toolbar:
        fail("the toolbar draws no system symbols")
    for pattern in (r"colorWith", r"0x[0-9A-Fa-f]"):
        if re.search(pattern, toolbar):
            fail("the toolbar mixes in its own colour (%s)" % pattern)

    # The raster icon set left the mac bundle with the strip.
    with open(CMAKE, encoding="utf-8") as fh:
        cmake = fh.read()
    if "Resources/icons" in cmake or "MARKDOWNIT_MAC_ICONS" in cmake:
        fail("CMake still packs the raster icon set into the mac bundle")

    print("the mac window wears native chrome: %d toolbar items, "
          "no strip, no raster icons" % len(EXPECTED))


if __name__ == "__main__":
    main()