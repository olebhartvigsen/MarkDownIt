// MarkDownIt - native Windows markdown viewer
//
// Task 7: parse the command line for a file path and open it.
// Drag-and-drop is handled in the window (WS_EX_ACCEPTFILES + WM_DROPFILES).

#include <windows.h>
#include <ole2.h>   // OleInitialize for COM (Ribbon framework)
#include <string>
#include "app.h"
#include "dom.h"

// Enable ComCtl32 v6 (visual styles) for thematic controls and ribbon.
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Strip surrounding quotes from a command-line argument if present.
static std::wstring Unquote(std::wstring s) {
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // COM initialization required by the Windows Ribbon Framework.
    OleInitialize(nullptr);

    AppWindow app;
    std::wstring cmdLine = (lpCmdLine && *lpCmdLine) ? Unquote(lpCmdLine) : std::wstring();
    if (!app.Init(hInst, nCmdShow, cmdLine)) {
        return 1;
    }
    if (!cmdLine.empty()) {
        app.OpenPendingFile(cmdLine);
    }
    return app.Run();
}
