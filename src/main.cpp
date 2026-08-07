// MarkDownIt - native Windows markdown viewer
// Stub: confirms the toolchain links and produces a PE32 GUI binary.
//
// dom.h is included here so CI compiles it as a syntax gate even before
// any code uses it. Remove the pragma-kept include once parser.cpp (Task 4)
// pulls it in for real.
#include <windows.h>

#include "dom.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Touch dom.h types so the include is not optimized away and MSVC
    // actually instantiates the structs.
    Document doc;
    (void)doc;

    MessageBoxW(nullptr, L"MarkDownIt stub", L"ok", MB_OK);
    return 0;
}
