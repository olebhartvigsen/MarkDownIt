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
- the toolbar draws system material: system symbols plus the one
  template floppy for Save, and no colours of its own;
- the format bar is built from the system's own controls, sends the
  command selectors the menu carries, and mixes in no colour of its
  own;
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
FORMAT_BAR_MM = os.path.join(MAC, "format_bar_mac.mm")

# The selectors the format bar is allowed to send: the command table's
# rows, nothing invented at the bar. A control that carried a command
# of its own would be a second command surface.
BAR_SELECTORS = [
    "toggleBold:", "toggleItalic:", "toggleStrike:", "toggleCode:",
    "toggleLink:", "insertTable:", "clearFormat:",
    "toggleBullets:", "toggleNumbering:", "toggleQuote:",
    "setBodyText:", "setHeading1:", "setHeading2:", "setHeading3:",
    "setHeading4:", "setHeading5:", "setHeading6:",
]
CMAKE = os.path.join(REPO, "CMakeLists.txt")

# The toolbar's deliberate set, in order. Everything else lives in the
# menu bar.
EXPECTED = ["MdRecent", "MdSave", "MdShare", "MdUndo", "MdRedo", "MdFind",
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

    # The toolbar draws system material: system symbols and the
    # template floppy, never a raster image.
    if "imageWithSystemSymbolName" not in toolbar:
        fail("the toolbar draws no system symbols")
    if "FloppySaveImage" not in toolbar:
        fail("the Save item lost its template floppy")
    for pattern in (r"colorWith", r"0x[0-9A-Fa-f]"):
        if re.search(pattern, toolbar):
            fail("the toolbar mixes in its own colour (%s)" % pattern)

    # The format bar is a row of the system's own controls over the
    # command table: a segmented control for the marks, a popup for the
    # paragraph style, buttons for the rest. It draws no colour of its
    # own and never invents a command.
    with open(FORMAT_BAR_MM, encoding="utf-8") as fh:
        bar = fh.read()
    for control in ("NSSegmentedControl", "NSPopUpButton", "NSButton"):
        if control not in bar:
            fail("the format bar does not build a %s" % control)
    if "imageWithSystemSymbolName" not in bar:
        fail("the format bar draws no system symbols")
    if "sendAction:" not in bar:
        fail("the format bar does not route through the responder chain")
    for selector in BAR_SELECTORS:
        if selector not in bar:
            fail("the format bar never sends %s" % selector)
    for pattern in (r"colorWith", r"0x[0-9A-Fa-f]"):
        if re.search(pattern, bar):
            fail("the format bar mixes in its own colour (%s)" % pattern)
    # The bar belongs to the document pane and follows the edit mode;
    # a strip that outlived the mode would offer dead commands.
    for needed in ("MdFormatBar", "applyFormatBarVisible", "formatBarSync"):
        if needed not in main_mm:
            fail("main_mac.mm never wires %s" % needed)

    # The raster icon set left the mac bundle with the strip.
    with open(CMAKE, encoding="utf-8") as fh:
        cmake = fh.read()
    if "Resources/icons" in cmake or "MARKDOWNIT_MAC_ICONS" in cmake:
        fail("CMake still packs the raster icon set into the mac bundle")

    print("the mac window wears native chrome: %d toolbar items, a "
          "format bar of system controls, no strip, no raster icons"
          % len(EXPECTED))


if __name__ == "__main__":
    main()