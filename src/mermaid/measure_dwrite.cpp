// Task 11: DirectWrite implementation of MeasureFn. Windows-only.
// Live rendering path plugs this into LayoutFlowchartWith; tests use a stub.
#ifdef _WIN32
#include "layout.h"

#include <string>
#include <vector>
#include <windows.h>
#include <dwrite.h>

namespace mermaid {

struct DWriteMeasureCtx {
    IDWriteFactory* factory;
    IDWriteTextFormat* format;
};

static std::wstring Utf8ToUtf16(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

LabelSize MeasureWithDWrite(const std::string& utf8, float maxWidth, void* ctx) {
    LabelSize out{0.0f, 0.0f};
    auto* c = static_cast<DWriteMeasureCtx*>(ctx);
    if (!c || !c->factory || !c->format) return out;
    std::wstring w = Utf8ToUtf16(utf8);
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = c->factory->CreateTextLayout(
        w.c_str(), (UINT32)w.size(), c->format, maxWidth, 1e6f, &layout);
    if (FAILED(hr) || !layout) return out;
    DWRITE_TEXT_METRICS m{};
    if (SUCCEEDED(layout->GetMetrics(&m))) {
        out.width  = m.width;
        out.height = m.height;
    }
    layout->Release();
    return out;
}

}  // namespace mermaid
#endif  // _WIN32
