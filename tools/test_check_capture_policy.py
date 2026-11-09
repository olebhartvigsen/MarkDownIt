#!/usr/bin/env python3
"""Tests for check_capture_policy.py, each with a deliberately bad tree."""

import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_capture_policy as cp  # noqa: E402

REPO = os.path.dirname(HERE)
FILES = ("tools/windows-smoke/capture.ps1",
         "tools/macos-smoke/launch_smoke.sh",
         ".github/workflows/build.yml")


def make_tree():
    root = tempfile.mkdtemp()
    for rel in FILES:
        dst = os.path.join(root, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(REPO, rel), dst)
    return root


def edit(root, rel, old, new):
    path = os.path.join(root, rel)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    assert old in text, old
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text.replace(old, new, 1))


class CapturePolicy(unittest.TestCase):
    def setUp(self):
        self.root = make_tree()

    def tearDown(self):
        shutil.rmtree(self.root)

    def errors(self):
        return cp.run(self.root)[0]

    def test_repo_is_clean(self):
        self.assertEqual(cp.run(REPO)[0], [])

    def test_unlisted_file_with_a_capture_fails(self):
        path = os.path.join(self.root, "tools", "grab.py")
        with open(path, "w") as fh:
            fh.write("import pyautogui\npyautogui.screenshot('a.png')\n")
        self.assertTrue(any("tools/grab.py" in e for e in self.errors()))

    def test_unlisted_cpp_capture_fails(self):
        os.makedirs(os.path.join(self.root, "src"))
        with open(os.path.join(self.root, "src", "x.cpp"), "w") as fh:
            fh.write("g.CopyFromScreen(a, b, c);\n")
        self.assertTrue(any("src/x.cpp" in e for e in self.errors()))

    def test_bare_screencapture_in_mac_script_fails(self):
        edit(self.root, "tools/macos-smoke/launch_smoke.sh",
             'ci_screencapture -x "build-mac/qa/cold-open.png"',
             'screencapture -x "build-mac/qa/cold-open.png"')
        self.assertTrue(any("bare screencapture" in e
                            for e in self.errors()))

    def test_mac_guard_without_ci_test_fails(self):
        edit(self.root, "tools/macos-smoke/launch_smoke.sh",
             '"${GITHUB_ACTIONS:-}" != "true"', '"x" != "y"')
        self.assertTrue(any("$GITHUB_ACTIONS" in e for e in self.errors()))

    def test_windows_guard_without_ci_test_fails(self):
        edit(self.root, "tools/windows-smoke/capture.ps1",
             '$env:GITHUB_ACTIONS -eq "true"', '$true')
        self.assertTrue(any("$env:GITHUB_ACTIONS" in e
                            for e in self.errors()))

    def test_windows_guard_removed_fails(self):
        edit(self.root, "tools/windows-smoke/capture.ps1",
             "function Test-CiCapturePolicy",
             "function Test-Other")
        self.assertTrue(any("Test-CiCapturePolicy" in e
                            for e in self.errors()))

    def test_windows_capture_before_guard_fails(self):
        edit(self.root, "tools/windows-smoke/capture.ps1", "exit 3", "")
        self.assertTrue(self.errors())

    def test_dark_capture_step_fails(self):
        edit(self.root, ".github/workflows/build.yml",
             "      - name: Capture the states (QA walk)\n",
             "      - name: Capture the states (QA walk)\n"
             "        if: ${{ false }}\n")
        self.assertTrue(any("switches a capture step off" in e
                            for e in self.errors()))


if __name__ == "__main__":
    unittest.main()
