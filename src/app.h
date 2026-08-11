#pragma once

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "dom.h"
#include "renderer.h"
#include "filewatch.h"

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Init(HINSTANCE hInst, int nCmdShow);
    int  Run();

    void OpenFile(const std::wstring& path);
    void Reload();

    // Public action methods, called by the Ribbon command handler.
    void OpenFileDialog();
    void ZoomIn();
    void ZoomOut();
    void ToggleWrap();
    void ShowAbout();

    // Called by CRibbonApplication::OnViewChanged when ribbon height changes.
    void OnRibbonHeightChanged();

private:
    HWND    hwnd_ = nullptr;
    HINSTANCE hinst_ = nullptr;

    ID2D1Factory*          d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* rt_ = nullptr;
    IDWriteFactory*        dw_factory_ = nullptr;

    Renderer               renderer_;
    bool                   renderer_inited_ = false;

    Document               doc_;
    std::wstring           file_path_;

    FileWatcher            watcher_;   // live reload (Task 9)

    // Scroll state (in DIPs)
    float  scrollY_    = 0.0f;
    float  totalH_     = 0.0f;
    int    clientW_    = 0;
    int    clientH_    = 0;

    static const wchar_t* kClassName;

    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnCreate(HWND hwnd);
    void OnPaint(HWND hwnd);
    void OnSize(HWND hwnd, int width, int height);
    void OnVScroll(HWND hwnd, int code, int pos);
    void OnMouseWheel(HWND hwnd, int delta);
    void OnDropFiles(HWND hwnd, HDROP hDrop);
    void OnReload();
    void OnDestroy();
    void RecreateRenderTarget();
    void EnsureRenderer();
    void UpdateScrollInfo();
    void LoadSampleDoc();
    void Repaint();
    float GetRibbonHeightDip();
    void RecreateRenderer();

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
