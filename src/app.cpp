// AppWindow: Win32 window with Direct2D render target.
//
// Task 5 scope: a real, blank, white, resizable window.
//  - DPI-aware (per-monitor v2 if available, else system)
//  - Direct2D HwndRenderTarget, cleared to white on paint
//  - Resize updates the render target
//  - WM_DPICHANGED: resize + (later) relayout
//  - D2DERR_RECREATE_TARGET: drop and recreate the render target
//
// Task 6 wires the renderer to draw markdown into this window.

#include "app.h"

#include <windows.h>
#include <d2d1.h>
#include <cstdio>

const wchar_t* AppWindow::kClassName = L"MarkDownItWindow";

// Helper: safe Release for COM pointers
template <typename T>
inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

AppWindow::AppWindow() {}
AppWindow::~AppWindow() {
    SafeRelease(rt_);
    SafeRelease(d2d_factory_);
}

// Set DPI awareness before any window is created.
// Per-monitor v2 (Win10 1703+), fall back to system awareness.
static void EnableDpiAwareness() {
    // SetProcessDpiAwarenessContext is available on Win10 1607+.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
        auto fn = (PFN_SetProcessDpiAwarenessContext)
            GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((DPI_CONTEXT_HANDLE)-4)
            HANDLE v2 = (HANDLE)(LONG_PTR)-4;
            if (fn(v2)) return;
            // Fall back to per-monitor v1 = -3
            HANDLE v1 = (HANDLE)(LONG_PTR)-3;
            if (fn(v1)) return;
            // Fall back to system = -2
            HANDLE sys = (HANDLE)(LONG_PTR)-2;
            if (fn(sys)) return;
        }
    }
    // Last resort: SetProcessDPICompat (system-aware, older API)
    SetProcessDPIAware();
}

bool AppWindow::Init(HINSTANCE hInst, int nCmdShow) {
    hinst_ = hInst;
    EnableDpiAwareness();

    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProcThunk;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // D2D paints everything
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClass failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    // Create window: 800x600 centered
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 800, h = 600;
    int x = (sw - w) / 2, y = (sh - h) / 2;

    hwnd_ = CreateWindowExW(
        0, kClassName, L"MarkDownIt",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInst, this);

    if (!hwnd_) {
        MessageBoxW(nullptr, L"CreateWindow failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

int AppWindow::Run() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void AppWindow::OnCreate(HWND hwnd) {
    // Create Direct2D factory
    D2D1_FACTORY_OPTIONS opts = {};
#ifdef _DEBUG
    // opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        opts,
        &d2d_factory_);
    if (FAILED(hr) || !d2d_factory_) {
        MessageBoxW(hwnd, L"D2D1CreateFactory failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    // Create the render target (initial size set in WM_SIZE)
    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(
        (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
        (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED);

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(hwnd, size);

    hr = d2d_factory_->CreateHwndRenderTarget(
        rtProps, hwndProps, &rt_);
    if (FAILED(hr)) {
        MessageBoxW(hwnd, L"CreateHwndRenderTarget failed", L"MarkDownIt", MB_ICONERROR);
    }
}

void AppWindow::RecreateRenderTarget() {
    SafeRelease(rt_);
    if (d2d_factory_ && hwnd_) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(
            (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
            (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        d2d_factory_->CreateHwndRenderTarget(
            rtProps, D2D1::HwndRenderTargetProperties(hwnd_, size), &rt_);
    }
}

void AppWindow::OnPaint(HWND hwnd) {
    if (!rt_) {
        // No render target yet: validate the window to avoid paint spam
        ValidateRect(hwnd, nullptr);
        return;
    }

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));
    HRESULT hr = rt_->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET) {
        RecreateRenderTarget();
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    EndPaint(hwnd, &ps);
}

void AppWindow::OnSize(HWND hwnd, int width, int height) {
    if (rt_) {
        D2D1_SIZE_U size = D2D1::SizeU(
            width > 0 ? width : 1, height > 0 ? height : 1);
        rt_->Resize(size);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void AppWindow::OnDestroy() {
    SafeRelease(rt_);
    SafeRelease(d2d_factory_);
    PostQuitMessage(0);
}

LRESULT AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            OnCreate(hwnd);
            return 0;

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            OnSize(hwnd, w, h);
            return 0;
        }

        case WM_DPICHANGED: {
            // Recommended rect is in lParam
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr,
                rc->left, rc->top,
                rc->right - rc->left, rc->bottom - rc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_ERASEBKGND:
            // We paint the whole window via D2D; suppress erase to avoid flicker
            return 1;

        case WM_DESTROY:
            OnDestroy();
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

LRESULT CALLBACK AppWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;

    if (msg == WM_CREATE) {
        // lp is CREATESTRUCT*; its lpCreateParams is the AppWindow* (passed to CreateWindow)
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (AppWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (self) {
        return self->WndProc(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
