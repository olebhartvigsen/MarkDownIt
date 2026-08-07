#pragma once

// AppWindow: owns the Win32 window and Direct2D render target.
// Task 5: blank white window that resizes and handles DPI changes.
// Task 6+: the renderer draws the parsed markdown into this window.

#include <windows.h>
#include <d2d1.h>
#include <string>
#include "dom.h"

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Init(HINSTANCE hInst, int nCmdShow);
    int  Run();  // message loop

private:
    HWND    hwnd_ = nullptr;
    HINSTANCE hinst_ = nullptr;

    // Direct2D
    ID2D1Factory*          d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;

    // Window class name
    static const wchar_t* kClassName;

    // Message handlers
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnCreate(HWND hwnd);
    void OnPaint(HWND hwnd);
    void OnSize(HWND hwnd, int width, int height);
    void OnDestroy();
    void RecreateRenderTarget();

    // Static thunk for SetWindowLongPtr
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
