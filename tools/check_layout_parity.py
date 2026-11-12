#!/usr/bin/env python3
"""Compare two layout dumps for exact equality.

Both check uses from the port alignment plan's "Layout dump parity"
gate call this with the same two arguments, in either order:

- a platform's own dump against the committed golden in
  tests/layout-golden/, so a failure names the platform that moved
- the Windows dump directly against the macOS dump, the backstop if a
  golden was updated wrongly

The dump (tests/layout_parity_dump.cpp) measures every fixture with
the fixed-width fake text engine, so the same arithmetic runs on both
runners and an exact match is the expected result. Any difference is
either a real layout regression or a platform branch in code that is
supposed to be shared.
"""

import argparse
import json
import sys
from pathlib import Path

SCHEMA_FIELDS = {"schema", "engine", "style", "contentWidth", "cases"}
CASE_FIELDS = {"file", "listWidth", "listHeight", "blocks", "items"}


def validate(document, label):
    if type(document) is not dict or set(document) != SCHEMA_FIELDS:
        raise ValueError(f"{label}: unrecognized layout dump fields")
    if document["schema"] != 1:
        raise ValueError(f"{label}: unrecognized layout dump schema")
    if document["engine"] != "fake-8px":
        raise ValueError(f"{label}: layout dumps must use the fake "
                          "8px text engine")
    cases = document["cases"]
    if type(cases) is not list or not cases:
        raise ValueError(f"{label}: layout dump has no cases")
    indexed = {}
    for case in cases:
        if type(case) is not dict or set(case) != CASE_FIELDS:
            raise ValueError(f"{label}: unrecognized layout case fields")
        name = case["file"]
        if name in indexed:
            raise ValueError(f"{label}: duplicate fixture {name}")
        indexed[name] = case
    return indexed


def diff_value(reference, actual, location, errors, limit=20):
    if len(errors) >= limit:
        return
    if type(reference) is not type(actual):
        errors.append(f"{location}: type differs "
                       f"({type(reference).__name__} vs "
                       f"{type(actual).__name__})")
        return
    if type(reference) is dict:
        keys = set(reference) | set(actual)
        for key in sorted(keys):
            if key not in reference:
                errors.append(f"{location}.{key}: extra field")
            elif key not in actual:
                errors.append(f"{location}.{key}: missing field")
            else:
                diff_value(reference[key], actual[key],
                           f"{location}.{key}", errors, limit)
    elif type(reference) is list:
        if len(reference) != len(actual):
            errors.append(f"{location}: length differs "
                           f"({len(reference)} vs {len(actual)})")
            return
        for index, (expected, observed) in enumerate(
                zip(reference, actual)):
            diff_value(expected, observed, f"{location}[{index}]",
                       errors, limit)
    elif reference != actual:
        errors.append(f"{location}: {reference!r} vs {actual!r}")


def compare(reference, actual, ref_label, act_label):
    ref_cases = validate(reference, ref_label)
    act_cases = validate(actual, act_label)
    if set(ref_cases) != set(act_cases):
        missing = set(ref_cases) - set(act_cases)
        extra = set(act_cases) - set(ref_cases)
        raise ValueError(
            f"Fixture set differs: missing={sorted(missing)}, "
            f"extra={sorted(extra)}")
    errors = []
    blocks = 0
    items = 0
    for name in sorted(ref_cases):
        diff_value(ref_cases[name], act_cases[name], name, errors)
        blocks += len(ref_cases[name]["blocks"])
        items += len(ref_cases[name]["items"])
    if errors:
        raise ValueError(
            f"{len(errors)} difference(s) between {ref_label} and "
            f"{act_label}:\n  " + "\n  ".join(errors[:20]))
    return len(ref_cases), blocks, items


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path,
                         help="the golden, or the macOS dump")
    parser.add_argument("actual", type=Path,
                         help="the platform's own dump, or the "
                              "Windows dump")
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding="utf-8"))
        actual = json.loads(args.actual.read_text(encoding="utf-8"))
        cases, blocks, items = compare(
            reference, actual, str(args.reference), str(args.actual))
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"Layout dump parity failed: {error}\n")
    print(f"Layout dump parity passed: {cases} fixtures, {blocks} "
          f"blocks, {items} display items match between "
          f"{args.reference} and {args.actual}")


if __name__ == "__main__":
    main()
