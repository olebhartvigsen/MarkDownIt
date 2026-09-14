#include "editcontroller.h"
#include "dom.h"
#include "navigation.h"
#include <chrono>
#include <cstdint>

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
        if (IsControl(previous) || IsControl(current) ||
            (previous == '\r' && current != '\n'))
            break;
        if (!GraphemeExtends(previous, current, regionalCount, zwjReady))
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

void EditController::RecordAndApply(uint32_t offset, uint32_t length,
                                    const std::string& replacement,
                                    EditType type) {
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

    Selection selBefore = *sel_;
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

void EditController::ReplaceText(uint32_t offset, uint32_t length,
                                  const std::string& replacement,
                                  EditType type) {
    RecordAndApply(offset, length, replacement, type);
}

void EditController::DeleteSelection() {
    if (sel_->Empty()) return;
    uint32_t start = sel_->Start();
    uint32_t len = sel_->Length();
    RecordAndApply(start, len, "", EditType::Delete);
}

void EditController::DeleteBackward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at == 0) return;
    uint32_t prev = PrevGraphemeBoundary(buf_->Text(), at);
    RecordAndApply(prev, at - prev, "", EditType::Delete);
}

void EditController::DeleteForward() {
    if (!sel_->Empty()) { DeleteSelection(); return; }
    uint32_t at = sel_->active.offset;
    if (at >= buf_->Length()) return;
    uint32_t next = NextGraphemeBoundary(buf_->Text(), at);
    RecordAndApply(at, next - at, "", EditType::Delete);
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

void EditController::InsertParagraphBreak(const Document& doc) {
    if (!sel_->Empty()) DeleteSelection();

    uint32_t at = sel_->active.offset;

    BlockKind ctx = BlockKind::Paragraph;
    bool inList = false;
    bool inCode = false;
    bool ordered = false;
    bool emptyListItem = false;
    bool inTable = false;

    for (const auto& n : doc.nodes) {
        if (at >= n.srcOffset && at <= n.srcOffset + n.srcLength) {
            ctx = n.block;
            if (n.block == BlockKind::List) {
                inList = true;
                ordered = n.ordered;
                bool hasContent = false;
                for (const auto& ib : n.children) {
                    if (!ib.text.empty()) { hasContent = true; break; }
                }
                emptyListItem = !hasContent;
            }
            if (n.block == BlockKind::CodeBlock) {
                inCode = true;
            }
            if (n.block == BlockKind::Table) {
                inTable = true;
            }
            break;
        }
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
                    if (!cell.u16ToSrc.empty())
                        cellEnd = cell.u16ToSrc.back() + 1;
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
        return;
    }

    if (inCode) {
        RecordAndApply(at, 0, "\n", EditType::ParagraphBreak);
        return;
    }

    if (inList) {
        if (emptyListItem) {
            uint32_t lineStart = buf_->LineStart(at);
            uint32_t toDelete = at - lineStart;
            if (toDelete > 0) {
                RecordAndApply(lineStart, toDelete, "", EditType::ParagraphBreak);
            }
            return;
        }
        std::string marker = ordered ? "\n1. " : "\n- ";
        RecordAndApply(at, 0, marker, EditType::ParagraphBreak);
        return;
    }

    RecordAndApply(at, 0, "\n\n", EditType::ParagraphBreak);
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
