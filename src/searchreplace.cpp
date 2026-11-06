#include "searchreplace.h"
#include "editcontroller.h"
#include "unicode_fold.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct CodePoint {
    char32_t value = 0;
    uint32_t start = 0;
    uint32_t length = 0;
};

static bool IsContinuation(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

static bool DecodeOne(const std::string& text, uint32_t offset,
                      CodePoint* out) {
    if (!out || offset >= text.size()) return false;
    const unsigned char b0 = static_cast<unsigned char>(text[offset]);
    char32_t value = 0;
    uint32_t length = 1;
    if (b0 < 0x80u) {
        value = b0;
    } else if (b0 >= 0xC2u && b0 <= 0xDFu && offset + 1 < text.size() &&
               IsContinuation(static_cast<unsigned char>(text[offset + 1]))) {
        value = static_cast<char32_t>(b0 & 0x1Fu) << 6;
        value |= static_cast<char32_t>(text[offset + 1] & 0x3Fu);
        length = 2;
    } else if (b0 >= 0xE0u && b0 <= 0xEFu && offset + 2 < text.size() &&
               IsContinuation(static_cast<unsigned char>(text[offset + 1])) &&
               IsContinuation(static_cast<unsigned char>(text[offset + 2]))) {
        const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        if (!((b0 == 0xE0u && b1 < 0xA0u) ||
              (b0 == 0xEDu && b1 >= 0xA0u))) {
            value = static_cast<char32_t>(b0 & 0x0Fu) << 12;
            value |= static_cast<char32_t>(b1 & 0x3Fu) << 6;
            value |= static_cast<char32_t>(b2 & 0x3Fu);
            length = 3;
        }
    } else if (b0 >= 0xF0u && b0 <= 0xF4u && offset + 3 < text.size() &&
               IsContinuation(static_cast<unsigned char>(text[offset + 1])) &&
               IsContinuation(static_cast<unsigned char>(text[offset + 2])) &&
               IsContinuation(static_cast<unsigned char>(text[offset + 3]))) {
        const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
        const unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
        if (!((b0 == 0xF0u && b1 < 0x90u) ||
              (b0 == 0xF4u && b1 >= 0x90u))) {
            value = static_cast<char32_t>(b0 & 0x07u) << 18;
            value |= static_cast<char32_t>(b1 & 0x3Fu) << 12;
            value |= static_cast<char32_t>(b2 & 0x3Fu) << 6;
            value |= static_cast<char32_t>(b3 & 0x3Fu);
            length = 4;
        }
    }
    if (length == 1 && b0 >= 0x80u) value = 0xFFFD;
    out->value = value;
    out->start = offset;
    out->length = length;
    return true;
}

static std::vector<CodePoint> Decode(const std::string& text) {
    std::vector<CodePoint> result;
    for (uint32_t offset = 0; offset < text.size();) {
        CodePoint cp;
        DecodeOne(text, offset, &cp);
        result.push_back(cp);
        offset += cp.length;
    }
    return result;
}

// Byte-offset boundary table for one search pass. The per-candidate check
// used to walk PrevGraphemeBoundary from the start of the document for
// every position it tested, twice, which made one search quadratic in the
// document size and could take tens of seconds per keystroke. Walking
// NextGraphemeBoundary once over the text and marking every boundary it
// lands on yields exactly the same offsets, at linear cost.
static std::vector<char> BuildGraphemeBoundaryTable(const std::string& text) {
    std::vector<char> boundary(text.size() + 1, 0);
    boundary[0] = 1;
    uint32_t at = 0;
    while (at < text.size()) {
        const uint32_t next = ::NextGraphemeBoundary(text, at);
        if (next <= at) break;
        if (next < boundary.size()) boundary[next] = 1;
        at = next;
    }
    return boundary;
}

static bool IsWord(char32_t cp);

// A folded unit the matcher runs on: the value after canonical
// decomposition and, unless the match is case sensitive, case folding.
// One source character can produce several units (a decomposition or a
// fold expansion), so every unit carries the byte range of the
// character it came from; that is how a match maps back to document
// bytes (find spec: folding and normalisation).
struct FoldUnit {
    char32_t value = 0;
    uint32_t start = 0;
    uint32_t end = 0;
};

static std::vector<FoldUnit> FoldUnits(const std::string& text,
                                       bool caseSensitive) {
    const std::vector<CodePoint> decoded = Decode(text);
    std::vector<FoldUnit> units;
    units.reserve(decoded.size());
    uint32_t decomposed[8];
    uint32_t folded[4];
    for (const CodePoint& cp : decoded) {
        const uint32_t cpEnd = cp.start + cp.length;
        const std::size_t count = unicode::DecomposeInto(cp.value, decomposed, 8);
        for (std::size_t i = 0; i < count; ++i) {
            if (caseSensitive) {
                units.push_back({static_cast<char32_t>(decomposed[i]), cp.start, cpEnd});
                continue;
            }
            const std::size_t foldedCount = unicode::FoldInto(decomposed[i], folded, 4);
            for (std::size_t j = 0; j < foldedCount; ++j)
                units.push_back({static_cast<char32_t>(folded[j]), cp.start, cpEnd});
        }
    }
    return units;
}

// The matcher core, shared by both public entry points. A match has to
// cover whole source characters: text that folded into several units is
// only matched as a whole, so "a" does not match the single character
// "A with a ring" while "A with a ring" matches a decomposed "A plus
// ring".
static std::vector<TextMatch> MatchCore(const std::string& text,
                                        const std::string& query,
                                        SearchOptions options,
                                        bool rejectLineBreaks,
                                        SearchRangeFilter filter,
                                        void* context) {
    std::vector<TextMatch> result;
    if (query.empty()) return result;
    const std::vector<FoldUnit> haystack = FoldUnits(text, options.caseSensitive);
    const std::vector<FoldUnit> needle = FoldUnits(query, options.caseSensitive);
    if (needle.empty() || needle.size() > haystack.size()) return result;
    const std::vector<char> boundaries = BuildGraphemeBoundaryTable(text);

    for (std::size_t i = 0; i + needle.size() <= haystack.size();) {
        const std::size_t endIndex = i + needle.size();
        const FoldUnit& first = haystack[i];
        const FoldUnit& last = haystack[endIndex - 1];
        // The first and the last matched unit must open and close their
        // source character, or the candidate matches half a character.
        const bool startsChar = i == 0 || haystack[i - 1].start != first.start;
        const bool endsChar = endIndex == haystack.size() ||
                              haystack[endIndex].start != last.start;
        if (!startsChar || !endsChar) {
            ++i;
            continue;
        }
        bool equal = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (haystack[i + j].value != needle[j].value) {
                equal = false;
                break;
            }
        }
        if (!equal) {
            ++i;
            continue;
        }

        const uint32_t start = first.start;
        const uint32_t end = last.end;
        if (start >= boundaries.size() || !boundaries[start] ||
            end >= boundaries.size() || !boundaries[end]) {
            ++i;
            continue;
        }
        // A match never spans a line break, so it can never join the end
        // of one block with the start of the next (find-replace guide 19).
        if (rejectLineBreaks) {
            const std::size_t nl = text.find('\n', start);
            if (nl != std::string::npos && nl < end) {
                ++i;
                continue;
            }
        }
        if (options.wholeWord) {
            const bool leftOk = i == 0 || !IsWord(haystack[i - 1].value);
            const bool rightOk = endIndex == haystack.size() ||
                                 !IsWord(haystack[endIndex].value);
            if (!leftOk || !rightOk) {
                ++i;
                continue;
            }
        }
        if (filter && !filter(start, end - start, context)) {
            ++i;
            continue;
        }
        result.push_back({start, end - start});
        i = endIndex;
    }
    return result;
}

static bool IsWord(char32_t cp) {
    if ((cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') ||
        (cp >= U'0' && cp <= U'9') || cp == U'_') return true;
    if ((cp >= 0x00C0 && cp <= 0x02AF) ||
        (cp >= 0x0370 && cp <= 0x052F) ||
        (cp >= 0x1E00 && cp <= 0x1EFF) ||
        (cp >= 0x2C60 && cp <= 0x2C7F) ||
        (cp >= 0xA720 && cp <= 0xA7FF) ||
        (cp >= 0x10000 && cp <= 0x1EFFF) ||
        (cp >= 0x20000 && cp <= 0x2FA1F)) return true;
    return cp >= 0x0300 && cp <= 0x036F;
}


}  // namespace

bool IsSearchWordCharacter(unsigned char c) {
    // Kept for source compatibility with existing callers. New matching uses
    // the Unicode code-point classifier above.
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

std::vector<TextMatch> FindTextMatches(const std::string& text,
                                       const std::string& query,
                                       SearchOptions options) {
    return MatchCore(text, query, options, false, nullptr, nullptr);
}

std::vector<TextMatch> FindTextMatchesScoped(const std::string& text,
                                             const std::string& query,
                                             SearchOptions options,
                                             SearchRangeFilter filter,
                                             void* context) {
    return MatchCore(text, query, options, true, filter, context);
}

std::string ReplaceTextMatches(const std::string& text,
                               const std::vector<TextMatch>& matches,
                               const std::string& replacement) {
    if (matches.empty()) return text;
    std::string out;
    out.reserve(text.size());
    uint32_t cursor = 0;
    for (const TextMatch& match : matches) {
        if (match.start < cursor || match.start > text.size()) continue;
        const uint32_t remaining = static_cast<uint32_t>(text.size()) - match.start;
        const uint32_t end = match.length > remaining
            ? static_cast<uint32_t>(text.size())
            : match.start + match.length;
        out.append(text, cursor, match.start - cursor);
        out += replacement;
        cursor = end;
    }
    out.append(text, cursor, std::string::npos);
    return out;
}
