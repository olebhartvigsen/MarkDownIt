// Crash breadcrumbs: append one line per stage and flush immediately,
// so the last line names the function that ran when a silent CTD hit.
#pragma once

#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <string>

namespace diag {

inline void TraceFmt(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    int n = _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
    va_end(args);
    if (n <= 0) return;
    char path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                path)))
        return;
    std::string dir = path;
    dir += "\\MarkDownIt";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string file = dir + "\\crash_trace.log";

    HANDLE h = CreateFileA(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char out[640];
    int m = _snprintf_s(out, sizeof(out), _TRUNCATE,
                       "%02u:%02u:%02u.%03u %lu %.300s\r\n", st.wHour,
                       st.wMinute, st.wSecond, st.wMilliseconds,
                       (unsigned long)GetCurrentThreadId(), line);
    if (m > 0) {
        DWORD written = 0;
        WriteFile(h, out, (DWORD)m, &written, nullptr);
    }
    CloseHandle(h);
}

inline void Trace(const char* stage) {
    char path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                path)))
        return;
    std::string dir = path;
    dir += "\\MarkDownIt";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string file = dir + "\\crash_trace.log";

    HANDLE h = CreateFileA(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char line[512];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                       "%02u:%02u:%02u.%03u %lu %s\r\n", st.wHour, st.wMinute,
                       st.wSecond, st.wMilliseconds,
                       (unsigned long)GetCurrentThreadId(), stage);
    if (n > 0) {
        DWORD written = 0;
        WriteFile(h, line, (DWORD)n, &written, nullptr);
    }
    CloseHandle(h);
}

}  // namespace diag
