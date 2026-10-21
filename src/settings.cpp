#include "settings.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

static const wchar_t* kKey = L"Software\\MarkDownIt";
static const wchar_t* kRecentSubkey = L"Software\\MarkDownIt\\RecentFiles";
static const int kMaxRecent = 12;

#ifdef _WIN32

AppSettings LoadSettings() {
    AppSettings s;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0,
            KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return s;

    DWORD val = 0, sz = sizeof(val), type = 0;

    if (RegQueryValueExW(hKey, L"FileAssoc", nullptr, &type,
            reinterpret_cast<BYTE*>(&val), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        s.fileAssoc = (val != 0);
    }

    val = 0; sz = sizeof(val); type = 0;
    if (RegQueryValueExW(hKey, L"ContentWidth", nullptr, &type,
            reinterpret_cast<BYTE*>(&val), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        s.contentWidthMode = static_cast<int>(val);
    }
    if (s.contentWidthMode < 0 || s.contentWidthMode > 3)
        s.contentWidthMode = 0;

    // Zoom factor: stored as a REG_DWORD holding the float bit pattern.
    // Missing or corrupt values fall back to the default (100%).
    val = 0; sz = sizeof(val); type = 0;
    if (RegQueryValueExW(hKey, L"ZoomFactor", nullptr, &type,
            reinterpret_cast<BYTE*>(&val), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        float zf = 0.0f;
        memcpy(&zf, &val, sizeof(zf));
        if (std::isfinite(zf)) {
            s.zoomFactor = zoom::Clamp(zf);
        }
    }

    // Outline pane: visible flag and width in DIP.
    val = 0; sz = sizeof(val); type = 0;
    if (RegQueryValueExW(hKey, L"OutlineVisible", nullptr, &type,
            reinterpret_cast<BYTE*>(&val), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        s.outlineVisible = (val != 0);
    }
    val = 0; sz = sizeof(val); type = 0;
    if (RegQueryValueExW(hKey, L"OutlineWidth", nullptr, &type,
            reinterpret_cast<BYTE*>(&val), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        float w = 0.0f;
        memcpy(&w, &val, sizeof(w));
        if (std::isfinite(w)) {
            if (w < 140.0f) w = 140.0f;
            if (w > 420.0f) w = 420.0f;
            s.outlineWidthDip = w;
        }
    }

    RegCloseKey(hKey);

    // Load recent files from HKCU\Software\MarkDownIt\RecentFiles
    HKEY hRecent = nullptr;
    LONG rc2 = RegOpenKeyExW(HKEY_CURRENT_USER, kRecentSubkey, 0,
            KEY_QUERY_VALUE, &hRecent);
    if (rc2 == ERROR_SUCCESS) {
        for (int i = 0; i < kMaxRecent; ++i) {
            wchar_t name[16];
            wsprintfW(name, L"File%d", i);
            DWORD pathSz = 0, pathType = 0;
            // First query: get size
            LONG rc = RegQueryValueExW(hRecent, name, nullptr, &pathType,
                nullptr, &pathSz);
            if (rc != ERROR_SUCCESS) continue;
            if (pathType != REG_SZ) continue;
            // Allocate and read
            std::wstring path(pathSz / 2, L'\0');
            pathSz = static_cast<DWORD>(path.size() * 2);
            rc = RegQueryValueExW(hRecent, name, nullptr, &pathType,
                reinterpret_cast<BYTE*>(path.data()), &pathSz);
            if (rc != ERROR_SUCCESS) continue;
            // Trim trailing null
            if (!path.empty() && path.back() == L'\0') path.pop_back();

            // Load preview
            wchar_t prevName[16];
            wsprintfW(prevName, L"Preview%d", i);
            DWORD prevSz = 0, prevType = 0;
            rc = RegQueryValueExW(hRecent, prevName, nullptr, &prevType,
                nullptr, &prevSz);
            std::string preview;
            if (rc == ERROR_SUCCESS && prevType == REG_BINARY) {
                preview.resize(prevSz);
                prevSz = static_cast<DWORD>(preview.size());
                RegQueryValueExW(hRecent, prevName, nullptr, &prevType,
                    reinterpret_cast<BYTE*>(preview.data()), &prevSz);
            }
            s.recentFiles.push_back({path, preview});
        }
        RegCloseKey(hRecent);
    }

    return s;
}

void SaveSettings(const AppSettings& s) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;

    DWORD fa = s.fileAssoc ? 1u : 0u;
    RegSetValueExW(hKey, L"FileAssoc", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&fa), sizeof(fa));

    DWORD cw = static_cast<DWORD>(s.contentWidthMode);
    RegSetValueExW(hKey, L"ContentWidth", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&cw), sizeof(cw));

    // Store the zoom factor bit pattern as a REG_DWORD (same layout as
    // the load path above).
    float zf = s.zoomFactor;
    DWORD zBits = 0;
    memcpy(&zBits, &zf, sizeof(zBits));
    RegSetValueExW(hKey, L"ZoomFactor", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&zBits), sizeof(zBits));

    DWORD ov = s.outlineVisible ? 1u : 0u;
    RegSetValueExW(hKey, L"OutlineVisible", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&ov), sizeof(ov));

    float owf = s.outlineWidthDip;
    DWORD ow = 0;
    memcpy(&ow, &owf, sizeof(ow));
    RegSetValueExW(hKey, L"OutlineWidth", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&ow), sizeof(ow));

    RegCloseKey(hKey);

    // Save recent files to HKCU\Software\MarkDownIt\RecentFiles
    HKEY hRecent = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, kRecentSubkey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &hRecent, nullptr);
    if (!hRecent) return;

    // Clear old entries first (write empty strings for any removed files)
    for (int i = 0; i < kMaxRecent; ++i) {
        wchar_t name[16];
        wsprintfW(name, L"File%d", i);
        RegDeleteValueW(hRecent, name);
        wsprintfW(name, L"Preview%d", i);
        RegDeleteValueW(hRecent, name);
    }

    for (size_t i = 0; i < s.recentFiles.size() && i < kMaxRecent; ++i) {
        wchar_t name[16];
        wsprintfW(name, L"File%d", static_cast<int>(i));
        const std::wstring& path = s.recentFiles[i].path;
        RegSetValueExW(hRecent, name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(path.c_str()),
            static_cast<DWORD>((path.size() + 1) * 2));

        wsprintfW(name, L"Preview%d", static_cast<int>(i));
        const std::string& prev = s.recentFiles[i].preview;
        if (!prev.empty()) {
            RegSetValueExW(hRecent, name, 0, REG_BINARY,
                reinterpret_cast<const BYTE*>(prev.data()),
                static_cast<DWORD>(prev.size()));
        }
    }

    RegCloseKey(hRecent);
}

#endif  // _WIN32

namespace {

// Whether two paths are the same, ignoring case. The comparison stops at
// the first difference and treats a shorter path as different from a
// longer one, so it never reads past the end of either string.
bool EqualPathCaseInsensitive(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i];
        wchar_t cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb - L'A' + L'a');
        if (ca != cb) return false;
    }
    return true;
}

}  // namespace

void AddRecentFile(AppSettings& s, const std::wstring& path) {
    // Remove existing entry for the same path (dedupe). The comparison
    // is case insensitive, because a path is the same file whichever
    // case it is typed in, and the Win32 call that used to do this is
    // not available on every platform.
    for (auto it = s.recentFiles.begin(); it != s.recentFiles.end(); ++it) {
        if (EqualPathCaseInsensitive(it->path, path)) {
            s.recentFiles.erase(it);
            break;
        }
    }

    // Read first ~300 bytes for preview
    std::string preview;
    std::ifstream f(path.c_str(), std::ios::binary);
    if (f.is_open()) {
        char buf[310] = {};
        f.read(buf, 300);
        preview.assign(buf, static_cast<size_t>(f.gcount()));
    }

    s.recentFiles.insert(s.recentFiles.begin(), {path, preview});
    if (s.recentFiles.size() > kMaxRecent)
        s.recentFiles.resize(kMaxRecent);
}
