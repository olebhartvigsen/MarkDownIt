#pragma once
#include <string>
#include <cstdint>

// Owns the markdown source as UTF-8. All edits go through Splice.
class TextBuffer {
public:
    void SetText(std::string utf8);
    const std::string& Text() const { return text_; }
    size_t Length() const { return text_.size(); }

    // Replace [offset, offset+length) with replacement.
    // Returns the offset just past the inserted text.
    uint32_t Splice(uint32_t offset, uint32_t length,
                    const std::string& replacement);

    // Byte offset of the line containing offset, and of its end
    // (one past the last byte of the line, not including the newline).
    uint32_t LineStart(uint32_t offset) const;
    uint32_t LineEnd(uint32_t offset) const;

    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

private:
    std::string text_;
    bool dirty_ = false;
};
