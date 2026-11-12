#!/usr/bin/env python3
"""Exercise the layout dump parity gate's failure paths."""

import copy
import unittest
from pathlib import Path

from check_layout_parity import compare


def case(name="fixture.md"):
    return {
        "file": name,
        "listWidth": 600.0,
        "listHeight": 120.0,
        "blocks": [
            {"kind": 2, "level": 0, "x": 0.0, "y": 0.0, "width": 600.0,
             "height": 24.0, "nodeIndex": 0, "srcOffset": 0,
             "srcLength": 10, "markerText": "", "markerWidth": 0.0,
             "task": False, "imagesHeight": 0.0, "imageUrls": [],
             "table": None},
        ],
        "items": [
            {"kind": "Text", "x": 0.0, "top": 0.0,
             "line": {"width": 80.0, "height": 24.0, "ascent": 19.0,
                      "runs": [{"text": "Overview",
                                "font": {"family": "Segoe UI", "size": 16.0,
                                         "bold": False, "italic": False,
                                         "monospace": False},
                                "r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0,
                                "x": 0.0, "underline": False,
                                "strike": False}]},
             "srcOffset": 0, "srcLength": 10},
        ],
    }


def trace():
    return {
        "schema": 1,
        "engine": "fake-8px",
        "style": "apple",
        "contentWidth": 600,
        "cases": [case("a.md"), case("b.md")],
    }


class LayoutParityGateTests(unittest.TestCase):
    def test_equal_dumps_pass(self):
        data = trace()
        cases, blocks, items = compare(
            data, copy.deepcopy(data), "ref", "act")
        self.assertEqual((cases, blocks, items), (2, 2, 2))

    def test_top_level_type_fails(self):
        for value in (None, [], "dump", 1, True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                compare(value, value, "ref", "act")

    def test_top_level_fields_exact(self):
        for field in ("schema", "engine", "style", "contentWidth",
                      "cases"):
            data = trace()
            data.pop(field)
            with self.subTest(missing=field), self.assertRaises(
                    ValueError):
                compare(data, trace(), "ref", "act")
        data = trace()
        data["extra"] = 0
        with self.assertRaises(ValueError):
            compare(data, trace(), "ref", "act")

    def test_schema_and_engine_must_match(self):
        data = trace()
        data["schema"] = 2
        with self.assertRaises(ValueError):
            compare(data, trace(), "ref", "act")
        data = trace()
        data["engine"] = "dwrite"
        with self.assertRaises(ValueError):
            compare(data, trace(), "ref", "act")

    def test_empty_inventory_fails(self):
        data = trace()
        data["cases"] = []
        with self.assertRaises(ValueError):
            compare(data, trace(), "ref", "act")

    def test_duplicate_fixture_fails(self):
        data = trace()
        data["cases"].append(data["cases"][0])
        with self.assertRaises(ValueError):
            compare(data, trace(), "ref", "act")

    def test_fixture_set_must_match(self):
        reference = trace()
        actual = trace()
        actual["cases"][1]["file"] = "c.md"
        with self.assertRaises(ValueError):
            compare(reference, actual, "ref", "act")

    def test_block_field_drift_fails(self):
        reference = trace()
        actual = copy.deepcopy(reference)
        actual["cases"][0]["blocks"][0]["x"] += 0.5
        with self.assertRaises(ValueError):
            compare(reference, actual, "ref", "act")

    def test_display_item_drift_fails(self):
        reference = trace()
        actual = copy.deepcopy(reference)
        actual["cases"][0]["items"][0]["line"]["width"] += 1.0
        with self.assertRaises(ValueError):
            compare(reference, actual, "ref", "act")

    def test_run_text_drift_fails(self):
        reference = trace()
        actual = copy.deepcopy(reference)
        run = actual["cases"][0]["items"][0]["line"]["runs"][0]
        run["text"] = "Changed"
        with self.assertRaises(ValueError):
            compare(reference, actual, "ref", "act")

    def test_case_order_is_not_significant(self):
        reference = trace()
        actual = copy.deepcopy(reference)
        actual["cases"].reverse()
        cases, _, _ = compare(reference, actual, "ref", "act")
        self.assertEqual(cases, 2)

    def test_layout_parity_dump_exists(self):
        root = Path(__file__).resolve().parents[1]
        self.assertTrue(
            (root / "tests/layout_parity_dump.cpp").exists())
        fixtures = list((root / "tests/fixtures/parity").glob("*.md"))
        self.assertGreaterEqual(len(fixtures), 5)


if __name__ == "__main__":
    unittest.main()
