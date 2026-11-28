// The document renderer: the frame, the surface replay order and the
// queries an editor asks of one laid out document.
//
// It draws through the RenderTarget seam and measures through the
// TextEngine seam, so it names no platform graphics type. The mac targets
// compile it today; it reads the theme's mac aliases, so the Windows build
// joins when its theme spellings sit behind shared helpers. The layout is
// the shared one; the images, the SVG reader and
// the appearance come in through their own seams (platform::ImageSource,
// platform::SvgRaster, the palette the shell hands in).

#include "document_renderer.h"

#include "blocktext.h"
#include "caretrecords.h"
#include "itemspans.h"
#include "mermaidrender.h"
#include "svgtext.h"
#include "hash.h"
#include "layout/block_overlays.h"
#include "layout/chips.h"
#include "layout/frame.h"
#include "layout/display_player.h"
#include "layout/range_highlights.h"
#include "layout/selection_rects.h"
#include "layout/text_runs.h"
#include "platform/offset_target.h"
#include "markers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <variant>

#include "editcontroller.h"
#include "mermaid/layout_cache.h"
#include "mermaid/layout.h"
#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"
#include "mermaid/painter.h"
#include "navigation.h"

namespace platform {

namespace {
class NullImageSource final : public ImageSource {};
}  // namespace

ImageSource& NoImageSource() {
    static NullImageSource source;
    return source;
}

namespace {

// A colour from the palette, and the DOM's UTF-32 to the encoder's UTF-8,
// both live in the shared helpers (layout::ToColor, layout::Utf32ToUtf8):
// this shell calls them and keeps no copy of its own.

// The fallback image height and the reading blank strip moved into the
// shared modules with the pass that uses them, so the renderer no longer
// holds its own copy.

// The width of the text column when the word wrap is off. The text is
// laid out on one very wide line and the user scrolls to its end.
constexpr float kNoWrapContentWidthDip = 10000.0f;

// The theme sizes are points, and the Windows renderer converts them to
// DIPs with this factor (kPtToDip there) when it creates its text
// formats. The mac renderer applies the same factor, so a document lays
// out at the same em on both platforms and the line breaks land in the
// same places.
constexpr float kPtToDip = 96.0f / 72.0f;

// The rule room under an H1 or H2: GitHub pads the heading by .3em
// and draws a 1px border under the padding. Those two values live in
// LayoutMetrics (theme.h), shared with the Windows renderer.

// The spacing rules are shared with the Windows renderer in
// blockspacing.h, so the two platforms leave the same space
// between the same pair of blocks.

// A font, in the layout unit. The family override names the face a
// heading asks for: the theme spells semibold as a family of its own,
// the way the Windows build spells it in its heading formats.
//
// The theme keeps a family in the platform's spelling (UTF-8 here and a
// wide string on Windows), and the seam's FontSpec carries UTF-8, so
// the shared helper does the mapping on both ports. No value changes:
// it is the same name in the same bytes.
FontSpec MakeFont(const LayoutMetrics& metrics, float size, bool bold,
                  bool italic, bool monospace,
                  const char* family = nullptr) {
    FontSpec font;
    font.family = monospace
        ? ThemeFamilyUtf8(metrics.codeFont)
        : (family ? std::string(family) : ThemeFamilyUtf8(metrics.bodyFont));
    font.size = size;
    font.bold = bold;
    font.italic = italic;
    font.monospace = monospace;
    return font;
}

// The diagram preparation both renderers share lives in
// src/mermaidrender.h: the label measure, the relaid flowchart and its
// cache, and the block kind switch. This shell brings the label face and
// the font's natural line box, the mac half of the one named difference
// in the plan's table.

// True when the diagram has a layout to draw.
bool HasMermaidLayout(const mermaid::MermaidRender& diagram) {
    switch (diagram.kind) {
        case mermaid::MermaidKind::Pie: return diagram.pie != nullptr;
        case mermaid::MermaidKind::Sequence: return diagram.seq != nullptr;
        case mermaid::MermaidKind::Gantt: return diagram.gantt != nullptr;
        case mermaid::MermaidKind::State: return diagram.state != nullptr;
        case mermaid::MermaidKind::Flowchart:
        default: return diagram.flow != nullptr;
    }
}

// The maps the drawn text keeps back to the source, and the range
// rectangle rule over them, live in the shared header, Batch 3.3. The
// caret reads them through the shared cache now: the renderer builds
// one UTF-16 record per block and per cell and asks the cache.

// The engine the shared layout measures through.
//
// The shared modules speak points into the seam and bake the zoom into
// neither the font sizes nor the wrap width, because the Windows engine
// applies the zoom when it creates its text formats. The mac fonts carry
// the zoom in the FontSpec instead (MakeFont callers multiply by it), so
// the renderer wraps its engine in this adapter when it hands it to the
// shared walk: the runs gain the zoom on the way in, and everything the
// engine measures and returns comes back in the zoomed space the mac
// renderer has always drawn in. At a zoom of one the adapter is a
// pass-through, value for value.
class ZoomEngine final : public TextEngine {
public:
    ZoomEngine(TextEngine* inner, float zoom) : inner_(inner), zoom_(zoom) {}

    void BeginLayout() override { inner_->BeginLayout(); }

    std::vector<TextLine> MeasureWrapped(const std::vector<GlyphRun>& runs,
                                         float maxWidth,
                                         float lineSpacing) override {
        std::vector<GlyphRun> scaled = runs;
        if (zoom_ != 1.0f) {
            for (GlyphRun& run : scaled) run.font.size *= zoom_;
        }
        return inner_->MeasureWrapped(scaled, maxWidth, lineSpacing);
    }

    uint32_t HitTestOffset(const TextLine& line, float x,
                           bool* outTrailing) const override {
        return inner_->HitTestOffset(line, x, outTrailing);
    }

    Rect OffsetToRect(const TextLine& line, uint32_t offset) const override {
        return inner_->OffsetToRect(line, offset);
    }

    // The font the line spacing asks about comes out of a measured
    // line, so its size already carries the zoom.
    float LineHeight(const FontSpec& font) const override {
        return inner_->LineHeight(font);
    }

private:
    TextEngine* inner_;
    float zoom_;
};

}  // namespace

// The mermaid layout of a block, in the form the painter takes.
//
// A flowchart is laid out again with the diagram text engine measuring
// every label, the way the Windows build lays it out with DirectWrite,
// and the result is cached by fence source and zoom. Without an
// engine, or when the re-layout comes back empty, the parse-time
// layout stands.
// The flowchart the painter draws, the one layout object the paint and
// the charge then share. The relaid comes from the diagram engine when
// the fence measured up, the parse provisional layout otherwise; the
// cache keeps the two callers of a frame on one object.
std::shared_ptr<mermaid::LaidOutFlowchart> DocumentRenderer::FlowRelaidFor(
    const Node& node) const {
    MermaidLabelMeasure ctx;
    ctx.engine = diagramEngine_.get();
    ctx.font = DiagramFontFor(node, metrics_.bodyFontSize,
                              false, false, false);
    return MermaidRelaidFlowchart(node, diagramEngine_.get(), &ctx,
                                  &mermaid_layout_cache_, zoom_);
}

mermaid::MermaidRender DocumentRenderer::MermaidRenderFor(const Node& node) const {
    return MermaidDiagramFor(node, FlowRelaidFor(node));
}

DocumentRenderer::DocumentRenderer() {
    // The theme, until the caller sets its own: the same numbers the
    // Windows renderer starts from.
    baseMetrics_ = BaseMetrics();
    metrics_ = ComputeMetrics();
}
DocumentRenderer::~DocumentRenderer() = default;

FontSpec DocumentRenderer::DiagramFontFor(const Node& node, float baseSize,
                                      bool bold, bool italic,
                                      bool monospace) const {
    // The mermaid painters scale their text by the zoom themselves, the
    // same way the Windows measure format stays unzoomed.
    (void)node;
    return MakeFont(metrics_, baseSize * kPtToDip, bold, italic, monospace);
}

void DocumentRenderer::ApplyLineSpacing(std::vector<TextLine>* lines, float ptSize,
                                    float lineHeightEm) const {
    const float em = ptSize * kPtToDip * zoom_;
    // The CSS model: the line box is lineHeightEm times the font size,
    // and the glyphs are centred in it with half-leading. The font's
    // natural box and ascent come from Core Text, so a large face and
    // a small one sit in their line boxes the way a browser places
    // them rather than being pushed to the top by one fixed ratio.
    float natural = em;
    float fontAscent = em;
    if (engine_ && !lines->empty() && !lines->front().runs.empty()) {
        const float measured =
            engine_->LineHeight(lines->front().runs.front().font);
        if (measured > 0.0f) natural = measured;
        if (lines->front().ascent > 0.0f) fontAscent = lines->front().ascent;
    }
    const float height = lineHeightEm * em;
    const float ascent = (height - natural) * 0.5f + fontAscent;
    for (TextLine& line : *lines) {
        line.height = height;
        line.ascent = ascent;
    }
}

float DocumentRenderer::Measure(const Document& doc, float widthDip) {
    blocks_.clear();
    caretRecords_.clear();
    displayList_ = display::DisplayList{};
    needsLayout_ = false;

    // The engine drops the measurements of the pass before this one, so
    // the hit tester works against the layout that is about to be made.
    if (engine_) {
        engine_->BeginLayout();
    }

    if (widthDip <= 0.0f) {
        contentWidth_ = 1.0f;
        originX_ = EffectivePadX(widthDip);
        totalHeight_ = metrics_.padTop;
        return totalHeight_;
    }

    // The content column: the view width less the theme padding. The
    // column follows the window instead of taking a fixed reading
    // measure, so a wide window does not leave a narrow strip in the
    // middle. With the wrap off the text gets one very wide line, and
    // the user scrolls to its end.
    const float pad = EffectivePadX(widthDip);
    widestRight_ = 0.0f;
    contentWidth_ = wrap_ ? widthDip - pad * 2.0f
                          : kNoWrapContentWidthDip;
    if (contentWidth_ <= 0.0f) {
        contentWidth_ = 1.0f;
    }
    originX_ = pad;

    float y = metrics_.padTop;

    // The source view (find guide 4) lays the raw buffer out
    // instead of the document: one monospace block with every
    // byte on screen, so the caret, the selection and the find
    // scope work on raw bytes.
    if (sourceMode_) {
        const float sourceInset = SourceViewInset(metrics_);
        LayoutSourceBlocks(originX_ + sourceInset,
                           SourceViewTextWidth(contentWidth_, metrics_), &y);
        totalHeight_ = y + metrics_.padTop * 2.0f;
        BuildCaretCache();
        return totalHeight_;
    }

    // One shared pass. The walk of the document, the measuring of
    // every block kind and the display items they paint from are the
    // layout module's now; the renderer owns the seams it hands in:
    // the zoom-carrying engine adapter, the image size answers, and
    // the mapping of the records into the shape the caret, the hits
    // and the app layers read.
    ZoomEngine layoutEngine(engine_, zoom_);
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &metrics_;
    in.palette = &palette_;
    in.contentWidth = contentWidth_;
    in.zoom = zoom_;
    in.editing = editing_;
    in.engine = engine_ ? &layoutEngine : nullptr;
    in.imageSize = [this, &doc](const std::string& url) {
        return ImageSizeForUrl(doc, url);
    };
    // The flowchart relaid the paint shares with the walk, the same
    // layout object MermaidRenderFor hands the diagram painter: the
    // charged band is then the ink the frame paints.
    in.flowchartRelaid = [this](const Node& n) {
        return FlowRelaidFor(n);
    };
    layout::DocumentLayout laid = layout::LayoutDocument(in);
    displayList_ = std::move(laid.list);
    // The filter buttons the press can land on are placed at paint, where
    // the scroll is known: a pinned header row sits at the viewport's top
    // edge, so its buttons follow the scroll, not the walk.

    // The one Text item the modules build by hand is the list marker:
    // its line box borrows the measured first line, but the run in it
    // never passed the engine, so unlike every other run it is still
    // in the point space the modules speak. The marker is the only
    // Text item of the list with no source length of its own, no
    // measured width on its line, and the clip of the gutter around
    // it, so it is found here once and scaled to the zoomed space the
    // replay draws the rest of the document at.
    if (zoom_ != 1.0f) {
        bool insideMarkerClip = false;
        for (display::Item& item : displayList_.items) {
            if (std::holds_alternative<display::PushClip>(item)) {
                insideMarkerClip = true;
            } else if (std::holds_alternative<display::PopClip>(item)) {
                insideMarkerClip = false;
            } else if (auto* text =
                           std::get_if<display::Text>(&item)) {
                if (insideMarkerClip && text->srcLength == 0 &&
                    text->line.width == 0.0f) {
                    for (GlyphRun& run : text->line.runs) {
                        run.font.size *= zoom_;
                    }
                }
            }
        }
    }

    blocks_.reserve(laid.blocks.size());
    for (const layout::LaidBlock& lb : laid.blocks) {
        AppendBlockFromLaid(doc, lb);
    }

    // The caret's record of the same pass: one entry per drawn block and
    // per table cell, in UTF-16, so every caret, hit test and offset
    // question is the shared model's.
    BuildCaretCache();

    // The item spans of the blocks, computed once here. The walk
    // pushes its items in block order, the same order as the records
    // above, so one merge of the two streams names the range every
    // block owns: a block with no items keeps an empty range. The
    // frame replays the whole list in one pass today, so nothing
    // slices by these spans yet; they are the contract a future per
    // block replay will take. The merge itself is shared with the
    // Windows renderer, src/itemspans.h; the padTop is the one thing
    // this shell brings, because its records carry the frame origin.
    layout::MergeItemSpans(laid.blocks, displayList_, metrics_.padTop,
                           &blockItemSpans_);

    // The column starts one padTop under the top of the view and the
    // shared walk measures from the column's own top, so the height is
    // the pad, the walked height, and the pad the bottom margin keeps.
    totalHeight_ = displayList_.height + metrics_.padTop * 3.0f;
    // A picture wider than the column extends the scrollable area.
    // Without this its right-hand part could not be reached.
    widestRight_ = displayList_.width;
    if (widestRight_ > contentWidth_) contentWidth_ = widestRight_;
    return totalHeight_;
}

void DocumentRenderer::SetContentWidthMode(int mode) {
    // The mac shell pins this to full width. The width modes and their
    // cap are a Windows feature; this renderer ignores the cap.
    if (mode < 0 || mode > 3) {
        mode = 0;
    }
    contentWidthMode_ = mode;
    metrics_ = ComputeMetrics();
}

// A page's layout, taken outside the frame's own: the walk runs at 100% in
// reading mode, with the page's width and the light palette, and none of the
// frame's state is written. Printing a dark window onto white paper would be
// unreadable, and a page at the window's zoom would not be a page.
std::shared_ptr<const layout::DocumentLayout> DocumentRenderer::PrintLayout(
    const Document& doc, float contentWidthSeam) {
    const LayoutMetrics metrics = PaperMetrics(baseMetrics_);
    const Palette printPalette = BasePalette();

    ZoomEngine layoutEngine(engine_, 1.0f);
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &metrics;
    in.palette = &printPalette;
    in.contentWidth = contentWidthSeam;
    in.zoom = 1.0f;
    in.editing = false;
    in.engine = engine_ ? &layoutEngine : nullptr;
    in.imageSize = [this, &doc](const std::string& url) {
        return ImageSizeForUrl(doc, url);
    };
    in.flowchartRelaid = [this](const Node& n) {
        return FlowRelaidFor(n);
    };
    return std::make_shared<const layout::DocumentLayout>(
        layout::LayoutDocument(in));
}

LayoutMetrics DocumentRenderer::ComputeMetrics() const {
    LayoutMetrics m = ScaleMetrics(baseMetrics_, zoom_);
    // The mac pane stretches to the window and has no width mode. Leave
    // the shared cap untouched, but never consult it in this renderer.
    return m;
}

float DocumentRenderer::EffectivePadX(float widthDip) const {
    // A narrow pane gives the measure its room back: the side padding
    // slides from the theme's own value down to 16 as the pane narrows
    // under 640, the way the guideline asks a column to keep its width.
    const float narrow = 16.0f * zoom_;
    if (widthDip >= 640.0f) return metrics_.padX;
    const float t = std::max(0.0f, (widthDip - 400.0f) / 240.0f);
    return narrow + (metrics_.padX - narrow) * std::min(1.0f, t);
}

bool DocumentRenderer::SvgLinkInView(float viewX, float viewY,
                                std::string* href) const {
    const svg::LinkBox* hit = svg::LinkInAreas(svgLinkAreas_, viewX, viewY);
    if (!hit) return false;
    if (href) *href = hit->href;
    return true;
}

SvgScene* DocumentRenderer::GetSvgDoc(const Node& node) {
    // The scene is read once per block content, and reused while the same
    // block says the same thing. The reader is the platform's, behind
    // platform::SvgRaster; this body owns the cache and the appearance
    // remap, not the parsing.
    if (!svgRaster_) return nullptr;
    const uint64_t rawHash = Fnv1aUtf32(node.raw);
    for (const SvgEntry& entry : svgDocs_) {
        if (entry.srcOffset == node.srcOffset && entry.rawHash == rawHash) {
            return entry.scene.get();
        }
    }

    std::unique_ptr<SvgScene> scene = svgRaster_->NewScene();
    if (!scene) return nullptr;
    // The reader has no appearance of its own: light-dark() is resolved
    // to the branch the page shows before the XML reaches it, the same
    // rule the Windows renderer applies before Direct2D sees the file.
    // light-dark() and the dark-page remap both happen here, on the raw
    // markup, so the reader stays appearance-blind.
    const std::string resolvedText = svg::PrepareSvgForAppearance(
        layout::Utf32ToUtf8(node.raw), palette_.dark, Rgb24(palette_.pageBg),
        Rgb24(palette_.textPrimary));
    // A fenced block keeps its text in raw, the way the Windows draw
    // reads it; the inline children are empty for a fence.
    if (!scene->Load(resolvedText)) return nullptr;
    // A reader who opens a hundred documents should not keep a hundred
    // drawings alive.
    if (svgDocs_.size() >= 32) svgDocs_.clear();
    SvgEntry entry;
    entry.srcOffset = node.srcOffset;
    entry.rawHash = rawHash;
    entry.scene = std::move(scene);
    svgDocs_.push_back(std::move(entry));
    return svgDocs_.back().scene.get();
}

namespace {

// The per column edge and width of a laid out table, the shared
// record's split, with the equal fall back of the legacy record.
float TableColumnX(const LaidOutBlock::TableLayout& t, size_t c) {
    return c < t.colXs.size() ? t.colXs[c]
                              : static_cast<float>(c) * t.colW;
}
float TableColumnW(const LaidOutBlock::TableLayout& t, size_t c) {
    return c < t.colWs.size() ? t.colWs[c] : t.colW;
}

// The mac's answer to the same question the Windows DWriteRangeSource
// answers: where a range of one region sits, as rectangles. The mac already
// holds the record it drew, so the rectangles come from the shared rule
// (layout::AppendVisibleRangeRects) over the region's own lines, and the
// indices the shared wash pass hands in are the region's rendered bytes,
// because the map it was given is the region's visToSrc.
class MacRangeSource final : public TextRangeSource {
public:
    MacRangeSource(const std::vector<TextLine>& lines,
                   const std::vector<uint32_t>& visToSrc,
                   const TextEngine& engine, float x, float top,
                   ColumnAlign align, float innerWidth)
        : lines_(lines),
          visToSrc_(visToSrc),
          engine_(engine),
          x_(x),
          top_(top),
          align_(align),
          innerWidth_(innerWidth) {}

    void AppendRangeRects(uint32_t start, uint32_t end, float /*cullTop*/,
                          float /*cullBottom*/, bool trimBlanks,
                          std::vector<Rect>* out) override {
        if (!out || lines_.empty() || visToSrc_.empty()) return;
        layout::AppendVisibleRangeRects(lines_, engine_, x_, top_, start, end,
                                        out, align_, innerWidth_, trimBlanks);
    }

private:
    const std::vector<TextLine>& lines_;
    const std::vector<uint32_t>& visToSrc_;
    const TextEngine& engine_;
    float x_ = 0.0f;
    float top_ = 0.0f;
    ColumnAlign align_ = ColumnAlign::Default;
    float innerWidth_ = 0.0f;
};

}  // namespace

void DocumentRenderer::LayoutSourceBlocks(float x, float width, float* y) {
    if (!sourceText_ || sourceText_->empty()) return;

    // The whole buffer is one block of monospace text, with the line
    // breaks kept inside the run so Core Text breaks the lines: the
    // same shape the code block path measures, and the same rule the
    // Windows source view follows (one wrapped monospaced layout of
    // the raw buffer).
    LaidOutBlock block;
    block.kind = BlockKind::Paragraph;
    block.x = x;
    block.width = width;
    const std::string& text = *sourceText_;

    GlyphRun run;
    run.text = text;
    run.font = MakeFont(metrics_, metrics_.codeFontSize * kPtToDip * zoom_,
                        false, false, true);
    const Color color = layout::ToColor(palette_.textPrimary);
    run.color = color;
    std::vector<GlyphRun> runs;
    runs.push_back(std::move(run));

    block.lines = engine_->MeasureWrapped(runs, width, 0.0f);
    ApplyLineSpacing(&block.lines, metrics_.codeFontSize,
                     metrics_.codeLineHeight);
    for (const TextLine& line : block.lines) {
        block.height += line.height;
    }

    // Every byte is drawn in this mode, so the map to the source is
    // the identity over the whole buffer.
    layout::AppendCodeMapping(text, 0, &block.visToSrc, &block.visToSrcEnd);

    block.y = *y;
    *y += block.height;
    blocks_.push_back(std::move(block));
    AppendCaretRecords(blocks_.back());
}

void DocumentRenderer::AppendBlockFromLaid(const Document& doc,
                                      const layout::LaidBlock& lb) {
    // The shared records count the nodes; the walk never points past
    // the document it measured.
    if (lb.nodeIndex >= doc.nodes.size()) return;
    const Node& node = doc.nodes[lb.nodeIndex];

    // The shared layout is in content column coordinates, its first
    // block at the column's own top left; the renderer's records carry
    // the origin and the top pad the way they always did, so the caret,
    // the hits and the app layers keep reading the same geometry.
    LaidOutBlock block;
    block.node = &node;
    block.kind = lb.kind;
    block.level = lb.level;
    block.x = originX_ + lb.x;
    block.y = metrics_.padTop + lb.y;
    block.tableRecord = lb.table;
    block.width = lb.width;
    block.height = lb.height;
    block.lines = lb.lines;
    block.images = lb.imageUrls;
    block.imagesHeight = lb.imagesHeight;
    block.markerWidth = lb.markerWidth;

    // The table record ships its own rows and heights; the renderer's
    // cell layout adds what only it keeps: the alignment of the
    // column, and the per byte map of the cell's text.
    if (lb.kind == BlockKind::Table && lb.table) {
        block.table = &node;
        LaidOutBlock::TableLayout& layout = block.tableLayout;
        layout.colW = lb.table->colW;
        layout.columnCount = lb.table->columnCount;
        layout.colWs = lb.table->colWs;
        layout.colXs = lb.table->colXs;
        layout.rowHeights = lb.table->rowHeights;
        for (const std::vector<layout::LaidTableCell>& row :
             lb.table->cells) {
            std::vector<LaidOutBlock::TableCellLayout> cells;
            cells.reserve(row.size());
            for (const layout::LaidTableCell& cell : row) {
                LaidOutBlock::TableCellLayout laid;
                laid.lines = cell.lines;
                laid.srcCellStart = cell.srcCellStart;
                laid.srcCellEnd = cell.srcCellEnd;
                cells.push_back(std::move(laid));
            }
            layout.rows.push_back(std::move(cells));
        }
        for (size_t r = 0; r < node.rows.size() &&
                           r < layout.rows.size(); ++r) {
            const TableRow& sourceRow = node.rows[r];
            for (size_t c = 0; c < sourceRow.cells.size() &&
                               c < layout.rows[r].size(); ++c) {
                const TableCell& cell = sourceRow.cells[c];
                LaidOutBlock::TableCellLayout& laid =
                    layout.rows[r][c];
                laid.align = layout::CellAlign(node, sourceRow, c, metrics_);
                layout::AppendCellMapping(cell, &laid.visToSrc,
                                           &laid.visToSrcEnd);
            }
        }
    }

    // The map from the drawn text back to the source, so the caret,
    // the selection and the hit tester never land on a Markdown marker.
    // The shared records carry only the content range, because this
    // per byte walk is the reader side of the caret seam.
    switch (node.block) {
        case BlockKind::Heading:
        case BlockKind::Paragraph:
            layout::AppendInlineMapping(node.children, &block.visToSrc,
                                        &block.visToSrcEnd);
            if (block.visToSrc.empty() &&
                (node.virtualEmptyParagraph ||
                 node.block == BlockKind::Heading)) {
                // The placeholder line maps to the position the empty
                // paragraph or the empty heading keeps for its caret.
                layout::FinishMapping(node.contentOffset, &block.visToSrc,
                                      &block.visToSrcEnd);
            }
            break;

        case BlockKind::List:
            layout::AppendInlineMapping(node.children, &block.visToSrc,
                                        &block.visToSrcEnd);
            if (block.visToSrc.empty()) {
                // The marker keeps the item addressable.
                layout::FinishMapping(node.contentOffset, &block.visToSrc,
                                      &block.visToSrcEnd);
            }
            break;

        case BlockKind::BlockQuote:
            layout::AppendInlineMapping(node.children, &block.visToSrc,
                                        &block.visToSrcEnd);
            break;

        case BlockKind::CodeBlock: {
            // The fence's text lives in raw; the inline children are
            // empty for a code block.
            std::string code = layout::Utf32ToUtf8(node.raw);
            while (!code.empty() &&
                   (code.back() == '\n' || code.back() == '\r')) {
                code.pop_back();
            }
            layout::AppendCodeMapping(code, node.contentOffset,
                                      &block.visToSrc, &block.visToSrcEnd);
            break;
        }

        default:
            break;
    }

    blocks_.push_back(std::move(block));
    AppendCaretRecords(blocks_.back());
}

layout::ImageSize DocumentRenderer::ImageSizeForUrl(const Document& doc,
                                               const std::string& url) {
    layout::ImageSize size;
    if (url.rfind("svg:", 0) == 0) {
        // The picture questions arrive under the "svg:" convention,
        // the fence text itself. The picture form is the caller's own
        // fact, the same one the old measure asked for: a standalone
        // .svg file whose document parsed. Anything else answers
        // nothing, and the shared svg block lays the fence out as the
        // code specimen it is.
        if (!standaloneSvg_) return size;
        for (const Node& node : doc.nodes) {
            if (node.block != BlockKind::CodeBlock ||
                node.lang != "svg") {
                continue;
            }
            if (url != "svg:" + layout::Utf32ToUtf8(node.raw)) continue;
            SvgScene* svgDoc = GetSvgDoc(node);
            if (svgDoc && svgDoc->Width() > 0.0f &&
                svgDoc->Height() > 0.0f) {
                size.width = svgDoc->Width();
                size.height = svgDoc->Height();
                size.known = true;
            }
            break;
        }
        return size;
    }

    // A raster image: the fetch starts here the way it always did, an
    // image still on its way is unknown, and the pass that sees it
    // arrive asks for the document to be laid out again.
    images_->Prefetch(url);
    float naturalW = 0.0f;
    float naturalH = 0.0f;
    if (images_->GetSize(url, &naturalW, &naturalH) && naturalW > 0.0f &&
        naturalH > 0.0f) {
        size.width = naturalW;
        size.height = naturalH;
        size.known = true;
    } else {
        needsLayout_ = true;
    }
    return size;
}

BitmapHandle DocumentRenderer::ImageHandleFor(const display::Image& item) {
    // The picture of a standalone .svg is drawn by the renderer from
    // its own parsed document, so the item answers nothing here and
    // the player skips it. Everything else comes from the image cache.
    if (item.url.rfind("svg:", 0) == 0) return nullptr;
    return images_->Get(item.url);
}

void DocumentRenderer::SetMarkers(const std::vector<TextMatch>* markers) {
    markers_.clear();
    if (!markers) return;
    markers_ = *markers;
}

void DocumentRenderer::SetSearchMatches(const std::vector<TextMatch>* matches,
                                  int currentIndex) {
    searchMatches_.clear();
    searchCurrent_ = -1;
    if (!matches) {
        return;
    }
    searchMatches_ = *matches;
    searchCurrent_ = currentIndex;
}

void DocumentRenderer::PaintOverlays(float scrollOffset) {
    // The layers the replay cannot draw. The code card and the table
    // row cover the global washes with their opaque surface, so their
    // fills run again above the surface, and the glyphs are drawn once
    // more above the fills: identical fonts at identical positions, so
    // the second pass lands exactly on the first. The mermaid diagrams
    // and the svg pictures stay in the renderer, whose painters have
    // them measured their own way.
    const bool haveWashes = !markers_.empty() || !searchMatches_.empty();
    for (const LaidOutBlock& block : blocks_) {
        switch (block.kind) {
            case BlockKind::CodeBlock: {
                // A standalone SVG file is drawn as a picture, without
                // the card behind it.
                if (block.node && block.node->lang == "svg" &&
                    standaloneSvg_) {
                    SvgScene* svgDoc = GetSvgDoc(*block.node);
                    if (svgDoc && svgDoc->Width() > 0.0f &&
                        svgDoc->Height() > 0.0f) {
                        // Natural size times the zoom, the same rule the
                        // image size bridge measured for.
                        const float drawX = block.x + metrics_.codePad;
                        const float drawY = block.y + metrics_.codePad -
                                            scrollOffset;
                        svgDoc->Draw(*target_, *engine_,
                                     Rect{drawX, drawY,
                                          svgDoc->Width() * zoom_,
                                          svgDoc->Height() * zoom_});
                        // The picture's own links, in the space the draw
                        // used, so a click can be answered from the
                        // frame that is on screen.
                        SvgLinkArea area;
                        area.x = drawX;
                        area.y = drawY;
                        area.scale = zoom_;
                        area.links = svg::CollectLinks(
                            layout::Utf32ToUtf8(block.node->raw));
                        if (!area.links.empty()) {
                            svgLinkAreas_.push_back(std::move(area));
                        }
                        break;
                    }
                }
                if (haveWashes || hasSelection_) {
                    // The card is opaque, so its fills sit above it and
                    // under the glyphs, which the re-run puts back. The
                    // selection belongs to the same layer: drawn before
                    // the frame, the card covered it, so marking a line
                    // of code showed nothing.
                    if (haveWashes) PaintBlockHighlights(block, scrollOffset);
                    if (hasSelection_) {
                        for (const Rect& rect :
                             RectsForRangeInBlock(block, selFrom_, selTo_)) {
                            Rect drawn = rect;
                            drawn.y -= scrollOffset;
                            target_->FillRect(drawn, selColour_);
                        }
                    }
                    layout::PaintTextLines(*target_, block.lines,
                                           TextX(block),
                                           TextTop(block) - scrollOffset);
                }
                break;
            }

            case BlockKind::Table: {
                // The band a scrolled table pins is drawn by the view, which
                // is the only place that knows where its viewport's edge is
                // (DrawPinnedHeaders): the renderer lays the whole page out
                // and lets the scroll view move it.
                PaintTableOverlays(block, scrollOffset);
                break;
            }

            case BlockKind::MermaidFlowchart:
            case BlockKind::MermaidPie:
            case BlockKind::MermaidSequence:
            case BlockKind::MermaidGantt:
            case BlockKind::MermaidState: {
                // The diagram is painted by the mermaid painter, which
                // draws the shared layout through the seam. The painter
                // follows the zoom itself, so it gets the unzoomed
                // font.
                const mermaid::MermaidRender diagram =
                    MermaidRenderFor(*block.node);
                if (!HasMermaidLayout(diagram)) break;
                if (diagramEngine_) diagramEngine_->BeginLayout();
                const float pad = mermaid::kMermaidBlockPad;
                const float availWidth =
                    block.width > 2.0f * pad ? block.width - 2.0f * pad
                                             : 0.0f;
                PaintMermaidDiagram(
                    diagramEngine_ ? diagramEngine_.get() : engine_,
                    target_, diagram, zoom_, availWidth,
                    block.x, block.y - scrollOffset, pad, palette_,
                    DiagramFontFor(*block.node, metrics_.bodyFontSize,
                                   false, false, false),
                    metrics_.bodyLineHeight);
                break;
            }

            default:
                break;
        }
    }
}

void DocumentRenderer::Render(const Document& doc, float scrollOffset) {
    svgLinkAreas_.clear();
    (void)doc;
    if (!target_) return;

    // The global marker and search washes go under everything the
    // replay draws above them. A code card and a table row paint their
    // own opaque surface, which hides the washes, so their fills run
    // again after the replay, above that surface (below).
    PaintMarkerHighlights(scrollOffset);
    PaintSearchHighlights(scrollOffset);

    // The inline code pills of the whole list: opaque pills laid over
    // the surfaces the replay will draw (none of them under a plain text
    // block), with the block's washes re-run above them and under the
    // glyphs. One shared pass reads the display list's per run spans, so
    // a wrapped span is one pill with a cap at each real end, the cap
    // rule Windows adopts with it.
    layout::PaintInlineCodeChips(*target_, displayList_, originX_,
                                 metrics_.padTop - scrollOffset, metrics_,
                                 layout::ToColor(palette_.inlineCodeBg),
                                 zoom_, -1.0e30f, 1.0e30f);

    // A washed text block draws its marker wash above the chips and
    // under its glyphs, and there is no opaque surface in the list
    // between them: the replay puts the glyphs above both, the same
    // order the per block painter used.
    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::Heading ||
            block.kind == BlockKind::Paragraph ||
            block.kind == BlockKind::List ||
            block.kind == BlockKind::BlockQuote) {
            PaintBlockHighlights(block, scrollOffset);
        }
    }

    if (sourceMode_) {
        // The source view has no display items; its one block draws
        // the raw buffer itself, with the washes under the selection
        // and the glyphs on top. The selection is the renderer's here
        // (owner decision, 2026-11-28), in the same one paint pass
        // Windows has always used, so the view no longer fills it.
        for (const LaidOutBlock& block : blocks_) {
            if (hasSelection_) {
                for (const Rect& rect :
                     RectsForRangeInBlock(block, selFrom_, selTo_)) {
                    Rect drawn = rect;
                    drawn.y -= scrollOffset;
                    target_->FillRect(drawn, selColour_);
                }
            }
            layout::PaintTextLines(*target_, block.lines, TextX(block),
                                   TextTop(block) - scrollOffset);
        }
        return;
    }

    // One replay of the whole list under one translate: the items are
    // in content column coordinates, so the wrapper pins the column at
    // originX_ and scrolls by the offset, the same numbers the per
    // block painter always drew at. The player owns the run
    // decorations and the dash flattening, so a line, a link and a
    // table grid draw exactly as the seam spells them.
    platform::OffsetTarget translated(target_, originX_,
                                metrics_.padTop - scrollOffset);
    // The shared frame: the replay of the list, then the layers above the
    // surface it drew. The order is the owner's composition decision, and
    // the Windows shell calls the same function around the same list.
    layout::PaintFrame(
        translated, displayList_,
        [this](const display::Image& item) {
            return ImageHandleFor(item);
        },
        [&] { PaintOverlays(scrollOffset); });
}

void DocumentRenderer::PaintWashesInRegion(
    const std::vector<TextLine>& lines,
    const std::vector<uint32_t>& visToSrc, float x, float topDoc,
    ColumnAlign align, float innerWidth, uint32_t blockStart,
    uint32_t blockEnd, float scrollOffset, bool withMarkers,
    bool withSearch) const {
    if (!target_ || !engine_ || lines.empty() || visToSrc.size() < 2) return;

    // The map speaks the index space the shared pass computes in. On the
    // mac a block's rendered text is addressed in UTF-8 bytes, so the map
    // is the region's own visToSrc: index i is rendered byte i and its
    // value is the source byte it came from. The same vector answers the
    // geometry, which is why the two can never disagree.
    layout::OverlayRegion region;
    region.indexToSrc = &visToSrc;
    region.indexLen = static_cast<uint32_t>(visToSrc.size() - 1);
    region.blockStart = blockStart;
    region.blockEnd = blockEnd;
    region.x = x;
    region.top = topDoc - scrollOffset;

    MacRangeSource source(lines, visToSrc, *engine_, x, topDoc - scrollOffset,
                          align, innerWidth);
    // The mac record answers every line it drew, so the pass is given a
    // band no line falls outside; the cull is the Windows adapter's.
    const layout::PaintBand band{-1.0e9f, 1.0e9f};
    static const std::vector<TextMatch> kNoMatches;
    layout::PaintRegionWashes(
        *target_, source, region, band,
        withMarkers ? markers_ : kNoMatches,
        withSearch ? searchMatches_ : kNoMatches, searchCurrent_,
        layout::ToColor(palette_.markerBg),
        layout::ToColor(palette_.markerLine),
        layout::ToColor(palette_.findMatchBg),
        layout::ToColor(palette_.findCurrentMatchBg));
}

void DocumentRenderer::PaintMarkerHighlights(float scrollOffset) {
    if (!target_ || markers_.empty()) return;

    // The global layer of the page: a code card and a table row paint their
    // own opaque surface, so their marks are re-run above it by
    // PaintBlockHighlights and PaintTableRowHighlights. The fills and the
    // two point underline are layout::PaintMarkerHighlights, the pass the
    // Windows painter runs.
    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::CodeBlock ||
            block.kind == BlockKind::Table) {
            continue;
        }
        const uint32_t blockStart = block.node ? block.node->srcOffset : 0;
        const uint32_t blockEnd =
            block.node ? blockStart + block.node->srcLength : 0;
        PaintWashesInRegion(block.lines, block.visToSrc, TextX(block),
                            TextTop(block), ColumnAlign::Default, 0.0f,
                            blockStart, blockEnd, scrollOffset,
                            /*withMarkers=*/true, /*withSearch=*/false);
    }
}


void DocumentRenderer::SetSelection(uint32_t from, uint32_t to, Color colour) {
    hasSelection_ = to > from;
    selFrom_ = from;
    selTo_ = to;
    selColour_ = colour;
}

void DocumentRenderer::PaintBlockHighlights(const LaidOutBlock& block,
                                        float scrollOffset) const {
    const uint32_t blockStart = block.node ? block.node->srcOffset : 0;
    const uint32_t blockEnd =
        block.node ? blockStart + block.node->srcLength : 0;
    PaintWashesInRegion(block.lines, block.visToSrc, TextX(block),
                        TextTop(block), ColumnAlign::Default, 0.0f, blockStart,
                        blockEnd, scrollOffset, /*withMarkers=*/true,
                        /*withSearch=*/true);
}


void DocumentRenderer::PaintTableRowHighlights(const LaidOutBlock& block,
                                           std::size_t row,
                                           float rowTop, float rowHeight,
                                           float scrollOffset) {
    if (!target_ || (markers_.empty() && searchMatches_.empty())) return;
    if (!block.table || row >= block.tableLayout.rows.size()) return;

    // A row's washes are its cells' washes. Each cell is one region with
    // its own rendered to source map, the geometry the row painter drew,
    // so the shared pass answers exactly where the words sit and no
    // rectangle has to be filtered back into the row.
    const LaidOutBlock::TableLayout& layout = block.tableLayout;
    const std::vector<LaidOutBlock::TableCellLayout>& cells =
        layout.rows[row];
    for (size_t c = 0; c < cells.size(); ++c) {
        const float innerW =
            TableColumnW(layout, c) - 2.0f * metrics_.cellPadX;
        const float cellX = block.x + TableColumnX(layout, c);
        float cellTextHeight = 0.0f;
        for (const TextLine& line : cells[c].lines) {
            cellTextHeight += line.height;
        }
        const float lineY =
            rowTop + (std::max)(metrics_.cellPadY,
                                (rowHeight - cellTextHeight) * 0.5f);
        PaintWashesInRegion(cells[c].lines, cells[c].visToSrc,
                            cellX + metrics_.cellPadX, lineY, cells[c].align,
                            innerW, cells[c].srcCellStart, cells[c].srcCellEnd,
                            scrollOffset, /*withMarkers=*/true,
                            /*withSearch=*/true);
    }
}


void DocumentRenderer::PaintSearchHighlights(float scrollOffset) {
    if (!target_ || searchMatches_.empty()) return;

    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::CodeBlock ||
            block.kind == BlockKind::Table) {
            continue;
        }
        const uint32_t blockStart = block.node ? block.node->srcOffset : 0;
        const uint32_t blockEnd =
            block.node ? blockStart + block.node->srcLength : 0;
        PaintWashesInRegion(block.lines, block.visToSrc, TextX(block),
                            TextTop(block), ColumnAlign::Default, 0.0f,
                            blockStart, blockEnd, scrollOffset,
                            /*withMarkers=*/false, /*withSearch=*/true);
    }
}



void DocumentRenderer::PaintTableOverlays(const LaidOutBlock& block,
                                     float scrollOffset) {
    if (!target_ || !block.table) return;
    const Node& node = *block.table;
    const LaidOutBlock::TableLayout& layout = block.tableLayout;
    if (layout.rowHeights.empty()) return;

    // The stripe, the grid and the cell glyphs are already items the
    // replay drew, but the stripe is opaque: the washed layers run
    // again above it, row by row, with the glyphs drawn once more
    // above the chips, at the identical lines and positions, so the
    // re-run lands exactly on the replayed text.
    //
    // One row's paint is a call, so the pinned header row can be drawn
    // again at the viewport's top edge with the same geometry: the
    // replay put the band's fills and hairlines there, and this puts
    // the words back on them.
    const auto paintRow = [&](size_t r, float rowTopScreen,
                              bool withHighlights) {
        const std::vector<LaidOutBlock::TableCellLayout>& cells =
            layout.rows[r];
        const float rowH = layout.rowHeights[r];
        const float y = rowTopScreen;

        if (withHighlights) {
            PaintTableRowHighlights(block, r, y + scrollOffset, rowH,
                                    scrollOffset);
        }

        // The cell pills of this row, above the stripe the replay drew and
        // under the cell words below: one shared pass over the walk's
        // cellsText copy, banded to this row so a pinned copy of another
        // row is not painted twice.
        if (block.tableRecord && !block.tableRecord->cellsText.items.empty()) {
            const float originY =
                block.y - scrollOffset - block.tableRecord->headerTop;
            layout::PaintInlineCodeChips(
                *target_, block.tableRecord->cellsText, block.x, originY,
                metrics_, layout::ToColor(palette_.inlineCodeBg), zoom_,
                y - originY, y - originY + rowH);
        }

        for (size_t c = 0; c < cells.size(); ++c) {
            const float innerW = TableColumnW(layout, c) -
                2.0f * metrics_.cellPadX;
            const float cellX = block.x + TableColumnX(layout, c);

            Rect inner;
            inner.x = cellX + metrics_.cellPadX;
            inner.y = y + metrics_.cellPadY;
            inner.width = innerW > 0.0f ? innerW : 0.0f;
            inner.height = rowH - 2.0f * metrics_.cellPadY;
            if (inner.width > 0.0f && inner.height > 0.0f) {
                target_->PushClip(inner);
            }
            float cellTextHeight = 0.0f;
            for (const TextLine& line : cells[c].lines) {
                cellTextHeight += line.height;
            }
            float lineY =
                y + std::max(metrics_.cellPadY,
                             (rowH - cellTextHeight) * 0.5f);

            for (const TextLine& line : cells[c].lines) {
                float lineX = cellX + metrics_.cellPadX;
                if (cells[c].align == ColumnAlign::Center) {
                    lineX += (innerW - line.width) * 0.5f;
                } else if (cells[c].align == ColumnAlign::Right) {
                    lineX += innerW - line.width;
                }
                layout::PaintTextLine(*target_, line, lineX, lineY);
                lineY += line.height;
            }
            if (inner.width > 0.0f && inner.height > 0.0f) {
                target_->PopClip();
            }
        }
    };

    const size_t rows = (std::min)(node.rows.size(), layout.rows.size());
    float y = block.y - scrollOffset;
    for (size_t r = 0; r < rows; ++r) {
        const float rowH = layout.rowHeights[r];
        paintRow(r, y, true);
        y += rowH;
    }
}

// The band a table keeps at the viewport's top edge while its body scrolls
// under it. The view calls this after the page, with the visible top in the
// page's own coordinates: the band's items are the walk's, so the copy is
// replayed at that edge with one translate and nothing is re-measured. The
// copy carries its surface, its hairlines, its funnels and its words, so a
// pinned header needs no painter of its own and neither port can forget it.
void DocumentRenderer::DrawPinnedHeaders(RenderTarget* target, float visibleTop) {
    if (!target) return;
    tableFilterButtons_.clear();
    qaRenderCount_ += 1;
    qaViewportTopSeen_ = visibleTop;
    qaPinnedHeaderHeight_ = 0.0f;
    qaPinBlockY_ = -1.0f;
    qaPinBlockHeight_ = 0.0f;
    qaPinHeaderHeight_ = 0.0f;
    qaPinTableBlocks_ = 0;
    const bool landOnEdge = !editing_;
    for (const LaidOutBlock& block : blocks_) {
        if (!block.tableRecord) continue;
        qaPinTableBlocks_ += 1;
        if (qaPinBlockY_ < 0.0f) {
            qaPinBlockY_ = block.y;
            qaPinBlockHeight_ = block.height;
            qaPinHeaderHeight_ = block.tableRecord->headerHeight;
        }
        float pinTop = 0.0f;
        const bool pinned =
            landOnEdge && layout::PinnedHeader(
                              block.y, block.height,
                              block.tableRecord->headerHeight, visibleTop,
                              &pinTop);
        // The buttons the next press can land on, in the frame the reader is
        // looking at: a pinned row sits at the edge, so its buttons move with
        // it, and the copy already carries their marks.
        for (const Rect& rect : block.tableRecord->filterButtons) {
            Rect hit = rect;
            hit.x += originX_;
            hit.y = pinned ? pinTop : rect.y + metrics_.padTop;
            tableFilterButtons_.push_back(hit);
        }
        if (!pinned) continue;
        qaPinnedHeaderHeight_ = block.tableRecord->headerHeight;
        // The band's items are the walk's, measured from the header's own
        // top; the target draws document coordinates. Putting that top at the
        // viewport's edge is one translate of pinTop minus the header's walk
        // top, which is not the block's document y: the block carries the
        // page's padding on top of it, and using that number put the band one
        // padding above the edge, out of sight.
        platform::OffsetTarget band(target, originX_,
                              pinTop - block.tableRecord->headerTop);
        display::Play(block.tableRecord->header, &band,
                      [this](const display::Image& item) {
                          return ImageHandleFor(item);
                      });
    }
}

bool DocumentRenderer::YForOffset(uint32_t offset, float& y) const {
    // The caret record answers it: the block that owns the position, and
    // the line it is drawn on. A position on the seam between two table
    // cells belongs to the later one, which is the text in front of the
    // caret, so the last record whose range holds it wins.
    const std::vector<BlockLayout>& blocks = caretCache_.Blocks();
    int chosen = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const BlockLayout& bl = blocks[i];
        const uint32_t end = (std::max)(bl.srcCellEnd, bl.srcOffset + bl.srcLength);
        if (offset >= bl.srcOffset && offset <= end) chosen = static_cast<int>(i);
    }
    if (chosen < 0) return false;
    float x = 0.0f;
    float h = 0.0f;
    return caretCache_.OffsetToCaretRectIn(chosen, offset, &x, &y, &h);
}

float DocumentRenderer::TextX(const LaidOutBlock& block) const {
    const BlockTextInset inset =
        BlockTextInsetFor(block.kind, block.markerWidth, block.imagesHeight,
                          metrics_);
    return block.x + inset.x;
}

float DocumentRenderer::TextTop(const LaidOutBlock& block) const {
    const BlockTextInset inset =
        BlockTextInsetFor(block.kind, block.markerWidth, block.imagesHeight,
                          metrics_);
    return block.y + inset.top;
}

float DocumentRenderer::TextWidth(const LaidOutBlock& block) const {
    if (block.kind == BlockKind::CodeBlock) {
        return block.width - metrics_.codePad * 2.0f;
    }
    // The list gutter and a paragraph's zero inset come from the same
    // shared rule as TextX, so the width and the x cannot disagree.
    const BlockTextInset inset =
        BlockTextInsetFor(block.kind, block.markerWidth, block.imagesHeight,
                          metrics_);
    return block.width - inset.x;
}

// One entry per drawn block and per table cell, in the shared cache's
// shape. The records come from the append pass (AppendCaretRecords), not
// from a second pass over the painting list, and a shell that fills its
// own records (the Windows renderer builds them in its paint pass) hands
// them in through SetCaretRecords; both ways give the cache the same
// entry, src/caretrecords.h.
void DocumentRenderer::BuildCaretCache() {
    caretCache_.Clear();
    caretCache_.SetSourceText(sourceText_);
    caretCache_.SetSyntaxRevealed(sourceMode_);
    caretCache_.SetCaretOnLowerLine(wrappedCaretOnLowerLine_);

    for (const BlockLayout& bl : caretRecords_) caretCache_.Add(bl);
}

void DocumentRenderer::SetCaretRecords(std::vector<BlockLayout> records) {
    caretRecords_ = std::move(records);
    BuildCaretCache();
}

// The caret records of one block: its own text, or one entry per cell of a
// table. The geometry is the painting block's, the lines and the maps are
// the walk's, and the record shape is the shared one.
void DocumentRenderer::AppendCaretRecords(const LaidOutBlock& block) {
    if (block.kind == BlockKind::Table) {
        float rowTop = block.y;
        for (size_t r = 0; r < block.tableLayout.rows.size(); ++r) {
            const std::vector<LaidOutBlock::TableCellLayout>& cells =
                block.tableLayout.rows[r];
            const float rowH = r < block.tableLayout.rowHeights.size()
                ? block.tableLayout.rowHeights[r] : 0.0f;
            for (size_t c = 0; c < cells.size(); ++c) {
                const LaidOutBlock::TableCellLayout& cell = cells[c];
                if (cell.visToSrc.empty()) continue;
                const float cellX =
                    block.x + TableColumnX(block.tableLayout, c);
                const float innerW = TableColumnW(block.tableLayout, c) -
                    2.0f * metrics_.cellPadX;
                caretRecords_.push_back(caret::Record(
                    cell.lines, cellX + metrics_.cellPadX,
                    rowTop + metrics_.cellPadY, innerW,
                    caret::LinesHeight(cell.lines, rowH),
                    cell.srcCellStart,
                    cell.srcCellEnd > cell.srcCellStart
                        ? cell.srcCellEnd - cell.srcCellStart : 0,
                    cell.srcCellStart, cell.srcCellEnd, true, cell.align, -1.0f,
                    0, 0.0f, cell.visToSrc, cell.visToSrcEnd));
            }
            rowTop += rowH;
        }
        return;
    }
    if (block.lines.empty() || block.visToSrc.empty()) return;

    const uint32_t start = block.visToSrc.front();
    const uint32_t end = block.visToSrc.back();
    caretRecords_.push_back(caret::Record(
        block.lines, TextX(block), TextTop(block), TextWidth(block),
        caret::LinesHeight(block.lines, 0.0f),
        start, end > start ? end - start : 0, start, end, false,
        ColumnAlign::Default, block.x, 0, 0.0f, block.visToSrc,
        block.visToSrcEnd));
}

uint32_t DocumentRenderer::HitTestPoint(float x, float y) const {
    uint32_t offset = 0;
    return PointToOffset(x, y, &offset) ? offset : UINT32_MAX;
}

bool DocumentRenderer::PointToOffset(float x, float y, uint32_t* outOffset) const {
    const uint32_t at = caretCache_.PointToOffsetNearestLine(x, y);
    if (at == UINT32_MAX) return false;
    if (outOffset) *outOffset = caretCache_.NormalizeToRenderedCaret(at);
    return true;
}

uint32_t DocumentRenderer::NormalizeToRenderedCaret(uint32_t offset) const {
    return caretCache_.NormalizeToRenderedCaret(offset);
}

bool DocumentRenderer::OffsetIsRendered(uint32_t offset) const {
    return caretCache_.OffsetIsRendered(offset);
}

bool DocumentRenderer::RangeIsRendered(uint32_t start, uint32_t length) const {
    return caretCache_.RangeIsRendered(start, length);
}

bool DocumentRenderer::ClipToRendered(uint32_t start, uint32_t length,
                                 uint32_t* outStart, uint32_t* outEnd) const {
    return caretCache_.ClipToRendered(start, length, outStart, outEnd);
}

uint32_t DocumentRenderer::StepGrapheme(uint32_t offset, bool left) const {
    // One step through the drawn text: the shared model crosses the
    // markers around a cluster and never leaves a cell.
    return caretCache_.StepRendered(offset, left);
}

uint32_t DocumentRenderer::StepWord(uint32_t offset, bool left) const {
    if (!sourceText_) return offset;
    const uint32_t clamped = caretCache_.NormalizeToRenderedCaret(offset);
    const uint32_t target = left
        ? MoveWordLeftInString(*sourceText_, clamped)
        : MoveWordRightInString(*sourceText_, clamped);
    return caretCache_.NormalizeToRenderedCaret(target);
}

uint32_t DocumentRenderer::LineBoundary(uint32_t offset, bool end) const {
    return end ? MoveLineEnd(caretCache_, offset)
               : MoveLineStart(caretCache_, offset);
}

float DocumentRenderer::TextBottom() const {
    float bottom = 0.0f;
    for (const BlockLayout& bl : caretCache_.Blocks()) {
        float height = bl.height;
        if (bl.layout) height = bl.layout->Height();
        if (bl.y + height > bottom) bottom = bl.y + height;
    }
    return bottom;
}

bool DocumentRenderer::LineRangeAtPoint(float x, float y, uint32_t* outStart,
                                   uint32_t* outEnd) const {
    // The x is ignored on purpose: a click in the margin is level with
    // a line, and the line is what the user wants to select.
    (void)x;
    const int block = caretCache_.FindBlockAtY(y);
    if (block < 0) return false;
    return caretCache_.GetLineRangeAtY(block, y, outStart, outEnd);
}

bool DocumentRenderer::OffsetToRect(uint32_t offset, float* outX, float* outY,
                               float* outWidth, float* outHeight) const {
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
    if (!caretCache_.OffsetToCaretRect(offset, &x, &y, &h)) return false;
    if (outX) *outX = x;
    if (outY) *outY = y;
    // A caret is one point wide; the view draws it one point wide and
    // never reads this as a glyph box.
    if (outWidth) *outWidth = 1.0f;
    if (outHeight) *outHeight = h;
    return true;
}

bool DocumentRenderer::VerticalNeighbour(uint32_t offset, float desiredX, int delta,
                                   uint32_t* outOffset) const {
    if (delta == 0 || !outOffset) return false;
    // The page as the one shared step reads it: the cache's bands, and
    // MoveVerticalIn, the algorithm both ports run. The side of a wrap
    // the caret is drawn on is the setter's, already on the cache.
    float desired = desiredX;
    const uint32_t moved = MoveVertical(caretCache_, offset, delta, &desired,
                                        0.0f, 0.0f,
                                        wrappedCaretOnLowerLine_);
    if (moved == offset) return false;
    // The shared step clamps to the document's first or last line when
    // there is nothing in that direction; the mac's answer to the same
    // question is "no such line", and a position that did not leave its
    // own drawn line is exactly that case.
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
    float movedX = 0.0f;
    float movedY = 0.0f;
    float movedH = 0.0f;
    if (caretCache_.OffsetToCaretRect(offset, &x, &y, &h) &&
        caretCache_.OffsetToCaretRect(moved, &movedX, &movedY, &movedH) &&
        std::fabs(movedY - y) < 0.01f) {
        return false;
    }
    *outOffset = moved;
    return true;
}

std::vector<Rect> DocumentRenderer::SelectionRects(uint32_t start,
                                               uint32_t end) const {
    return RectsForRange(start, end);
}

std::vector<Rect> DocumentRenderer::RectsForRange(uint32_t start,
                                             uint32_t end) const {
    std::vector<Rect> rects;
    for (const LaidOutBlock& block : blocks_) {
        const std::vector<Rect> blockRects =
            RectsForRangeInBlock(block, start, end);
        rects.insert(rects.end(), blockRects.begin(), blockRects.end());
    }
    return rects;
}

void DocumentRenderer::AppendRangeRects(const std::vector<TextLine>& lines,
                                    const std::vector<uint32_t>& visToSrc,
                                    float x, float top, uint32_t start,
                                    uint32_t end,
                                    std::vector<Rect>* rects,
                                    ColumnAlign align,
                                    float innerWidth,
                                    bool trimBlanks) const {
    // Batch 3.3: the range rule is the shared helper's, so the mac and
    // the Windows renderer answer with the same rectangles. The helper
    // clamps a boundary outside the region's drawn span to the nearest
    // drawn edge, where this body used to drop the region outright, so
    // a selection crossing block boundaries washes every block it
    // covers, the word processor guide's selection criterion.
    layout::AppendRangeRects(lines, visToSrc, *engine_, x, top, start, end,
                             rects, align, innerWidth, trimBlanks);
}

std::vector<Rect> DocumentRenderer::RectsForRangeInBlock(const LaidOutBlock& block,
                                                     uint32_t start,
                                                     uint32_t end,
                                                     bool trimBlanks) const {
    std::vector<Rect> rects;

    // The block's own text.
    AppendRangeRects(block.lines, block.visToSrc,
                     TextX(block), TextTop(block), start, end, &rects,
                     ColumnAlign::Default, 0.0f, trimBlanks);

    // A table draws its text cell by cell, so a range inside a cell
    // gets the cell's own geometry. A row centers a short cell against
    // a taller one, the shift the painter applies to the glyphs, so the
    // rectangle band follows it: the top below is the painter's lineY,
    // where this body used to read the cell's top padding and answered
    // a band above the glyphs of every short cell in a wrapped row.
    if (block.kind == BlockKind::Table) {
        float rowTop = block.y;
        for (size_t r = 0; r < block.tableLayout.rows.size(); ++r) {
            const std::vector<LaidOutBlock::TableCellLayout>& cells =
                block.tableLayout.rows[r];
            const float rowH = r < block.tableLayout.rowHeights.size()
                ? block.tableLayout.rowHeights[r] : 0.0f;
            for (size_t c = 0; c < cells.size(); ++c) {
                const LaidOutBlock::TableCellLayout& cell = cells[c];
                float textH = 0.0f;
                for (const TextLine& line : cell.lines) {
                    textH += line.height;
                }
                const float lineY = rowTop +
                    (std::max)(metrics_.cellPadY,
                               (rowH - textH) * 0.5f);
                AppendRangeRects(
                    cell.lines, cell.visToSrc,
                    block.x + TableColumnX(block.tableLayout, c) +
                        metrics_.cellPadX,
                    lineY, start, end, &rects,
                    cell.align,
                    TableColumnW(block.tableLayout, c) -
                        2.0f * metrics_.cellPadX,
                    trimBlanks);
            }
            rowTop += rowH;
        }
    }
    return rects;
}

}  // namespace platform
