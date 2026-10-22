#!/usr/bin/env python3
"""Check that the Windows headers in the shared sources are guarded.

The mac build and the portable suite use the shared sources, so a
Windows header in one of them has to sit inside a _WIN32 block. A
comment that mentions the header does not count.

Only the sources that are really shared are checked. The Windows-only
files stay in the Windows build and may include the Windows headers
without a guard.

The list of the shared sources is read from the build file, so the
check follows the build.

Usage: python3 tools/check_shared_guards.py
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

WINDOWS_HEADERS = ("windows.h", "d2d1.h", "dwrite.h", "d2d1_3.h",
                   "shlobj.h", "commctrl.h", "windowsx.h")

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def shared_sources():
    """The sources the mac build and the portable suite both use."""
    path = os.path.join(REPO, "CMakeLists.txt")
    with open(path, encoding="utf-8") as f:
        cmake = f.read()

    match = re.search(r"set\(MARKDOWNIT_CORE_SOURCES(.*?)\)", cmake, re.S)
    if not match:
        print("FAIL: the shared sources are not in the build file")
        sys.exit(1)

    return re.findall(r"(src/[A-Za-z0-9_/]+\.(?:cpp|h))", match.group(1))


def main() -> int:
    shared = shared_sources()
    unguarded = []

    for rel in shared:
        path = os.path.join(REPO, rel)
        if not os.path.exists(path):
            continue

        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")

        # The stack of the open preprocessor conditions. A header is
        # fine when any of them says _WIN32.
        stack = []
        for i, line in enumerate(lines):
            stripped = line.strip()

            if stripped.startswith("#if"):
                stack.append("_WIN32" in stripped)
            elif stripped.startswith("#ifdef"):
                stack.append(stripped.split()[1] == "_WIN32")
            elif stripped.startswith("#ifndef"):
                stack.append(False)
            elif stripped.startswith("#else"):
                if stack:
                    stack[-1] = not stack[-1]
            elif stripped.startswith("#endif"):
                if stack:
                    stack.pop()

            found = INCLUDE.match(line)
            if found is None:
                continue

            header = os.path.basename(found.group(1)).lower()
            if header not in WINDOWS_HEADERS:
                continue

            if any(stack):
                continue

            unguarded.append("%s:%d  %s" % (rel, i + 1, found.group(1)))

    print("the shared sources are %d files" % len(shared))
    if unguarded:
        print("UNGUARDED WINDOWS HEADERS:")
        for item in unguarded:
            print("  " + item)
        return 1

    print("every Windows header in the shared sources is guarded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
