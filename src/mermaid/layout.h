#pragma once
// Public mermaid layout entry point.
#include "model.h"

namespace mermaid {

// Convert a parsed Flowchart to an internal LayoutGraph, applying the
// oracle's node sizing (CHAR_W/LINE_H/PADDING). Exposed so tests and the
// LayoutFlowchart wrapper share one path.
LayoutGraph FlowchartToLayoutGraph(const Flowchart& flow);

// Run the full pipeline: AssignRanks -> Normalize -> Order ->
// AssignCoordinates -> RouteEdges -> Denormalize, then compute the
// overall bounding box (including margin from LayoutParams).
LaidOutFlowchart LayoutFlowchart(const Flowchart& flow, const LayoutParams& p);

// Task 11: DirectWrite text measurement seam.
// LabelSize is the raw text metrics (no padding). LayoutFlowchartWith wraps
// labels at wrappingWidth = 200 and adds padding = 15 on every side, matching
// mermaid. Tests inject a deterministic stub MeasureFn; live rendering plugs
// in MeasureWithDWrite (see measure_dwrite.cpp, Windows-only).
struct LabelSize { float width; float height; };
using MeasureFn = LabelSize (*)(const std::string& utf8, float maxWidth, void* ctx);

LaidOutFlowchart LayoutFlowchartWith(const Flowchart& flow,
                                     MeasureFn measure,
                                     void* ctx,
                                     float zoom = 1.0f);

}  // namespace mermaid
