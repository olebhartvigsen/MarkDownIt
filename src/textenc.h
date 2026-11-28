#pragma once

#include <cstdint>
#include <string>

// Portable UTF conversions shared by the platform-independent code.
//
// The find bar's model speaks UTF-8 bytes, because that is the document
// encoding, while the text fields speak the platform's wide string. The
// Windows implementation used the Win32 conversion API directly; this
// header does the same job without a platform API, so the macOS build can
// share the model unchanged.
//
// On Windows wchar_t is 16 bits, on macOS it is 32 bits. Both are
// handled: the conversion goes through UTF-16 code units, and a 32-bit
// wide string is treated as a sequence of Unicode code points.

namespace textenc {

// Encode a wide string as UTF-8.
inline std::string WideToUtf8(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const uint32_t cp = static_cast<uint32_t>(text[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            const uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                const uint32_t code = 0x10000 + ((cp - 0xD800) << 10) +
                                      (low - 0xDC00);
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                ++i;
                continue;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Decode a UTF-8 string into a wide string.
inline std::wstring Utf8ToWide(const std::string& text) {
    std::wstring out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            // A stray continuation byte: keep it as a replacement so the
            // conversion never loses the rest of the string.
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }
        if (i + extra >= text.size()) {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            break;
        }
        bool valid = true;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!valid) {
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }
        i += extra + 1;
        if (cp >= 0x10000 && sizeof(wchar_t) == 2) {
            const uint32_t v = cp - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

// Encode code points as UTF-16 code units, the unit a seam text layout
// speaks. A code point above the basic plane becomes its surrogate pair.
inline std::u16string Utf32ToUtf16(const std::u32string& text) {
    std::u16string out;
    out.reserve(text.size());
    for (char32_t cp : text) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

// Decode UTF-8 into UTF-16 code units. A malformed sequence advances one
// byte at a time and never drops the rest of the string, the rule the
// renderer's own conversion always kept.
inline std::u16string Utf8ToUtf16(const std::string& text) {
    std::u16string out;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char16_t>(c);
            i += 1;
        } else if (c < 0xC0) {
            i += 1;  // skip continuation byte
        } else if (c < 0xE0) {
            if (i + 1 < text.size()) {
                const char16_t ch = ((c & 0x1F) << 6) |
                    (static_cast<unsigned char>(text[i + 1]) & 0x3F);
                out += ch;
                i += 2;
            } else {
                i += 1;
            }
        } else if (c < 0xF0) {
            if (i + 2 < text.size()) {
                const char16_t ch = ((c & 0x0F) << 12) |
                    ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(text[i + 2]) & 0x3F);
                out += ch;
                i += 3;
            } else {
                i += 1;
            }
        } else {
            if (i + 3 < text.size()) {
                uint32_t cp = ((c & 0x07) << 18) |
                    ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                    ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(text[i + 3]) & 0x3F);
                cp -= 0x10000;
                out += static_cast<char16_t>(0xD800 + (cp >> 10));
                out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
                i += 4;
            } else {
                i += 1;
            }
        }
    }
    return out;
}

}  // namespace textenc
