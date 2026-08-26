#include "undostack.h"
#include <chrono>

static uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void UndoStack::Push(const UndoEntry& entry) {
    // Try to coalesce with the previous entry.
    if (!coalesce_broken_ && !undo_stack_.empty()) {
        UndoEntry& prev = undo_stack_.back();
        if (ShouldCoalesce(prev, entry)) {
            // Merge into the previous entry.
            if (entry.type == EditType::Insert) {
                prev.inserted += entry.inserted;
            } else if (entry.type == EditType::Delete) {
                // Backward deletes: removed text extends to the left,
                // so prepend and move the entry start backward.
                if (entry.offset < prev.offset) {
                    prev.removed = entry.removed + prev.removed;
                    prev.offset = entry.offset;
                } else {
                    // Forward deletes: caret stationary, removed text
                    // extends to the right, so append.
                    prev.removed = prev.removed + entry.removed;
                }
            }
            prev.selAfter = entry.selAfter;
            prev.timestamp = entry.timestamp;
            // Clear redo: a new edit invalidates the redo stack.
            redo_stack_.clear();
            return;
        }
    }

    undo_stack_.push_back(entry);
    coalesce_broken_ = false;
    // Clear redo: a new edit invalidates the redo stack.
    redo_stack_.clear();
}

bool UndoStack::ShouldCoalesce(const UndoEntry& prev, const UndoEntry& next) const {
    // Same edit type.
    if (prev.type != next.type) return false;
    // Under 800 ms apart.
    if (next.timestamp - prev.timestamp > 800) return false;
    // Contiguous offsets: the next insert starts where the previous one ended.
    if (prev.type == EditType::Insert) {
        if (prev.offset + static_cast<uint32_t>(prev.inserted.size()) != next.offset)
            return false;
        // Break on whitespace: typing a space should start a new undo group.
        if (!next.inserted.empty() && (next.inserted.back() == ' ' || next.inserted.back() == '\n'))
            return false;
        return true;
    }
    if (prev.type == EditType::Delete) {
        // Backward deletes: next offset is before prev offset.
        if (next.offset + static_cast<uint32_t>(next.removed.size()) == prev.offset)
            return true;
        // Forward deletes: caret stationary, removed text extends the
        // previous entry (Delete key runs should group like typing).
        if (next.offset == prev.offset)
            return true;
        return false;
    }
    return false;
}

bool UndoStack::Undo(UndoEntry& out) {
    if (undo_stack_.empty()) return false;
    out = undo_stack_.back();
    undo_stack_.pop_back();
    redo_stack_.push_back(out);
    coalesce_broken_ = true;
    return true;
}

bool UndoStack::Redo(UndoEntry& out) {
    if (redo_stack_.empty()) return false;
    out = redo_stack_.back();
    redo_stack_.pop_back();
    undo_stack_.push_back(out);
    coalesce_broken_ = true;
    return true;
}

void UndoStack::BreakCoalesce() {
    coalesce_broken_ = true;
}

void UndoStack::Clear() {
    undo_stack_.clear();
    redo_stack_.clear();
    coalesce_broken_ = true;
}
