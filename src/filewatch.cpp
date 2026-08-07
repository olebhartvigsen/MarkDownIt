#include "filewatch.h"

#include <windows.h>
#include <string>

FileWatcher::FileWatcher() {}
FileWatcher::~FileWatcher() { Stop(); }

void FileWatcher::Stop() {
    if (!state_.running) {
        if (state_.stopEvent) { CloseHandle(state_.stopEvent); state_.stopEvent = nullptr; }
        return;
    }
    InterlockedExchange(&state_.running, 0);
    if (state_.stopEvent) SetEvent(state_.stopEvent);
    if (state_.thread) {
        WaitForSingleObject(state_.thread, 2000);
        CloseHandle(state_.thread);
        state_.thread = nullptr;
    }
    if (state_.stopEvent) { CloseHandle(state_.stopEvent); state_.stopEvent = nullptr; }
}

void FileWatcher::Start(HWND hwnd, const std::wstring& path) {
    Stop();
    if (path.empty()) return;

    state_.hwnd     = hwnd;
    state_.filePath = path;
    state_.hasPrev  = false;

    // Record current last-write time so we do not fire on startup.
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        state_.lastWrite = fad.ftLastWriteTime;
        state_.hasPrev = true;
    }

    state_.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state_.stopEvent) return;

    InterlockedExchange(&state_.running, 1);
    state_.thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!state_.thread) {
        InterlockedExchange(&state_.running, 0);
        CloseHandle(state_.stopEvent);
        state_.stopEvent = nullptr;
    }
}

DWORD WINAPI FileWatcher::ThreadProc(LPVOID param) {
    static_cast<FileWatcher*>(param)->Run();
    return 0;
}

void FileWatcher::Run() {
    while (state_.running) {
        // Sleep 500ms, wake early if stop event is signalled.
        DWORD result = WaitForSingleObject(state_.stopEvent, 500);
        if (result == WAIT_OBJECT_0) break;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(state_.filePath.c_str(),
                                   GetFileExInfoStandard, &fad)) {
            continue;  // file might be temporarily locked during save
        }

        FILETIME current = fad.ftLastWriteTime;
        if (state_.hasPrev) {
            if (CompareFileTime(&current, &state_.lastWrite) > 0) {
                state_.lastWrite = current;
                // Small debounce: editors write in two bursts.
                Sleep(100);
                if (state_.running) {
                    PostMessageW(state_.hwnd, WM_USER_RELOAD, 0, 0);
                }
            }
        } else {
            state_.lastWrite = current;
            state_.hasPrev = true;
        }
    }
}
