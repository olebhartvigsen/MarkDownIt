#!/usr/bin/env python3
"""Check that every source is in exactly one manifest group or one target.

Findings it guards against (plan findings 4 and 6):

- a file that sits in src/ but is in no source list, so it can rot
  without anyone noticing. The mac GLOB used to hide this on macOS;
  the manifest makes every listing explicit.
- a manifest group used by a target it was not declared for, so the
  platform builds drift apart again.
- the same file compiled twice into one target (group ref plus a
  literal), which breaks the link.

The groups and their allowed surfaces live in cmake/sources.cmake in
`usage:` comments. The surfaces:

- windows-app    the MarkDownIt exe target
- windows-tests  the MarkDownIt.tests target on Windows
- macos-core     the markdownit_core static library
- macos-app      the MarkDownItMac app bundle
- macos-tests    the MarkDownIt.tests target on macOS

Sources built by no target on purpose (src/textlayout_generic.cpp
today) are allowed only while they are listed with a reason under
"Sources built by no target" in references/platform-parity.md.

The src/macos/ listing itself is owned by tools/check_mac_sources.py;
this check reads it, but reports mac gaps through that script.

Usage: python3 tools/check_sources_listed.py [repo-root]
"""

import os
import re
import sys

SURFACES = [
    ("windows-app", "add_executable(MarkDownIt WIN32",
     "if(BUILD_TESTS AND APPLE)"),
    ("macos-tests", "if(BUILD_TESTS AND APPLE)", "elseif(BUILD_TESTS)"),
    ("windows-tests", "elseif(BUILD_TESTS)",
     "# Both platforms record the unchanged macOS SVG scene"),
    ("macos-core", "add_library(markdownit_core",
     "add_executable(MarkDownItMac"),
    ("macos-app", "add_executable(MarkDownItMac", None),
]

GROUP_RE = re.compile(
    r"#\s*usage:\s*([\w\- ]+)\s*\nset\((MARKDOWNIT_\w+)\s*\n"
    r"((?:\s+[\w/.\-]+\n)+)\)", re.M)
FILE_RE = re.compile(r"^\s*(src/[\w/.\-]+\.(?:cpp|mm|c))$", re.M)
LITERAL_RE = {name: re.compile(r"^\s+(src/[\w/.\-]+\.(?:cpp|mm|c))\s*$",
                               re.M) for name, _, _ in SURFACES}


def parse_manifest(text):
    """Return {group: (usage set, [files])} from cmake/sources.cmake."""
    groups = {}
    for usage, name, body in GROUP_RE.findall(text):
        files = FILE_RE.findall(body)
        if not files:
            raise ValueError("group %s lists no files" % name)
        for other, (_, ofiles) in groups.items():
            shared = set(files) & set(ofiles)
            if shared:
                raise ValueError("%s and %s both list %s" %
                                  (name, other, ", ".join(sorted(shared))))
        groups[name] = (set(usage.split()), files)
    return groups


def parse_ledger_unbuilt(root):
    """Files allowed to be built by nothing, with a reason string."""
    path = os.path.join(root, "references", "platform-parity.md")
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    in_section = False
    for line in lines:
        if line.startswith("## Sources built by no target"):
            in_section = True
            continue
        if in_section and line.startswith("## "):
            in_section = False
            continue
        if in_section:
            for m in re.findall(r"`(src/[\w/.\-]+)`", line):
                out[m] = line.strip()
    return out


def squash_regions(text, markers, keep_ranges):
    """Blank everything outside the keep ranges, keep markers aligned."""
    out = list("\0" * len(text))
    for a, b in keep_ranges:
        for k in range(a, b):
            out[k] = text[k]
    return "".join(out)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "CMakeLists.txt"),
              encoding="utf-8") as f:
        cmake = f.read()
    with open(os.path.join(root, "cmake", "sources.cmake"),
              encoding="utf-8") as f:
        manifest = f.read()
    groups = parse_manifest(manifest)
    unbuilt_allowed = parse_ledger_unbuilt(root)
    errors = []

    # Surface segments in CMakeLists.txt.
    segments = []
    pos = 0
    for name, start, end in SURFACES:
        i = cmake.index(start, pos)
        j = len(cmake) if end is None else cmake.index(end, i)
        segments.append((name, i, j))
        pos = i

    # Group usage must match the declared surfaces exactly.
    for name, (usage, files) in sorted(groups.items()):
        for s in usage - {n for n, _, _ in SURFACES}:
            errors.append("sources.cmake: %s names unknown surface %s" %
                          (name, s))
        for f in files:
            if not os.path.exists(os.path.join(root, f)):
                errors.append("%s is in %s but does not exist" % (f, name))
    group_of = {}
    for name, (_, files) in groups.items():
        for f in files:
            group_of[f] = name
    for name, i, j in segments:
        seg = cmake[i:j]
        for gname, (usage, _) in sorted(groups.items()):
            if ("${%s}" % gname) in seg:
                if name not in usage:
                    errors.append(
                        "%s is used on %s, which cmake/sources.cmake does "
                        "not declare. Fix the usage comment or the "
                        "target." % (gname, name))
        for m in LITERAL_RE[name].finditer(seg):
            f = m.group(1)
            if f in group_of:
                errors.append(
                    "%s is a literal in %s and in the group %s that target "
                    "already takes. Remove the literal." %
                    (f, name, group_of[f]))
    for gname, (usage, _) in sorted(groups.items()):
        used = {name for name, i, j in segments
                if "${%s}" % gname in cmake[i:j]}
        for s in sorted(usage - used):
            errors.append(
                "%s is declared for %s but no target there uses it. Edit "
                "the usage comment or add the group to the target." %
                (gname, s))

    # Literals collected per surface for the "listed somewhere" walk.
    built = {f for _, i, j in segments
             for f in LITERAL_RE[_].findall(cmake[i:j])}
    built.update(f for _, (_, fs) in groups.items() for f in fs)

    # Every source in src/ root, src/mermaid and src/office is built or
    # ledgered as unbuilt. src/macos belongs to check_mac_sources.py.
    unbuilt_real = []
    for sub in ("", "mermaid", "office"):
        d = os.path.join(root, "src", sub)
        if not os.path.isdir(d):
            errors.append("src/%s is missing from the repository" %
                          (sub and sub + "/"))
            continue
        for n in sorted(os.listdir(d)):
            if not n.endswith((".cpp", ".mm", ".c")):
                continue
            rel = "src/%s%s" % (sub and sub + "/", n)
            if rel not in built:
                unbuilt_real.append(rel)

    for rel in unbuilt_real:
        if rel in unbuilt_allowed:
            continue
        errors.append(
            "%s is in no manifest group and no target list. Add it to "
            "cmake/sources.cmake with a usage comment, or list it under "
            "Sources built by no target in "
            "references/platform-parity.md with the reason." % rel)
    for rel in sorted(set(unbuilt_allowed) & set(built)):
        errors.append("%s is listed as unbuilt in the ledger but it is "
                      "built. Remove the stale row." % rel)

    n_files = sum(len(f) for _, f in groups.values())
    print("manifest: %d groups, %d files; unbuilt by design: %d" %
          (len(groups), n_files, len(set(unbuilt_allowed) & set(unbuilt_real))))
    if errors:
        for e in errors:
            print("FAIL: " + e)
        return 1
    print("every source file is in the manifest or a target list, and "
          "every group stays on its declared surfaces")
    return 0


if __name__ == "__main__":
    sys.exit(main())
