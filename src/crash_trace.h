// Crash breadcrumbs: append one line per stage and flush immediately,
// so the last line names the function that ran when a silent CTD hit.
#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>

#if defined(_WIN32)

// The Windows headers come before the diag namespace on purpose. The
// SDK has a namespace of its own called diag, with COM wrappers in it,
// and including the headers inside a namespace diag would put those
// wrappers in this namespace, where they cannot find the global
// IStream they derive from.
#include <windows.h>
#include <shlobj.h>

#endif  // _WIN32

namespace diag {

#if defined(_WIN32)

namespace detail {

// Resolve the log file, create the directory, and return a handle
// positioned at the end of the file. Returns INVALID_HANDLE_VALUE when
// the log cannot be opened.
inline HANDLE OpenLog() {
    char path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                path)))
        return INVALID_HANDLE_VALUE;
    std::string dir = path;
    dir += "\\MarkDownIt";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string file = dir + "\\crash_trace.log";
    HANDLE h = CreateFileA(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return h;
    SetFilePointer(h, 0, nullptr, FILE_END);
    return h;
}

// Format the timestamp and thread id prefix shared by every line.
inline int FormatPrefix(char* out, size_t outSize, const char* body) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    return _snprintf_s(out, outSize, _TRUNCATE,
                       "%02u:%02u:%02u.%03u %lu %s\r\n", st.wHour,
                       st.wMinute, st.wSecond, st.wMilliseconds,
                       (unsigned long)GetCurrentThreadId(), body);
}

}  // namespace detail

inline void TraceFmt(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    int n = _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
    va_end(args);
    if (n <= 0) return;
    HANDLE h = detail::OpenLog();
    if (h == INVALID_HANDLE_VALUE) return;
    char out[640];
    int m = detail::FormatPrefix(out, sizeof(out), line);
    if (m > 0) {
        DWORD written = 0;
        WriteFile(h, out, (DWORD)m, &written, nullptr);
    }
    CloseHandle(h);
}

inline void Trace(const char* stage) {
    HANDLE h = detail::OpenLog();
    if (h == INVALID_HANDLE_VALUE) return;
    char out[640];
    int n = detail::FormatPrefix(out, sizeof(out), stage);
    if (n > 0) {
        DWORD written = 0;
        WriteFile(h, out, (DWORD)n, &written, nullptr);
    }
    CloseHandle(h);
}

#else  // !_WIN32

// Portable fallback used by the macOS build and by the portable test
// target. The log lives in the user's home directory so the breadcrumbs
// survive a crash without needing any platform specific API.
namespace detail {

inline std::string LogPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return std::string();
    std::string dir = home;
    dir += "/.markdownit";
    // Create the directory if it is not there yet. Failures are ignored:
    // the log is a diagnostic aid, not a hard requirement.
    std::string cmd = "mkdir -p '" + dir + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        // Fall through: the append below will simply fail if the
        // directory does not exist.
    }
    return dir + "/crash_trace.log";
}

inline int FormatPrefix(char* out, size_t outSize, const char* body) {
    return std::snprintf(out, outSize, "%s\n", body);
}

}  // namespace detail

inline void Trace(const char* stage) {
    const std::string path = detail::LogPath();
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    char out[640];
    const int m = detail::FormatPrefix(out, sizeof(out), stage);
    if (m > 0) std::fwrite(out, 1, static_cast<size_t>(m), f);
    std::fclose(f);
}

inline void TraceFmt(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0) return;
    Trace(line);
}

#endif  // _WIN32

}  // namespace diag
