#!/usr/bin/env python3
"""Tests for tools/check_painter_seam.py.

The checker guards a number that only ever moves down, so the tests pin the
two ways it can be wrong: counting the wrong things, and letting a rise
through.
"""

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def load():
    spec = importlib.util.spec_from_file_location(
        "check_painter_seam", os.path.join(HERE, "check_painter_seam.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CountTest(unittest.TestCase):
    def test_counts_each_occurrence_not_each_line(self):
        m = load()
        text = ("void Draw(ID2D1RenderTarget* rt, IDWriteTextLayout* layout);\n"
                "ID2D1SolidColorBrush* brush;\n")
        self.assertEqual(m.count(m.WINDOWS_RAW, text), 3)

    def test_ignores_names_that_are_not_platform_types(self):
        m = load()
        text = "int width = 3; float height = 4.0f; // nothing platform here\n"
        self.assertEqual(m.count(m.WINDOWS_RAW, text), 0)

    def test_mac_pattern_catches_core_graphics_and_text(self):
        m = load()
        text = "CGContextRef ctx; CTLineRef line; NSGraphicsContext* g;\n"
        self.assertEqual(m.count(m.MAC_RAW, text), 3)


class VerdictTest(unittest.TestCase):
    def test_at_baseline_is_ok(self):
        m = load()
        self.assertEqual(m.verdict(111, 111), "ok")

    def test_below_baseline_is_ok(self):
        m = load()
        self.assertEqual(m.verdict(40, 111), "ok")

    def test_above_baseline_is_over(self):
        m = load()
        self.assertEqual(m.verdict(112, 111), "over")

    def test_zero_baseline_allows_zero_only(self):
        m = load()
        self.assertEqual(m.verdict(0, 0), "ok")
        self.assertEqual(m.verdict(1, 0), "over")


class RepositoryTest(unittest.TestCase):
    def test_the_repository_is_at_or_below_the_baseline(self):
        m = load()
        self.assertEqual(m.main(["--report"]), 0)


if __name__ == "__main__":
    unittest.main()
