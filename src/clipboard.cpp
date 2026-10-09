#include "clipboard.h"
#include <vector>
#include <algorithm>
#include <climits>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

const char* kMarkdownFormatName = "MarkDownIt Markdown";

#ifdef _WIN32
// Read wide text within the clipboard allocation.
static bool ReadClipboardWide(HGLOBAL hData, std::wstring* out) {
    if (!hData || !out) return false;
    const SIZE_T bytes = GlobalSize(hData);
    if (bytes < sizeof(wchar_t)) return false;
    const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!data) return false;
    const size_t maxLen = bytes / sizeof(wchar_t);
    size_t len = 0;
    while (len < maxLen && data[len] != L'\0') ++len;
    out->assign(data, len);
    GlobalUnlock(hData);
    return true;
}
#endif

// --- Escape markdown metacharacters ---
std::string EscapeMarkdown(const std::string& text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        // Markdown permits escaping all ASCII punctuation. Escape it so
        // lists, HTML, entities and automatic links also remain literal.
        const unsigned char byte = static_cast<unsigned char>(c);
        if ((byte >= 0x21 && byte <= 0x2F) ||
            (byte >= 0x3A && byte <= 0x40) ||
            (byte >= 0x5B && byte <= 0x60) ||
            (byte >= 0x7B && byte <= 0x7E)) {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// --- HTML to Markdown converter ---
// A deliberately small tag scanner. No full HTML parser; handles the
// tags a browser actually puts on the clipboard for rich text.

struct HtmlTag {
    std::string name;       // lowercase tag name, e.g. "b", "h1", "a"
    bool closing;           // true for </tag>
    std::string attrs;       // attribute string (for links)
    bool selfClosing;       // <br/>, <hr/>
};

static HtmlTag parseTag(const char* &p, const char* end) {
    HtmlTag tag{};
    p++; // skip <
    if (p < end && *p == '/') { tag.closing = true; p++; }
    while (p < end && *p != ' ' && *p != '>' && *p != '/') {
        tag.name += static_cast<char>(tolower(*p));
        p++;
    }
    // Read attributes until > or />
    while (p < end && *p != '>') {
        if (*p == '/' && p + 1 < end && *(p + 1) == '>') {
            tag.selfClosing = true;
            p += 2;
            return tag;
        }
        tag.attrs += *p;
        p++;
    }
    if (p < end && *p == '>') p++;
    return tag;
}

std::string HtmlToMarkdown(const std::string& html) {
    std::string out;
    const char* p = html.c_str();
    const char* end = p + html.size();

    // Track open formatting tags
    bool inBold = false, inItalic = false, inCode = false;
    bool inPre = false;
    int headingLevel = 0;
    bool atLineStart = true;
    // Stack of hrefs for nested <a> elements; each </a> closes with the
    // href pushed by its opener.
    std::vector<std::string> hrefStack;

    while (p < end) {
        if (*p == '<') {
            HtmlTag tag = parseTag(p, end);

            if (tag.name == "b" || tag.name == "strong") {
                if (tag.closing) { out += "**"; inBold = false; }
                else { out += "**"; inBold = true; }
            } else if (tag.name == "i" || tag.name == "em") {
                if (tag.closing) { out += "*"; inItalic = false; }
                else { out += "*"; inItalic = true; }
            } else if (tag.name == "code") {
                if (tag.closing) { out += "`"; inCode = false; }
                else { out += "`"; inCode = true; }
            } else if (tag.name == "pre") {
                if (tag.closing) { out += "\n```\n"; inPre = false; }
                else { out += "```\n"; inPre = true; }
            } else if (tag.name == "br") {
                out += "\n";
                atLineStart = true;
            } else if (tag.name == "hr") {
                out += "\n---\n";
                atLineStart = true;
            } else if (tag.name.size() == 2 && tag.name[0] == 'h' &&
                       tag.name[1] >= '1' && tag.name[1] <= '6') {
                if (tag.closing) {
                    out += "\n\n";
                    headingLevel = 0;
                } else {
                    headingLevel = tag.name[1] - '0';
                    for (int i = 0; i < headingLevel; i++) out += "#";
                    out += " ";
                }
                atLineStart = false;
            } else if (tag.name == "a") {
                if (tag.closing) {
                    // Close with ](href): pop the href pushed by the opener.
                    std::string href =
                        hrefStack.empty() ? "" : hrefStack.back();
                    if (!hrefStack.empty()) hrefStack.pop_back();
                    out += "](" + href + ")";
                } else {
                    // Extract href from attrs and push it for the closer.
                    std::string href;
                    size_t hpos = tag.attrs.find("href=\"");
                    if (hpos != std::string::npos) {
                        hpos += 6;
                        while (hpos < tag.attrs.size() && tag.attrs[hpos] != '"') {
                            href += tag.attrs[hpos];
                            hpos++;
                        }
                    }
                    hrefStack.push_back(href);
                    out += "[";
                }
            } else if (tag.name == "ul" || tag.name == "ol") {
                if (!tag.closing) out += "\n";
            } else if (tag.name == "li") {
                if (!tag.closing) {
                    if (atLineStart) out += "- ";
                    else out += "\n- ";
                    atLineStart = false;
                } else {
                    out += "\n";
                    atLineStart = true;
                }
            } else if (tag.name == "blockquote") {
                if (!tag.closing) out += "\n> ";
                else out += "\n";
            } else if (tag.name == "p") {
                if (!tag.closing && !out.empty() && out.back() != '\n')
                    out += "\n\n";
                else if (tag.closing)
                    out += "\n\n";
                atLineStart = true;
            } else if (tag.name == "del" || tag.name == "s" || tag.name == "strike") {
                if (tag.closing) out += "~~"; else out += "~~";
            } else if (tag.name == "span" || tag.name == "div") {
                // Ignore: just pass through text
            } else {
                // Unknown tag: skip
            }
            continue;
        }

        // Regular text content
        if (*p == '&') {
            // HTML entity
            if (strncmp(p, "&amp;", 5) == 0) { out += '&'; p += 5; continue; }
            if (strncmp(p, "&lt;", 4) == 0) { out += '<'; p += 4; continue; }
            if (strncmp(p, "&gt;", 4) == 0) { out += '>'; p += 4; continue; }
            if (strncmp(p, "&quot;", 6) == 0) { out += '"'; p += 6; continue; }
            if (strncmp(p, "&nbsp;", 6) == 0) { out += ' '; p += 6; continue; }
            if (strncmp(p, "&#39;", 5) == 0) { out += '\''; p += 5; continue; }
        }

        out += *p;
        if (*p == '\n') atLineStart = true;
        else atLineStart = false;
        p++;
    }

    return out;
}

#ifdef _WIN32
static std::string NormalizeToCrlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x0D) {
            out.push_back(static_cast<char>(0x0D));
            if (i + 1 >= text.size() ||
                static_cast<unsigned char>(text[i + 1]) != 0x0A) {
                out.push_back(static_cast<char>(0x0A));
            }
        } else if (c == 0x0A) {
            out.push_back(static_cast<char>(0x0D));
            out.push_back(static_cast<char>(0x0A));
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

static std::string NormalizeToLf(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x0D) {
            if (i + 1 < text.size() &&
                static_cast<unsigned char>(text[i + 1]) == 0x0A) {
                ++i;
            }
            out.push_back(static_cast<char>(0x0A));
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

// --- Win32 clipboard operations ---

static std::string WideToUtf8(const wchar_t* wide, int len) {
    if (len <= 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, len, nullptr, 0, nullptr, nullptr);
    std::string out(needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, len, &out[0], needed, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     utf8.data(), static_cast<int>(utf8.size()),
                                     nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      utf8.data(), static_cast<int>(utf8.size()),
                                      wide.data(), needed);
    if (written != needed) return {};
    return wide;
}

bool ClipboardCopy(HWND hwnd, const std::string& utf8) {
    const std::string unicodeText = NormalizeToCrlf(utf8);
    std::wstring wide = Utf8ToWide(unicodeText);
    if (!unicodeText.empty() && wide.empty()) return false;

    UINT customFmt = RegisterClipboardFormatA(kMarkdownFormatName);
    if (!customFmt) return false;

    const size_t wideBytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL hWide = GlobalAlloc(GMEM_MOVEABLE, wideBytes);
    HGLOBAL hCustom = GlobalAlloc(GMEM_MOVEABLE, utf8.size() + 1);
    if (!hWide || !hCustom) {
        if (hWide) GlobalFree(hWide);
        if (hCustom) GlobalFree(hCustom);
        return false;
    }

    void* wideDst = GlobalLock(hWide);
    void* customDst = GlobalLock(hCustom);
    if (!wideDst || !customDst) {
        if (wideDst) GlobalUnlock(hWide);
        if (customDst) GlobalUnlock(hCustom);
        GlobalFree(hWide);
        GlobalFree(hCustom);
        return false;
    }
    memcpy(wideDst, wide.c_str(), wideBytes);
    memcpy(customDst, utf8.c_str(), utf8.size() + 1);
    GlobalUnlock(hWide);
    GlobalUnlock(hCustom);

    if (!OpenClipboard(hwnd)) {
        GlobalFree(hWide);
        GlobalFree(hCustom);
        return false;
    }

    bool success = false;
    if (EmptyClipboard()) {
        // Clipboard owns a handle only after SetClipboardData succeeds.
        if (SetClipboardData(CF_UNICODETEXT, hWide)) {
            hWide = nullptr;
            if (SetClipboardData(customFmt, hCustom)) {
                hCustom = nullptr;
                success = true;
            }
        }
    }
    if (hWide) GlobalFree(hWide);
    if (hCustom) GlobalFree(hCustom);
    CloseClipboard();
    return success;
}

bool ClipboardCut(HWND hwnd, const std::string& utf8) {
    return ClipboardCopy(hwnd, utf8);
}

// Read the plain-text clipboard only. rawOut (optional) receives the
// unescaped text for structural conversion (Excel tabs and quotes).
static std::string PasteImpl(HWND hwnd, std::string* rawOut) {
    if (!OpenClipboard(hwnd)) return {};
    std::string result;

    // Use only the plain-text representation. Registered format names do not
    // prove origin, so custom Markdown, HTML, RTF and objects are ignored.
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HGLOBAL hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            std::wstring wide;
            if (ReadClipboardWide(hData, &wide) &&
                wide.size() <= static_cast<size_t>(INT_MAX)) {
                result = EscapeMarkdown(WideToUtf8(wide.c_str(),
                    static_cast<int>(wide.size())));
                if (rawOut) {
                    *rawOut = WideToUtf8(wide.c_str(),
                        static_cast<int>(wide.size()));
                }
            }
        }
    }

    result = NormalizeToLf(result);
    CloseClipboard();
    return result;
}

std::string ClipboardPaste(HWND hwnd) {
    return PasteImpl(hwnd, nullptr);
}

std::string ClipboardPasteRaw(HWND hwnd, std::string* rawOut) {
    const std::string escaped = PasteImpl(hwnd, rawOut);
    if (rawOut) *rawOut = NormalizeToLf(*rawOut);
    return escaped;
}

#endif // _WIN32
