#pragma once

// The layout profile a Mermaid diagram is laid out with.
//
// Eclipse Layout Kernel is a Java library, and elkjs is a JavaScript one.
// This app ships neither a JVM nor a JavaScript engine: the Mermaid
// renderer is deliberately native, offline and dependency free. What ELK
// *is*, though, is a layered (Sugiyama) layout, and so is the engine
// already in src/mermaid/: cycle breaking, ranking by network simplex,
// ordering to reduce crossings, coordinate assignment by Brandes-Koepf,
// and routing through dummy nodes for the long edges. Those are ELK's own
// algorithms under `elk.layered`.
//
// So "use ELK" here means use ELK's settings and defaults over those
// phases, which is what this header describes: one struct naming the
// options, and a mapping onto the LayoutParams the engine already takes.
//
// ELK's numbers are the default. MARKDOWNIT_MERMAID_LAYOUT=dagre selects
// the older profile, which the golden oracle still uses: those goldens
// come from dagre-d3-es, so the comparison that pins the engine against
// it keeps running on the profile that can be compared with it.

#include "layout_internal.h"

namespace mermaid {

// ELK's layered options, with the values ELK documents as its defaults.
// The names are the option ids ELK uses, so the mapping back to its
// documentation is direct.
struct ElkSettings {
    // elk.spacing.nodeNode: between two nodes in the same layer.
    double spacing_node_node = 20.0;
    // elk.layered.spacing.nodeNodeBetweenLayers: between two layers.
    double spacing_node_node_between_layers = 20.0;
    // elk.spacing.edgeNode: between an edge and a node it passes.
    double spacing_edge_node = 10.0;
    // elk.spacing.edgeEdge: between two parallel edges.
    double spacing_edge_edge = 10.0;
    // elk.padding: the margin around the whole drawing.
    double padding = 12.0;
    // elk.layered.layering.strategy.
    const char* layering = "NETWORK_SIMPLEX";
    // elk.layered.nodePlacement.strategy.
    const char* node_placement = "BRANDES_KOEPF";
    // elk.layered.cycleBreaking.strategy.
    const char* cycle_breaking = "GREEDY";
    // elk.edgeRouting.
    const char* edge_routing = "ORTHOGONAL";
    // elk.layered.considerModelOrder.strategy. The engine orders by the
    // document, so the drawing reads in the order the author wrote it.
    const char* consider_model_order = "NODES_AND_EDGES";
};

ElkSettings ElkDefaults();

// The parameters the engine takes, for each profile.
LayoutParams DagreParams();
LayoutParams ElkParams();
LayoutParams ParamsFor(const ElkSettings& settings);

// The profile this process lays Mermaid diagrams out with, chosen once by
// MARKDOWNIT_MERMAID_LAYOUT. ELK unless the value is "dagre".
LayoutParams LayoutParamsForProfile();
const char* MermaidLayoutProfileName();

}  // namespace mermaid
