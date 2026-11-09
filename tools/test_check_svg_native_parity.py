#!/usr/bin/env python3
"""Failure-path tests for native paint-probe comparison."""

import copy
import unittest

from check_svg_native_parity import PROBES, TEXT, compare


def trace():
    return {
        "schema": 1,
        "probes": [{"name": name, "rgb": [0, 0, 0]} for name in PROBES],
        "text": [{"id": name, "bounds": [30, 112, 50, 130]} for name in TEXT],
    }


class NativeParityTests(unittest.TestCase):
    def test_equal(self):
        self.assertEqual(compare(trace(), trace()), (18, 3))

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


if __name__ == "__main__":
    unittest.main()
