#pragma once
#include "textbuffer.h"
#include "caret.h"
#include <string>

class EditController {
public:
    EditController(TextBuffer* buf, Selection* sel)
        : buf_(buf), sel_(sel) {}

    void InsertText(const std::string& utf8);
    void DeleteBackward();
    void DeleteForward();
    void DeleteSelection();

private:
    TextBuffer* buf_;
    Selection*  sel_;
};
