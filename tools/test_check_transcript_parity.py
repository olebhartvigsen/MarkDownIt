#!/usr/bin/env python3
"""Tests for check_transcript_parity.py, each on a deliberately bad case."""

import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_transcript_parity as ctp  # noqa: E402


def case(name="Bold", passed=True, undo_depth=2, text_hash="ab12",
         **extra):
    entry = {"name": name, "passed": passed, "undo_depth": undo_depth,
             "text_hash": text_hash}
    entry.update(extra)
    return entry


def digest(cases):
    return json.dumps({"cases": cases})


SAME = digest([case(), case("Heading", undo_depth=3, text_hash="cd34")])
EMPTY = digest([])


class TranscriptParity(unittest.TestCase):
    def _run(self, win=SAME, mac=SAME):
        root = self.root = tempfile.mkdtemp()
        wp = os.path.join(root, "win.json")
        mp = os.path.join(root, "mac.json")
        with open(wp, "w", encoding="utf-8") as fh:
            fh.write(win)
        with open(mp, "w", encoding="utf-8") as fh:
            fh.write(mac)
        hold = sys.argv
        sys.argv = ["x", "--windows", wp, "--macos", mp]
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = ctp.main()
        finally:
            sys.argv = hold
        return rc, buf.getvalue()

    def tearDown(self):
        shutil.rmtree(getattr(self, "root", "/nonexistent"),
                      ignore_errors=True)

    def test_identical_digests_pass(self):
        rc, out = self._run()
        self.assertEqual(rc, 0, out)
        self.assertIn("agree on every case", out)

    def test_empty_digests_pass_with_a_reason(self):
        # Before the corpus lands, both runners publish no cases; the
        # gate stays green and says why instead of failing silently.
        rc, out = self._run(win=EMPTY, mac=EMPTY)
        self.assertEqual(rc, 0, out)
        self.assertIn("no cases yet", out)

    def test_case_on_windows_only_fails(self):
        rc, out = self._run(win=digest([case(), case("Macless")]),
                            mac=digest([case()]))
        self.assertEqual(rc, 1, out)
        self.assertIn("Macless exists in the Windows digest only", out)

    def test_case_on_macos_only_fails(self):
        rc, out = self._run(win=digest([case()]),
                            mac=digest([case(), case("Winless")]))
        self.assertEqual(rc, 1, out)
        self.assertIn("Winless exists in the macOS digest only", out)

    def test_outcome_difference_fails(self):
        rc, out = self._run(win=digest([case(passed=True)]),
                            mac=digest([case(passed=False)]))
        self.assertEqual(rc, 1, out)
        # passed is printed as its JSON true/false spelling.
        self.assertIn("passed differs (windows True, macos False)", out)

    def test_undo_depth_difference_fails(self):
        rc, out = self._run(win=digest([case(undo_depth=4)]),
                            mac=digest([case(undo_depth=2)]))
        self.assertEqual(rc, 1, out)
        self.assertIn("undo_depth differs (windows 4, macos 2)", out)

    def test_text_hash_difference_fails(self):
        rc, out = self._run(win=digest([case(text_hash="a")]),
                            mac=digest([case(text_hash="b")]))
        self.assertEqual(rc, 1, out)
        self.assertIn("text_hash differs (windows 'a', macos 'b')", out)

    def test_missing_field_fails(self):
        broken = json.dumps({"cases": [
            {"name": "Bold", "passed": True, "undo_depth": 1}]})
        rc, out = self._run(win=broken)
        self.assertEqual(rc, 2, out)
        self.assertIn("without text_hash", out)

    def test_duplicate_case_fails(self):
        rc, out = self._run(win=digest([case(), case()]))
        self.assertEqual(rc, 2, out)
        self.assertIn("names the case Bold twice", out)

    def test_not_json_fails(self):
        rc, out = self._run(win="not json at all")
        self.assertEqual(rc, 2, out)
        self.assertIn("not valid JSON", out)

    def test_case_list_missing_fails(self):
        rc, out = self._run(win=json.dumps({"cases": "no"}))
        self.assertEqual(rc, 2, out)
        self.assertIn("cases list", out)

    def test_missing_file_fails(self):
        # A digest that one runner never wrote is a plumbing break,
        # not parity, and the checker says which side is missing.
        root = self.root = tempfile.mkdtemp()
        wp = os.path.join(root, "win.json")
        with open(wp, "w", encoding="utf-8") as fh:
            fh.write(SAME)
        hold = sys.argv
        sys.argv = ["x", "--windows", wp, "--macos",
                    os.path.join(root, "absent.json")]
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = ctp.main()
        finally:
            sys.argv = hold
        self.assertEqual(rc, 2)
        self.assertIn("could not be read", buf.getvalue())

    def test_extra_case_fields_are_ignored(self):
        # The runner may grow new per-case fields; they carry no
        # parity meaning, so they never fail this gate.
        rc, out = self._run(win=digest([case(duration_ms=1)]),
                            mac=digest([case(duration_ms=2)]))
        self.assertEqual(rc, 0, out)


if __name__ == "__main__":
    unittest.main()
