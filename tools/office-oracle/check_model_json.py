#!/usr/bin/env python3
"""Compare a produced content model against the golden Word model.

The MarkDownIt importer writes the model it read out of a .docx as JSON in the
same schema that dump_docx.py uses. This script compares such a file against
tests/office/golden/reference_docx.json, using the same normalization and the
same flattened fact list as check_docx.py, so an import check and an export
check read the same way.

Usage:
    python3 check_model_json.py <produced_model.json> [--golden PATH]
"""
import argparse
import json
import os
import sys

import check_docx

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_GOLDEN = os.path.join(REPO_ROOT, "tests", "office", "golden", "reference_docx.json")


def check(produced_path, golden_path, max_differences):
    with open(golden_path, encoding="utf-8") as handle:
        golden = json.load(handle)
    try:
        with open(produced_path, encoding="utf-8") as handle:
            produced = json.load(handle)
    except (OSError, ValueError) as exc:
        print("MISMATCH: %s could not be read as JSON: %s" % (produced_path, exc))
        return 1

    golden_lines = check_docx.flatten(golden)
    produced_lines = check_docx.flatten(produced)
    problems = check_docx.geometry_problems(golden, produced)

    if golden_lines == produced_lines and not problems:
        print(
            "OK: %s matches %s (%d blocks, %d facts)"
            % (
                produced_path,
                os.path.relpath(golden_path, REPO_ROOT),
                len(produced.get("blocks", [])),
                len(golden_lines),
            )
        )
        return 0

    print("MISMATCH: %s does not match %s" % (produced_path, golden_path))
    if len(golden_lines) != len(produced_lines):
        print("fact count: golden %d, produced %d" % (len(golden_lines), len(produced_lines)))
    kept, changes, truncated = check_docx.limited_diff(
        golden_lines, produced_lines, max_differences
    )
    for line in kept:
        print(line)
    if truncated:
        print("... further differences truncated after %d changes" % changes)
    for problem in problems:
        print("--- %s" % problem)
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("model", help="produced content model JSON")
    parser.add_argument("--golden", default=DEFAULT_GOLDEN)
    parser.add_argument("--max-differences", type=int, default=20)
    args = parser.parse_args(argv)
    return check(args.model, args.golden, args.max_differences)


if __name__ == "__main__":
    sys.exit(main())
