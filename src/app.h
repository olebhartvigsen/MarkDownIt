#pragma once

// AppWindow: owns the Win32 window and Direct2D render target.
// DirectWrite factory + Renderer draw the parsed markdown.

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "renderer.h"

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Init(HINSTANCE hInst, int nCmdShow);
    int  Run();

private:
    HWND    hwnd_ = nullptr;
    HINSTANCE hinst_ = nullptr;

    // Direct2D
    ID2D1Factory*          d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;

    // DirectWrite
    IDWriteFactory*        dw_factory_ = nullptr;

    // Renderer
    Renderer               renderer_;
    bool                   renderer_inited_ = false;

    // Sample document (hardcoded for Task 6; real file open is Task 7)
    Document               doc_;

    static const wchar_t* kClassName;

    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnCreate(HWND hwnd);
    void OnPaint(HWND hwnd);
    void OnSize(HWND hwnd, int width, int height);
    void OnDestroy();
    void RecreateRenderTarget();
    void EnsureRenderer();

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
