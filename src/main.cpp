// MarkDownIt - native Windows markdown viewer
//
// Task 7: parse the command line for a file path and open it.
// Drag-and-drop is handled in the window (WS_EX_ACCEPTFILES + WM_DROPFILES).

#include <windows.h>
#include <ole2.h>   // OleInitialize for COM (Ribbon framework)
#include <shellapi.h>
#include <string>
#include "app.h"
#include "crash_trace.h"
#include "dom.h"

// Enable ComCtl32 v6 (visual styles) for thematic controls and ribbon.
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// The startup file arrives as the first command-line argument. The
// Windows parser is used instead of hand-stripping quotes, so every
// quoting shape a launcher hands over (the shell's file association, a
// script, a path with spaces) lands as the plain path. The argument is
// traced, because the launch smoke reads that trace when a capture
// shows the open failing.
static std::wstring StartFileArg() {
    std::wstring path;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1 && argv[1] && *argv[1]) path = argv[1];
        LocalFree(argv);
    }
    diag::TraceFmt("startup file arg=[%ls]", path.c_str());
    return path;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // COM initialization required by the Windows Ribbon Framework.
    OleInitialize(nullptr);

    AppWindow app;
    std::wstring cmdLine = StartFileArg();
    (void)lpCmdLine;
    if (!app.Init(hInst, nCmdShow, cmdLine)) {
        return 1;
    }
    if (!cmdLine.empty()) {
        app.OpenPendingFile(cmdLine);
    }
    return app.Run();
}
