#!/usr/bin/env python3
"""Exercise the parity gate's failure paths without native drawing APIs."""

import copy
import unittest

from check_svg_parity import FILES, OP_FIELDS, ZOOMS, compare


def operations():
    rect = [10, 20, 30, 40, 0, 0, 0, 0, 1, 0]
    return [
        *[[name, *rect] for name in (
            "fillRect", "fillRoundedRect", "strokeRect", "strokeRoundedRect",
            "fillEllipse", "strokeEllipse", "pushClip",
        )],
        ["line", 10, 20, 30, 40, 0, 0, 0, 1, 2],
        ["text", "Reference", "Arial", 12, 0, 0, 0, 10.0, 20.0, 0, 0, 0, 1],
        ["fillPolygon", [[0, 0], [10, 0], [0, 10]], 0, 0, 0, 1, 0, 1],
        ["strokePolygon", [[0, 0], [10, 0]], 0, 0, 0, 1, 2, 0],
        ["pieSlice", 10, 20, 30, 40, 0, 180, 0, 0, 0, 1],
        ["popClip"],
    ]


def trace():
    return {
        "schema": 1,
        "reference": "macOS",
        "metrics": "fixed-half-em",
        "cases": [
            {
                "file": name,
                "zoom": zoom,
                "translated": translated,
                "operations": operations(),
            }
            for name in FILES
            for zoom in ZOOMS
            for translated in (0, 1)
        ],
    }


def replace_operation(data, operation):
    data["cases"][0]["operations"] = [copy.deepcopy(operation)]


class ParityGateTests(unittest.TestCase):
    def assert_rejected(self, change):
        # Invalid data must fail on either side, even when both sides agree.
        for side in ("macOS", "Windows", "both"):
            with self.subTest(side=side):
                reference = trace()
                windows = copy.deepcopy(reference)
                if side in ("macOS", "both"):
                    change(reference)
                if side in ("Windows", "both"):
                    change(windows)
                with self.assertRaises(ValueError):
                    compare(reference, windows)

    def assert_operation_rejected(self, operation):
        self.assert_rejected(lambda data: replace_operation(data, operation))

    def test_equal_traces_pass(self):
        data = trace()
        self.assertEqual(len(FILES), 6)
        self.assertEqual(len(data["cases"]), 36)
        self.assertEqual(set(OP_FIELDS), {op[0] for op in operations()})
        self.assertEqual(compare(data, copy.deepcopy(data)), (36, 36 * len(operations())))

    def test_top_level_type_fails(self):
        for value in (None, [], "trace", 1, True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                compare(value, value)

    def test_top_level_fields_exact(self):
        for field in ("schema", "reference", "metrics", "cases"):
            with self.subTest(missing=field):
                self.assert_rejected(lambda data: data.pop(field))
        self.assert_rejected(lambda data: data.update(extra=0))

    def test_metadata_types_and_values(self):
        for field, values in {
            "schema": (True, 1.0, "1", 0, 2, None),
            "reference": (None, [], 1, "Windows"),
            "metrics": (None, [], 1, "native"),
            "cases": (None, {}, "cases", True),
        }.items():
            for value in values:
                with self.subTest(field=field, value=value):
                    self.assert_rejected(lambda data: data.__setitem__(field, value))

    def test_missing_case_fails(self):
        self.assert_rejected(lambda data: data["cases"].pop())

    def test_duplicate_case_fails(self):
        self.assert_rejected(lambda data: data["cases"].append(data["cases"][0]))
        self.assert_rejected(lambda data: data["cases"].__setitem__(-1, data["cases"][0]))

    def test_empty_inventory_fails(self):
        self.assert_rejected(lambda data: data["cases"].clear())

    def test_case_fields_exact(self):
        for field in ("file", "zoom", "translated", "operations"):
            with self.subTest(missing=field):
                self.assert_rejected(lambda data: data["cases"][0].pop(field))
        self.assert_rejected(lambda data: data["cases"][0].update(extra=0))
        for value in ([], None, "case", 1):
            self.assert_rejected(lambda data: data["cases"].__setitem__(0, value))

    def test_case_identity_types_and_values(self):
        for field, values in {
            "file": (None, [], 1, "unknown.svg"),
            "zoom": (True, False, None, [], "1", 0, -1, 1.0001, float("nan"), float("inf")),
            "translated": (True, False, None, [], "0", 0.0, 1.0, 0.0001, 0.9999, -1, 2),
        }.items():
            for value in values:
                with self.subTest(field=field, value=value):
                    self.assert_rejected(lambda data: data["cases"][0].__setitem__(field, value))

    def test_empty_or_invalid_operation_stream_fails(self):
        for value in ([], None, {}, "commands", True):
            self.assert_rejected(lambda data: data["cases"][0].__setitem__("operations", value))

    def test_unknown_or_malformed_operation_fails(self):
        for operation in (["not-a-drawing-command"], [], None, "text", {}, [1], [True], [[]]):
            self.assert_operation_rejected(operation)

    def test_exact_arity_for_every_operation(self):
        for operation in operations():
            with self.subTest(operation=operation[0]):
                self.assert_operation_rejected(operation[:-1])
                self.assert_operation_rejected(operation + [0])
        self.assert_operation_rejected(["text"])

    def test_all_numeric_fields_require_finite_numbers(self):
        for operation in operations():
            for index, kind in enumerate(OP_FIELDS[operation[0]], 1):
                if kind != "n":
                    continue
                for value in (True, False, "0", None, [], {}, float("nan"), float("inf"), -float("inf"), 10 ** 400):
                    with self.subTest(operation=operation[0], field=index, value_type=type(value).__name__):
                        changed = copy.deepcopy(operation)
                        changed[index] = value
                        self.assert_operation_rejected(changed)

    def test_text_and_font_must_be_strings(self):
        for index in (1, 2):
            for value in (None, True, 12, [], {}):
                changed = operations()[8]
                changed[index] = value
                self.assert_operation_rejected(changed)

    def test_flags_require_integer_zero_or_one(self):
        for operation in operations():
            for index, kind in enumerate(OP_FIELDS[operation[0]], 1):
                if kind != "f":
                    continue
                for value in (True, False, 0.0, 1.0, -1, 2, 0.0001, 0.9999, "0", None, [], float("nan")):
                    with self.subTest(operation=operation[0], field=index, value=value):
                        changed = copy.deepcopy(operation)
                        changed[index] = value
                        self.assert_operation_rejected(changed)

    def test_polygon_point_shapes_and_counts(self):
        for operation in operations()[9:11]:
            for points in (None, {}, "points", [], [[0, 0]], [0, 1, 2],
                           [[0], [1, 2], [3, 4]], [[0, 1, 2], [1, 2], [3, 4]],
                           [[0, 0], None, [3, 4]]):
                changed = copy.deepcopy(operation)
                changed[1] = points
                self.assert_operation_rejected(changed)
        filled = operations()[9]
        filled[1] = [[0, 0], [1, 1]]
        self.assert_operation_rejected(filled)
        closed = operations()[10]
        closed[-1] = 1
        self.assert_operation_rejected(closed)

    def test_nested_coordinates_require_finite_numbers(self):
        for operation in operations()[9:11]:
            for axis in (0, 1):
                for value in (True, "0", None, [], float("nan"), float("inf"), -float("inf")):
                    changed = copy.deepcopy(operation)
                    changed[1][0][axis] = value
                    self.assert_operation_rejected(changed)

    def test_filled_polygon_must_be_closed(self):
        changed = operations()[9]
        changed[-1] = 0
        self.assert_operation_rejected(changed)

    def test_small_rounding_difference_passes(self):
        reference = trace()
        windows = copy.deepcopy(reference)
        for operation in windows["cases"][0]["operations"]:
            for index, kind in enumerate(OP_FIELDS[operation[0]], 1):
                if kind == "n":
                    operation[index] += 0.0001
                elif kind == "p":
                    operation[index][0][0] += 0.0001
        self.assertEqual(compare(reference, windows)[0], 36)

    def test_numeric_drift_fails_for_every_field(self):
        for operation_index, operation in enumerate(operations()):
            for index, kind in enumerate(OP_FIELDS[operation[0]], 1):
                if kind not in ("n", "p"):
                    continue
                reference = trace()
                windows = copy.deepcopy(reference)
                changed = windows["cases"][0]["operations"][operation_index]
                if kind == "n":
                    changed[index] += 0.0011
                else:
                    changed[index][-1][1] += 0.0011
                for left, right in ((reference, windows), (windows, reference)):
                    with self.subTest(operation=operation[0], field=index), self.assertRaises(ValueError):
                        compare(left, right)

    def test_valid_discrete_flag_changes_fail(self):
        for index in (4, 5, 6):
            reference = trace()
            windows = copy.deepcopy(reference)
            windows["cases"][0]["operations"][8][index] = 1
            with self.assertRaises(ValueError):
                compare(reference, windows)
        reference = trace()
        reference["cases"][0]["operations"][10][1].append([0, 10])
        windows = copy.deepcopy(reference)
        windows["cases"][0]["operations"][10][-1] = 1
        with self.assertRaises(ValueError):
            compare(reference, windows)

    def test_text_font_and_operation_drift_fail(self):
        for index in (1, 2):
            reference = trace()
            windows = copy.deepcopy(reference)
            windows["cases"][0]["operations"][8][index] = "Changed"
            with self.assertRaises(ValueError):
                compare(reference, windows)
        reference = trace()
        windows = copy.deepcopy(reference)
        windows["cases"][0]["operations"][0][0] = "strokeRect"
        with self.assertRaises(ValueError):
            compare(reference, windows)

    def test_command_and_point_counts_match(self):
        for change in (
            lambda data: data["cases"][0]["operations"].pop(),
            lambda data: data["cases"][0]["operations"][9][1].append([5, 5]),
            lambda data: data["cases"][0]["operations"].reverse(),
        ):
            reference = trace()
            windows = copy.deepcopy(reference)
            change(windows)
            with self.assertRaises(ValueError):
                compare(reference, windows)

    def test_case_order_is_not_significant(self):
        reference = trace()
        windows = copy.deepcopy(reference)
        windows["cases"].reverse()
        self.assertEqual(compare(reference, windows)[0], 36)


if __name__ == "__main__":
    unittest.main()
