#pragma once
#include <string>
#include <cstdint>

// A byte range that changed between two parses of the same document, in the
// coordinates the incremental reparse needs: `start` is the same in both
// texts, `oldEnd` is the end of the changed region in the text before the
// edits and `newEnd` the end in the text after them. Several edits between
// two parses only know their union, so the range may be wider than any single
// change, never narrower. Declared here so a text buffer can hand it out
// without pulling in the parser's document types.
struct ReparseRange {
    uint32_t start = 0;
    uint32_t oldEnd = 0;
    uint32_t newEnd = 0;
    bool valid = false;
};

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

    // The union of every splice since the last ClearReparseRange(), expressed
    // against the text as it was at the last parse. A whole-document reload
    // leaves it invalid: the whole text changed and only a full parse can say
    // what it became. See ReparseRange above.
    const ReparseRange& reparseRange() const { return reparse_; }
    void ClearReparseRange() { reparse_ = ReparseRange{}; }

    // Byte offset of the line containing offset, and of its end
    // (one past the last byte of the line, not including the newline).
    uint32_t LineStart(uint32_t offset) const;
    uint32_t LineEnd(uint32_t offset) const;

    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

private:
    std::string text_;
    bool dirty_ = false;
    ReparseRange reparse_;
};
