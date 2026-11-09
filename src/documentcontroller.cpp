// The document controller bodies (plan 4.1, extraction batch 4).
//
// The command bodies below are the movers from the two ports, kept
// byte for byte in what they do: the splice and the undo entry come
// from the shared formatting functions exactly as before, and only
// the shell around them moved. The undo stamping is the one
// deliberate change: both ports stamp through the platform clock
// now, where Windows stamped GetTickCount64 and the mac port stamped
// zero (platform-parity, divergence 3).

#include "documentcontroller.h"

#include <algorithm>

#include "formatting.h"
#include "platformservices.h"

DocumentController::DocumentController()
    : editor_(&buffer_, &sel_) {
    // The edit controller and the undo stack work as one unit: every
    // typed or spliced change lands in the stack through the editor.
    editor_.SetUndoStack(&undo_);
}

// Family 2: headings and lists. One body now, where the Windows
// wrappers (app.cpp) and the mac twins (main_mac.mm) each carried a
// copy. The gate first: a mutating command is a no-op in read mode,
// never a mode switch. The undo entry comes from the formatting
// function itself, the coalescing break keeps the command its own
// undo step, and the port re-parses and redraws through the handler.
void DocumentController::SetHeading(int level) {
    if (!editing_) return;
    SetHeadingLevel(&buffer_, &sel_, level, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::ToggleBullets() {
    if (!editing_) return;
    ToggleUnorderedList(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::ToggleNumbering() {
    if (!editing_) return;
    ToggleOrderedList(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::ToggleQuote() {
    if (!editing_) return;
    ToggleBlockquote(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::Indent() {
    if (!editing_) return;
    IndentLine(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::Outdent() {
    if (!editing_) return;
    OutdentLine(&buffer_, &sel_, &undo_);
    editor_.BreakUndoCoalesce();
    NotifyDocumentChanged();
}

void DocumentController::SpliceWithUndo(uint32_t offset, uint32_t length,
                                        const std::string& replacement) {
    std::string removed = buffer_.Text().substr(
        std::min(offset, static_cast<uint32_t>(buffer_.Text().size())),
        std::min(length, static_cast<uint32_t>(
            buffer_.Text().size() - std::min(offset,
                static_cast<uint32_t>(buffer_.Text().size())))));
    Selection selBefore = sel_;
    buffer_.Splice(offset, length, replacement);
    Selection selAfter = sel_;
    UndoEntry entry{};
    entry.offset = offset;
    entry.removed = removed;
    entry.inserted = replacement;
    entry.selBefore = selBefore;
    entry.selAfter = selAfter;
    entry.timestamp = platform::GetMonotonicMs();
    entry.type = EditType::Insert;
    undo_.Push(entry);
}

void DocumentController::RecordPendingFormatUndo(
    const Selection& sel, bool beforeBold, bool beforeItalic,
    bool beforeBoldSet, bool beforeItalicSet, bool afterBold,
    bool afterItalic, bool afterBoldSet, bool afterItalicSet) {
    UndoEntry entry{};
    entry.offset = sel.active.offset;
    entry.selBefore = sel;
    entry.selAfter = sel;
    entry.timestamp = platform::GetMonotonicMs();
    entry.type = EditType::PendingFormat;
    entry.hasPendingFormat = true;
    entry.pendingBold = beforeBold;
    entry.pendingItalic = beforeItalic;
    entry.pendingBoldSet = beforeBoldSet;
    entry.pendingItalicSet = beforeItalicSet;
    entry.afterPendingBold = afterBold;
    entry.afterPendingItalic = afterItalic;
    entry.afterPendingBoldSet = afterBoldSet;
    entry.afterPendingItalicSet = afterItalicSet;
    undo_.Push(entry);
    // The after state rides in the same entry because the public
    // stack API cannot append a redo side: the current state is
    // deterministic from the before state and this toggle, so
    // ApplyUndo/ApplyRedo recomputes the opposite state for this
    // entry.
}
