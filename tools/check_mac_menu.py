#!/usr/bin/env python3
"""Check that every command of the mac menu is carried out.

A command in the menu that no handler acts on is a silent no-op, and
that is the kind of gap the parity pass is meant to catch. The check
walks the two places a command has to be: the menu offers it, and the
app delegate carries it out. The items carry no target of their own, so
AppKit finds the delegate through the responder chain; the check also
makes sure the menu never hands an item a target of its own again,
because a target that does not implement the selectors is what left
every item grey.

Usage: python3 tools/check_mac_menu.py
"""

import os
import re
import sys

# The repository root, from the location of this script, so the check
# also runs from a CI checkout.
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MACOS = os.path.join(REPO, "src", "macos")

# The selectors AppKit carries out itself. They are not app commands.
APPKIT = {"hide", "terminate", "orderFrontStandardAboutPanel"}


def main() -> int:
    with open(os.path.join(MACOS, "menu_mac.mm"), encoding="utf-8") as handle:
        menu = handle.read()
    with open(os.path.join(MACOS, "main_mac.mm"), encoding="utf-8") as handle:
        window = handle.read()

    # The names carry digits, so the heading selectors match too.
    selectors = sorted(set(re.findall(r"@selector\(([A-Za-z0-9]+):\)", menu)))

    problems = []

    # The items route through the responder chain, so the menu must not
    # hand an item a target of its own.
    if ".target" in menu:
        problems.append("menu_mac.mm assigns a target of its own")

    for selector in selectors:
        if selector in APPKIT:
            continue

        # The delegate carries the command out. The definition is the
        # one with a body, not a declaration.
        match = re.search(
            r"- \(void\)%s:\(id\)sender\s*\{" % re.escape(selector), window)
        if not match:
            problems.append(
                "no -[MdAppDelegate %s:] carries the command out" % selector)
            continue

        # The body has to do something, so a stub that only drops the
        # sender does not pass for a handler.
        rest = window[match.end():]
        end = rest.find("\n}")
        body = rest[:end if end >= 0 else len(rest)]
        body = body.replace("(void)sender;", "").replace("(void)sender", "")
        if len(body.strip()) < 4:
            problems.append("-[MdAppDelegate %s:] does nothing" % selector)

    count = len(selectors) - len(APPKIT)
    if problems:
        print("the mac menu check failed (%d commands checked):" % count)
        for problem in problems:
            print("  " + problem)
        return 1

    print("all %d mac menu commands are carried out" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
