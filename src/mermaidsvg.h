#pragma once

// MermaidRenderer: converts mermaid source code to SVG via mermaid.js
// running in a hidden WebView2 instance.
//
// The flow is asynchronous:
// 1. Request(srcOffset, code, callback) queues a render.
// 2. WebView2 runs mermaid.js to produce SVG.
// 3. Callback is called with the SVG string (empty on failure).
//
// If WebView2 is not available, Available() returns false and all
// requests immediately fail (callback gets empty SVG).

#include <windows.h>
#include <string>
#include <functional>
#include <cstdint>

namespace mermaid {

class MermaidRenderer {
public:
    using RenderCallback = std::function<void(uint32_t srcOffset,
                                        const std::string& svg)>;

    MermaidRenderer();
    ~MermaidRenderer();

    // Initialize with a parent window. Returns false if WebView2
    // is not available on this system.
    bool Init(HWND parent);

    // True if WebView2 is ready and mermaid.js is loaded.
    bool Available() const;

    // Request an SVG render. The callback is called asynchronously
    // when the SVG is ready. Empty SVG string means rendering failed.
    void Request(uint32_t srcOffset, const std::string& code,
                 RenderCallback cb);

    // Shut down WebView2 and release resources.
    void Shutdown();

private:
    bool available_ = false;
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace mermaid
