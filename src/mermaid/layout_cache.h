// Mermaid layout cache helpers (Task 12 followup).
//
// Design note: the LaidOutFlowchart is already computed once per parse and
// stored on Node::mermaid_layout (see src/parser.cpp). Rendering therefore
// never re-runs layout on WM_PAINT; the shared_ptr on Node acts as the cache.
//
// The helpers below exist so that MeasureMermaidBlock and DrawMermaidBlock
// derive the same block height from the same inputs, preventing the class of
// scrollbar bugs where measure and paint disagree on how tall a block is.
// They also provide a stable hash of the fence source keyed by zoom, so that
// once MeasureFn (Task 11) forces a re-layout with real DirectWrite metrics,
// a genuine cache table can key on this value without further churn.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "model.h"

namespace mermaid {

// Padding used above and below the flowchart drawing when placed inline
// in a document. Kept here so measure and render can never drift.
constexpr float kMermaidBlockPad = 12.0f;

// Height in device-independent pixels of the mermaid block at the given
// zoom. Both MeasureMermaidBlock and DrawMermaidBlock must call this so
// they agree by construction.
inline float MeasureLayoutHeight(const LaidOutFlowchart& lo, float zoom) {
    return static_cast<float>(lo.height) * zoom + 2.0f * kMermaidBlockPad;
}

// Stable hash of the fence source keyed by zoom. Uses std::hash<std::string>
// mixed with the bit pattern of the zoom float, so identical source at the
// same zoom produces the same key and different sources or zooms diverge.
inline uint64_t HashFenceSource(const std::string& src, float zoom) {
    uint64_t h = static_cast<uint64_t>(std::hash<std::string>{}(src));
    uint32_t zbits = 0;
    std::memcpy(&zbits, &zoom, sizeof(zbits));
    // splitmix-style mix so zoom actually perturbs low bits.
    uint64_t k = h ^ (static_cast<uint64_t>(zbits) * 0x9E3779B97F4A7C15ULL);
    k ^= k >> 30;
    k *= 0xBF58476D1CE4E5B9ULL;
    k ^= k >> 27;
    k *= 0x94D049BB133111EBULL;
    k ^= k >> 31;
    return k;
}

} // namespace mermaid
