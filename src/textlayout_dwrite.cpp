// Windows implementation of the platform-neutral text layout seam.
//
// The shared layout cache asks a layout a handful of questions; this file
// answers them with DirectWrite. The macOS counterpart is
// src/textlayout_coretext.cpp.
//
// Positions are reported as UTF-16 code-unit indices, which is what
// DirectWrite uses natively and what the cache's u16ToSrc mapping is
// keyed on.

#include "textlayout.h"

#ifdef _WIN32

#include <dwrite.h>

#include <vector>

namespace textlayout {

TextLayoutHandle Create(const std::string& utf8, float maxWidth) {
    // The renderer owns the DirectWrite factory and the text format, and
    // it builds the layout itself, so there is no way to create one here
    // without duplicating that setup. The macOS side does create layouts
    // through this function, because the mac renderer has no separate
    // factory to borrow.
    (void)utf8;
    (void)maxWidth;
    return nullptr;
}

void Release(TextLayoutHandle handle) {
    if (handle) {
        handle->Release();
    }
}

uint32_t LineCount(TextLayoutHandle handle) {
    if (!handle) return 0;
    UINT32 count = 0;
    HRESULT hr = handle->GetLineMetrics(nullptr, 0, &count);
    if (FAILED(hr) || count == 0) return 0;
    return count;
}

namespace {

// Fetch the line metrics for the whole layout. The count has to be
// queried first: a fixed-size buffer silently truncates for layouts with
// more lines, which broke line selection in long code blocks.
bool FetchLineMetrics(TextLayoutHandle handle,
                      std::vector<DWRITE_LINE_METRICS>* out) {
    UINT32 count = 0;
    HRESULT hr = handle->GetLineMetrics(nullptr, 0, &count);
    if (FAILED(hr) || count == 0) return false;
    if (count > 4096) count = 4096;  // sanity cap
    out->resize(static_cast<size_t>(count));
    UINT32 got = 0;
    hr = handle->GetLineMetrics(out->data(), count, &got);
    if (FAILED(hr) || got == 0) return false;
    out->resize(static_cast<size_t>(got));
    return true;
}

}  // namespace

float LineHeight(TextLayoutHandle handle, uint32_t lineIndex) {
    if (!handle) return 0.0f;
    std::vector<DWRITE_LINE_METRICS> metrics;
    if (!FetchLineMetrics(handle, &metrics)) return 0.0f;
    if (lineIndex >= metrics.size()) return 0.0f;
    return metrics[lineIndex].height;
}

bool LineTextRange(TextLayoutHandle handle, uint32_t lineIndex,
                   float layoutWidth, uint32_t* outStart, uint32_t* outEnd) {
    if (!handle) return false;
    std::vector<DWRITE_LINE_METRICS> metrics;
    if (!FetchLineMetrics(handle, &metrics)) return false;
    if (lineIndex >= metrics.size()) return false;

    // DWRITE_LINE_METRICS carries no text positions, so the line's range
    // is found by hit testing the left and right edges of the line at mid
    // height. The right edge is probed at the layout width so the hit
    // lands on the line's last insertion position.
    const float lineY = metrics[lineIndex].height * 0.5f;
    DWRITE_HIT_TEST_METRICS htmStart = {};
    DWRITE_HIT_TEST_METRICS htmEnd = {};
    BOOL isTrailing = FALSE;
    BOOL isInside = FALSE;
    handle->HitTestPoint(0.0f, lineY, &isTrailing, &isInside, &htmStart);
    handle->HitTestPoint(layoutWidth, lineY, &isTrailing, &isInside, &htmEnd);
    if (outStart) *outStart = htmStart.textPosition;
    if (outEnd) *outEnd = htmEnd.textPosition + htmEnd.length;
    return true;
}

Hit HitTestPoint(TextLayoutHandle handle, float x, float y, bool* outTrailing) {
    Hit hit;
    if (!handle) return hit;
    DWRITE_HIT_TEST_METRICS htm = {};
    BOOL isTrailing = FALSE;
    BOOL isInside = FALSE;
    handle->HitTestPoint(x, y, &isTrailing, &isInside, &htm);
    hit.textPosition = htm.textPosition;
    hit.height = htm.height;
    hit.trailing = isTrailing != FALSE;
    hit.inside = isInside != FALSE;
    if (outTrailing) *outTrailing = hit.trailing;
    return hit;
}

Hit HitTestTextPosition(TextLayoutHandle handle, uint32_t textPosition,
                        float* outX, float* outY) {
    Hit hit;
    if (!handle) return hit;
    FLOAT x = 0.0f;
    FLOAT y = 0.0f;
    DWRITE_HIT_TEST_METRICS htm = {};
    BOOL isTrailing = FALSE;
    handle->HitTestTextPosition(textPosition, FALSE, &x, &y, &htm);
    hit.textPosition = htm.textPosition;
    hit.height = htm.height;
    if (outX) *outX = x;
    if (outY) *outY = y;
    return hit;
}

}  // namespace textlayout

#endif  // _WIN32
