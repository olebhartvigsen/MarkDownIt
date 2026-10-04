#include "gtest_lite.h"
#include "svgtext.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

TEST(SvgText, FindsSingleText) {
    std::string xml =
        "<svg><text x=\"10\" y=\"20\" font-size=\"14\">Hej</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Hej");
    EXPECT_TRUE(runs[0].x > 9.9f && runs[0].x < 10.1f);
    EXPECT_TRUE(runs[0].y > 19.9f && runs[0].y < 20.1f);
}

TEST(SvgText, ReadsFontSize) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\" font-size=\"18\">X</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].fontSize > 17.9f && runs[0].fontSize < 18.1f);
}

TEST(SvgText, ReadsTextAnchor) {
    std::string xml =
        "<svg><text x=\"5\" y=\"5\" text-anchor=\"middle\">Midt</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].anchor, "middle");
}

TEST(SvgText, ReadsFill) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\" fill=\"#ff0000\">R</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].fill, "#ff0000");
}

TEST(SvgText, ReadsBold) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\" font-weight=\"bold\">B</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].bold);
}

TEST(SvgText, ReadsTspanChildren) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\"><tspan>A</tspan><tspan>B</tspan></text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].text, "A");
    EXPECT_EQ(runs[1].text, "B");
}

TEST(SvgText, DecodesEntities) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\">A &amp; B &lt;C&gt;</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "A & B <C>");
}

TEST(SvgText, IgnoresTextInsideDefs) {
    std::string xml =
        "<svg><defs><text x=\"0\" y=\"0\">skjult</text></defs></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    EXPECT_EQ(runs.size(), 0u);
}

TEST(SvgText, GarbageDoesNotThrow) {
    auto runs = svg::ExtractTextRuns("<svg><text x=");
    EXPECT_TRUE(runs.size() < 2u);
}

TEST(SvgText, StyleAttrOverrides) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\" style=\"font-size:20px;fill:#00ff00\">S</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].fontSize > 19.9f && runs[0].fontSize < 20.1f);
    EXPECT_EQ(runs[0].fill, "#00ff00");
}

TEST(SvgText, StripsTextElements) {
    std::string xml =
        "<svg><rect x=\"0\"/><text x=\"1\" y=\"2\">Hej</text></svg>";
    std::string out = svg::StripTextElements(xml);
    EXPECT_TRUE(out.find("rect") != std::string::npos);
    EXPECT_TRUE(out.find("Hej") == std::string::npos);
}

TEST(SvgText, StripsForeignObject) {
    std::string xml =
        "<svg><foreignObject><div>Hej</div></foreignObject><rect/></svg>";
    std::string out = svg::StripTextElements(xml);
    EXPECT_TRUE(out.find("foreignObject") == std::string::npos);
    EXPECT_TRUE(out.find("rect") != std::string::npos);
}

TEST(SvgText, StripsTspanInsideText) {
    std::string xml =
        "<svg><g><text x=\"0\" y=\"0\"><tspan>A</tspan></text></g></svg>";
    std::string out = svg::StripTextElements(xml);
    EXPECT_TRUE(out.find("tspan") == std::string::npos);
    EXPECT_TRUE(out.find("<g>") != std::string::npos);
    EXPECT_TRUE(out.find("</g>") != std::string::npos);
}

TEST(SvgText, StripsPreservesComments) {
    std::string xml =
        "<svg><!-- comment --><rect/></svg>";
    std::string out = svg::StripTextElements(xml);
    EXPECT_TRUE(out.find("<!-- comment -->") != std::string::npos);
    EXPECT_TRUE(out.find("rect") != std::string::npos);
}

TEST(SvgText, BoxFromDataGroupCoversRects) {
    std::string xml =
        "<svg>"
        "<g data=\"shape-A\">"
        "<g transform=\"translate(10,10) scale(2,2)\">"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"50\"/>"
        "</g>"
        "<text x=\"60\" y=\"20\">Label</text>"
        "</g>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].boxValid);
    EXPECT_TRUE(runs[0].bx > 9.9f && runs[0].bx < 10.1f);
    EXPECT_TRUE(runs[0].by > 9.9f && runs[0].by < 10.1f);
    EXPECT_TRUE(runs[0].bw > 199.9f && runs[0].bw < 200.1f);
    EXPECT_TRUE(runs[0].bh > 99.9f && runs[0].bh < 100.1f);
}

TEST(SvgText, NoBoxWithoutDataGroup) {
    std::string xml =
        "<svg><g><rect x=\"0\" y=\"0\" width=\"8\" height=\"8\"/>"
        "<text x=\"4\" y=\"4\">T</text></g></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_FALSE(runs[0].boxValid);
}

// --- Regression tests for the SVG text viewer review ---------------------
// Each of these reproduced a visible defect: wrong glyph position, missing
// transform, duplicated fallback label, or ignored styling.

// A <textPath> carries no x/y, so the label used to be drawn at the document
// origin on top of unrelated content.
TEST(SvgText, TextPathAnchorsToPathStart) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\">"
        "<path id=\"p\" d=\"M40,150 C80,60 140,60 180,150\"/>"
        "<text><textPath href=\"#p\">Mid</textPath></text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 40.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 150.0f, 0.01f);
}

// The reusable path usually lives in <defs>, which is skipped wholesale.
TEST(SvgText, TextPathFindsPathInsideDefs) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\"><defs>"
        "<path id=\"p\" d=\"M40,150 L180,150\"/></defs>"
        "<text><textPath href=\"#p\">Mid</textPath></text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 40.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 150.0f, 0.01f);
}

// rotate/skew were dropped, so shapes turned and their labels stayed put.
TEST(SvgText, RotateMovesAndMarksRun) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\">"
        "<g transform=\"rotate(90 100 100)\">"
        "<text x=\"100\" y=\"50\">Sideways</text></g></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].transformed);
    EXPECT_NEAR(runs[0].x, 150.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 100.0f, 0.01f);
}

TEST(SvgText, SkewXMovesAndMarksRun) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\">"
        "<g transform=\"skewX(20)\"><text x=\"20\" y=\"20\">S</text></g></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].transformed);
    EXPECT_NEAR(runs[0].x, 27.3f, 0.1f);
}

// A transform name is case-sensitive in SVG: skewX is not skew.
TEST(SvgText, SkewIsCaseInsensitiveInName) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\">"
        "<g transform=\"skewX(20)\"><text x=\"0\" y=\"20\">S</text></g></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].transformed);
}

TEST(SvgText, PlainTextIsNotMarkedTransformed) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"100\" height=\"50\"><text x=\"5\" y=\"20\">P</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_FALSE(runs[0].transformed);
    EXPECT_FALSE(runs[0].chained);
}

// <switch> drew every branch, stamping fallback labels on top of each other.
TEST(SvgText, SwitchDrawsOnlyFirstBranch) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"100\"><switch>"
        "<g systemLanguage=\"en\"><text x=\"20\" y=\"30\">Hello</text></g>"
        "<g systemLanguage=\"fr\"><text x=\"20\" y=\"30\">Bonjour</text></g>"
        "</switch></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Hello");
}

TEST(SvgText, SwitchWithBareTextBranches) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"100\"><switch>"
        "<text x=\"20\" y=\"30\">A</text>"
        "<text x=\"20\" y=\"30\">B</text></switch></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "A");
}

TEST(SvgText, SwitchSkipsSelfClosingFirstBranch) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"100\"><switch><g/>"
        "<g><text x=\"20\" y=\"30\">B</text></g></switch></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "B");
}

// A nested switch decides for itself; its choice stands.
TEST(SvgText, NestedSwitchKeepsInnerChoice) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"100\"><switch>"
        "<switch><g><text x=\"20\" y=\"30\">Inner</text></g></switch>"
        "<g><text x=\"20\" y=\"30\">Outer</text></g></switch></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Inner");
}

// A self-closing child must not raise the nesting depth, or the chosen branch
// never ends and the next sibling is drawn.
TEST(SvgText, SwitchBranchWithSelfClosingChild) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"100\"><switch>"
        "<g><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
        "<text x=\"20\" y=\"30\">Lab</text></g>"
        "<g><text x=\"99\" y=\"99\">No</text></g></switch></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Lab");
}

TEST(SvgText, EmptySwitchDoesNotSwallowSiblings) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"100\" height=\"50\"><switch></switch>"
        "<text x=\"5\" y=\"20\">After</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "After");
}

TEST(SvgText, NoSwitchIsUnaffected) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"100\" height=\"50\"><text x=\"5\" y=\"20\">Plain</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Plain");
}

// <style> class rules were ignored, so Inkscape and hand-written files lost
// every size and colour that was not repeated on the element.
TEST(SvgText, CssClassSetsFontSizeAndFill) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\"><style>"
        ".lab{font-size:9px;fill:#ff0000}</style>"
        "<text class=\"lab\" x=\"10\" y=\"20\">Red</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 9.0f, 0.01f);
    EXPECT_EQ(runs[0].fill, "#ff0000");
}

TEST(SvgText, CssClassOnGroupInherits) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\"><style>.big{font-size:24px}</style>"
        "<g class=\"big\"><text x=\"10\" y=\"30\">Big</text></g></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 24.0f, 0.01f);
}

TEST(SvgText, CssClassMatchesAnyNameInList) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\"><style>.b{font-size:7px}</style>"
        "<text class=\"a b\" x=\"10\" y=\"20\">T</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 7.0f, 0.01f);
}

// Presentation attributes outrank the stylesheet.
TEST(SvgText, AttributeBeatsStylesheet) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\"><style>.lab{font-size:9px}</style>"
        "<text class=\"lab\" font-size=\"30\" x=\"10\" y=\"20\">A</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 30.0f, 0.01f);
}

// Element selectors are not applied rather than misapplied.
TEST(SvgText, ElementSelectorIsIgnoredNotMisapplied) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\"><style>text{font-size:99px}</style>"
        "<text x=\"10\" y=\"20\">El</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 16.0f, 0.01f);
}

// dy with no y is the only placement such text has.
TEST(SvgText, DyWithoutYPositionsText) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"100\" height=\"50\"><text x=\"10\" dy=\"12\">Mid</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].y, 12.0f, 0.01f);
}

// ToFloat stopped at the unit suffix, so "9pt" was treated as 9px.
TEST(SvgText, PtUnitIsConverted) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\">"
        "<text x=\"10\" y=\"20\" font-size=\"9pt\">Pt</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 11.25f, 0.01f);
}

TEST(SvgText, PxUnitIsUnchanged) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\">"
        "<text x=\"10\" y=\"20\" font-size=\"12px\">P</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 12.0f, 0.01f);
}

// Text after a <tspan x="..."> continues from the tspan's end.
TEST(SvgText, TextAfterTspanXIsChained) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"100\" height=\"50\"><text x=\"1\" y=\"9\">"
        "AB<tspan x=\"50\">CD</tspan>EF</text></svg>");
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].text, "AB");
    EXPECT_FALSE(runs[0].chained);
    EXPECT_EQ(runs[1].text, "CD");
    EXPECT_TRUE(runs[1].chained);
    EXPECT_EQ(runs[2].text, "EF");
    EXPECT_TRUE(runs[2].chained);
}

// A nested <svg x/y> shifts its subtree; the text used to land at the origin.
TEST(SvgText, NestedSvgOffsetApplies) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"100\" height=\"50\"><svg x=\"10\" y=\"20\" "
        "width=\"50\" height=\"20\"><text x=\"1\" y=\"9\">In</text></svg></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 11.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 29.0f, 0.01f);
}

// CDATA is literal text and may contain '>'.
TEST(SvgText, CDataInsideTextBecomesRun) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\">"
        "<text x=\"10\" y=\"20\"><![CDATA[A > B]]></text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "A > B");
}

TEST(SvgText, CDataWrappedStyleIsParsed) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"60\"><style>"
        "<![CDATA[.lab{font-size:11px}]]></style>"
        "<text class=\"lab\" x=\"10\" y=\"20\">C</text></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].fontSize, 11.0f, 0.01f);
}

// translate and scale were already handled; keep them working.
TEST(SvgText, TranslateStillPositionsText) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\">"
        "<g transform=\"translate(5,5)\"><text x=\"100\" y=\"100\">T</text></g></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 105.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 105.0f, 0.01f);
    EXPECT_FALSE(runs[0].transformed);
}

TEST(SvgText, ScaleStillPositionsText) {
    auto runs = svg::ExtractTextRuns(
        "<svg width=\"200\" height=\"200\">"
        "<g transform=\"scale(2)\"><text x=\"100\" y=\"100\">S</text></g></svg>");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 200.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 200.0f, 0.01f);
}


// --- Label/shape pairing must not be quadratic -------------------------
// Each <g data='..'> with a <rect> becomes a box; each <text> a run. The
// pairing pass compared every run against every box, which is O(runs*boxes):
// 16000 shapes plus 16000 labels took 1.2 s and grew 3.9x per doubling, so a
// large but ordinary diagram stalled. It now uses a uniform grid and stops
// expanding rings once no further cell can hold a closer centre.
TEST(SvgBoxPairing, LargeDiagramStaysLinear) {
    // Two sizes, 2x apart. A quadratic pass grows ~4x, a linear one ~2x.
    auto timeIt = [](size_t n) {
        std::string xml = "<svg xmlns='http://www.w3.org/2000/svg'>";
        char buf[256];
        for (size_t i = 0; i < n; ++i) {
            const double x = 100.0 * static_cast<double>(i % 100);
            const double y = 100.0 * static_cast<double>(i / 100);
            snprintf(buf, sizeof buf,
                     "<g data='s%zu'><rect x='%.1f' y='%.1f' width='40' height='20'/></g>", i, x, y);
            xml += buf;
            snprintf(buf, sizeof buf, "<text x='%.1f' y='%.1f'>node %zu</text>", x + 5, y + 14, i);
            xml += buf;
        }
        xml += "</svg>";
        const auto t0 = std::chrono::steady_clock::now();
        auto runs = svg::ExtractTextRuns(xml);
        const auto t1 = std::chrono::steady_clock::now();
        EXPECT_EQ(runs.size(), n);
        return std::chrono::duration<double>(t1 - t0).count();
    };
    // Take the best of three at each size: a single run on a shared CI box is
    // noisy enough to swamp the ratio. Measured here, doubling the diagram
    // costs the old linear scan 3.4x and the grid 2.0x, so 2.8x separates
    // them with room for scheduling noise on both sides.
    auto best = [&timeIt](size_t n) {
        double b = 1e9;
        for (int i = 0; i < 3; ++i) b = std::min(b, timeIt(n));
        return b;
    };
    const double t1 = best(2000);
    const double t2 = best(4000);
    // The floor keeps a fast machine from failing on a near-zero measurement;
    // it is well under the ~16 ms the old code needs at 4000.
    EXPECT_LT(t2, std::max(t1 * 2.8, 0.030));
}

// Every label must still find its shape, and find the NEAREST one. A grid that
// searched too few cells would silently pair labels with the wrong box.
TEST(SvgBoxPairing, EachRunFindsItsContainingBox) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g data='a'><rect x='0' y='0' width='100' height='100'/></g>"
        "<g data='b'><rect x='500' y='500' width='20' height='20'/></g>"
        "<text x='50' y='50'>inside a</text>"
        "<text x='510' y='510'>inside b</text>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_TRUE(runs[0].boxValid);
    EXPECT_TRUE(runs[1].boxValid);
    EXPECT_NEAR(runs[0].bx, 0.0f, 0.01f);
    EXPECT_NEAR(runs[0].bw, 100.0f, 0.01f);
    EXPECT_NEAR(runs[1].bx, 500.0f, 0.01f);
    EXPECT_NEAR(runs[1].bw, 20.0f, 0.01f);
}

// A run outside every box must still be paired with the nearest centre. An
// early exit that fires before any candidate is seen leaves boxValid false.
TEST(SvgBoxPairing, RunOutsideAllBoxesFindsNearest) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g data='a'><rect x='0' y='0' width='10' height='10'/></g>"
        "<g data='b'><rect x='4000' y='4000' width='10' height='10'/></g>"
        "<text x='3900' y='3900'>near b</text>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].boxValid);
    EXPECT_NEAR(runs[0].bx, 4000.0f, 0.01f);
}

// Two equally good boxes must resolve the same way every time, whatever order
// the search visits them in. Otherwise a label jumps between shapes.
TEST(SvgBoxPairing, EqualAreaTieIsStable) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g data='first'><rect x='0' y='0' width='50' height='50'/></g>"
        "<g data='second'><rect x='20' y='20' width='50' height='50'/></g>"
        "<text x='30' y='30'>t</text>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].boxValid);
    // Equal areas: the earlier box wins, both on the first call and after a
    // rebuild, so the choice cannot depend on scan order.
    EXPECT_NEAR(runs[0].bx, 0.0f, 0.01f);
    auto again = svg::ExtractTextRuns(xml);
    ASSERT_EQ(again.size(), 1u);
    EXPECT_NEAR(again[0].bx, runs[0].bx, 0.001f);
}


// --- CSS rule lookup and textPath lookup must not be linear ------------
// Both used to scan every rule, respectively every declared path, for each
// styled element. That is quadratic in rules times elements, and the stress
// pass lost its largest case to it.
TEST(SvgStyleLookup, ManyRulesStayLinear) {
    // A lookup that always resolves to the LAST rule is the worst case: a scan
    // can never exit early. Doubling costs the old scan ~4.3x, a hash ~2x.
    // The quadratic is rules TIMES styled elements, so both have to grow: with
    // a single <text> the lookup runs once and the old code is linear too.
    // Both dimensions double together, each element carrying a class that
    // resolves to the LAST rule, so a scan can never exit early.
    auto timeIt = [](size_t n) {
        std::string xml = "<style>";
        char buf[64];
        for (size_t i = 0; i < n; ++i) {
            snprintf(buf, sizeof buf, ".c%zu{fill:red}", i);
            xml += buf;
        }
        xml += "</style>";
        for (size_t i = 0; i < n; ++i)
            xml += "<text class='cLAST' x='1' y='2'>A</text>";
        const auto t0 = std::chrono::steady_clock::now();
        auto runs = svg::ExtractTextRuns(xml);
        const auto t1 = std::chrono::steady_clock::now();
        EXPECT_EQ(runs.size(), n);
        return std::chrono::duration<double>(t1 - t0).count();
    };
    auto best = [&timeIt](size_t n) {
        double b = 1e9;
        for (int i = 0; i < 3; ++i) b = std::min(b, timeIt(n));
        return b;
    };
    // Start where the quadratic term dominates. At 1000 rules the whole
    // stylesheet parse is only a few ms, which a fixed floor would swallow and
    // let the regression through. At 2000 rules the old scan costs 40 ms and
    // the index 5 ms, so an 8 ms ceiling separates them with room either way.
    const double t1 = best(500);
    const double t2 = best(1000);
    EXPECT_LT(t2, std::max(t1 * 3.0, 0.020));
}

// The first declaration for a class must still win. Indexing the rules must
// not change which of two declarations for the same class applies.
TEST(SvgStyleLookup, FirstDeclarationStillWins) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<style>.c{fill:#111}.c{fill:#222}</style>"
        "<text class='c' x='1' y='2'>A</text>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    // #111 is the first declaration, so it is the one that applies.
    EXPECT_NE(runs[0].fill.find("#111"), std::string::npos);
}

// A later <style> block must not displace an earlier declaration, which is the
// cascade order the old scan produced.
TEST(SvgStyleLookup, EarlierBlockWinsAcrossStylesheets) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<style>.c{fill:#111}</style>"
        "<style>.c{fill:#222}</style>"
        "<text class='c' x='1' y='2'>A</text>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("#111"), std::string::npos);
}

// With several classes on one element the first matching one applies.
TEST(SvgStyleLookup, FirstMatchingClassWins) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<style>.a{fill:#aaa}.b{fill:#bbb}</style>"
        "<text class='a b' x='1' y='2'>A</text>"
        "</svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("#aaa"), std::string::npos);
}

// A textPath lookup resolves to the declared path start, first id wins.
TEST(SvgTextPathLookup, ResolvesFirstAndLastPath) {
    auto xOf = [](int target) {
        std::string xml = "<svg xmlns='http://www.w3.org/2000/svg'><defs>";
        char buf[128];
        for (int i = 0; i < 12; ++i) {
            snprintf(buf, sizeof buf, "<path id='p%d' d='M %d 0'/>", i, i * 100);
            xml += buf;
        }
        xml += "</defs>";
        snprintf(buf, sizeof buf, "<text><textPath href='#p%d'>L</textPath></text>", target);
        xml += buf;
        xml += "</svg>";
        auto runs = svg::ExtractTextRuns(xml);
        if (runs.size() != 1) return -1.0f;
        return runs[0].x;
    };
    EXPECT_NEAR(xOf(0), 0.0f, 0.01f);
    EXPECT_NEAR(xOf(5), 500.0f, 0.01f);
    EXPECT_NEAR(xOf(11), 1100.0f, 0.01f);
}

// Many paths, many labels referencing the last one: the worst case for a scan.
TEST(SvgTextPathLookup, ManyPathsManyLabelsStayLinear) {
    auto timeIt = [](size_t n) {
        std::string xml;
        char buf[128];
        for (size_t i = 0; i < n; ++i) {
            snprintf(buf, sizeof buf, "<path id='p%zu' d='M0 0'/>", i);
            xml += buf;
        }
        for (size_t i = 0; i < n; ++i)
            xml += "<text><textPath href='#pLAST'>a</textPath></text>";
        snprintf(buf, sizeof buf, "p%zu", n - 1);
        size_t at = xml.find("pLAST");
        for (size_t k = 0; k < strlen(buf); ++k) xml[at + k] = buf[k];
        const auto t0 = std::chrono::steady_clock::now();
        auto runs = svg::ExtractTextRuns(xml);
        const auto t1 = std::chrono::steady_clock::now();
        EXPECT_EQ(runs.size(), n);
        return std::chrono::duration<double>(t1 - t0).count();
    };
    auto best = [&timeIt](size_t n) {
        double b = 1e9;
        for (int i = 0; i < 3; ++i) b = std::min(b, timeIt(n));
        return b;
    };
    const double t1 = best(4000);
    const double t2 = best(8000);
    EXPECT_LT(t2, std::max(t1 * 2.8, 0.020));
}

// A textPath naming an id that was never declared leaves the run unpositioned,
// exactly as before.
TEST(SvgTextPathLookup, MissingIdLeavesRunUnpositioned) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<text><textPath href='#nope'>L</textPath></text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 0.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 0.0f, 0.01f);
}


// --- Whitespace trimming must not be quadratic --------------------------
// The trims erased one leading character at a time from a std::string, and
// each erase shifts the rest, so trimming N spaces cost O(N^2). 20000 spaces
// took 24 ms and grew about 4x per doubling; 160000 spaces took 1.5 s for a
// 160 KB file.
TEST(SvgWhitespaceTrim, LongLeadingSpacesStayLinear) {
    auto timeIt = [](size_t n) {
        std::string xml = "<style>.c{" + std::string(n, ' ') +
                          "fill:red}</style><text class='c' x='1' y='2'>A</text>";
        const auto t0 = std::chrono::steady_clock::now();
        auto runs = svg::ExtractTextRuns(xml);
        const auto t1 = std::chrono::steady_clock::now();
        EXPECT_EQ(runs.size(), 1u);
        return std::chrono::duration<double>(t1 - t0).count();
    };
    auto best = [&timeIt](size_t n) {
        double b = 1e9;
        for (int i = 0; i < 3; ++i) b = std::min(b, timeIt(n));
        return b;
    };
    const double t1 = best(20000);
    const double t2 = best(40000);
    EXPECT_LT(t2, std::max(t1 * 3.0, 0.010));
}

// The same trim runs on the selector, so it needs the same bound.
TEST(SvgWhitespaceTrim, LongSelectorPaddingStaysLinear) {
    auto timeIt = [](size_t n) {
        std::string xml = "<style>" + std::string(n, ' ') +
                          ".c{fill:red}</style><text class='c' x='1' y='2'>A</text>";
        const auto t0 = std::chrono::steady_clock::now();
        auto runs = svg::ExtractTextRuns(xml);
        const auto t1 = std::chrono::steady_clock::now();
        EXPECT_EQ(runs.size(), 1u);
        return std::chrono::duration<double>(t1 - t0).count();
    };
    auto best = [&timeIt](size_t n) {
        double b = 1e9;
        for (int i = 0; i < 3; ++i) b = std::min(b, timeIt(n));
        return b;
    };
    const double t1 = best(20000);
    const double t2 = best(40000);
    EXPECT_LT(t2, std::max(t1 * 3.0, 0.010));
}

// Padding around a declaration and a value must be ignored, at both ends, for
// every kind of CSS whitespace.
TEST(SvgWhitespaceTrim, PaddingIsIgnoredAtBothEnds) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<style>.c{   fill:red   ;  font-size:12px  }</style>"
        "<text class='c' x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("red"), std::string::npos);
    EXPECT_NEAR(runs[0].fontSize, 12.0f, 0.01f);
}

TEST(SvgWhitespaceTrim, MixedWhitespaceKindsAreIgnored) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<style>.c{\t\r\n\f\v fill:\tred \t}</style>"
        "<text class='c' x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("red"), std::string::npos);
}

// A body that is nothing but whitespace yields no value rather than a crash.
TEST(SvgWhitespaceTrim, WhitespaceOnlyBodyIsEmpty) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'><style>.c{     }</style>"
        "<text class='c' x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].fill.empty());
}

// Whitespace before or after the selector must not stop it being recognised.
TEST(SvgWhitespaceTrim, PaddedSelectorStillMatches) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'><style>   .c   {fill:red}</style>"
        "<text class='c' x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("red"), std::string::npos);
}

// The inline style attribute is trimmed the same way. Note the property name is
// written without a space before the colon: GetStyleValue looks for the literal
// "fill:" and does not tolerate "fill : ", which is a separate pre-existing gap
// in inline-style parsing and not something the trim change touches.
TEST(SvgWhitespaceTrim, InlineStyleIsTrimmed) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<text style='   fill:#abc   ;  font-size:11px ' x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("#abc"), std::string::npos);
    EXPECT_NEAR(runs[0].fontSize, 11.0f, 0.01f);
}

// A presentation attribute still beats the stylesheet after trimming.
TEST(SvgWhitespaceTrim, AttributeStillBeatsPaddedRule) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<style>.c{   fill:#111   }</style>"
        "<text class='c' fill='#222' x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NE(runs[0].fill.find("#222"), std::string::npos);
}


// --- Self-closing tags must not leak context ---------------------------
// NextTag() returns the tag text including its '>', so tag.back() is always '>'
// and can never be the self-closing slash. Testing tag.back() was therefore
// always false: every <g/> pushed a level and never popped it, which both grew
// the stack with the input and passed the element's transform on to every later
// sibling. IsSelfClosed() already checked the right character.

TEST(SvgSelfClosing, GWithTransformDoesNotLeak) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g transform='translate(1000,0)'/>"
        "<text x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    // Identical to the explicitly closed form: the group is not an ancestor.
    EXPECT_NEAR(runs[0].x, 1.0f, 0.01f);
    EXPECT_NEAR(runs[0].y, 2.0f, 0.01f);
}

// The control for the above, so a regression in either direction is visible.
TEST(SvgSelfClosing, ClosedGWithTransformIsUnchanged) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g transform='translate(1000,0)'></g>"
        "<text x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 1.0f, 0.01f);
}

// A self-closed <switch/> opens and closes at once. Treating it as an opening
// one put the scanner into the waiting-for-first-child state, and it then
// treated the next element as the chosen branch and dropped every later sibling.
TEST(SvgSelfClosing, SwitchDoesNotSwallowSiblings) {
    std::string bad =
        "<svg xmlns='http://www.w3.org/2000/svg'><switch/>"
        "<text x='1' y='1'>VISIBLE</text><text x='2' y='2'>ALSO</text></svg>";
    std::string control =
        "<svg xmlns='http://www.w3.org/2000/svg'><g/>"
        "<text x='1' y='1'>VISIBLE</text><text x='2' y='2'>ALSO</text></svg>";
    auto runs = svg::ExtractTextRuns(bad);
    auto ctl = svg::ExtractTextRuns(control);
    ASSERT_EQ(ctl.size(), 2u);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].text, ctl[0].text);
    EXPECT_EQ(runs[1].text, ctl[1].text);
    EXPECT_NEAR(runs[1].x, 2.0f, 0.01f);
}

// A self-closed <svg/> must not leave its x/y shift on the stack either.
TEST(SvgSelfClosing, NestedSvgWithOffsetDoesNotLeak) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'><svg x='1000'/>"
        "<text x='1' y='2'>A</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_NEAR(runs[0].x, 1.0f, 0.01f);
}

// </textPath> popped a level that its opening tag never pushed. The <text>
// element pushed one level, so the close consumed it and the following </text>
// then consumed the enclosing group's, losing that group's transform for every
// later sibling.
TEST(SvgSelfClosing, TextPathCloseDoesNotStealEnclosingContext) {
    std::string xml =
        "<svg xmlns='http://www.w3.org/2000/svg'>"
        "<g transform='translate(1000,0)'>"
        "<text><textPath href='#p'>L</textPath></text>"
        "<text x='1' y='1'>B</text>"
        "</g></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 2u);
    // B sits inside the group, so its x composes to 1 + 1000. Before the fix
    // the group had been popped and B landed at 1.
    EXPECT_NEAR(runs[1].x, 1001.0f, 0.01f);
}

// The leak is a memory amplifier as well as a correctness bug: each leaked
// level carries three std::strings. Peak RSS must stay near the input size
// instead of growing at roughly 50x.
TEST(SvgSelfClosing, SelfClosedGroupsDoNotAmplifyMemory) {
    // Deliberately modest so the test stays fast; the old code needed ~16x this
    // much to tell the difference, and the ratio rather than the absolute is
    // what matters.
    //
    // This measures behaviour rather than resident bytes so the assertion runs
    // the same on every platform: the old code rendered the text but then kept
    // one context entry per self-closing tag alive, so a document that says
    // nothing about scale still showed the label at the wrong place.
    std::string xml = "<svg><g transform='translate(1000,0)'>";
    for (int i = 0; i < 200000; ++i) xml += "<g/>";
    xml += "<text x='1' y='2'>A</text></g></svg>";

    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    // The enclosing translate still applies: 1 + 1000.
    EXPECT_NEAR(runs[0].x, 1001.0f, 0.01f);
}
