#!/usr/bin/env python3
"""Failure-path tests for native paint-probe comparison."""

import copy
import io
import json
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

from check_svg_native_parity import PROBES, TEXT, compare, main


def trace():
    return {
        "schema": 2,
        "probes": [{"name": name, "rgb": [0, 0, 0]} for name in PROBES],
        "text": [{"id": name, "bounds": [30, 112, 50, 130]} for name in TEXT],
        "arrows": [
            {"id": "right", "bounds": [30, 16, 80, 24], "marker": [70, 16, 80, 24]},
            {"id": "left-dashed", "bounds": [160, 16, 210, 24], "marker": [160, 16, 170, 24]},
            {"id": "down", "bounds": [16, 80, 24, 110], "marker": [16, 100, 24, 110]},
            {"id": "up-dashed", "bounds": [76, 80, 84, 110], "marker": [76, 80, 84, 90]},
        ],
    }


class NativeParityTests(unittest.TestCase):
    def test_equal(self):
        self.assertEqual(compare(trace(), trace()), (18, 3, 4))

    def reject(self, change):
        for side in (0, 1):
            args = [trace(), trace()]
            change(args[side])
            with self.assertRaises((ValueError, KeyError, TypeError)):
                compare(*args)

    def test_missing_probe(self):
        self.reject(lambda d: d["probes"].pop())

    def test_wrong_probe_name(self):
        self.reject(lambda d: d["probes"][0].__setitem__("name", "unknown"))

    def test_invalid_color(self):
        self.reject(lambda d: d["probes"][0].__setitem__("rgb", [False, 0, 0]))

    def test_color_drift(self):
        self.reject(lambda d: d["probes"][0].__setitem__("rgb", [255, 0, 0]))

    def test_extra_baseline(self):
        self.reject(lambda d: d["text"][1].__setitem__("bounds", [30, 112, 50, 139]))

    def test_empty_text(self):
        self.reject(lambda d: d["text"][0].__setitem__("bounds", [30, 112, 30, 130]))

    def test_small_ink_rounding(self):
        actual = copy.deepcopy(trace())
        actual["text"][0]["bounds"][0] += 1
        compare(trace(), actual)

    def test_old_schema(self):
        self.reject(lambda d: d.__setitem__("schema", 1))

    def test_invalid_schema_type(self):
        for value in (True, 2.0, "2", None, 3):
            with self.subTest(value=value):
                self.reject(lambda d: d.__setitem__("schema", value))

    def test_missing_arrows_field(self):
        self.reject(lambda d: d.pop("arrows"))

    def test_invalid_arrows_list(self):
        for value in (None, {}, "arrows", tuple(trace()["arrows"])):
            with self.subTest(value=value):
                self.reject(lambda d: d.__setitem__("arrows", value))

    def test_missing_arrow(self):
        self.reject(lambda d: d["arrows"].pop())

    def test_extra_arrow(self):
        self.reject(lambda d: d["arrows"].append(copy.deepcopy(d["arrows"][0])))

    def test_reordered_arrows(self):
        self.reject(lambda d: d["arrows"].reverse())

    def test_duplicate_arrow(self):
        self.reject(lambda d: d["arrows"].__setitem__(1, copy.deepcopy(d["arrows"][0])))

    def test_malformed_arrow(self):
        for item in (None, [], {}, {"id": "right"}, {"bounds": [30, 16, 80, 24]},
                     {"id": "unknown", "bounds": [30, 16, 80, 24], "marker": [70, 16, 80, 24]},
                     {"id": "right", "bounds": [30, 16, 80, 24], "marker": [70, 16, 80, 24], "extra": 0}):
            with self.subTest(item=item):
                self.reject(lambda d: d["arrows"].__setitem__(0, item))

    def test_malformed_arrow_bounds(self):
        for bounds in (None, {}, "30,16,80,24", (30, 16, 80, 24), [],
                       [30, 16, 80], [30, 16, 80, 24, 25]):
            with self.subTest(bounds=bounds):
                self.reject(lambda d: d["arrows"][0].__setitem__("bounds", bounds))

    def test_invalid_arrow_coordinates(self):
        for arrow in range(4):
            for axis in range(4):
                for value in (False, True, 30.0, "30", None, -1):
                    with self.subTest(arrow=arrow, axis=axis, value=value):
                        self.reject(lambda d: d["arrows"][arrow]["bounds"].__setitem__(axis, value))

    def test_empty_or_out_of_range_arrow(self):
        for bounds in ([30, 16, 30, 24], [80, 16, 30, 24],
                       [30, 24, 80, 24], [30, 25, 80, 24],
                       [30, 16, 241, 24], [30, 16, 80, 141]):
            with self.subTest(bounds=bounds):
                self.reject(lambda d: d["arrows"][0].__setitem__("bounds", bounds))

    def test_arrow_one_pixel_tolerance(self):
        for side in (0, 1):
            for arrow in range(4):
                for axis in range(4):
                    for delta in (-1, 1):
                        with self.subTest(side=side, arrow=arrow, axis=axis, delta=delta):
                            args = [trace(), trace()]
                            args[side]["arrows"][arrow]["bounds"][axis] += delta
                            self.assertEqual(compare(*args), (18, 3, 4))

    def test_arrow_two_pixel_drift(self):
        for arrow in range(4):
            for axis in range(4):
                for delta in (-2, 2):
                    with self.subTest(arrow=arrow, axis=axis, delta=delta):
                        def change(data):
                            data["arrows"][arrow]["bounds"][axis] += delta
                        self.reject(change)

    def test_arrow_pairwise_tolerance(self):
        for arrow in range(4):
            for axis in range(4):
                with self.subTest(arrow=arrow, axis=axis):
                    macos, windows = trace(), trace()
                    macos["arrows"][arrow]["bounds"][axis] -= 1
                    windows["arrows"][arrow]["bounds"][axis] += 1
                    with self.assertRaisesRegex(ValueError, "macOS=.*Windows="):
                        compare(macos, windows)

    def test_identical_source_gap_fails_reference(self):
        for arrow, axis, delta in ((0, 0, 4), (1, 2, -4), (2, 1, 4), (3, 3, -4)):
            with self.subTest(arrow=arrow):
                bad = trace()
                bad["arrows"][arrow]["bounds"][axis] += delta
                with self.assertRaisesRegex(ValueError, "differs from reference"):
                    compare(bad, copy.deepcopy(bad))

    def test_identical_tip_overshoot_fails_reference(self):
        for arrow, axis, delta in ((0, 2, 4), (1, 0, -4), (2, 3, 4), (3, 1, -4)):
            with self.subTest(arrow=arrow):
                bad = trace()
                bad["arrows"][arrow]["bounds"][axis] += delta
                with self.assertRaisesRegex(ValueError, "differs from reference"):
                    compare(bad, copy.deepcopy(bad))

    def test_missing_marker(self):
        for arrow in range(4):
            with self.subTest(arrow=arrow):
                self.reject(lambda d: d["arrows"][arrow].pop("marker"))

    def test_malformed_marker_bounds(self):
        for arrow in range(4):
            for marker in (None, {}, "70,16,80,24", (70, 16, 80, 24), [],
                           [70, 16, 80], [70, 16, 80, 24, 25]):
                with self.subTest(arrow=arrow, marker=marker):
                    self.reject(lambda d: d["arrows"][arrow].__setitem__("marker", marker))

    def test_invalid_marker_coordinates(self):
        for arrow in range(4):
            for axis in range(4):
                for value in (False, True, 70.0, "70", None, -1):
                    with self.subTest(arrow=arrow, axis=axis, value=value):
                        self.reject(lambda d: d["arrows"][arrow]["marker"].__setitem__(axis, value))

    def test_empty_or_out_of_range_marker(self):
        for arrow in range(4):
            for marker in ([70, 16, 70, 24], [80, 16, 70, 24],
                           [70, 24, 80, 24], [70, 25, 80, 24],
                           [70, 16, 241, 24], [70, 16, 80, 141]):
                with self.subTest(arrow=arrow, marker=marker):
                    self.reject(lambda d: d["arrows"][arrow].__setitem__("marker", marker))

    def test_reordered_marker_bounds(self):
        self.reject(lambda d: d["arrows"][0]["marker"].reverse())

    def test_reordered_marker_cases(self):
        def change(data):
            markers = [item["marker"] for item in reversed(data["arrows"])]
            for item, marker in zip(data["arrows"], markers):
                item["marker"] = marker
        self.reject(change)

    def test_reordered_arrow_fields_allowed(self):
        actual = trace()
        for item in actual["arrows"]:
            reordered = {"marker": item["marker"], "bounds": item["bounds"], "id": item["id"]}
            item.clear()
            item.update(reordered)
        self.assertEqual(compare(trace(), actual), (18, 3, 4))
        self.assertEqual(compare(actual, trace()), (18, 3, 4))

    def test_marker_one_pixel_tolerance(self):
        for side in (0, 1):
            for arrow in range(4):
                for axis in range(4):
                    for delta in (-1, 1):
                        with self.subTest(side=side, arrow=arrow, axis=axis, delta=delta):
                            args = [trace(), trace()]
                            args[side]["arrows"][arrow]["marker"][axis] += delta
                            self.assertEqual(compare(*args), (18, 3, 4))

    def test_marker_two_pixel_drift(self):
        for arrow in range(4):
            for axis in range(4):
                for delta in (-2, 2):
                    with self.subTest(arrow=arrow, axis=axis, delta=delta):
                        def change(data):
                            data["arrows"][arrow]["marker"][axis] += delta
                        self.reject(change)

    def test_marker_pairwise_tolerance(self):
        for arrow in range(4):
            for axis in range(4):
                for delta in (-1, 1):
                    with self.subTest(arrow=arrow, axis=axis, delta=delta):
                        macos, windows = trace(), trace()
                        macos["arrows"][arrow]["marker"][axis] -= delta
                        windows["arrows"][arrow]["marker"][axis] += delta
                        with self.assertRaisesRegex(ValueError, "Native arrow marker differs.*macOS=.*Windows="):
                            compare(macos, windows)

    def test_identical_marker_tip_undershoot_fails_reference(self):
        for arrow, axis, delta in ((0, 2, -4), (1, 0, 4), (2, 3, -4), (3, 1, 4)):
            with self.subTest(arrow=arrow):
                bad = trace()
                bad["arrows"][arrow]["marker"][axis] += delta
                self.assertEqual(bad["arrows"][arrow]["bounds"], trace()["arrows"][arrow]["bounds"])
                with self.assertRaisesRegex(ValueError, "Native arrow marker differs from reference"):
                    compare(bad, copy.deepcopy(bad))

    def test_identical_marker_tip_overshoot_fails_reference(self):
        for arrow, axis, delta in ((0, 2, 4), (1, 0, -4), (2, 3, 4), (3, 1, -4)):
            with self.subTest(arrow=arrow):
                bad = trace()
                bad["arrows"][arrow]["marker"][axis] += delta
                self.assertEqual(bad["arrows"][arrow]["bounds"], trace()["arrows"][arrow]["bounds"])
                with self.assertRaisesRegex(ValueError, "Native arrow marker differs from reference"):
                    compare(bad, copy.deepcopy(bad))

    def test_shape_tolerance_unchanged(self):
        actual = trace()
        actual["probes"][0]["rgb"] = [2, 2, 2]
        self.assertEqual(compare(trace(), actual), (18, 3, 4))
        actual["probes"][0]["rgb"][0] = 3
        with self.assertRaisesRegex(ValueError, "Native paint differs"):
            compare(trace(), actual)

    def test_text_tolerance_unchanged(self):
        actual = trace()
        actual["text"][0]["bounds"] = [33, 115, 53, 133]
        self.assertEqual(compare(trace(), actual), (18, 3, 4))
        actual["text"][0]["bounds"][0] += 1
        with self.assertRaisesRegex(ValueError, "Native text differs"):
            compare(trace(), actual)

    def test_main_reports_arrow_contacts(self):
        output = io.StringIO()
        with patch("sys.argv", ["check_svg_native_parity.py", "macos.json", "windows.json"]), \
             patch("check_svg_native_parity.Path.read_text", return_value=json.dumps(trace())), \
             redirect_stdout(output):
            main()
        self.assertEqual(output.getvalue(), "Native SVG parity passed: 18 shape probes, 3 text bounds, 4 arrow contacts match macOS\n")


if __name__ == "__main__":
    unittest.main()
