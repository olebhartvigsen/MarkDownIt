#!/usr/bin/env python3
"""Check that every command of the mac menu is carried out.

A command in the menu that no handler acts on is a silent no-op, and
that is the kind of gap the parity pass is meant to catch. The check
walks the three places a command has to be: the menu offers it, the
adapter passes it to the window, and the window does something with it.

Usage: python3 tools/check_mac_menu.py
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MACOS = os.path.join(REPO, "src", "macos")

# The selectors AppKit handles itself. They are not commands of the app.
APPKIT = {"hide", "terminate", "orderFrontStandardAboutPanel"}


def main() -> int:
    with open(os.path.join(MACOS, "menu_mac.mm"), encoding="utf-8") as f:
        menu = f.read()
    with open(os.path.join(MACOS, "main_mac.mm"), encoding="utf-8") as f:
        window = f.read()

    selectors = sorted(set(re.findall(r"@selector\(([A-Za-z]+):\)", menu)))

    missing = []
    for sel in selectors:
        if sel in APPKIT:
            continue

        # The adapter passes the selector to the window.
        if ("delegate_ %s:nil" % sel) not in window:
            missing.append("%s (not in the adapter)" % sel)
            continue

        # The window carries the command out. The definition is the one
        # with a body, not the declaration.
        match = re.search(
            r"- \(void\)%s:\(id\)sender\s*\{" % re.escape(sel), window)
        if not match:
            missing.append("%s (not in the window)" % sel)
            continue

        # The body runs to the end of the method.
        rest = window[match.end():]
        end = rest.find("\n}")
        body = rest[:end if end >= 0 else len(rest)].strip()

        # A body that only throws the sender away does nothing.
        without_sender = body.replace("(void)sender;", "").replace("(void)sender", "")
        if len(without_sender.strip()) < 4:
            missing.append("%s (does nothing)" % sel)

    print("the mac menu offers %d commands" % (len(selectors) - len(APPKIT)))
    if missing:
        print("MISSING:")
        for item in missing:
            print("  " + item)
        return 1

    print("every command in the mac menu is carried out")
    return 0


if __name__ == "__main__":
    sys.exit(main())
