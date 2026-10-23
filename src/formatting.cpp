#include "formatting.h"
#include "undostack.h"
#include <algorithm>
#include <cstdint>
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

// The marker runs of one inline format. The toggle scans them first so
// a selection that only PARTIALLY overlaps a run extends the run
// instead of nesting a second pair inside it, and a `*` toggle over a
// `**` run adds emphasis without touching the bold (text guide, fed og
// kursiv rules 2, 3, 5 and 8).
namespace {

struct MarkerRun {
    uint32_t start;
    uint32_t end;
};

// A marker character right after a backslash is text, not markup.
bool MarkerRunIsEscaped(const std::string& text, uint32_t pos) {
    uint32_t slashes = 0;
    while (pos > slashes && text[pos - slashes - 1] == '\\') slashes++;
    return (slashes % 2) == 1;
}

std::vector<MarkerRun> ScanMarkerRuns(const std::string& text, char mc) {
    std::vector<MarkerRun> runs;
    uint32_t i = 0;
    while (i < text.size()) {
        if (text[i] != mc) {
            i++;
            continue;
        }
        uint32_t j = i;
        while (j < text.size() && text[j] == mc) j++;
        if (!MarkerRunIsEscaped(text, i)) runs.push_back({i, j});
        i = j;
    }
    return runs;
}

struct MarkerUnit {
    uint32_t start;
    uint32_t end;
    uint32_t run;    // index of the run the unit belongs to
};

struct MarkerPair {
    MarkerUnit open;
    MarkerUnit close;
};

// The units a run can hold. A `**` run holds as many two-character
// markers as fit from its start. A `*` run only holds a single unit
// when it is odd: `***text***` is bold plus emphasis, and the emphasis
// piece is the character nearest the text, so a single `*` toggle sees
// it as an emphasis pair while `**` stays untouched.
std::vector<MarkerUnit> RunUnits(const MarkerRun& run, uint32_t runIndex,
                                 uint32_t mlen, const std::string& text,
                                 char mc) {
    std::vector<MarkerUnit> units;
    uint32_t length = run.end - run.start;
    if (mlen >= 2) {
        for (uint32_t at = run.start; at + mlen <= run.end; at += mlen)
            units.push_back({at, at + mlen, runIndex});
        return units;
    }
    if (length % 2 == 1) {
        // The unit nearest the text: the last character when the text
        // follows the run, the first when the text leads it.
        bool textFollows = (run.end >= text.size()) || (text[run.end] != mc);
        uint32_t at = textFollows ? run.end - 1 : run.start;
        units.push_back({at, at + 1, runIndex});
    }
    return units;
}

// Pair the units left to right, always across runs: the first unit of
// the left run pairs with the first of the right run, so `****x****`
// pairs span the text instead of pairing inside one run.
std::vector<MarkerPair> PairUnits(const std::vector<MarkerUnit>& units) {
    std::vector<MarkerPair> pairs;
    std::vector<bool> used(units.size(), false);
    for (size_t i = 0; i < units.size(); i++) {
        if (used[i]) continue;
        size_t partner = units.size();
        for (size_t j = i + 1; j < units.size(); j++) {
            if (!used[j] && units[j].run != units[i].run) {
                partner = j;
                break;
            }
        }
        if (partner == units.size()) break;   // an unmatched leftover
        used[i] = true;
        used[partner] = true;
        pairs.push_back({units[i], units[partner]});
    }
    return pairs;
}

}  // namespace

void ToggleInlineMarker(TextBuffer* buf, Selection* sel, const std::string& marker,
                       UndoStack* undo) {
    if (marker.empty() || buf == nullptr || sel == nullptr) return;
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
    char mc = marker[0];

    // First try: selection already includes the markers.
    if (IsWrappedIn(text, start, end, marker)) {
        // Remove the markers: single splice replacing "**content**" with "content".
        std::string removed = text.substr(start, end - start); // "**content**"
        std::string kept = removed.substr(mlen, removed.size() - mlen * 2); // "content"
        buf->Splice(start, end - start, kept);
        sel->anchor = {start};
        sel->active = {start + static_cast<uint32_t>(kept.size())};
        RecordUndo(undo, start, removed, kept, selBefore, *sel);
        return;
    }

    // The marker runs around and inside the selection decide whether
    // this is a removal (the runs already cover the selection), an
    // extension (part of the selection is already formatted) or a
    // plain wrap (no run overlaps).
    std::vector<MarkerRun> runs = ScanMarkerRuns(text, mc);
    std::vector<MarkerUnit> units;
    for (uint32_t i = 0; i < runs.size(); i++) {
        std::vector<MarkerUnit> runUnits =
            RunUnits(runs[i], i, mlen, text, mc);
        units.insert(units.end(), runUnits.begin(), runUnits.end());
    }
    std::vector<MarkerPair> pairs = PairUnits(units);

    // Which pairs touch the selection: their markers overlap it, or the
    // pair encloses it (the selection is the formatted content).
    std::vector<size_t> touched;
    bool allEnclose = !pairs.empty();
    for (size_t i = 0; i < pairs.size(); i++) {
        const MarkerPair& p = pairs[i];
        bool markerOverlap =
            (p.open.start < end && start < p.open.end) ||
            (p.close.start < end && start < p.close.end);
        bool encloses = (p.open.end <= start && end <= p.close.start);
        if (!markerOverlap && !encloses) continue;
        touched.push_back(i);
        if (!encloses) allEnclose = false;
    }

    if (!touched.empty() && allEnclose) {
        // Every touched pair encloses the selection: this is the
        // removal direction.
        const MarkerPair& p = pairs[touched[0]];
        uint32_t cs = p.open.end;
        uint32_t ce = p.close.start;
        if (touched.size() == 1 && (start > cs || end < ce)) {
            // The selection sits strictly inside the pair, so the text
            // outside keeps its formatting and the run splits at the
            // selection (guide rule 5 and 8). Trailing and leading
            // spaces move out of the split halves so the markers open
            // on a word. When the selection covers the whole content
            // the pair is dropped in one piece instead.
            std::string left = text.substr(cs, start - cs);
            std::string right = text.substr(end, ce - end);
            size_t leftLen = left.size();
            while (leftLen > 0 &&
                   (left[leftLen - 1] == ' ' || left[leftLen - 1] == '\t'))
                leftLen--;
            std::string leftSpill = left.substr(leftLen);
            left = left.substr(0, leftLen);
            size_t rightAt = 0;
            while (rightAt < right.size() &&
                   (right[rightAt] == ' ' || right[rightAt] == '\t'))
                rightAt++;
            std::string rightSpill = right.substr(0, rightAt);
            right = right.substr(rightAt);

            std::string out;
            if (!left.empty()) out += marker + left + marker;
            out += leftSpill;
            out += text.substr(start, end - start);
            out += rightSpill;
            if (!right.empty()) out += marker + right + marker;

            uint32_t fullStart = p.open.start;
            uint32_t fullEnd = p.close.end;
            std::string fullRemoved = text.substr(fullStart, fullEnd - fullStart);
            buf->Splice(fullStart, fullEnd - fullStart, out);
            uint32_t rightOut = right.empty()
                ? 0u
                : static_cast<uint32_t>(mlen + right.size() + mlen);
            uint32_t afterSel = static_cast<uint32_t>(rightSpill.size()) + rightOut;
            uint32_t selStart = fullStart + static_cast<uint32_t>(out.size()) -
                                afterSel - (end - start);
            sel->anchor = {selStart};
            sel->active = {selStart + (end - start)};
            RecordUndo(undo, fullStart, fullRemoved, out, selBefore, *sel);
            return;
        }

        // One pair around the whole selection (or a doubled run): drop
        // every touched pair's markers and keep the selection.
        std::vector<std::pair<uint32_t, uint32_t>> spans;
        for (size_t i : touched) {
            spans.push_back({pairs[i].open.start, pairs[i].open.end});
            spans.push_back({pairs[i].close.start, pairs[i].close.end});
        }
        std::sort(spans.begin(), spans.end());
        uint32_t fullStart = spans.front().first;
        uint32_t fullEnd = spans.back().second;
        // An enclosing pair's markers sit outside the selection, so the
        // kept text is exactly the selection.
        std::string kept = text.substr(start, end - start);
        std::string fullRemoved = text.substr(fullStart, fullEnd - fullStart);
        buf->Splice(fullStart, fullEnd - fullStart, kept);
        sel->anchor = {fullStart};
        sel->active = {fullStart + static_cast<uint32_t>(kept.size())};
        RecordUndo(undo, fullStart, fullRemoved, kept, selBefore, *sel);
        return;
    }

    if (!touched.empty()) {
        // Partial overlap: extend. The replacement covers the selection
        // and every touched pair, the touched pairs' own markers are
        // dropped, and one fresh pair wraps the result. Formatting
        // outside the coverage is left alone.
        uint32_t lo = start;
        uint32_t hi = end;
        std::vector<std::pair<uint32_t, uint32_t>> strips;
        for (size_t i : touched) {
            const MarkerPair& p = pairs[i];
            lo = std::min(lo, p.open.start);
            hi = std::max(hi, p.close.end);
            strips.push_back({p.open.start, p.open.end});
            strips.push_back({p.close.start, p.close.end});
        }
        std::sort(strips.begin(), strips.end());
        std::string content;
        uint32_t at = lo;
        for (const auto& strip : strips) {
            if (strip.first >= hi) break;
            content += text.substr(at, strip.first - at);
            at = std::max(at, strip.second);
        }
        content += text.substr(at, hi - at);
        std::string removed = text.substr(lo, hi - lo);
        std::string inserted = marker + content + marker;
        buf->Splice(lo, hi - lo, inserted);
        sel->anchor = {lo};
        sel->active = {lo + static_cast<uint32_t>(inserted.size())};
        RecordUndo(undo, lo, removed, inserted, selBefore, *sel);
        return;
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

struct SimpleLinkSpan {
    uint32_t linkStart = 0;
    uint32_t linkEnd = 0;
    uint32_t labelStart = 0;
    uint32_t labelEnd = 0;
    uint32_t urlStart = 0;
    uint32_t urlEnd = 0;
};

static bool FindSimpleLinkContaining(const std::string& text,
                                     uint32_t start, uint32_t end,
                                     SimpleLinkSpan* out) {
    if (!out || start > text.size() || end > text.size()) return false;
    size_t open = text.rfind('[', start == 0 ? 0 : start - 1);
    while (open != std::string::npos) {
        size_t close = text.find("](", open + 1);
        if (close == std::string::npos) return false;
        size_t urlEnd = text.find(')', close + 2);
        if (urlEnd == std::string::npos) return false;
        if (text.find('\n', open) != std::string::npos &&
            text.find('\n', open) < urlEnd) return false;
        uint32_t labelStart = static_cast<uint32_t>(open + 1);
        uint32_t labelEnd = static_cast<uint32_t>(close);
        uint32_t linkEnd = static_cast<uint32_t>(urlEnd + 1);
        if (start <= labelEnd && end >= labelStart) {
            out->linkStart = static_cast<uint32_t>(open);
            out->linkEnd = linkEnd;
            out->labelStart = labelStart;
            out->labelEnd = labelEnd;
            out->urlStart = static_cast<uint32_t>(close + 2);
            out->urlEnd = static_cast<uint32_t>(urlEnd);
            return true;
        }
        if (open == 0) break;
        open = text.rfind('[', open - 1);
    }
    return false;
}

static void UpdateLinkDestination(TextBuffer* buf, Selection* sel,
                                  const std::string& url,
                                  const SimpleLinkSpan& link,
                                  UndoStack* undo) {
    const std::string oldText = buf->Text();
    uint32_t start = sel->Start();
    uint32_t end = start + sel->Length();
    Selection before = *sel;

    if (start >= link.labelStart && end <= link.labelEnd) {
        std::string label = oldText.substr(link.labelStart,
                                           link.labelEnd - link.labelStart);
        std::string replacement;
        uint32_t selectedStart = 0;
        if (start == link.labelStart && end == link.labelEnd) {
            replacement = "[" + label + "](" + url + ")";
            selectedStart = 1;
        } else {
            uint32_t localStart = start - link.labelStart;
            uint32_t localEnd = end - link.labelStart;
            std::string left = label.substr(0, localStart);
            std::string selected = label.substr(localStart, localEnd - localStart);
            std::string right = label.substr(localEnd);
            replacement = (left.empty() ? "" : "[" + left + "](" +
                          oldText.substr(link.urlStart, link.urlEnd - link.urlStart) + ")")
                        + "[" + selected + "](" + url + ")"
                        + (right.empty() ? "" : "[" + right + "](" +
                          oldText.substr(link.urlStart, link.urlEnd - link.urlStart) + ")");
            selectedStart = left.empty()
                ? 1u
                : static_cast<uint32_t>(left.size() +
                                        (link.urlEnd - link.urlStart) + 5);
        }
        buf->Splice(link.linkStart, link.linkEnd - link.linkStart, replacement);
        sel->anchor = {link.linkStart + selectedStart};
        sel->active = {link.linkStart + selectedStart + (end - start)};
        RecordUndo(undo, link.linkStart,
                   oldText.substr(link.linkStart, link.linkEnd - link.linkStart),
                   replacement, before, *sel);
        return;
    }

    // A selection that crosses a link boundary must not wrap the existing
    // Markdown syntax in another pair of brackets. Update the link that it
    // touches and leave the outside text unchanged.
    std::string replacement = oldText.substr(link.linkStart,
                                             link.urlStart - link.linkStart) +
                              url + oldText.substr(link.urlEnd,
                                                   link.linkEnd - link.urlEnd);
    const int64_t delta = static_cast<int64_t>(replacement.size()) -
                           static_cast<int64_t>(link.linkEnd - link.linkStart);
    auto adjust = [&](uint32_t value) -> uint32_t {
        if (delta > 0 && value >= link.linkEnd)
            return value + static_cast<uint32_t>(delta);
        if (delta < 0 && value >= link.linkEnd)
            return value - static_cast<uint32_t>(-delta);
        return value;
    };
    start = adjust(start);
    end = adjust(end);
    buf->Splice(link.linkStart, link.linkEnd - link.linkStart, replacement);
    sel->anchor = {start};
    sel->active = {end};
    RecordUndo(undo, link.linkStart,
               oldText.substr(link.linkStart, link.linkEnd - link.linkStart),
               replacement, before, *sel);
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
        SimpleLinkSpan existing;
        if (FindSimpleLinkContaining(buf->Text(), start, end, &existing)) {
            UpdateLinkDestination(buf, sel, url, existing, undo);
            return;
        }
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

// Forward declaration; LineStartOf is defined later in this file.
static uint32_t LineStartOf(const std::string& text, uint32_t offset);

static uint32_t LineEndOf(const std::string& text, uint32_t lineStart) {
    uint32_t end = lineStart;
    while (end < text.size() && text[end] != '\n') ++end;
    return end;
}

// Recognize a triple-backtick fence line. An opening fence may carry an
// info string, while a closing fence may only have trailing whitespace.
static bool IsFenceLine(const std::string& text, uint32_t lineStart,
                        bool opening) {
    if (lineStart > text.size()) return false;
    const uint32_t lineEnd = LineEndOf(text, lineStart);
    uint32_t cursor = lineStart;
    while (cursor < lineEnd &&
           (text[cursor] == ' ' || text[cursor] == '\t')) {
        ++cursor;
    }
    if (cursor + 3 > lineEnd || text[cursor] != '`' ||
        text[cursor + 1] != '`' || text[cursor + 2] != '`') {
        return false;
    }

    cursor += 3;
    if (opening) {
        // Backticks are not valid in a fenced-code info string. Rejecting
        // them also prevents a four-backtick fence being mistaken for three.
        while (cursor < lineEnd) {
            if (text[cursor] == '`') return false;
            ++cursor;
        }
        return true;
    }

    while (cursor < lineEnd &&
           (text[cursor] == ' ' || text[cursor] == '\t' ||
            text[cursor] == '\r')) {
        ++cursor;
    }
    return cursor == lineEnd;
}

void ToggleCodeBlock(TextBuffer* buf, Selection* sel, UndoStack* undo) {
    const std::string& text = buf->Text();
    uint32_t selStart = sel->Start();
    uint32_t selEnd = selStart + sel->Length();
    Selection selBefore = *sel;

    // Find the start of the first selected line.
    uint32_t firstLineStart = LineStartOf(text, selStart);
    uint32_t lastLineStart = selEnd;
    if (lastLineStart > 0 && lastLineStart <= text.size() &&
        text[lastLineStart - 1] == '\n') {
        --lastLineStart;
    }
    lastLineStart = LineStartOf(text, lastLineStart);
    uint32_t afterLastLine = LineEndOf(text, lastLineStart);
    if (afterLastLine < text.size() && text[afterLastLine] == '\n') {
        ++afterLastLine;
    }
    const uint32_t lastLineEnd = afterLastLine;

    const std::string fence = "```";
    uint32_t fenceLineStart = 0;
    uint32_t closeFenceStart = 0;
    uint32_t openEnd = 0;
    uint32_t closeEnd = 0;
    bool foundOpen = false;
    bool foundClose = false;

    // A selection beginning on the opening fence may include the complete
    // block. Scan its selected lines so the closing fence is recognized too.
    if (selStart == firstLineStart && IsFenceLine(text, firstLineStart, true)) {
        uint32_t line = LineEndOf(text, firstLineStart);
        if (line < text.size() && text[line] == '\n') ++line;
        while (line < text.size()) {
            if (IsFenceLine(text, line, false)) {
                const uint32_t lineEnd = LineEndOf(text, line);
                if (selEnd >= lineEnd) {
                    fenceLineStart = firstLineStart;
                    closeFenceStart = line;
                    foundOpen = true;
                    foundClose = true;
                    break;
                }
                break;
            }
            const uint32_t next = LineEndOf(text, line);
            line = next < text.size() ? next + 1 : next;
        }
    }

    // Also support selecting only the content lines inside an existing
    // fenced block. The opening line may contain an info string such as cpp.
    if (!foundOpen && firstLineStart > 0) {
        const uint32_t previousLineStart =
            LineStartOf(text, firstLineStart - 1);
        uint32_t closingLineStart = afterLastLine;
        if (IsFenceLine(text, lastLineStart, false))
            closingLineStart = lastLineStart;
        if (IsFenceLine(text, previousLineStart, true) &&
            IsFenceLine(text, closingLineStart, false)) {
            fenceLineStart = previousLineStart;
            closeFenceStart = closingLineStart;
            foundOpen = true;
            foundClose = true;
        }
    }

    if (foundOpen && foundClose) {
        openEnd = LineEndOf(text, fenceLineStart);
        if (openEnd < text.size() && text[openEnd] == '\n') ++openEnd;
        closeEnd = LineEndOf(text, closeFenceStart);
        if (closeEnd < text.size() && text[closeEnd] == '\n') ++closeEnd;
        std::string openRemoved =
            text.substr(fenceLineStart, openEnd - fenceLineStart);
        std::string closeRemoved =
            text.substr(closeFenceStart, closeEnd - closeFenceStart);

        // Snapshot the content between the fences BEFORE any splice:
        // `text` is a live reference to the buffer and both splices
        // below mutate it, making the old offsets read garbage.
        std::string contentBetween = text.substr(openEnd, closeFenceStart - openEnd);

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
            entry.removed = openRemoved + contentBetween + closeRemoved;
            // Recompute inserted: the content remains, fences are removed.
            entry.inserted = contentBetween;
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
// A heading line (starting with #) is always its own paragraph; the
// following non-blank line starts a NEW paragraph, even without a blank
// line between them.
static uint32_t ParagraphStartOf(const std::string& text, uint32_t offset) {
    uint32_t lineStart = LineStartOf(text, offset);
    while (lineStart > 0) {
        // Move to the previous line.
        uint32_t prevEnd = lineStart - 1; // skip the '\n'
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
        uint32_t nextStart = end + 1; // skip this '\n'
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
        // List and blockquote markers interrupt the paragraph too;
        // otherwise SetHeadingLevel would swallow them as heading text.
        {
            size_t i = 0;
            while (i < nextLine.size() && nextLine[i] == ' ') i++;
            if (i >= nextLine.size()) break;
            char c = nextLine[i];
            bool isMarker = c == '-' || c == '+' || c == '*' || c == '>';
            if (!isMarker && c >= '0' && c <= '9') {
                // Ordered marker: digits followed by '.' or ')'.
                size_t j = i;
                while (j < nextLine.size() && nextLine[j] >= '0' && nextLine[j] <= '9') j++;
                isMarker = j < nextLine.size() &&
                    (nextLine[j] == '.' || nextLine[j] == ')') &&
                    j + 1 < nextLine.size() && nextLine[j + 1] == ' ';
            }
            if (isMarker) break;
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
            cur++; // skip '\n'
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
        if (pos < text.size()) pos++; // skip the \n
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
    // Number the lines first, then apply bottom-to-top. Forward order would
    // shift every later line start once an earlier splice changes length
    // (multi-line toggle corrupted the buffer). Reversing keeps offsets
    // valid; itemNumber counts forward so numbering still ascends.
    std::vector<std::string> newPrefixes(lineStarts.size());
    {
        size_t li = 0;
        while (li < lineStarts.size()) {
            uint32_t ls = lineStarts[li];
            std::string line = GetLine(buf->Text(), ls);
            std::string prefix = GetLinePrefix(line);
            if (isOL) {
                newPrefixes[li] = ""; // remove marker
            } else {
                newPrefixes[li] = std::to_string(itemNumber) + ". ";
            }
            itemNumber++;
            li++;
        }
    }
    for (int li = static_cast<int>(lineStarts.size()) - 1; li >= 0; --li) {
        uint32_t ls = lineStarts[static_cast<size_t>(li)];
        const std::string& cur = buf->Text();
        std::string line = GetLine(cur, ls);
        std::string prefix = GetLinePrefix(line);
        const std::string& numPrefix = newPrefixes[static_cast<size_t>(li)];
        if (numPrefix.empty()) {
            std::string removed = prefix;
            buf->Splice(ls, static_cast<uint32_t>(prefix.size()), "");
            Selection dummySel = *sel;
            dummySel.Collapse({ls});
            RecordUndo(undo, ls, removed, "", selBefore, dummySel);
        } else {
            std::string removed = prefix;
            buf->Splice(ls, static_cast<uint32_t>(prefix.size()), numPrefix);
            Selection dummySel = *sel;
            dummySel.Collapse({ls});
            RecordUndo(undo, ls, removed, numPrefix, selBefore, dummySel);
        }
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

std::vector<InlineMarkerRun> CollectInlineMarkerRuns(
    const Document& doc, const std::string& text, uint32_t start, uint32_t end) {
    std::vector<InlineMarkerRun> runs;

    for (const Node& node : doc.nodes) {
        // Only the blocks that overlap the selection.
        const uint32_t blockStart = node.srcOffset;
        const uint32_t blockEnd = blockStart + node.srcLength;
        if (end <= blockStart || start >= blockEnd) continue;

        for (const InlineBlock& child : node.children) {
            // Only the spans that carry a marker.
            if (!child.strong && !child.em && !child.code && !child.strike)
                continue;

            const uint32_t contentStart = child.srcOffset;
            const uint32_t contentEnd = contentStart + child.srcLength;
            if (contentEnd <= contentStart) continue;
            if (contentEnd <= start || contentStart >= end) continue;

            // The marker character of the run. Bold and italic both
            // use '*', the strike uses '~' and the code uses '`'.
            char marker = '*';
            if (child.code) marker = '`';
            if (child.strike) marker = '~';

            // The run of markers on the left of the content.
            uint32_t leftRun = 0;
            while (contentStart > leftRun &&
                   text[contentStart - leftRun - 1] == marker) {
                ++leftRun;
            }
            // The run of markers on the right of the content.
            uint32_t rightRun = 0;
            while (contentEnd + rightRun < text.size() &&
                   text[contentEnd + rightRun] == marker) {
                ++rightRun;
            }

            if (leftRun > 0 && rightRun > 0) {
                InlineMarkerRun run;
                run.markerStart = contentStart - leftRun;
                run.markerEnd = contentEnd + rightRun;
                run.contentStart = contentStart;
                run.contentEnd = contentEnd;
                runs.push_back(run);
            }
        }
    }

    // The runs come back from the right, so a splice does not move the
    // ones that are still to come.
    std::sort(runs.begin(), runs.end(),
              [](const InlineMarkerRun& a, const InlineMarkerRun& b) {
                  return a.markerStart > b.markerStart;
              });

    // Two runs can share their markers, a bold and an italic for
    // example, so the ones that overlap are merged into the wider one.
    std::vector<InlineMarkerRun> merged;
    for (const InlineMarkerRun& run : runs) {
        if (!merged.empty() && run.markerEnd >= merged.back().markerStart) {
            merged.back().markerStart =
                std::min(merged.back().markerStart, run.markerStart);
            merged.back().markerEnd =
                std::max(merged.back().markerEnd, run.markerEnd);
            merged.back().contentStart =
                std::min(merged.back().contentStart, run.contentStart);
            merged.back().contentEnd =
                std::max(merged.back().contentEnd, run.contentEnd);
        } else {
            merged.push_back(run);
        }
    }
    return merged;
}

void ClearInlineFormatting(const Document& doc, TextBuffer* buf, Selection* sel,
                            UndoStack* undo) {
    if (!buf || !sel) return;
    if (sel->Empty()) return;  // a selection is needed to clear anything

    const uint32_t start = sel->Start();
    uint32_t end = start + sel->Length();
    const std::string& text = buf->Text();
    if (end > text.size()) end = static_cast<uint32_t>(text.size());

    const std::vector<InlineMarkerRun> runs =
        CollectInlineMarkerRuns(doc, text, start, end);
    if (runs.empty()) return;

    // The selection before the change, so the undo step can bring it
    // back.
    const Selection selBefore = *sel;

    // The splices shorten the text, so the end of the selection moves
    // left by the number of bytes that are taken away.
    int32_t removedTotal = 0;
    for (const InlineMarkerRun& run : runs) {
        const std::string content =
            text.substr(run.contentStart, run.contentEnd - run.contentStart);
        const std::string removed =
            text.substr(run.markerStart, run.markerEnd - run.markerStart);

        buf->Splice(run.markerStart, run.markerEnd - run.markerStart, content);
        if (undo) {
            UndoEntry entry{};
            entry.offset = run.markerStart;
            entry.removed = removed;
            entry.inserted = content;
            entry.selBefore = selBefore;
            entry.selAfter = *sel;
            entry.type = EditType::Other;
            undo->Push(entry);
        }
        removedTotal += static_cast<int32_t>(removed.size()) -
                        static_cast<int32_t>(content.size());
    }

    // The selection covers the same text, now without the markers.
    const uint32_t newEnd =
        (end > static_cast<uint32_t>(removedTotal))
            ? end - static_cast<uint32_t>(removedTotal)
            : start;
    sel->anchor = {start};
    sel->active = {newEnd};
}
