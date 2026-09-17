#include "editcontroller.h"
#include "dom.h"
#include "navigation.h"
#include <chrono>
#include <cstdint>
#include <cctype>
#include <utility>
#include <vector>

namespace {
struct CodePoint {
    char32_t value = 0;
    uint32_t next = 0;
};

static bool IsContinuation(unsigned char b) {
    return (b & 0xC0) == 0x80;
}

static CodePoint DecodeAt(const std::string& s, uint32_t at) {
    if (at >= s.size()) return {};
    const auto b = static_cast<unsigned char>(s[at]);
    if (b < 0x80) return {b, at + 1};
    if (b >= 0xC2 && b <= 0xDF && at + 1 < s.size()) {
        const auto b1 = static_cast<unsigned char>(s[at + 1]);
        if (IsContinuation(b1))
            return {static_cast<char32_t>((b & 0x1F) << 6 | (b1 & 0x3F)), at + 2};
    }
    if (b >= 0xE0 && b <= 0xEF && at + 2 < s.size()) {
        const auto b1 = static_cast<unsigned char>(s[at + 1]);
        const auto b2 = static_cast<unsigned char>(s[at + 2]);
        const bool valid = IsContinuation(b1) && IsContinuation(b2) &&
            !(b == 0xE0 && b1 < 0xA0) &&
            !(b == 0xED && b1 >= 0xA0);
        if (valid)
            return {static_cast<char32_t>((b & 0x0F) << 12 |
                                          (b1 & 0x3F) << 6 | (b2 & 0x3F)), at + 3};
    }
    if (b >= 0xF0 && b <= 0xF4 && at + 3 < s.size()) {
        const auto b1 = static_cast<unsigned char>(s[at + 1]);
        const auto b2 = static_cast<unsigned char>(s[at + 2]);
        const auto b3 = static_cast<unsigned char>(s[at + 3]);
        const bool valid = IsContinuation(b1) && IsContinuation(b2) &&
            IsContinuation(b3) && !(b == 0xF0 && b1 < 0x90) &&
            !(b == 0xF4 && b1 > 0x8F);
        if (valid)
            return {static_cast<char32_t>((b & 0x07) << 18 |
                                          (b1 & 0x3F) << 12 |
                                          (b2 & 0x3F) << 6 | (b3 & 0x3F)), at + 4};
    }
    return {0xFFFD, at + 1};
}

static void AppendUtf8(std::string* out, char32_t cp) {
    if (cp <= 0x7F) {
        out->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static std::string NormalizeUtf8(const std::string& input) {
    std::string out;
    uint32_t at = 0;
    while (at < input.size()) {
        CodePoint cp = DecodeAt(input, at);
        AppendUtf8(&out, cp.value);
        at = cp.next;
    }
    return out;
}

struct ListLineInfo {
    uint32_t lineStart = 0;
    uint32_t lineEnd = 0;
    uint32_t contentStart = 0;
    std::string prefix;
    bool empty = false;
};

static bool GetListLineInfo(const std::string& text, uint32_t at,
                            ListLineInfo* out) {
    if (!out || at > text.size()) return false;

    uint32_t lineStart = at;
    while (lineStart > 0 && text[lineStart - 1] != '\n') --lineStart;
    uint32_t lineEnd = lineStart;
    while (lineEnd < text.size() && text[lineEnd] != '\n') ++lineEnd;

    uint32_t markerStart = lineStart;
    while (markerStart < lineEnd &&
           (text[markerStart] == ' ' || text[markerStart] == '\t')) {
        ++markerStart;
    }
    uint32_t markerEnd = markerStart;
    if (markerStart < lineEnd &&
        (text[markerStart] == '-' || text[markerStart] == '*' ||
         text[markerStart] == '+')) {
        markerEnd = markerStart + 1;
    } else {
        while (markerEnd < lineEnd &&
               std::isdigit(static_cast<unsigned char>(text[markerEnd]))) {
            ++markerEnd;
        }
        if (markerEnd == markerStart || markerEnd >= lineEnd ||
            (text[markerEnd] != '.' && text[markerEnd] != ')')) {
            return false;
        }
        ++markerEnd;
    }

    if (markerEnd >= lineEnd ||
        (text[markerEnd] != ' ' && text[markerEnd] != '\t')) {
        return false;
    }
    uint32_t contentStart = markerEnd;
    while (contentStart < lineEnd &&
           (text[contentStart] == ' ' || text[contentStart] == '\t')) {
        ++contentStart;
    }
    uint32_t contentCheck = contentStart;
    while (contentCheck < lineEnd && text[contentCheck] == '\r') ++contentCheck;

    out->lineStart = lineStart;
    out->lineEnd = lineEnd;
    out->contentStart = contentStart;
    out->prefix = text.substr(lineStart, contentStart - lineStart);
    out->empty = contentCheck == lineEnd;
    return true;
}

static std::string ListContinuationPrefix(const std::string& text,
                                           uint32_t at) {
    ListLineInfo line;
    if (!GetListLineInfo(text, at, &line) || line.empty) return {};
    // Keep the exact indentation, bullet character, ordered marker, and
    // whitespace used by the current item. Ordered lists commonly use 1.
    // for every source marker, so do not renumber the continuation here.
    return "\n" + line.prefix;
}

static bool IsCombining(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) ||
           (cp >= 0x0483 && cp <= 0x0489) ||
           (cp >= 0x0591 && cp <= 0x05BD) ||
           (cp >= 0x0610 && cp <= 0x061A) ||
           (cp >= 0x064B && cp <= 0x065F) ||
           cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED) ||
           (cp >= 0x0711 && cp <= 0x0711) ||
           (cp >= 0x0730 && cp <= 0x074A) ||
           (cp >= 0x07A6 && cp <= 0x07B0) ||
           (cp >= 0x07EB && cp <= 0x07F3) ||
           (cp >= 0x0816 && cp <= 0x0819) ||
           (cp >= 0x081B && cp <= 0x0823) ||
           (cp >= 0x0825 && cp <= 0x0827) ||
           (cp >= 0x0829 && cp <= 0x082D) ||
           (cp >= 0x0859 && cp <= 0x085F) ||
           (cp >= 0x08D3 && cp <= 0x08FF) ||
           (cp >= 0x0900 && cp <= 0x0903) ||
           (cp >= 0x093A && cp <= 0x093C) ||
           (cp >= 0x093E && cp <= 0x094F) ||
           (cp >= 0x0951 && cp <= 0x0957) ||
           (cp >= 0x0962 && cp <= 0x0963) ||
           (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) ||
           (cp >= 0x1D165 && cp <= 0x1D169) ||
           (cp >= 0x1D16D && cp <= 0x1D172) ||
           (cp >= 0x1D17B && cp <= 0x1D182) ||
           (cp >= 0x1D185 && cp <= 0x1D18B) ||
           (cp >= 0x1D1AA && cp <= 0x1D1AD) ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

static bool IsSpacingMark(char32_t cp) {
    return (cp >= 0x0903 && cp <= 0x093B) ||
           (cp >= 0x093E && cp <= 0x0940) ||
           (cp >= 0x0949 && cp <= 0x094C) ||
           (cp >= 0x0982 && cp <= 0x0983) ||
           (cp >= 0x0A03 && cp <= 0x0A03) ||
           (cp >= 0x0ABE && cp <= 0x0AC0) ||
           (cp >= 0x0B02 && cp <= 0x0B03) ||
           (cp >= 0x0BBE && cp <= 0x0BC2) ||
           (cp >= 0x0C01 && cp <= 0x0C03) ||
           (cp >= 0x0CC0 && cp <= 0x0CC4) ||
           (cp >= 0x0D02 && cp <= 0x0D03) ||
           (cp >= 0x0D3E && cp <= 0x0D44) ||
           (cp >= 0x0D46 && cp <= 0x0D48) ||
           (cp >= 0x0D4A && cp <= 0x0D4C) ||
           (cp >= 0x0F3E && cp <= 0x0F3F) ||
           (cp >= 0x102B && cp <= 0x102C) ||
           (cp >= 0x17B6 && cp <= 0x17C8) ||
           (cp >= 0x1A55 && cp <= 0x1A57) ||
           (cp >= 0x1B04 && cp <= 0x1B04) ||
           (cp >= 0x1B35 && cp <= 0x1B35) ||
           (cp >= 0xA823 && cp <= 0xA824);
}

static bool IsPrepend(char32_t cp) {
    return (cp >= 0x0600 && cp <= 0x0605) ||
           cp == 0x06DD || (cp >= 0x070F && cp <= 0x070F) ||
           cp == 0x0890 || cp == 0x0891 || cp == 0x08E2 ||
           (cp >= 0x110BD && cp <= 0x110BD) ||
           (cp >= 0x111C2 && cp <= 0x111C3);
}

static bool IsEmojiTag(char32_t cp) {
    return cp >= 0xE0020 && cp <= 0xE007F;
}


static bool IsEmojiModifier(char32_t cp) {
    return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

static bool IsRegionalIndicator(char32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

static bool IsJoiner(char32_t cp) { return cp == 0x200D; }

static bool IsControl(char32_t cp) {
    return (cp <= 0x001F && cp != 0x000D && cp != 0x000A) ||
           (cp >= 0x007F && cp <= 0x009F);
}

static bool IsExtendedPictographic(char32_t cp) {
    return (cp >= 0x1F000 && cp <= 0x1FAFF) ||
           (cp >= 0x2300 && cp <= 0x23FF) ||
           (cp >= 0x2B00 && cp <= 0x2BFF) ||
           cp == 0x00A9 || cp == 0x00AE || cp == 0x203C ||
           cp == 0x2049 || cp == 0x2122 || cp == 0x2139 ||
           cp == 0x3030 || cp == 0x303D || cp == 0x3297 ||
           cp == 0x3299;
}

static bool IsHangulL(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0xA960 && cp <= 0xA97C);
}
static bool IsHangulV(char32_t cp) {
    return (cp >= 0x1160 && cp <= 0x11A7) || (cp >= 0xD7B0 && cp <= 0xD7C6);
}
static bool IsHangulT(char32_t cp) {
    return (cp >= 0x11A8 && cp <= 0x11FF) || (cp >= 0xD7CB && cp <= 0xD7FB);
}
static bool IsHangulLV(char32_t cp) {
    return cp >= 0xAC00 && cp <= 0xD7A3 && ((cp - 0xAC00) % 28) == 0;
}
static bool IsHangulLVT(char32_t cp) {
    return cp >= 0xAC00 && cp <= 0xD7A3 && ((cp - 0xAC00) % 28) != 0;
}

static bool GraphemeExtends(char32_t previous, char32_t current,
                            int regionalCount, bool zwjReady) {
    if (previous == '\r' && current == '\n') return true;
    if (IsCombining(current) || IsSpacingMark(current) ||
        IsEmojiModifier(current) || IsEmojiTag(current) ||
        IsJoiner(current)) return true;
    if (IsPrepend(previous)) return true;
    if (IsHangulL(previous) &&
        (IsHangulL(current) || IsHangulV(current))) return true;
    if ((IsHangulLV(previous) || IsHangulV(previous)) &&
        (IsHangulV(current) || IsHangulT(current))) return true;
    if ((IsHangulLVT(previous) || IsHangulT(previous)) &&
        IsHangulT(current)) return true;
    if (IsRegionalIndicator(current) && IsRegionalIndicator(previous))
        return (regionalCount % 2) == 1;
    if (previous == 0x200D && zwjReady &&
        IsExtendedPictographic(current)) return true;
    return false;
}

}  // namespace

static uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

uint32_t NextGraphemeBoundary(const std::string& s, uint32_t offset) {
    if (offset >= s.size()) return static_cast<uint32_t>(s.size());
    CodePoint first = DecodeAt(s, offset);
    uint32_t i = first.next;
    char32_t previous = first.value;
    int regionalCount = IsRegionalIndicator(previous) ? 1 : 0;
    bool epPrefix = IsExtendedPictographic(previous);
    bool zwjReady = false;
    while (i < s.size()) {
        CodePoint current = DecodeAt(s, i);
        if (IsControl(previous) || IsControl(current.value) ||
            (previous == '\r' && current.value != '\n'))
            break;
        if (!GraphemeExtends(previous, current.value, regionalCount, zwjReady))
            break;
        const bool currentIsExtend = IsCombining(current.value) ||
            IsSpacingMark(current.value) || IsEmojiModifier(current.value) ||
            IsEmojiTag(current.value);
        const bool currentIsJoiner = IsJoiner(current.value);
        if (IsRegionalIndicator(current.value)) ++regionalCount;
        if (currentIsJoiner) {
            zwjReady = epPrefix;
        } else if (!currentIsExtend) {
            epPrefix = IsExtendedPictographic(current.value);
            zwjReady = false;
        }
        previous = current.value;
        i = current.next;
    }
    return i;
}

uint32_t PrevGraphemeBoundary(const std::string& s, uint32_t offset) {
    if (offset == 0) return 0;
    if (offset > s.size()) offset = static_cast<uint32_t>(s.size());
    uint32_t previous = 0;
    uint32_t current = 0;
    while (current < offset) {
        previous = current;
        uint32_t next = NextGraphemeBoundary(s, current);
        if (next >= offset) return previous;
        current = next;
    }
    return previous;
}

bool IsGraphemeBoundary(const std::string& s, uint32_t offset) {
    if (offset == 0 || offset >= s.size()) return offset <= s.size();
    const uint32_t previous = PrevGraphemeBoundary(s, offset);
    return NextGraphemeBoundary(s, previous) == offset;
}

void EditController::RecordAndApply(uint32_t offset, uint32_t length,
                                      const std::string& replacement,
                                      EditType type,
                                      const Selection* undoSelectionBefore) {
    // Clamp to the buffer before substr: after an external buffer change
    // (file reload, watcher) a stale selection can exceed the text and
    // std::string::substr would throw std::out_of_range.
    const std::string normalized = NormalizeUtf8(replacement);
    const std::string& applied = normalized;
    uint32_t bufLen = static_cast<uint32_t>(buf_->Text().size());
    if (offset > bufLen) offset = bufLen;
    if (length > bufLen - offset) length = bufLen - offset;

    // Record what we are about to remove.
    std::string removed = buf_->Text().substr(offset, length);

    Selection selBefore = undoSelectionBefore ? *undoSelectionBefore : *sel_;
    uint32_t end = buf_->Splice(offset, length, applied);
    sel_->Collapse({end});
    Selection selAfter = *sel_;

    if (undo_) {
        UndoEntry entry{};
        entry.offset = offset;
        entry.removed = removed;
        entry.inserted = applied;
        entry.selBefore = selBefore;
        entry.selAfter = selAfter;
        entry.timestamp = NowMs();
        entry.type = type;
        undo_->Push(entry);
    }
}

void EditController::InsertText(const std::string& utf8) {
    uint32_t at = sel_->Empty() ? sel_->active.offset : sel_->Start();
    uint32_t length = sel_->Empty() ? 0 : sel_->Length();
    RecordAndApply(at, length, utf8, EditType::Insert);
}

void EditController::ReplaceTextRange(uint32_t offset, uint32_t length,
                                        const std::string& replacement,
                                        EditType type,
                                        const Selection* undoSelectionBefore) {
    RecordAndApply(offset, length, replacement, type, undoSelectionBefore);
}

void EditController::DeleteSelection() {
    if (sel_->Empty()) return;
    uint32_t start = sel_->Start();
    uint32_t len = sel_->Length();
    RecordAndApply(start, len, "", EditType::Delete);
}

namespace {

uint32_t ParagraphSeparatorLengthAt(const std::string& text, uint32_t offset) {
    if (offset > text.size()) return 0;
    if (offset + 4 <= text.size() &&
        text.compare(offset, 4, "\r\n\r\n") == 0) return 4;
    if (offset + 2 <= text.size() && text.compare(offset, 2, "\n\n") == 0)
        return 2;
    return 0;
}

bool IsParagraphSeparator(const Document* doc, uint32_t offset,
                          uint32_t separatorLength) {
    if (!doc || separatorLength == 0) return false;
    const uint32_t end = offset + separatorLength;
    bool paragraphBefore = false;
    bool paragraphAfter = false;
    for (const auto& node : doc->nodes) {
        if (node.block != BlockKind::Paragraph) continue;
        const uint32_t nodeEnd = node.srcOffset + node.srcLength;
        if (nodeEnd <= offset) paragraphBefore = true;
        if (node.srcOffset >= end) paragraphAfter = true;
    }
    return paragraphBefore && paragraphAfter;
}

}  // namespace

bool EditController::DeleteBackward(const Document* doc) {
    if (!sel_->Empty()) { DeleteSelection(); return true; }
    const uint32_t at = sel_->active.offset;
    if (at == 0) return false;

    const uint32_t separatorStart = at >= 4 &&
        ParagraphSeparatorLengthAt(buf_->Text(), at - 4) == 4 ? at - 4 :
        (at >= 2 && ParagraphSeparatorLengthAt(buf_->Text(), at - 2) == 2 ?
             at - 2 : at);
    const uint32_t separatorLength = separatorStart == at ? 0 :
        ParagraphSeparatorLengthAt(buf_->Text(), separatorStart);
    if (IsParagraphSeparator(doc, separatorStart, separatorLength)) {
        RecordAndApply(separatorStart, separatorLength, "", EditType::Delete);
        return true;
    }

    const uint32_t previous = PrevGraphemeBoundary(buf_->Text(), at);
    RecordAndApply(previous, at - previous, "", EditType::Delete);
    return true;
}

bool EditController::DeleteForward(const Document* doc) {
    if (!sel_->Empty()) { DeleteSelection(); return true; }
    const uint32_t at = sel_->active.offset;
    if (at >= buf_->Length()) return false;

    const uint32_t separatorLength = ParagraphSeparatorLengthAt(buf_->Text(), at);
    if (IsParagraphSeparator(doc, at, separatorLength)) {
        RecordAndApply(at, separatorLength, "", EditType::Delete);
        return true;
    }

    const uint32_t next = NextGraphemeBoundary(buf_->Text(), at);
    RecordAndApply(at, next - at, "", EditType::Delete);
    return true;
}

void EditController::DeleteWordBackward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at == 0) return;
    uint32_t start = MoveWordLeft(*buf_, at);
    if (start < at) RecordAndApply(start, at - start, "", EditType::Delete);
}

void EditController::DeleteWordForward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at >= buf_->Length()) return;
    uint32_t end = MoveWordRight(*buf_, at);
    if (end > at) RecordAndApply(at, end - at, "", EditType::Delete);
}

static std::string InlineMarker(const InlineBlock& inlineBlock,
                                const std::string& source) {
    if (inlineBlock.code) {
        uint32_t start = inlineBlock.srcOffset;
        while (start > 0 && source[start - 1] == '`') --start;
        const uint32_t length = inlineBlock.srcOffset - start;
        return length == 0 ? "`" : source.substr(start, length);
    }
    if (inlineBlock.strong && inlineBlock.em) {
        if (inlineBlock.srcOffset >= 3) {
            const char marker = source[inlineBlock.srcOffset - 1];
            if ((marker == '*' || marker == '_') &&
                source[inlineBlock.srcOffset - 2] == marker &&
                source[inlineBlock.srcOffset - 3] == marker)
                return std::string(3, marker);
        }
        return "***";
    }
    if (inlineBlock.strong) {
        if (inlineBlock.srcOffset >= 2) {
            const char marker = source[inlineBlock.srcOffset - 1];
            if ((marker == '*' || marker == '_') &&
                source[inlineBlock.srcOffset - 2] == marker)
                return std::string(2, marker);
        }
        return "**";
    }
    if (inlineBlock.em) {
        if (inlineBlock.srcOffset > 0) {
            const char marker = source[inlineBlock.srcOffset - 1];
            if (marker == '*' || marker == '_') return std::string(1, marker);
        }
        return "*";
    }
    if (inlineBlock.strike) return "~~";
    return {};
}

static std::string ParagraphSplitReplacement(const Document& doc,
                                             const std::string& source,
                                             uint32_t offset) {
    for (const auto& node : doc.nodes) {
        for (const auto& inlineBlock : node.children) {
            const uint32_t inlineEnd = inlineBlock.srcOffset + inlineBlock.srcLength;
            // Only an insertion inside rendered inline text needs balancing.
            if (offset <= inlineBlock.srcOffset || offset >= inlineEnd) continue;
            if (inlineBlock.kind == InlineKind::Image) return "\n\n";

            const std::string marker = InlineMarker(inlineBlock, source);
            std::string close = marker;
            std::string open = marker;
            if (inlineBlock.kind == InlineKind::Link) {
                close += "](" + inlineBlock.url + ")";
                open = "[" + open;
            }
            return close + "\n\n" + open;
        }
    }
    return "\n\n";
}

bool EditController::InsertParagraphBreak(const Document& doc) {
    // A table cell is single-line Markdown syntax. Test the original
    // selection before deleting it, because Enter is a no-op in a table.
    if (IsOffsetInTable(doc, sel_->active.offset) ||
        (!sel_->Empty() && IsOffsetInTable(doc, sel_->Start()))) {
        return false;
    }
    const Selection selectionBefore = *sel_;
    const uint32_t at = sel_->Empty() ? sel_->active.offset : sel_->Start();
    const uint32_t replacedLength = sel_->Empty() ? 0 : sel_->Length();
    const std::string paragraphReplacement = replacedLength == 0
        ? ParagraphSplitReplacement(doc, buf_->Text(), at) : "\n\n";
    ListLineInfo listLine;
    const bool hasListLine = GetListLineInfo(buf_->Text(), at, &listLine);

    BlockKind ctx = BlockKind::Paragraph;
    bool inList = false;
    bool inCode = false;
    bool emptyListItem = false;
    bool inTable = false;

    for (const auto& n : doc.nodes) {
        if (at >= n.srcOffset && at <= n.srcOffset + n.srcLength) {
            ctx = n.block;
            if (n.block == BlockKind::List) {
                inList = true;
                bool hasContent = false;
                for (const auto& ib : n.children) {
                    if (!ib.text.empty()) { hasContent = true; break; }
                }
                emptyListItem = !hasContent;
            }
            if (n.block == BlockKind::CodeBlock ||
                n.block == BlockKind::MermaidFlowchart ||
                n.block == BlockKind::MermaidPie ||
                n.block == BlockKind::MermaidSequence) {
                inCode = true;
            }
            if (n.block == BlockKind::Table) {
                inTable = true;
            }
            break;
        }
    }

    // Source ranges for an empty list item may be absent from the parsed
    // document. The line syntax is authoritative after a selection edit.
    if (hasListLine) {
        inList = true;
        emptyListItem = listLine.empty;
    }

    // Second pass: the table node's srcLength may not cover the full
    // cell content (e.g., closing ** markers after bold text are past
    // the last cb_text offset). Check each cell's source range directly.
    if (!inTable) {
        for (const auto& n : doc.nodes) {
            if (n.block != BlockKind::Table) continue;
            for (const auto& row : n.rows) {
                for (const auto& cell : row.cells) {
                    if (cell.srcOffset == 0) continue;
                    uint32_t cellLen = 0;
                    for (char32_t cp : cell.text) {
                        cellLen += (cp <= 0x7F) ? 1 : (cp <= 0x7FF) ? 2 :
                                   (cp <= 0xFFFF) ? 3 : 4;
                    }
                    uint32_t cellEnd = cell.srcOffset + cellLen;
                    if (!cell.u16ToSrc.empty()) {
                        const char32_t last = cell.text.empty() ? U' ' : cell.text.back();
                        const uint32_t bytes = last <= 0x7F ? 1 :
                            last <= 0x7FF ? 2 : last <= 0xFFFF ? 3 : 4;
                        cellEnd = cell.u16ToSrc.back() + bytes;
                    }
                    if (at >= cell.srcOffset && at <= cellEnd) {
                        inTable = true;
                        break;
                    }
                }
                if (inTable) break;
            }
            if (inTable) break;
        }
    }

    // In a table cell, inserting any newline breaks the table syntax; 
    // markdown tables require single-line rows. Do nothing instead.
    if (inTable || ctx == BlockKind::Table) {
        return false;
    }

    if (inCode) {
        RecordAndApply(at, replacedLength, "\n", EditType::ParagraphBreak,
                       &selectionBefore);
        return true;
    }

    if (inList) {
        if (emptyListItem && hasListLine) {
            // Remove only the empty item's marker. Keep its line break so
            // Enter exits the list into a blank paragraph.
            RecordAndApply(listLine.lineStart,
                           listLine.lineEnd - listLine.lineStart,
                           "", EditType::ParagraphBreak, &selectionBefore);
            return true;
        }
        const std::string marker = ListContinuationPrefix(buf_->Text(), at);
        if (!marker.empty()) {
            RecordAndApply(at, replacedLength, marker, EditType::ParagraphBreak,
                           &selectionBefore);
            return true;
        }
        RecordAndApply(at, replacedLength, "\n", EditType::ParagraphBreak,
                       &selectionBefore);
        return true;
    }

    RecordAndApply(at, replacedLength, paragraphReplacement,
                   EditType::ParagraphBreak, &selectionBefore);
    return true;
}

bool EditController::InsertSoftBreak(const Document& doc) {
    // Tables use a one-line Markdown syntax, so a newline would corrupt the
    // row. Keep Enter and Shift+Enter consistently disabled in table cells.
    if (IsOffsetInTable(doc, sel_->active.offset) ||
        (!sel_->Empty() && IsOffsetInTable(doc, sel_->Start()))) {
        return false;
    }
    const Selection selectionBefore = *sel_;
    const uint32_t at = sel_->Empty() ? sel_->active.offset : sel_->Start();
    const uint32_t replacedLength = sel_->Empty() ? 0 : sel_->Length();
    bool inCode = false;
    for (const auto& node : doc.nodes) {
        if (at < node.srcOffset || at > node.srcOffset + node.srcLength)
            continue;
        inCode = node.block == BlockKind::CodeBlock ||
                 node.block == BlockKind::MermaidFlowchart ||
                 node.block == BlockKind::MermaidPie ||
                 node.block == BlockKind::MermaidSequence;
        break;
    }
    // Markdown renders a lone newline as a space. Two preceding spaces make
    // this a rendered hard line break while retaining one logical paragraph.
    // Code fences retain their literal newlines without a Markdown marker.
    RecordAndApply(at, replacedLength, inCode ? "\n" : "  \n",
                   EditType::ParagraphBreak, &selectionBefore);
    return true;
}

bool EditController::Undo(UndoEntry* undone) {
    if (!undo_) return false;
    UndoEntry entry;
    if (!undo_->Undo(entry)) return false;
    buf_->Splice(entry.offset,
                 static_cast<uint32_t>(entry.inserted.size()),
                 entry.removed);
    *sel_ = entry.selBefore;
    if (undone) *undone = entry;
    return true;
}

bool EditController::Redo(UndoEntry* redone) {
    if (!undo_) return false;
    UndoEntry entry;
    if (!undo_->Redo(entry)) return false;
    buf_->Splice(entry.offset,
                 static_cast<uint32_t>(entry.removed.size()),
                 entry.inserted);
    *sel_ = entry.selAfter;
    if (redone) *redone = entry;
    return true;
}
