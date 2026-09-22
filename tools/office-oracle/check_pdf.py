#!/usr/bin/env python3
"""Validate a produced .pdf against the golden PDF dump.

The golden is tests/office/golden/reference_pdf.json, produced by
make_fixtures.py through pypdf. A produced file (from MarkDownIt) must match:

  * page count, exactly
  * every page width and height within 1 point of A4
  * every page's content character sequence, exactly

Layout is ignored on purpose: PDF text extraction depends on the reader, line
breaking and kerning, so only the visible content characters are compared, in
order, page by page. Two things are normalized away on both sides before the
comparison, because they belong to the writer rather than to the document:

  * control characters (0x00-0x1F and 0x7F), which readers emit for glyphs
    they cannot map to Unicode. reportlab's bullet glyph extracts as 0x7F.
  * list markers at the start of a line: bullets, the "o" that reportlab uses
    for a nested list, and "1." style numbering. The marker glyph is the
    writer's choice, the item text is not.

Everything else, including the "Side N" footer, the Danish characters and the
table text, must match exactly and in order. Pass --strict to compare the raw
extraction instead, which is only useful when debugging a diff.

Exit status 0 when the file matches, 1 otherwise, with a unified diff of the
page text on mismatch.

Usage:
    python3 check_pdf.py <produced.pdf> [--golden PATH] [--max-differences N]
"""
import argparse
import difflib
import json
import os
import re
import sys

import dump_pdf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_GOLDEN = os.path.join(REPO_ROOT, "tests", "office", "golden", "reference_pdf.json")
PAGE_SIZE_TOLERANCE_PT = 1.0


CONTROL_RE = re.compile(r"[\x00-\x1f\x7f]")

# A marker is a bullet glyph, the nested-list "o" reportlab emits, or an
# "N." / "N)" number, followed by at least one space and then item text.
LINE_MARKER_RE = re.compile(
    r"^[ \t]*(?:[\u2022\u25cf\u25e6\u25aa\u00b7\u2023\u2043*]|[-o]|\d{1,3}[.)])[ \t]+(?=\S)"
)


def content_chars(text, strict=False):
    """The content characters of a page: no whitespace, no markers."""
    body = text or ""
    if strict:
        return "".join(body.split())
    lines = []
    for raw_line in CONTROL_RE.sub("", body).splitlines():
        line = raw_line.strip()
        if not line:
            continue
        stripped = LINE_MARKER_RE.sub("", line, count=1).strip()
        lines.append(stripped if stripped else line)
    return "".join("".join(lines).split())


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


def check(produced_path, golden_path, max_differences, strict=False):
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
        golden_text = content_chars(golden_page.get("text", ""), strict)
        produced_text = content_chars(produced_page.get("text", ""), strict)
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
    parser.add_argument(
        "--strict",
        action="store_true",
        help="compare the raw extraction, markers and control characters included",
    )
    args = parser.parse_args(argv)
    return check(args.pdf, args.golden, args.max_differences, args.strict)


if __name__ == "__main__":
    sys.exit(main())
