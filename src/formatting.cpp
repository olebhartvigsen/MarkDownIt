#include "formatting.h"
#include "undostack.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

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

// Helper: record a single contiguous splice as an undo entry.
static void RecordUndo(UndoStack* undo, uint32_t offset,
                       const std::string& removed, const std::string& inserted,
                       const Selection& selBefore, const Selection& selAfter) {
    if (!undo) return;
    UndoEntry entry{};
    entry.offset = offset;
    entry.removed = removed;
    entry.inserted = inserted;
    entry.selBefore = selBefore;
    entry.selAfter = selAfter;
    entry.type = EditType::Other;
    undo->Push(entry);
}

void ToggleInlineMarker(TextBuffer* buf, Selection* sel, const std::string& marker,
                       UndoStack* undo) {
    if (sel->Empty()) {
        // Empty selection: insert marker pair and put caret between them.
        uint32_t at = sel->active.offset;
        std::string inserted = marker + marker;
        Selection selBefore = *sel;
        buf->Splice(at, 0, inserted);
        sel->Collapse({at + static_cast<uint32_t>(marker.size())});
        RecordUndo(undo, at, "", inserted, selBefore, *sel);
        return;
    }

    uint32_t start = sel->Start();
    uint32_t end = start + sel->Length();
    const std::string& text = buf->Text();
    Selection selBefore = *sel;
    uint32_t mlen = static_cast<uint32_t>(marker.size());

    // First try: selection already includes the markers.
    if (IsWrappedIn(text, start, end, marker)) {
        // Remove the markers: single splice replacing "**content**" with "content".
        std::string removed = text.substr(start, end - start);  // "**content**"
        std::string kept = removed.substr(mlen, removed.size() - mlen * 2);  // "content"
        buf->Splice(start, end - start, kept);
        sel->anchor = {start};
        sel->active = {start + static_cast<uint32_t>(kept.size())};
        RecordUndo(undo, start, removed, kept, selBefore, *sel);
        return;
    }

    // Second try: the selection is the inner content (markers are just
    // outside). This happens when the user selects rendered bold text —
    // the selection offsets point to the content, not the ** markers.
    // Expand outward to include adjacent markers, then remove them.
    if (start >= mlen && end + mlen <= text.size()) {
        bool hasLeftMarker = true, hasRightMarker = true;
        for (size_t i = 0; i < mlen; i++) {
            if (text[start - mlen + i] != marker[i]) { hasLeftMarker = false; break; }
        }
        for (size_t i = 0; i < mlen; i++) {
            if (text[end + i] != marker[i]) { hasRightMarker = false; break; }
        }
        if (hasLeftMarker && hasRightMarker) {
            // Expand selection to include the markers, then remove them.
            uint32_t fullStart = start - mlen;
            uint32_t fullEnd = end + mlen;
            std::string removed = text.substr(fullStart, fullEnd - fullStart);
            std::string kept = text.substr(start, end - start);
            buf->Splice(fullStart, fullEnd - fullStart, kept);
            sel->anchor = {fullStart};
            sel->active = {fullStart + static_cast<uint32_t>(kept.size())};
            RecordUndo(undo, fullStart, removed, kept, selBefore, *sel);
            return;
        }
    }

    // Otherwise: add the markers around the selection, but place them inside
    // any surrounding whitespace so md4c recognizes the emphasis.
    {
        uint32_t contentStart = start;
        uint32_t contentEnd = end;
        while (contentStart < contentEnd &&
               (text[contentStart] == ' ' || text[contentStart] == '\t'))
            contentStart++;
        while (contentEnd > contentStart &&
               (text[contentEnd - 1] == ' ' || text[contentEnd - 1] == '\t' ||
                text[contentEnd - 1] == '\n' || text[contentEnd - 1] == '\r'))
            contentEnd--;

        // Single contiguous splice: replace "content" with "**content**".
        std::string removed = text.substr(contentStart, contentEnd - contentStart);
        std::string inserted = marker + removed + marker;
        buf->Splice(contentStart, contentEnd - contentStart, inserted);
        sel->anchor = {contentStart};
        sel->active = {contentStart + static_cast<uint32_t>(inserted.size())};
        RecordUndo(undo, contentStart, removed, inserted, selBefore, *sel);
    }
}

void InsertLink(TextBuffer* buf, Selection* sel, const std::string& url,
                UndoStack* undo) {
    Selection selBefore = *sel;
    if (sel->Empty()) {
        // Insert [](url) and place caret between [ and ].
        uint32_t at = sel->active.offset;
        std::string link = "[](" + url + ")";
        buf->Splice(at, 0, link);
        sel->anchor = {at + 1};
        sel->active = {at + 1};
        RecordUndo(undo, at, "", link, selBefore, *sel);
    } else {
        uint32_t start = sel->Start();
        uint32_t end = start + sel->Length();
        // Single contiguous splice: replace "text" with "[text](url)".
        const std::string& text = buf->Text();
        std::string removed = text.substr(start, end - start);
        std::string inserted = "[" + removed + "](" + url + ")";
        buf->Splice(start, end - start, inserted);
        sel->anchor = {start};
        sel->active = {start + static_cast<uint32_t>(inserted.size())};
        RecordUndo(undo, start, removed, inserted, selBefore, *sel);
    }
}


// --- Code block toggle ---

// Forward declaration — LineStartOf is defined later in this file.
static uint32_t LineStartOf(const std::string& text, uint32_t offset);

// Check if a line starting at lineStart begins with the given prefix.
static bool LineHasPrefix(const std::string& text, uint32_t lineStart,
                          const std::string& prefix) {
    if (lineStart + prefix.size() > text.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (text[lineStart + i] != prefix[i]) return false;
    }
    return true;
}

void ToggleCodeBlock(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();
    uint32_t selStart = sel->Start();
    uint32_t selEnd = selStart + sel->Length();
    Selection selBefore = *sel;

    // Find the start of the first line and the end (including newline) of
    // the last line in the selection.
    uint32_t firstLineStart = LineStartOf(text, selStart);
    uint32_t lastLineEnd = selEnd;
    while (lastLineEnd < text.size() && text[lastLineEnd] != '\n') lastLineEnd++;
    // Include the trailing newline in the block content.
    if (lastLineEnd < text.size() && text[lastLineEnd] == '\n') lastLineEnd++;

    // Check if already inside a code block: look for a line with ```
    // just before the first line, and a line with ``` just after the
    // last line.
    const std::string fence = "```";

    // Look backwards from firstLineStart for an opening ``` fence.
    uint32_t fenceLineStart = 0;
    bool foundOpen = false;
    if (firstLineStart >= fence.size() + 1) {
        // The line before firstLineStart ends at firstLineStart - 1 (the \n).
        // Walk back to find the start of that line.
        uint32_t prevLineStart = firstLineStart - 1;
        if (prevLineStart > 0) prevLineStart--;
        while (prevLineStart > 0 && text[prevLineStart - 1] != '\n') prevLineStart--;
        if (LineHasPrefix(text, prevLineStart, fence)) {
            fenceLineStart = prevLineStart;
            // Include the trailing newline after the opening fence.
            uint32_t afterFence = prevLineStart + static_cast<uint32_t>(fence.size());
            // Verify the fence line is just the fence (optionally followed by newline).
            if (afterFence < text.size() && (text[afterFence] == '\n' || afterFence == text.size())) {
                foundOpen = true;
            }
        }
    }

    // Look forwards from lastLineEnd for a closing ``` fence.
    uint32_t closeFenceStart = 0;
    bool foundClose = false;
    if (foundOpen) {
        if (LineHasPrefix(text, lastLineEnd, fence)) {
            uint32_t afterClose = lastLineEnd + static_cast<uint32_t>(fence.size());
            if (afterClose <= text.size() &&
                (afterClose == text.size() || text[afterClose] == '\n')) {
                closeFenceStart = lastLineEnd;
                foundClose = true;
            }
        }
    }

    if (foundOpen && foundClose) {
        // Remove the code block fences.
        // Remove closing fence (+ newline after it if present).
        uint32_t closeEnd = closeFenceStart + static_cast<uint32_t>(fence.size());
        if (closeEnd < text.size() && text[closeEnd] == '\n') closeEnd++;
        std::string closeRemoved = text.substr(closeFenceStart, closeEnd - closeFenceStart);

        // Remove opening fence (+ newline after it if present).
        uint32_t openEnd = fenceLineStart + static_cast<uint32_t>(fence.size());
        if (openEnd < text.size() && text[openEnd] == '\n') openEnd++;
        std::string openRemoved = text.substr(fenceLineStart, openEnd - fenceLineStart);

        // Splice out closing fence first (so opening fence offset is unaffected).
        buf->Splice(closeFenceStart, closeEnd - closeFenceStart, "");
        // Now splice out opening fence.
        buf->Splice(fenceLineStart, openEnd - fenceLineStart, "");

        // Set selection to cover the content that was inside the fences.
        uint32_t contentStart = fenceLineStart;
        uint32_t contentEnd = closeFenceStart - (openEnd - fenceLineStart);
        sel->anchor = {contentStart};
        sel->active = {contentEnd};

        // Record undo: the combined removed text is open + content + close.
        // We do two splices, so record the net effect as one entry:
        // removed = openRemoved + content + closeRemoved
        // inserted = content (the same text, without fences)
        // But the two splices are non-contiguous, so we record the opening
        // fence removal as the undo entry (the larger one covers the net change).
        // For simplicity, record as a single entry at the opening fence position.
        if (undo) {
            UndoEntry entry{};
            entry.offset = fenceLineStart;
            entry.removed = openRemoved + text.substr(openEnd, closeFenceStart - openEnd) + closeRemoved;
            // Recompute inserted: the content remains, fences are removed.
            // The content is text between openEnd and closeFenceStart.
            entry.inserted = text.substr(openEnd, closeFenceStart - openEnd);
            entry.selBefore = selBefore;
            entry.selAfter = *sel;
            entry.type = EditType::Other;
            undo->Push(entry);
        }
        return;
    }

    // Add code block fences: insert ``` on its own line before and after.
    // The fence line should be: "```\n" before the content, and "```\n" after.
    // But we need to be careful about existing newlines.

    // Build the replacement: ```
    // + existing content (firstLineStart..lastLineEnd) +
    // ```
    // We wrap the content with fence lines.
    std::string oldText = text.substr(firstLineStart, lastLineEnd - firstLineStart);
    std::string newText;

    // If there's content before us on the same line (shouldn't happen for
    // paragraph-level selection, but handle gracefully), add a newline.
    bool needLeadingNewline = (firstLineStart > 0 && text[firstLineStart - 1] != '\n');
    if (needLeadingNewline) newText += "\n";

    newText += fence + "\n";
    // Ensure the content ends with a newline before the closing fence.
    std::string content = oldText;
    if (content.empty() || content.back() != '\n') content += '\n';
    newText += content;
    newText += fence;

    // If the original text had a trailing newline after lastLineEnd, include it.
    // Otherwise add one after the closing fence.
    if (lastLineEnd < text.size() && text[lastLineEnd] == '\n') {
        newText += "\n";
    } else {
        newText += "\n";
    }

    buf->Splice(firstLineStart, lastLineEnd - firstLineStart, newText);
    sel->anchor = {firstLineStart};
    sel->active = {firstLineStart + static_cast<uint32_t>(newText.size())};
    RecordUndo(undo, firstLineStart, oldText, newText, selBefore, *sel);
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

void SetHeadingLevel(TextBuffer* buf, Selection* sel, int level,
                      UndoStack* undo) {
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
    std::string removed = text.substr(paraStart, oldLen);
    Selection selBefore = *sel;
    buf->Splice(paraStart, oldLen, replacement);

    // Place the caret at the end of the new content (content start if the
    // line was empty). This mirrors the old behaviour of keeping the caret in
    // the same relative position within the content.
    uint32_t newOffset = paraStart + static_cast<uint32_t>(replacement.size());
    sel->Collapse({newOffset});
    RecordUndo(undo, paraStart, removed, replacement, selBefore, *sel);
}

// Fix: sel_ should be *sel

// Helper: collect all line-start offsets within a [start, end) range.
static std::vector<uint32_t> LinesInRange(const std::string& text,
                                           uint32_t start, uint32_t end) {
    std::vector<uint32_t> lines;
    if (end > text.size()) end = static_cast<uint32_t>(text.size());
    uint32_t pos = LineStartOf(text, start);
    while (pos < end) {
        lines.push_back(pos);
        // Advance to next line.
        while (pos < text.size() && text[pos] != '\n') pos++;
        if (pos < text.size()) pos++;  // skip the \n
    }
    return lines;
}

void ToggleUnorderedList(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();

    // Collect all lines in the selection range (or just the caret line).
    std::vector<uint32_t> lineStarts;
    if (sel->Empty()) {
        lineStarts.push_back(LineStartOf(text, sel->active.offset));
    } else {
        lineStarts = LinesInRange(text, sel->Start(), sel->Start() + sel->Length());
    }

    // Determine action from the FIRST line: if it's already a bullet, remove;
    // otherwise, add.
    std::string firstLine = GetLine(text, lineStarts[0]);
    std::string firstPrefix = GetLinePrefix(firstLine);
    bool isUL = false;
    {
        size_t i = 0;
        while (i < firstPrefix.size() && firstPrefix[i] == ' ') i++;
        if (i < firstPrefix.size() && (firstPrefix[i] == '-' || firstPrefix[i] == '*' || firstPrefix[i] == '+') &&
            i + 1 < firstPrefix.size() && firstPrefix[i + 1] == ' ')
            isUL = true;
    }

    Selection selBefore = *sel;
    // Process lines in REVERSE order so earlier splices don't shift later offsets.
    for (int li = static_cast<int>(lineStarts.size()) - 1; li >= 0; --li) {
        uint32_t ls = lineStarts[li];
        // Re-read line each time (buffer may have changed by earlier splices).
        const std::string& cur = buf->Text();
        std::string line = GetLine(cur, ls);
        std::string prefix = GetLinePrefix(line);
        if (isUL) {
            // Remove bullet marker (only if this line actually has one).
            size_t i = 0;
            while (i < prefix.size() && prefix[i] == ' ') i++;
            bool hasBullet = (i < prefix.size() &&
                (prefix[i] == '-' || prefix[i] == '*' || prefix[i] == '+') &&
                i + 1 < prefix.size() && prefix[i + 1] == ' ');
            if (hasBullet) {
                std::string removed = prefix;
                buf->Splice(ls, static_cast<uint32_t>(prefix.size()), "");
                // Record undo as a separate entry per line.
                Selection dummySel = *sel;
                dummySel.Collapse({ls});
                RecordUndo(undo, ls, removed, "", selBefore, dummySel);
            }
        } else {
            // Add "- " prefix (replace existing indent prefix).
            std::string removed = prefix;
            buf->Splice(ls, static_cast<uint32_t>(prefix.size()), "- ");
            Selection dummySel = *sel;
            dummySel.Collapse({ls});
            RecordUndo(undo, ls, removed, "- ", selBefore, dummySel);
        }
    }
    // Collapse caret to start of selection.
    sel->Collapse({selBefore.Start()});
}

void ToggleOrderedList(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();

    std::vector<uint32_t> lineStarts;
    if (sel->Empty()) {
        lineStarts.push_back(LineStartOf(text, sel->active.offset));
    } else {
        lineStarts = LinesInRange(text, sel->Start(), sel->Start() + sel->Length());
    }

    // Determine action from the FIRST line.
    std::string firstLine = GetLine(text, lineStarts[0]);
    std::string firstPrefix = GetLinePrefix(firstLine);
    bool isOL = false;
    {
        size_t i = 0;
        while (i < firstPrefix.size() && firstPrefix[i] == ' ') i++;
        if (i < firstPrefix.size() && firstPrefix[i] >= '0' && firstPrefix[i] <= '9') {
            size_t j = i;
            while (j < firstPrefix.size() && firstPrefix[j] >= '0' && firstPrefix[j] <= '9') j++;
            if (j < firstPrefix.size() && firstPrefix[j] == '.' && j + 1 < firstPrefix.size() && firstPrefix[j + 1] == ' ')
                isOL = true;
        }
    }

    Selection selBefore = *sel;
    int itemNumber = 1;
    // Process lines in REVERSE order.
    for (int li = static_cast<int>(lineStarts.size()) - 1; li >= 0; --li) {
        uint32_t ls = lineStarts[li];
        const std::string& cur = buf->Text();
        std::string line = GetLine(cur, ls);
        std::string prefix = GetLinePrefix(line);
        if (isOL) {
            // Check if this line has an ordered list prefix.
            size_t i = 0;
            while (i < prefix.size() && prefix[i] == ' ') i++;
            bool hasOL = false;
            if (i < prefix.size() && prefix[i] >= '0' && prefix[i] <= '9') {
                size_t j = i;
                while (j < prefix.size() && prefix[j] >= '0' && prefix[j] <= '9') j++;
                if (j < prefix.size() && prefix[j] == '.' && j + 1 < prefix.size() && prefix[j + 1] == ' ')
                    hasOL = true;
            }
            if (hasOL) {
                std::string removed = prefix;
                buf->Splice(ls, static_cast<uint32_t>(prefix.size()), "");
                Selection dummySel = *sel;
                dummySel.Collapse({ls});
                RecordUndo(undo, ls, removed, "", selBefore, dummySel);
            }
        } else {
            // Add "N. " prefix.
            std::string numPrefix = std::to_string(itemNumber) + ". ";
            std::string removed = prefix;
            buf->Splice(ls, static_cast<uint32_t>(prefix.size()), numPrefix);
            Selection dummySel = *sel;
            dummySel.Collapse({ls});
            RecordUndo(undo, ls, removed, numPrefix, selBefore, dummySel);
        }
        itemNumber++;
    }
    sel->Collapse({selBefore.Start()});
}

void ToggleBlockquote(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();

    std::vector<uint32_t> lineStarts;
    if (sel->Empty()) {
        lineStarts.push_back(LineStartOf(text, sel->active.offset));
    } else {
        lineStarts = LinesInRange(text, sel->Start(), sel->Start() + sel->Length());
    }

    // Determine action from the FIRST line.
    std::string firstLine = GetLine(text, lineStarts[0]);
    std::string firstPrefix = GetLinePrefix(firstLine);
    bool isQuote = false;
    {
        size_t i = 0;
        while (i < firstPrefix.size() && firstPrefix[i] == ' ') i++;
        if (i < firstPrefix.size() && firstPrefix[i] == '>')
            isQuote = true;
    }

    Selection selBefore = *sel;
    for (int li = static_cast<int>(lineStarts.size()) - 1; li >= 0; --li) {
        uint32_t ls = lineStarts[li];
        const std::string& cur = buf->Text();
        std::string line = GetLine(cur, ls);
        std::string prefix = GetLinePrefix(line);
        if (isQuote) {
            // Check if this line has a > prefix.
            size_t i = 0;
            while (i < prefix.size() && prefix[i] == ' ') i++;
            if (i < prefix.size() && prefix[i] == '>') {
                std::string removed = prefix;
                buf->Splice(ls, static_cast<uint32_t>(prefix.size()), "");
                Selection dummySel = *sel;
                dummySel.Collapse({ls});
                RecordUndo(undo, ls, removed, "", selBefore, dummySel);
            }
        } else {
            std::string removed = prefix;
            buf->Splice(ls, static_cast<uint32_t>(prefix.size()), "> ");
            Selection dummySel = *sel;
            dummySel.Collapse({ls});
            RecordUndo(undo, ls, removed, "> ", selBefore, dummySel);
        }
    }
    sel->Collapse({selBefore.Start()});
}

void IndentLine(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    // Insert two spaces at the beginning of the line.
    Selection selBefore = *sel;
    buf->Splice(lineStart, 0, "  ");
    sel->Collapse({sel->active.offset + 2});
    RecordUndo(undo, lineStart, "", "  ", selBefore, *sel);
}

void OutdentLine(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();
    uint32_t lineStart = LineStartOf(text, sel->active.offset);
    // Remove up to 2 leading spaces.
    int removeCount = 0;
    for (int i = 0; i < 2 && lineStart + i < text.size() && text[lineStart + i] == ' '; i++)
        removeCount++;
    if (removeCount > 0) {
        Selection selBefore = *sel;
        std::string removed = text.substr(lineStart, removeCount);
        buf->Splice(lineStart, removeCount, "");
        sel->Collapse({static_cast<uint32_t>(std::max(0, static_cast<int32_t>(sel->active.offset) - removeCount))});
        RecordUndo(undo, lineStart, removed, "", selBefore, *sel);
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
