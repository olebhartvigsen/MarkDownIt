#pragma once

// FileWatcher: polls a file's last-write time every 500ms.
// On change, posts WM_USER_RELOAD to the HWND.
// Simple and reliable across all Windows versions and path types.

#include <windows.h>
#include <string>

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    void Start(HWND hwnd, const std::wstring& path);
    void Stop();

    static const UINT WM_USER_RELOAD = WM_USER + 1;

private:
    struct WatchState {
        HWND    hwnd = nullptr;
        std::wstring filePath;
        HANDLE  stopEvent = nullptr;
        HANDLE  thread    = nullptr;
        volatile LONG  running = 0;
        FILETIME lastWrite = {};
        bool     hasPrev = false;
    };

    WatchState state_;

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();
};
