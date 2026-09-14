#pragma once
#include "caret.h"
#include <string>
#include <vector>
#include <cstdint>

enum class EditType {
    Insert,
    Delete,
    ParagraphBreak,
    Other,
    PendingFormat,
};

struct UndoEntry {
    uint32_t offset;
    std::string removed;    // what was there before
    std::string inserted;   // what replaced it
    Selection selBefore;
    Selection selAfter;
    uint64_t timestamp;
    EditType type;
    bool hasPendingFormat = false;
    bool pendingBold = false;
    bool pendingItalic = false;
    bool pendingBoldSet = false;
    bool pendingItalicSet = false;
    bool afterPendingBold = false;
    bool afterPendingItalic = false;
    bool afterPendingBoldSet = false;
    bool afterPendingItalicSet = false;
};

class UndoStack {
public:
    void Push(const UndoEntry& entry);

    // Undo: returns the entry to reverse. Returns false if stack is empty.
    bool Undo(UndoEntry& out);

    // Redo: returns the entry to re-apply. Returns false if no redo.
    bool Redo(UndoEntry& out);

    // Break coalescing: called on caret movement or other non-text event.
    void BreakCoalesce();

    void Clear();

    bool CanUndo() const { return !undo_stack_.empty(); }
    bool CanRedo() const { return !redo_stack_.empty(); }

private:
    std::vector<UndoEntry> undo_stack_;
    std::vector<UndoEntry> redo_stack_;
    bool coalesce_broken_ = true;

    bool ShouldCoalesce(const UndoEntry& prev, const UndoEntry& next) const;
};
