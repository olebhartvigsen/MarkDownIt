#include "editcontroller.h"

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
    // Naive: delete one byte back. Task 9 will use grapheme boundaries.
    buf_->Splice(at - 1, 1, "");
    sel_->Collapse({at - 1});
}

void EditController::DeleteForward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at >= buf_->Length()) return;
    buf_->Splice(at, 1, "");
    sel_->Collapse({at});
}
