#include "gtest_lite.h"
#include "svgtext.h"

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
