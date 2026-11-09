#!/usr/bin/env python3
"""Compare the Windows and macOS test inventories.

Both test exes answer `--list` (see tests/gtest_lite.h) with one
suite.case per line. This check compares the two inventories and holds
them against the "Tests built on one platform" table in
references/platform-parity.md.

It fails when:

- a suite.case exists on one platform only and no table row covers it,
  so a test quietly stopped running somewhere (findings 4 and 8);
- a row's pattern matches nothing on its platform, so the row is
  stale after a rename or a move.

Usage:
  python3 tools/check_test_inventory.py --windows F --macos F [--root R]

A row matches a suite.case when the row pattern (a suite name with an
optional `.*`) covers the case name.
"""

import os
import re
import sys

ROW_RE = re.compile(r"^\|\s*([A-Za-z0-9_]+(?:\.\*)?)\s*\|\s*(\w+)\s*\|")


def read_inventory(path):
    with open(path, encoding="utf-8") as fh:
        return {line.strip() for line in fh if line.strip()}


def read_rows(root):
    path = os.path.join(root, "references", "platform-parity.md")
    rows = []
    with open(path, encoding="utf-8") as fh:
        in_section = False
        past_rule = False
        for line in fh:
            if not in_section:
                if line.startswith("## Tests built on one platform"):
                    in_section = True
                continue
            if line.startswith("## "):
                break
            if not past_rule:
                # skip the header row, stop at the |---|---| rule;
                # lines without a pipe are prose and are skipped too
                if not line.startswith("|"):
                    continue
                past_rule = set(line.strip()) <= set("|-: ")
                continue
            m = ROW_RE.match(line)
            if m:
                rows.append((m.group(1), m.group(2)))
    return rows


def matches(pattern, name):
    if pattern == ".*":
        return True
    if pattern.endswith(".*"):
        return name.startswith(pattern[:-2] + ".")
    return name == pattern


def main():
    args = sys.argv[1:]
    flags = {}
    it = iter(args)
    for a in it:
        if a in ("--windows", "--macos"):
            flags[a] = next(it)
        elif a == "--root":
            flags[a] = next(it)
        else:
            print("FAIL: unknown argument %s" % a)
            return 2
    missing = [k for k in ("--windows", "--macos") if k not in flags]
    if missing:
        print("FAIL: %s is required" % " and ".join(missing))
        return 2
    root = flags.get("--root", os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))))

    win = read_inventory(flags["--windows"])
    mac = read_inventory(flags["--macos"])
    rows = read_rows(root)
    errors = []
    for pattern, platform in rows:
        if platform not in ("windows", "macos"):
            errors.append("row %s has platform %s, want windows or macos"
                          % (pattern, platform))
        pool = win if platform == "windows" else mac
        if not any(matches(pattern, name) for name in pool):
            errors.append("row %s (%s) matches no test in the %s "
                          "inventory. Remove it after a rename, or move "
                          "the test back." % (pattern, platform, platform))

    for name in sorted(win - mac):
        if not any(p == "windows" and matches(pattern, name)
                  for pattern, p in rows):
            errors.append("%s runs on Windows only and has no ledger row. "
                          "Give both platforms the test, or a row under "
                          "Tests built on one platform with the reason."
                          % name)
    for name in sorted(mac - win):
        if not any(p == "macos" and matches(pattern, name)
                  for pattern, p in rows):
            errors.append("%s runs on macOS only and has no ledger row. "
                          "Give both platforms the test, or a row under "
                          "Tests built on one platform with the reason."
                          % name)

    print("test inventory: %d windows, %d macos, %d shared, %d rows" %
          (len(win), len(mac), len(win & mac), len(rows)))
    if errors:
        for e in errors:
            print("FAIL: " + e)
        return 1
    print("the two test inventories agree, and every difference has a "
          "ledger row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
