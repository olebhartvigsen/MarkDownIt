#pragma once

// FileWatcher: monitors a file for changes via ReadDirectoryChangesW.
// Runs a background thread; on change, posts WM_USER_RELOAD to the HWND.
// Only one file is watched at a time. Restart with Start() for a new path.

#include <windows.h>
#include <string>

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    // Start watching `path` (a file). Posts WM_USER_RELOAD to `hwnd` on change.
    // If already watching, stop the old watch first.
    void Start(HWND hwnd, const std::wstring& path);

    // Stop watching and join the thread.
    void Stop();

    // Custom window message posted to hwnd when the file changes.
    static const UINT WM_USER_RELOAD = WM_USER + 1;

private:
    struct WatchState {
        HWND    hwnd;
        std::wstring filePath;
        std::wstring fileName;   // basename, for filtering
        HANDLE  dirHandle = nullptr;
        HANDLE  stopEvent = nullptr;
        HANDLE  thread    = nullptr;
        volatile LONG  running = 0;
    };

    WatchState state_;

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();
    static std::wstring Basename(const std::wstring& path);
};
