#include "inputfilter.h"

static bool IsAtLineStart(const std::string& text, uint32_t offset) {
    if (offset == 0) return true;
    // Walk backwards past spaces only (indented line start is still line start).
    uint32_t i = offset;
    while (i > 0 && text[i - 1] == ' ') i--;
    return i == 0 || text[i - 1] == 0x0A;  // start of line or start of file
}

static bool NeedsEscape(char c, const std::string& text, uint32_t offset) {
    switch (c) {
        case '*':
        case '_':
            // These create emphasis anywhere. Escape when followed by
            // non-space (would start a span) or preceded by non-space.
            return true;
        case '`':
            // Backtick creates code spans. Escape always.
            return true;
        case '#':
            // Hash creates headings only at line start.
            return IsAtLineStart(text, offset);
        case '>':
            // Greater-than creates blockquotes only at line start.
            return IsAtLineStart(text, offset);
        case '[':
        case ']':
        case '(':
        case ')':
        case '!':
            // These create links and images. Escape when paired.
            // Conservative: escape [ and ! always, others only after [.
            if (c == '[' || c == '!') return true;
            return false;
        case '\\':
            // Backslash is the escape character itself. Escape it.
            return true;
        case '-':
            // Dash creates bullet lists at line start and ---
            // horizontal rules. Also used for setext headings (under h1)
            return IsAtLineStart(text, offset);
        case '+':
            // Plus creates bullet lists at line start.
            return IsAtLineStart(text, offset);
        case '~':
            // Tilde creates strikethrough and horizontal rules.
            return true;
        case '|':
            // Pipe creates tables. Escape always.
            return true;
        default:
            return false;
    }
}

std::string EscapeForInsert(const TextBuffer& buf, uint32_t offset,
                            const std::string& typed) {
    const std::string& text = buf.Text();
    std::string result;
    result.reserve(typed.size() * 2);

    for (size_t i = 0; i < typed.size(); i++) {
        char c = typed[i];
        if (NeedsEscape(c, text, offset + i)) {
            result += '\\';
        }
        result += c;
    }
    return result;
}

std::string EscapeForPaste(const TextBuffer& buf, uint32_t offset,
                           const std::string& text) {
    // Same logic as EscapeForInsert, but for a multi-character block.
    return EscapeForInsert(buf, offset, text);
}
