#!/usr/bin/env python3
"""Tests for check_test_inventory.py, each on a deliberately bad case."""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_test_inventory as cti  # noqa: E402

REPO = os.path.dirname(HERE)

LEDGER = """# Platform parity ledger

## Tests built on one platform

| Test | Platform | Reason |
|---|---|---|
| FindBarWindow.* | windows | a Win32 window test |
| TextEngineMac.* | macos | the Core Text engine |

## Sources built by no target

- `src/textlayout_generic.cpp`: the third engine.
"""

WIN = "Parser.Opens\nFindBarWindow.Navigates\nEditController.Undo\n"
MAC = "Parser.Opens\nTextEngineMac.Measures\nEditController.Undo\n"


def make_tree(ledger=LEDGER):
    root = tempfile.mkdtemp()
    os.makedirs(os.path.join(root, "references"))
    with open(os.path.join(root, "references", "platform-parity.md"),
              "w", encoding="utf-8") as fh:
        fh.write(ledger)
    return root


class TestInventory(unittest.TestCase):
    def _run(self, root, win=WIN, mac=MAC):
        wp = os.path.join(root, "win.txt")
        mp = os.path.join(root, "mac.txt")
        with open(wp, "w") as fh:
            fh.write(win)
        with open(mp, "w") as fh:
            fh.write(mac)
        hold = sys.argv
        sys.argv = ["x", "--windows", wp, "--macos", mp, "--root", root]
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = cti.main()
        finally:
            sys.argv = hold
        return rc, buf.getvalue()

    def tearDown(self):
        shutil.rmtree(getattr(self, "root", "/nonexistent"),
                      ignore_errors=True)

    def test_ledger_rows_wellformed(self):
        # The real ledger must parse, and every row must name a platform.
        self.root = make_tree(ledger=open(
            os.path.join(REPO, "references", "platform-parity.md"),
            encoding="utf-8").read())
        rows = cti.read_rows(self.root)
        self.assertTrue(rows)
        for pattern, platform in rows:
            self.assertRegex(pattern or "x",
                     r"^[A-Za-z0-9_][A-Za-z0-9_.]*(\.\*)?$")
            self.assertIn(platform, ("windows", "macos"))
        self.assertIn(("FindBarWindow.*", "windows"), rows)

    def test_documented_difference_passes(self):
        self.root = make_tree()
        rc, out = self._run(self.root)
        self.assertEqual(rc, 0, out)

    def test_undocumented_difference_fails(self):
        self.root = make_tree()
        rc, out = self._run(self.root,
                           win="Parser.Opens\nSurprise.OnlyHere\n",
                           mac="Parser.Opens\n")
        self.assertEqual(rc, 1)
        self.assertIn("Surprise.OnlyHere runs on Windows only", out)

    def test_undocumented_mac_difference_fails(self):
        self.root = make_tree()
        rc, out = self._run(self.root,
                            win="Parser.Opens\n",
                            mac="Parser.Opens\nSurprise.MacOnly\n")
        self.assertEqual(rc, 1)
        self.assertIn("Surprise.MacOnly runs on macOS only", out)

    def test_stale_row_fails(self):
        self.root = make_tree(ledger=LEDGER.replace(
            "| FindBarWindow.* | windows | a Win32 window test |",
            "| GoneSuite.* | windows | no such suite |"))
        rc, out = self._run(self.root, win="Parser.Opens\n",
                            mac="Parser.Opens\n")
        self.assertEqual(rc, 1)
        self.assertIn("GoneSuite.* (windows) matches no test", out)

    def test_wrong_platform_value_fails(self):
        self.root = make_tree(ledger=LEDGER.replace(
            "| macos | the Core Text engine |",
            "| three | the Core Text engine |"))
        rc, out = self._run(self.root, win="Parser.Opens\n",
                            mac="Parser.Opens\n")
        self.assertEqual(rc, 1)
        self.assertIn("has platform three", out)


if __name__ == "__main__":
    unittest.main()
