// MarkDownIt - native Windows markdown viewer
//
// Task 7: parse the command line for a file path and open it.
// Drag-and-drop is handled in the window (WS_EX_ACCEPTFILES + WM_DROPFILES).

#include <windows.h>
#include <string>
#include "app.h"
#include "dom.h"

// Strip surrounding quotes from a command-line argument if present.
static std::wstring Unquote(std::wstring s) {
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    AppWindow app;
    if (!app.Init(hInst, nCmdShow)) {
        return 1;
    }
    if (lpCmdLine && *lpCmdLine) {
        app.OpenFile(Unquote(lpCmdLine));
    }
    return app.Run();
}
