// The macOS renderer.
//
// The Windows renderer draws with Direct2D. This one draws with Core
// Graphics through the RenderTarget seam, and measures with Core Text
// through the TextEngine seam.
//
// The layout is the shared one: the theme metrics, the DOM structure and
// the mermaid engine are all reused, so the two platforms lay the same
// document out the same way. Only the drawing calls differ.

#include "renderer_mac.h"
#include "hash.h"
#include "layout/display_player.h"
#include "layout/selection_rects.h"
#include "layout/text_runs.h"
#include "markers.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

#include "editcontroller.h"
#include "mermaid/layout_cache.h"
#include "mermaid/layout.h"
#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"
#include "mermaid_painter_mac.h"
#include "navigation.h"
#include "svg_mac.h"

namespace platform {
namespace {

// A colour from the palette.
Color ToColor(const ThemeColor& c) {
    Color out;
    out.r = c.r;
    out.g = c.g;
    out.b = c.b;
    out.a = c.a;
    return out;
}

// The DOM holds UTF-32, the encoder is UTF-8, so each code point
// becomes one to four bytes.
std::string Utf32ToUtf8(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

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

// The inline code card: a small side padding (LayoutMetrics::inlineCodePadX,
// kept under half a space so it never reaches the neighbouring words), .2em of vertical
// padding and a 6px corner radius, the GitHub values.
constexpr float kInlineCodePadYRatio = 0.2f;
constexpr float kInlineCodeRadius = 6.0f;

// The spacing rules are shared with the Windows renderer in
// blockspacing.h, so the two platforms leave the same space
// between the same pair of blocks.

// A font, in the layout unit. The family override names the face a
// heading asks for: the theme spells semibold as a family of its own,
// the way the Windows build spells it in its heading formats.
FontSpec MakeFont(const LayoutMetrics& metrics, float size, bool bold,
                  bool italic, bool monospace,
                  const char* family = nullptr) {
    FontSpec font;
    font.family = monospace ? metrics.codeFont
                            : (family ? family : metrics.bodyFont);
    font.size = size;
    font.bold = bold;
    font.italic = italic;
    font.monospace = monospace;
    return font;
}

// The measurement the shared flowchart layout asks for, answered with
// the diagram text engine. The labels are joined with the same
// newline the DirectWrite bridge uses, so both platforms measure the
// same text, and the widest line and the summed heights are the raw
// text metrics the layout expects.
struct EngineMeasureCtx {
    TextEngine* engine = nullptr;
    FontSpec font;
};

mermaid::LabelSize MeasureWithEngine(const std::string& label, float maxWidth,
                                      void* ctx) {
    const auto* c = static_cast<const EngineMeasureCtx*>(ctx);
    if (!c || !c->engine) return {0.0f, 0.0f};

    const std::vector<std::string> lines = mermaid::SplitLabelLines(label);
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) text.push_back('\n');
        text += lines[i];
    }
    if (text.empty()) return {0.0f, 0.0f};

    GlyphRun run;
    run.text = text;
    run.font = c->font;
    const std::vector<TextLine> measured = c->engine->MeasureWrapped(
        {run}, maxWidth > 0.0f ? maxWidth : 1.0e6f, 0.0f);
    if (measured.empty()) return {0.0f, 0.0f};

    float width = 0.0f;
    float height = 0.0f;
    for (const TextLine& line : measured) {
        if (line.width > width) width = line.width;
        height += line.height;
    }
    return {width, height};
}


// True when the diagram has a layout to draw.
bool HasMermaidLayout(const mermaid::MermaidRender& diagram) {
    switch (diagram.kind) {
        case mermaid::MermaidKind::Pie: return diagram.pie != nullptr;
        case mermaid::MermaidKind::Sequence: return diagram.seq != nullptr;
        case mermaid::MermaidKind::Flowchart:
        default: return diagram.flow != nullptr;
    }
}

// The visible text of a run of lines, as the runs hold it.
std::string VisibleTextOfLines(const std::vector<TextLine>& lines) {
    std::string text;
    for (const TextLine& line : lines) {
        for (const GlyphRun& run : line.runs) text += run.text;
    }
    return text;
}

// The source position of a visible byte offset. The trailing flag asks
// for the end of the character at the position.
uint32_t SourceAtVisible(const std::vector<uint32_t>& visToSrc,
                          const std::vector<uint32_t>& visToSrcEnd,
                          uint32_t vis, bool trailing) {
    if (visToSrc.empty()) return 0;
    const uint32_t last = static_cast<uint32_t>(visToSrc.size() - 1);
    if (vis >= last) {
        return visToSrcEnd.empty() ? visToSrc[last] : visToSrcEnd.back();
    }
    if (trailing && vis < visToSrcEnd.size()) return visToSrcEnd[vis];
    return visToSrc[vis];
}

// The visible byte offset of a source position. Returns false when the
// block does not draw the position at all.
bool VisibleAtSource(const std::vector<uint32_t>& visToSrc, uint32_t src,
                      uint32_t* out) {
    if (visToSrc.empty()) return false;
    if (src < visToSrc.front() || src > visToSrc.back()) return false;
    const uint32_t last = static_cast<uint32_t>(visToSrc.size() - 1);
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(visToSrc.size());
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (visToSrc[mid] < src) lo = mid + 1;
        else hi = mid;
    }
    if (out) *out = lo > last ? last : lo;
    return true;
}

// The maps the drawn text keeps back to the source, and the range
// rectangle rule over them, live in the shared header now, Batch 3.3:
// the Windows renderer's twin answers from the same functions, so the
// selection wash holds one shape on both ports. What stays here is the
// mac renderer's own reading of the records (the caret and the hits),
// plus VisibleAtSource above, which the caret keeps all for itself.

// The width a mermaid block is measured against.
//
// The Windows build measures a flowchart against the width inside the
// block padding, and a pie and a sequence against the full column. The
// painters fit the diagram into the padded width, so the measurement
// follows the same split.
float MermaidMeasureWidth(const mermaid::MermaidRender& diagram,
                          float width) {
    const float pad = mermaid::kMermaidBlockPad;
    if (diagram.kind != mermaid::MermaidKind::Flowchart) return width;
    return width > 2.0f * pad ? width - 2.0f * pad : 0.0f;
}

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

// The target the finished display list replays onto.
//
// The list is in content column coordinates: origin at the top left
// of the column, y growing down, the scroll not applied. The renderer
// draws in view coordinates with the column pinned at originX_, so one
// translate wraps the real target and every replayed item lands where
// the per block painter used to draw it.
class TranslatedTarget final : public RenderTarget {
public:
    TranslatedTarget(RenderTarget* inner, float dx, float dy)
        : inner_(inner), dx_(dx), dy_(dy) {}

    void FillRect(const Rect& rect, const Color& color) override {
        inner_->FillRect(Shift(rect), color);
    }
    void FillRoundedRect(const Rect& rect, float radius,
                         const Color& color) override {
        inner_->FillRoundedRect(Shift(rect), radius, color);
    }
    void FillRoundedRectSides(const Rect& rect, float radius,
                              const Color& color, bool roundLeft,
                              bool roundRight) override {
        inner_->FillRoundedRectSides(Shift(rect), radius, color, roundLeft,
                                     roundRight);
    }
    void StrokeRect(const Rect& rect, const Color& color,
                    float lineWidth) override {
        inner_->StrokeRect(Shift(rect), color, lineWidth);
    }
    void StrokeRoundedRect(const Rect& rect, float radius,
                           const Color& color, float lineWidth) override {
        inner_->StrokeRoundedRect(Shift(rect), radius, color, lineWidth);
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                    const Color& color, float lineWidth) override {
        inner_->StrokeLine(x0 + dx_, y0 + dy_, x1 + dx_, y1 + dy_, color,
                           lineWidth);
    }
    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                  float baselineY, const Color& color) override {
        inner_->DrawText(utf8, font, x + dx_, baselineY + dy_, color);
    }
    void DrawBitmap(BitmapHandle image, const Rect& rect) override {
        inner_->DrawBitmap(image, Shift(rect));
    }
    void FillEllipse(const Rect& rect, const Color& color) override {
        inner_->FillEllipse(Shift(rect), color);
    }
    void StrokeEllipse(const Rect& rect, const Color& color,
                       float lineWidth) override {
        inner_->StrokeEllipse(Shift(rect), color, lineWidth);
    }
    void FillPolygon(const std::vector<Point>& points,
                     const Color& color) override {
        inner_->FillPolygon(Shift(points), color);
    }
    void StrokePolygon(const std::vector<Point>& points,
                       const Color& color, float lineWidth,
                       bool closed) override {
        inner_->StrokePolygon(Shift(points), color, lineWidth, closed);
    }
    void FillPieSlice(const Rect& rect, float startAngle, float endAngle,
                      const Color& color) override {
        inner_->FillPieSlice(Shift(rect), startAngle, endAngle, color);
    }
    void PushClip(const Rect& rect) override { inner_->PushClip(Shift(rect)); }
    void PopClip() override { inner_->PopClip(); }
    void DrawChevron(const Rect& rect, bool expanded,
                     const Color& color) override {
        inner_->DrawChevron(Shift(rect), expanded, color);
    }

private:
    Rect Shift(const Rect& rect) const {
        Rect out = rect;
        out.x += dx_;
        out.y += dy_;
        return out;
    }
    std::vector<Point> Shift(const std::vector<Point>& points) const {
        std::vector<Point> out = points;
        for (Point& p : out) {
            p.x += dx_;
            p.y += dy_;
        }
        return out;
    }

    RenderTarget* inner_;
    float dx_;
    float dy_;
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
std::shared_ptr<mermaid::LaidOutFlowchart> MacRenderer::FlowRelaidFor(
    const Node& node) const {
    if (node.mermaid_flowchart && !node.mermaid_source.empty() &&
        diagramEngine_) {
        if (auto cached = mermaid_layout_cache_.Find(node.mermaid_source,
                                                   zoom_)) {
            return cached;
        }
        EngineMeasureCtx ctx;
        ctx.engine = diagramEngine_.get();
        ctx.font = DiagramFontFor(node, metrics_.bodyFontSize,
                                  false, false, false);
        auto laid = std::make_shared<mermaid::LaidOutFlowchart>(
            mermaid::LayoutFlowchartWith(*node.mermaid_flowchart,
                                         MeasureWithEngine, &ctx));
        if (!laid->nodes.empty()) {
            mermaid_layout_cache_.Put(node.mermaid_source, zoom_, laid);
            return laid;
        }
    }
    return node.mermaid_layout;
}

mermaid::MermaidRender MacRenderer::MermaidRenderFor(const Node& node) const {
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
        case BlockKind::MermaidFlowchart:
        default: {
            diagram.kind = mermaid::MermaidKind::Flowchart;
            diagram.flow = FlowRelaidFor(node);
            break;
        }
    }
    return diagram;
}

MacRenderer::MacRenderer() {
    // The theme, until the caller sets its own: the same numbers the
    // Windows renderer starts from.
    baseMetrics_ = BaseMetrics();
    metrics_ = ComputeMetrics();
}
MacRenderer::~MacRenderer() = default;

FontSpec MacRenderer::DiagramFontFor(const Node& node, float baseSize,
                                      bool bold, bool italic,
                                      bool monospace) const {
    // The mermaid painters scale their text by the zoom themselves, the
    // same way the Windows measure format stays unzoomed.
    (void)node;
    return MakeFont(metrics_, baseSize * kPtToDip, bold, italic, monospace);
}

void MacRenderer::ApplyLineSpacing(std::vector<TextLine>* lines, float ptSize,
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

float MacRenderer::Measure(const Document& doc, float widthDip) {
    blocks_.clear();
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
        LayoutSourceBlocks(originX_, contentWidth_, &y);
        totalHeight_ = y + metrics_.padTop * 2.0f;
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

    // The item spans of the blocks, computed once here. The walk
    // pushes its items in block order, the same order as the records
    // above, so one merge of the two streams names the range every
    // block owns: a block with no items keeps an empty range. The
    // frame replays the whole list in one pass today, so nothing
    // slices by these spans yet; they are the contract a future per
    // block replay will take.
    blockItemSpans_.assign(blocks_.size(),
                           {displayList_.items.size(),
                            displayList_.items.size()});
    {
        size_t item = 0;
        size_t block = 0;
        const float padTop = metrics_.padTop;
        auto blockStartsBy = [&](size_t i) {
            return blocks_[i].y - padTop;
        };
        for (; item < displayList_.items.size(); ++item) {
            const display::Item& di = displayList_.items[item];
            float refY = 0.0f;
            bool hasY = false;
            if (const auto* fill = std::get_if<display::FillRect>(&di)) {
                refY = fill->rect.y; hasY = true;
            } else if (const auto* fill =
                           std::get_if<display::FillRoundedRect>(&di)) {
                refY = fill->rect.y; hasY = true;
            } else if (const auto* fill =
                           std::get_if<display::FillRoundedRectSides>(&di)) {
                refY = fill->rect.y; hasY = true;
            } else if (const auto* s =
                           std::get_if<display::StrokeRect>(&di)) {
                refY = s->rect.y; hasY = true;
            } else if (const auto* s =
                           std::get_if<display::StrokeRoundedRect>(&di)) {
                refY = s->rect.y; hasY = true;
            } else if (const auto* s =
                           std::get_if<display::StrokeLine>(&di)) {
                refY = s->y0; hasY = true;
            } else if (const auto* s =
                           std::get_if<display::StrokePolygon>(&di)) {
                if (!s->points.empty()) { refY = s->points[0].y; hasY = true; }
            } else if (const auto* f =
                           std::get_if<display::FillPolygon>(&di)) {
                if (!f->points.empty()) { refY = f->points[0].y; hasY = true; }
            } else if (const auto* e =
                           std::get_if<display::FillEllipse>(&di)) {
                refY = e->rect.y; hasY = true;
            } else if (const auto* e =
                           std::get_if<display::StrokeEllipse>(&di)) {
                refY = e->rect.y; hasY = true;
            } else if (const auto* pie =
                           std::get_if<display::PieSlice>(&di)) {
                refY = pie->bounds.y; hasY = true;
            } else if (const auto* t = std::get_if<display::Text>(&di)) {
                refY = t->top; hasY = true;
            } else if (const auto* img =
                           std::get_if<display::Image>(&di)) {
                refY = img->rect.y; hasY = true;
            } else if (const auto* clip =
                           std::get_if<display::PushClip>(&di)) {
                refY = clip->rect.y; hasY = true;
            }
            // PopClip carries no place of its own; it closes the clip
            // of the block the cursor is already in.
            if (!hasY) continue;
            while (block + 1 < blocks_.size() &&
                   blockStartsBy(block + 1) <= refY) {
                ++block;
            }
            if (blockItemSpans_[block].first > item) {
                blockItemSpans_[block].first = item;
            }
            blockItemSpans_[block].second = item + 1;
        }
    }

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

void MacRenderer::SetContentWidthMode(int mode) {
    // The mac shell pins this to full width. The width modes and their
    // cap are a Windows feature; this renderer ignores the cap.
    if (mode < 0 || mode > 3) {
        mode = 0;
    }
    contentWidthMode_ = mode;
    metrics_ = ComputeMetrics();
}

LayoutMetrics MacRenderer::ComputeMetrics() const {
    LayoutMetrics m = ScaleMetrics(baseMetrics_, zoom_);
    // The mac pane stretches to the window and has no width mode. Leave
    // the shared cap untouched, but never consult it in this renderer.
    return m;
}

float MacRenderer::EffectivePadX(float widthDip) const {
    // A narrow pane gives the measure its room back: the side padding
    // slides from the theme's own value down to 16 as the pane narrows
    // under 640, the way the guideline asks a column to keep its width.
    const float narrow = 16.0f * zoom_;
    if (widthDip >= 640.0f) return metrics_.padX;
    const float t = std::max(0.0f, (widthDip - 400.0f) / 240.0f);
    return narrow + (metrics_.padX - narrow) * std::min(1.0f, t);
}

const SvgDoc* MacRenderer::GetSvgDoc(const Node& node) {
    // The document is read once per block content, and reused while the
    // same block says the same thing.
    const uint64_t rawHash = Fnv1aUtf32(node.raw);
    for (const SvgEntry& entry : svgDocs_) {
        if (entry.srcOffset == node.srcOffset && entry.rawHash == rawHash) {
            return &entry.doc;
        }
    }

    SvgDoc doc;
    // A fenced block keeps its text in raw, the way the Windows draw
    // reads it; the inline children are empty for a fence.
    if (!doc.Load(Utf32ToUtf8(node.raw))) return nullptr;
    // A reader who opens a hundred documents should not keep a hundred
    // drawings alive.
    if (svgDocs_.size() >= 32) svgDocs_.clear();
    SvgEntry entry;
    entry.srcOffset = node.srcOffset;
    entry.rawHash = rawHash;
    entry.doc = std::move(doc);
    svgDocs_.push_back(std::move(entry));
    return &svgDocs_.back().doc;
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

}  // namespace

void MacRenderer::LayoutSourceBlocks(float x, float width, float* y) {
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
    const Color color = ToColor(palette_.textPrimary);
    run.r = color.r;
    run.g = color.g;
    run.b = color.b;
    run.a = color.a;
    std::vector<GlyphRun> runs;
    runs.push_back(std::move(run));

    block.lines = engine_->MeasureWrapped(
        runs, width - metrics_.codePad * 2.0f, 0.0f);
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
}

void MacRenderer::AppendBlockFromLaid(const Document& doc,
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
    block.width = lb.width;
    block.height = lb.height;
    block.lines = lb.lines;
    block.images = lb.imageUrls;
    block.imagesHeight = lb.imagesHeight;

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
            if (block.visToSrc.empty() && node.virtualEmptyParagraph) {
                // The placeholder line maps to the position the empty
                // paragraph keeps for its caret.
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
            std::string code = Utf32ToUtf8(node.raw);
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
}

layout::ImageSize MacRenderer::ImageSizeForUrl(const Document& doc,
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
            if (url != "svg:" + Utf32ToUtf8(node.raw)) continue;
            const SvgDoc* svgDoc = GetSvgDoc(node);
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
    images_.Prefetch(url);
    float naturalW = 0.0f;
    float naturalH = 0.0f;
    if (images_.GetSize(url, &naturalW, &naturalH) && naturalW > 0.0f &&
        naturalH > 0.0f) {
        size.width = naturalW;
        size.height = naturalH;
        size.known = true;
    } else {
        needsLayout_ = true;
    }
    return size;
}

BitmapHandle MacRenderer::ImageHandleFor(const display::Image& item) {
    // The picture of a standalone .svg is drawn by the renderer from
    // its own parsed document, so the item answers nothing here and
    // the player skips it. Everything else comes from the image cache.
    if (item.url.rfind("svg:", 0) == 0) return nullptr;
    return images_.Get(item.url);
}

void MacRenderer::SetMarkers(const std::vector<TextMatch>* markers) {
    markers_.clear();
    if (!markers) return;
    markers_ = *markers;
}

void MacRenderer::SetSearchMatches(const std::vector<TextMatch>* matches,
                                  int currentIndex) {
    searchMatches_.clear();
    searchCurrent_ = -1;
    if (!matches) {
        return;
    }
    searchMatches_ = *matches;
    searchCurrent_ = currentIndex;
}

void MacRenderer::Render(const Document& doc, float scrollOffset) {
    (void)doc;
    if (!target_) return;

    // The global marker and search washes go under everything the
    // replay draws above them. A code card and a table row paint their
    // own opaque surface, which hides the washes, so their fills run
    // again after the replay, above that surface (below).
    PaintMarkerHighlights(scrollOffset);
    PaintSearchHighlights(scrollOffset);

    // The inline code chips of the plain text blocks: opaque pills
    // with the block's washes re-run above them and under the glyphs
    // the replay draws. A renderer-side layer, because the chip rounds
    // the real edges of each wrapped span, which the per block painter
    // always drew from the block's own map.
    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::Heading ||
            block.kind == BlockKind::Paragraph ||
            block.kind == BlockKind::List ||
            block.kind == BlockKind::BlockQuote) {
            PaintInlineCodeBg(block, scrollOffset);
        }
    }

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
        // the raw buffer itself, with the washes under the glyphs.
        for (const LaidOutBlock& block : blocks_) {
            PaintLines(block.lines, TextX(block),
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
    TranslatedTarget translated(target_, originX_,
                                metrics_.padTop - scrollOffset);
    display::Play(displayList_, &translated,
                  [this](const display::Image& item) {
                      return ImageHandleFor(item);
                  });

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
                    const SvgDoc* svgDoc = GetSvgDoc(*block.node);
                    if (svgDoc && svgDoc->Width() > 0.0f &&
                        svgDoc->Height() > 0.0f) {
                        // Natural size times the zoom, the same rule the
                        // image size bridge measured for.
                        svgDoc->Draw(target_, engine_,
                                     block.x + metrics_.codePad,
                                     block.y + metrics_.codePad -
                                         scrollOffset,
                                     svgDoc->Width() * zoom_,
                                     svgDoc->Height() * zoom_);
                        break;
                    }
                }
                if (haveWashes) {
                    // The card is opaque, so its fills sit above it and
                    // under the glyphs, which the re-run puts back.
                    PaintBlockHighlights(block, scrollOffset);
                    PaintLines(block.lines, TextX(block),
                               TextTop(block) - scrollOffset);
                }
                break;
            }

            case BlockKind::Table: {
                PaintTableOverlays(block, scrollOffset);
                break;
            }

            case BlockKind::MermaidFlowchart:
            case BlockKind::MermaidPie:
            case BlockKind::MermaidSequence: {
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
                                   false, false, false));
                break;
            }

            default:
                break;
        }
    }
}

void MacRenderer::PaintMarkerHighlights(float scrollOffset) {
    if (!target_ || markers_.empty()) return;

    // A code card and a table row paint their own background after this
    // global layer. Their markers go through PaintBlockHighlights or
    // PaintTableRowHighlights, so the wash lands above that background.
    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::CodeBlock ||
            block.kind == BlockKind::Table) {
            continue;
        }
        for (const TextMatch& marker : markers_) {
            for (const Rect& rect : RectsForRangeInBlock(
                     block, marker.start, marker.start + marker.length,
                     true)) {
                Rect drawn = rect;
                drawn.y -= scrollOffset;
                target_->FillRect(drawn, ToColor(palette_.markerBg));

                // The line along the bottom, so a mark never relies on
                // colour alone.
                Rect line;
                line.x = drawn.x;
                line.y = drawn.y + drawn.height - 1.0f;
                line.width = drawn.width;
                line.height = 1.0f;
                target_->FillRect(line, ToColor(palette_.markerLine));
            }
        }
    }
}

void MacRenderer::PaintBlockHighlights(const LaidOutBlock& block,
                                        float scrollOffset) const {
    if (!target_ || (markers_.empty() && searchMatches_.empty())) return;

    for (const TextMatch& marker : markers_) {
        for (const Rect& rect : RectsForRangeInBlock(
                 block, marker.start, marker.start + marker.length, true)) {
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, ToColor(palette_.markerBg));
            Rect line;
            line.x = drawn.x;
            line.y = drawn.y + drawn.height - 1.0f;
            line.width = drawn.width;
            line.height = 1.0f;
            target_->FillRect(line, ToColor(palette_.markerLine));
        }
    }
    for (size_t i = 0; i < searchMatches_.size(); ++i) {
        const TextMatch& match = searchMatches_[i];
        const bool current = static_cast<int>(i) == searchCurrent_;
        for (const Rect& rect : RectsForRangeInBlock(
                 block, match.start, match.start + match.length)) {
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, current
                                        ? ToColor(palette_.findCurrentMatchBg)
                                        : ToColor(palette_.findMatchBg));
        }
    }
}

void MacRenderer::PaintTableRowHighlights(const LaidOutBlock& block,
                                           std::size_t row,
                                           float rowTop, float rowHeight,
                                           float scrollOffset) {
    (void)row;
    if (!target_ || (markers_.empty() && searchMatches_.empty())) return;

    const auto inRow = [rowTop, rowHeight](const Rect& rect) {
        return rect.y >= rowTop && rect.y < rowTop + rowHeight;
    };
    for (const TextMatch& marker : markers_) {
        for (const Rect& rect : RectsForRangeInBlock(
                 block, marker.start, marker.start + marker.length, true)) {
            if (!inRow(rect)) continue;
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, ToColor(palette_.markerBg));
            Rect line;
            line.x = drawn.x;
            line.y = drawn.y + drawn.height - 1.0f;
            line.width = drawn.width;
            line.height = 1.0f;
            target_->FillRect(line, ToColor(palette_.markerLine));
        }
    }
    for (size_t i = 0; i < searchMatches_.size(); ++i) {
        const TextMatch& match = searchMatches_[i];
        const bool current = static_cast<int>(i) == searchCurrent_;
        for (const Rect& rect : RectsForRangeInBlock(
                 block, match.start, match.start + match.length)) {
            if (!inRow(rect)) continue;
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, current
                                        ? ToColor(palette_.findCurrentMatchBg)
                                        : ToColor(palette_.findMatchBg));
        }
    }
}

void MacRenderer::PaintSearchHighlights(float scrollOffset) {
    if (!target_ || searchMatches_.empty()) return;

    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::CodeBlock ||
            block.kind == BlockKind::Table) {
            continue;
        }
        for (size_t i = 0; i < searchMatches_.size(); ++i) {
            const TextMatch& match = searchMatches_[i];
            const bool current = static_cast<int>(i) == searchCurrent_;
            for (const Rect& rect : RectsForRangeInBlock(
                     block, match.start, match.start + match.length)) {
                Rect drawn = rect;
                drawn.y -= scrollOffset;
                target_->FillRect(drawn, current
                                            ? ToColor(palette_.findCurrentMatchBg)
                                            : ToColor(palette_.findMatchBg));
            }
        }
    }
}


void MacRenderer::PaintLines(const std::vector<TextLine>& lines, float x,
                              float top) const {
    float y = top;
    for (const TextLine& line : lines) {
        PaintLineRuns(line, x, y);
        y += line.height;
    }
}

void MacRenderer::PaintLineRuns(const TextLine& line, float x,
                                 float y) const {
    if (!target_) return;

    for (size_t i = 0; i < line.runs.size(); ++i) {
        const GlyphRun& run = line.runs[i];
        Color color;
        color.r = run.r;
        color.g = run.g;
        color.b = run.b;
        color.a = run.a;
        target_->DrawText(run.text, run.font, x + run.x,
                          y + line.ascent, color);

        // The underline of a link and the strike-through of a struck
        // span. The seam has no draw call for the two decorations, so
        // they are stroked: two points under the baseline for the link,
        // and across the middle of the x for the strike.
        if (!run.underline && !run.strike) continue;
        float runWidth = line.width - run.x;
        if (i + 1 < line.runs.size()) {
            runWidth = line.runs[i + 1].x - run.x;
        }
        if (runWidth <= 0.0f) continue;
        if (run.underline) {
            target_->StrokeLine(x + run.x, y + line.ascent + 2.0f,
                                x + run.x + runWidth,
                                y + line.ascent + 2.0f, color, 1.0f);
        }
        if (run.strike) {
            const float strikeY = y + line.ascent - 0.30f * run.font.size;
            target_->StrokeLine(x + run.x, strikeY, x + run.x + runWidth,
                                strikeY, color, 1.0f);
        }
    }
}

void MacRenderer::PaintCodeRects(const std::vector<Rect>& rects,
                                  float scrollOffset) const {
    if (!target_) return;

    // GitHub's inline code card: a 6px rounded fill with .4em of
    // horizontal and .2em of vertical padding around the code run.
    // The padding and the rounded cap belong to the real start and
    // end of the span, not to every visual line: a wrapped span is
    // one run, so the middle fragments meet the line edges square.
    const float codeEm = metrics_.codeFontSize * kPtToDip * zoom_;
    const float padX = metrics_.inlineCodePadX * codeEm;
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const Rect& rect = rects[i];
        if (rect.height <= 0.0f) continue;
        const bool first = i == 0;
        const bool last = i + 1 == rects.size();
        const float boxH = std::min(rect.height, 1.6f * codeEm);
        Rect drawn;
        drawn.x = rect.x - (first ? padX : 0.0f);
        drawn.y = rect.y - scrollOffset + (rect.height - boxH) * 0.5f;
        drawn.width = rect.width + (first ? padX : 0.0f) +
                      (last ? padX : 0.0f);
        drawn.height = boxH;
        target_->FillRoundedRectSides(drawn, kInlineCodeRadius * zoom_,
                                      ToColor(palette_.inlineCodeBg),
                                      first, last);
    }
}

void MacRenderer::PaintInlineCodeBg(const LaidOutBlock& block,
                                     float scrollOffset) const {
    if (!target_ || !block.node) return;

    bool painted = false;
    uint32_t visible = 0;
    for (const InlineBlock& child : block.node->children) {
        if (child.kind == InlineKind::Image) continue;
        const uint32_t length =
            static_cast<uint32_t>(Utf32ToUtf8(child.text).size());
        if (length == 0) continue;
        if (child.code) {
            std::vector<Rect> rects;
            AppendVisibleRangeRects(block.lines, TextX(block), TextTop(block),
                                    visible, visible + length, &rects);
            // A wrapped code span keeps one run per visual line. If the
            // source-to-visible map cannot place the span (the marker
            // mapping can collapse a span to a zero-width edge), use the
            // monospace runs themselves: their text is the span's text,
            // split at the line breaks, and their x/width are already the
            // glyph positions.
            const std::string codeText = Utf32ToUtf8(child.text);
            std::vector<Rect> runRects;
            std::size_t matched = 0;
            float lineTop = TextTop(block);
            for (const TextLine& line : block.lines) {
                for (std::size_t i = 0; i < line.runs.size() && matched < codeText.size();
                     ++i) {
                    const GlyphRun& run = line.runs[i];
                    if (!run.font.monospace) continue;
                    if (run.text.size() > codeText.size() - matched) continue;
                    if (codeText.compare(matched, run.text.size(), run.text) != 0)
                        continue;
                    const float runRight =
                        i + 1 < line.runs.size()
                            ? line.runs[i + 1].x
                            : line.width;
                    Rect rect;
                    rect.x = TextX(block) + run.x;
                    rect.y = lineTop;
                    rect.width = runRight - run.x;
                    rect.height = line.height;
                    if (rect.width > 0.0f) runRects.push_back(rect);
                    matched += run.text.size();
                }
                lineTop += line.height;
            }
            if (matched == codeText.size() && !runRects.empty()) {
                PaintCodeRects(runRects, scrollOffset);
            } else {
                PaintCodeRects(rects, scrollOffset);
            }
            painted = true;
        }
        visible += length;
    }

    // The chip hides the wash painted in the global layer: it is opaque and
    // wider than its glyphs, so the box, not the mark, is what the reader
    // sees. Paint this block's washes again above the chips and below the
    // text, so a marked code span shows yellow on its own characters.
    if (painted) {
        PaintBlockHighlights(block, scrollOffset);
    }
}

void MacRenderer::PaintTableOverlays(const LaidOutBlock& block,
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
    float y = block.y - scrollOffset;
    for (size_t r = 0; r < node.rows.size() && r < layout.rows.size(); ++r) {
        const std::vector<LaidOutBlock::TableCellLayout>& cells =
            layout.rows[r];
        const float rowH = layout.rowHeights[r];

        PaintTableRowHighlights(block, r, y + scrollOffset, rowH,
                                scrollOffset);

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

            // A cell's code span gets the same wash as a paragraph
            // span. The geometry comes from the cell's own line box,
            // because a row centers a short cell against a taller one.
            const TableCell& sourceCell = node.rows[r].cells[c];
            for (const CellInlineSpan& span : sourceCell.inlineSpans) {
                if (!span.code || span.u16End <= span.u16Start) continue;
                uint32_t start = sourceCell.srcOffset;
                uint32_t end = sourceCell.srcEnd;
                if (span.u16Start < sourceCell.u16ToSrc.size()) {
                    start = sourceCell.u16ToSrc[span.u16Start];
                }
                const size_t last = static_cast<size_t>(span.u16End) - 1;
                if (last < sourceCell.u16ToSrcEnd.size()) {
                    end = sourceCell.u16ToSrcEnd[last];
                }
                if (end <= start) continue;
                std::vector<Rect> rects;
                AppendRangeRects(cells[c].lines, cells[c].visToSrc,
                                 cellX + metrics_.cellPadX,
                                 lineY + scrollOffset, start, end, &rects,
                                 cells[c].align, innerW);
                PaintCodeRects(rects, scrollOffset);
            }
            for (const TextLine& line : cells[c].lines) {
                float lineX = cellX + metrics_.cellPadX;
                if (cells[c].align == ColumnAlign::Center) {
                    lineX += (innerW - line.width) * 0.5f;
                } else if (cells[c].align == ColumnAlign::Right) {
                    lineX += innerW - line.width;
                }
                PaintLineRuns(line, lineX, lineY);
                lineY += line.height;
            }
            if (inner.width > 0.0f && inner.height > 0.0f) {
                target_->PopClip();
            }
        }
        y += rowH;
    }
}

bool MacRenderer::YForOffset(uint32_t offset, float& y) const {
    // The regions are the same ones the hit test reads, so a table cell
    // is addressable here too: a find jump inside a table lands on the
    // row that holds the match, and a block without a region has no y.
    //
    // A position on the seam between two regions goes to the later one:
    // both maps hold the end of their last character, which is where
    // the next region's text starts, and a match that starts there
    // belongs to the text in front of the caret.
    bool found = false;
    for (const TextRegion& region : TextRegions()) {
        if (offset < region.srcStart || offset > region.srcEnd) continue;
        uint32_t vis = 0;
        if (!VisibleAtSource(*region.visToSrc, offset, &vis)) continue;

        // The line the visible position sits on, the same walk the hit
        // test does.
        float lineY = region.top;
        uint32_t lineStart = 0;
        for (const TextLine& line : *region.lines) {
            uint32_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += static_cast<uint32_t>(run.text.size());
            }
            if (vis <= lineStart + length) break;
            lineStart += length;
            lineY += line.height;
        }
        y = lineY;
        found = true;
    }
    return found;
}

float MacRenderer::TextX(const LaidOutBlock& block) const {
    if (block.kind == BlockKind::List) {
        // The text sits after the marker gutter, the same reservation
        // the layout makes.
        return block.x + metrics_.listIndent * metrics_.markerRatio + metrics_.markerGutter;
    }
    if (block.kind == BlockKind::CodeBlock) {
        return block.x + metrics_.codePad;
    }
    return block.x;
}

float MacRenderer::TextTop(const LaidOutBlock& block) const {
    if (block.kind == BlockKind::CodeBlock) {
        return block.y + metrics_.codePad;
    }
    return block.y + block.imagesHeight;
}

float MacRenderer::TextWidth(const LaidOutBlock& block) const {
    if (block.kind == BlockKind::List) {
        return block.width -
               (metrics_.listIndent * metrics_.markerRatio + metrics_.markerGutter);
    }
    if (block.kind == BlockKind::CodeBlock) {
        return block.width - metrics_.codePad * 2.0f;
    }
    return block.width;
}

std::vector<MacRenderer::TextRegion> MacRenderer::TextRegions() const {
    std::vector<TextRegion> regions;
    for (const LaidOutBlock& block : blocks_) {
        if (block.kind == BlockKind::Table) {
            float rowTop = block.y;
            for (size_t r = 0; r < block.tableLayout.rows.size(); ++r) {
                const std::vector<LaidOutBlock::TableCellLayout>& cells =
                    block.tableLayout.rows[r];
                for (size_t c = 0; c < cells.size(); ++c) {
                    const LaidOutBlock::TableCellLayout& cell = cells[c];
                    if (cell.visToSrc.empty()) continue;
                    TextRegion region;
                    region.lines = &cell.lines;
                    region.visToSrc = &cell.visToSrc;
                    region.visToSrcEnd = &cell.visToSrcEnd;
                    region.trackX =
                        block.x + TableColumnX(block.tableLayout, c);
                    region.x = region.trackX + metrics_.cellPadX;
                    region.top = rowTop + metrics_.cellPadY;
                    region.width = TableColumnW(block.tableLayout, c) -
                        2.0f * metrics_.cellPadX;
                    if (cell.lines.empty() &&
                        r < block.tableLayout.rowHeights.size()) {
                        // An empty cell draws nothing, but its box stays
                        // addressable: a click in it parks the caret at
                        // the cell's content start.
                        region.emptyBandHeight =
                            block.tableLayout.rowHeights[r];
                    }
                    region.srcStart = cell.srcCellStart;
                    region.srcEnd = cell.srcCellEnd;
                    region.cell = true;
                    regions.push_back(region);
                }
                if (r < block.tableLayout.rowHeights.size()) {
                    rowTop += block.tableLayout.rowHeights[r];
                }
            }
        } else if (!block.lines.empty() && !block.visToSrc.empty()) {
            TextRegion region;
            region.lines = &block.lines;
            region.visToSrc = &block.visToSrc;
            region.visToSrcEnd = &block.visToSrcEnd;
            region.x = TextX(block);
            region.top = TextTop(block);
            region.width = TextWidth(block);
            region.trackX = block.x;
            region.srcStart = block.visToSrc.front();
            region.srcEnd = block.visToSrc.back();
            region.cell = false;
            regions.push_back(region);
        }
    }
    return regions;
}

void MacRenderer::CollectRenderedSpans(std::vector<RenderedSpan>* out) const {
    out->clear();
    for (const TextRegion& region : TextRegions()) {
        const std::vector<uint32_t>& visToSrc = *region.visToSrc;
        const std::vector<uint32_t>& visToSrcEnd = *region.visToSrcEnd;
        if (visToSrc.size() < 2) {
            // A block with no drawn text (a blank paragraph, an empty
            // cell) keeps one addressable position, its content start,
            // and a caret there is valid. A point span carries it, so
            // the normalizer leaves the position alone instead of
            // snapping it into a neighbour (table guideline 7).
            if (visToSrc.size() == 1 && visToSrcEnd.size() == 1) {
                RenderedSpan span;
                span.start = visToSrc.front();
                span.end = span.start;
                out->push_back(span);
            }
            continue;
        }
        for (size_t i = 0; i + 1 < visToSrc.size(); ++i) {
            RenderedSpan span;
            span.start = visToSrc[i];
            span.end = i < visToSrcEnd.size() ? visToSrcEnd[i] : span.start + 1;
            if (span.start == span.end) continue;
            out->push_back(span);
        }
    }
    std::sort(out->begin(), out->end(),
              [](const RenderedSpan& a, const RenderedSpan& b) {
                  return a.start < b.start;
              });
}

uint32_t MacRenderer::HitTestPoint(float x, float y) const {
    uint32_t offset = 0;
    return PointToOffset(x, y, &offset) ? offset : UINT32_MAX;
}

bool MacRenderer::PointToOffset(float x, float y, uint32_t* outOffset) const {
    const std::vector<TextRegion> regions = TextRegions();
    if (regions.empty()) return false;

    // An exact hit wins; otherwise the nearest line band does, with a
    // band the x is inside beating one that is only near, so a point in
    // a gap between two blocks still lands on a caret.
    int bestScore = -1;
    float bestDistance = 0.0f;
    size_t bestRegion = 0;
    size_t bestLine = 0;
    for (size_t r = 0; r < regions.size(); ++r) {
        const TextRegion& region = regions[r];
        const bool inX = x >= region.x && x <= region.x + region.width;
        if (region.lines->empty()) {
            // A cell with no drawn text: its box is the band.
            const float bandBottom = region.top + region.emptyBandHeight;
            const float distance = y < region.top
                ? region.top - y
                : (y > bandBottom ? y - bandBottom : 0.0f);
            const int score = (distance == 0.0f ? 2 : 0) + (inX ? 1 : 0);
            if (score > bestScore ||
                (score == bestScore && distance < bestDistance)) {
                bestScore = score;
                bestDistance = distance;
                bestRegion = r;
                bestLine = 0;
            }
            continue;
        }
        float lineTop = region.top;
        for (size_t i = 0; i < region.lines->size(); ++i) {
            const TextLine& line = (*region.lines)[i];
            const float lineBottom = lineTop + line.height;
            const float distance = y < lineTop
                ? lineTop - y
                : (y > lineBottom ? y - lineBottom : 0.0f);
            const bool inBand = distance == 0.0f;
            const int score = (inBand ? 2 : 0) + (inX ? 1 : 0);
            // Ties go to the line below: a point between two bands is
            // on its way down the page, so a Down key or a click under a
            // heading lands on the next line instead of staying on the
            // one above, where the move would go nowhere.
            if (score > bestScore ||
                (score == bestScore && distance <= bestDistance)) {
                bestScore = score;
                bestDistance = distance;
                bestRegion = r;
                bestLine = i;
            }
            lineTop = lineBottom;
        }
    }
    if (bestScore < 0) return false;

    const TextRegion& region = regions[bestRegion];
    // A point left of the region's track, the margin of the document
    // column, is not a caret; the caller keeps the Word-like line
    // selection. A cell owns its whole box, so a point in its padding
    // is still a caret in the cell.
    if (!region.cell && x < region.trackX) return false;

    if (region.lines->empty()) {
        // An empty cell: the caret parks at the cell's content start.
        uint32_t src = region.visToSrc->empty() ? region.srcStart
                                                : region.visToSrc->front();
        if (region.cell) {
            if (src < region.srcStart) src = region.srcStart;
            if (src > region.srcEnd) src = region.srcEnd;
        }
        if (outOffset) *outOffset = NormalizeToRenderedCaret(src);
        return true;
    }

    // The x inside the line, clamped to the text: a point past the
    // right end of a line is that line's last insertion position.
    float localX = x - region.x;
    if (localX < 0.0f) localX = 0.0f;
    const TextLine& line = (*region.lines)[bestLine];
    if (line.width > 0.0f && localX > line.width) localX = line.width;

    bool trailing = false;
    const uint32_t inLine = engine_->HitTestOffset(line, localX, &trailing);

    uint32_t lineStartVis = 0;
    for (size_t i = 0; i < bestLine; ++i) {
        for (const GlyphRun& run : (*region.lines)[i].runs) {
            lineStartVis += static_cast<uint32_t>(run.text.size());
        }
    }
    uint32_t vis = lineStartVis + inLine;
    const uint32_t lastIndex = static_cast<uint32_t>(region.visToSrc->size() - 1);
    if (vis > lastIndex) vis = lastIndex;

    uint32_t src = SourceAtVisible(*region.visToSrc, *region.visToSrcEnd, vis,
                                   trailing);
    if (region.cell) {
        // The binding rule: the caret lands in the clicked cell.
        if (src < region.srcStart) src = region.srcStart;
        if (src > region.srcEnd) src = region.srcEnd;
    }
    if (outOffset) *outOffset = NormalizeToRenderedCaret(src);
    return true;
}

uint32_t MacRenderer::NormalizeToRenderedCaret(uint32_t offset) const {
    std::vector<RenderedSpan> spans;
    CollectRenderedSpans(&spans);
    if (spans.empty()) return offset;

    uint32_t previous = UINT32_MAX;
    uint32_t next = UINT32_MAX;
    for (const RenderedSpan& span : spans) {
        if (offset == span.start) return offset;
        if (offset == span.end) {
            // The position just past a character: the markers that
            // follow it are stepped over, so a caret at the visual end
            // of bold or italic text stays outside the syntax.
            uint32_t normalized = span.end;
            // The source view draws the markers too, so nothing
            // is stepped over there (find guide 4).
            if (sourceText_ && !sourceMode_) {
                const std::string& text = *sourceText_;
                while (normalized < text.size() &&
                       (text[normalized] == '*' || text[normalized] == '_' ||
                        text[normalized] == '`' || text[normalized] == '~')) {
                    ++normalized;
                }
                // Link text ends before `](destination)`. Keep a
                // position at that visual end outside the hidden link
                // syntax as well.
                if (normalized + 1 < text.size() &&
                    text[normalized] == ']' && text[normalized + 1] == '(') {
                    uint32_t cursor = normalized + 2;
                    int depth = 1;
                    while (cursor < text.size() && depth > 0) {
                        const char c = text[cursor++];
                        if (c == '\\' && cursor < text.size()) {
                            ++cursor;
                        } else if (c == '(') {
                            ++depth;
                        } else if (c == ')') {
                            --depth;
                        }
                    }
                    if (depth == 0) normalized = cursor;
                }
            }
            return normalized;
        }
        if (offset > span.start && offset < span.end) return span.end;
        if (span.end < offset && (previous == UINT32_MAX || span.end > previous)) {
            previous = span.end;
        }
        if (span.start > offset && (next == UINT32_MAX || span.start < next)) {
            next = span.start;
        }
    }
    uint32_t snapped;
    if (previous == UINT32_MAX) snapped = next;
    else if (next == UINT32_MAX) snapped = previous;
    else snapped = offset - previous <= next - offset ? previous : next;

    // The cell guard: a cell owns the whole byte range between its
    // pipes. A snapped position outside the clicked cell would move the
    // caret into a different cell, so it is clamped back; a cell with
    // no drawn text parks the caret at its own start.
    for (const LaidOutBlock& block : blocks_) {
        if (block.kind != BlockKind::Table) continue;
        for (const std::vector<LaidOutBlock::TableCellLayout>& row :
             block.tableLayout.rows) {
            for (const LaidOutBlock::TableCellLayout& cell : row) {
                if (cell.srcCellStart == 0 && cell.srcCellEnd == 0) continue;
                if (offset < cell.srcCellStart || offset > cell.srcCellEnd) {
                    continue;
                }
                if (snapped < cell.srcCellStart || snapped > cell.srcCellEnd) {
                    // A snapped position outside the clicked cell would
                    // move the caret into a different cell, so it clamps
                    // back inside. The cell mapping always ends with a
                    // sentinel, so "no drawn text" is a mapping of one
                    // entry, and such a cell parks the caret at its own
                    // content start (table guideline 7).
                    if (cell.visToSrc.size() <= 1) return cell.srcCellStart;
                    return snapped < cell.srcCellStart ? cell.srcCellStart
                                                       : cell.srcCellEnd;
                }
                return snapped;
            }
        }
    }
    return snapped;
}

bool MacRenderer::OffsetIsRendered(uint32_t offset) const {
    std::vector<RenderedSpan> spans;
    CollectRenderedSpans(&spans);
    for (const RenderedSpan& span : spans) {
        if (offset == span.start) return true;
        if (span.start > offset) break;
    }
    return false;
}

bool MacRenderer::RangeIsRendered(uint32_t start, uint32_t length) const {
    const uint32_t end = start + length;
    if (length == 0 || end < start) return false;
    if (!sourceText_ || end > sourceText_->size()) return false;

    std::vector<RenderedSpan> spans;
    CollectRenderedSpans(&spans);
    if (spans.empty()) return false;

    for (uint32_t offset = start; offset < end; ++offset) {
        const unsigned char byte =
            static_cast<unsigned char>((*sourceText_)[offset]);
        if ((byte & 0xC0u) == 0x80u) continue;
        bool covered = false;
        for (const RenderedSpan& span : spans) {
            if (span.start <= offset && offset < span.end) {
                // The whole span has to sit inside the range, so a
                // range that starts or ends inside a drawn word is not
                // fully rendered.
                if (start > span.start || end < span.end) return false;
                covered = true;
                break;
            }
        }
        if (!covered) return false;
    }
    return true;
}

bool MacRenderer::ClipToRendered(uint32_t start, uint32_t length,
                                  uint32_t* outStart, uint32_t* outEnd) const {
    if (!outStart || !outEnd) return false;
    const uint32_t end = start + length;
    if (length == 0 || end < start) return false;
    if (!sourceText_ || end > sourceText_->size()) return false;

    std::vector<RenderedSpan> spans;
    CollectRenderedSpans(&spans);

    // The tightened range is the outer edge of every span that reaches
    // into the range, the same answer the Windows cache gives.
    uint32_t firstStart = UINT32_MAX;
    uint32_t lastEnd = 0;
    for (const RenderedSpan& span : spans) {
        if (span.end <= start || span.start >= end) continue;
        if (firstStart == UINT32_MAX || span.start < firstStart) {
            firstStart = span.start;
        }
        if (span.end > lastEnd) lastEnd = span.end;
    }
    if (firstStart == UINT32_MAX) return false;
    *outStart = firstStart;
    *outEnd = lastEnd;
    return true;
}

uint32_t MacRenderer::StepGrapheme(uint32_t offset, bool left) const {
    const uint32_t clamped = NormalizeToRenderedCaret(offset);
    const std::vector<TextRegion> regions = TextRegions();
    for (const TextRegion& region : regions) {
        if (clamped < region.visToSrc->front() ||
            clamped > region.visToSrc->back()) {
            continue;
        }
        uint32_t vis = 0;
        if (!VisibleAtSource(*region.visToSrc, clamped, &vis)) continue;

        const std::string text = VisibleTextOfLines(*region.lines);
        if (left && vis > 0) {
            const uint32_t step = PrevGraphemeBoundary(text, vis);
            return NormalizeToRenderedCaret(SourceAtVisible(
                *region.visToSrc, *region.visToSrcEnd, step, false));
        }
        if (!left && vis < text.size()) {
            const uint32_t step = NextGraphemeBoundary(text, vis);
            return NormalizeToRenderedCaret(SourceAtVisible(
                *region.visToSrc, *region.visToSrcEnd, step, false));
        }

        // The edge of the region: the nearest drawn text on the other
        // side of the marker gap. A cell edge is a wall, though: an
        // arrow never steps into the next cell (table guideline 12).
        if (region.cell) return clamped;
        uint32_t edge = clamped;
        bool found = false;
        std::vector<RenderedSpan> spans;
        CollectRenderedSpans(&spans);
        for (const RenderedSpan& span : spans) {
            if (left) {
                if (span.end < clamped && (!found || span.end > edge)) {
                    edge = span.end;
                    found = true;
                }
            } else {
                if (span.start > clamped && (!found || span.start < edge)) {
                    edge = span.start;
                    found = true;
                }
            }
        }
        return found ? NormalizeToRenderedCaret(edge) : clamped;
    }
    return clamped;
}

uint32_t MacRenderer::StepWord(uint32_t offset, bool left) const {
    if (!sourceText_) return offset;
    const uint32_t clamped = NormalizeToRenderedCaret(offset);
    const uint32_t target = left
        ? MoveWordLeftInString(*sourceText_, clamped)
        : MoveWordRightInString(*sourceText_, clamped);
    return NormalizeToRenderedCaret(target);
}

uint32_t MacRenderer::LineBoundary(uint32_t offset, bool end) const {
    const std::vector<TextRegion> regions = TextRegions();
    for (const TextRegion& region : regions) {
        if (offset < region.visToSrc->front() ||
            offset > region.visToSrc->back()) {
            continue;
        }
        uint32_t vis = 0;
        if (!VisibleAtSource(*region.visToSrc, offset, &vis)) continue;

        uint32_t lineStartVis = 0;
        for (const TextLine& line : *region.lines) {
            uint32_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += static_cast<uint32_t>(run.text.size());
            }
            const uint32_t lineEndVis = lineStartVis + length;
            if (vis >= lineStartVis && vis <= lineEndVis) {
                if (!end) {
                    return SourceAtVisible(*region.visToSrc,
                                           *region.visToSrcEnd, lineStartVis,
                                           false);
                }
                if (lineEndVis == 0) {
                    return SourceAtVisible(*region.visToSrc,
                                           *region.visToSrcEnd, 0, false);
                }
                return SourceAtVisible(*region.visToSrc, *region.visToSrcEnd,
                                       lineEndVis - 1, true);
            }
            lineStartVis = lineEndVis;
        }
        if (!end) {
            return SourceAtVisible(*region.visToSrc, *region.visToSrcEnd, 0,
                                   false);
        }
        return region.visToSrc->back();
    }
    return NormalizeToRenderedCaret(offset);
}

float MacRenderer::TextBottom() const {
    float bottom = 0.0f;
    for (const TextRegion& region : TextRegions()) {
        float lineTop = region.top;
        for (const TextLine& line : *region.lines) {
            lineTop += line.height;
        }
        if (lineTop > bottom) bottom = lineTop;
    }
    return bottom;
}

bool MacRenderer::LineRangeAtPoint(float x, float y, uint32_t* outStart,
                                   uint32_t* outEnd) const {
    // The x is ignored on purpose: a click in the margin is level with
    // a line, and the line is what the user wants to select.
    (void)x;

    for (const LaidOutBlock& block : blocks_) {
        if (block.lines.empty()) continue;
        if (block.visToSrc.empty()) continue;
        if (y < block.y || y > block.y + block.height) continue;

        float lineTop = TextTop(block);
        uint32_t lineStartVis = 0;
        for (const TextLine& line : block.lines) {
            uint32_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += static_cast<uint32_t>(run.text.size());
            }
            const float lineBottom = lineTop + line.height;
            if (y >= lineTop && y < lineBottom) {
                if (outStart) {
                    *outStart = SourceAtVisible(block.visToSrc,
                                                block.visToSrcEnd,
                                                lineStartVis, false);
                }
                if (outEnd) {
                    if (length == 0) {
                        *outEnd = SourceAtVisible(block.visToSrc,
                                                  block.visToSrcEnd,
                                                  lineStartVis, false);
                    } else {
                        *outEnd = SourceAtVisible(block.visToSrc,
                                                  block.visToSrcEnd,
                                                  lineStartVis + length - 1,
                                                  true);
                    }
                }
                return true;
            }
            lineStartVis += length;
            lineTop = lineBottom;
        }
        return false;
    }
    return false;
}

bool MacRenderer::OffsetToRect(uint32_t offset, float* outX, float* outY,
                               float* outWidth, float* outHeight) const {
    const std::vector<TextRegion> regions = TextRegions();
    for (const TextRegion& region : regions) {
        if (offset < region.visToSrc->front() ||
            offset > region.visToSrc->back()) {
            continue;
        }
        uint32_t vis = 0;
        if (!VisibleAtSource(*region.visToSrc, offset, &vis)) continue;

        // The lines are walked with the visible range each one covers,
        // so the line the position is on is the one that is found.
        float lineTop = region.top;
        uint32_t lineStartVis = 0;
        for (const TextLine& line : *region.lines) {
            uint32_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += static_cast<uint32_t>(run.text.size());
            }
            const uint32_t lineEndVis = lineStartVis + length;
            if (vis >= lineStartVis && vis <= lineEndVis) {
                const uint32_t inLine = vis >= lineEndVis
                    ? length
                    : vis - lineStartVis;
                const Rect rect = engine_->OffsetToRect(line, inLine);
                if (outX) *outX = region.x + rect.x;
                if (outY) *outY = lineTop;
                if (outWidth) *outWidth = rect.width;
                if (outHeight) *outHeight = line.height;
                return true;
            }
            lineStartVis = lineEndVis;
            lineTop += line.height;
        }
        break;
    }

    // A position between two drawn lines has no region of its own: the
    // blank line between two paragraphs, or the end of the document, is
    // where the caret can sit but where nothing is drawn. Every vertical
    // move and every scroll asks this question first, so an unanswered
    // position left the arrows dead the moment the caret reached one.
    // Answer with the end of the nearest drawn line above, or with the
    // start of the nearest line below when the position is before all of
    // them.
    const TextRegion* above = nullptr;
    const TextRegion* below = nullptr;
    for (const TextRegion& region : regions) {
        if (!region.visToSrc || !region.lines || region.lines->empty()) {
            continue;
        }
        const uint32_t first = region.visToSrc->front();
        const uint32_t last = region.visToSrc->back();
        if (last < offset) {
            if (!above || last > above->visToSrc->back()) above = &region;
        } else if (first > offset) {
            if (!below || first < below->visToSrc->front()) below = &region;
        }
    }
    if (above) {
        const TextLine& line = above->lines->back();
        uint32_t length = 0;
        for (const GlyphRun& run : line.runs) {
            length += static_cast<uint32_t>(run.text.size());
        }
        float top = above->top;
        for (size_t i = 0; i + 1 < above->lines->size(); ++i) {
            top += (*above->lines)[i].height;
        }
        const Rect rect = engine_->OffsetToRect(line, length);
        if (outX) *outX = above->x + rect.x;
        if (outY) *outY = top;
        if (outWidth) *outWidth = rect.width;
        if (outHeight) *outHeight = line.height;
        return true;
    }
    if (below) {
        const TextLine& line = below->lines->front();
        const Rect rect = engine_->OffsetToRect(line, 0);
        if (outX) *outX = below->x + rect.x;
        if (outY) *outY = below->top;
        if (outWidth) *outWidth = rect.width;
        if (outHeight) *outHeight = line.height;
        return true;
    }
    return false;
}

bool MacRenderer::VerticalNeighbour(uint32_t offset, float desiredX, int delta,
                                   uint32_t* outOffset) const {
    if (delta == 0 || !outOffset) return false;

    // Every drawn line, in the order the page draws them.
    struct LineRef {
        float top;
        float height;
    };
    std::vector<LineRef> lines;
    const std::vector<TextRegion> regions = TextRegions();
    int caretLine = -1;
    // The last line of the nearest region that ends before the position,
    // for a position that sits between two regions: on the newline that
    // ends a block, nothing holds it, and the step has to start from the
    // line just above the gap.
    int lineAbove = -1;
    for (const TextRegion& region : regions) {
        if (!region.lines || !region.visToSrc || region.lines->empty()) {
            continue;
        }
        uint32_t vis = 0;
        const bool holdsCaret =
            offset >= region.visToSrc->front() &&
            offset <= region.visToSrc->back() &&
            VisibleAtSource(*region.visToSrc, offset, &vis);
        float top = region.top;
        uint32_t lineStartVis = 0;
        for (const TextLine& line : *region.lines) {
            uint32_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += static_cast<uint32_t>(run.text.size());
            }
            const uint32_t lineEndVis = lineStartVis + length;
            if (holdsCaret && vis >= lineStartVis && vis <= lineEndVis) {
                caretLine = static_cast<int>(lines.size());
            }
            LineRef ref;
            ref.top = top;
            ref.height = line.height;
            lines.push_back(ref);
            lineStartVis = lineEndVis;
            top += line.height;
        }
        if (region.visToSrc->back() < offset) {
            lineAbove = static_cast<int>(lines.size()) - 1;
        }
    }
    if (caretLine < 0) {
        if (lineAbove < 0) {
            if (delta < 0) return false;
            // Before the first drawn line: one step down is its first
            // line, and there is nothing above it.
            caretLine = -1;
        } else {
            caretLine = lineAbove;
        }
    }

    // The point sits in the middle of a line's band, so the hit test
    // cannot drift into the line above or below it. A block's last line
    // can be one that draws nothing, and its middle answers with the
    // caret's own position again; the step then keeps going in the same
    // direction until the layout names a position that differs.
    const int lineCount = static_cast<int>(lines.size());
    const int direction = delta > 0 ? 1 : -1;
    for (int line = caretLine + delta; line >= 0 && line < lineCount;
         line += direction) {
        const LineRef& target = lines[static_cast<size_t>(line)];
        const uint32_t hit =
            HitTestPoint(desiredX, target.top + target.height * 0.5f);
        // A band that draws no glyphs, a spacer the layout keeps for
        // spacing alone, has no text position to answer with. Step past
        // it to the next line that does.
        if (hit == UINT32_MAX) continue;
        if (hit != offset) {
            *outOffset = hit;
            return true;
        }
    }
    return false;
}

std::vector<Rect> MacRenderer::SelectionRects(uint32_t start,
                                               uint32_t end) const {
    return RectsForRange(start, end);
}

std::vector<Rect> MacRenderer::RectsForRange(uint32_t start,
                                             uint32_t end) const {
    std::vector<Rect> rects;
    for (const LaidOutBlock& block : blocks_) {
        const std::vector<Rect> blockRects =
            RectsForRangeInBlock(block, start, end);
        rects.insert(rects.end(), blockRects.begin(), blockRects.end());
    }
    return rects;
}

void MacRenderer::AppendRangeRects(const std::vector<TextLine>& lines,
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

void MacRenderer::AppendVisibleRangeRects(
    const std::vector<TextLine>& lines, float x, float top, uint32_t start,
    uint32_t end, std::vector<Rect>* rects, ColumnAlign align,
    float innerWidth, bool trimBlanks) const {
    layout::AppendVisibleRangeRects(lines, *engine_, x, top, start, end,
                                    rects, align, innerWidth, trimBlanks);
}

std::vector<Rect> MacRenderer::RectsForRangeInBlock(const LaidOutBlock& block,
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
