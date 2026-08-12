#include "autoformat.h"
#include "formatting.h"
#include <cstring>

static uint32_t LineStartOf(const std::string& text, uint32_t offset) {
    while (offset > 0 && text[offset - 1] != '\n') offset--;
    return offset;
}

static std::string GetLine(const std::string& text, uint32_t start) {
    uint32_t end = start;
    while (end < text.size() && text[end] != '\n') end++;
    return text.substr(start, end - start);
}

// Check if the text from lineStart to caret matches a heading pattern.
// Returns the heading level (1-6) if it matches "#", "## " etc.
// Returns 0 if no match.
static int MatchHeading(const std::string& text, uint32_t lineStart, uint32_t caret) {
    int level = 0;
    uint32_t i = lineStart;
    while (i < caret && i < text.size() && text[i] == '#' && level < 6) {
        level++;
        i++;
    }
    // Must have exactly level #'s followed by a space at the caret.
    if (level >= 1 && level <= 6 && i == caret - 1 && i < text.size() && text[i] == ' ')
        return level;
    return 0;
}

bool AutoformatHeading(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t caret = sel->active.offset;
    if (caret == 0) return false;
    uint32_t lineStart = LineStartOf(text, caret);

    // The space was just typed at `caret - 1`. Check if the text
    // from lineStart to caret-1 is all #'s.
    int level = 0;
    for (uint32_t i = lineStart; i < caret - 1 && i < text.size(); i++) {
        if (text[i] != '#') return false;
        level++;
    }
    if (level < 1 || level > 6) return false;
    if (text[caret - 1] != ' ') return false;

    // Check that the line before the space is ONLY #'s (nothing else).
    if (lineStart + level != caret - 1) return false;

    // The heading is already in the buffer as "## " etc.
    // No transformation needed: md4c will parse it as a heading.
    // But we should remove any existing paragraph wrapping.
    // Actually, the markdown IS the heading format. The "autoformat"
    // is that the renderer will now show it as a heading.
    // The real value is that the space triggers reparse.
    return true;  // signal that it's a heading, but no buffer change needed
}

bool AutoformatBulletList(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t caret = sel->active.offset;
    if (caret < 2) return false;
    uint32_t lineStart = LineStartOf(text, caret);

    // Check: lineStart has "-" or "*" followed by space at caret-1.
    if (lineStart + 1 != caret - 1) return false;  // only one char before space
    char marker = text[lineStart];
    if (marker != '-' && marker != '*') return false;
    if (text[caret - 1] != ' ') return false;

    // Check the line is empty before the marker (line start or only whitespace).
    // The marker must be at the very start of the line (after possible indentation).
    // For now, only trigger if marker is at lineStart (no indentation).
    return true;  // already "- " in buffer, md4c will parse as list
}

bool AutoformatOrderedList(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t caret = sel->active.offset;
    if (caret < 3) return false;
    uint32_t lineStart = LineStartOf(text, caret);

    // Check: "N." followed by space at caret-1.
    // The text from lineStart to caret-2 must be digits + "."
    uint32_t i = lineStart;
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
    while (i < caret - 2 && i < text.size() && text[i] >= '0' && text[i] <= '9') i++;
    if (i >= text.size() || text[i] != '.') return false;
    i++;
    if (i != caret - 1) return false;
    if (text[caret - 1] != ' ') return false;

    return true;
}

bool AutoformatBlockquote(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t caret = sel->active.offset;
    if (caret < 2) return false;
    uint32_t lineStart = LineStartOf(text, caret);

    if (lineStart + 1 != caret - 1) return false;
    if (text[lineStart] != '>') return false;
    if (text[caret - 1] != ' ') return false;

    return true;
}

bool AutoformatBold(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t caret = sel->active.offset;
    if (caret < 4) return false;  // need at least "**x*"

    // Check if the last two typed chars are "**" and there's an
    // opening "**" earlier on the same line.
    if (caret >= 2 && text[caret - 1] == '*' && text[caret - 2] == '*') {
        // Find opening "**" before this.
        uint32_t lineStart = LineStartOf(text, caret - 2);
        for (uint32_t i = caret - 3; i >= lineStart && i < caret; i--) {
            if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*' && i + 1 < caret - 2) {
                // Found opening **. The text between is the content.
                // The markdown is already valid: **content**
                // Just signal success.
                return true;
            }
            if (i == 0) break;
        }
    }
    return false;
}

bool AutoformatInlineCode(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t caret = sel->active.offset;
    if (caret < 3) return false;  // need at least "`x`"

    // Check if the last typed char is "`" and there's an opening "`" earlier.
    if (text[caret - 1] != '`') return false;

    uint32_t lineStart = LineStartOf(text, caret - 1);
    for (uint32_t i = caret - 2; i >= lineStart && i < caret; i--) {
        if (text[i] == '`' && i < caret - 1) {
            return true;
        }
        if (i == 0) break;
    }
    return false;
}

bool CheckAutoformat(TextBuffer* buf, Selection* sel, char trigger) {
    switch (trigger) {
        case ' ':
            if (AutoformatHeading(buf, sel)) return true;
            if (AutoformatBulletList(buf, sel)) return true;
            if (AutoformatOrderedList(buf, sel)) return true;
            if (AutoformatBlockquote(buf, sel)) return true;
            break;
        case '*':
            if (AutoformatBold(buf, sel)) return true;
            break;
        case '`':
            if (AutoformatInlineCode(buf, sel)) return true;
            break;
    }
    return false;
}
