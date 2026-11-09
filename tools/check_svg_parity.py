#!/usr/bin/env python3
"""Compare Windows SVG drawing commands against the macOS reference.

These traces use injected text metrics. They verify scene geometry, styles,
text and draw order, not glyph rasterization or native font availability.
"""

import argparse
import json
import math
from pathlib import Path

FILES = ("ai-components.svg", "diagram.svg", "svg-parity.svg", "two-boxes.svg", "wide-diagram.svg", "mermaid-diagram-1788899219877.svg")
ZOOMS = (0.4, 1, 2)
TOLERANCE = 0.001

# Field layouts emitted by Commands in tests/svg_parity_dump.cpp.
# n = finite number, s = string, f = integer flag, p = point array.
RECT_FIELDS = "n" * 10
OP_FIELDS = {
    name: RECT_FIELDS for name in (
        "fillRect", "fillRoundedRect", "strokeRect", "strokeRoundedRect",
        "fillEllipse", "strokeEllipse", "pushClip",
    )
}
OP_FIELDS.update({
    "line": "n" * 9,
    "text": "ssnfff" + "n" * 6,
    "fillPolygon": "p" + "n" * 5 + "f",
    "strokePolygon": "p" + "n" * 5 + "f",
    "pieSlice": "n" * 10,
    "popClip": "",
})


def finite_number(value):
    if type(value) not in (int, float):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def flag(value):
    return type(value) is int and value in (0, 1)


def validate_operation(operation, location):
    if (type(operation) is not list or not operation
            or type(operation[0]) is not str or operation[0] not in OP_FIELDS):
        raise ValueError(f"{location}: unknown or malformed drawing command")
    name = operation[0]
    fields = OP_FIELDS[name]
    if len(operation) != len(fields) + 1:
        raise ValueError(f"{location}: wrong arity for {name}")
    for index, (kind, value) in enumerate(zip(fields, operation[1:]), 1):
        valid = False
        if kind == "n":
            valid = finite_number(value)
        elif kind == "s":
            valid = type(value) is str
        elif kind == "f":
            valid = flag(value)
        elif kind == "p":
            # Filled polygons need an area; open strokes need two points.
            minimum = 3 if name == "fillPolygon" or operation[-1] == 1 else 2
            valid = (type(value) is list and len(value) >= minimum
                     and all(type(point) is list and len(point) == 2
                             and all(finite_number(axis) for axis in point)
                             for point in value))
        if not valid:
            raise ValueError(f"{location}[{index}]: invalid {kind} field for {name}")
    if name == "fillPolygon" and operation[-1] != 1:
        raise ValueError(f"{location}: filled polygon must be closed")


def validate(document):
    if (type(document) is not dict
            or set(document) != {"schema", "reference", "metrics", "cases"}):
        raise ValueError("Unrecognized SVG trace fields")
    if (type(document["schema"]) is not int or document["schema"] != 1
            or type(document["reference"]) is not str or document["reference"] != "macOS"):
        raise ValueError("Unrecognized SVG trace schema or reference")
    if type(document["metrics"]) is not str or document["metrics"] != "fixed-half-em":
        raise ValueError("SVG traces must use the same injected text metrics")
    cases = document["cases"]
    if type(cases) is not list:
        raise ValueError("SVG trace has no case list")
    expected = {(name, zoom, translated) for name in FILES for zoom in ZOOMS for translated in (0, 1)}
    indexed = {}
    for case in cases:
        if type(case) is not dict or set(case) != {"file", "zoom", "translated", "operations"}:
            raise ValueError("Unrecognized SVG case fields")
        if (type(case["file"]) is not str or case["file"] not in FILES
                or not finite_number(case["zoom"]) or case["zoom"] not in ZOOMS
                or not flag(case["translated"])):
            raise ValueError("Invalid SVG case identity")
        key = (case["file"], case["zoom"], case["translated"])
        if key in indexed:
            raise ValueError(f"Duplicate SVG case: {key}")
        operations = case["operations"]
        if type(operations) is not list or not operations:
            raise ValueError(f"Empty SVG drawing: {key}")
        for index, operation in enumerate(operations):
            validate_operation(operation, f"{key} command {index}")
        indexed[key] = case
    if set(indexed) != expected:
        raise ValueError(f"SVG case inventory mismatch: missing={expected - set(indexed)}, extra={set(indexed) - expected}")
    return indexed


def compare_value(reference, actual, location):
    if type(reference) in (int, float):
        if not finite_number(reference) or not finite_number(actual):
            raise ValueError(f"{location}: invalid numeric value")
        if not math.isclose(reference, actual, rel_tol=0, abs_tol=TOLERANCE):
            raise ValueError(f"{location}: macOS={reference}, Windows={actual}")
    elif type(reference) is list:
        if type(actual) is not list or len(reference) != len(actual):
            raise ValueError(f"{location}: command or point count differs")
        for index, (expected, observed) in enumerate(zip(reference, actual)):
            compare_value(expected, observed, f"{location}[{index}]")
    elif type(reference) is not type(actual) or reference != actual:
        raise ValueError(f"{location}: macOS={reference!r}, Windows={actual!r}")


def compare(reference, windows):
    mac_cases = validate(reference)
    win_cases = validate(windows)
    count = 0
    for key in sorted(mac_cases):
        expected = mac_cases[key]["operations"]
        actual = win_cases[key]["operations"]
        if len(expected) != len(actual):
            raise ValueError(f"{key}: command count differs")
        for index, (mac_op, win_op) in enumerate(zip(expected, actual)):
            location = f"{key} command {index}"
            if mac_op[0] != win_op[0]:
                raise ValueError(f"{location}: operation differs")
            for field, kind in enumerate(OP_FIELDS[mac_op[0]], 1):
                if kind == "f":
                    if mac_op[field] != win_op[field]:
                        raise ValueError(f"{location}[{field}]: discrete flag differs")
                else:
                    compare_value(mac_op[field], win_op[field], f"{location}[{field}]")
        count += len(expected)
    return len(mac_cases), count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("macos", type=Path)
    parser.add_argument("windows", type=Path)
    args = parser.parse_args()
    try:
        cases, commands = compare(
            json.loads(args.macos.read_text(encoding="utf-8")),
            json.loads(args.windows.read_text(encoding="utf-8")),
        )
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"SVG parity failed: {error}\n")
    print(f"SVG parity passed: {cases} cases, {commands} commands match macOS (tolerance {TOLERANCE})")


if __name__ == "__main__":
    main()
