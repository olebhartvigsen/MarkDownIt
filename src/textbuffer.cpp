#include "textbuffer.h"

void TextBuffer::SetText(std::string utf8) {
    text_ = std::move(utf8);
    dirty_ = true;
}

uint32_t TextBuffer::Splice(uint32_t offset, uint32_t length,
                            const std::string& replacement) {
    // Clamp offset to valid range.
    if (offset > text_.size()) offset = static_cast<uint32_t>(text_.size());
    // Clamp length so we do not read past the end.
    if (offset + length > text_.size())
        length = static_cast<uint32_t>(text_.size()) - offset;

    text_.replace(offset, length, replacement);
    dirty_ = true;
    return offset + static_cast<uint32_t>(replacement.size());
}

uint32_t TextBuffer::LineStart(uint32_t offset) const {
    if (offset >= text_.size()) offset = static_cast<uint32_t>(text_.size());
    while (offset > 0 && text_[offset - 1] != '\n')
        offset--;
    return offset;
}

uint32_t TextBuffer::LineEnd(uint32_t offset) const {
    if (offset >= text_.size()) return static_cast<uint32_t>(text_.size());
    while (offset < text_.size() && text_[offset] != '\n')
        offset++;
    return offset;
}
