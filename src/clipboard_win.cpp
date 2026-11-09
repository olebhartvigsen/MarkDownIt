// The Win32 clipboard half of the shared clipboard interface.
//
// The markdown escaping, the HTML conversion and the line ending
// normalizers are shared (src/clipboard.cpp). This file moves bytes in
// and out of the Windows clipboard, and src/macos/clipboard_mac.mm
// does the same for the other platform.

#include "clipboard.h"
#include <climits>
#include <cstring>

#include <windows.h>

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
