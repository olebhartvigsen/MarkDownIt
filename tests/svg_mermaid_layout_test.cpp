// The golden tests of the shared SVG and Mermaid block charging, plan
// Batch 3.2 step 7. The fake engine draws a code point at 8 DIP, so
// the code fallback of an SVG fence holds on both runners.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"
#include "mermaid/layout_cache.h"

#include <memory>
#include <variant>

namespace {

#define NEAR(a, b) EXPECT_NEAR(a, b, 0.001f)
const float kPt = 96.0f / 72.0f;
const LayoutMetrics kM = BaseMetrics();
const Palette kPal = BasePalette();
constexpr size_t kCard = 1, kImage = 12;

// One fake engine, one list, metrics pre zoomed, at width and zoom.
struct Rig {
    fake::FakeTextEngine engine;
    LayoutMetrics m;
    layout::LayoutInput in;
    display::DisplayList list;
    layout::LayoutCtx ctx;
    explicit Rig(float width, float zoom = 1.0f)
        : m(ScaleMetrics(BaseMetrics(), zoom)) {
        in.metrics = &m; in.palette = &kPal;
        in.contentWidth = width; in.zoom = zoom; in.engine = &engine;
        ctx.in = &in; ctx.list = &list;
    }
};

}  // namespace

TEST(SvgMermaidLayout, SvgPictureNaturalSizeAndWidestRight) {
    Node n;  // a standalone .svg file's svg fence, the picture form
    n.block = BlockKind::CodeBlock; n.lang = "svg";
    n.raw = U"<svg width=\"200\" height=\"100\"/>";
    Rig rig(300.0f, 2.0f);
    std::string asked;
    rig.in.imageSize = [&](const std::string& url) {
        asked = url;  // the caller's cache answers the parsed size
        layout::ImageSize size;
        size.known = true; size.width = 200.0f; size.height = 100.0f;
        return size;
    };
    float y = 10.0f;
    layout::LaidBlock b = layout::LayoutSvgBlock(rig.ctx, n, 4, &y);

    // The url convention first: "svg:" plus the raw fence text.
    EXPECT_EQ(asked, std::string("svg:<svg width=\"200\" height=\"100\"/>"));
    // The charge: natural size times the zoom plus the two zoomed pads.
    NEAR(b.height, 100.0f * 2.0f + 2.0f * rig.m.codePad);
    NEAR(b.width, 200.0f * 2.0f + 2.0f * rig.m.codePad);
    EXPECT_GT(b.width, 300.0f);  // past the column: the widest right
    EXPECT_EQ(b.kind, BlockKind::CodeBlock);
    // One Vector picture one pad in, and no card anywhere.
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(1));
    EXPECT_EQ(rig.list.items[0].index(), kImage);
    const display::Image& pic = std::get<display::Image>(rig.list.items[0]);
    EXPECT_TRUE(pic.kind == platform::ImageKind::Vector);
    NEAR(pic.rect.x, rig.m.codePad);
    NEAR(pic.rect.height, 200.0f);
    NEAR(y, 10.0f + b.height);
}

TEST(SvgMermaidLayout, SvgUnknownSizeFallsBackToCodeCard) {
    Node n;
    n.block = BlockKind::CodeBlock;
    n.lang = "svg";
    n.raw = U"circle\n";
    Rig rig(300.0f);
    rig.in.imageSize = [](const std::string& url) {
        (void)url;  // the parse failed: the cache cannot answer yet
        return layout::ImageSize();
    };
    float y = 0.0f;
    layout::LaidBlock b = layout::LayoutSvgBlock(rig.ctx, n, 2, &y);

    // The code card the fallback charges: one line of the fence text.
    const float h = kM.codeLineHeight * kM.codeFontSize * kPt;
    ASSERT_EQ(rig.list.items.size(), static_cast<size_t>(2));
    EXPECT_EQ(rig.list.items[0].index(), kCard);  // the code card
    NEAR(b.height, h + 2.0f * kM.codePad);
}

TEST(SvgMermaidLayout, MermaidFlowchartChargesTheSharedLayout) {
    // Two nodes, one edge, totals that agree with them.
    auto laid = std::make_shared<mermaid::LaidOutFlowchart>();
    mermaid::LayoutNode start; start.id = 0; start.label = "Start";
    start.width = 120.0f; start.height = 40.0f;
    mermaid::LayoutNode stop; stop.id = 1; stop.label = "Stop";
    stop.width = 120.0f; stop.height = 40.0f;
    mermaid::LayoutEdge edge; edge.from = 0; edge.to = 1;
    laid->nodes = {start, stop};
    laid->edges = {edge};
    laid->width = 576.0;  // twice the padded column: the fit halves
    laid->height = 100.0;
    Node n;
    n.block = BlockKind::MermaidFlowchart;
    n.mermaid_layout = laid;
    Rig rig(312.0f);
    float y = 0.0f;
    layout::LaidBlock b = layout::LayoutMermaidBlock(rig.ctx, n, 1, &y);

    // The mac measure rule (renderer_mac.cpp 1042-1048): 288 of padded
    // column halves a 576 wide diagram, the pads stay fixed, and the
    // charged width equals the column the fit filled.
    mermaid::MermaidRender d;  // the dispatch the module mirrors
    d.kind = mermaid::MermaidKind::Flowchart; d.flow = n.mermaid_layout;
    NEAR(b.height, mermaid::MeasureMermaidHeight(
                       d, 1.0f, 312.0f - 2.0f * mermaid::kMermaidBlockPad));
    NEAR(b.width, 312.0f);
    EXPECT_EQ(b.kind, BlockKind::MermaidFlowchart);
    EXPECT_EQ(b.nodeIndex, static_cast<uint32_t>(1));
    // Charging only: a mermaid block emits no display item.
    EXPECT_EQ(rig.list.items.size(), static_cast<size_t>(0));
}

TEST(SvgMermaidLayout, FlowchartRelaidHookChargesThePaintersLayout) {
    // The relaid the painter hands over becomes the band: the walk
    // charges what the paint will draw, so an engine re-laid flowchart
    // holds the same room in the walk as the ink it paints. Without
    // the hook, or on an empty answer, the parse provisional layout
    // keeps the room, the older rule.
    auto parse = std::make_shared<mermaid::LaidOutFlowchart>();
    mermaid::LayoutNode p; p.id = 0; p.label = "P";
    p.width = 100.0f; p.height = 30.0f;
    parse->nodes = {p}; parse->edges = {};
    parse->width = 100.0f; parse->height = 40.0f;

    auto relaid = std::make_shared<mermaid::LaidOutFlowchart>();
    mermaid::LayoutNode a; a.id = 0; a.label = "Document";
    a.width = 150.0f; a.height = 44.0f;
    relaid->nodes = {a}; relaid->edges = {};
    relaid->width = 240.0f; relaid->height = 90.0f;

    Node n;
    n.block = BlockKind::MermaidFlowchart;
    n.mermaid_layout = parse;  // the parse provisional layout
    n.mermaid_source = "graph LR";
    Rig rig(1000.0f);  // a wide column: no fit shrink at zoom 1
    rig.in.flowchartRelaid = [&relaid](const Node&) {
        return relaid;
    };
    float y = 0.0f;
    layout::LaidBlock b = layout::LayoutMermaidBlock(rig.ctx, n, 0, &y);

    mermaid::MermaidRender d;  // the charge the relaid defines
    d.kind = mermaid::MermaidKind::Flowchart; d.flow = relaid;
    const float measureW = 1000.0f - 2.0f * mermaid::kMermaidBlockPad;
    NEAR(b.height, mermaid::MeasureMermaidHeight(d, 1.0f, measureW));
    NEAR(b.width, 240.0f + 2.0f * mermaid::kMermaidBlockPad);

    // An empty answer keeps the provisional parse room.
    rig.in.flowchartRelaid = [](const Node&) {
        return std::shared_ptr<mermaid::LaidOutFlowchart>();
    };
    float y2 = 0.0f;
    layout::LaidBlock b2 = layout::LayoutMermaidBlock(rig.ctx, n, 0, &y2);
    mermaid::MermaidRender d2;
    d2.kind = mermaid::MermaidKind::Flowchart; d2.flow = parse;
    NEAR(b2.height, mermaid::MeasureMermaidHeight(d2, 1.0f, measureW));
    EXPECT_EQ(rig.list.items.size(), static_cast<size_t>(0));
}

TEST(SvgMermaidLayout, MermaidWithoutLayoutKeepsPlaceholder) {
    Node n;  // a flowchart whose source never measured up
    n.block = BlockKind::MermaidFlowchart;
    n.contentOffset = 12; n.contentLength = 34;
    n.mermaid_source = "graph TD;";
    Rig rig(312.0f);
    float y = 7.0f;
    layout::LaidBlock b = layout::LayoutMermaidBlock(rig.ctx, n, 0, &y);

    // The 100 DIP placeholder of both measure passes, no width, and
    // still nothing to draw: the module only charges the record.
    NEAR(b.height, 100.0f);
    NEAR(b.width, 0.0f);
    NEAR(y, 107.0f);
    EXPECT_EQ(b.srcOffset, static_cast<uint32_t>(12));
    EXPECT_EQ(rig.list.items.size(), static_cast<size_t>(0));
}

TEST(SvgMermaidLayout, MermaidPieAndSequenceCharges) {
    // Pie and sequence charge against the full column: the default
    // pie, and a sequence charging its viewBox height past it.
    Rig rig(700.0f);
    float y = 0.0f;
    Node pie; pie.block = BlockKind::MermaidPie;
    pie.mermaid_pie = std::make_shared<mermaid::LaidOutPie>();  // 528x450
    layout::LaidBlock p = layout::LayoutMermaidBlock(rig.ctx, pie, 0, &y);
    NEAR(p.height, 450.0f + 2.0f * mermaid::kMermaidBlockPad);
    NEAR(p.width, 528.0f + 2.0f * mermaid::kMermaidBlockPad);
    Node seq; seq.block = BlockKind::MermaidSequence;
    seq.mermaid_seq = std::make_shared<mermaid::LaidOutSequence>();
    seq.mermaid_seq->width = 700.0;
    seq.mermaid_seq->height = 300.0;    // the svg height, not charged
    seq.mermaid_seq->vbheight = 340.0;  // the charged viewBox height
    layout::LaidBlock s = layout::LayoutMermaidBlock(rig.ctx, seq, 1, &y);
    NEAR(s.height, 340.0f + 2.0f * mermaid::kMermaidBlockPad);
    NEAR(s.width, 700.0f + 2.0f * mermaid::kMermaidBlockPad);
}
