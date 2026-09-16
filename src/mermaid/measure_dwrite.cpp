// DirectWrite implementation of the flowchart measurement seam.
#ifdef _WIN32
#include "measure_dwrite.h"

#include <string>
#include <windows.h>
#include <dwrite.h>

namespace mermaid {
namespace {
struct DWriteMeasureCtx {
    IDWriteFactory* factory = nullptr;
    IDWriteTextFormat* format = nullptr;
};

std::wstring Utf8ToUtf16(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                            w.data(), n) != n) return {};
    return w;
}

LabelSize MeasureWithDWrite(const std::string& label, float max_width, void* ctx) {
    const auto* c = static_cast<const DWriteMeasureCtx*>(ctx);
    if (!c || !c->factory || !c->format) return {0.0f, 0.0f};
    const auto lines = SplitLabelLines(label);
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) text.push_back('\n');
        text += lines[i];
    }
    const std::wstring w = Utf8ToUtf16(text);
    if (w.empty() && !text.empty()) return {0.0f, 0.0f};
    IDWriteTextLayout* layout = nullptr;
    const HRESULT hr = c->factory->CreateTextLayout(
        w.data(), static_cast<UINT32>(w.size()), c->format, max_width, 1.0e6f,
        &layout);
    if (FAILED(hr) || !layout) return {0.0f, 0.0f};
    DWRITE_TEXT_METRICS metrics{};
    const bool ok = SUCCEEDED(layout->GetMetrics(&metrics));
    layout->Release();
    return ok ? LabelSize{metrics.widthIncludingTrailingWhitespace, metrics.height}
              : LabelSize{0.0f, 0.0f};
}
}  // namespace

LaidOutFlowchart LayoutFlowchartWithDWrite(const Flowchart& flow,
                                           void* factory,
                                           void* format) {
    DWriteMeasureCtx ctx{static_cast<IDWriteFactory*>(factory),
                         static_cast<IDWriteTextFormat*>(format)};
    return LayoutFlowchartWith(flow, MeasureWithDWrite, &ctx);
}

}  // namespace mermaid
#endif  // _WIN32
