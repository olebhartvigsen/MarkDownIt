#!/usr/bin/env python3
"""Tests for check_no_telemetry.py, each with a deliberately bad tree."""

import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_no_telemetry as nt  # noqa: E402

REPO = os.path.dirname(HERE)
FILES = (".github/workflows/build.yml",
         ".github/workflows/release.yml",
         "tools/macos-smoke/launch_smoke.sh",
         "tools/windows-smoke/capture.ps1",
         "src/analytics.h",
         "src/analytics.cpp",
         "tests/analytics_test.cpp")


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


class NoTelemetry(unittest.TestCase):
    def setUp(self):
        self.root = make_tree()

    def tearDown(self):
        shutil.rmtree(self.root)

    def errors(self):
        return nt.run(self.root)[0]

    def test_repo_is_clean(self):
        self.assertEqual(nt.run(REPO)[0], [])

    def test_workflow_without_the_veto_fails(self):
        edit(self.root, ".github/workflows/build.yml",
             "  MARKDOWNIT_NO_ANALYTICS: 1\n", "")
        self.assertTrue(any("does not set MARKDOWNIT_NO_ANALYTICS" in e
                            for e in self.errors()))

    def test_a_zeroed_veto_fails(self):
        edit(self.root, ".github/workflows/release.yml",
             "  MARKDOWNIT_NO_ANALYTICS: 1", "  MARKDOWNIT_NO_ANALYTICS: 0")
        self.assertTrue(any("release.yml" in e and "does not set" in e
                            for e in self.errors()))

    def test_veto_in_a_step_only_fails(self):
        # Step-level env does not cover the other steps that start the app.
        edit(self.root, ".github/workflows/build.yml",
             "env:\n  MARKDOWNIT_NO_ANALYTICS: 1\n", "env:\n")
        edit(self.root, ".github/workflows/build.yml",
             "      - name: Capture the states (QA walk)\n",
             "      - name: Capture the states (QA walk)\n"
             "        env:\n          MARKDOWNIT_NO_ANALYTICS: 1\n")
        self.assertTrue(any("does not set MARKDOWNIT_NO_ANALYTICS" in e
                            for e in self.errors()))

    def test_harness_without_the_veto_fails(self):
        edit(self.root, "tools/macos-smoke/launch_smoke.sh",
             "export MARKDOWNIT_NO_ANALYTICS=1\n", "")
        self.assertTrue(any("launch_smoke.sh" in e and "without setting" in e
                            for e in self.errors()))

    def test_windows_harness_without_the_veto_fails(self):
        edit(self.root, "tools/windows-smoke/capture.ps1",
             '$env:MARKDOWNIT_NO_ANALYTICS = "1"\n', "")
        self.assertTrue(any("capture.ps1" in e and "without setting" in e
                            for e in self.errors()))

    def test_a_missing_workflow_fails(self):
        os.remove(os.path.join(self.root, ".github/workflows/release.yml"))
        self.assertTrue(any("release.yml is missing" in e
                            for e in self.errors()))

    def test_a_second_sender_fails(self):
        with open(os.path.join(self.root, "src/renderer.cpp"), "w",
                  encoding="utf-8") as fh:
            fh.write('const char* u = "https://eu.aptabase.com/api/v0/event";\n')
        self.assertTrue(any("src/renderer.cpp" in e and "second sender" in e
                            for e in self.errors()))

    def test_the_one_sender_is_allowed(self):
        # src/analytics.h holds the endpoint and must not be flagged.
        self.assertIn("src/analytics.h", nt.run(self.root)[1])
        self.assertEqual(self.errors(), [])


if __name__ == "__main__":
    unittest.main()