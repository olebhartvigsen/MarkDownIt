#include "formatting.h"
#include <algorithm>
#include <cstdlib>

bool IsWrappedIn(const std::string& text, uint32_t start, uint32_t end,
                 const std::string& marker) {
    if (start + marker.size() * 2 > end) return false;
    // Check if text[start..start+m] == marker and text[end-m..end] == marker.
    for (size_t i = 0; i < marker.size(); i++) {
        if (start + i >= text.size() || text[start + i] != marker[i]) return false;
        if (end - marker.size() + i >= text.size() || text[end - marker.size() + i] != marker[i])
            return false;
    }
    return true;
}

void ToggleInlineMarker(TextBuffer* buf, Selection* sel, const std::string& marker) {
    if (sel->Empty()) {
        // Empty selection: insert marker pair and put caret between them.
        uint32_t at = sel->active.offset;
        buf->Splice(at, 0, marker + marker);
        // Caret goes to position after first marker.
        sel->Collapse({at + static_cast<uint32_t>(marker.size())});
        return;
    }

    uint32_t start = sel->Start();
    uint32_t end = start + sel->Length();
    const std::string& text = buf->Text();

    if (IsWrappedIn(text, start, end, marker)) {
        // Remove the markers.
        uint32_t mlen = static_cast<uint32_t>(marker.size());
        // Remove end marker first (so offsets don't shift).
        buf->Splice(end - mlen, mlen, "");
        buf->Splice(start, mlen, "");
        // Adjust selection: the inner text remains, markers removed.
        sel->anchor = {start};
        sel->active = {end - mlen * 2};
    } else {
        // Add the markers around the selection, but place them inside
        // any surrounding whitespace so md4c recognizes the emphasis.
        // E.g. " text " becomes " **text** " not "** text **".
        uint32_t contentStart = start;
        uint32_t contentEnd = end;
        while (contentStart < contentEnd &&
               (text[contentStart] == ' ' || text[contentStart] == '\t'))
            contentStart++;
        while (contentEnd > contentStart &&
               (text[contentEnd - 1] == ' ' || text[contentEnd - 1] == '\t' ||
                text[contentEnd - 1] == '\n' || text[contentEnd - 1] == '\r'))
            contentEnd--;

        // Insert end marker first (so start offset doesn't shift).
        buf->Splice(contentEnd, 0, marker);
        buf->Splice(contentStart, 0, marker);
        // Selection covers the same text plus the markers.
        sel->anchor = {contentStart};
        sel->active = {contentEnd + static_cast<uint32_t>(marker.size() * 2)};
    }
}

void InsertLink(TextBuffer* buf, Selection* sel, const std::string& url) {
    if (sel->Empty()) {
        // Insert [](url) and place caret between [ and ].
        uint32_t at = sel->active.offset;
        std::string link = "[](" + url + ")";
        buf->Splice(at, 0, link);
        sel->anchor = {at + 1};
        sel->active = {at + 1};
    } else {
        uint32_t start = sel->Start();
        uint32_t end = start + sel->Length();
        // Insert ](url) after end, then [ before start.
        buf->Splice(end, 0, "](" + url + ")");
        buf->Splice(start, 0, "[");
        sel->anchor = {start};
        sel->active = {end + 1 + static_cast<uint32_t>(url.size()) + 3};
    }
}


// --- Block formatting ---

static uint32_t LineStartOf(const std::string& text, uint32_t offset) {
    while (offset > 0 && text[offset - 1] != '\n') offset--;
    return offset;
}

static std::string GetLine(const std::string& text, uint32_t start) {
    uint32_t end = start;
    while (end < text.size() && text[end] != '\n') end++;
    return text.substr(start, end - start);
}

// Find the start of the paragraph (block of consecutive non-blank lines)
// containing offset. A blank line (only whitespace) ends a paragraph.
// A heading line (starting with #) is always its own paragraph — the
// following non-blank line starts a NEW paragraph, even without a blank
// line between them.
static uint32_t ParagraphStartOf(const std::string& text, uint32_t offset) {
    uint32_t lineStart = LineStartOf(text, offset);
    while (lineStart > 0) {
        // Move to the previous line.
        uint32_t prevEnd = lineStart - 1;  // skip the '\n'
        uint32_t prevStart = LineStartOf(text, prevEnd);
        std::string prevLine = GetLine(text, prevStart);
        // Check if the previous line is blank (only whitespace).
        bool blank = true;
        for (char c : prevLine) {
            if (c != ' ' && c != '\t' && c != '\r') { blank = false; break; }
        }
        if (blank) break;
        // A heading line is always its own paragraph. If the previous
        // line is a heading, the current line starts a new paragraph.
        {
            size_t i = 0;
            while (i < prevLine.size() && prevLine[i] == ' ') i++;
            if (i < prevLine.size() && prevLine[i] == '#') break;
        }
        lineStart = prevStart;
    }
    return lineStart;
}

// Find the end (one past last char, before the newline) of the paragraph
// containing offset. A heading line ends the paragraph on its own line.
static uint32_t ParagraphEndOf(const std::string& text, uint32_t offset) {
    uint32_t end = offset;
    // Advance to end of current line.
    while (end < text.size() && text[end] != '\n') end++;

    // If the current line is a heading, the paragraph is just this line.
    {
        uint32_t ls = LineStartOf(text, offset);
        size_t i = 0;
        while (i < text.size() - ls && text[ls + i] == ' ') i++;
        if (i < text.size() - ls && text[ls + i] == '#') return end;
    }

    while (end < text.size()) {
        // Peek at the next line.
        uint32_t nextStart = end + 1;  // skip this '\n'
        if (nextStart >= text.size()) break;
        std::string nextLine = GetLine(text, nextStart);
        bool blank = true;
        for (char c : nextLine) {
            if (c != ' ' && c != '\t' && c != '\r') { blank = false; break; }
        }
        if (blank) break;
        // A heading line starts a new paragraph.
        {
            size_t i = 0;
            while (i < nextLine.size() && nextLine[i] == ' ') i++;
            if (i < nextLine.size() && nextLine[i] == '#') break;
        }
        // Advance end to the end of the next line.
        end = nextStart;
        while (end < text.size() && text[end] != '\n') end++;
    }
    return end;
}

// Check if a line is blank (only whitespace or empty).
static bool IsBlankLine(const std::string& line) {
    for (char c : line) {
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

static std::string GetLinePrefix(const std::string& line) {
    // Return the existing prefix: #'s, - , * , 1. , > , or spaces.
    size_t i = 0;
    // Skip leading spaces (indentation).
    while (i < line.size() && line[i] == ' ') i++;
    // Check for heading prefix.
    if (i < line.size() && line[i] == '#') {
        while (i < line.size() && line[i] == '#') i++;
        if (i < line.size() && line[i] == ' ') i++;
        return line.substr(0, i);
    }
    // Check for list marker: - , * , + , or N.
    if (i < line.size() && (line[i] == '-' || line[i] == '*' || line[i] == '+')) {
        if (i + 1 < line.size() && line[i + 1] == ' ') return line.substr(0, i + 2);
    }
    if (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        size_t j = i;
        while (j < line.size() && line[j] >= '0' && line[j] <= '9') j++;
        if (j < line.size() && line[j] == '.' && j + 1 < line.size() && line[j + 1] == ' ')
            return line.substr(0, j + 2);
    }
    // Check for blockquote.
    if (i < line.size() && line[i] == '>') {
        if (i + 1 < line.size() && line[i + 1] == ' ') return line.substr(0, i + 2);
        return line.substr(0, i + 1);
    }
    return "";
}

void SetHeadingLevel(TextBuffer* buf, Selection* sel, int level) {
    const std::string& text = buf->Text();

    // A markdown heading is a single line: it must span the entire paragraph
    // block containing the cursor, not just the wrapping (source) line at the
    // cursor position. So we find the full paragraph boundaries and join all
    // wrapping lines into one before applying the heading prefix.
    //
    // Special case: if the first line of the paragraph already has a heading
    // prefix, the heading is just that one line (markdown headings are always
    // single-line). Continuation lines form a separate paragraph, so we only
    // operate on the heading line itself.
    uint32_t paraStart = ParagraphStartOf(text, sel->active.offset);

    std::string firstLine = GetLine(text, paraStart);
    std::string prefix = GetLinePrefix(firstLine);

    // Check if the first line is already a heading.
    bool firstIsHeading = false;
    {
        size_t i = 0;
        while (i < prefix.size() && prefix[i] == ' ') i++;
        firstIsHeading = (i < prefix.size() && prefix[i] == '#');
    }

    uint32_t paraEnd;
    if (firstIsHeading) {
        // Heading is single-line: paragraph is just the first line.
        paraEnd = paraStart + static_cast<uint32_t>(firstLine.size());
    } else {
        paraEnd = ParagraphEndOf(text, sel->active.offset);
    }

    // Extract the paragraph content, removing any existing block prefix from
    // the first line and collapsing soft line breaks into single spaces.
    std::string firstContent = firstLine.substr(prefix.size());

    // Gather remaining lines of the paragraph as plain content.
    std::string content = firstContent;
    {
        uint32_t cur = paraStart;
        // Advance past the first line.
        while (cur < text.size() && text[cur] != '\n') cur++;
        while (cur < paraEnd) {
            // cur is at '\n'; move to next line.
            cur++;  // skip '\n'
            std::string line = GetLine(text, cur);
            // Skip any indent on continuation lines.
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            std::string rest = line.substr(i);
            if (!rest.empty()) {
                if (!content.empty()) content += ' ';
                content += rest;
            }
            // Advance cur to end of this line.
            while (cur < text.size() && text[cur] != '\n') cur++;
        }
    }

    // Build the new text: heading prefix + content (or just content if level 0).
    std::string newPrefix;
    if (level > 0) {
        for (int i = 0; i < level; i++) newPrefix += "#";
        newPrefix += " ";
    }
    std::string replacement = newPrefix + content;

    // Splice: replace the whole paragraph block with the new single line.
    uint32_t oldLen = paraEnd - paraStart;
    buf->Splice(paraStart, oldLen, replacement);

    // Place the caret at the end of the new content (content start if the
    // line was empty). This mirrors the old behaviour of keeping the caret in
    // the same relative position within the content.
    uint32_t newOffset = paraStart + static_cast<uint32_t>(replacement.size());
    sel->Collapse({newOffset});
}

// Fix: sel_ should be *sel

void ToggleUnorderedList(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    std::string line = GetLine(text, lineStart);
    std::string prefix = GetLinePrefix(line);
    std::string content = line.substr(prefix.size());

    // Check if already an unordered list.
    bool isUL = false;
    size_t i = 0;
    while (i < prefix.size() && prefix[i] == ' ') i++;
    if (i < prefix.size() && (prefix[i] == '-' || prefix[i] == '*' || prefix[i] == '+') &&
        i + 1 < prefix.size() && prefix[i + 1] == ' ')
        isUL = true;

    if (isUL) {
        // Remove the list marker.
        buf->Splice(lineStart, static_cast<uint32_t>(prefix.size()), "");
        int32_t delta = -static_cast<int32_t>(prefix.size());
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) + delta))});
    } else {
        // Add "- " prefix.
        buf->Splice(lineStart, static_cast<uint32_t>(prefix.size()), "- ");
        int32_t delta = 2 - static_cast<int32_t>(prefix.size());
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) + delta))});
    }
}

void ToggleOrderedList(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    std::string line = GetLine(text, lineStart);
    std::string prefix = GetLinePrefix(line);
    std::string content = line.substr(prefix.size());

    // Check if already an ordered list.
    bool isOL = false;
    size_t i = 0;
    while (i < prefix.size() && prefix[i] == ' ') i++;
    if (i < prefix.size() && prefix[i] >= '0' && prefix[i] <= '9') {
        size_t j = i;
        while (j < prefix.size() && prefix[j] >= '0' && prefix[j] <= '9') j++;
        if (j < prefix.size() && prefix[j] == '.' && j + 1 < prefix.size() && prefix[j + 1] == ' ')
            isOL = true;
    }

    if (isOL) {
        buf->Splice(lineStart, static_cast<uint32_t>(prefix.size()), "");
        int32_t delta = -static_cast<int32_t>(prefix.size());
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) + delta))});
    } else {
        buf->Splice(lineStart, static_cast<uint32_t>(prefix.size()), "1. ");
        int32_t delta = 3 - static_cast<int32_t>(prefix.size());
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) + delta))});
    }
}

void ToggleBlockquote(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    std::string line = GetLine(text, lineStart);
    std::string prefix = GetLinePrefix(line);

    // Check if already a blockquote.
    bool isQuote = false;
    size_t i = 0;
    while (i < prefix.size() && prefix[i] == ' ') i++;
    if (i < prefix.size() && prefix[i] == '>')
        isQuote = true;

    if (isQuote) {
        buf->Splice(lineStart, static_cast<uint32_t>(prefix.size()), "");
        int32_t delta = -static_cast<int32_t>(prefix.size());
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) + delta))});
    } else {
        buf->Splice(lineStart, static_cast<uint32_t>(prefix.size()), "> ");
        int32_t delta = 2 - static_cast<int32_t>(prefix.size());
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) + delta))});
    }
}

void IndentLine(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    // Insert two spaces at the beginning of the line.
    buf->Splice(lineStart, 0, "  ");
    sel->Collapse({sel->active.offset + 2});
}

void OutdentLine(TextBuffer* buf, Selection* sel) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    // Remove up to 2 leading spaces.
    int removeCount = 0;
    for (int i = 0; i < 2 && lineStart + i < text.size() && text[lineStart + i] == ' '; i++)
        removeCount++;
    if (removeCount > 0) {
        buf->Splice(lineStart, removeCount, "");
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) - removeCount))});
    }
}

void RenumberOrderedList(TextBuffer* buf, uint32_t lineStart) {
    // Walk backwards to find the first list item in this run.
    // Then walk forward, renumbering each "N. " prefix.
    const std::string& text = buf->Text();

    // Find the start of the list run (walk back to first non-list line).
    uint32_t pos = lineStart;
    int itemNumber = 1;

    // Walk backwards to find the first ordered list item.
    while (pos > 0) {
        uint32_t prevLineStart = pos;
        if (prevLineStart > 0) prevLineStart--;
        while (prevLineStart > 0 && text[prevLineStart - 1] != '\n') prevLineStart--;
        std::string prevLine = GetLine(text, prevLineStart);
        std::string prevPrefix = GetLinePrefix(prevLine);
        // Check if it's an ordered list item.
        size_t i = 0;
        while (i < prevPrefix.size() && prevPrefix[i] == ' ') i++;
        if (i < prevPrefix.size() && prevPrefix[i] >= '0' && prevPrefix[i] <= '9') {
            pos = prevLineStart;
            itemNumber++;
        } else {
            break;
        }
    }

    // Walk forward, renumbering.
    uint32_t cur = pos;
    while (cur < text.size()) {
        uint32_t curLineEnd = cur;
        while (curLineEnd < text.size() && text[curLineEnd] != '\n') curLineEnd++;
        std::string curLine = text.substr(cur, curLineEnd - cur);
        std::string curPrefix = GetLinePrefix(curLine);

        // Check if it's an ordered list item.
        size_t i = 0;
        while (i < curPrefix.size() && curPrefix[i] == ' ') i++;
        if (i < curPrefix.size() && curPrefix[i] >= '0' && curPrefix[i] <= '9') {
            // Find the number in the prefix.
            size_t numStart = i;
            size_t numEnd = i;
            while (numEnd < curPrefix.size() && curPrefix[numEnd] >= '0' && curPrefix[numEnd] <= '9') numEnd++;
            // Replace the number.
            std::string newNum = std::to_string(itemNumber);
            buf->Splice(cur + numStart, static_cast<uint32_t>(numEnd - numStart), newNum);
            // Adjust cur for the splice delta.
            int32_t delta = static_cast<int32_t>(newNum.size()) - static_cast<int32_t>(numEnd - numStart);
            curLineEnd += delta;
            itemNumber++;
        } else {
            break;
        }
        cur = curLineEnd + 1; // skip \n
    }
}
