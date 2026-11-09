#!/usr/bin/env python3
"""Tests for check_parity_ledger.py, each on a deliberately bad tree."""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_parity_ledger as cpl  # noqa: E402

REPO = os.path.dirname(HERE)

MANIFEST = """# core
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_CORE_SOURCES
    src/parser.cpp
)

# state, one platform only
# usage: windows-app
set(MARKDOWNIT_STATE_SOURCES
    src/mermaid/state_parse.cpp
)
"""

LEDGER = """# Platform parity ledger

## Standing differences

| Source or feature | Windows | macOS | Reason |
|---|---|---|---|
| State diagrams `src/mermaid/state_parse.cpp` | exe | tests only | one platform |
| More prose for the row. |

## Commands: ribbon to mac menu

| Ribbon command | mac selector |
|---|---|

## Commands without a mac counterpart

| Ribbon command | Reason |
|---|---|

## Mac commands without a ribbon counterpart

| mac selector | Reason |
|---|---|

## Sources built by no target

- `src/textlayout_generic.cpp`: the third engine, owned by no target.
"""


def make_tree(manifest=MANIFEST, ledger=LEDGER):
    from pathlib import Path
    root = tempfile.mkdtemp()
    paths = [Path(root) / x for x in (
        "cmake/sources.cmake", "CMakeLists.txt",
        "references/platform-parity.md",
        "src/parser.cpp", "src/mermaid/state_parse.cpp",
        "src/textlayout_generic.cpp")]
    for path in paths:
        path.parent.mkdir(parents=True, exist_ok=True)
    paths[0].write_text(manifest)
    paths[1].write_text(
        "# the source pools live in cmake/sources.cmake in this tree\n")
    paths[2].write_text(ledger)
    for path in paths[3:]:
        path.touch()
    (Path(root) / "src" / "office").mkdir(exist_ok=True)
    return root


class ParityLedger(unittest.TestCase):
    def _run_on(self, root):
        hold = sys.argv
        sys.argv = ["x", root]
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = cpl.main()
        finally:
            sys.argv = hold
        return rc, buf.getvalue()

    def tearDown(self):
        shutil.rmtree(getattr(self, "root", "/nonexistent"),
                      ignore_errors=True)

    def test_repo_is_clean(self):
        rc, out = self._run_on(REPO)
        self.assertEqual(rc, 0, out)

    def test_explained_skew_passes(self):
        self.root = make_tree()
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 0, out)

    def test_unexplained_skew_fails(self):
        # The state group is on windows-app only, and the ledger row
        # names no source from the group.
        self.root = make_tree()
        path = os.path.join(self.root, "references", "platform-parity.md")
        with open(path, encoding="utf-8") as f:
            ledger = f.read().replace(
                "| State diagrams `src/mermaid/state_parse.cpp` | exe | tests "
                "only | one platform |",
                "| Plain prose about prose | exe | tests only | nothing named |")
        with open(path, "w", encoding="utf-8") as f:
            f.write(ledger)
        import gc; gc.collect()
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("MARKDOWNIT_STATE_SOURCES is built on windows-app",
                      out)

    def test_stale_file_in_row_fails(self):
        self.root = make_tree()
        path = os.path.join(self.root, "references", "platform-parity.md")
        with open(path, encoding="utf-8") as f:
            ledger = f.read().replace("state_parse.cpp", "state_old.cpp")
        with open(path, "w", encoding="utf-8") as f:
            f.write(ledger)
        import gc; gc.collect()
        # nothing names state_parse.cpp now, and state_old.cpp is gone
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertTrue(
            "does not exist on disk" in out or
            "MARKDOWNIT_STATE_SOURCES is built on windows-app" in out, out)

    def test_unbuilt_without_row_fails(self):
        self.root = make_tree()
        path = os.path.join(self.root, "references", "platform-parity.md")
        with open(path, encoding="utf-8") as f:
            ledger = f.read().replace(
                "- `src/textlayout_generic.cpp`: the third engine, owned by"
                " no target.\n", "")
        with open(path, "w", encoding="utf-8") as f:
            f.write(ledger)
        import gc; gc.collect()
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/textlayout_generic.cpp is built by no target "
                      "and is not in the unbuilt section of the ledger",
                      out)


if __name__ == "__main__":
    unittest.main()
