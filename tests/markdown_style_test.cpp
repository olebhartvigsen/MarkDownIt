// The visual style profiles. Every test names its style explicitly, so
// the result does not depend on MARKDOWNIT_STYLE or on the test
// target's pinned default.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"
#include "layout/text_runs.h"
#include "markdown_style.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <variant>

TEST(MarkdownStyle, ParsesNamesIgnoringCaseAndSpaces) {
    MarkdownStyle s = MarkdownStyle::GitHub;
    EXPECT_TRUE(ParseMarkdownStyle("wikipedia", &s));
    EXPECT_TRUE(s == MarkdownStyle::Wikipedia);
    EXPECT_TRUE(ParseMarkdownStyle("  GitHub ", &s));
    EXPECT_TRUE(s == MarkdownStyle::GitHub);
    EXPECT_TRUE(ParseMarkdownStyle("WIKIPEDIA", &s));
    EXPECT_TRUE(s == MarkdownStyle::Wikipedia);
}

TEST(MarkdownStyle, UnknownNameLeavesTheValueAlone) {
    MarkdownStyle s = MarkdownStyle::GitHub;
    EXPECT_FALSE(ParseMarkdownStyle("", &s));
    EXPECT_FALSE(ParseMarkdownStyle("gitlab", &s));
    EXPECT_TRUE(s == MarkdownStyle::GitHub);
}

TEST(MarkdownStyle, DefaultIsApple) {
    EXPECT_TRUE(kDefaultMarkdownStyle == MarkdownStyle::Apple);
    MarkdownStyle s = MarkdownStyle::GitHub;
    EXPECT_TRUE(ParseMarkdownStyle(" Apple ", &s));
    EXPECT_TRUE(s == MarkdownStyle::Apple);
}

TEST(MarkdownStyle, GitHubProfileIsUnchanged) {
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::GitHub);
    EXPECT_NEAR(m.bodyFontSize, 12.0f, 0.001f);
    EXPECT_NEAR(m.headingSizes[1], 24.0f, 0.001f);
    EXPECT_NEAR(m.thematicBreakThickness, 4.0f, 0.001f);
    EXPECT_TRUE(m.linkUnderline);
    EXPECT_TRUE(m.tableZebra);
    EXPECT_FALSE(m.tableBodyFill);
    EXPECT_NEAR(m.codeBorderWidth, 0.0f, 0.001f);
    EXPECT_TRUE(m.headingFamily[1] == nullptr);
    const Palette p = BasePalette(MarkdownStyle::GitHub);
    EXPECT_NEAR(p.link.r, 9.0f / 255.0f, 0.001f);  // 0969DA
}

TEST(MarkdownStyle, WikipediaProfileFollowsVector) {
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Wikipedia);
    // 14 px body at line-height 1.6; h1 1.8em, h2 1.5em, h3 1.2em.
    EXPECT_NEAR(m.bodyFontSize * 96.0f / 72.0f, 14.0f, 0.001f);
    EXPECT_NEAR(m.bodyLineHeight, 1.6f, 0.001f);
    EXPECT_NEAR(m.headingSizes[1] / m.bodyFontSize, 1.8f, 0.001f);
    EXPECT_NEAR(m.headingSizes[2] / m.bodyFontSize, 1.5f, 0.001f);
    EXPECT_NEAR(m.headingSizes[3] / m.bodyFontSize, 1.2f, 0.001f);
    EXPECT_NEAR(m.headingSizes[4], m.bodyFontSize, 0.001f);
    // Serif regular for h1 and h2, bold sans for h3 to h6.
    EXPECT_EQ(std::string(m.headingFamily[1]), std::string("Georgia"));
    EXPECT_EQ(std::string(m.headingFamily[2]), std::string("Georgia"));
    EXPECT_FALSE(m.headingBold[1]);
    EXPECT_FALSE(m.headingBold[2]);
    for (int i = 3; i <= 6; ++i) EXPECT_TRUE(m.headingBold[i]);
    EXPECT_FALSE(m.linkUnderline);
    EXPECT_NEAR(m.thematicBreakThickness, 1.0f, 0.001f);
    EXPECT_FALSE(m.tableZebra);
    EXPECT_TRUE(m.tableBodyFill);
    EXPECT_TRUE(m.tableHeaderFill);
    const Palette p = BasePalette(MarkdownStyle::Wikipedia);
    EXPECT_NEAR(p.link.r, 0x33 / 255.0f, 0.001f);   // 3366cc
    EXPECT_NEAR(p.tableHeaderBg.r, 0xEA / 255.0f, 0.001f);
    EXPECT_NEAR(p.rule.r, 0xA2 / 255.0f, 0.001f);
}

namespace {
layout::DocumentLayout Lay(const Document& doc, const LayoutMetrics& m,
                           const Palette& pal) {
    static fake::FakeTextEngine engine;
    layout::LayoutInput in;
    in.doc = &doc; in.metrics = &m; in.palette = &pal;
    in.contentWidth = 600.0f; in.zoom = 1.0f; in.engine = &engine;
    return layout::LayoutDocument(in);
}

Node Heading(int level, const char32_t* text) {
    Node n;
    n.block = BlockKind::Heading;
    n.level = level;
    n.contentLength = 4;
    InlineBlock c;
    c.text = text;
    n.children.push_back(c);
    return n;
}
}  // namespace

TEST(MarkdownStyle, HeadingFacesAndRuleFollowTheStyle) {
    Document doc;
    doc.nodes.push_back(Heading(1, U"Title"));
    doc.nodes.push_back(Heading(3, U"Part"));
    for (MarkdownStyle style :
         {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia}) {
        const LayoutMetrics m = BaseMetrics(style);
        const Palette pal = BasePalette(style);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        const display::Text* h1 = nullptr;
        const display::Text* h3 = nullptr;
        for (const auto& item : laid.list.items) {
            if (auto* t = std::get_if<display::Text>(&item)) {
                (h1 ? h3 : h1) = t;
            }
        }
        ASSERT_TRUE(h1 != nullptr && h3 != nullptr);
        if (style == MarkdownStyle::GitHub) {
            EXPECT_EQ(h1->line.runs[0].font.family,
                      std::string("Segoe UI Semibold"));
            EXPECT_EQ(h3->line.runs[0].font.family,
                      std::string("Segoe UI Semibold"));
            EXPECT_FALSE(h3->line.runs[0].font.bold);
        } else {
            EXPECT_EQ(h1->line.runs[0].font.family, std::string("Georgia"));
            EXPECT_FALSE(h1->line.runs[0].font.bold);
            EXPECT_EQ(h3->line.runs[0].font.family, std::string("Segoe UI"));
            EXPECT_TRUE(h3->line.runs[0].font.bold);
        }
        // Only H1 and H2 carry a rule, in both styles.
        int rules = 0;
        for (const auto& item : laid.list.items)
            if (std::holds_alternative<display::FillRect>(item)) ++rules;
        EXPECT_EQ(rules, 1);
    }
}

TEST(MarkdownStyle, LinkUnderlineFollowsTheStyle) {
    Document doc;
    Node n;
    n.block = BlockKind::Paragraph;
    n.contentLength = 4;
    InlineBlock c;
    c.text = U"link";
    c.kind = InlineKind::Link;
    n.children.push_back(c);
    doc.nodes.push_back(n);
    for (MarkdownStyle style :
         {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia}) {
        const LayoutMetrics m = BaseMetrics(style);
        const Palette pal = BasePalette(style);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        const display::Text& t =
            std::get<display::Text>(laid.list.items[0]);
        EXPECT_EQ(t.line.runs[0].underline, style == MarkdownStyle::GitHub);
        EXPECT_NEAR(t.line.runs[0].r,
                    style == MarkdownStyle::GitHub ? 9.0f / 255.0f
                                                   : 0x33 / 255.0f,
                    0.001f);
    }
}

TEST(MarkdownStyle, ThematicBreakThicknessFollowsTheStyle) {
    Document doc;
    Node n;
    n.block = BlockKind::ThematicBreak;
    doc.nodes.push_back(n);
    for (MarkdownStyle style :
         {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia}) {
        const LayoutMetrics m = BaseMetrics(style);
        const Palette pal = BasePalette(style);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        const display::FillRect& hr =
            std::get<display::FillRect>(laid.list.items[0]);
        EXPECT_NEAR(hr.rect.height, m.thematicBreakThickness, 0.001f);
    }
}

TEST(MarkdownStyle, CodeCardGetsABorderOnlyWhereTheStyleAsksForOne) {
    Document doc;
    Node n;
    n.block = BlockKind::CodeBlock;
    n.raw = U"int x;";
    doc.nodes.push_back(n);
    for (MarkdownStyle style :
         {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia}) {
        const LayoutMetrics m = BaseMetrics(style);
        const Palette pal = BasePalette(style);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        int cards = 0;
        for (const auto& item : laid.list.items)
            if (std::holds_alternative<display::FillRoundedRect>(item))
                ++cards;
        EXPECT_EQ(cards, style == MarkdownStyle::GitHub ? 1 : 2);
    }
}

TEST(MarkdownStyle, WikitableCentersHeaderCellsWithoutAMark) {
    Node table;
    table.block = BlockKind::Table;
    table.aligns = {ColumnAlign::Default, ColumnAlign::Right};
    TableRow head, body;
    for (int c = 0; c < 2; ++c) {
        TableCell h, b;
        h.isHeader = true;
        head.cells.push_back(h);
        body.cells.push_back(b);
    }
    const LayoutMetrics gh = BaseMetrics(MarkdownStyle::GitHub);
    const LayoutMetrics wp = BaseMetrics(MarkdownStyle::Wikipedia);
    EXPECT_TRUE(layout::CellAlign(table, head, 0, gh) == ColumnAlign::Default);
    EXPECT_TRUE(layout::CellAlign(table, head, 0, wp) == ColumnAlign::Center);
    // An explicit mark always wins, and body cells keep the column mark.
    EXPECT_TRUE(layout::CellAlign(table, head, 1, wp) == ColumnAlign::Right);
    EXPECT_TRUE(layout::CellAlign(table, body, 0, wp) == ColumnAlign::Default);
}

namespace {
float Lin(float c) {
    return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float Luma(const ThemeColor& c) {
    return 0.2126f * Lin(c.r) + 0.7152f * Lin(c.g) + 0.0722f * Lin(c.b);
}
// WCAG 2.x contrast ratio, the measure the Manual of Style's
// accessibility page points to.
float Contrast(const ThemeColor& a, const ThemeColor& b) {
    const float la = Luma(a), lb = Luma(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}
}  // namespace

// MOS:Accessibility asks for text a reader can see, and for meaning
// that does not rest on colour alone. Normal text needs 4.5:1 (WCAG AA).
// The white paper is the one the Wikipedia style draws on.
TEST(MarkdownStyle, WikipediaTextMeetsWcagAaContrast) {
    const Palette p = BasePalette(MarkdownStyle::Wikipedia);
    const ThemeColor white = MakeColor(0xFFFFFF);
    EXPECT_TRUE(Contrast(p.textPrimary, white) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textMuted, white) >= 4.5f);
    EXPECT_TRUE(Contrast(p.heading, white) >= 4.5f);
    EXPECT_TRUE(Contrast(p.link, white) >= 4.5f);
    // Text on the washes it sits on: code, table header, striped row.
    EXPECT_TRUE(Contrast(p.textPrimary, p.codeBg) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textPrimary, p.tableHeaderBg) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textPrimary, p.tableRowAlt) >= 4.5f);
    EXPECT_TRUE(Contrast(p.link, p.tableRowAlt) >= 4.5f);
}

// A link that is not underlined must not rest on hue alone: it has to
// stand apart from the body ink by more than hue (WCAG 1.4.1 asks for
// 3:1 between link and surrounding text when there is no underline).
TEST(MarkdownStyle, UnderlinelessLinkStandsApartFromBodyText) {
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Wikipedia);
    const Palette p = BasePalette(MarkdownStyle::Wikipedia);
    EXPECT_FALSE(m.linkUnderline);
    EXPECT_TRUE(Contrast(p.link, p.textPrimary) >= 3.0f);
}

// MOS:Accessibility: table headers are marked as headers, not only
// shaded. The header row keeps its bold weight and its own wash.
TEST(MarkdownStyle, WikipediaTableHeaderIsMarkedBeyondColour) {
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Wikipedia);
    EXPECT_TRUE(m.tableHeaderFill);
    EXPECT_TRUE(m.tableHeaderCenter);
}

// ---- The Apple style ------------------------------------------------

TEST(AppleStyle, ProfileFollowsTheDocCScale) {
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Apple);
    EXPECT_NEAR(m.bodyFontSize * 96.0f / 72.0f, 17.0f, 0.001f);
    EXPECT_NEAR(m.headingSizes[1] * 96.0f / 72.0f, 40.0f, 0.001f);
    EXPECT_NEAR(m.headingSizes[2] * 96.0f / 72.0f, 28.0f, 0.001f);
    EXPECT_NEAR(m.bodyLineHeight, 1.47f, 0.001f);
    // A strict descending scale, so the hierarchy reads at a glance.
    for (int i = 1; i < 5; ++i)
        EXPECT_TRUE(m.headingSizes[i] > m.headingSizes[i + 1]);
    EXPECT_NEAR(m.headingRuleThickness, 1.0f, 0.001f);
    EXPECT_FALSE(m.linkUnderline);
    EXPECT_FALSE(m.tableVerticalLines);
    EXPECT_TRUE(m.calloutBoxes);
    EXPECT_TRUE(m.leadScale > 1.0f);
    const Palette p = BasePalette(MarkdownStyle::Apple);
    EXPECT_NEAR(p.link.b, 0xCC / 255.0f, 0.001f);   // 0066cc
}

TEST(AppleStyle, OtherStylesKeepTheirOwnSwitches) {
    for (MarkdownStyle s : {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia}) {
        const LayoutMetrics m = BaseMetrics(s);
        EXPECT_FALSE(m.calloutBoxes);
        EXPECT_NEAR(m.leadScale, 1.0f, 0.001f);
        EXPECT_TRUE(m.tableVerticalLines);
    }
}

namespace {
Node Quote(const char32_t* text) {
    Node n;
    n.block = BlockKind::BlockQuote;
    n.contentLength = 8;
    InlineBlock c;
    c.text = text;
    n.children.push_back(c);
    return n;
}
Node Para(const char32_t* text) {
    Node n;
    n.block = BlockKind::Paragraph;
    n.contentLength = 8;
    InlineBlock c;
    c.text = text;
    n.children.push_back(c);
    return n;
}
}  // namespace

TEST(AppleStyle, DetectsTheFourInformationBoxes) {
    struct { const char32_t* text; CalloutKind kind; } cases[] = {
        {U"Note: a", CalloutKind::Info},
        {U"info: a", CalloutKind::Info},
        {U"WARNING: a", CalloutKind::Warning},
        {U"Important: a", CalloutKind::Warning},
        {U"Danger: a", CalloutKind::Danger},
        {U"Error: a", CalloutKind::Danger},
        {U"Success: a", CalloutKind::Success},
        {U"Tip: a", CalloutKind::Success},
    };
    for (const auto& c : cases) {
        const layout::CalloutMatch hit = layout::DetectCallout(Quote(c.text));
        EXPECT_TRUE(hit.found);
        EXPECT_TRUE(hit.kind == c.kind);
    }
    // Not a label: no colon, a longer word, or empty.
    EXPECT_FALSE(layout::DetectCallout(Quote(U"Note a")).found);
    EXPECT_FALSE(layout::DetectCallout(Quote(U"Notes: a")).found);
    EXPECT_FALSE(layout::DetectCallout(Quote(U"")).found);
    EXPECT_FALSE(layout::DetectCallout(Quote(U"Plain quote")).found);
}

TEST(AppleStyle, InformationBoxIsATintedRoundedSurfaceWithABoldLabel) {
    Document doc;
    doc.nodes.push_back(Quote(U"Warning: mind the gap"));
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Apple);
    const Palette pal = BasePalette(MarkdownStyle::Apple);
    layout::DocumentLayout laid = Lay(doc, m, pal);
    const display::FillRoundedRect* box = nullptr;
    const display::Text* text = nullptr;
    for (const auto& item : laid.list.items) {
        if (auto* b = std::get_if<display::FillRoundedRect>(&item)) box = b;
        if (auto* x = std::get_if<display::Text>(&item)) text = x;
    }
    ASSERT_TRUE(box != nullptr && text != nullptr);
    EXPECT_NEAR(box->color.r, pal.calloutBg[1].r, 0.001f);
    EXPECT_NEAR(box->radius, m.calloutRadius, 0.001f);
    // The text sits inside the padding on every side.
    EXPECT_TRUE(text->x >= box->rect.x + m.calloutPadX - 0.01f);
    EXPECT_TRUE(text->top >= box->rect.y + m.calloutPadY - 0.01f);
    EXPECT_TRUE(text->top + text->line.height <=
                box->rect.y + box->rect.height - m.calloutPadY + 0.01f);
    // The label word is its own bold run in the kind's colour, and the
    // runs still spell the source text.
    ASSERT_TRUE(text->line.runs.size() >= 2);
    EXPECT_EQ(text->line.runs[0].text, std::string("Warning:"));
    EXPECT_TRUE(text->line.runs[0].font.bold);
    EXPECT_NEAR(text->line.runs[0].r, pal.calloutLabel[1].r, 0.001f);
    std::string all;
    for (const auto& r : text->line.runs) all += r.text;
    EXPECT_EQ(all, std::string("Warning: mind the gap"));
    EXPECT_FALSE(text->line.runs[1].font.bold);
}

TEST(AppleStyle, PlainQuoteKeepsItsBarAndOtherStylesDrawNoBox) {
    Document doc;
    doc.nodes.push_back(Quote(U"Note: kept as a plain quote"));
    for (MarkdownStyle s : {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia}) {
        const LayoutMetrics m = BaseMetrics(s);
        const Palette pal = BasePalette(s);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        int boxes = 0, bars = 0;
        for (const auto& item : laid.list.items) {
            if (std::holds_alternative<display::FillRoundedRect>(item)) ++boxes;
            if (std::holds_alternative<display::FillRect>(item)) ++bars;
        }
        EXPECT_EQ(boxes, 0);
        EXPECT_EQ(bars, 1);
    }
    // An Apple quote without a label word is a bar too.
    Document plain;
    plain.nodes.push_back(Quote(U"Just a quote"));
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Apple);
    const Palette pal = BasePalette(MarkdownStyle::Apple);
    layout::DocumentLayout laid = Lay(plain, m, pal);
    EXPECT_TRUE(std::holds_alternative<display::FillRect>(laid.list.items[0]));
}

TEST(AppleStyle, PaddingIsChargedToTheGapNotTheBlock) {
    Document doc;
    doc.nodes.push_back(Quote(U"Note: one line"));
    doc.nodes.push_back(Para(U"after"));
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Apple);
    const Palette pal = BasePalette(MarkdownStyle::Apple);
    layout::DocumentLayout laid = Lay(doc, m, pal);
    ASSERT_TRUE(laid.blocks.size() == 2);
    const auto& q = laid.blocks[0];
    const auto& p = laid.blocks[1];
    // Block height is the text only; the next block starts after the
    // bottom padding and the paragraph gap.
    EXPECT_NEAR(p.y, q.y + q.height + m.calloutPadY + m.paraGap, 0.01f);
}

TEST(AppleStyle, TheLineAfterTheTitleIsTheLeadDescription) {
    Document doc;
    doc.nodes.push_back(Heading(1, U"Title"));
    doc.nodes.push_back(Para(U"The description."));
    doc.nodes.push_back(Para(U"Body text."));
    for (MarkdownStyle s : {MarkdownStyle::GitHub, MarkdownStyle::Apple}) {
        const LayoutMetrics m = BaseMetrics(s);
        const Palette pal = BasePalette(s);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        std::vector<const display::Text*> texts;
        for (const auto& item : laid.list.items)
            if (auto* t = std::get_if<display::Text>(&item)) texts.push_back(t);
        ASSERT_TRUE(texts.size() == 3);
        const float lead = texts[1]->line.runs[0].font.size;
        const float body = texts[2]->line.runs[0].font.size;
        if (s == MarkdownStyle::Apple) {
            EXPECT_NEAR(lead, body * m.leadScale, 0.01f);
            EXPECT_NEAR(texts[1]->line.runs[0].r, pal.textMuted.r, 0.001f);
            EXPECT_NEAR(texts[2]->line.runs[0].r, pal.textPrimary.r, 0.001f);
        } else {
            EXPECT_NEAR(lead, body, 0.01f);
        }
    }
}

TEST(AppleStyle, HeadingsAreBoldTitleSemiboldRestAndAHairlineOnH1AndH2) {
    Document doc;
    doc.nodes.push_back(Heading(1, U"Title"));
    doc.nodes.push_back(Heading(2, U"Section"));
    const LayoutMetrics m = BaseMetrics(MarkdownStyle::Apple);
    const Palette pal = BasePalette(MarkdownStyle::Apple);
    layout::DocumentLayout laid = Lay(doc, m, pal);
    std::vector<const display::Text*> texts;
    int rules = 0;
    for (const auto& item : laid.list.items) {
        if (auto* t = std::get_if<display::Text>(&item)) texts.push_back(t);
        if (std::holds_alternative<display::FillRect>(item)) ++rules;
    }
    ASSERT_TRUE(texts.size() == 2);
    EXPECT_TRUE(texts[0]->line.runs[0].font.bold);
    EXPECT_EQ(texts[1]->line.runs[0].font.family,
              std::string("Segoe UI Semibold"));
    EXPECT_EQ(rules, 2);   // a hairline under each of H1 and H2
}

TEST(AppleStyle, TablesDrawRowHairlinesAndNoVerticals) {
    Node n;
    n.block = BlockKind::Table;
    for (int r = 0; r < 2; ++r) {
        TableRow row;
        for (int c = 0; c < 2; ++c) {
            TableCell cell;
            cell.text = r == 0 ? U"Head" : U"Cell";
            cell.isHeader = r == 0;
            row.cells.push_back(cell);
        }
        n.rows.push_back(row);
    }
    Document doc;
    doc.nodes.push_back(n);
    for (MarkdownStyle s : {MarkdownStyle::GitHub, MarkdownStyle::Apple}) {
        const LayoutMetrics m = BaseMetrics(s);
        const Palette pal = BasePalette(s);
        layout::DocumentLayout laid = Lay(doc, m, pal);
        int vertical = 0, horizontal = 0;
        for (const auto& item : laid.list.items) {
            auto* f = std::get_if<display::FillRect>(&item);
            if (!f) continue;
            if (f->rect.width <= 1.01f && f->rect.height > 2.0f) ++vertical;
            if (f->rect.height <= 1.01f && f->rect.width > 2.0f) ++horizontal;
        }
        if (s == MarkdownStyle::Apple) {
            EXPECT_EQ(vertical, 0);
            EXPECT_EQ(horizontal, 2);   // the bottom of each row
        } else {
            EXPECT_TRUE(vertical > 0);
            EXPECT_EQ(horizontal, 3);   // top plus the bottom of each row
        }
    }
}

TEST(AppleStyle, TextMeetsWcagAaContrast) {
    const Palette p = BasePalette(MarkdownStyle::Apple);
    const ThemeColor white = MakeColor(0xFFFFFF);
    EXPECT_TRUE(Contrast(p.textPrimary, white) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textMuted, white) >= 4.5f);
    EXPECT_TRUE(Contrast(p.link, white) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textPrimary, p.codeBg) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textPrimary, p.inlineCodeBg) >= 4.5f);
    EXPECT_TRUE(Contrast(p.textMuted, p.codeBg) >= 4.5f);
    for (int k = 0; k < 4; ++k) {
        EXPECT_TRUE(Contrast(p.calloutLabel[k], p.calloutBg[k]) >= 4.5f);
        EXPECT_TRUE(Contrast(p.textPrimary, p.calloutBg[k]) >= 4.5f);
        EXPECT_TRUE(Contrast(p.link, p.calloutBg[k]) >= 4.5f);
    }
}

// The kinds stay apart without colour: each box carries its label word,
// and the four tints differ from each other.
TEST(AppleStyle, TheFourKindsAreDistinct) {
    const Palette p = BasePalette(MarkdownStyle::Apple);
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) {
            const bool same = p.calloutBg[a].r == p.calloutBg[b].r &&
                              p.calloutBg[a].g == p.calloutBg[b].g &&
                              p.calloutBg[a].b == p.calloutBg[b].b;
            EXPECT_FALSE(same);
        }
}

// The inline code wash covers the term and nothing else: its side
// padding stays under half a space of the body face (a space is about
// 0.28 em), so it never reaches the neighbouring words or spaces.
TEST(MarkdownStyle, InlineCodeWashDoesNotReachTheNeighbours) {
    for (MarkdownStyle s : {MarkdownStyle::GitHub, MarkdownStyle::Wikipedia,
                            MarkdownStyle::Apple}) {
        const LayoutMetrics m = BaseMetrics(s);
        const float padPts = m.inlineCodePadX * m.codeFontSize;
        const float halfSpacePts = 0.14f * m.bodyFontSize;
        EXPECT_TRUE(padPts > 0.0f);
        EXPECT_TRUE(padPts <= halfSpacePts);
    }
}
