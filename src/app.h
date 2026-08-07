#pragma once

#include <windows.h>
#include <shellapi.h>
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

    // Open a markdown file: read, parse, update title bar, repaint.
    void OpenFile(const std::wstring& path);

private:
    HWND    hwnd_ = nullptr;
    HINSTANCE hinst_ = nullptr;

    ID2D1Factory*          d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    IDWriteFactory*        dw_factory_ = nullptr;

    Renderer               renderer_;
    bool                   renderer_inited_ = false;

    Document               doc_;
    std::wstring           file_path_;   // empty means no file (sample shown)

    static const wchar_t* kClassName;

    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnCreate(HWND hwnd);
    void OnPaint(HWND hwnd);
    void OnSize(HWND hwnd, int width, int height);
    void OnDropFiles(HWND hwnd, HDROP hDrop);
    void OnDestroy();
    void RecreateRenderTarget();
    void EnsureRenderer();
    void LoadSampleDoc();

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
