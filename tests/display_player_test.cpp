// The golden tests of the shared display list player, plan Batch 3.
// A fake RenderTarget records every seam call as one compact line,
// and the handmade goldens pin the order, the dash flattening, the
// text baseline, and the image resolver contract.

#include "gtest_lite.h"
#include "layout/display_player.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using platform::Color;
using platform::FontSpec;
using platform::GlyphRun;
using platform::Point;
using platform::Rect;

// Every seam call as one line: name, then numbers at three decimals.
class FakeTarget : public platform::RenderTarget {
public:
    std::vector<std::string> calls;
    void Rec(const char* fmt, ...) {  // for calls with strings in
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
    void StrokeRect(const Rect& r, const Color& c, float w) override {
        Add("strokeRect", {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a, w});
    }
    void StrokeRoundedRect(const Rect& r, float rad, const Color& c,
                           float w) override {
        Add("strokeRoundedRect", {r.x, r.y, r.width, r.height, rad, c.r,
                                  c.g, c.b, c.a, w});
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                    const Color& c, float w) override {
        Add("strokeLine", {x0, y0, x1, y1, c.r, c.g, c.b, c.a, w});
    }
    void DrawText(const std::string& t, const FontSpec& f, float x,
                  float y, const Color& c) override {
        Rec("drawText %s %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
            t.c_str(), f.family.c_str(), f.size, x, y, c.r, c.g, c.b, c.a);
    }
    void DrawBitmap(platform::BitmapHandle image, const Rect& r) override {
        Rec("drawBitmap %llx %.3f %.3f %.3f %.3f",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(image)),
            r.x, r.y, r.width, r.height);
    }
    void FillEllipse(const Rect& r, const Color& c) override {
        Add("fillEllipse", {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a});
    }
    void StrokeEllipse(const Rect& r, const Color& c, float w) override {
        Add("strokeEllipse",
            {r.x, r.y, r.width, r.height, c.r, c.g, c.b, c.a, w});
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

// Golden compare that names both sides; a bare EXPECT_EQ does not.
void ExpectLines(const std::vector<std::string>& got,
                 const std::vector<std::string>& want) {
    EXPECT_EQ(got.size(), want.size());
    for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
        if (got[i] != want[i]) std::cerr << "  got  " << got[i]
                                          << "\n  want " << want[i] << "\n";
        EXPECT_EQ(got[i], want[i]);
    }
}

}  // namespace

TEST(DisplayPlayer, GoldenHandmade) {
    display::StrokeLine dashed{10, 5.0f, 40, 5.0f,
                               Color{0.2f, 0.4f, 0.6f, 1.0f}, 1.5f, {4.0f, 3.0f}};
    display::Text text;
    text.x = 8; text.top = 20;  // baseline 32: top plus ascent
    text.line.width = 120; text.line.ascent = 12;
    GlyphRun plain, link;  // body text, then an underlined link run
    plain.text = "plain"; plain.font.family = "Segoe UI";
    plain.color = Color{0.2f, 0.2f, 0.2f, 1.0f};
    link.text = "link"; link.font.family = "Segoe UI"; link.x = 42;
    link.underline = true; link.color = Color{0.1f, 0.4f, 0.9f, 1.0f};
    text.line.runs = {plain, link};
    display::StrokePolygon route;  // the dash walks across the corner
    route.points = {Point{0, 0}, Point{10, 0}, Point{10, 10}};
    route.color = Color{1, 0, 0, 1}; route.lineWidth = 1; route.dash = {6, 4};
    display::Image image;
    image.url = "logo.png"; image.rect = Rect{100, 200, 60, 40};
    display::DisplayList list;
    list.items = {display::PushClip{Rect{4, 2, 300, 200}},
                  display::FillRect{Rect{10, 12, 50, 30},
                                    Color{0.1f, 0.1f, 0.12f, 1.0f}},
                  dashed, text, route, image, display::PopClip{}};
    FakeTarget target;
    display::ImageResolver resolver = [](const display::Image&) {
        return reinterpret_cast<platform::BitmapHandle>(uintptr_t(0x1234));
    };
    display::Play(list, &target, resolver);
    // One line per seam call: five dash segments with the last cut
    // short; the runs on the shared baseline; the carried route dash.
    ExpectLines(target.calls, {
        "pushClip 4.000 2.000 300.000 200.000",
        "fillRect 10.000 12.000 50.000 30.000 0.100 0.100 0.120 1.000",
        "strokeLine 10.000 5.000 14.000 5.000 0.200 0.400 0.600 1.000 1.500",
        "strokeLine 17.000 5.000 21.000 5.000 0.200 0.400 0.600 1.000 1.500",
        "strokeLine 24.000 5.000 28.000 5.000 0.200 0.400 0.600 1.000 1.500",
        "strokeLine 31.000 5.000 35.000 5.000 0.200 0.400 0.600 1.000 1.500",
        "strokeLine 38.000 5.000 40.000 5.000 0.200 0.400 0.600 1.000 1.500",
        "drawText plain Segoe UI 14.000 8.000 32.000 0.200 0.200 0.200 1.000",
        "drawText link Segoe UI 14.000 50.000 32.000 0.100 0.400 0.900 1.000",
        "strokeLine 50.000 34.000 128.000 34.000 0.100 0.400 0.900 1.000 1.000",
        "strokeLine 0.000 0.000 6.000 0.000 1.000 0.000 0.000 1.000 1.000",
        "strokeLine 10.000 0.000 10.000 6.000 1.000 0.000 0.000 1.000 1.000",
        "drawBitmap 1234 100.000 200.000 60.000 40.000",
        "popClip"});
    // With no resolver at all, or a null handle answer, skip the image.
    std::vector<std::string> spliced = target.calls;
    spliced.erase(spliced.begin() + 12);  // where the image drew
    display::ImageResolver none =
        [](const display::Image&) { return platform::BitmapHandle{}; };
    for (const bool viaNull : {false, true}) {
        FakeTarget skipped;
        if (viaNull) display::Play(list, &skipped, none);
        else display::Play(list, &skipped);
        EXPECT_EQ(skipped.calls, spliced);
    }
}

// ItemBounds -----------------------------------------------------------
//
// Pagination cuts the page by these rectangles, so every alternative of the
// item variant is asked for its bounds here. A new item type has to add its
// overload or the visitor in display_player.cpp stops compiling, and this
// test is the second half of that guard: it pins what the numbers mean.

TEST(DisplayPlayer, ItemBoundsOfEveryAlternative) {
    using display::ItemBounds;

    display::FillRect fill;
    fill.rect = Rect{10.0f, 20.0f, 30.0f, 40.0f};
    Rect r = ItemBounds(fill);
    EXPECT_EQ(r.x, 10.0f);
    EXPECT_EQ(r.y, 20.0f);
    EXPECT_EQ(r.width, 30.0f);
    EXPECT_EQ(r.height, 40.0f);

    display::FillRoundedRect card;
    card.rect = Rect{1.0f, 2.0f, 3.0f, 4.0f};
    card.radius = 5.0f;
    r = ItemBounds(card);
    EXPECT_EQ(r.height, 4.0f);

    display::FillRoundedRectSides pills;
    pills.rect = Rect{2.0f, 3.0f, 4.0f, 5.0f};
    r = ItemBounds(pills);
    EXPECT_EQ(r.width, 4.0f);

    // A stroke's bounds are the path it strokes, never the pen's width: the
    // page cut decides by where an item is, not by how wide its pen is.
    display::StrokeRect stroke;
    stroke.rect = Rect{10.0f, 10.0f, 10.0f, 10.0f};
    stroke.lineWidth = 4.0f;
    r = ItemBounds(stroke);
    EXPECT_EQ(r.x, 10.0f);
    EXPECT_EQ(r.width, 10.0f);

    display::StrokeRoundedRect rounded;
    rounded.rect = Rect{10.0f, 10.0f, 10.0f, 10.0f};
    rounded.lineWidth = 2.0f;
    r = ItemBounds(rounded);
    EXPECT_EQ(r.y, 10.0f);

    // A rule is a hairline: its bounds have no height at all, which is what
    // puts it on the page its own y is on and on no other.
    display::StrokeLine rule;
    rule.x0 = 100.0f;
    rule.x1 = 20.0f;  // right to left, so the minimum is not the first point
    rule.y0 = rule.y1 = 50.0f;
    rule.lineWidth = 2.0f;
    r = ItemBounds(rule);
    EXPECT_EQ(r.x, 20.0f);
    EXPECT_EQ(r.width, 80.0f);
    EXPECT_EQ(r.y, 50.0f);
    EXPECT_EQ(r.height, 0.0f);

    // A polygon whose points are not in reading order: the bounds are the
    // extremes, not the first and the last point.
    display::StrokePolygon route;
    route.lineWidth = 0.0f;
    route.points = {Point{50.0f, 10.0f}, Point{10.0f, 60.0f},
                    Point{30.0f, 20.0f}};
    r = ItemBounds(route);
    EXPECT_EQ(r.x, 10.0f);
    EXPECT_EQ(r.y, 10.0f);
    EXPECT_EQ(r.width, 40.0f);
    EXPECT_EQ(r.height, 50.0f);

    display::FillEllipse ellipse;
    ellipse.rect = Rect{5.0f, 5.0f, 15.0f, 25.0f};
    r = ItemBounds(ellipse);
    EXPECT_EQ(r.height, 25.0f);

    display::StrokeEllipse ring;
    ring.rect = Rect{5.0f, 5.0f, 15.0f, 25.0f};
    ring.lineWidth = 6.0f;
    r = ItemBounds(ring);
    EXPECT_EQ(r.x, 5.0f);

    display::FillPolygon arrow;
    arrow.points = {Point{0.0f, 0.0f}, Point{8.0f, 4.0f}, Point{0.0f, 8.0f}};
    r = ItemBounds(arrow);
    EXPECT_EQ(r.width, 8.0f);
    EXPECT_EQ(r.height, 8.0f);

    display::PieSlice slice;
    slice.bounds = Rect{0.0f, 0.0f, 100.0f, 100.0f};
    r = ItemBounds(slice);
    EXPECT_EQ(r.width, 100.0f);

    // A text line with no width is legal (the fake engine can answer one),
    // and its bounds are honest about it.
    display::Text text;
    text.x = 12.0f;
    text.top = 34.0f;
    text.line.width = 0.0f;
    text.line.height = 16.0f;
    r = ItemBounds(text);
    EXPECT_EQ(r.x, 12.0f);
    EXPECT_EQ(r.width, 0.0f);
    EXPECT_EQ(r.height, 16.0f);

    display::Image picture;
    picture.rect = Rect{3.0f, 4.0f, 50.0f, 60.0f};
    r = ItemBounds(picture);
    EXPECT_EQ(r.height, 60.0f);

    // A clip reports its rect, and a pop has none: slicing keeps both
    // whatever the rect says.
    display::PushClip push;
    push.rect = Rect{0.0f, 500.0f, 100.0f, 100.0f};
    r = ItemBounds(push);
    EXPECT_EQ(r.y, 500.0f);
    r = ItemBounds(display::PopClip{});
    EXPECT_EQ(r.width, 0.0f);
    EXPECT_EQ(r.height, 0.0f);
}
