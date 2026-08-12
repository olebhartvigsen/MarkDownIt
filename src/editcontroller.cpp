#include "editcontroller.h"
#include "dom.h"

// Naive grapheme boundary: step back over one UTF-8 codepoint.
// ICU's ubrk_open would be more correct for combining marks, but this
// handles BMP characters and surrogate pairs correctly.
uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset) {
    if (offset == 0 || offset > s.size()) return 0;
    uint32_t i = offset;
    // Skip continuation bytes.
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80)
        i--;
    // Now s[i-1] is the start byte of the UTF-8 sequence.
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

void EditController::InsertText(const std::string& utf8) {
    if (!sel_->Empty()) DeleteSelection();
    uint32_t at = sel_->active.offset;
    uint32_t end = buf_->Splice(at, 0, utf8);
    sel_->Collapse({end});
}

void EditController::DeleteSelection() {
    if (sel_->Empty()) return;
    uint32_t start = sel_->Start();
    uint32_t len = sel_->Length();
    buf_->Splice(start, len, "");
    sel_->Collapse({start});
}

void EditController::DeleteBackward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at == 0) return;
    uint32_t prev = PrevGraphemeBoundary(buf_->Text(), at);
    buf_->Splice(prev, at - prev, "");
    sel_->Collapse({prev});
}

void EditController::DeleteForward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at >= buf_->Length()) return;
    uint32_t next = NextGraphemeBoundary(buf_->Text(), at);
    buf_->Splice(at, next - at, "");
    sel_->Collapse({at});
}

void EditController::InsertParagraphBreak(const Document& doc) {
    // Handle selection: delete it first.
    if (!sel_->Empty()) DeleteSelection();

    uint32_t at = sel_->active.offset;

    // Find the context node by scanning the document for the node
    // whose source range contains the caret offset.
    BlockKind ctx = BlockKind::Paragraph;
    bool inList = false;
    bool inCode = false;
    bool ordered = false;
    bool emptyListItem = false;
    uint32_t listMarkerLen = 0;

    for (const auto& n : doc.nodes) {
        if (at >= n.srcOffset && at <= n.srcOffset + n.srcLength) {
            ctx = n.block;
            if (n.block == BlockKind::List) {
                inList = true;
                ordered = n.ordered;
                // Check if this is an empty list item (no content text yet).
                // Count non-space children.
                bool hasContent = false;
                for (const auto& ib : n.children) {
                    if (!ib.text.empty()) { hasContent = true; break; }
                }
                emptyListItem = !hasContent;
                listMarkerLen = ordered ? 3 : 2; // "1. " or "- "
            }
            if (n.block == BlockKind::CodeBlock) {
                inCode = true;
            }
            break;
        }
    }

    if (inCode) {
        // Inside a code block: insert a single newline.
        buf_->Splice(at, 0, "\n");
        sel_->Collapse({at + 1});
        return;
    }

    if (inList) {
        if (emptyListItem) {
            // On an empty list item: remove the marker, ending the list.
            // Delete backwards to the start of the line, replacing the
            // marker ("- " or "1. ") with nothing.
            uint32_t lineStart = buf_->LineStart(at);
            uint32_t toDelete = at - lineStart;
            if (toDelete > 0) {
                buf_->Splice(lineStart, toDelete, "");
                sel_->Collapse({lineStart});
            }
            return;
        }
        // At the end of a list item: insert newline + marker.
        std::string marker = ordered ? "\n1. " : "\n- ";
        buf_->Splice(at, 0, marker);
        sel_->Collapse({at + static_cast<uint32_t>(marker.size())});
        return;
    }

    // Default: insert paragraph break (two newlines).
    buf_->Splice(at, 0, "\n\n");
    sel_->Collapse({at + 2});
}
