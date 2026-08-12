#pragma once
#include <cstdint>

// A caret position is a byte offset into the TextBuffer.
// Storing source offsets rather than block indices means the caret
// survives a reparse that renumbers blocks.
struct CaretPos {
    uint32_t offset = 0;
    bool operator==(const CaretPos& o) const { return offset == o.offset; }
    bool operator!=(const CaretPos& o) const { return offset != o.offset; }
    bool operator<(const CaretPos& o) const { return offset < o.offset; }
    bool operator>(const CaretPos& o) const { return offset > o.offset; }
};

// A selection is an anchor and an active end. Anchor is where the
// drag started; active is where the mouse or shift-arrow is now.
struct Selection {
    CaretPos anchor;
    CaretPos active;

    bool Empty() const { return anchor == active; }
    uint32_t Start() const {
        return anchor < active ? anchor.offset : active.offset;
    }
    uint32_t End() const {
        return anchor < active ? active.offset : anchor.offset;
    }
    uint32_t Length() const { return End() - Start(); }
    void Collapse(CaretPos p) { anchor = p; active = p; }
};
