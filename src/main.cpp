// MarkDownIt - native Windows markdown viewer
//
// Task 5: creates the AppWindow and runs the message loop.
// The stub is gone; this is a real (blank) window now.

#include <windows.h>
#include "app.h"
#include "dom.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    AppWindow app;
    if (!app.Init(hInst, nCmdShow)) {
        return 1;
    }
    return app.Run();
}
