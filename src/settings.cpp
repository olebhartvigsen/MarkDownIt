#include "settings.h"

static const wchar_t* kKey = L"Software\\MarkDownIt";

AppSettings LoadSettings() {
    AppSettings s;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0,
            KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return s;  // defaults: fileAssoc=true, contentWidthMode=0

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

    RegCloseKey(hKey);
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

    RegCloseKey(hKey);
}
