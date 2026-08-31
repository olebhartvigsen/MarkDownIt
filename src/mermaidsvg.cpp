#include "mermaidsvg.h"

#if HAS_WEBVIEW2

// WebView2 requires these headers in a specific order.
// windows.h and unknwn.h must come before WebView2.h.
#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <WebView2.h>
#include <sstream>
#include <queue>
#include <map>
#include <mutex>
#include <utility>

// ---------------------------------------------------------------------------
// Dynamic loading of WebView2Loader.dll — the NuGet package ships the DLL
// but no import library, so we LoadLibrary + GetProcAddress at runtime.
// ---------------------------------------------------------------------------

typedef HRESULT (WINAPI *PFN_CreateCoreWebView2EnvironmentWithOptions)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

static PFN_CreateCoreWebView2EnvironmentWithOptions g_pfnCreateEnv = nullptr;
static HMODULE g_webview2LoaderDll = nullptr;

static bool LoadWebView2Loader() {
    if (g_pfnCreateEnv) return true;
    g_webview2LoaderDll = LoadLibraryW(L"WebView2Loader.dll");
    if (!g_webview2LoaderDll) return false;
    g_pfnCreateEnv = reinterpret_cast<PFN_CreateCoreWebView2EnvironmentWithOptions>(
        GetProcAddress(g_webview2LoaderDll, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!g_pfnCreateEnv) {
        FreeLibrary(g_webview2LoaderDll);
        g_webview2LoaderDll = nullptr;
        return false;
    }
    return true;
}

// Read mermaid.min.js from RCDATA resource.
static std::string LoadMermaidJs() {
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!hRes) return {};
    HGLOBAL hMem = LoadResource(hMod, hRes);
    if (!hMem) return {};
    DWORD size = SizeofResource(hMod, hRes);
    const char* data = static_cast<const char*>(LockResource(hMem));
    if (!data || size == 0) return {};
    return std::string(data, size);
}

// HTML page that loads mermaid.js and provides a render function.
static std::string BuildMermaidHtml(const std::string& mermaidJs) {
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
         << "<style>body{margin:0;padding:0;}"
         << "#container{position:absolute;left:-9999px;top:-9999px;}"
         << "</style></head><body>"
         << "<div id=\"container\"></div>"
         << "<script>" << mermaidJs << "</script>"
         << "<script>"
         << "let mermaidReady = false;"
         << "try {"
         << "  mermaid.initialize({"
         << "    startOnLoad: false,"
         << "    htmlLabels: false,"
         << "    securityLevel: 'loose',"
         << "    theme: 'default'"
         << "  });"
         << "  mermaidReady = true;"
         << "} catch(e) {"
         << "  console.error('mermaid initialize failed:', e);"
         << "}"
         << "window.chrome.webview.addEventListener('message', async (e) => {"
         << "  const {id, code} = e.data;"
         << "  if (!mermaidReady) {"
         << "    window.chrome.webview.postMessage({id: id, svg: ''});"
         << "    return;"
         << "  }"
         << "  try {"
         << "    const {svg} = await mermaid.render('m' + id, code);"
         << "    window.chrome.webview.postMessage({id: id, svg: svg});"
         << "  } catch(err) {"
         << "    window.chrome.webview.postMessage({id: id, svg: ''});"
         << "  }"
         << "});"
         << "</script></body></html>";
    return html.str();
}

namespace mermaid {

struct MermaidRenderer::Impl {
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;

    HWND parent = nullptr;
    HWND hidden_wnd = nullptr;

    struct PendingReq {
        uint32_t srcOffset = 0;
        std::string code;
        MermaidRenderer::RenderCallback cb;
    };
    std::queue<PendingReq> pending;
    std::mutex pendingMutex;

    int nextId = 1;

    struct OutstandingCb {
        uint32_t srcOffset = 0;
        MermaidRenderer::RenderCallback cb;
    };
    std::map<int, OutstandingCb> outstanding;
    std::mutex outstandingMutex;

    EventRegistrationToken messageToken = {};
    EventRegistrationToken navToken = {};

    bool webviewReady = false;

    HRESULT OnMessageReceived(ICoreWebView2WebMessageReceivedEventArgs* args) {
        LPWSTR rawMsg = nullptr;
        args->TryGetWebMessageAsString(&rawMsg);
        if (!rawMsg) return S_OK;

        std::wstring wmsg(rawMsg);
        CoTaskMemFree(rawMsg);

        // Extract id from JSON
        int id = 0;
        {
            std::wstring idKey = L"\"id\":";
            size_t p = wmsg.find(idKey);
            if (p != std::wstring::npos) {
                p += idKey.size();
                while (p < wmsg.size() && (wmsg[p] == ' ' || wmsg[p] == '\t')) p++;
                std::wstring numStr;
                while (p < wmsg.size() && wmsg[p] >= L'0' && wmsg[p] <= L'9') {
                    numStr += wmsg[p];
                    p++;
                }
                id = std::stoi(numStr);
            }
        }

        // Extract svg
        std::string svg;
        {
            std::wstring svgKey = L"\"svg\":\"";
            size_t p = wmsg.find(svgKey);
            if (p != std::wstring::npos) {
                p += svgKey.size();
                std::wstring svgW;
                while (p < wmsg.size() && wmsg[p] != L'"') {
                    if (wmsg[p] == L'\\' && p + 1 < wmsg.size()) {
                        p++;
                        if (wmsg[p] == L'n') svgW += L'\n';
                        else if (wmsg[p] == L't') svgW += L'\t';
                        else if (wmsg[p] == L'\\') svgW += L'\\';
                        else if (wmsg[p] == L'"') svgW += L'"';
                        else if (wmsg[p] == L'/') svgW += L'/';
                        else if (wmsg[p] == L'r') svgW += L'\r';
                        else svgW += wmsg[p];
                    } else {
                        svgW += wmsg[p];
                    }
                    p++;
                }
                int len = WideCharToMultiByte(CP_UTF8, 0, svgW.c_str(),
                    static_cast<int>(svgW.size()), nullptr, 0, nullptr, nullptr);
                svg.resize(len);
                WideCharToMultiByte(CP_UTF8, 0, svgW.c_str(),
                    static_cast<int>(svgW.size()), &svg[0], len, nullptr, nullptr);
            }
        }

        // Invoke callback
        {
            std::lock_guard<std::mutex> lock(outstandingMutex);
            auto it = outstanding.find(id);
            if (it != outstanding.end()) {
                if (it->second.cb) it->second.cb(it->second.srcOffset, svg);
                outstanding.erase(it);
            }
        }
        return S_OK;
    }

    void ProcessPending() {
        std::lock_guard<std::mutex> lock(pendingMutex);
        while (!pending.empty() && webviewReady) {
            auto req = std::move(pending.front());
            pending.pop();
            SendRenderRequest(req.srcOffset, req.code, std::move(req.cb));
        }
    }

    void SendRenderRequest(uint32_t srcOffset, const std::string& code,
                           MermaidRenderer::RenderCallback cb) {
        int id = nextId++;
        {
            std::lock_guard<std::mutex> lock(outstandingMutex);
            outstanding[id] = {srcOffset, std::move(cb)};
        }

        // Escape code for JSON
        std::string escaped;
        for (char c : code) {
            if (c == '\\') escaped += "\\\\";
            else if (c == '"') escaped += "\\\"";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else escaped += c;
        }

        std::string msg = "{\"id\":" + std::to_string(id)
            + ",\"code\":\"" + escaped + "\"}";

        std::wstring wmsg;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(),
            static_cast<int>(msg.size()), nullptr, 0);
        wmsg.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(),
            static_cast<int>(msg.size()), &wmsg[0], wlen);

        webview->PostWebMessageAsString(wmsg.c_str());
    }
};

MermaidRenderer::MermaidRenderer() = default;
MermaidRenderer::~MermaidRenderer() { Shutdown(); }

bool MermaidRenderer::Init(HWND parent) {
    if (!parent) return false;

    // Dynamically load WebView2Loader.dll
    if (!LoadWebView2Loader()) return false;

    impl_ = new Impl();
    impl_->parent = parent;

    // Create hidden child window for WebView2
    impl_->hidden_wnd = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT, 0, 0, 1, 1,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!impl_->hidden_wnd) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    // Load mermaid.js from resource
    std::string mermaidJs = LoadMermaidJs();
    if (mermaidJs.empty()) {
        DestroyWindow(impl_->hidden_wnd);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    // Create WebView2 environment via dynamically loaded function
    HRESULT hr = g_pfnCreateEnv(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) return S_OK;
                impl_->env = env;

                env->CreateCoreWebView2Controller(impl_->hidden_wnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) return S_OK;
                            impl_->controller = controller;
                            controller->get_CoreWebView2(&impl_->webview);

                            if (impl_->webview) {
                                // Set up message handler
                                auto handler = Microsoft::WRL::Callback<
                                    ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*,
                                        ICoreWebView2WebMessageReceivedEventArgs* args)
                                    -> HRESULT {
                                        return impl_->OnMessageReceived(args);
                                    });
                                impl_->webview->add_WebMessageReceived(
                                    handler.Get(), &impl_->messageToken);

                                // Navigate to mermaid HTML
                                std::string html = BuildMermaidHtml(LoadMermaidJs());
                                std::wstring htmlW;
                                int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                    html.c_str(), static_cast<int>(html.size()),
                                    nullptr, 0);
                                htmlW.resize(wlen);
                                MultiByteToWideChar(CP_UTF8, 0,
                                    html.c_str(), static_cast<int>(html.size()),
                                    &htmlW[0], wlen);

                                impl_->webview->NavigateToString(htmlW.c_str());

                                // NavigationCompleted -> mark ready
                                auto navHandler = Microsoft::WRL::Callback<
                                    ICoreWebView2NavigationCompletedEventHandler>(
                                        [this](ICoreWebView2*,
                                            ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                            impl_->webviewReady = true;
                                            impl_->ProcessPending();
                                            return S_OK;
                                        });
                                impl_->webview->add_NavigationCompleted(
                                    navHandler.Get(),
                                    &impl_->navToken);
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        DestroyWindow(impl_->hidden_wnd);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    available_ = true;
    return true;
}

bool MermaidRenderer::Available() const {
    return available_ && impl_ && impl_->webviewReady;
}

void MermaidRenderer::Request(uint32_t srcOffset, const std::string& code,
                               RenderCallback cb) {
    if (!impl_ || !available_) {
        if (cb) cb(srcOffset, {});
        return;
    }

    if (impl_->webviewReady) {
        impl_->SendRenderRequest(srcOffset, code, std::move(cb));
    } else {
        std::lock_guard<std::mutex> lock(impl_->pendingMutex);
        impl_->pending.push({srcOffset, code, std::move(cb)});
    }
}

void MermaidRenderer::Shutdown() {
    if (impl_) {
        if (impl_->webview) {
            impl_->webview->remove_WebMessageReceived(impl_->messageToken);
            impl_->webview->remove_NavigationCompleted(impl_->navToken);
        }
        impl_->controller.Reset();
        impl_->webview.Reset();
        impl_->env.Reset();
        if (impl_->hidden_wnd) {
            DestroyWindow(impl_->hidden_wnd);
        }
        delete impl_;
        impl_ = nullptr;
    }
    available_ = false;
}

} // namespace mermaid

#else // !HAS_WEBVIEW2

namespace mermaid {

MermaidRenderer::MermaidRenderer() = default;
MermaidRenderer::~MermaidRenderer() = default;

bool MermaidRenderer::Init(HWND) { return false; }
bool MermaidRenderer::Available() const { return false; }
void MermaidRenderer::Request(uint32_t srcOffset, const std::string&,
                               RenderCallback cb) {
    if (cb) cb(srcOffset, {});
}
void MermaidRenderer::Shutdown() {}

} // namespace mermaid

#endif // HAS_WEBVIEW2
