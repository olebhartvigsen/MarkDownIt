// File association: register .md files so Explorer shows the MarkDownIt
// document icon and double-click opens the file in MarkDownIt.
//
// Per-user (HKCU), no admin rights needed. Later a settings toggle
// can call UnregisterMdAssociation() to undo.

#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <string>
#include "fileassoc.h"

static const wchar_t* kProgId  = L"MarkDownIt.md";
static const wchar_t* kExt     = L".md";

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
    // SHDeleteKeyW does recursive delete in one call.
    SHDeleteKeyW(root, sub);
}

void RegisterMdAssociation(const wchar_t* exePath) {
    // Build "exePath" "%1" for the open command.
    std::wstring openCmd = L"\"";
    openCmd += exePath;
    openCmd += L"\" \"%1\"";

    // Build "exePath,1" for DefaultIcon (resource index 1 = IDI_DOCICON).
    std::wstring iconPath = L"\"";
    iconPath += exePath;
    iconPath += L"\",1";

    // --- Register the ProgID: MarkDownIt.md ---
    // FriendlyTypeName shown in Explorer's Type column.
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

    // --- Associate .md extension with the ProgID ---
    // Point .md's default to our ProgID so we become the handler.
    SetStr(HKEY_CURRENT_USER,
        L"Software\\Classes\\.md",
        nullptr,
        kProgId);
    // OpenWithProgids makes us appear in the "Open with" list.
    SetDword(HKEY_CURRENT_USER,
        L"Software\\Classes\\.md\\OpenWithProgids",
        kProgId,
        0);

    // Notify the shell that associations changed so Explorer refreshes
    // its icon cache immediately.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void UnregisterMdAssociation() {
    // Remove the ProgID entirely.
    DeleteTree(HKEY_CURRENT_USER, L"Software\\Classes\\MarkDownIt.md");
    // Remove our ProgID reference from the .md extension.
    // Do NOT delete HKCU\.md itself, other apps may use OpenWithProgids.
    // Clear the default value of .md so Windows falls back.
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\.md\\OpenWithProgids", 0,
            KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, kProgId);
        RegCloseKey(hKey);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
