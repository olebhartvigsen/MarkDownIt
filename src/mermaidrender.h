#pragma once

#include "dom.h"
#include "mermaid/layout.h"
#include "mermaid/layout_cache.h"
#include "platform/seams.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// The Mermaid render preparation both renderers wrote for themselves: the
// label measure the flowchart layout calls, the relaid flowchart and its
// cache, and the block kind to MermaidRender switch. Shared by
// src/renderer.cpp and src/document_renderer.cpp.
//
// What a shell still decides is the label's face and its line box. The
// box is the one named difference in the plan's table: Windows measures
// the labels against a 1.3 em box with a 1.24 em baseline, the mac
// against the font's natural box. It is an argument here so neither port
// changes a pixel, and the face is built by the caller for the same
// reason. The two faces are the same values today (body face at
// bodyFontSize times points-to-DIPs, unzoomed because the painters apply
// the zoom once), so only the box argument differs in practice.

// The engine and the label face a flowchart measure needs.
struct MermaidLabelMeasure {
    platform::TextEngine* engine = nullptr;
    platform::FontSpec font;
    platform::LineBox box;
};

// One diagram label measured by the page's own text engine. The label is
// joined with the newline its own line breaks stand for, so the engine
// measures the same text the painter draws. Strictly positive maxWidth
// only: the layout asks for 200 points, and the alternative is an
// unbreakable line rather than a degenerate answer.
inline mermaid::LabelSize MermaidMeasureLabel(const std::string& label,
                                             float maxWidth, void* ctx) {
    const auto* c = static_cast<const MermaidLabelMeasure*>(ctx);
    if (!c || !c->engine) return {0.0f, 0.0f};
    const std::vector<std::string> lines = mermaid::SplitLabelLines(label);
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) text.push_back('\n');
        text += lines[i];
    }
    if (text.empty()) return {0.0f, 0.0f};
    const platform::TextMeasure measured = c->engine->MeasureText(
        text, c->font, maxWidth > 0.0f ? maxWidth : 1.0e6f, c->box);
    return {measured.width, measured.height};
}

// The flowchart the painter draws, the one layout object the paint and
// the charge then share. The relaid comes from the diagram engine when
// the fence measured up, the parse provisional layout otherwise; the
// cache keeps the two callers of a frame on one object.
inline std::shared_ptr<mermaid::LaidOutFlowchart> MermaidRelaidFlowchart(
    const Node& node, platform::TextEngine* engine,
    MermaidLabelMeasure* measure, mermaid::MermaidLayoutCache* cache,
    float zoom) {
    if (!node.mermaid_flowchart || node.mermaid_source.empty() || !engine)
        return node.mermaid_layout;
    if (auto cached = cache->Find(node.mermaid_source, zoom)) return cached;
    auto laid = std::make_shared<mermaid::LaidOutFlowchart>(
        mermaid::LayoutFlowchartWith(*node.mermaid_flowchart,
                                     MermaidMeasureLabel, measure));
    if (!laid->nodes.empty()) {
        cache->Put(node.mermaid_source, zoom, laid);
        return laid;
    }
    return node.mermaid_layout;
}

// The diagram a block draws, from the node's block kind. The flowchart's
// relaid layout is handed in, because only that kind runs one; the other
// four ride on the layout the parser made.
inline mermaid::MermaidRender MermaidDiagramFor(
    const Node& node, std::shared_ptr<mermaid::LaidOutFlowchart> flow) {
    mermaid::MermaidRender diagram;
    switch (node.block) {
        case BlockKind::MermaidPie:
            diagram.kind = mermaid::MermaidKind::Pie;
            diagram.pie = node.mermaid_pie;
            break;
        case BlockKind::MermaidSequence:
            diagram.kind = mermaid::MermaidKind::Sequence;
            diagram.seq = node.mermaid_seq;
            break;
        case BlockKind::MermaidGantt:
            diagram.kind = mermaid::MermaidKind::Gantt;
            diagram.gantt = node.mermaid_gantt;
            break;
        case BlockKind::MermaidState:
            diagram.kind = mermaid::MermaidKind::State;
            diagram.state = node.mermaid_state;
            break;
        case BlockKind::MermaidFlowchart:
        default:
            diagram.kind = mermaid::MermaidKind::Flowchart;
            diagram.flow = std::move(flow);
            break;
    }
    return diagram;
}
