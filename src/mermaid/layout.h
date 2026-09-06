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

}  // namespace mermaid
