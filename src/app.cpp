#include "app.h"
#include "ribbon.h"
#include "parser.h"
#include <commdlg.h>

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cmath>

const wchar_t* AppWindow::kClassName = L"MarkDownItWindow";
const wchar_t* AppWindow::kContentClassName = L"MarkDownItContent";

template <typename T>
inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

static UINT GetWindowDpi(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        auto fn = (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        if (fn) return fn(hwnd);
    }
    return 96;
}

AppWindow::AppWindow() {}
AppWindow::~AppWindow() {
    SafeRelease(rt_);
    SafeRelease(d2d_factory_);
    SafeRelease(dw_factory_);
    renderer_.Release();
}

static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
        auto fn = (PFN_SetProcessDpiAwarenessContext)
            GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            if (fn((HANDLE)(LONG_PTR)-4)) return;
            if (fn((HANDLE)(LONG_PTR)-3)) return;
            if (fn((HANDLE)(LONG_PTR)-2)) return;
        }
    }
    SetProcessDPIAware();
}

// Save window placement to registry. Called at shutdown.
static void SaveWinPlacement(HWND hwnd) {
    WINDOWPLACEMENT wp;
    ZeroMemory(&wp, sizeof(wp));
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp)) return;

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MarkDownIt", 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;

    DWORD maximized = IsZoomed(hwnd) ? 1u : 0u;
    RegSetValueExW(hKey, L"WindowPlacement", 0, REG_BINARY,
        reinterpret_cast<BYTE*>(&wp), sizeof(wp));
    RegSetValueExW(hKey, L"WindowMaximized", 0, REG_DWORD,
        reinterpret_cast<BYTE*>(&maximized), sizeof(maximized));

    RegCloseKey(hKey);
}

// Restore window placement from registry if available and on-screen.
// Returns true on successful restore (also calls ShowWindow itself).
static bool RestoreWinPlacement(HWND hwnd) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\MarkDownIt", 0,
            KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD size = sizeof(WINDOWPLACEMENT);
    DWORD type = 0;
    WINDOWPLACEMENT wp;
    ZeroMemory(&wp, sizeof(wp));
    wp.length = sizeof(wp);
    LSTATUS lr = RegQueryValueExW(hKey, L"WindowPlacement", nullptr, &type,
        reinterpret_cast<BYTE*>(&wp), &size);

    DWORD maxVal = 0;
    DWORD maxSz = sizeof(maxVal);
    DWORD maxType = 0;
    RegQueryValueExW(hKey, L"WindowMaximized", nullptr, &maxType,
        reinterpret_cast<BYTE*>(&maxVal), &maxSz);

    RegCloseKey(hKey);

    if (lr != ERROR_SUCCESS || type != REG_BINARY || wp.length != sizeof(WINDOWPLACEMENT))
        return false;

    // Validate that the restored rect is at least partially on-screen.
    // Minimum visible window: 200x100 pixels.
    RECT rc = wp.rcNormalPosition;
    HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    if (!hMon) return false;

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return false;

    RECT work = mi.rcWork;
    int visLeft = (rc.left > work.left) ? rc.left : work.left;
    int visTop = (rc.top > work.top) ? rc.top : work.top;
    int visRight = (rc.right < work.right) ? rc.right : work.right;
    int visBottom = (rc.bottom < work.bottom) ? rc.bottom : work.bottom;
    int visW = visRight - visLeft;
    int visH = visBottom - visTop;
    if (visW < 200 || visH < 100) return false;

    wp.showCmd = SW_SHOWNORMAL;
    wp.flags = 0;
    SetWindowPlacement(hwnd, &wp);
    ShowWindow(hwnd, maxVal ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    return true;
}

// Find the Ribbon Framework's internal DirectUI window.
static BOOL CALLBACK FindRibbonWnd(HWND hwnd, LPARAM lParam) {
    wchar_t cls[256];
    if (GetClassNameW(hwnd, cls, 256) > 0) {
        if (wcscmp(cls, L"DirectUIHWND") == 0) {
            *(HWND*)lParam = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

// The Ribbon Framework always renders an Application Menu button at the
// left edge. There is no API to remove it, so we shift the ribbon's
// internal window left by the button width, pushing the button off-screen.
static void HideAppMenuButton(HWND mainWnd) {
    HWND ribbonWnd = nullptr;
    EnumChildWindows(mainWnd, FindRibbonWnd, (LPARAM)&ribbonWnd);
    if (!ribbonWnd) return;

    RECT rc;
    GetWindowRect(ribbonWnd, &rc);
    if (rc.right - rc.left == 0) return;

    // Account for Windows reference offset (parent coordinates).
    POINT origin = {0, 0};
    ClientToScreen(mainWnd, &origin);
    int parentX = origin.x;
    int parentY = origin.y;

    // Application Menu button width: 26 DIPs at 96 DPI.
    UINT dpi = GetWindowDpi ? 0 : 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        auto fn = (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        if (fn) dpi = fn(mainWnd);
        else dpi = 96;
    } else {
        dpi = 96;
    }
    int buttonW = 26 * static_cast<int>(dpi) / 96;

    // Shift the ribbon window left by buttonW and widen by buttonW.
    // The button (leftmost buttonW pixels) goes off-screen.
    SetWindowPos(ribbonWnd, nullptr,
        (rc.left - parentX) - buttonW,
        rc.top - parentY,
        (rc.right - rc.left) + buttonW,
        rc.bottom - rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

bool AppWindow::Init(HINSTANCE hInst, int nCmdShow) {
    hinst_ = hInst;
    EnableDpiAwareness();

    // Register the main window class.
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProcThunk;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClassName;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClass failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    // Register the content child window class.
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc   = ContentWndProcThunk;
    wc2.hInstance     = hInst;
    wc2.lpszClassName = kContentClassName;
    wc2.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc2.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc2.style         = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc2)) {
        MessageBoxW(nullptr, L"RegisterClass (content) failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 900, h = 640;
    int x = (sw - w) / 2, y = (sh - h) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kClassName, L"MarkDownIt",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInst, this);

    if (!hwnd_) {
        MessageBoxW(nullptr, L"CreateWindow failed", L"MarkDownIt", MB_ICONERROR);
        return false;
    }

    // Restore saved window placement, or use default if not available.
    if (!RestoreWinPlacement(hwnd_)) {
        ShowWindow(hwnd_, nCmdShow);
    }
    UpdateWindow(hwnd_);

    // Initialize the Ribbon Framework after the window is visible.
    InitRibbon(hwnd_, this);

    // Force a content resize + repaint. The ribbon may not fire
    // OnViewChanged immediately, so we ensure the content window
    // is positioned correctly right after initialization.
    ResizeContentWindow();
    Repaint();

    // Set a one-shot timer to re-layout and repaint after the ribbon
    // has had time to report its height. The ribbon's OnViewChanged
    // callback may fire asynchronously, so this is a safety net.
    SetTimer(hwnd_, 1, 300, nullptr);

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

void AppWindow::EnsureRenderer() {
    if (renderer_inited_ || !dw_factory_) return;
    renderer_inited_ = renderer_.Init(dw_factory_);
}

void AppWindow::LoadSampleDoc() {
    const char* sample =
        "# MarkDownIt\n\n"
        "A native Windows markdown viewer, built with C++, Direct2D, and "
        "DirectWrite. No Electron, no .NET runtime.\n\n"
        "## Open a file\n\n"
        "Drag a .md file onto this window, or launch with a path:\n"
        "    MarkDownIt.exe C:\\path\\to\\file.md\n\n"
        "Code blocks, lists, blockquotes, and inline formatting arrive in "
        "later tasks.\n\n"
        "## Scroll\n\n"
        "This is a long block of text so you can test the scrollbar. "
        "Use the mouse wheel, the scrollbar, page down, or the arrow keys. "
        "Resize the window and the text reflows. The scroll range updates "
        "automatically when the content height changes.\n\n"
        "Resize wider to see fewer lines. Resize narrower to see more lines "
        "and more scrolling. The content stays readable at any width.\n\n"
        "## End\n\n"
        "This is the last block. You have scrolled to the bottom.\n";
    ParseMarkdown(sample, doc_);
}

void AppWindow::Repaint() {
    if (hwnd_content_) InvalidateRect(hwnd_content_, nullptr, FALSE);
}

void AppWindow::OpenFile(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) {
        MessageBoxW(hwnd_, L"Could not open file", L"MarkDownIt", MB_ICONWARNING);
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string utf8 = ss.str();

    doc_ = Document{};
    ParseMarkdown(utf8, doc_);
    file_path_ = path;
    scrollY_ = 0.0f;
    totalH_ = 0.0f;

    std::wstring title = L"MarkDownIt";
    size_t slash = path.find_last_of(L"\\/");
    std::wstring base = (slash != std::wstring::npos)
        ? path.substr(slash + 1) : path;
    if (!base.empty()) title = L"MarkDownIt - " + base;
    SetWindowTextW(hwnd_, title.c_str());

    UpdateScrollInfo();
    Repaint();

    watcher_.Start(hwnd_, path);
}

void AppWindow::Reload() {
    if (file_path_.empty()) return;
    std::ifstream f(file_path_.c_str(), std::ios::binary);
    if (!f.is_open()) return;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string utf8 = ss.str();

    float savedY = scrollY_;
    doc_ = Document{};
    ParseMarkdown(utf8, doc_);
    UpdateScrollInfo();
    if (scrollY_ > savedY) scrollY_ = savedY;
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::OnReload() {
    Reload();
}

void AppWindow::OnDropFiles(HWND hwnd, HDROP hDrop) {
    wchar_t path[MAX_PATH] = {};
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0) {
        DragQueryFileW(hDrop, 0, path, MAX_PATH);
        OpenFile(path);
    }
    DragFinish(hDrop);
}

void AppWindow::OnCreate(HWND hwnd) {
    hwnd_ = hwnd;

    D2D1_FACTORY_OPTIONS opts = {};
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, &d2d_factory_);
    if (FAILED(hr) || !d2d_factory_) {
        MessageBoxW(hwnd, L"D2D1CreateFactory failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dw_factory_));
    if (FAILED(hr) || !dw_factory_) {
        MessageBoxW(hwnd, L"DWriteCreateFactory failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    // Create the content child window. The D2D render target will be
    // created on this child, NOT on the main window. This prevents
    // the D2D Present() from overwriting the ribbon's rendering.
    hwnd_content_ = CreateWindowExW(
        0, kContentClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS,
        0, 0, 0, 0,
        hwnd, nullptr, hinst_, this);
    if (!hwnd_content_) {
        MessageBoxW(hwnd, L"CreateWindow (content) failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    // Create D2D render target on the child window.
    RECT rc;
    GetClientRect(hwnd_content_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(
        (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
        (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    hr = d2d_factory_->CreateHwndRenderTarget(
        rtProps, D2D1::HwndRenderTargetProperties(hwnd_content_, size), &rt_);
    if (FAILED(hr)) {
        MessageBoxW(hwnd, L"CreateHwndRenderTarget failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    EnsureRenderer();
    LoadSampleDoc();
    UpdateScrollInfo();
}

void AppWindow::UpdateScrollInfo() {
    if (!hwnd_content_) return;

    float clientHDip = static_cast<float>(clientH_);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        clientHDip = rtSize.height;
    }

    float maxScroll = 0.0f;
    if (totalH_ > clientHDip) {
        maxScroll = totalH_ - clientHDip;
    }

    if (scrollY_ < 0.0f) scrollY_ = 0.0f;
    if (scrollY_ > maxScroll) scrollY_ = maxScroll;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = static_cast<int>(totalH_);
    si.nPage  = static_cast<UINT>(clientHDip > 0 ? clientHDip : 1);
    si.nPos   = static_cast<int>(scrollY_);
    SetScrollInfo(hwnd_content_, SB_VERT, &si, TRUE);
}

void AppWindow::ResizeContentWindow() {
    if (!hwnd_ || !hwnd_content_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    int contentY = static_cast<int>(g_ribbonHeight);
    int contentH = rc.bottom - contentY;
    if (contentH < 1) contentH = 1;

    SetWindowPos(hwnd_content_, nullptr,
        0, contentY,
        rc.right - rc.left, contentH,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void AppWindow::OnSize(HWND hwnd, int width, int height) {
    ResizeContentWindow();
}

void AppWindow::OnContentSize(HWND hwnd, int width, int height) {
    clientW_ = width;
    clientH_ = height;
    if (rt_) {
        D2D1_SIZE_U size = D2D1::SizeU(
            width > 0 ? width : 1, height > 0 ? height : 1);
        HRESULT hr = rt_->Resize(size);
        if (hr == D2DERR_RECREATE_TARGET) {
            RecreateRenderTarget();
        }
    }
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::OnContentVScroll(HWND hwnd, int code, int pos) {
    float oldY = scrollY_;
    float page = static_cast<float>(clientH_ > 0 ? clientH_ : 1);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        page = rtSize.height;
    }

    switch (code) {
        case SB_LINEUP:        scrollY_ -= 30.0f; break;
        case SB_LINEDOWN:       scrollY_ += 30.0f; break;
        case SB_PAGEUP:         scrollY_ -= page;  break;
        case SB_PAGEDOWN:       scrollY_ += page;  break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            scrollY_ = static_cast<float>(si.nTrackPos);
            break;
        }
        case SB_TOP:     scrollY_ = 0.0f; break;
        case SB_BOTTOM:  scrollY_ = totalH_; break;
    }

    UpdateScrollInfo();
    if (std::fabs(scrollY_ - oldY) > 0.01f) Repaint();
}

void AppWindow::OnContentMouseWheel(HWND hwnd, int delta) {
    float oldY = scrollY_;
    float step = 120.0f;
    scrollY_ -= (delta / WHEEL_DELTA) * step * 3.0f;
    UpdateScrollInfo();
    if (std::fabs(scrollY_ - oldY) > 0.01f) Repaint();
}

void AppWindow::RecreateRenderTarget() {
    SafeRelease(rt_);
    if (d2d_factory_ && hwnd_content_) {
        RECT rc;
        GetClientRect(hwnd_content_, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(
            (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
            (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        d2d_factory_->CreateHwndRenderTarget(
            rtProps, D2D1::HwndRenderTargetProperties(hwnd_content_, size), &rt_);
    }
}

void AppWindow::OnContentPaint(HWND hwnd) {
    if (!rt_) {
        RecreateRenderTarget();
        if (!rt_) { ValidateRect(hwnd, nullptr); return; }
    }

    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        totalH_ = renderer_.Measure(dw_factory_, doc_, size.width, 0.0f);
        UpdateScrollInfo();
    }

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        renderer_.Render(rt_, dw_factory_, doc_, size.width, scrollY_, 0.0f);
    }

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        RecreateRenderTarget();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    EndPaint(hwnd, &ps);
}

void AppWindow::OnDestroy() {
    SaveWinPlacement(hwnd_);
    DestroyRibbon();
    watcher_.Stop();
    renderer_.Release();
    SafeRelease(rt_);
    SafeRelease(dw_factory_);
    SafeRelease(d2d_factory_);
    PostQuitMessage(0);
}

void AppWindow::OnRibbonHeightChanged() {
    ResizeContentWindow();
    Repaint();
    HideAppMenuButton(hwnd_);
}

void AppWindow::RecreateRenderer() {
    renderer_.Release();
    renderer_inited_ = false;
    EnsureRenderer();
}

void AppWindow::OpenFileDialog() {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Open Markdown File";
    if (GetOpenFileNameW(&ofn)) {
        OpenFile(buf);
    }
}

void AppWindow::ZoomIn() {
    float z = renderer_.Zoom();
    z *= 1.25f;
    if (z > 4.0f) z = 4.0f;
    renderer_.SetZoom(z);
    RecreateRenderer();
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::ZoomOut() {
    float z = renderer_.Zoom();
    z /= 1.25f;
    if (z < 0.5f) z = 0.5f;
    renderer_.SetZoom(z);
    RecreateRenderer();
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::ToggleWrap() {
    renderer_.SetWrap(!renderer_.Wrap());
    UpdateRibbonWrapState(renderer_.Wrap());
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::ShowAbout() {
    MessageBoxW(hwnd_,
        L"MarkDownIt - Native Windows Markdown Viewer\n"
        L"Built with C++, Direct2D, and DirectWrite.\n"
        L"No Electron, no .NET runtime.\n\n"
        L"Drag a .md file onto the window or use Open.\n"
        L"F5 to reload, mouse wheel to scroll.",
        L"About MarkDownIt", MB_OK | MB_ICONINFORMATION);
}

//
// Main window WndProc.
// Handles WM_SIZE, WM_DROPFILES, WM_DESTROY, WM_DPICHANGED.
// Does NOT handle WM_PAINT (validated by DefWindowProc, ribbon paints NC).
//
LRESULT AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:    OnCreate(hwnd);    return 0;
        case WM_DROPFILES: OnDropFiles(hwnd, (HDROP)wp); return 0;
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            OnSize(hwnd, w, h);
            return 0;
        }
        case WM_DPICHANGED: {
            renderer_.Release();
            EnsureRenderer();
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top,
                rc->right - rc->left, rc->bottom - rc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            ResizeContentWindow();
            Repaint();
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                KillTimer(hwnd_, 1);
                ResizeContentWindow();
                Repaint();
                HideAppMenuButton(hwnd_);
            }
            return 0;
        case FileWatcher::WM_USER_RELOAD: OnReload(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY:   OnDestroy();   return 0;
        case WM_PAINT: {
            // Main window does not paint. The ribbon framework handles
            // its own area, and the content child handles content.
            ValidateRect(hwnd, nullptr);
            return 0;
        }
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

//
// Content child window WndProc.
// Handles WM_PAINT, WM_VSCROLL, WM_MOUSEWHEEL, WM_KEYDOWN, WM_SIZE.
//
LRESULT AppWindow::ContentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:     OnContentPaint(hwnd); return 0;
        case WM_VSCROLL:    OnContentVScroll(hwnd, (int)LOWORD(wp), (int)HIWORD(wp)); return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            OnContentMouseWheel(hwnd, delta);
            return 0;
        }
        case WM_KEYDOWN: {
            switch (wp) {
                case VK_DOWN: OnContentVScroll(hwnd, SB_LINEDOWN, 0); break;
                case VK_UP:   OnContentVScroll(hwnd, SB_LINEUP, 0);   break;
                case VK_NEXT: OnContentVScroll(hwnd, SB_PAGEDOWN, 0); break;
                case VK_PRIOR:OnContentVScroll(hwnd, SB_PAGEUP, 0);   break;
                case VK_HOME: OnContentVScroll(hwnd, SB_TOP, 0);     break;
                case VK_END:  OnContentVScroll(hwnd, SB_BOTTOM, 0);  break;
                case VK_F5:   Reload();                              break;
                default: return DefWindowProcW(hwnd, msg, wp, lp);
            }
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            OnContentSize(hwnd, w, h);
            return 0;
        }
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

LRESULT CALLBACK AppWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (AppWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK AppWindow::ContentWndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (AppWindow*)cs->lpCreateParams;
        // Content window also stores a pointer to the AppWindow.
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->ContentWndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
