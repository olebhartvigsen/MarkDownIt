#include "searchreplace.h"
#include "editcontroller.h"

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

static char32_t Fold(char32_t cp) {
    if (cp >= U'A' && cp <= U'Z') return cp + (U'a' - U'A');
    if (cp >= 0x00C0 && cp <= 0x00D6) return cp + 0x20;
    if (cp >= 0x00D8 && cp <= 0x00DE) return cp + 0x20;
    if (cp >= 0x0391 && cp <= 0x03AB) return cp + 0x20;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    return cp;
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
    std::vector<TextMatch> result;
    if (query.empty()) return result;
    const std::vector<CodePoint> haystack = Decode(text);
    const std::vector<CodePoint> needle = Decode(query);
    if (needle.empty() || needle.size() > haystack.size()) return result;

    for (std::size_t i = 0; i + needle.size() <= haystack.size();) {
        const uint32_t candidateStart = haystack[i].start;
        const std::size_t endIndex = i + needle.size();
        const uint32_t candidateEnd = endIndex == haystack.size()
            ? static_cast<uint32_t>(text.size())
            : haystack[endIndex].start;
        if (!::IsGraphemeBoundary(text, candidateStart) ||
            !::IsGraphemeBoundary(text, candidateEnd)) {
            ++i;
            continue;
        }
        bool equal = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            const char32_t a = haystack[i + j].value;
            const char32_t b = needle[j].value;
            if ((options.caseSensitive ? a : Fold(a)) !=
                (options.caseSensitive ? b : Fold(b))) {
                equal = false;
                break;
            }
        }
        if (!equal) {
            ++i;
            continue;
        }
        const bool leftOk = i == 0 || !IsWord(haystack[i - 1].value);
        const bool rightOk = endIndex == haystack.size() ||
                             !IsWord(haystack[endIndex].value);
        if (!options.wholeWord || (leftOk && rightOk)) {
            const uint32_t start = haystack[i].start;
            const uint32_t end = endIndex == haystack.size()
                ? static_cast<uint32_t>(text.size())
                : haystack[endIndex].start;
            result.push_back({start, end - start});
            i = endIndex;
        } else {
            ++i;
        }
    }
    return result;
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
