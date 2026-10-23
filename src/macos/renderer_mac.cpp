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

#include <algorithm>
#include <cmath>
#include <utility>

#include "editcontroller.h"
#include "mermaid/layout_cache.h"
#include "mermaid/layout.h"
#include "mermaid/model.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"
#include "mermaid_painter_mac.h"
#include "navigation.h"
#include "svg_mac.h"

namespace mac {
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

// The text of a block's inline children, in UTF-8.
std::string InlineTextUtf8(const Node& node) {
    std::string text;
    for (const InlineBlock& child : node.children) {
        text += Utf32ToUtf8(child.text);
    }
    return text;
}

// A node that holds one run of text, so the table code can reuse the run
// builder without building a whole DOM node.
Node TextNode(const std::u32string& text) {
    Node node;
    InlineBlock child;
    child.kind = InlineKind::Text;
    child.text = text;
    node.children.push_back(child);
    return node;
}

// The height an image takes when its size is not known yet.
//
// The Windows renderer charges the same height, so the two agree on how
// much room an image takes before it has been measured.
constexpr float kImageFallbackHeight = 100.0f;

// The largest height the images of one block may take.
constexpr float kImageMaxHeight = 400.0f;

// The width of the text column when the word wrap is off. The text is
// laid out on one very wide line and the user scrolls to its end.
constexpr float kNoWrapContentWidthDip = 10000.0f;

// The theme sizes are points, and the Windows renderer converts them to
// DIPs with this factor (kPtToDip there) when it creates its text
// formats. The mac renderer applies the same factor, so a document lays
// out at the same em on both platforms and the line breaks land in the
// same places.
constexpr float kPtToDip = 96.0f / 72.0f;

// The baseline offset inside a body line, as a multiple of the font
// size. The Windows body format sets the same value (SetLineSpacing
// with 1.24).
constexpr float kBodyBaselineOffset = 1.24f;

// The gap before a block, from the block that came before it: the rules
// and the numbers of the Windows renderer (Renderer::GapForTransition),
// so the two platforms leave the same space between the same pair of
// blocks.
float GapForTransition(BlockKind prev, BlockKind cur, BlockKind next,
                       int curDepth, int prevDepth, const LayoutMetrics& m) {
    (void)next;
    (void)curDepth;
    (void)prevDepth;
    if (cur == BlockKind::Heading) {
        if (prev == BlockKind::Heading) return m.headingGapAfter;
        return m.headingGapBefore;
    }
    if (prev == BlockKind::Heading) return m.headingGapAfter;
    if (cur == BlockKind::List && prev == BlockKind::List) {
        return m.listItemGap;
    }
    if (cur == BlockKind::List && prev != BlockKind::List) return m.listGap;
    if (prev == BlockKind::List && cur != BlockKind::List) return m.listGap;
    if (cur == BlockKind::CodeBlock || prev == BlockKind::CodeBlock) {
        return m.codeBlockGap;
    }
    return m.paraGap;
}

// A font, in the layout unit.
FontSpec MakeFont(const LayoutMetrics& metrics, float size, bool bold,
                  bool italic, bool monospace) {
    FontSpec font;
    font.family = monospace ? metrics.codeFont : metrics.bodyFont;
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

// The UTF-8 byte length of the character that starts at the byte.
size_t Utf8CharLen(const std::string& s, size_t at) {
    const unsigned char b = static_cast<unsigned char>(s[at]);
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
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

// Fully append the visible-to-source map of an inline block list. One
// entry per rendered byte, plus the end of the last character as the
// final entry. The mirror of the mapping the Windows renderer builds
// while it lays a block out.
void AppendInlineMapping(const std::vector<InlineBlock>& children,
                          std::vector<uint32_t>* visToSrc,
                          std::vector<uint32_t>* visToSrcEnd) {
    for (const InlineBlock& child : children) {
        if (child.kind == InlineKind::Image) continue;
        if (child.text.empty()) continue;
        uint32_t srcByte = child.srcOffset;
        for (char32_t cp : child.text) {
            const uint32_t utf8Len = cp <= 0x7F ? 1u
                                    : cp <= 0x7FF ? 2u
                                    : cp <= 0xFFFF ? 3u : 4u;
            uint32_t srcEnd = srcByte + utf8Len;
            // A span the parser gave a longer source than its text (an
            // escape or an entity) ends where the source ends.
            if (child.text.size() == 1 && child.srcLength > utf8Len) {
                srcEnd = child.srcOffset + child.srcLength;
            }
            for (uint32_t k = 0; k < utf8Len; ++k) {
                visToSrc->push_back(srcByte);
                visToSrcEnd->push_back(srcEnd);
            }
            srcByte += utf8Len;
        }
    }
    if (!visToSrcEnd->empty()) {
        visToSrc->push_back(visToSrcEnd->back());
        visToSrcEnd->push_back(visToSrcEnd->back());
    }
}

// Close a map with the sentinel entry, or leave it holding the one
// addressable position an empty region keeps.
void FinishMapping(uint32_t emptyPosition, std::vector<uint32_t>* visToSrc,
                    std::vector<uint32_t>* visToSrcEnd) {
    if (!visToSrcEnd->empty()) {
        visToSrc->push_back(visToSrcEnd->back());
        visToSrcEnd->push_back(visToSrcEnd->back());
    } else {
        visToSrc->push_back(emptyPosition);
        visToSrcEnd->push_back(emptyPosition);
    }
}

// The visible-to-source map of a table cell, from the parser's own
// UTF-16-to-source mapping.
void AppendCellMapping(const TableCell& cell,
                        std::vector<uint32_t>* visToSrc,
                        std::vector<uint32_t>* visToSrcEnd) {
    uint32_t u16 = 0;
    uint32_t srcByte = cell.srcOffset;
    for (char32_t cp : cell.text) {
        const uint32_t utf8Len = cp <= 0x7F ? 1u
                                : cp <= 0x7FF ? 2u
                                : cp <= 0xFFFF ? 3u : 4u;
        uint32_t start = srcByte;
        uint32_t end = srcByte + utf8Len;
        if (u16 < cell.u16ToSrc.size()) {
            start = cell.u16ToSrc[u16];
            end = u16 < cell.u16ToSrcEnd.size() ? cell.u16ToSrcEnd[u16]
                                                : start + utf8Len;
        }
        for (uint32_t k = 0; k < utf8Len; ++k) {
            visToSrc->push_back(start);
            visToSrcEnd->push_back(end);
        }
        u16 += cp > 0xFFFF ? 2u : 1u;
        srcByte = end;
    }
    FinishMapping(cell.srcOffset, visToSrc, visToSrcEnd);
}

// The visible-to-source map of a code block: the raw text is drawn as
// it stands, so the map walks the source from the content start.
void AppendCodeMapping(const std::string& code, uint32_t contentOffset,
                        std::vector<uint32_t>* visToSrc,
                        std::vector<uint32_t>* visToSrcEnd) {
    uint32_t srcByte = contentOffset;
    for (size_t i = 0; i < code.size();) {
        const size_t len = Utf8CharLen(code, i);
        for (size_t k = 0; k < len; ++k) {
            visToSrc->push_back(srcByte);
            visToSrcEnd->push_back(srcByte + static_cast<uint32_t>(len));
        }
        srcByte += static_cast<uint32_t>(len);
        i += len;
    }
    FinishMapping(contentOffset, visToSrc, visToSrcEnd);
}

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

}  // namespace

// The mermaid layout of a block, in the form the painter takes.
//
// A flowchart is laid out again with the diagram text engine measuring
// every label, the way the Windows build lays it out with DirectWrite,
// and the result is cached by fence source and zoom. Without an
// engine, or when the re-layout comes back empty, the parse-time
// layout stands.
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
            if (node.mermaid_flowchart && !node.mermaid_source.empty() &&
                diagramEngine_) {
                if (auto cached = mermaid_layout_cache_.Find(
                        node.mermaid_source, zoom_)) {
                    diagram.flow = cached;
                    break;
                }
                EngineMeasureCtx ctx;
                ctx.engine = diagramEngine_;
                ctx.font = DiagramFontFor(node, metrics_.bodyFontSize,
                                          false, false, false);
                auto laid = std::make_shared<mermaid::LaidOutFlowchart>(
                    mermaid::LayoutFlowchartWith(*node.mermaid_flowchart,
                                                 MeasureWithEngine, &ctx));
                if (!laid->nodes.empty()) {
                    mermaid_layout_cache_.Put(node.mermaid_source, zoom_, laid);
                    diagram.flow = laid;
                    break;
                }
            }
            diagram.flow = node.mermaid_layout;
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
MacRenderer::~MacRenderer() {
    // The engine of the diagrams belongs to the renderer.
    delete diagramEngine_;
    diagramEngine_ = nullptr;
}

FontSpec MacRenderer::FontFor(const Node& node, float baseSize, bool bold,
                               bool italic, bool monospace) const {
    (void)node;
    return MakeFont(metrics_, baseSize * kPtToDip * zoom_, bold, italic,
                    monospace);
}

FontSpec MacRenderer::DiagramFontFor(const Node& node, float baseSize,
                                      bool bold, bool italic,
                                      bool monospace) const {
    // The mermaid painters scale their text by the zoom themselves, the
    // same way the Windows measure format stays unzoomed.
    (void)node;
    return MakeFont(metrics_, baseSize * kPtToDip, bold, italic, monospace);
}

void MacRenderer::ApplyLineSpacing(std::vector<TextLine>* lines, float ptSize,
                                    float heightMult,
                                    float baselineMult) const {
    const float em = ptSize * kPtToDip * zoom_;
    for (TextLine& line : *lines) {
        line.height = heightMult * em;
        line.ascent = baselineMult * em;
    }
}

std::vector<GlyphRun> MacRenderer::BuildCellRuns(const TableCell& cell,
                                                  bool bold,
                                                  const Color& color) const {
    // The cell text, cut into runs at the boundaries of the inline
    // spans. The spans index the text in UTF-16 units, so the walk
    // counts them as it goes.
    std::vector<GlyphRun> runs;
    const std::u32string& text = cell.text;
    const size_t count = text.size();

    auto styleAt = [&](uint32_t index, bool* strong, bool* em, bool* code,
                       bool* strike) {
        *strong = bold;
        *em = false;
        *code = false;
        *strike = false;
        for (const CellInlineSpan& span : cell.inlineSpans) {
            if (index < span.u16Start || index >= span.u16End) continue;
            *strong = *strong || span.bold;
            *em = *em || span.italic;
            *code = *code || span.code;
            *strike = *strike || span.strike;
        }
    };
    auto isLink = [&](uint32_t index) {
        if (index >= cell.u16ToSrc.size()) return false;
        const uint32_t source = cell.u16ToSrc[index];
        for (const TableLink& link : cell.links) {
            if (source >= link.srcOffset &&
                source < link.srcOffset + link.srcLength) {
                return true;
            }
        }
        return false;
    };

    uint32_t u16 = 0;
    size_t i = 0;
    while (i < count) {
        bool strong = false, em = false, code = false, strike = false;
        styleAt(u16, &strong, &em, &code, &strike);
        const bool link = isLink(u16);

        // The run takes every code point with the same style.
        std::string run;
        while (i < count) {
            bool next = false, nextEm = false, nextCode = false;
            bool nextStrike = false;
            styleAt(u16, &next, &nextEm, &nextCode, &nextStrike);
            if (next != strong || nextEm != em || nextCode != code ||
                nextStrike != strike || isLink(u16) != link) {
                break;
            }
            run += Utf32ToUtf8(std::u32string(1, text[i]));
            u16 += text[i] > 0xFFFF ? 2u : 1u;
            ++i;
        }
        if (run.empty()) continue;

        GlyphRun glyph;
        glyph.text = std::move(run);
        // The windows renderer gives a cell code span the code family
        // at the body size, and the header its weight.
        glyph.font = FontFor(Node{}, metrics_.bodyFontSize, strong, em, code);
        glyph.r = color.r;
        glyph.g = color.g;
        glyph.b = color.b;
        glyph.a = color.a;
        glyph.underline = link;
        glyph.strike = strike;
        runs.push_back(std::move(glyph));
    }
    return runs;
}

std::vector<GlyphRun> MacRenderer::BuildRuns(const Node& node, float baseSize,
                                              bool bold, bool italic,
                                              const Color& color,
                                              bool monospace) const {
    std::vector<GlyphRun> runs;

    // The children are walked one by one, so a run carries the style of
    // the span it came from. A link is a colour change and an
    // underline, an inline code span switches to the smaller code face,
    // and everything else is a font change.
    for (const InlineBlock& child : node.children) {
        const std::string runText = Utf32ToUtf8(child.text);
        if (runText.empty()) continue;

        GlyphRun run;
        run.text = runText;
        // An inline code span uses the code size, the way the Windows
        // renderer sets codeFontSize on the span.
        const bool childCode = child.code;
        const float runSize = childCode ? metrics_.codeFontSize : baseSize;
        run.font = FontFor(node, runSize, bold || child.strong,
                           italic || child.em, monospace || childCode);
        // A link is drawn in the link colour, everything else in the text
        // colour of the block.
        const bool link = child.kind == InlineKind::Link;
        const Color runColor = link ? ToColor(palette_.link) : color;
        run.r = runColor.r;
        run.g = runColor.g;
        run.b = runColor.b;
        run.a = runColor.a;
        run.underline = link;
        run.strike = child.strike;
        runs.push_back(run);
    }
    return runs;
}

float MacRenderer::Measure(const Document& doc, float widthDip) {
    blocks_.clear();
    needsLayout_ = false;

    // The engine drops the measurements of the pass before this one, so
    // the hit tester works against the layout that is about to be made.
    if (engine_) {
        engine_->BeginLayout();
    }

    if (widthDip <= 0.0f) {
        contentWidth_ = 1.0f;
        originX_ = metrics_.padX;
        totalHeight_ = metrics_.padTop;
        return totalHeight_;
    }

    // The content column: the view width less the theme padding, capped
    // at the reading width, and centered when it is narrower than the
    // space. The same arithmetic the Windows renderer uses, so a
    // document sits the same way on both platforms. With the wrap off
    // the text gets one very wide line, and the user scrolls to its
    // end.
    const float available = widthDip - metrics_.padX * 2.0f;
    contentWidth_ = wrap_ ? std::min(available, metrics_.maxContentWidth)
                          : kNoWrapContentWidthDip;
    if (contentWidth_ <= 0.0f) {
        contentWidth_ = 1.0f;
    }
    originX_ = wrap_ ? (widthDip - contentWidth_) * 0.5f : metrics_.padX;

    float y = metrics_.padTop;
    BlockKind prevBlock = BlockKind::Paragraph;
    int prevDepth = -1;
    bool first = true;

    for (const Node& node : doc.nodes) {
        // The gap belongs to the block that follows it, and the first
        // block gets none, the same model the Windows renderer uses.
        if (!first) {
            y += GapForTransition(prevBlock, node.block, BlockKind::Paragraph,
                                  node.depth, prevDepth, metrics_);
        }
        first = false;
        LayoutBlock(node, originX_, contentWidth_, &y);
        prevBlock = node.block;
        prevDepth = node.depth;
    }

    totalHeight_ = y + metrics_.padTop * 2.0f;
    return totalHeight_;
}

void MacRenderer::SetContentWidthMode(int mode) {
    if (mode < 0 || mode > 3) {
        mode = 0;
    }
    contentWidthMode_ = mode;
    metrics_ = ComputeMetrics();
}

LayoutMetrics MacRenderer::ComputeMetrics() const {
    LayoutMetrics m = ScaleMetrics(baseMetrics_, zoom_);
    // Override the content width cap based on the user's width setting.
    // Mode 3 (Full) means no cap, so we use a very large value. The
    // same numbers the Windows renderer resolves, zoom included.
    switch (contentWidthMode_) {
        case 1: m.maxContentWidth = 960.0f * zoom_; break;
        case 2: m.maxContentWidth = 1600.0f * zoom_; break;
        case 3: m.maxContentWidth = 100000.0f; break;
        case 0:
        default: m.maxContentWidth = 800.0f * zoom_; break;
    }
    return m;
}

const SvgDoc* MacRenderer::GetSvgDoc(const Node& node) {
    // The document is read once, and it is kept until the document
    // changes.
    for (const auto& entry : svgDocs_) {
        if (entry.first == node.srcOffset) return &entry.second;
    }

    SvgDoc doc;
    if (!doc.Load(InlineTextUtf8(node))) return nullptr;
    svgDocs_.emplace_back(node.srcOffset, std::move(doc));
    return &svgDocs_.back().second;
}

float MacRenderer::ChargeForImages(LaidOutBlock* block) {
    // The images are drawn above the text of the block, so the block
    // has to reserve the room for them, the same height the Windows
    // layout charges.
    //
    // A remote image is fetched in the background. Until it has arrived
    // the block gets the fallback height, and the document is laid out
    // again when the image shows up.
    float imageHeight = 0.0f;
    for (const InlineBlock& child : block->node->children) {
        if (child.kind != InlineKind::Image || child.url.empty()) continue;

        images_.Prefetch(child.url);

        float height = kImageFallbackHeight;
        if (imageHeight + height > kImageMaxHeight) {
            height = kImageMaxHeight;
        }
        imageHeight += height + metrics_.paraGap;
        block->images.push_back(child.url);

        if (!images_.GetSize(child.url, nullptr, nullptr)) {
            needsLayout_ = true;
        }
    }
    block->imagesHeight = imageHeight;
    return imageHeight;
}

void MacRenderer::LayoutBlock(const Node& node, float x, float width,
                              float* y) {
    LaidOutBlock block;
    block.node = &node;
    block.kind = node.block;
    block.level = node.level;

    // A quote and a list step their content inward, and the text
    // narrows with it, the same way the Windows renderer measures them.
    float drawX = x;
    float drawW = width;
    if (node.block == BlockKind::BlockQuote) {
        const float indent = metrics_.quoteIndent * (node.depth + 1);
        drawX += indent;
        drawW -= indent;
    } else if (node.block == BlockKind::List) {
        const float indent = metrics_.listIndent * (node.depth + 1);
        drawX += indent;
        drawW -= indent;
    }
    block.x = drawX;
    block.width = drawW;

    switch (node.block) {
        case BlockKind::Heading: {
            const int level =
                node.level < 1 ? 1 : (node.level > 6 ? 6 : node.level);
            const float size = metrics_.headingSizes[level];
            const std::vector<GlyphRun> runs = BuildRuns(
                node, size, true, false, ToColor(palette_.heading), false);
            block.lines = engine_->MeasureWrapped(runs, drawW, 0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.headingLineHeight,
                             1.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            // The rule under an H1 and an H2 lives inside the block, so
            // the next block keeps its distance from the rule.
            if (level <= 2 && !block.lines.empty()) {
                block.height += metrics_.ruleGapAbove + metrics_.ruleGapBelow;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::Paragraph: {
            const float size = metrics_.bodyFontSize;
            std::vector<GlyphRun> runs = BuildRuns(
                node, size, false, false, ToColor(palette_.textPrimary),
                false);
            if (runs.empty() && node.virtualEmptyParagraph) {
                // A blank paragraph keeps a line, so the caret has a
                // place to sit: the same placeholder line the Windows
                // layout keeps for it.
                runs = BuildRuns(TextNode(U" "), size, false, false,
                                 ToColor(palette_.textPrimary), false);
            }
            block.lines = engine_->MeasureWrapped(runs, drawW, 0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.bodyLineHeight,
                             kBodyBaselineOffset);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::CodeBlock: {
            // A standalone SVG file is a picture, not a code specimen,
            // so it is measured as one.
            if (node.lang == "svg" && standaloneSvg_) {
                const SvgDoc* doc = GetSvgDoc(node);
                if (doc && doc->Width() > 0.0f && doc->Height() > 0.0f) {
                    const float available = drawW - metrics_.codePad * 2.0f;
                    const float scale = available / doc->Width();
                    block.height = doc->Height() * scale;
                    break;
                }
            }

            // The raw text, with the trailing newlines stripped the way
            // the Windows measure pass strips them.
            std::string code = InlineTextUtf8(node);
            while (!code.empty() &&
                   (code.back() == '\n' || code.back() == '\r')) {
                code.pop_back();
            }

            // One run, with the line breaks kept in the text: Core Text
            // breaks a line there, so a blank line in the source stays a
            // blank line on screen.
            std::vector<GlyphRun> runs;
            if (!code.empty()) {
                GlyphRun run;
                run.text = code;
                run.font = FontFor(node, metrics_.codeFontSize, false, false,
                                   true);
                const Color color = ToColor(palette_.textPrimary);
                run.r = color.r;
                run.g = color.g;
                run.b = color.b;
                run.a = color.a;
                runs.push_back(std::move(run));
            }
            block.lines = engine_->MeasureWrapped(
                runs, drawW - metrics_.codePad * 2.0f, 0.0f);
            ApplyLineSpacing(&block.lines, metrics_.codeFontSize,
                             metrics_.codeLineHeight, 1.0f);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += metrics_.codePad * 2.0f;
            break;
        }

        case BlockKind::BlockQuote: {
            const float size = metrics_.bodyFontSize;
            const std::vector<GlyphRun> runs = BuildRuns(
                node, size, false, false, ToColor(palette_.quoteText), false);
            block.lines = engine_->MeasureWrapped(runs, drawW, 0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.bodyLineHeight,
                             kBodyBaselineOffset);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::ThematicBreak: {
            // The band the rule sits in the middle of, the same 12
            // points the Windows layout reserves.
            block.height = 12.0f;
            break;
        }

        case BlockKind::List: {
            const float size = metrics_.bodyFontSize;
            std::vector<GlyphRun> runs = BuildRuns(
                node, size, false, false, ToColor(palette_.textPrimary),
                false);
            bool hasImage = false;
            for (const InlineBlock& child : node.children) {
                if (child.kind == InlineKind::Image && !child.url.empty()) {
                    hasImage = true;
                }
            }
            if (runs.empty() && !hasImage) {
                // The marker keeps the item addressable even without
                // text, the way the Windows layout keeps its line.
                runs = BuildRuns(TextNode(U" "), size, false, false,
                                 ToColor(palette_.textPrimary), false);
            }
            // The text wraps in the space that is left after the marker
            // gutter: the same reservation the Windows layout makes.
            const float markerW =
                metrics_.listIndent * 0.6f + metrics_.markerGutter;
            block.lines = engine_->MeasureWrapped(runs, drawW - markerW,
                                                  0.0f);
            ApplyLineSpacing(&block.lines, size, metrics_.bodyLineHeight,
                             kBodyBaselineOffset);
            for (const TextLine& line : block.lines) {
                block.height += line.height;
            }
            block.height += ChargeForImages(&block);
            break;
        }

        case BlockKind::Table: {
            block.table = &node;
            size_t columnCount = 0;
            for (const TableRow& row : node.rows) {
                columnCount = std::max(columnCount, row.cells.size());
            }
            if (columnCount == 0) {
                break;
            }

            // The columns are equal, the width the Windows renderer
            // gives them (colW = width / cols).
            block.tableLayout.colW = drawW / static_cast<float>(columnCount);
            const float innerW =
                block.tableLayout.colW - 2.0f * metrics_.cellPadX;

            float total = 0.0f;
            for (const TableRow& row : node.rows) {
                std::vector<LaidOutBlock::TableCellLayout> cells;
                float tallest = 0.0f;
                for (size_t c = 0; c < row.cells.size() && c < columnCount;
                     ++c) {
                    const TableCell& cell = row.cells[c];
                    LaidOutBlock::TableCellLayout laid;
                    if (c < node.aligns.size()) {
                        laid.align = node.aligns[c];
                    }
                    const std::vector<GlyphRun> runs = BuildCellRuns(
                        cell, cell.isHeader, ToColor(palette_.textPrimary));
                    laid.lines = engine_->MeasureWrapped(
                        runs, innerW > 0.0f ? innerW : 1.0f, 0.0f);
                    ApplyLineSpacing(&laid.lines, metrics_.bodyFontSize,
                                     metrics_.bodyLineHeight,
                                     kBodyBaselineOffset);
                    float height = 0.0f;
                    for (const TextLine& line : laid.lines) {
                        height += line.height;
                    }
                    if (height > tallest) tallest = height;
                    cells.push_back(std::move(laid));
                }
                // A row with no text at all still has the Windows
                // minimum, and every row carries the cell padding.
                if (tallest == 0.0f) tallest = 20.0f;
                const float rowH = tallest + 2.0f * metrics_.cellPadY;
                block.tableLayout.rowHeights.push_back(rowH);
                block.tableLayout.rows.push_back(std::move(cells));
                total += rowH;
            }
            block.height = total;
            break;
        }

        case BlockKind::MermaidFlowchart:
        case BlockKind::MermaidPie:
        case BlockKind::MermaidSequence: {
            // The mermaid layout is shared and already computed by the
            // parser. The height is the one the Windows build reserves,
            // so the two platforms lay the document out the same way.
            const mermaid::MermaidRender diagram = MermaidRenderFor(node);
            float diagramHeight = 0.0f;
            if (HasMermaidLayout(diagram)) {
                diagramHeight = mermaid::MeasureMermaidHeight(
                    diagram, zoom_, MermaidMeasureWidth(diagram, drawW));
            }
            block.height = diagramHeight > 0.0f ? diagramHeight : 100.0f;
            break;
        }
    }

    // The map from the drawn text back to the source, so the caret,
    // the selection and the hit tester never land on a Markdown marker.
    switch (node.block) {
        case BlockKind::Heading:
        case BlockKind::Paragraph:
            AppendInlineMapping(node.children, &block.visToSrc,
                                 &block.visToSrcEnd);
            if (block.visToSrc.empty() && node.virtualEmptyParagraph) {
                // The placeholder line maps to the position the empty
                // paragraph keeps for its caret.
                FinishMapping(node.contentOffset, &block.visToSrc,
                               &block.visToSrcEnd);
            }
            break;

        case BlockKind::List:
            AppendInlineMapping(node.children, &block.visToSrc,
                                 &block.visToSrcEnd);
            if (block.visToSrc.empty()) {
                // The marker keeps the item addressable.
                FinishMapping(node.contentOffset, &block.visToSrc,
                               &block.visToSrcEnd);
            }
            break;

        case BlockKind::BlockQuote:
            AppendInlineMapping(node.children, &block.visToSrc,
                                 &block.visToSrcEnd);
            break;

        case BlockKind::CodeBlock: {
            std::string code = InlineTextUtf8(node);
            while (!code.empty() &&
                   (code.back() == '\n' || code.back() == '\r')) {
                code.pop_back();
            }
            AppendCodeMapping(code, node.contentOffset, &block.visToSrc,
                               &block.visToSrcEnd);
            break;
        }

        case BlockKind::Table:
            for (size_t r = 0; r < node.rows.size() &&
                               r < block.tableLayout.rows.size(); ++r) {
                const TableRow& sourceRow = node.rows[r];
                for (size_t c = 0; c < sourceRow.cells.size() &&
                                   c < block.tableLayout.rows[r].size();
                     ++c) {
                    const TableCell& cell = sourceRow.cells[c];
                    LaidOutBlock::TableCellLayout& laid =
                        block.tableLayout.rows[r][c];
                    AppendCellMapping(cell, &laid.visToSrc, &laid.visToSrcEnd);
                    laid.srcCellStart = cell.srcOffset;
                    laid.srcCellEnd = cell.srcEnd;
                }
            }
            break;

        default:
            break;
    }

    block.y = *y;
    *y += block.height;
    blocks_.push_back(std::move(block));
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

    // The marks go under the text, so they are painted first.
    PaintMarkerHighlights(scrollOffset);
    PaintSearchHighlights(scrollOffset);

    // The ordered list markers count across a run of items, and the
    // count restarts when the list ends or the depth changes, the same
    // way the Windows renderer counts them.
    int listCounter = 0;
    BlockKind prevKind = BlockKind::Paragraph;
    int prevDepth = -1;
    for (const LaidOutBlock& block : blocks_) {
        std::string marker;
        const int depth = block.node ? block.node->depth : 0;
        if (block.kind == BlockKind::List) {
            if (prevKind != BlockKind::List || depth != prevDepth) {
                listCounter = 0;
            }
            if (block.node && block.node->ordered) {
                ++listCounter;
                marker = std::to_string(listCounter) + ". ";
            } else {
                marker = "\xE2\x80\xA2 ";
            }
        } else {
            listCounter = 0;
        }
        PaintBlock(block, scrollOffset, marker);
        prevKind = block.kind;
        prevDepth = depth;
    }
}

void MacRenderer::PaintMarkerHighlights(float scrollOffset) {
    if (!target_ || markers_.empty()) return;

    for (const TextMatch& marker : markers_) {
        for (const Rect& rect : RectsForRange(marker.start,
                                              marker.start + marker.length)) {
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

void MacRenderer::PaintSearchHighlights(float scrollOffset) {
    if (!target_ || searchMatches_.empty()) return;

    for (size_t i = 0; i < searchMatches_.size(); ++i) {
        const TextMatch& match = searchMatches_[i];
        const bool current = static_cast<int>(i) == searchCurrent_;
        for (const Rect& rect : RectsForRange(match.start,
                                              match.start + match.length)) {
            Rect drawn = rect;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, current ? ToColor(palette_.findCurrentMatchBg)
                                              : ToColor(palette_.findMatchBg));
        }
    }
}

void MacRenderer::PaintBlock(const LaidOutBlock& block, float scrollOffset,
                              const std::string& marker) {
    if (!target_) return;
    const float top = block.y - scrollOffset;

    switch (block.kind) {
        case BlockKind::ThematicBreak: {
            // The rule sits in the middle of the band the layout
            // reserves for it, the same place the Windows renderer puts
            // it.
            Rect rect;
            rect.x = block.x;
            rect.y = top + 6.0f;
            rect.width = block.width;
            rect.height = 1.0f;
            target_->FillRect(rect, ToColor(palette_.rule));
            break;
        }

        case BlockKind::CodeBlock: {
            // A standalone SVG file is drawn as a picture, without the
            // card behind it.
            if (block.node->lang == "svg" && standaloneSvg_) {
                const SvgDoc* doc = GetSvgDoc(*block.node);
                if (doc && doc->Width() > 0.0f && doc->Height() > 0.0f) {
                    const float available =
                        block.width - metrics_.codePad * 2.0f;
                    const float scale = available / doc->Width();
                    doc->Draw(target_, block.x + metrics_.codePad, top,
                              available, doc->Height() * scale);
                    break;
                }
            }

            // The background and the border, then the code itself. The
            // border follows the same rounded outline as the fill, the
            // way the Windows renderer strokes it.
            Rect bg;
            bg.x = block.x;
            bg.y = top;
            bg.width = block.width;
            bg.height = block.height;
            target_->FillRoundedRect(bg, metrics_.codeRadius,
                                     ToColor(palette_.codeBg));
            target_->StrokeRoundedRect(bg, metrics_.codeRadius,
                                       ToColor(palette_.codeBorder), 1.0f);

            PaintLines(block.lines, block.x + metrics_.codePad,
                       top + metrics_.codePad);
            break;
        }

        case BlockKind::BlockQuote: {
            // The bar steps inward with the nesting depth, and it is as
            // tall as the text. The geometry is the Windows one: the
            // bar is centered four points left of the text edge.
            float textHeight = 0.0f;
            for (const TextLine& line : block.lines) {
                textHeight += line.height;
            }
            const float barX = block.x - metrics_.quoteIndent + 4.0f;
            Rect bar;
            bar.x = barX - metrics_.quoteBarWidth * 0.5f;
            bar.y = top + block.imagesHeight;
            bar.width = metrics_.quoteBarWidth;
            bar.height = textHeight;
            target_->FillRect(bar, ToColor(palette_.quoteBar));

            PaintImages(block, top, block.width);
            PaintInlineCodeBg(block, scrollOffset);
            PaintLines(block.lines, block.x, top + block.imagesHeight);
            break;
        }

        case BlockKind::List: {
            // The text sits after the marker gutter, and the marker sits
            // in the gutter, clipped to it the way the Windows renderer
            // clips its marker layout.
            const float markerW =
                metrics_.listIndent * 0.6f + metrics_.markerGutter;
            float textHeight = 0.0f;
            for (const TextLine& line : block.lines) {
                textHeight += line.height;
            }
            const float textTop = top + block.imagesHeight;
            if (!marker.empty()) {
                const FontSpec markerFont = FontFor(
                    *block.node, metrics_.bodyFontSize, false, false, false);
                Rect clip;
                clip.x = block.x;
                clip.y = textTop;
                clip.width = markerW;
                clip.height = textHeight > 0.0f ? textHeight
                                                : metrics_.bodyFontSize;
                const float baseline =
                    textTop + (block.lines.empty()
                                   ? metrics_.bodyFontSize
                                   : block.lines.front().ascent);
                target_->PushClip(clip);
                target_->DrawText(marker, markerFont, block.x, baseline,
                                  ToColor(palette_.textPrimary));
                target_->PopClip();
            }
            PaintImages(block, top, block.width);
            PaintInlineCodeBg(block, scrollOffset);
            PaintLines(block.lines, block.x + markerW, textTop);
            break;
        }

        case BlockKind::Table: {
            PaintTable(block, scrollOffset);
            break;
        }

        case BlockKind::MermaidFlowchart:
        case BlockKind::MermaidPie:
        case BlockKind::MermaidSequence: {
            // The diagram is painted by the mermaid painter, which draws
            // the shared layout through the seam. The painter follows
            // the zoom itself, so it gets the unzoomed font.
            const mermaid::MermaidRender diagram =
                MermaidRenderFor(*block.node);
            if (!HasMermaidLayout(diagram)) break;
            if (diagramEngine_) diagramEngine_->BeginLayout();
            const float pad = mermaid::kMermaidBlockPad;
            const float availWidth =
                block.width > 2.0f * pad ? block.width - 2.0f * pad : 0.0f;
            PaintMermaidDiagram(diagramEngine_ ? diagramEngine_ : engine_,
                                target_, diagram, zoom_, availWidth,
                                block.x, top, pad, palette_,
                                DiagramFontFor(*block.node,
                                               metrics_.bodyFontSize, false,
                                               false, false));
            break;
        }

        case BlockKind::Heading:
        case BlockKind::Paragraph: {
            const float textTop = top + block.imagesHeight;
            PaintImages(block, top, block.width);
            PaintInlineCodeBg(block, scrollOffset);
            PaintLines(block.lines, block.x, textTop);

            // The rule under an H1 and an H2, inside the room the layout
            // reserved: under the text, the gap of the rule above it.
            if (block.kind == BlockKind::Heading && block.level <= 2) {
                float textHeight = 0.0f;
                for (const TextLine& line : block.lines) {
                    textHeight += line.height;
                }
                const float ruleY =
                    textTop + textHeight + metrics_.ruleGapAbove;
                target_->StrokeLine(block.x, ruleY, block.x + block.width,
                                    ruleY, ToColor(palette_.rule), 1.0f);
            }
            break;
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

void MacRenderer::PaintInlineCodeBg(const LaidOutBlock& block,
                                     float scrollOffset) const {
    if (!target_ || !block.node) return;

    // The background of an inline code span, behind the text and two
    // points wider than the glyphs, the same fill the Windows renderer
    // draws.
    for (const InlineBlock& child : block.node->children) {
        if (!child.code || child.srcLength == 0) continue;
        for (const Rect& rect : RectsForRangeInBlock(
                 block, child.srcOffset, child.srcOffset + child.srcLength)) {
            Rect drawn = rect;
            drawn.x -= 2.0f;
            drawn.width += 4.0f;
            drawn.y -= scrollOffset;
            target_->FillRect(drawn, ToColor(palette_.inlineCodeBg));
        }
    }
}

void MacRenderer::PaintImages(const LaidOutBlock& block, float top,
                              float width) {
    if (!target_) return;

    // The images are drawn in the room the layout reserved for them, one
    // under the other.
    float y = top;
    for (const std::string& url : block.images) {
        float height = kImageFallbackHeight;
        float imageWidth = 0.0f;
        float imageHeight = 0.0f;
        if (images_.GetSize(url, &imageWidth, &imageHeight) &&
            imageWidth > 0.0f && imageHeight > 0.0f) {
            // The image keeps its aspect ratio and fits the width of
            // the column.
            height = width * (imageHeight / imageWidth);
            if (height > kImageMaxHeight) {
                height = kImageMaxHeight;
            }
        }

        Rect rect;
        rect.x = block.x;
        rect.y = y;
        rect.width = width;
        rect.height = height;

        BitmapHandle image = images_.Get(url);
        if (image) {
            target_->DrawBitmap(image, rect);
        }
        y += height + metrics_.paraGap;
    }
}

void MacRenderer::PaintTable(const LaidOutBlock& block, float scrollOffset) {
    if (!target_ || !block.table) return;
    const Node& node = *block.table;
    const LaidOutBlock::TableLayout& layout = block.tableLayout;
    if (layout.rowHeights.empty()) return;

    float y = block.y - scrollOffset;
    for (size_t r = 0; r < node.rows.size() && r < layout.rows.size(); ++r) {
        const TableRow& row = node.rows[r];
        const std::vector<LaidOutBlock::TableCellLayout>& cells =
            layout.rows[r];
        const bool isHeaderRow = !row.cells.empty() && row.cells[0].isHeader;
        const float rowH = layout.rowHeights[r];

        // The row background: the header has its own, and the body rows
        // alternate, the same stripes the Windows renderer paints.
        Rect rowBg;
        rowBg.x = block.x;
        rowBg.y = y;
        rowBg.width = block.width;
        rowBg.height = rowH;
        if (isHeaderRow) {
            target_->FillRect(rowBg, ToColor(palette_.tableHeaderBg));
        } else if (r % 2 == 0) {
            target_->FillRect(rowBg, ToColor(palette_.tableRowAlt));
        }

        const float innerW = layout.colW - 2.0f * metrics_.cellPadX;
        for (size_t c = 0; c < cells.size(); ++c) {
            const float cellX = block.x + static_cast<float>(c) * layout.colW;

            // The cell border, one rectangle per cell.
            Rect border;
            border.x = cellX;
            border.y = y;
            border.width = layout.colW;
            border.height = rowH;
            target_->StrokeRect(border, ToColor(palette_.tableBorder), 1.0f);

            // The lines of the cell, placed at the column alignment of
            // the delimiter row, inside the cell padding, clipped to
            // the cell the way the Windows renderer clips them.
            Rect inner;
            inner.x = cellX + metrics_.cellPadX;
            inner.y = y + metrics_.cellPadY;
            inner.width = innerW > 0.0f ? innerW : 0.0f;
            inner.height = rowH - 2.0f * metrics_.cellPadY;
            if (inner.width > 0.0f && inner.height > 0.0f) {
                target_->PushClip(inner);
            }
            float lineY = y + metrics_.cellPadY;
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
    for (const LaidOutBlock& block : blocks_) {
        if (!block.node) continue;
        if (block.visToSrc.empty()) continue;
        if (offset < block.visToSrc.front() || offset > block.visToSrc.back()) {
            continue;
        }

        uint32_t vis = 0;
        if (!VisibleAtSource(block.visToSrc, offset, &vis)) continue;

        // The line the visible position sits on.
        float lineY = TextTop(block);
        uint32_t lineStart = 0;
        for (const TextLine& line : block.lines) {
            uint32_t length = 0;
            for (const GlyphRun& run : line.runs) {
                length += static_cast<uint32_t>(run.text.size());
            }
            if (vis <= lineStart + length) {
                y = lineY;
                return true;
            }
            lineStart += length;
            lineY += line.height;
        }
        y = lineY;
        return true;
    }
    return false;
}

float MacRenderer::TextX(const LaidOutBlock& block) const {
    if (block.kind == BlockKind::List) {
        // The text sits after the marker gutter, the same reservation
        // the layout makes.
        return block.x + metrics_.listIndent * 0.6f + metrics_.markerGutter;
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
               (metrics_.listIndent * 0.6f + metrics_.markerGutter);
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
                        block.x + static_cast<float>(c) * block.tableLayout.colW;
                    region.x = region.trackX + metrics_.cellPadX;
                    region.top = rowTop + metrics_.cellPadY;
                    region.width =
                        block.tableLayout.colW - 2.0f * metrics_.cellPadX;
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
        if (visToSrc.size() < 2) continue;
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
            if (score > bestScore ||
                (score == bestScore && distance < bestDistance)) {
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
            if (sourceText_) {
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
                    return cell.visToSrc.empty() ? cell.srcCellStart : snapped;
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
        // side of the marker gap.
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
    for (const TextRegion& region : TextRegions()) {
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
        return false;
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
                                    std::vector<Rect>* rects) const {
    if (lines.empty() || visToSrc.empty()) return;
    if (end < start) {
        const uint32_t swap = start;
        start = end;
        end = swap;
    }
    if (start > visToSrc.back() || end < visToSrc.front()) return;

    // The range is walked in visible space, so the rectangles land on
    // the drawn text and the markers inside the range never widen it.
    uint32_t visStart = 0;
    uint32_t visEnd = 0;
    if (!VisibleAtSource(visToSrc, start, &visStart)) return;
    if (!VisibleAtSource(visToSrc, end, &visEnd)) return;
    if (visEnd <= visStart) return;

    float lineTop = top;
    uint32_t lineStartVis = 0;
    for (const TextLine& line : lines) {
        uint32_t length = 0;
        for (const GlyphRun& run : line.runs) {
            length += static_cast<uint32_t>(run.text.size());
        }
        const uint32_t lineEndVis = lineStartVis + length;

        const uint32_t from = visStart > lineStartVis ? visStart : lineStartVis;
        const uint32_t to = visEnd < lineEndVis ? visEnd : lineEndVis;
        if (from < to) {
            const Rect first = engine_->OffsetToRect(line, from - lineStartVis);
            // The right edge of the range is the left edge of the
            // character after it, the rect OffsetToRect gives for that
            // position.
            const Rect after = engine_->OffsetToRect(line, to - lineStartVis);
            Rect rect;
            rect.x = x + first.x;
            rect.y = lineTop;
            rect.width = after.x - first.x;
            rect.height = line.height;
            if (rect.width < 0.0f) rect.width = 0.0f;
            rects->push_back(rect);
        }
        lineStartVis = lineEndVis;
        lineTop += line.height;
    }
}

std::vector<Rect> MacRenderer::RectsForRangeInBlock(const LaidOutBlock& block,
                                                     uint32_t start,
                                                     uint32_t end) const {
    std::vector<Rect> rects;

    // The block's own text.
    AppendRangeRects(block.lines, block.visToSrc,
                     TextX(block), TextTop(block), start, end, &rects);

    // A table draws its text cell by cell, so a range inside a cell
    // gets the cell's own geometry.
    if (block.kind == BlockKind::Table) {
        float rowTop = block.y;
        for (size_t r = 0; r < block.tableLayout.rows.size(); ++r) {
            const std::vector<LaidOutBlock::TableCellLayout>& cells =
                block.tableLayout.rows[r];
            for (size_t c = 0; c < cells.size(); ++c) {
                const LaidOutBlock::TableCellLayout& cell = cells[c];
                AppendRangeRects(cell.lines, cell.visToSrc,
                                 block.x +
                                     static_cast<float>(c) *
                                         block.tableLayout.colW +
                                     metrics_.cellPadX,
                                 rowTop + metrics_.cellPadY, start, end,
                                 &rects);
            }
            if (r < block.tableLayout.rowHeights.size()) {
                rowTop += block.tableLayout.rowHeights[r];
            }
        }
    }
    return rects;
}

}  // namespace mac
