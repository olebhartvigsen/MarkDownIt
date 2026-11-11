// The shared paragraph layout of plan Batch 3: the rules both
// renderers measure a paragraph with, ported from their code so there
// is one place to change them. The images of a text block ride above
// its text in every text bearing kind, so their charge is exported
// from here for the list and quote layouts too.

#include "layout/paragraph_layout.h"

#include "layout/text_runs.h"

#include <algorithm>
#include <string>
#include <vector>

namespace layout {
namespace {

// The placeholder of a blank paragraph: one space of the body size,
// the run both renderers substitute so an empty block keeps a line
// and the caret has a rect inside it.
std::vector<platform::GlyphRun> PlaceholderRuns(const LayoutInput& in) {
    Node node;
    InlineBlock child;
    child.kind = InlineKind::Text;
    child.text = U" ";
    node.children.push_back(child);
    return BuildRuns(node, in.metrics->bodyFontSize, false, false,
                      ToColor(in.palette->textPrimary), false, nullptr,
                      in.metrics->codeFontSize, in.palette->link,
                      in.metrics->linkUnderline);
}

}  // namespace

float ChargeForImages(const LayoutCtx& ctx, const Node& node,
                      LaidBlock* block, float top) {
    const LayoutInput& in = *ctx.in;
    // The images stack above the text, so the charge and the painted
    // rects are the same walk: an image's room is its fitted height
    // plus one paragraph gap, and the text starts below them all.
    float imageHeight = 0.0f;
    float y = top;
    for (const InlineBlock& child : node.children) {
        if (child.kind != InlineKind::Image || child.url.empty()) continue;

        ImageSize size;
        if (in.imageSize) size = in.imageSize(child.url);

        // The browser rule: intrinsic size, shrunk to the column when
        // it is wider, then scaled by the page zoom. The block's own
        // width is the column, because that is what the painter draws
        // the bitmap into. An unknown size keeps the placeholder: the
        // full width of the column and the fallback height, the same
        // reservation both renderers make until the image arrives.
        float drawnW = block->width > 0.0f ? block->width : in.contentWidth;
        float drawnH = kImageFallbackHeight;
        float naturalW = 0.0f;
        float naturalH = 0.0f;
        if (size.known && size.width > 0.0f && size.height > 0.0f) {
            naturalW = size.width;
            naturalH = size.height;
            const float column =
                block->width > 0.0f ? block->width : in.contentWidth;
            const float scale =
                std::min(1.0f, column / naturalW) * in.zoom;
            drawnW = naturalW * scale;
            drawnH = naturalH * scale;
        }

        display::Image item;
        item.url = child.url;
        item.kind = platform::ImageKind::Raster;
        item.rect.x = block->x;
        item.rect.y = y;
        item.rect.width = drawnW;
        item.rect.height = drawnH;
        item.naturalWidth = naturalW;
        item.naturalHeight = naturalH;
        ctx.list->items.push_back(item);

        imageHeight += drawnH + in.metrics->paraGap;
        y += drawnH + in.metrics->paraGap;
        block->imageUrls.push_back(child.url);
    }
    block->imagesHeight = imageHeight;
    return imageHeight;
}

LaidBlock LayoutParagraph(const LayoutCtx& ctx, const Node& node,
                          uint32_t nodeIndex, float* y) {
    const LayoutInput& in = *ctx.in;
    const LayoutMetrics& m = *in.metrics;
    LaidBlock block;
    block.kind = BlockKind::Paragraph;
    block.level = node.level;
    block.nodeIndex = nodeIndex;
    block.x = 0.0f;
    block.width = in.contentWidth;
    block.srcOffset = node.contentOffset;
    block.srcLength = node.contentLength;

    const float size = m.bodyFontSize;
    std::vector<platform::GlyphRun> runs =
        BuildRuns(node, size, false, false, ToColor(in.palette->textPrimary),
                  false, nullptr, m.codeFontSize, in.palette->link,
                  m.linkUnderline);
    if (runs.empty() && node.virtualEmptyParagraph) {
        runs = PlaceholderRuns(in);
    }

    std::vector<platform::TextLine> lines =
        in.engine->MeasureWrapped(runs, in.contentWidth, 0.0f);
    ApplyLineSpacing(&lines, size, m.bodyLineHeight, in.zoom, in.engine);
    if (!in.editing && node.virtualEmptyParagraph) {
        // While reading, the blank line next to a list is a thin strip.
        // Editing keeps the full line for the caret.
        for (platform::TextLine& line : lines) {
            line.height = kReadBlankLineStripDip;
        }
    }

    block.y = *y;
    const float imageHeight = ChargeForImages(ctx, node, &block, *y);

    float textY = *y + imageHeight;
    float textHeight = 0.0f;
    for (const platform::TextLine& line : lines) {
        display::Text item;
        item.x = block.x;
        item.top = textY;
        item.line = line;
        item.srcOffset = node.contentOffset;
        item.srcLength = node.contentLength;
        ctx.list->items.push_back(item);
        textY += line.height;
        textHeight += line.height;
    }
    block.lines = lines;

    block.height = imageHeight + textHeight;
    *y = textY;
    return block;
}

}  // namespace layout
