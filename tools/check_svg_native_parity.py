#!/usr/bin/env python3
"""Compare private Core Graphics and WARP paint probes, using Mac as reference."""

import argparse
import json
from pathlib import Path

PROBES = (
    "rounded fill", "rounded stroke", "rounded corner", "ellipse fill",
    "ellipse stroke", "ellipse corner", "line stroke", "line exterior",
    "rect stroke", "rect interior", "polygon fill", "polygon stroke",
    "polygon exterior", "open path vertical", "open path horizontal",
    "open path gap", "closed path diagonal", "nonzero winding fill",
)
TEXT = ("plain", "newline", "anchored-newline")


def validate(data):
    if not isinstance(data, dict) or set(data) != {"schema", "probes", "text"}:
        raise ValueError("Invalid native probe fields")
    if type(data["schema"]) is not int or data["schema"] != 1:
        raise ValueError("Invalid native probe schema")
    if not isinstance(data["probes"], list) or not isinstance(data["text"], list):
        raise ValueError("Missing native probe lists")
    if len(data["probes"]) != len(PROBES) or len(data["text"]) != len(TEXT):
        raise ValueError("Incomplete native probe inventory")
    for probe, name in zip(data["probes"], PROBES):
        if not isinstance(probe, dict) or set(probe) != {"name", "rgb"} or probe["name"] != name:
            raise ValueError("Native shape probe inventory differs")
        rgb = probe["rgb"]
        if not isinstance(rgb, list) or len(rgb) != 3 or any(type(c) is not int or not 0 <= c <= 255 for c in rgb):
            raise ValueError(f"Invalid native color: {name}")
    for item, name in zip(data["text"], TEXT):
        if not isinstance(item, dict) or set(item) != {"id", "bounds"} or item["id"] != name:
            raise ValueError("Native text probe inventory differs")
        bounds = item["bounds"]
        if not isinstance(bounds, list) or len(bounds) != 4 or any(type(c) is not int or c < 0 for c in bounds):
            raise ValueError(f"Invalid native text bounds: {name}")
        left, top, right, bottom = bounds
        if not (left < right <= 240 and top < bottom <= 140):
            raise ValueError(f"Empty or out-of-range native text: {name}")


def compare(macos, windows):
    validate(macos)
    validate(windows)
    for expected, actual in zip(macos["probes"], windows["probes"]):
        if any(abs(a - b) > 2 for a, b in zip(expected["rgb"], actual["rgb"])):
            raise ValueError(f"Native paint differs for {expected['name']}: macOS={expected['rgb']}, Windows={actual['rgb']}")
    for expected, actual in zip(macos["text"], windows["text"]):
        # Same Arial request, with a small allowance for Core Text/DirectWrite
        # ink hinting. A new baseline or a changed anchor exceeds this bound.
        if any(abs(a - b) > 3 for a, b in zip(expected["bounds"], actual["bounds"])):
            raise ValueError(f"Native text differs for {expected['id']}: macOS={expected['bounds']}, Windows={actual['bounds']}")
    return len(PROBES), len(TEXT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("macos", type=Path)
    parser.add_argument("windows", type=Path)
    args = parser.parse_args()
    try:
        counts = compare(json.loads(args.macos.read_text()), json.loads(args.windows.read_text()))
    except (ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Native SVG parity failed: {error}\n")
    print(f"Native SVG parity passed: {counts[0]} shape probes, {counts[1]} text bounds match macOS")


if __name__ == "__main__":
    main()
