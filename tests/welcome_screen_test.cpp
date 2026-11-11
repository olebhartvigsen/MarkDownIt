// The welcome screen's cards, drawn against a fixed text engine.
//
// The screen shows the recent files as cards, and a card has to keep its
// text inside itself: the preview wraps to the width of the card, the
// name and the folder are cut with an ellipsis, and a card for a diagram
// shows the picture instead of the first bytes of the XML.

#include "gtest_lite.h"

#include "../src/platform/seams.h"
#include "../src/welcome_screen.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using platform::Color;
using platform::FontSpec;
using platform::GlyphRun;
using platform::Rect;
using platform::TextEngine;
using platform::TextLine;
using platform::WelcomeScreen;

namespace {

// A text engine on a fixed grid: a character is half an em wide. The
// card's arithmetic is what the tests check, so the engine stays simple
// and deterministic.
class GridEngine : public TextEngine {
public:
    std::vector<TextLine> MeasureWrapped(const std::vector<GlyphRun>& runs,
                                          float maxWidth,
                                          float lineSpacing) override {
        (void)lineSpacing;
        (void)maxWidth;
        std::string text;
        float size = 12.0f;
        for (const GlyphRun& run : runs) {
            text += run.text;
            size = run.font.size;
        }
        if (text.empty()) return {};

        TextLine line;
        line.width = static_cast<float>(text.size()) * size * 0.5f;
        line.height = size;
        line.ascent = size;
        line.runs = runs;
        return {line};
    }

    uint32_t HitTestOffset(const TextLine& line, float x,
                            bool* outTrailing) const override {
        (void)line;
        if (outTrailing) *outTrailing = false;
        return static_cast<uint32_t>(x);
    }

    Rect OffsetToRect(const TextLine& line, uint32_t offset) const override {
        (void)line;
        (void)offset;
        return Rect{};
    }

    float LineHeight(const FontSpec& font) const override {
        return font.size;
    }

    // The width the screen would measure for a string.
    float Width(const std::string& text, const FontSpec& font) {
        GlyphRun run;
        run.text = text;
        run.font = font;
        const std::vector<TextLine> lines =
            MeasureWrapped({run}, 1.0e9f, 0.0f);
        return lines.empty() ? 0.0f : lines[0].width;
    }
};

// A target that remembers what was drawn.
class RecordingTarget : public platform::RenderTarget {
public:
    struct TextCall {
        std::string text;
        FontSpec font;
        float x = 0.0f;
        float y = 0.0f;
    };
    struct FillCall {
        Rect rect;
        Color color;
    };
    std::vector<TextCall> texts;
    std::vector<FillCall> fills;

    void FillRect(const Rect& rect, const Color& color) override {
        fills.push_back(FillCall{rect, color});
    }
    void FillRoundedRect(const Rect& rect, float radius,
                          const Color& color) override {
        (void)radius;
        fills.push_back(FillCall{rect, color});
    }
    void StrokeRect(const Rect& rect, const Color& color,
                     float lineWidth) override {
        (void)rect;
        (void)color;
        (void)lineWidth;
    }
    void StrokeRoundedRect(const Rect& rect, float radius,
                            const Color& color, float lineWidth) override {
        (void)rect;
        (void)radius;
        (void)color;
        (void)lineWidth;
    }
    void StrokeLine(float x0, float y0, float x1, float y1,
                     const Color& color, float lineWidth) override {
        (void)x0;
        (void)y0;
        (void)x1;
        (void)y1;
        (void)color;
        (void)lineWidth;
    }
    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                   float baselineY, const Color& color) override {
        (void)color;
        texts.push_back(TextCall{utf8, font, x, baselineY});
    }
    void DrawBitmap(platform::BitmapHandle image, const Rect& rect) override {
        (void)image;
        (void)rect;
    }
    void FillEllipse(const Rect& rect, const Color& color) override {
        fills.push_back(FillCall{rect, color});
    }
    void StrokeEllipse(const Rect& rect, const Color& color,
                        float lineWidth) override {
        (void)rect;
        (void)color;
        (void)lineWidth;
    }
    void FillPolygon(const std::vector<platform::Point>& points,
                      const Color& color) override {
        (void)points;
        (void)color;
    }
    void StrokePolygon(const std::vector<platform::Point>& points,
                        const Color& color, float lineWidth,
                        bool closed) override {
        (void)points;
        (void)color;
        (void)lineWidth;
        (void)closed;
    }
    void FillPieSlice(const Rect& rect, float startAngle, float endAngle,
                       const Color& color) override {
        (void)rect;
        (void)startAngle;
        (void)endAngle;
        (void)color;
    }
    void PushClip(const Rect& rect) override { (void)rect; }
    void PopClip() override {}

    // The texts drawn under the name of the card, which is the preview.
    std::vector<TextCall> PreviewRows(float cardTop) const {
        std::vector<TextCall> rows;
        for (const TextCall& call : texts) {
            if (call.y >= cardTop + 60.0f) rows.push_back(call);
        }
        return rows;
    }
};

// The width of the column of text inside a card, and the top of the
// first one.
const float kCardTextWidth = 280.0f - 4.0f - 24.0f;
const float kCardTop = WelcomeScreen::kGridTop;

std::wstring Widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

bool EndsWithEllipsis(const std::string& text) {
    const std::string ellipsis = "\xE2\x80\xA6";
    if (text.size() < ellipsis.size()) return false;
    return text.compare(text.size() - ellipsis.size(), ellipsis.size(),
                        ellipsis) == 0;
}

}  // namespace

TEST(WelcomeScreen, WrapsALongLineOfThePreview) {
    WelcomeScreen screen;
    std::vector<RecentFile> files(1);
    files[0].path = Widen("/documents/long.md");
    files[0].preview =
        "A line of prose long enough that it cannot fit inside one card "
        "and has to be broken into rows instead of running over the card "
        "next to it.\n";
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    const std::vector<RecordingTarget::TextCall> rows =
        target.PreviewRows(kCardTop);
    EXPECT_GE(rows.size(), static_cast<size_t>(3));

    // Every row fits the card, and the rows walk down it in order.
    for (const RecordingTarget::TextCall& row : rows) {
        EXPECT_LE(engine.Width(row.text, row.font), kCardTextWidth);
    }
    for (size_t i = 1; i < rows.size(); ++i) {
        EXPECT_GT(rows[i].y, rows[i - 1].y);
    }
    // The last row still sits above the bottom of the card.
    EXPECT_LE(rows.back().y, kCardTop + 180.0f - 12.0f);
}

TEST(WelcomeScreen, SplitsAWordWiderThanTheCard) {
    WelcomeScreen screen;
    std::vector<RecentFile> files(1);
    files[0].path = Widen("/documents/word.md");
    files[0].preview = std::string(200, 'x') + "\n";
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    const std::vector<RecordingTarget::TextCall> rows =
        target.PreviewRows(kCardTop);
    // One unbroken word still has to come out in rows that fit.
    EXPECT_GE(rows.size(), static_cast<size_t>(2));
    for (const RecordingTarget::TextCall& row : rows) {
        EXPECT_LE(engine.Width(row.text, row.font), kCardTextWidth);
    }
}

TEST(WelcomeScreen, CutsALongNameAndFolderInsideTheCard) {
    WelcomeScreen screen;
    std::vector<RecentFile> files(1);
    files[0].path = Widen(
        "/a/very/long/folder/name/that/goes/on/and/on/and/on/for/the/card/"
        "a-file-name-that-is-far-too-long-to-fit-on-one-line-of-the-card.md");
    files[0].preview = "text\n";
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    // The heading of the screen is drawn first, so the card's own texts
    // are picked by where they sit.
    std::vector<RecordingTarget::TextCall> cardTexts;
    for (const RecordingTarget::TextCall& call : target.texts) {
        if (call.y >= kCardTop) cardTexts.push_back(call);
    }
    ASSERT_GE(cardTexts.size(), static_cast<size_t>(2));
    const RecordingTarget::TextCall& title = cardTexts[0];
    const RecordingTarget::TextCall& folder = cardTexts[1];
    EXPECT_TRUE(EndsWithEllipsis(title.text));
    EXPECT_LE(engine.Width(title.text, title.font), kCardTextWidth);
    EXPECT_TRUE(EndsWithEllipsis(folder.text));
    EXPECT_LE(engine.Width(folder.text, folder.font), kCardTextWidth);
}

TEST(WelcomeScreen, ShowsADiagramAsAPicture) {
    // A diagram on a card shows itself; the first bytes of the file are
    // the XML header, which reads as noise.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "markdownit-welcome-card.svg";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\""
               " height=\"100\"><rect x=\"10\" y=\"10\" width=\"180\""
               " height=\"80\" fill=\"#ff0000\"/></svg>";
    }

    WelcomeScreen screen;
    std::vector<RecentFile> files(1);
    files[0].path = Widen(path.string());
    files[0].preview = "<svg xmlns=\"http://www.w3.org/2000/svg\" ...";
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    // No markup as text under the name.
    EXPECT_EQ(target.PreviewRows(kCardTop).size(), static_cast<size_t>(0));

    // The red box of the diagram was drawn inside the card.
    bool sawRed = false;
    for (const RecordingTarget::FillCall& fill : target.fills) {
        if (fill.color.r > 0.9f && fill.color.g < 0.1f &&
            fill.color.b < 0.1f) {
            sawRed = true;
            EXPECT_GE(fill.rect.y, kCardTop);
        }
    }
    EXPECT_TRUE(sawRed);

    std::filesystem::remove(path);
}

TEST(WelcomeScreen, TheScreenOffersTheOpenButton) {
    // The button sits under the heading, inside the window and above
    // the grid, and it is found on its own: a click on it opens the
    // panel instead of whatever card is underneath.
    WelcomeScreen screen;
    std::vector<RecentFile> files(2);
    files[0].path = Widen("/documents/one.md");
    files[1].path = Widen("/documents/two.md");
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    const Rect button = screen.OpenButtonRect();
    EXPECT_GT(button.width, 0.0f);
    EXPECT_GT(button.height, 0.0f);
    EXPECT_GT(button.y, 60.0f);
    EXPECT_LT(button.y + button.height, 700.0f);
    EXPECT_TRUE(screen.HitsOpenButton(button.x + button.width * 0.5f,
                                      button.y + button.height * 0.5f));
    EXPECT_FALSE(screen.HitsOpenButton(button.x - 6.0f, button.y - 6.0f));
    EXPECT_FALSE(screen.HitsOpenButton(button.x + button.width + 6.0f,
                                       button.y + 4.0f));
    // No card lies under the button.
    EXPECT_EQ(screen.HitTest(button.x + 4.0f, button.y + 4.0f), -1);
}

TEST(WelcomeScreen, TheEmptyScreenStillOffersTheOpenButton) {
    // With nothing in the list the screen is a hint, and the way in
    // has to stay on it.
    WelcomeScreen screen;
    EXPECT_EQ(screen.CardCount(), 0);
    screen.Layout(900.0f, 700.0f);

    const Rect button = screen.OpenButtonRect();
    EXPECT_GT(button.width, 0.0f);
    EXPECT_TRUE(screen.HitsOpenButton(button.x + 2.0f, button.y + 2.0f));
}

TEST(WelcomeScreen, ReadsMarkdownMarksAsTheReaderSeesThem) {
    // A card shows a heading without its hashes, a bullet as a bullet
    // and a table row as cells divided by a bar, the way the Windows card always did.
    WelcomeScreen screen;
    std::vector<RecentFile> files(1);
    files[0].path = Widen("/documents/marks.md");
    files[0].preview = "# Title\n- item\n| a | b |\n|---|---|\n";
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    const std::vector<RecordingTarget::TextCall> rows =
        target.PreviewRows(kCardTop);
    ASSERT_EQ(rows.size(), static_cast<size_t>(3));
    EXPECT_EQ(rows[0].text, std::string("Title"));
    EXPECT_EQ(rows[1].text, std::string("\xE2\x80\xA2 item"));
    EXPECT_EQ(rows[2].text, std::string("a \xE2\x94\x82 b"));
}
