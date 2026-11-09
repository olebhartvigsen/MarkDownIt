#!/usr/bin/env python3
"""Check the parity ledger against the build.

The ledger is references/platform-parity.md. It fails when:

- a manifest group is used on fewer or other surfaces than the fully
  shared quad (findings 4 and 6), and no ledger row names its sources
  or the group, so the difference is undocumented drift;
- a ledger row names a source path that does not exist, so the row is
  stale after a rename or a move.

The fully shared quad is windows-app, macos-core, windows-tests,
macos-tests, the usage of MARKDOWNIT_CORE_SOURCES and friends. A
group that is shared on all four needs no row. Any other group is a
difference that the ledger must explain.

Usage: python3 tools/check_parity_ledger.py [repo-root]
"""

import os
import re
import sys

QUAD = {"windows-app", "macos-core", "windows-tests", "macos-tests"}

GROUP_RE = re.compile(
    r"#\s*usage:\s*([\w\- ]+)\s*\nset\((MARKDOWNIT_\w+)\s*\n"
    r"((?:\s+[\w/.\-]+\n)+)\)", re.M)
FILE_RE = re.compile(r"^\s*(src/[\w/.\-]+\.(?:cpp|mm|c))$", re.M)


def parse_manifest(text):
    groups = {}
    for usage, name, body in GROUP_RE.findall(text):
        groups[name] = (set(usage.split()), FILE_RE.findall(body))
    return groups


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "cmake", "sources.cmake"),
              encoding="utf-8") as f:
        manifest = f.read()
    groups = parse_manifest(manifest)
    with open(os.path.join(root, "references", "platform-parity.md"),
              encoding="utf-8") as f:
        ledger = f.read()
    errors = []

    # The source paths the ledger mentions, from all tables and lists.
    mentioned = set(re.findall(r"`(src/[\w/.\-]+\.(?:cpp|mm|c))`", ledger))

    # Stale rows: a mentioned path must exist.
    for path in sorted(mentioned):
        if not os.path.exists(os.path.join(root, path)):
            errors.append("%s is named in the ledger but does not exist "
                          "on disk. Remove or update the row." % path)

    # Every group whose usage is not the shared quad must be explained:
    # the ledger names one of its files, or states the file/group name
    # in a row. Search by file name and by file stem (without dirs).
    stems = set()
    for path in mentioned:
        stems.add(os.path.basename(path))
    for name, (usage, files) in sorted(groups.items()):
        if usage == QUAD:
            continue
        ok = False
        for f in files:
            if "`%s`" % f in ledger or os.path.basename(f) in stems:
                ok = True
                break
        if name.lower().replace("_", " ") in ledger.lower():
            ok = True
        if not ok:
            errors.append(
                "%s is built on %s, not the shared quad %s, and the "
                "ledger has no row for %s. Add a row with the reason to "
                "references/platform-parity.md." %
                (name, ", ".join(sorted(usage)), ", ".join(sorted(QUAD)),
                 ", ".join(files)))

    # Files on disk that no target builds: the unbuilt section must know
    # them, unless they are mac files (check_mac_sources.py owns those).
    built = set()
    for _, (_, files) in groups.items():
        built.update(files)
    with open(os.path.join(root, "CMakeLists.txt"),
              encoding="utf-8") as f:
        cmake = f.read()
    built.update(FILE_RE.findall(cmake))
    unbuilt = []
    for sub in ("", "mermaid", "office"):
        d = os.path.join(root, "src", sub)
        for n in sorted(os.listdir(d)):
            if not n.endswith((".cpp", ".mm", ".c")):
                continue
            rel = "src/%s%s" % (sub and sub + "/", n)
            if rel not in built:
                unbuilt.append(rel)

    unbuilt_section = re.search(
        r"## Sources built by no target\n(.*?)(?=\n## |\Z)", ledger, re.S)
    listed = set(re.findall(r"`(src/[\w/.\-]+\.(?:cpp|mm|c))`",
                            unbuilt_section.group(1))) if unbuilt_section else set()
    for rel in unbuilt:
        if rel not in listed:
            errors.append(
                "%s is built by no target and is not in the unbuilt "
                "section of the ledger." % rel)

    print("parity ledger: %d sources mentioned, %d groups, %d unbuilt "
          "files" % (len(mentioned), len(groups), len(unbuilt)))
    if errors:
        for e in errors:
            print("FAIL: " + e)
        return 1
    print("every platform difference in the build has a ledger row with "
          "a reason, and no row is stale")
    return 0


if __name__ == "__main__":
    sys.exit(main())
