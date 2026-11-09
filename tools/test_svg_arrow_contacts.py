#!/usr/bin/env python3
"""Exercise arrow contacts and their failure cases through the compiled renderer."""

import argparse
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROUTES = (
    ("right", "M30,20 L80,20", "M34,20 L80,20", "M30,20 L84,20"),
    ("left-dashed", "M210,20 L160,20", "M206,20 L160,20", "M210,20 L156,20"),
    ("down", "M20,80 L20,110", "M20,84 L20,110", "M20,80 L20,114"),
    ("up-dashed", "M80,110 L80,80", "M80,106 L80,80", "M80,110 L80,76"),
)


def check_contacts(executable: Path, source: Path) -> int:
    original = source.read_text(encoding="utf-8")
    failures = 0
    with tempfile.TemporaryDirectory(prefix="svg-arrow-contacts-") as folder:
        fixture = Path(folder) / "svg-arrow-contacts.svg"
        output = Path(folder) / "commands.json"

        def run(svg: str) -> subprocess.CompletedProcess:
            fixture.write_text(svg, encoding="utf-8")
            return subprocess.run(
                [str(executable), str(output), str(fixture)],
                text=True, capture_output=True, check=False,
            )

        good = run(original)
        if good.returncode:
            raise AssertionError(f"Valid arrow contacts failed: {good.stderr}")
        cases = json.loads(output.read_text(encoding="utf-8"))["cases"]
        if len(cases) != 6:
            raise AssertionError(f"Expected six zoom/placement cases, got {len(cases)}")

        def reject(svg: str, name: str, reason: str) -> None:
            nonlocal failures
            result = run(svg)
            message = f"SVG arrow border contact failed: {name}"
            if result.returncode == 0 or message not in result.stderr:
                raise AssertionError(
                    f"{name} {reason} was not rejected by its contact check: "
                    f"exit={result.returncode}, stderr={result.stderr!r}"
                )
            failures += 1

        for name, route, gap, overshoot in ROUTES:
            if original.count(route) != 1:
                raise AssertionError(f"Missing or ambiguous route: {route}")
            reject(original.replace(route, gap), name, "source gap")
            reject(original.replace(route, overshoot), name, "destination overshoot")
            # Shift only this marker. The shaft still reaches the border, so a
            # combined shaft/marker bound alone cannot detect an undershoot.
            for ref_x, reason in (("6", "marker overshoot"), ("14", "marker undershoot")):
                root = ET.fromstring(original)
                defs = root.find("defs")
                if defs is None:
                    raise AssertionError("Fixture has no marker definitions")
                original_marker = defs.find("marker")
                if original_marker is None:
                    raise AssertionError("Fixture has no arrow marker")
                marker = copy.deepcopy(original_marker)
                marker.set("id", "shifted")
                marker.set("refX", ref_x)
                defs.append(marker)
                path = next(p for p in root.findall("path") if p.get("d") == route)
                path.set("marker-end", "url(#shifted)")
                reject(ET.tostring(root, encoding="unicode"), name, reason)
    return failures


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    count = check_contacts(args.exe.resolve(), root / "tests/fixtures/svg-arrow-contacts.svg")
    print(f"SVG arrow contacts passed: 6 zoom/placement cases, {count} bad geometries rejected")


if __name__ == "__main__":
    main()
