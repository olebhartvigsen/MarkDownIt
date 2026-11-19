// The print painter suite of the modern print plan (tasks 1, 2 and 4):
// the player's origin shift, the one band painter that places a page, and
// the page a panel's description builds. Everything runs on handmade
// layouts and a recording seam target, the same recording style as the
// player's own goldens, so both runners print the same bytes.

#include "gtest_lite.h"
#include "layout/display_player.h"
#include "layout/print_pages.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using platform::Color;
using platform::Point;
using platform::Rect;

// Every seam call as one line: name, then numbers at three decimals.
// The copy of the player's own recording target, so a painter suite and
// a player suite read the same way.
class FakeTarget : public platform::RenderTarget {
public:
    std::vector<std::string> calls;
    void Rec(const char* fmt, ...) {
        char buf[240];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        calls.emplace_back(buf);
    }
    void Add(const char* name, std::vector<float> ns) {
        std::string s = name; char b[24];
        for (float v : ns) { std::snprintf(b, sizeof b, " %.3f", v); s += b; }
        calls.push_back(s);
    }
    static std::vector<float> Flat(const std::vector<Point>& ps,
                                  const Color& c) {
        std::vector<float> ns;
        for (const Point& p : ps) { ns.push_back(p.x); ns.push_back(p.y); }
        ns.push_back(c.r); ns.push_back(c.g); ns.push_back(c.b); ns.push_back(c.a);
        return ns;
    }
    void FillRect(const Rect& r, const Color& c) override {
        Add("fillRect", {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a});
    }
    void FillRoundedRect(const Rect& r, float rad, const Color& c) override {
        Add("fillRoundedRect", {r.x, r.y, r.width, r.height, rad,
                                c.r, c.g, c.b, c.a});
    }
    void FillRoundedRectSides(const Rect& r, float rad, const Color& c,
                              bool left, bool right) override {
        Add("fillRoundedRectSides", {r.x, r.y, r.width, r.height, rad,
                                      c.r, c.g, c.b, c.a, left ? 1.0f : 0.0f,
                                      right ? 1.0f : 0.0f});
    }
    void StrokeRect(const Rect& r, const Color& c, float w) override {
        Add("strokeRect", {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a, w});
    }
    void StrokeRoundedRect(const Rect& r, float rad, const Color& c,
                           float w) override {
        Add("strokeRoundedRect", {r.x, r.y, r.width, r.height, rad, c.r, c.g,
                                  c.b, c.a, w});
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                    const Color& c, float w) override {
        Add("strokeLine", {x0, y0, x1, y1, c.r, c.g, c.b, c.a, w});
    }
    void DrawText(const std::string& t, const platform::FontSpec& f, float x,
                  float y, const Color& c) override {
        Rec("drawText %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
            t.c_str(), f.family.c_str(), f.size, x, y, c.r, c.g, c.b, c.a);
    }
    void DrawBitmap(platform::BitmapHandle, const Rect&) override {}
    void FillEllipse(const Rect& r, const Color& c) override {
        Add("fillEllipse", {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a});
    }
    void StrokeEllipse(const Rect& r, const Color& c, float w) override {
        Add("strokeEllipse", {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a,
                              w});
    }
    void FillPolygon(const std::vector<Point>& ps, const Color& c) override {
        Add("fillPolygon", Flat(ps, c));
    }
    void StrokePolygon(const std::vector<Point>& ps, const Color& c,
                       float w, bool closed) override {
        std::vector<float> ns = Flat(ps, c);
        ns.push_back(w); ns.push_back(closed ? 1.0f : 0.0f);
        Add("strokePolygon", ns);
    }
    void FillPieSlice(const Rect& r, float s, float e, const Color& c) override {
        Add("fillPieSlice", {r.x, r.y, r.width, r.height, s, e, c.r, c.g, c.b,
                             c.a});
    }
    void PushClip(const Rect& r) override {
        Add("pushClip", {r.x, r.y, r.width, r.height});
    }
    void PopClip() override { calls.push_back("popClip"); }
};

void ExpectLines(const std::vector<std::string>& got,
                 const std::vector<std::string>& want) {
    EXPECT_EQ(got.size(), want.size());
    for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
        if (got[i] != want[i]) std::cerr << "  got  " << got[i]
                                          << "\n  want " << want[i] << "\n";
        EXPECT_EQ(got[i], want[i]);
    }
}

const Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};

display::Item Box(float x, float y, float w, float h) {
    display::FillRect fill;
    fill.rect = platform::Rect{x, y, w, h};
    fill.color = kBlack;
    return fill;
}

display::Item Rule(float y) {
    display::StrokeLine line;
    line.x0 = 0.0f;
    line.x1 = 200.0f;
    line.y0 = y;
    line.y1 = y;
    line.lineWidth = 1.0f;
    line.color = kBlack;
    return line;
}

// A laid document whose blocks and items are placed by hand: the page
// model is pure geometry, so the painter tests need no engine on the
// numbers that matter.
layout::DocumentLayout HandLayout(std::vector<display::Item> items,
                                  std::vector<layout::LaidBlock> blocks) {
    layout::DocumentLayout laid;
    laid.blocks = std::move(blocks);
    laid.list.items = std::move(items);
    return laid;
}

layout::LaidBlock Para(float y, float h) {
    layout::LaidBlock b;
    b.kind = BlockKind::Paragraph;
    b.y = y;
    b.height = h;
    return b;
}

}  // namespace

// The player ------------------------------------------------------------

TEST(PlayWithOrigin, ZeroIsPlainPlay) {
    display::DisplayList list;
    list.items.push_back(Box(0, 0, 100, 50));
    list.items.push_back(Rule(40));
    list.items.push_back(Box(10, 20, 5, 5));
    FakeTarget plain;
    display::Play(list, &plain);
    FakeTarget zeroed;
    display::PlayWithOrigin(list, &zeroed, 0.0f, 0.0f);
    ExpectLines(zeroed.calls, plain.calls);
    EXPECT_FALSE(plain.calls.empty());
}

TEST(PlayWithOrigin, ShiftsEveryGeometry) {
    display::DisplayList list;
    list.items.push_back(Box(0, 0, 100, 50));
    list.items.push_back(Rule(40));
    display::FillPolygon poly;
    poly.points = {Point{0, 0}, Point{10, 0}, Point{10, 20}};
    poly.color = kBlack;
    list.items.push_back(poly);
    display::PushClip clip;
    clip.rect = Rect{0, 10, 600, 300};
    list.items.push_back(clip);
    list.items.push_back(display::PopClip{});
    FakeTarget t;
    display::PlayWithOrigin(list, &t, 37.5f, -12.25f);
    ExpectLines(t.calls, {
        "fillRect 37.500 -12.250 100.000 50.000 0.000 0.000 0.000 1.000",
        "strokeLine 37.500 27.750 237.500 27.750 0.000 0.000 0.000 1.000 1.000",
        "fillPolygon 37.500 -12.250 47.500 -12.250 47.500 7.750 0.000 0.000"
        " 0.000 1.000",
        "pushClip 37.500 -2.250 600.000 300.000",
        "popClip",
    });
}

// The band painter ------------------------------------------------------

TEST(PaintBandPage, FirstBandSitsAtTheMargin) {
    const layout::PrintPage page;  // A4, 2 cm margin, the shared default
    std::vector<display::Item> items;
    items.push_back(Box(0, 10, 100, 50));
    items.push_back(Rule(40));
    const layout::DocumentLayout laid =
        HandLayout(std::move(items), {Para(0, 300)});
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, page);
    ASSERT_EQ(bands.size(), 1u);
    FakeTarget t;
    layout::PaintBandPage(t, laid, page, bands, 0);
    // The band starts at the document's top, so the placement is the page
    // margin itself: the first box moves by (75.6, 75.6).
    ExpectLines(t.calls, {
        "fillRect 75.600 85.600 100.000 50.000 0.000 0.000 0.000 1.000",
        "strokeLine 75.600 115.600 275.600 115.600 0.000 0.000 0.000"
        " 1.000 1.000",
    });
}

TEST(PaintBandPage, SecondBandStartsOver) {
    const layout::PrintPage page;
    std::vector<display::Item> items;
    items.push_back(Box(0, 10, 100, 50));
    items.push_back(Box(0, 610, 100, 50));
    std::vector<layout::LaidBlock> blocks;
    blocks.push_back(Para(0, 600));
    blocks.push_back(Para(600, 500));
    const layout::DocumentLayout laid =
        HandLayout(std::move(items), std::move(blocks));
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, page);
    ASSERT_EQ(bands.size(), 2u);
    EXPECT_NEAR(bands[1].top, 600.0f, 0.001f);
    FakeTarget t;
    layout::PaintBandPage(t, laid, page, bands, 1);
    // The second band cuts at 600, so the box at 610 lands at 75.6 + 10:
    // the band's own distance from the top comes back out.
    ExpectLines(t.calls, {
        "fillRect 75.600 85.600 100.000 50.000 0.000 0.000 0.000 1.000",
    });
}

TEST(PaintBandPage, IsTheOneRuleAndThePlayer) {
    // The painter is the placement and the player, nothing else: painting
    // a band equals playing its items with PlacementForBand by hand, for
    // both bands of a document cut in two.
    const layout::PrintPage page;
    std::vector<display::Item> items;
    for (int i = 0; i < 40; ++i) {
        items.push_back(Box(float(i) * 2, float(i) * 60, 80, 40));
        items.push_back(Rule(float(i) * 60 + 20));
    }
    std::vector<layout::LaidBlock> blocks;
    blocks.push_back(Para(0, 1200));
    blocks.push_back(Para(1250, 400));
    const layout::DocumentLayout laid =
        HandLayout(std::move(items), std::move(blocks));
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, page);
    EXPECT_GE(bands.size(), 2u);
    for (size_t i = 0; i < bands.size(); ++i) {
        FakeTarget painted;
        layout::PaintBandPage(painted, laid, page, bands, i);
        FakeTarget manual;
        const layout::PagePlacement place =
            layout::PlacementForBand(page, bands[i]);
        display::PlayWithOrigin(layout::PageItems(laid, bands[i]), &manual,
                                place.x, place.y);
        ExpectLines(painted.calls, manual.calls);
        EXPECT_FALSE(painted.calls.empty());
    }
}

// The page from a description -------------------------------------------

TEST(PrintPageFromDescription, NoStripKeepsThePrintRule) {
    // A driver that reports no strip, the DC shape: the sheet passes twice
    // and the margin is the old inset, 2 cm on A4, fifteen percent of the
    // shorter side on a small one.
    const layout::PrintPage a4 =
        layout::PrintPageFromDescription(793.7f, 1122.5f, 0.0f, 0.0f,
                                          793.7f, 1122.5f);
    EXPECT_NEAR(a4.margin, 75.6f, 0.001f);
    const layout::PrintPage label =
        layout::PrintPageFromDescription(384.0f, 576.0f, 0.0f, 0.0f,
                                          384.0f, 576.0f);
    EXPECT_NEAR(label.margin, 57.6f, 0.001f);
}

TEST(PrintPageFromDescription, NarrowStripKeepsTheTwoCmRule) {
    // A quarter-inch strip all around: the inset of 75.6 stands, because
    // the printer's strip is inside it.
    const layout::PrintPage page =
        layout::PrintPageFromDescription(793.7f, 1122.5f, 24.0f, 24.0f,
                                          745.7f, 1074.5f);
    EXPECT_NEAR(page.width, 793.7f, 0.001f);
    EXPECT_NEAR(page.height, 1122.5f, 0.001f);
    EXPECT_NEAR(page.margin, 75.6f, 0.001f);
}

TEST(PrintPageFromDescription, WidestEdgeDecides) {
    // A punched or driven tray with a wide left strip: the layout must
    // clear it, so the margin takes the edge and drops the 2-cm look.
    const layout::PrintPage page =
        layout::PrintPageFromDescription(793.7f, 1122.5f, 120.0f, 24.0f,
                                          649.7f, 1074.5f);
    EXPECT_NEAR(page.margin, 120.0f, 0.001f);
}

TEST(PrintPageFromDescription, SmallSheetShrinksTheInset) {
    // A label printer: the strip is narrow, but the 2 cm does not fit, so
    // the inset shrinks to fifteen percent of the shorter side, as the
    // DC rule always did.
    const layout::PrintPage page =
        layout::PrintPageFromDescription(384.0f, 576.0f, 12.0f, 12.0f,
                                          360.0f, 552.0f);
    EXPECT_NEAR(page.margin, 57.6f, 0.001f);
}

TEST(PrintPageFromDescription, ContentStaysInsideTheImageableArea) {
    // Whatever the strip is, the content width clears the widest edge on
    // both sides, so a line never reaches past the printer's rectangle.
    const layout::PrintPage page =
        layout::PrintPageFromDescription(793.7f, 1122.5f, 40.0f, 120.0f,
                                          713.7f, 1022.5f);
    EXPECT_NEAR(page.margin - 120.0f, 0.0f, 0.001f);  // the bottom edge wins
    EXPECT_LE(page.ContentWidth(), 793.7f - 40.0f - 40.0f + 0.001f);
}
