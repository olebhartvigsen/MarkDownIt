#include "textbuffer.h"

void TextBuffer::SetText(std::string utf8) {
    text_ = std::move(utf8);
    dirty_ = true;
    // The whole text changed: only a full parse can say what it became.
    reparse_ = ReparseRange{};
}

uint32_t TextBuffer::Splice(uint32_t offset, uint32_t length,
                            const std::string& replacement) {
    // Clamp offset to valid range.
    if (offset > text_.size()) offset = static_cast<uint32_t>(text_.size());
    // Clamp length so we do not read past the end.
    if (offset + length > text_.size())
        length = static_cast<uint32_t>(text_.size()) - offset;

    // Widen the pending range to cover this edit. The offset is already in
    // the coordinates of the text as it was at the last parse, and every
    // earlier edit in the same run moved the text after it, so the widest
    // range is the union of the old and new ends.
    const uint32_t oldEnd = offset + length;
    const uint32_t newEnd = offset + static_cast<uint32_t>(replacement.size());
    if (reparse_.valid) {
        if (offset < reparse_.start) reparse_.start = offset;
        if (oldEnd > reparse_.oldEnd) reparse_.oldEnd = oldEnd;
        if (newEnd > reparse_.newEnd) reparse_.newEnd = newEnd;
    } else {
        reparse_.start = offset;
        reparse_.oldEnd = oldEnd;
        reparse_.newEnd = newEnd;
        reparse_.valid = true;
    }

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
