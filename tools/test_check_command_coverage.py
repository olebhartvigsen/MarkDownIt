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
  <Command Name="cmdBold" Symbol="IDC_CMD_BOLD" Id="10300"/>
  <Command Name="cmdItalic" Symbol="IDC_CMD_ITALIC" Id="10301"/>
  <Command Name="cmdWidthMenu"/>
  <Command Name="cmdWidthStandard" Symbol="IDC_CMD_WIDTH_STD" Id="10601"/>
</Application>
"""

# The command table: the ribbon name, the ribbon number and the mac
# selector of every command the fixture tree holds.
COMMANDS_H = """// command table fixture
inline constexpr CommandSpec kCommands[] = {
    {CommandId::Bold, "Bold", "cmdBold", 10300, "toggleBold:", true, NoWin,
     Key("b")},
    {CommandId::Italic, "Italic", "cmdItalic", 10301, "toggleItalic:", true,
     NoWin, Key("i")},
    {CommandId::WidthStandard, "Standard", "cmdWidthStandard", 10601,
     "widthStandard:", false, NoWin, NoMac},
    {CommandId::Settings, "Settings", nullptr, 0, "openSettings:", false,
     NoWin, NoMac},
};
"""

RIBBON_CPP = """// ribbon fixture
commands::Enabled(spec->id, m_pApp->CommandContext())
"""

MAIN_MAC = """// mac menu fixture
commands::FromDocument(app_->controller());
commands::Enabled(spec->id, context);
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


def make_tree(ribbon=RIBBON, menu=None, toolbar=None, ledger=LEDGER,
              commands_h=COMMANDS_H, ribbon_cpp=RIBBON_CPP,
              main_mac=MAIN_MAC):
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
    (Path(root) / "src" / "commands.h").write_text(commands_h)
    (Path(root) / "src" / "ribbon.cpp").write_text(ribbon_cpp)
    (Path(root) / "src" / "macos" / "menu_mac.mm").write_text(menu)
    (Path(root) / "src" / "macos" / "toolbar_mac.mm").write_text(toolbar)
    (Path(root) / "src" / "macos" / "main_mac.mm").write_text(main_mac)
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

    def test_ribbon_number_mismatch_fails(self):
        # The table says 10300, the markup says 10302: the button would
        # ask about a different command than the one it runs.
        self.root = make_tree(
            ribbon=RIBBON.replace('Id="10300"', 'Id="10302"'))
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("ask about a different command", out)

    def test_ribbon_command_without_a_row_fails(self):
        self.root = make_tree(
            commands_h=COMMANDS_H.replace(
                '    {CommandId::Italic, "Italic", "cmdItalic", 10301, '
                '"toggleItalic:", true,\n     NoWin, Key("i")},\n', ''))
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("cmdItalic has no row in src/commands.h", out)

    def test_ribbon_without_the_shared_state_fails(self):
        self.root = make_tree(ribbon_cpp="// ribbon fixture, gate of its own\n")
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/ribbon.cpp does not read the shared command state",
                      out)

    def test_mac_menu_with_its_own_selector_list_fails(self):
        self.root = make_tree(
            main_mac=MAIN_MAC + "\n- (BOOL)actionChangesTheDocument:(SEL)a;\n")
        rc, out = run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("keeps a mutating selector list of its own", out)


if __name__ == "__main__":
    unittest.main()
