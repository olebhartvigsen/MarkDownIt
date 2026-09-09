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
#include <memory>
#include <string>

#include "model.h"

#include "mermaid/pie_parse.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"

namespace mermaid {

enum class MermaidKind { Flowchart, Pie, Sequence };

// A parsed mermaid fence of any supported diagram kind. Exactly one shared
// pointer is set; kind says which.
struct MermaidRender {
    MermaidKind kind = MermaidKind::Flowchart;
    std::shared_ptr<LaidOutFlowchart> flow;
    std::shared_ptr<LaidOutPie> pie;
    std::shared_ptr<LaidOutSequence> seq;
};

// Padding used above and below the flowchart drawing when placed inline
// in a document. Kept here so measure and render can never drift.
constexpr float kMermaidBlockPad = 12.0f;

// Height in device-independent pixels of the mermaid block at the given
// zoom, constrained to the available width. Diagrams wider than the text
// column are scaled down to fit (same-fit rule as the SVG block path), so
// measure and draw must both pass the same availW or they will disagree.
inline float MeasureLayoutHeight(const LaidOutFlowchart& lo, float zoom,
                                 float availW) {
    // Lineær zoom: skaler ned så diagrammet passer i kolonnen, aldrig op.
    // scale = min(zoom, availW/naturalW) — IKKE zoom*fit, som bliver
    // kvadratisk i zoom, fordi selve kolonnebredden også skaleres med zoom.
    float scale = zoom;
    if (lo.width > 0 && availW > 0) {
        float fit_scale = availW / static_cast<float>(lo.width);
        if (fit_scale < scale) scale = fit_scale;
    }
    return static_cast<float>(lo.height) * scale + 2.0f * kMermaidBlockPad;
}

// Pie variant: canvas is 450 DIP tall, same padding scheme.
inline float MeasurePieHeight(const LaidOutPie& lp, float zoom, float availW) {
    // Samme lineære regel som draw-pathen (se MeasureLayoutHeight).
    float scale = zoom;
    if (lp.width > 0 && availW > 0) {
        float fit_scale = availW / static_cast<float>(lp.width);
        if (fit_scale < scale) scale = fit_scale;
    }
    return static_cast<float>(lp.height) * scale + 2.0f * kMermaidBlockPad;
}

// Sequence variant: vbwidth/vbheight already include the extra 40 for a title.
inline float MeasureSequenceHeight(const LaidOutSequence& ls, float zoom,
                                   float availW) {
    // Samme lineære regel som draw-pathen (se MeasureLayoutHeight).
    float scale = zoom;
    if (ls.width > 0 && availW > 0) {
        float fit_scale = availW / static_cast<float>(ls.width);
        if (fit_scale < scale) scale = fit_scale;
    }
    return static_cast<float>(ls.vbheight) * scale + 2.0f * kMermaidBlockPad;
}

// Height for either variant.
inline float MeasureMermaidHeight(const MermaidRender& mr, float zoom,
                                  float availW) {
    switch (mr.kind) {
        case MermaidKind::Pie:
            return MeasurePieHeight(*mr.pie, zoom, availW);
        case MermaidKind::Sequence:
            return MeasureSequenceHeight(*mr.seq, zoom, availW);
        case MermaidKind::Flowchart:
        default:
            return MeasureLayoutHeight(*mr.flow, zoom, availW);
    }
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
