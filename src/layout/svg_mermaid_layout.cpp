// The shared charging of the picture blocks of plan Batch 3.2 step 7:
// the standalone SVG fence and the three Mermaid fences. Both entry
// points RESERVE the room of the picture; the renderers keep the
// drawing, because the SVG parsers and the mermaid painters are
// platform code.
//
// SVG, the picture form of a CodeBlock whose info string is "svg". It
// is a page image, not a code specimen: natural size times the zoom
// plus the two code pads, drawn with NO card (renderer_mac.cpp
// 1329-1339). The charged width may reach past the content column the
// way a browser scrolls a page image; it rides block.width, the walk's
// widest right edge. The natural size comes from the LayoutInput image
// size seam under the url convention "svg:" plus the raw fence text:
// the caller owns the parse and the cache and answers ImageSize over
// that string. An unknown size or a failed parse charges the code
// card instead, through LayoutCodeBlock, the fallback both renderers
// take for an SVG document that will not load (renderer.cpp 2569-2591).
//
// The caret: LayoutInput has no notion of one, so LayoutSvgBlock always
// lays out the picture form. The caller that can see the caret picks
// layout::LayoutCodeBlock for the fence it sits in while editing, the
// same code view the renderers switch to, and routes here only a
// stand alone .svg file's fence, another caller fact layout cannot see.
//
// Mermaid: the three kinds charge from the layouts the parser left on
// the node, mermaid_pie and mermaid_seq always, a flowchart through
// node.mermaid_layout. No engine is consulted, on purpose: the
// document engine belongs to the text and the painters keep their own
// diagram engine. That is a deliberate divergence from the mac
// renderer, which re-lays a flowchart out before measuring it; only
// the CHARGED height is shared now, from the parse time layout, while
// the painters may keep re-laying the diagram through their engine. A
// node with no layout at all, because mermaid_source never measured
// up, keeps the 100 DIP placeholder and no width, so the block still
// holds its room. Mermaid blocks emit NO display items: the renderers
// keep painting the diagrams themselves from the node layouts.

#include "layout/document_layout.h"
#include "layout/text_runs.h"
#include "mermaid/layout_cache.h"

#include <algorithm>
#include <string>

namespace layout {
namespace {

// The url identity of a standalone SVG fence under the image size
// callback: the "svg:" convention in front of the raw fence text,
// verbatim, the string the caller's parser consumes, so its cache can
// key the parsed document by it.
std::string SvgUrlOf(const Node& node) {
    return "svg:" + Utf32ToUtf8(node.raw);
}

// The width a mermaid diagram is measured against. The mac renderer
// keeps this split (renderer_mac.cpp 339-344, not exported from there):
// a flowchart fits into the column less twice the block pad, a pie and
// a sequence take the full column, because their painters do.
float MermaidMeasureWidth(const mermaid::MermaidRender& diagram,
                          float width) {
    const float pad = mermaid::kMermaidBlockPad;
    if (diagram.kind != mermaid::MermaidKind::Flowchart) return width;
    return width > 2.0f * pad ? width - 2.0f * pad : 0.0f;
}

// The natural width of the diagram kind at hand, or negative when the
// layout is absent.
float MermaidNaturalWidth(const mermaid::MermaidRender& diagram) {
    switch (diagram.kind) {
        case mermaid::MermaidKind::Pie:
            return diagram.pie ? static_cast<float>(diagram.pie->width)
                               : -1.0f;
        case mermaid::MermaidKind::Sequence:
            return diagram.seq ? static_cast<float>(diagram.seq->width)
                               : -1.0f;
        case mermaid::MermaidKind::Flowchart:
        default:
            return diagram.flow ? static_cast<float>(diagram.flow->width)
                                : -1.0f;
    }
}

}  // namespace

LaidBlock LayoutSvgBlock(const LayoutCtx& ctx, const Node& node,
                         uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    const LayoutMetrics& m = *in.metrics;

    ImageSize size;
    if (node.lang == "svg" && in.imageSize) {
        size = in.imageSize(SvgUrlOf(node));
    }

    // No svg fence, or no answer with a positive size yet: the block
    // is the code specimen the fence is, measured and painted by the
    // shared code path. Everything below charges the picture instead.
    if (node.lang != "svg" || !size.known || size.width <= 0.0f ||
        size.height <= 0.0f) {
        return LayoutCodeBlock(ctx, node, nodeIndex, y);
    }

    LaidBlock block;
    block.kind = BlockKind::CodeBlock;
    block.nodeIndex = nodeIndex;
    block.x = 0.0f;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    // A standalone .svg is a page image: natural size times the zoom,
    // the browser's 100 percent, with the code pads as its margin.
    const float drawnW = size.width * in.zoom;
    const float drawnH = size.height * in.zoom;
    const float blockH = drawnH + 2.0f * m.codePad;

    // The picture is one handle free Vector image, carried by the
    // list for a painter to resolve against its own SVG cache. The
    // renderers draw the document themselves today; an item painted
    // straight from this record lands at the same place.
    display::Image item;
    item.url = SvgUrlOf(node);
    item.kind = platform::ImageKind::Vector;
    item.rect.x = m.codePad;
    item.rect.y = *y + m.codePad;
    item.rect.width = drawnW;
    item.rect.height = drawnH;
    item.naturalWidth = size.width;
    item.naturalHeight = size.height;
    ctx.list->items.push_back(item);

    block.y = *y;
    block.width = drawnW + 2.0f * m.codePad;
    block.height = blockH;
    *y += blockH;
    return block;
}

LaidBlock LayoutMermaidBlock(const LayoutCtx& ctx, const Node& node,
                             uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;

    LaidBlock block;
    block.kind = node.block;
    block.nodeIndex = nodeIndex;
    block.x = 0.0f;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    // The layouts of the parse, never a second pass: pie and sequence
    // arrive measured at parse time, a flowchart keeps the provisional
    // layout the parser produced. The re-layout through the text
    // engine is the mac renderer's own step, one this module skips on
    // purpose; see the file comment.
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
        default:
            diagram.kind = mermaid::MermaidKind::Flowchart;
            diagram.flow = node.mermaid_layout;
            break;
    }

    const float natural = MermaidNaturalWidth(diagram);
    block.y = *y;
    if (natural >= 0.0f) {
        const float measureW =
            MermaidMeasureWidth(diagram, in.contentWidth);
        const float measured =
            mermaid::MeasureMermaidHeight(diagram, in.zoom, measureW);
        // The measure can answer nothing for a zero height diagram;
        // both renderers then keep the 100 DIP placeholder.
        block.height = measured > 0.0f ? measured : 100.0f;
        // The width contribution, like the widest right rule the mac
        // renderer keeps for a picture: the natural width shrunk to
        // the measure width when wider, never below 1:1, plus the two
        // block pads.
        float fit = 1.0f;
        if (measureW > 0.0f) fit = std::min(1.0f, measureW / natural);
        block.width = 2.0f * mermaid::kMermaidBlockPad + natural * fit;
    } else {
        // No layout at all: the placeholder room, no width. The gap
        // walk and the caret still see a charged block record.
        block.height = 100.0f;
        block.width = 0.0f;
    }
    *y += block.height;
    return block;
}

}  // namespace layout
