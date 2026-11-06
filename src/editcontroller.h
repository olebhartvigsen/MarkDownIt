#pragma once
#include "textbuffer.h"
#include "caret.h"
#include "dom.h"
#include "undostack.h"
#include <string>

class EditController {
public:
    EditController() : buf_(nullptr), sel_(nullptr), undo_(nullptr) {}
    EditController(TextBuffer* buf, Selection* sel)
        : buf_(buf), sel_(sel), undo_(nullptr) {}

    void SetUndoStack(UndoStack* u) { undo_ = u; }

    void InsertText(const std::string& utf8);
    void ReplaceTextRange(uint32_t offset, uint32_t length,
                          const std::string& replacement,
                          EditType type = EditType::Other,
                          const Selection* undoSelectionBefore = nullptr);
    // Return true when the source buffer changed. Passing the parsed document
    // lets paragraph-boundary deletion remove one complete paragraph separator.
    bool DeleteBackward(const Document* doc = nullptr);
    // Cheap source-only hint for refreshing a debounced document before
    // structural Backspace. The parsed model still decides list semantics.
    bool MayRemoveEmptyBullet() const;
    bool DeleteForward(const Document* doc = nullptr);
    void DeleteWordBackward();
    void DeleteWordForward();
    void DeleteSelection();

    // Insert a paragraph break, context-aware.
    bool InsertParagraphBreak(const Document& doc);

    // Insert one Markdown soft line break without creating a new paragraph.
    bool InsertSoftBreak(const Document& doc);

    // Table editing boundaries: each returns true when the buffer changed.
    // Backspace at the start of a cell's content: extend left into hidden
    // cell padding only; the cell boundary (pipe) and the neighboring cell
    // are never consumed. Returns true when padding was removed.
    bool DeleteBackwardInCell(const Document& doc);
    // Delete at the end of a cell's content: consume that padding only.
    // Returns true when padding was removed.
    bool DeleteForwardInCell(const Document& doc);

    // Undo/redo: apply inverse or re-apply an entry.
    bool Undo(UndoEntry* undone = nullptr);
    bool Redo(UndoEntry* redone = nullptr);

    // Break undo coalescing (call on caret movement).
    void BreakUndoCoalesce() { if (undo_) undo_->BreakCoalesce(); }

    // IME composition (marked text). A temporary composition is text
    // input, so it creates no undo entry while it is being written; the
    // confirmation is exactly one entry, and a cancelled composition
    // puts the text and the selection back the way they were (text
    // editing spec, "Almindeligt tekstinput": a temporary IME
    // composition is not an undo step until the text is confirmed).
    void BeginComposition();
    void SetCompositionText(const std::string& utf8);
    void CommitComposition();
    void CancelComposition();
    bool HasComposition() const { return comp_.active; }

    // The byte range the marked text covers, so a caller can report it
    // to the input method that asked for it.
    void CompositionRange(uint32_t* start, uint32_t* length) const {
        *start = comp_.start;
        *length = comp_.length;
    }

private:
    TextBuffer* buf_;
    Selection*  sel_;
    UndoStack*  undo_;

    // The state of one composition: where it sits, how much of the
    // buffer it owns right now, the text it replaced, and the selection
    // to put back when it is cancelled.
    struct Composition {
        bool active = false;
        uint32_t start = 0;
        uint32_t length = 0;
        std::string replaced;
        Selection selBefore{};
    } comp_;

    void RecordAndApply(uint32_t offset, uint32_t length,
                        const std::string& replacement,
                        EditType type,
                        const Selection* undoSelectionBefore = nullptr);
};

// Find the previous grapheme boundary before the given offset.
// Returns 0 if already at the start.
uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset);

// Find the next grapheme boundary after the given offset.
// Returns s.size() if already at the end.
uint32_t NextGraphemeBoundary(const std::string& s, uint32_t offset);

// Return true only at a valid UTF-8 grapheme-cluster boundary.
bool IsGraphemeBoundary(const std::string& s, uint32_t offset);
