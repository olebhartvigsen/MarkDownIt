#!/usr/bin/env python3
"""Check that every ribbon command exists on the mac side.

The ledger holds the mapping: references/platform-parity.md, table
"Commands: ribbon to mac menu". The check fails when:

- a ribbon command in src/ribbon.xml is in the mapping table but the
  selector is not in menu_mac.mm or toolbar_mac.mm, so the row promises
  something the mac side does not carry out;
- a ribbon command is in no table and has no exception row, so a
  command exists only on Windows;
- a table row names a command or selector that does not exist, so the
  row is stale after a rename.

The ribbon also carries structural names: tabs, groups, the quick
access toolbar and the split buttons. They mark up the chrome, they
are not commands, and the check leaves them alone.

Usage: python3 tools/check_command_coverage.py [repo-root]
"""

import os
import re
import sys

STRUCTURAL = {
    "cmdAppMenu", "cmdQAT", "cmdTableTools",
} | {  # ribbon tabs
    "cmdTabHome", "cmdTabInsert",
} | {  # ribbon groups, including the width split button
    "cmdGroupEdit", "cmdGroupFile", "cmdGroupFind", "cmdGroupFormat",
    "cmdGroupHeadings", "cmdGroupHistory", "cmdGroupMarkers",
    "cmdGroupOffice", "cmdGroupParagraph", "cmdGroupTable", "cmdGroupView",
    "cmdWidthMenu",
}


def section(ledger, title, _next_titles=None):
    """Return the text of one markdown section, up to the next one."""
    start = ledger.index("## " + title) + len(title) + 4
    end = ledger.find("\n## ", start)
    return ledger[start:end if end != -1 else len(ledger)]


def table(body):
    """Data rows of a markdown table, after its header and rule."""
    rows = []
    past_rule = False
    for line in body.splitlines():
        if not line.startswith("|"):
            continue
        if not past_rule:
            past_rule = set(line) <= set("|-: ")  # the |---|---| rule
            continue
        rows.append([c.strip() for c in line.strip("|").split("|")])
    return rows


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    ribbon = read(os.path.join(root, "src", "ribbon.xml"))
    menu = read(os.path.join(root, "src", "macos", "menu_mac.mm"))
    toolbar = read(os.path.join(root, "src", "macos", "toolbar_mac.mm"))
    ledger = read(os.path.join(root, "references", "platform-parity.md"))
    errors = []

    commands = sorted(set(re.findall(r'Command Name="(cmd\w+)"', ribbon)))
    mac = set(re.findall(r"@selector\((\w+):?\)", menu)) | \
        set(re.findall(r"action:@selector\((\w+)", toolbar))

    titles = ["Standing differences", "Commands: ribbon to mac menu",
              "Commands without a mac counterpart",
              "Mac commands without a ribbon counterpart",
              "Sources built by no target"]
    mapping_rows = table(section(ledger, titles[1], titles[2:]))
    mapping = {r[0]: r[1] for r in mapping_rows if len(r) >= 2}
    no_mac = {r[0] for r in table(section(ledger, titles[2], titles[3:]))
              if r and not r[0].startswith("---")}
    mac_only = {r[0] for r in table(section(ledger, titles[3], titles[4:]))
                if r and not r[0].startswith("---")}

    command_count = [c for c in commands if c not in STRUCTURAL]

    # Ribbon command without mac entry and without an exception row.
    for c in command_count:
        if c in mapping and c in no_mac:
            errors.append("%s is both in the mapping table and in the "
                          "no-mac table. Keep it in one." % c)
        selector = mapping.get(c)
        if selector:
            if selector not in mac:
                errors.append(
                    "%s maps to %s, but menu_mac.mm and toolbar_mac.mm "
                    "carry no such selector. Fix the row or implement "
                    "the selector." % (c, selector))
        elif c not in no_mac:
            errors.append(
                "ribbon command %s has no mac entry and no row in the "
                "no-mac table. Add the selector to the mac menu or "
                "toolbar, or a row with the reason to "
                "references/platform-parity.md." % c)

    # Stale rows on both sides.
    real = set(command_count)
    for c in sorted(set(mapping) - real):
        errors.append("%s is mapped in the ledger but is not a ribbon "
                      "command. Remove the row." % c)
    for c in sorted(no_mac - real):
        errors.append("%s is in the no-mac table but is not a ribbon "
                      "command. Remove the row." % c)
    for sel in sorted(mac_only - mac):
        errors.append("%s is in the mac-only table but the mac sources "
                      "carry no such selector. Remove the row." % sel)

    # Mac commands outside any table must be AppKit's own.
    appkit = {"hide", "hideOtherApplications", "unhideAllApplications",
              "terminate", "orderFrontStandardAboutPanel",
              "performMiniaturize", "performZoom", "arrangeInFront",
              "toggleFullScreen"}
    in_mapping = set(mapping.values())
    for sel in sorted(mac - in_mapping - mac_only - appkit - {"widthMenu"}):
        errors.append(
            "mac selector %s is carried by menu or toolbar, is not "
            "AppKit's own, and has no ledger row. Add it to the mapping "
            "table or the mac-only table with its reason." % sel)

    print("commands: %d in the ribbon (%d structural skipped), %d mac "
          "selectors, %d mac-only rows" %
          (len(command_count), len(commands) - len(command_count),
           len(mac), len(mac_only)))
    if errors:
        for e in errors:
            print("FAIL: " + e)
        return 1
    print("every ribbon command has a mac entry, an exception row, and "
          "no ledger row is stale")
    return 0


if __name__ == "__main__":
    sys.exit(main())
