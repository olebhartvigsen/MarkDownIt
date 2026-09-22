#!/usr/bin/env python3
"""Validate a produced .docx against the golden Word content model.

The golden is tests/office/golden/reference_docx.json, produced by
make_fixtures.py from python-docx. A produced file (from MarkDownIt) must
carry the same content model:

  * same blocks in the same order, same kinds, same heading levels
  * same list kind (bullet/ordered) and nesting level
  * same table shape and same cell text
  * same runs per block, with bold, italic, mono, underline and link
    compared exactly
  * same page setup (width, height, margins) within 0.01 pt

Text values are normalized by collapsing runs of whitespace, so run splits
and stray spaces do not matter.

Exit status 0 when the file matches, 1 otherwise. On mismatch the flattened
model is printed as a unified diff, limited to the first N changes.

Usage:
    python3 check_docx.py <produced.docx> [--golden PATH] [--max-differences N]
"""
import argparse
import difflib
import json
import os
import re
import sys

import dump_docx

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_GOLDEN = os.path.join(REPO_ROOT, "tests", "office", "golden", "reference_docx.json")
GEOMETRY_TOLERANCE_PT = 0.01

WHITESPACE_RE = re.compile(r"\s+")


def normalize_text(value):
    return WHITESPACE_RE.sub(" ", value or "").strip()


def flatten(model):
    """Flatten a content model into comparable one-line facts."""
    lines = []
    for index, block in enumerate(model.get("blocks", [])):
        tag = "[%03d]" % index
        kind = block.get("kind")
        if kind == "table":
            rows = block.get("rows", [])
            lines.append("%s table rows=%d" % (tag, len(rows)))
            for row_index, row in enumerate(rows):
                cells = " | ".join(normalize_text(cell.get("text", "")) for cell in row)
                lines.append("%s table row %d: %s" % (tag, row_index, cells))
            continue

        details = []
        if kind == "heading":
            details.append("level=%s" % block.get("level"))
        if kind == "list_item":
            info = block.get("list") or {}
            details.append("list=%s/%s" % (info.get("kind"), info.get("level")))
        suffix = (" " + " ".join(details)) if details else ""
        lines.append(
            "%s %s%s text=%s" % (tag, kind, suffix, normalize_text(block.get("text", "")))
        )
        for run_index, run in enumerate(block.get("runs", [])):
            lines.append(
                "%s run %d text=%s bold=%s italic=%s mono=%s underline=%s link=%s"
                % (
                    tag,
                    run_index,
                    normalize_text(run.get("text", "")),
                    run.get("bold"),
                    run.get("italic"),
                    run.get("mono"),
                    run.get("underline"),
                    run.get("link"),
                )
            )
    return lines


def geometry_problems(golden, produced):
    """Compare section page setup numerically, returning readable problems."""
    problems = []
    golden_sections = golden.get("sections", [])
    produced_sections = produced.get("sections", [])
    if len(golden_sections) != len(produced_sections):
        problems.append(
            "section count: golden %d != produced %d"
            % (len(golden_sections), len(produced_sections))
        )
    for index, golden_section in enumerate(golden_sections[: len(produced_sections)]):
        produced_section = produced_sections[index]
        for key in ("page_width_pt", "page_height_pt"):
            golden_value = golden_section.get(key)
            produced_value = produced_section.get(key)
            if golden_value is None or produced_value is None:
                if golden_value != produced_value:
                    problems.append(
                        "section %d %s: golden %s != produced %s"
                        % (index, key, golden_value, produced_value)
                    )
                continue
            if abs(golden_value - produced_value) > GEOMETRY_TOLERANCE_PT:
                problems.append(
                    "section %d %s: golden %.2f != produced %.2f"
                    % (index, key, golden_value, produced_value)
                )
        golden_margins = golden_section.get("margins_pt") or {}
        produced_margins = produced_section.get("margins_pt") or {}
        for key in ("top", "right", "bottom", "left"):
            golden_value = golden_margins.get(key)
            produced_value = produced_margins.get(key)
            if golden_value is None or produced_value is None:
                if golden_value != produced_value:
                    problems.append(
                        "section %d margin %s: golden %s != produced %s"
                        % (index, key, golden_value, produced_value)
                    )
                continue
            if abs(golden_value - produced_value) > GEOMETRY_TOLERANCE_PT:
                problems.append(
                    "section %d margin %s: golden %.2f != produced %.2f"
                    % (index, key, golden_value, produced_value)
                )
    return problems


def limited_diff(golden_lines, produced_lines, max_differences):
    """Return a unified diff truncated after max_differences changed lines."""
    diff = difflib.unified_diff(
        golden_lines,
        produced_lines,
        fromfile="golden",
        tofile="produced",
        lineterm="",
        n=1,
    )
    kept = []
    changes = 0
    truncated = False
    for line in diff:
        is_change = (line.startswith("+") or line.startswith("-")) and not line.startswith(
            ("+++", "---")
        )
        if is_change:
            if changes >= max_differences:
                truncated = True
                break
            changes += 1
        kept.append(line)
    return kept, changes, truncated


def check(produced_path, golden_path, max_differences):
    with open(golden_path, encoding="utf-8") as handle:
        golden = json.load(handle)
    produced = dump_docx.dump_docx(produced_path)

    golden_lines = flatten(golden)
    produced_lines = flatten(produced)
    problems = geometry_problems(golden, produced)

    if golden_lines == produced_lines and not problems:
        print(
            "OK: %s matches %s (%d blocks, %d facts)"
            % (produced_path, os.path.relpath(golden_path, REPO_ROOT), len(produced.get("blocks", [])), len(golden_lines))
        )
        return 0

    print("MISMATCH: %s does not match %s" % (produced_path, golden_path))
    if len(golden_lines) != len(produced_lines):
        print(
            "fact count: golden %d, produced %d" % (len(golden_lines), len(produced_lines))
        )
    kept, changes, truncated = limited_diff(golden_lines, produced_lines, max_differences)
    if kept:
        print("--- content model diff (golden -> produced) ---")
        for line in kept:
            print(line)
        if truncated:
            print("... further differences truncated after %d changes" % changes)
    if problems:
        print("--- page setup problems ---")
        for problem in problems:
            print(problem)
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("docx", help="produced .docx file")
    parser.add_argument("--golden", default=DEFAULT_GOLDEN)
    parser.add_argument("--max-differences", type=int, default=20)
    args = parser.parse_args(argv)
    return check(args.docx, args.golden, args.max_differences)


if __name__ == "__main__":
    sys.exit(main())
