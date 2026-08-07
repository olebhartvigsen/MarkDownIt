#include "filewatch.h"

#include <windows.h>
#include <string>

FileWatcher::FileWatcher() {}
FileWatcher::~FileWatcher() { Stop(); }

std::wstring FileWatcher::Basename(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return (slash != std::wstring::npos) ? path.substr(slash + 1) : path;
}

void FileWatcher::Stop() {
    if (!state_.running) {
        if (state_.dirHandle) { CloseHandle(state_.dirHandle); state_.dirHandle = nullptr; }
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
    if (state_.dirHandle) { CloseHandle(state_.dirHandle); state_.dirHandle = nullptr; }
}

void FileWatcher::Start(HWND hwnd, const std::wstring& path) {
    Stop();
    if (path.empty()) return;

    state_.hwnd     = hwnd;
    state_.filePath = path;
    state_.fileName = Basename(path);

    // Open the directory containing the file (needs FILE_LIST_DIRECTORY).
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash != std::wstring::npos) ? path.substr(0, slash) : L".";
    if (dir.empty()) dir = L".";

    state_.dirHandle = CreateFileW(
        dir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (state_.dirHandle == INVALID_HANDLE_VALUE) {
        state_.dirHandle = nullptr;
        return;
    }

    state_.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!state_.stopEvent) {
        CloseHandle(state_.dirHandle);
        state_.dirHandle = nullptr;
        return;
    }

    InterlockedExchange(&state_.running, 1);
    state_.thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!state_.thread) {
        InterlockedExchange(&state_.running, 0);
        CloseHandle(state_.stopEvent);   state_.stopEvent = nullptr;
        CloseHandle(state_.dirHandle);  state_.dirHandle = nullptr;
    }
}

DWORD WINAPI FileWatcher::ThreadProc(LPVOID param) {
    static_cast<FileWatcher*>(param)->Run();
    return 0;
}

void FileWatcher::Run() {
    const DWORD bufSize = 4096;
    BYTE buffer[4096];
    HANDLE waits[2] = { state_.stopEvent, state_.dirHandle };

    while (state_.running) {
        DWORD bytesReturned = 0;
        ZeroMemory(buffer, bufSize);
        BOOL ok = ReadDirectoryChangesW(
            state_.dirHandle,
            buffer, bufSize, FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            &bytesReturned, nullptr, nullptr);

        if (!state_.running) break;

        if (!ok) break;  // error or handle closed

        // Walk the FILE_NOTIFY_INFORMATION chain and look for our basename.
        BYTE* ptr = buffer;
        while (true) {
            FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)ptr;
            if (info->Action == FILE_ACTION_MODIFIED ||
                info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                std::wstring name(info->FileName,
                                  info->FileNameLength / sizeof(WCHAR));
                if (_wcsicmp(name.c_str(), state_.fileName.c_str()) == 0) {
                    // Debounce: editors write in two close bursts. Wait 300ms
                    // then post a single reload so we catch the final state.
                    Sleep(300);
                    if (state_.running) {
                        PostMessageW(state_.hwnd, WM_USER_RELOAD, 0, 0);
                    }
                    break;
                }
            }
            if (info->NextEntryOffset == 0) break;
            ptr += info->NextEntryOffset;
        }
    }
}
