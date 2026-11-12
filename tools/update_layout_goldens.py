#!/usr/bin/env python3
"""Regenerate tests/layout-golden/layout-golden.json.

The golden for the "Layout dump parity" gate changes only through this
script, never by hand, the same rule AGENTS.md sets for the Office
goldens: the change shows in the diff for review.

Usage:
    python3 tools/update_layout_goldens.py --exe PATH/TO/MarkDownIt.layout-parity

The exe is the layout-parity dump target built by CMake
(tests/layout_parity_dump.cpp). It is run against every fixture in
tests/fixtures/parity/*.md, fixed-width fake text engine only, so the
output is the same whether the binary was built by MSVC or Clang.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--exe", type=Path,
                        help="the built MarkDownIt.layout-parity binary; "
                             "run it against every fixture")
    group.add_argument("--from-dump", type=Path,
                        help="a dump already produced by a real build "
                             "(for example a downloaded CI artifact from "
                             "the macOS or Windows runner), copied in "
                             "after a schema check instead of run here")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    golden = root / "tests/layout-golden/layout-golden.json"
    golden.parent.mkdir(parents=True, exist_ok=True)

    if args.from_dump is not None:
        document = json.loads(args.from_dump.read_text(encoding="utf-8"))
        if document.get("schema") != 1 or document.get("engine") != \
                "fake-8px" or not document.get("cases"):
            parser.exit(1, f"{args.from_dump}: not a valid layout "
                        "dump (schema/engine/cases)\n")
        golden.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"Wrote {golden} from {args.from_dump} "
              f"({len(document['cases'])} fixtures).")
        return

    fixtures = sorted((root / "tests/fixtures/parity").glob("*.md"))
    if not fixtures:
        parser.exit(1, "No fixtures under tests/fixtures/parity/*.md\n")

    result = subprocess.run(
        [str(args.exe), str(golden), *(str(f) for f in fixtures)],
        check=False, capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        parser.exit(result.returncode, "layout-parity dump failed\n")
    print(f"Wrote {golden} from {len(fixtures)} fixtures.")


if __name__ == "__main__":
    main()
