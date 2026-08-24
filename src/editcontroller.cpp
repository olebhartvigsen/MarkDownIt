#include "editcontroller.h"
#include "dom.h"
#include <chrono>

static uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset) {
    if (offset == 0 || offset > s.size()) return 0;
    uint32_t i = offset;
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80)
        i--;
    if (i > 0) i--;
    return i;
}

uint32_t NextGraphemeBoundary(const std::string& s, uint32_t offset) {
    if (offset >= s.size()) return static_cast<uint32_t>(s.size());
    uint32_t i = offset;
    unsigned char b = static_cast<unsigned char>(s[i]);
    if (b < 0x80) i += 1;
    else if ((b & 0xE0) == 0xC0) i += 2;
    else if ((b & 0xF0) == 0xE0) i += 3;
    else if ((b & 0xF8) == 0xF0) i += 4;
    else i += 1;
    if (i > s.size()) i = static_cast<uint32_t>(s.size());
    return i;
}

void EditController::RecordAndApply(uint32_t offset, uint32_t length,
                                    const std::string& replacement,
                                    EditType type) {
    // Record what we are about to remove.
    std::string removed = buf_->Text().substr(offset, length);

    Selection selBefore = *sel_;
    uint32_t end = buf_->Splice(offset, length, replacement);
    sel_->Collapse({end});
    Selection selAfter = *sel_;

    if (undo_) {
        UndoEntry entry{};
        entry.offset = offset;
        entry.removed = removed;
        entry.inserted = replacement;
        entry.selBefore = selBefore;
        entry.selAfter = selAfter;
        entry.timestamp = NowMs();
        entry.type = type;
        undo_->Push(entry);
    }
}

void EditController::InsertText(const std::string& utf8) {
    if (!sel_->Empty()) DeleteSelection();
    uint32_t at = sel_->active.offset;
    RecordAndApply(at, 0, utf8, EditType::Insert);
}

void EditController::DeleteSelection() {
    if (sel_->Empty()) return;
    uint32_t start = sel_->Start();
    uint32_t len = sel_->Length();
    RecordAndApply(start, len, "", EditType::Delete);
}

void EditController::DeleteBackward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at == 0) return;
    uint32_t prev = PrevGraphemeBoundary(buf_->Text(), at);
    RecordAndApply(prev, at - prev, "", EditType::Delete);
}

void EditController::DeleteForward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at >= buf_->Length()) return;
    uint32_t next = NextGraphemeBoundary(buf_->Text(), at);
    RecordAndApply(at, next - at, "", EditType::Delete);
}

void EditController::InsertParagraphBreak(const Document& doc) {
    if (!sel_->Empty()) DeleteSelection();

    uint32_t at = sel_->active.offset;

    BlockKind ctx = BlockKind::Paragraph;
    bool inList = false;
    bool inCode = false;
    bool ordered = false;
    bool emptyListItem = false;

    for (const auto& n : doc.nodes) {
        if (at >= n.srcOffset && at <= n.srcOffset + n.srcLength) {
            ctx = n.block;
            if (n.block == BlockKind::List) {
                inList = true;
                ordered = n.ordered;
                bool hasContent = false;
                for (const auto& ib : n.children) {
                    if (!ib.text.empty()) { hasContent = true; break; }
                }
                emptyListItem = !hasContent;
            }
            if (n.block == BlockKind::CodeBlock) {
                inCode = true;
            }
            break;
        }
    }

    if (inCode) {
        RecordAndApply(at, 0, "\n", EditType::ParagraphBreak);
        return;
    }

    if (inList) {
        if (emptyListItem) {
            uint32_t lineStart = buf_->LineStart(at);
            uint32_t toDelete = at - lineStart;
            if (toDelete > 0) {
                RecordAndApply(lineStart, toDelete, "", EditType::ParagraphBreak);
            }
            return;
        }
        std::string marker = ordered ? "\n1. " : "\n- ";
        RecordAndApply(at, 0, marker, EditType::ParagraphBreak);
        return;
    }

    // In a table cell, inserting "\n\n" would break the table syntax.
    // Insert a soft line break instead (Markdown "  \n" = two spaces
    // + newline), which creates a visual <br> in HTML rendering
    // without breaking the table row.
    if (ctx == BlockKind::Table) {
        RecordAndApply(at, 0, "  \n", EditType::ParagraphBreak);
        return;
    }

    RecordAndApply(at, 0, "\n\n", EditType::ParagraphBreak);
}

void EditController::Undo() {
    if (!undo_) return;
    UndoEntry entry;
    if (!undo_->Undo(entry)) return;
    // Apply the inverse: remove what was inserted, put back what was removed.
    buf_->Splice(entry.offset,
                 static_cast<uint32_t>(entry.inserted.size()),
                 entry.removed);
    *sel_ = entry.selBefore;
}

void EditController::Redo() {
    if (!undo_) return;
    UndoEntry entry;
    if (!undo_->Redo(entry)) return;
    // Re-apply: remove what was there, put back what was inserted.
    buf_->Splice(entry.offset,
                 static_cast<uint32_t>(entry.removed.size()),
                 entry.inserted);
    *sel_ = entry.selAfter;
}
