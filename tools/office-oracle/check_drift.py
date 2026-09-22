#!/usr/bin/env python3
"""CI drift gate: are the committed fixtures and goldens still what we generate?

make_fixtures.py is deterministic, so regenerating into a scratch directory
must reproduce the committed files. This script compares the two trees file
by file and prints GitHub annotations for anything that drifted.

Comparison rules

  .json, .md   text files, compared byte for byte after newline normalization
  .docx        compared by sha256 first; if the compressed bytes differ but
               every decompressed package member is identical, the file is
               reported as a warning, because that is a toolchain artifact
               (zlib version) rather than a content change
  .pdf         compared by sha256 first; if the bytes differ but page count,
               page sizes and page text are identical, same warning rule

Anything else, a missing file, or a real content difference is an error and
exit status 1.

Usage:
    python3 check_drift.py <committed_dir> <regenerated_dir>
"""
import difflib
import hashlib
import json
import os
import sys
import zipfile

import dump_pdf

MAX_DIFF_LINES = 40


def relative_files(root):
    found = {}
    for directory, _subdirs, files in os.walk(root):
        for name in files:
            path = os.path.join(directory, name)
            found[os.path.relpath(path, root)] = path
    return found


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def text_bytes(path):
    with open(path, "rb") as handle:
        return handle.read().replace(b"\r\n", b"\n")


def zip_members(path):
    with zipfile.ZipFile(path) as archive:
        return {info.filename: archive.read(info.filename) for info in archive.infolist()}


def report_diff(name, committed, regenerated):
    committed_lines = committed.decode("utf-8", "replace").splitlines()
    regenerated_lines = regenerated.decode("utf-8", "replace").splitlines()
    diff = list(
        difflib.unified_diff(
            committed_lines,
            regenerated_lines,
            fromfile="committed/%s" % name,
            tofile="regenerated/%s" % name,
            lineterm="",
        )
    )
    for line in diff[:MAX_DIFF_LINES]:
        print("    %s" % line)
    if len(diff) > MAX_DIFF_LINES:
        print("    ... %d more diff lines" % (len(diff) - MAX_DIFF_LINES))


def compare(name, committed_path, regenerated_path):
    """Return (status, message) with status in {ok, warn, error}."""
    digest_committed = sha256(committed_path)
    digest_regenerated = sha256(regenerated_path)
    if digest_committed == digest_regenerated:
        return "ok", "sha256 %s" % digest_committed

    if name.endswith(".json") or name.endswith(".md"):
        payload_committed = text_bytes(committed_path)
        payload_regenerated = text_bytes(regenerated_path)
        if payload_committed == payload_regenerated:
            return "ok", "content identical to committed copy (line endings differ)"
        return "error", "content drifted from the committed copy"

    if name.endswith(".docx"):
        members_committed = zip_members(committed_path)
        members_regenerated = zip_members(regenerated_path)
        if members_committed == members_regenerated:
            return (
                "warn",
                "package members identical, zip bytes differ (zlib version); commit the "
                "regenerated file or pin the toolchain",
            )
        differing = sorted(
            set(members_committed) ^ set(members_regenerated)
            | {
                member
                for member in set(members_committed) & set(members_regenerated)
                if members_committed[member] != members_regenerated[member]
            }
        )
        return "error", "package members drifted: %s" % ", ".join(differing[:10])

    if name.endswith(".pdf"):
        dump_committed = dump_pdf.dump_pdf(committed_path)
        dump_regenerated = dump_pdf.dump_pdf(regenerated_path)
        if dump_committed == dump_regenerated:
            return (
                "warn",
                "pages identical, pdf bytes differ (zlib version); commit the regenerated "
                "file or pin the toolchain",
            )
        lines_committed = json.dumps(dump_committed, indent=2, ensure_ascii=False).splitlines()
        lines_regenerated = json.dumps(dump_regenerated, indent=2, ensure_ascii=False).splitlines()
        print("    --- page dump diff ---")
        report_diff(name, "\n".join(lines_committed).encode("utf-8"), "\n".join(lines_regenerated).encode("utf-8"))
        return "error", "page dump drifted from the committed copy"

    return "error", "file bytes differ and the format has no content comparison"


def main(argv=None):
    if len(argv or sys.argv[1:]) != 2:
        print("usage: check_drift.py <committed_dir> <regenerated_dir>")
        return 2
    committed_root, regenerated_root = (argv or sys.argv[1:])[:2]

    committed = relative_files(committed_root)
    regenerated = relative_files(regenerated_root)

    errors = 0
    warnings = 0
    for name in sorted(set(committed) | set(regenerated)):
        in_committed = committed.get(name)
        in_regenerated = regenerated.get(name)
        if in_committed is None or in_regenerated is None:
            errors += 1
            where = "committed" if in_committed is None else "regenerated"
            print("::error::Office oracle drift: %s is missing from the %s tree" % (name, where))
            continue

        status, message = compare(name, in_committed, in_regenerated)
        if status == "ok":
            print("ok    %s (%s)" % (name, message))
        elif status == "warn":
            warnings += 1
            print("::warning::Office oracle: %s %s" % (name, message))
        else:
            errors += 1
            print("::error::Office oracle drift: %s %s" % (name, message))

    if errors:
        print(
            "FAILED: %d file(s) drifted from the committed copies; regenerate with "
            "make_fixtures.py and commit the result" % errors
        )
        return 1
    print("PASS: committed fixtures and goldens match the regenerated set (%d warnings)" % warnings)
    return 0


if __name__ == "__main__":
    sys.exit(main())
