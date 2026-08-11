#include "app.h"
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

bool AppWindow::Init(HINSTANCE hInst, int nCmdShow) {
    hinst_ = hInst;
    EnableDpiAwareness();

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

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 900, h = 640;
    int x = (sw - w) / 2, y = (sh - h) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kClassName, L"MarkDownIt",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
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
    InvalidateRect(hwnd_, nullptr, FALSE);
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

    // Start watching the file for live reload.
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
    // Restore scroll position if it still fits.
    if (scrollY_ > savedY) scrollY_ = savedY;
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::OnReload() {
    // Called when the FileWatcher posts WM_USER_RELOAD (file changed on disk).
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

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(
        (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 1,
        (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    hr = d2d_factory_->CreateHwndRenderTarget(
        rtProps, D2D1::HwndRenderTargetProperties(hwnd, size), &rt_);
    if (FAILED(hr)) {
        MessageBoxW(hwnd, L"CreateHwndRenderTarget failed", L"MarkDownIt", MB_ICONERROR);
        return;
    }

    EnsureRenderer();
    LoadSampleDoc();
    CreateToolbar();
    UpdateScrollInfo();
}

void AppWindow::UpdateScrollInfo() {
    if (!hwnd_) return;

    // Use DIPs for consistent scrollbar math. The render target's
    // GetSize() returns DIPs; WM_SIZE gives pixels. At non-100% DPI
    // these differ, so we must use DIPs for both totalH_ and page size.
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
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void AppWindow::OnSize(HWND hwnd, int width, int height) {
    clientW_ = width;
    clientH_ = height;
    if (rt_) {
        D2D1_SIZE_U size = D2D1::SizeU(
            width > 0 ? width : 1, height > 0 ? height : 1);
        rt_->Resize(size);
    }
    UpdateScrollInfo();
    Repaint();
}

void AppWindow::OnVScroll(HWND hwnd, int code, int pos) {
    float oldY = scrollY_;
    float page = static_cast<float>(clientH_ > 0 ? clientH_ : 1);
    if (rt_) {
        D2D1_SIZE_F rtSize = rt_->GetSize();
        page = rtSize.height;
    }

    switch (code) {
        case SB_LINEUP:        scrollY_ -= 30.0f; break;
        case SB_LINEDOWN:      scrollY_ += 30.0f; break;
        case SB_PAGEUP:        scrollY_ -= page;  break;
        case SB_PAGEDOWN:      scrollY_ += page;  break;
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

void AppWindow::OnMouseWheel(HWND hwnd, int delta) {
    float oldY = scrollY_;
    // WHEEL_DELTA is 120; scroll 3 lines per notch, ~40 DIPs per line.
    float step = 120.0f;
    scrollY_ -= (delta / WHEEL_DELTA) * step * 3.0f;
    UpdateScrollInfo();
    if (std::fabs(scrollY_ - oldY) > 0.01f) Repaint();
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
    if (!rt_) { ValidateRect(hwnd, nullptr); return; }

    // Measure content height first (no draw), so the scrollbar is correct
    // and we do not draw the whole document twice.
    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        totalH_ = renderer_.Measure(dw_factory_, doc_, size.width,
                                      GetRibbonHeightDip());
        UpdateScrollInfo();
    }

    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (renderer_inited_ && dw_factory_) {
        D2D1_SIZE_F size = rt_->GetSize();
        renderer_.Render(rt_, dw_factory_, doc_, size.width, scrollY_,
                          GetRibbonHeightDip());
    }

    HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        RecreateRenderTarget();
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    EndPaint(hwnd, &ps);
}

void AppWindow::OnDestroy() {
    watcher_.Stop();
    renderer_.Release();
    SafeRelease(rt_);
    SafeRelease(dw_factory_);
    SafeRelease(d2d_factory_);
    PostQuitMessage(0);
}

float AppWindow::GetRibbonHeightDip() {
    if (!rt_ || ribbonHeightPx_ == 0.0f) return 0.0f;
    D2D1_SIZE_F dipSize = rt_->GetSize();
    D2D1_SIZE_U pxSize = rt_->GetPixelSize();
    if (pxSize.height == 0) return 0.0f;
    float scale = dipSize.height / (float)(pxSize.height);
    return ribbonHeightPx_ * scale;
}

void AppWindow::RecreateRenderer() {
    renderer_.Release();
    renderer_inited_ = false;
    EnsureRenderer();
}

void AppWindow::CreateToolbar() {
    struct Btn { const wchar_t* label; int id; };
    Btn buttons[] = {
        { L"Open",     IDC_TB_OPEN },
        { L"Reload",   IDC_TB_RELOAD },
        { L"Wrap: On", IDC_TB_WRAP },
        { L"Zoom +",   IDC_TB_ZOOMIN },
        { L"Zoom -",   IDC_TB_ZOOMOUT },
        { L"About",    IDC_TB_ABOUT },
    };
    UINT dpi = GetWindowDpi(hwnd_);
    int btnH = MulDiv(28, dpi, 96);
    int btnW = MulDiv(72, dpi, 96);
    int pad  = MulDiv(4, dpi, 96);
    int top  = MulDiv(4, dpi, 96);
    int x = pad;
    for (const auto& b : buttons) {
        CreateWindowExW(0, L"BUTTON", b.label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, top, btnW, btnH,
            hwnd_, (HMENU)(LONG_PTR)(b.id),
            hinst_, nullptr);
        x += btnW + pad;
    }
    ribbonHeightPx_ = (float)(btnH + top * 2);
}

void AppWindow::RepositionToolbar() {
    UINT dpi = GetWindowDpi(hwnd_);
    int btnH = MulDiv(28, dpi, 96);
    int btnW = MulDiv(72, dpi, 96);
    int pad  = MulDiv(4, dpi, 96);
    int top  = MulDiv(4, dpi, 96);
    int x = pad;
    int ids[] = { IDC_TB_OPEN, IDC_TB_RELOAD, IDC_TB_WRAP,
                 IDC_TB_ZOOMIN, IDC_TB_ZOOMOUT, IDC_TB_ABOUT };
    for (int id : ids) {
        HWND btn = GetDlgItem(hwnd_, id);
        if (btn) MoveWindow(btn, x, top, btnW, btnH, TRUE);
        x += btnW + pad;
    }
    ribbonHeightPx_ = (float)(btnH + top * 2);
}

void AppWindow::UpdateWrapButton() {
    HWND btn = GetDlgItem(hwnd_, IDC_TB_WRAP);
    if (btn) {
        SetWindowTextW(btn, renderer_.Wrap() ? L"Wrap: On" : L"Wrap: Off");
    }
}

void AppWindow::OnCommand(WPARAM wp) {
    int id = LOWORD(wp);
    switch (id) {
        case IDC_TB_OPEN:    OpenFileDialog(); break;
        case IDC_TB_RELOAD:  Reload();        break;
        case IDC_TB_WRAP:    ToggleWrap();    break;
        case IDC_TB_ZOOMIN:  ZoomIn();        break;
        case IDC_TB_ZOOMOUT: ZoomOut();       break;
        case IDC_TB_ABOUT:   ShowAbout();     break;
    }
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
    UpdateWrapButton();
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

LRESULT AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:    OnCreate(hwnd);    return 0;
        case WM_PAINT:     OnPaint(hwnd);     return 0;
        case WM_DROPFILES: OnDropFiles(hwnd, (HDROP)wp); return 0;
        case WM_VSCROLL:   OnVScroll(hwnd, (int)LOWORD(wp), (int)HIWORD(wp)); return 0;
        case WM_COMMAND:    OnCommand(wp); return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            OnMouseWheel(hwnd, delta);
            return 0;
        }
        case WM_KEYDOWN: {
            switch (wp) {
                case VK_DOWN: OnVScroll(hwnd, SB_LINEDOWN, 0); break;
                case VK_UP:   OnVScroll(hwnd, SB_LINEUP, 0);   break;
                case VK_NEXT: OnVScroll(hwnd, SB_PAGEDOWN, 0); break;
                case VK_PRIOR:OnVScroll(hwnd, SB_PAGEUP, 0);   break;
                case VK_HOME: OnVScroll(hwnd, SB_TOP, 0);     break;
                case VK_END:  OnVScroll(hwnd, SB_BOTTOM, 0);  break;
                case VK_F5:  Reload();                        break;
                default: return DefWindowProcW(hwnd, msg, wp, lp);
            }
            return 0;
        }
        case WM_SIZE: {
            int w = LOWORD(lp), h = HIWORD(lp);
            OnSize(hwnd, w, h);
            return 0;
        }
        case WM_DPICHANGED: {
            // Recreate text formats so they pick up the new DPI.
            renderer_.Release();
            EnsureRenderer();
            RepositionToolbar();
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top,
                rc->right - rc->left, rc->bottom - rc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateScrollInfo();
            Repaint();
            return 0;
        }
        case FileWatcher::WM_USER_RELOAD: OnReload(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY:   OnDestroy();   return 0;
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
