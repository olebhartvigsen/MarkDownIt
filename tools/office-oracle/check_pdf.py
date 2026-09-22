#!/usr/bin/env python3
"""Validate a produced .pdf against the golden PDF dump.

The golden is tests/office/golden/reference_pdf.json, produced by
make_fixtures.py through pypdf. A produced file (from MarkDownIt) must match:

  * page count, exactly
  * every page width and height within 1 point of A4
  * every page's non-whitespace character sequence, exactly

Whitespace is ignored on purpose: PDF text extraction depends on the reader,
line breaking and kerning, so only the visible characters are compared, in
order, page by page.

Exit status 0 when the file matches, 1 otherwise, with a unified diff of the
page text on mismatch.

Usage:
    python3 check_pdf.py <produced.pdf> [--golden PATH] [--max-differences N]
"""
import argparse
import difflib
import json
import os
import sys

import dump_pdf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_GOLDEN = os.path.join(REPO_ROOT, "tests", "office", "golden", "reference_pdf.json")
PAGE_SIZE_TOLERANCE_PT = 1.0


def normalize_text(text):
    """Non-whitespace character sequence of a page."""
    return "".join((text or "").split())


def words(text):
    return (text or "").split()


def page_problems(index, golden_page, produced_page):
    problems = []
    for key in ("width", "height"):
        golden_value = golden_page["media_box_pt"][key]
        produced_value = produced_page["media_box_pt"][key]
        if abs(golden_value - produced_value) > PAGE_SIZE_TOLERANCE_PT:
            problems.append(
                "page %d %s: golden %.2f pt != produced %.2f pt"
                % (index + 1, key, golden_value, produced_value)
            )
    return problems


def check(produced_path, golden_path, max_differences):
    with open(golden_path, encoding="utf-8") as handle:
        golden = json.load(handle)
    produced = dump_pdf.dump_pdf(produced_path)

    golden_pages = golden.get("pages", [])
    produced_pages = produced.get("pages", [])

    problems = []
    if golden.get("page_count") != produced.get("page_count"):
        problems.append(
            "page count: golden %s != produced %s"
            % (golden.get("page_count"), produced.get("page_count"))
        )

    text_diffs = []
    for index, golden_page in enumerate(golden_pages):
        if index >= len(produced_pages):
            break
        produced_page = produced_pages[index]
        problems.extend(page_problems(index, golden_page, produced_page))
        golden_text = normalize_text(golden_page.get("text", ""))
        produced_text = normalize_text(produced_page.get("text", ""))
        if golden_text != produced_text:
            text_diffs.append(
                (
                    index,
                    list(
                        difflib.unified_diff(
                            words(golden_page.get("text", "")),
                            words(produced_page.get("text", "")),
                            fromfile="golden page %d" % (index + 1),
                            tofile="produced page %d" % (index + 1),
                            lineterm="",
                            n=1,
                        )
                    ),
                )
            )

    if not problems and not text_diffs:
        print(
            "OK: %s matches %s (%d pages, A4, per-page text identical)"
            % (produced_path, os.path.relpath(golden_path, REPO_ROOT), produced.get("page_count", 0))
        )
        return 0

    print("MISMATCH: %s does not match %s" % (produced_path, golden_path))
    printed = 0
    for index, diff in text_diffs:
        printed += 1
        if printed > max_differences:
            print("... further page diffs truncated after %d pages" % max_differences)
            break
        print("--- page %d text diff (golden -> produced) ---" % (index + 1))
        for line in diff:
            print(line)
    for problem in problems:
        print("--- %s" % problem)
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pdf", help="produced .pdf file")
    parser.add_argument("--golden", default=DEFAULT_GOLDEN)
    parser.add_argument("--max-differences", type=int, default=5)
    args = parser.parse_args(argv)
    return check(args.pdf, args.golden, args.max_differences)


if __name__ == "__main__":
    sys.exit(main())
