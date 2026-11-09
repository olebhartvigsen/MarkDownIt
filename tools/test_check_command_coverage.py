#!/usr/bin/env python3
"""Tests for check_command_coverage.py, each on a deliberately bad tree."""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_command_coverage as ccc  # noqa: E402

REPO = os.path.dirname(HERE)

RIBBON = """<?xml ?>
<Application>
  <Command Name="cmdTabHome"/>
  <Command Name="cmdGroupFormat"/>
  <Command Name="cmdBold"/>
  <Command Name="cmdItalic"/>
  <Command Name="cmdWidthMenu"/>
  <Command Name="cmdWidthStandard"/>
</Application>
"""

TOOLBAR = ("// mac toolbar, carries a selector the menu also has\n"
           "action:@selector(toggleBold:)\n")

LEDGER = """# Platform parity ledger

## Commands: ribbon to mac menu

| Ribbon command | mac selector |
|---|---|
| cmdBold | toggleBold |
| cmdItalic | toggleItalic |

## Commands without a mac counterpart

| Ribbon command | Reason |
|---|---|
| cmdWidthStandard | the mac side follows the pane |

## Mac commands without a ribbon counterpart

| mac selector | Reason |
|---|---|
| openSettings | the Windows app keeps settings under File |

## Sources built by no target

- none
"""


def make_tree(ribbon=RIBBON, menu=None, toolbar=None, ledger=LEDGER):
    from pathlib import Path
    root = tempfile.mkdtemp()
    if menu is None:
        menu = "// mac menu\n" \
              "[m action:@selector(toggleBold:)];\n" \
              "[m action:@selector(toggleItalic:)];\n" \
              "[m action:@selector(openSettings:)];\n"
    if toolbar is None:
        toolbar = "// mac toolbar\naction:@selector(toggleBold:)\n"
    (Path(root) / "src" / "macos").mkdir(parents=True)
    (Path(root) / "references").mkdir(parents=True)
    (Path(root) / "src" / "ribbon.xml").write_text(ribbon)
    (Path(root) / "src" / "macos" / "menu_mac.mm").write_text(menu)
    (Path(root) / "src" / "macos" / "toolbar_mac.mm").write_text(toolbar)
    (Path(root) / "references" / "platform-parity.md").write_text(ledger)
    return root


def run_on(root):
    hold = sys.argv
    sys.argv = ["x", root]
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            rc = ccc.main()
    finally:
        sys.argv = hold
    return rc, buf.getvalue()


class CommandCoverage(unittest.TestCase):
    def tearDown(self):
        shutil.rmtree(getattr(self, "root", "/nonexistent"),
                      ignore_errors=True)

    def test_repo_is_clean(self):
        rc, out = run_on(REPO)
        self.assertEqual(rc, 0, out)

    def test_covered_commands_pass(self):
        self.root = make_tree()
        rc, out = run_on(self.root)
        self.assertEqual(rc, 0, out)

    def test_promised_selector_missing_fails(self):
        # The ledger maps cmdBold to toggleBold, but the mac side
        # carries only toggleItalic and openSettings.
        self.root = make_tree(
            menu="// mac menu\n[m action:@selector(toggleItalic:)];\n"
                 "[m action:@selector(openSettings:)];\n",
            toolbar="// mac toolbar, empty here\n")
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("cmdBold maps to toggleBold, but", out)

    def test_command_without_entry_or_exception_fails(self):
        self.root = make_tree(ledger=LEDGER.replace(
            "| cmdItalic | toggleItalic |\n", ""))
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("cmdItalic has no mac entry and no row", out)

    def test_stale_mapping_row_fails(self):
        self.root = make_tree(ledger=LEDGER.replace(
            "| cmdBold | toggleBold |", "| cmdGhost | toggleBold |"))
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("cmdGhost is mapped in the ledger", out)

    def test_stale_mac_only_row_fails(self):
        self.root = make_tree(ledger=LEDGER.replace(
            "| openSettings |", "| ghost |"))
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("ghost is in the mac-only table but", out)

    def test_undocumented_mac_selector_fails(self):
        self.root = make_tree(ledger=LEDGER.replace(
            "| openSettings | the Windows app keeps settings under File |",
            ""))
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("openSettings is carried by menu or toolbar", out)


if __name__ == "__main__":
    unittest.main()
