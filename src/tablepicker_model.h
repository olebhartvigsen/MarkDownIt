#pragma once

// The rules of the Word-style table-size picker: how many cells the grid
// offers, how the selection clamps, and what the label says. The Windows
// popup in src/app.cpp (TableGridPicker) follows these same rules, and the
// mac picker in src/macos/table_picker_mac.mm reads this struct, so the two
// cannot disagree about the range or the label.
//
// The label counts the header row the way Word does: three data rows make a
// "4 x 3 Table", because the header is part of the table the user gets.
//
// references/ribbon-menu-guidelines.md section 12 requires the picker to
// open below the Table button, to take keyboard input as well as the mouse,
// and to stay inside the monitor's work area. The range and label live
// here; the anchoring and the input live in each platform's view.

#include <string>

struct TablePickerModel {
    static constexpr int kMinCols = 1;
    static constexpr int kMinRows = 1;
    static constexpr int kMaxCols = 10;
    static constexpr int kMaxRows = 8;  // data rows; the header row is extra

    // Three by three is what Word offers before the pointer moves.
    int cols = 3;
    int rows = 3;

    static int ClampCols(int value) {
        if (value < kMinCols) return kMinCols;
        if (value > kMaxCols) return kMaxCols;
        return value;
    }

    static int ClampRows(int value) {
        if (value < kMinRows) return kMinRows;
        if (value > kMaxRows) return kMaxRows;
        return value;
    }

    // Sets the size from a hovered cell. Both counts are 1-based: hovering
    // the first cell asks for a 1 x 1 table.
    void Set(int newCols, int newRows) {
        cols = ClampCols(newCols);
        rows = ClampRows(newRows);
    }

    // Arrow keys: one cell at a time in either direction.
    void Move(int deltaCols, int deltaRows) {
        Set(cols + deltaCols, rows + deltaRows);
    }

    // What the label counts, and what InsertTable receives for the header.
    int VisibleRows() const { return rows + 1; }

    // "4 x 3 Table": rows first, the way Word writes it.
    std::string Label() const {
        return std::to_string(VisibleRows()) + " x " +
               std::to_string(cols) + " Table";
    }
};
