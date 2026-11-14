#pragma once

// FileWatcher: polls a file's last-write time and length every 500ms.
// On a change it posts WM_USER_RELOAD to the HWND, and the window asks
// watch::Tracker what the change means (filewatch_model.h).
// Simple and reliable across all Windows versions and path types.

#include <windows.h>

#include <cstdint>
#include <string>

#include "filewatch_model.h"

// The stamp the disk reports for a path, in the same terms the mac reader
// uses: bytes, and the modification time in 100 ns ticks. `valid` is false
// when the file cannot be read, which the policy treats as "no news".
watch::Stamp CurrentFileStamp(const std::wstring& path);

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
        uint64_t lastSize = 0;
        bool     hasPrev = false;
    };

    WatchState state_;

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();
};
