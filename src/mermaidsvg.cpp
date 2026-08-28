#include "mermaidsvg.h"

#ifdef HAS_WEBVIEW2

#include <WebView2.h>
#include <wrl.h>
#include <wil/com.h>
#include <sstream>
#include <queue>
#include <mutex>

using namespace Microsoft::WRL;

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
// Receives render requests via window.chrome.webview.postMessage,
// renders with mermaid.render(), and posts the SVG back.
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
         << "    securityLevel: 'strict',"
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

struct MermaidRenderer::Impl {
    // COM interfaces
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    HWND parent = nullptr;
    HWND hidden_wnd = nullptr;

    // Pending requests waiting for WebView2 to be ready.
    struct PendingReq {
        uint32_t srcOffset = 0;
        std::string code;
        MermaidRenderer::Callback cb;
    };
    std::queue<PendingReq> pending;
    std::mutex pendingMutex;

    // Next render request ID
    int nextId = 1;

    // Callbacks waiting for SVG from JS
    struct OutstandingCb {
        uint32_t srcOffset;
        MermaidRenderer::Callback cb;
    };
    std::map<int, OutstandingCb> outstanding;
    std::mutex outstandingMutex;

    // Event to signal when WebView2 is ready
    bool webviewReady = false;

    // WebMessageReceived handler
    EventRegistrationToken messageToken = {};

    HRESULT OnMessageReceived(ICoreWebView2* sender,
                              ICoreWebView2WebMessageReceivedEventArgs* args) {
        LPWSTR jsonMsg = nullptr;
        args->TryGetWebMessageAsString(&jsonMsg);
        if (!jsonMsg) return S_OK;

        // Parse JSON: {"id": N, "svg": "..."}
        // Simple parse since we control the format.
        std::wstring wmsg(jsonMsg);
        CoTaskMemFree(jsonMsg);

        // Extract id
        int id = 0;
        {
            std::wstring idKey = L"\"id\":";
            size_t p = wmsg.find(idKey);
            if (p != std::wstring::npos) {
                p += idKey.size();
                while (p < wmsg.size() && (wmsg[p] == ' ' || wmsg[p] == '\t')) p++;
                std::wstring numStr;
                while (p < wmsg.size() && wmsg[p] >= '0' && wmsg[p] <= '9') {
                    numStr += wmsg[p];
                    p++;
                }
                id = std::stoi(numStr);
            }
        }

        // Extract svg (empty or actual SVG)
        std::string svg;
        {
            std::wstring svgKey = L"\"svg\":\"";
            size_t p = wmsg.find(svgKey);
            if (p != std::wstring::npos) {
                p += svgKey.size();
                // Read until closing quote, handling escapes
                std::wstring svgW;
                while (p < wmsg.size() && wmsg[p] != '"') {
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
                // Convert to UTF-8
                int len = WideCharToMultiByte(CP_UTF8, 0, svgW.c_str(),
                    static_cast<int>(svgW.size()), nullptr, 0, nullptr, nullptr);
                svg.resize(len);
                WideCharToMultiByte(CP_UTF8, 0, svgW.c_str(),
                    static_cast<int>(svgW.size()), &svg[0], len, nullptr, nullptr);
            }
        }

        // Call the callback
        {
            std::lock_guard<std::mutex> lock(outstandingMutex);
            auto it = outstanding.find(id);
            if (it != outstanding.end()) {
                it->second.cb(it->second.srcOffset, svg);
                outstanding.erase(it);
            }
        }

        return S_OK;
    }

    void ProcessPending() {
        std::lock_guard<std::mutex> lock(pendingMutex);
        while (!pending.empty() && webviewReady) {
            auto& req = pending.front();
            SendRenderRequest(req.srcOffset, req.code, std::move(req.cb));
            pending.pop();
        }
    }

    void SendRenderRequest(uint32_t srcOffset, const std::string& code,
                           MermaidRenderer::Callback cb) {
        int id = nextId++;
        {
            std::lock_guard<std::mutex> lock(outstandingMutex);
            outstanding[id] = {srcOffset, std::move(cb)};
        }

        // Send code to JS via postMessage
        // We need to escape the code for JSON
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

        // Convert to wide
        std::wstring wmsg;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(),
            static_cast<int>(msg.size()), nullptr, 0);
        wmsg.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(),
            static_cast<int>(msg.size()), &wmsg[0], wlen);

        webview->PostWebMessageAsString(wmsg.c_str());
    }
};

// Static callback thunks for COM
static HRESULT STDMETHODCALLTYPE WebMessageReceivedThunk(
    ICoreWebView2WebMessageReceivedEventHandler* This,
    ICoreWebView2* sender,
    ICoreWebView2WebMessageReceivedEventArgs* args) {
    auto* impl = reinterpret_cast<MermaidRenderer::Impl*>(This);
    // Actually we need a proper COM object. Let's use Callback<>.
    return S_OK;
}

MermaidRenderer::MermaidRenderer() = default;
MermaidRenderer::~MermaidRenderer() { Shutdown(); }

bool MermaidRenderer::Init(HWND parent) {
    if (!parent) return false;
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

    // Create WebView2 environment
    auto hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) return S_OK;
                impl_->env = env;

                // Create controller
                env->CreateCoreWebView2Controller(impl_->hidden_wnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) return S_OK;
                            impl_->controller = controller;
                            controller->get_CoreWebView2(&impl_->webview);

                            if (impl_->webview) {
                                // Set up message handler
                                auto handler = Callback<
                                    ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*,
                                        ICoreWebView2WebMessageReceivedEventArgs* args)
                                    -> HRESULT {
                                        return impl_->OnMessageReceived(nullptr, args);
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

                                // Navigate to data URI
                                std::wstring dataUri = L"data:text/html;charset=utf-8,"
                                    + htmlW;
                                impl_->webview->Navigate(dataUri.c_str());

                                // Wait for navigation to complete
                                // We'll mark ready on NavigationCompleted
                                impl_->webview->add_NavigationCompleted(
                                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [this](ICoreWebView2*,
                                            ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                            impl_->webviewReady = true;
                                            impl_->ProcessPending();
                                            return S_OK;
                                        }).Get(),
                                    &impl_->messageToken);
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
                               Callback cb) {
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

#else // !HAS_WEBVIEW2

MermaidRenderer::MermaidRenderer() = default;
MermaidRenderer::~MermaidRenderer() = default;

bool MermaidRenderer::Init(HWND) { return false; }
bool MermaidRenderer::Available() const { return false; }
void MermaidRenderer::Request(uint32_t srcOffset, const std::string&,
                               Callback cb) {
    if (cb) cb(srcOffset, {});
}
void MermaidRenderer::Shutdown() {}

#endif // HAS_WEBVIEW2
