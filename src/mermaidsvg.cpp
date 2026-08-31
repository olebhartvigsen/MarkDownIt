#include "mermaidsvg.h"

#if HAS_WEBVIEW2

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
// Debug logging
// ---------------------------------------------------------------------------
static void DebugLog(const char* msg) {
    char path[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, path);
    if (len == 0) return;
    strcat_s(path, MAX_PATH, "markdownit-mermaid-debug.log");
    FILE* f = nullptr;
    fopen_s(&f, path, "a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, msg);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Dynamic loading of WebView2Loader.dll
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
    if (!g_webview2LoaderDll) {
        DebugLog("LoadWebView2Loader: LoadLibraryW failed");
        return false;
    }
    g_pfnCreateEnv = reinterpret_cast<PFN_CreateCoreWebView2EnvironmentWithOptions>(
        GetProcAddress(g_webview2LoaderDll, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!g_pfnCreateEnv) {
        DebugLog("LoadWebView2Loader: GetProcAddress failed");
        FreeLibrary(g_webview2LoaderDll);
        g_webview2LoaderDll = nullptr;
        return false;
    }
    DebugLog("LoadWebView2Loader: OK");
    return true;
}

// Read mermaid.min.js from RCDATA resource 101.
static std::string LoadMermaidJs() {
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!hRes) { DebugLog("LoadMermaidJs: FindResourceW failed"); return {}; }
    HGLOBAL hMem = LoadResource(hMod, hRes);
    if (!hMem) { DebugLog("LoadMermaidJs: LoadResource failed"); return {}; }
    DWORD size = SizeofResource(hMod, hRes);
    const char* data = static_cast<const char*>(LockResource(hMem));
    if (!data || size == 0) { DebugLog("LoadMermaidJs: LockResource/size failed"); return {}; }
    char buf[64];
    sprintf_s(buf, "LoadMermaidJs: %lu bytes", (unsigned long)size);
    DebugLog(buf);
    return std::string(data, size);
}

// Minimal HTML page — mermaid.js is injected separately via
// AddScriptToExecuteOnDocumentLoaded to avoid the ~2MB NavigateToString limit.
static std::string BuildMermaidHtml() {
    return "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
           "<style>body{margin:0;padding:0;}"
           "#container{position:absolute;left:-9999px;top:-9999px;}"
           "</style></head><body>"
           "<div id=\"container\"></div>"
           "<script>"
           "let mermaidReady = false;"
           "window.chrome.webview.addEventListener('message', async (e) => {"
           "  const {id, code} = e.data;"
           "  if (typeof mermaid === 'undefined' || !mermaidReady) {"
           "    window.chrome.webview.postMessage({id: id, svg: ''});"
           "    return;"
           "  }"
           "  try {"
           "    const {svg} = await mermaid.render('m' + id, code);"
           "    window.chrome.webview.postMessage({id: id, svg: svg});"
           "  } catch(err) {"
           "    window.chrome.webview.postMessage({id: id, svg: ''});"
           "  }"
           "});"
           "</script></body></html>";
}

// Script that initializes mermaid after it's loaded.
static std::string BuildMermaidInitScript(const std::string& mermaidJs) {
    return mermaidJs +
           "\ntry{mermaid.initialize({startOnLoad:false,htmlLabels:false,"
           "securityLevel:'loose',theme:'default'});mermaidReady=true;"
           "}catch(e){console.error('mermaid init failed:',e);}";
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

        char buf[80];
        sprintf_s(buf, "OnMessageReceived: id=%d svgLen=%zu", id, svg.size());
        DebugLog(buf);

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
        DebugLog("ProcessPending: queue size + sending");
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
    if (!parent) { DebugLog("Init: null parent"); return false; }

    if (!LoadWebView2Loader()) return false;

    impl_ = new Impl();
    impl_->parent = parent;

    // Create hidden child window for WebView2
    impl_->hidden_wnd = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT, 0, 0, 1, 1,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!impl_->hidden_wnd) {
        DebugLog("Init: CreateWindowExW failed");
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    DebugLog("Init: hidden window created");

    // Load mermaid.js from resource
    std::string mermaidJs = LoadMermaidJs();
    if (mermaidJs.empty()) {
        DestroyWindow(impl_->hidden_wnd);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    // Build the mermaid init script (mermaid.js + initialize call)
    std::string initScript = BuildMermaidInitScript(mermaidJs);

    // Build minimal HTML (no inline mermaid.js)
    std::string html = BuildMermaidHtml();

    // Convert HTML to wide string for NavigateToString
    std::wstring htmlW;
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, html.c_str(),
            static_cast<int>(html.size()), nullptr, 0);
        htmlW.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, html.c_str(),
            static_cast<int>(html.size()), &htmlW[0], wlen);
    }

    // Convert init script to wide string for AddScriptToExecuteOnDocumentLoaded
    std::wstring initScriptW;
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, initScript.c_str(),
            static_cast<int>(initScript.size()), nullptr, 0);
        initScriptW.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, initScript.c_str(),
            static_cast<int>(initScript.size()), &initScriptW[0], wlen);
    }

    DebugLog("Init: calling CreateCoreWebView2EnvironmentWithOptions");

    HRESULT hr = g_pfnCreateEnv(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, htmlW, initScriptW](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                char buf[80];
                sprintf_s(buf, "Env completed: hr=0x%08lX", (unsigned long)result);
                DebugLog(buf);
                if (FAILED(result) || !env) return S_OK;
                impl_->env = env;

                env->CreateCoreWebView2Controller(impl_->hidden_wnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, htmlW, initScriptW](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            char buf2[80];
                            sprintf_s(buf2, "Controller completed: hr=0x%08lX", (unsigned long)result);
                            DebugLog(buf2);
                            if (FAILED(result) || !controller) return S_OK;
                            impl_->controller = controller;
                            controller->get_CoreWebView2(&impl_->webview);

                            if (impl_->webview) {
                                DebugLog("Got ICoreWebView2");

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

                                // Inject mermaid.js as a script
                                DebugLog("Adding mermaid.js script");
                                impl_->webview->AddScriptToExecuteOnDocumentLoaded(
                                    initScriptW.c_str(),
                                    Microsoft::WRL::Callback<
                                        ICoreWebView2AddScriptToExecuteOnDocumentLoadedCompletedHandler>(
                                        [this](HRESULT error, PCWSTR id) -> HRESULT {
                                            char sb[80];
                                            sprintf_s(sb, "AddScript completed: hr=0x%08lX", (unsigned long)error);
                                            DebugLog(sb);
                                            return S_OK;
                                        }).Get());

                                // Navigate to minimal HTML
                                DebugLog("NavigateToString");
                                impl_->webview->NavigateToString(htmlW.c_str());

                                // NavigationCompleted -> mark ready
                                auto navHandler = Microsoft::WRL::Callback<
                                    ICoreWebView2NavigationCompletedEventHandler>(
                                        [this](ICoreWebView2*,
                                            ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                            BOOL success = FALSE;
                                            if (args) args->get_IsSuccess(&success);
                                            char sb[80];
                                            sprintf_s(sb, "NavigationCompleted: success=%d", (int)success);
                                            DebugLog(sb);
                                            impl_->webviewReady = true;
                                            impl_->ProcessPending();
                                            return S_OK;
                                        });
                                impl_->webview->add_NavigationCompleted(
                                    navHandler.Get(),
                                    &impl_->navToken);
                            } else {
                                DebugLog("get_CoreWebView2 returned null");
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    char hbuf[80];
    sprintf_s(hbuf, "CreateEnv returned: hr=0x%08lX", (unsigned long)hr);
    DebugLog(hbuf);

    if (FAILED(hr)) {
        DebugLog("Init: CreateEnv FAILED");
        DestroyWindow(impl_->hidden_wnd);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    available_ = true;
    DebugLog("Init: returning true (async init pending)");
    return true;
}

bool MermaidRenderer::Available() const {
    return available_ && impl_ && impl_->webviewReady;
}

bool MermaidRenderer::IsInitialized() const {
    return available_;
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
        char buf[80];
        sprintf_s(buf, "Request: queueing (offset=%u)", srcOffset);
        DebugLog(buf);
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
    DebugLog("Shutdown");
}

} // namespace mermaid

#else // !HAS_WEBVIEW2

namespace mermaid {

MermaidRenderer::MermaidRenderer() = default;
MermaidRenderer::~MermaidRenderer() = default;

bool MermaidRenderer::Init(HWND) { return false; }
bool MermaidRenderer::Available() const { return false; }
bool MermaidRenderer::IsInitialized() const { return false; }
void MermaidRenderer::Request(uint32_t srcOffset, const std::string&,
                               RenderCallback cb) {
    if (cb) cb(srcOffset, {});
}
void MermaidRenderer::Shutdown() {}

} // namespace mermaid

#endif // HAS_WEBVIEW2
