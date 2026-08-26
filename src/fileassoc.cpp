#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <string>
#include "fileassoc.h"

static const wchar_t* kProgId  = L"MarkDownIt.md";
static const wchar_t* kExts[]   = { L".md", L".markdown" };

// Write a string value to a registry key.
static bool SetStr(HKEY root, const wchar_t* sub, const wchar_t* val,
                   const wchar_t* data) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, sub, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    DWORD len = (DWORD)(wcslen(data) + 1) * sizeof(wchar_t);
    bool ok = RegSetValueExW(hKey, val, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(data), len) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return ok;
}

// Remove the default value of a key (used during unregister to clear
// the .md/.markdown default without deleting the key itself).
static void ClearDefault(HKEY root, const wchar_t* sub) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, nullptr);
        RegCloseKey(hKey);
    }
}

// Read the default (unnamed) string value of a key. Returns false when
// the key or value is missing.
static bool GetStr(HKEY root, const wchar_t* sub, std::wstring& out) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    wchar_t buf[256] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    bool ok = RegQueryValueExW(hKey, nullptr, nullptr, &type,
        reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS &&
        type == REG_SZ;
    RegCloseKey(hKey);
    if (ok) out.assign(buf);
    return ok;
}

// Write a DWORD value to a registry key (used for OpenWithProgids).
static bool SetDword(HKEY root, const wchar_t* sub, const wchar_t* val,
                     DWORD data) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, sub, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    (void)RegSetValueExW(hKey, val, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(hKey);
    return true;
}

// Delete a key with all its subkeys (recursive).
static void DeleteTree(HKEY root, const wchar_t* sub) {
    SHDeleteKeyW(root, sub);
}

void RegisterMdAssociation(const wchar_t* exePath) {
    // Build open command and icon path once.
    std::wstring openCmd = L"\"";
    openCmd += exePath;
    openCmd += L"\" \"%1\"";

    // Use negative resource ID to reference the icon by its ID (IDI_DOCICON=2)
    // rather than by 0-based index, which can shift if other icon resources are
    // added to the binary. Windows DefaultIcon syntax: "path,-resourceID".
    std::wstring iconPath = L"\"";
    iconPath += exePath;
    iconPath += L"\",-2";

    // --- Register the ProgID: MarkDownIt.md ---
    SetStr(HKEY_CURRENT_USER,
        L"Software\\Classes\\MarkDownIt.md",
        nullptr,
        L"Markdown Document");
    SetStr(HKEY_CURRENT_USER,
        L"Software\\Classes\\MarkDownIt.md\\DefaultIcon",
        nullptr,
        iconPath.c_str());
    SetStr(HKEY_CURRENT_USER,
        L"Software\\Classes\\MarkDownIt.md\\shell",
        nullptr,
        L"open");
    SetStr(HKEY_CURRENT_USER,
        L"Software\\Classes\\MarkDownIt.md\\shell\\open\\command",
        nullptr,
        openCmd.c_str());

    // --- Associate each extension with the ProgID ---
    for (const wchar_t* ext : kExts) {
        // Build "\\Software\\Classes\\<ext>" path
        std::wstring extKey = L"Software\\Classes\\";
        extKey += ext;
        // Save any existing default association so unregister can
        // restore it instead of leaving the extension unassociated.
        std::wstring prevKey = L"Software\\Classes\\MarkDownIt.md\\PreviousDefault";
        std::wstring prevDefault;
        bool hadPrev = GetStr(HKEY_CURRENT_USER, extKey.c_str(), prevDefault);
        if (hadPrev && prevDefault != kProgId) {
            SetStr(HKEY_CURRENT_USER, prevKey.c_str(), ext, prevDefault.c_str());
        }

        // Point the extension's default to our ProgID.
        SetStr(HKEY_CURRENT_USER, extKey.c_str(), nullptr, kProgId);

        // OpenWithProgids makes us appear in the "Open with" list.
        std::wstring progIdKey = extKey + L"\\OpenWithProgids";
        SetDword(HKEY_CURRENT_USER, progIdKey.c_str(), kProgId, 0);
    }

    // Notify the shell that associations changed.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void UnregisterMdAssociation() {
    // Remove the ProgID entirely.
    DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\MarkDownIt.md");

    // Remove our ProgID reference from each extension.
    // Do NOT delete the extension key itself, other apps may use it.
    for (const wchar_t* ext : kExts) {
        std::wstring progIdKey = L"Software\\Classes\\";
        progIdKey += ext;
        progIdKey += L"\\OpenWithProgids";

        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, progIdKey.c_str(), 0,
                KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, kProgId);
            RegCloseKey(hKey);
        }

        std::wstring extKey = L"Software\\Classes\\";
        extKey += ext;

        // Restore the association that existed before registration,
        // if we saved one; otherwise clear so Windows falls back.
        std::wstring prevKey = L"Software\\Classes\\MarkDownIt.md\\PreviousDefault\\";
        prevKey += ext;
        std::wstring prevDefault;
        if (GetStr(HKEY_CURRENT_USER, prevKey.c_str(), prevDefault)) {
            SetStr(HKEY_CURRENT_USER, extKey.c_str(), nullptr, prevDefault.c_str());
            DeleteTree(HKEY_CURRENT_USER,
                L"Software\\Classes\\MarkDownIt.md\\PreviousDefault");
        } else {
            ClearDefault(HKEY_CURRENT_USER, extKey.c_str());
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool IsMdRegistered() {
    // Check whether .md's default value points to our ProgID.
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.md", 0,
            KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t buf[64] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    bool found = false;
    if (RegQueryValueExW(hKey, nullptr, nullptr, &type,
            reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS &&
        type == REG_SZ) {
        found = (wcsncmp(buf, kProgId, wcslen(kProgId)) == 0);
    }
    RegCloseKey(hKey);
    return found;
}
