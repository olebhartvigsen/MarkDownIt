// Links inside a diagram: the scan, the hit test and the URL gate.
//
// All of it is shared code with no platform header, so the suite runs on
// both runners and pins the answer a click gives.

#include "gtest_lite.h"
#include "svglink.h"

#include <string>

namespace {

TEST(SvgLinks, FindsAnAnchorAroundAShape) {
    const std::string xml =
        "<svg><a href='https://example.com/docs'>"
        "<rect x='10' y='20' width='100' height='40'/>"
        "</a></svg>";
    const std::vector<svg::LinkBox> links = svg::CollectLinks(xml);
    ASSERT_EQ(links.size(), 1u);
    EXPECT_NEAR(links[0].x, 10.0f, 0.01f);
    EXPECT_NEAR(links[0].y, 20.0f, 0.01f);
    EXPECT_NEAR(links[0].width, 100.0f, 0.01f);
    EXPECT_NEAR(links[0].height, 40.0f, 0.01f);
    EXPECT_TRUE(links[0].href == "https://example.com/docs");
}

TEST(SvgLinks, ReadsTheOlderXlinkAttribute) {
    const std::string xml =
        "<a xlink:href='https://example.com/x'>"
        "<circle cx='50' cy='50' r='20'/></a>";
    const std::vector<svg::LinkBox> links = svg::CollectLinks(xml);
    ASSERT_EQ(links.size(), 1u);
    EXPECT_TRUE(links[0].href == "https://example.com/x");
    EXPECT_NEAR(links[0].x, 30.0f, 0.01f);
    EXPECT_NEAR(links[0].width, 40.0f, 0.01f);
}

TEST(SvgLinks, TakesTheUnionOfTheChildren) {
    // A label beside its box is one target, not two.
    const std::string xml =
        "<a href='https://example.com'>"
        "<rect x='0' y='0' width='40' height='20'/>"
        "<text x='60' y='15'>label</text>"
        "</a>";
    const std::vector<svg::LinkBox> links = svg::CollectLinks(xml);
    ASSERT_EQ(links.size(), 1u);
    EXPECT_NEAR(links[0].x, 0.0f, 0.01f);
    EXPECT_GT(links[0].x + links[0].width, 60.0f);
    EXPECT_NEAR(links[0].y, 0.0f, 0.01f);
}

TEST(SvgLinks, DecodesEntitiesInTheHref) {
    const std::string xml =
        "<a href='https://example.com/?a=1&amp;b=2'><rect width='5' height='5'/></a>";
    const std::vector<svg::LinkBox> links = svg::CollectLinks(xml);
    ASSERT_EQ(links.size(), 1u);
    EXPECT_TRUE(links[0].href == "https://example.com/?a=1&b=2");
}

TEST(SvgLinks, SkipsAnchorsWithNoShape) {
    const std::string xml = "<a href='https://example.com'></a><rect/>";
    EXPECT_EQ(svg::CollectLinks(xml).size(), 0u);
}

TEST(SvgLinks, DoesNotMistakeOtherElementsForAnchors) {
    // <animate> and <adapter> both start with "<a".
    const std::string xml =
        "<svg><animate href='https://example.com' attributeName='x'/>"
        "<rect width='4' height='4'/></svg>";
    EXPECT_EQ(svg::CollectLinks(xml).size(), 0u);
}

TEST(SvgLinks, HitTestPrefersTheLastDrawnBox) {
    // Two links overlap; the reader clicks what is on top, which is the
    // one painted last.
    const std::vector<svg::LinkBox> links = {
        {0.0f, 0.0f, 100.0f, 100.0f, "https://first"},
        {20.0f, 20.0f, 40.0f, 40.0f, "https://second"},
    };
    const svg::LinkBox* hit = svg::LinkAt(links, 30.0f, 30.0f);
    ASSERT_TRUE(hit != nullptr);
    EXPECT_TRUE(hit->href == "https://second");
    // Outside the top box but inside the first, the first answers.
    hit = svg::LinkAt(links, 90.0f, 90.0f);
    ASSERT_TRUE(hit != nullptr);
    EXPECT_TRUE(hit->href == "https://first");
    // And nothing outside both.
    EXPECT_TRUE(svg::LinkAt(links, 200.0f, 200.0f) == nullptr);
}

TEST(SvgLinks, OnlyWebAndMailSchemesAreOpenable) {
    EXPECT_TRUE(svg::IsOpenableUrl("https://example.com"));
    EXPECT_TRUE(svg::IsOpenableUrl("http://example.com"));
    EXPECT_TRUE(svg::IsOpenableUrl("HTTPS://EXAMPLE.COM"));
    EXPECT_TRUE(svg::IsOpenableUrl("mailto:someone@example.com"));
    // A diagram is untrusted input: nothing else reaches the shell.
    EXPECT_FALSE(svg::IsOpenableUrl("javascript:alert(1)"));
    EXPECT_FALSE(svg::IsOpenableUrl("data:text/html,<script>"));
    EXPECT_FALSE(svg::IsOpenableUrl("file:///etc/passwd"));
    EXPECT_FALSE(svg::IsOpenableUrl("relative/path.html"));
    EXPECT_FALSE(svg::IsOpenableUrl(""));
}

}  // namespace
