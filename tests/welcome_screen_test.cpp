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
        Color color;
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
    struct LineCall {
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        Color color;
        float width = 0.0f;
    };
    std::vector<LineCall> lines;

    void StrokeLine(float x0, float y0, float x1, float y1,
                     const Color& color, float lineWidth) override {
        lines.push_back(LineCall{x0, y0, x1, y1, color, lineWidth});
    }
    void DrawText(const std::string& utf8, const FontSpec& font, float x,
                   float baselineY, const Color& color) override {
        texts.push_back(TextCall{utf8, font, x, baselineY, color});
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
    struct PolygonCall {
        std::vector<platform::Point> points;
        Color color;
    };
    std::vector<PolygonCall> polygons;

    void FillPolygon(const std::vector<platform::Point>& points,
                      const Color& color) override {
        polygons.push_back(PolygonCall{points, color});
    }
    struct OutlineCall {
        std::vector<platform::Point> points;
        float width = 0.0f;
    };
    std::vector<OutlineCall> outlines;

    void StrokePolygon(const std::vector<platform::Point>& points,
                        const Color& color, float lineWidth,
                        bool closed) override {
        (void)color;
        (void)closed;
        outlines.push_back(OutlineCall{points, lineWidth});
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
            if (call.y >= cardTop + 70.0f) rows.push_back(call);
        }
        return rows;
    }
};

// The width of the column of text inside a card, and the top of the
// first one.
float CardTextWidth(const WelcomeScreen& screen) {
    return screen.CardRect(0).width - 32.0f;
}
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
        EXPECT_LE(engine.Width(row.text, row.font), CardTextWidth(screen));
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
        EXPECT_LE(engine.Width(row.text, row.font), CardTextWidth(screen));
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
    EXPECT_LE(engine.Width(title.text, title.font), CardTextWidth(screen));
    EXPECT_TRUE(EndsWithEllipsis(folder.text));
    EXPECT_LE(engine.Width(folder.text, folder.font), CardTextWidth(screen));
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
    EXPECT_GT(button.y, 8.0f);
    EXPECT_LT(button.y + button.height, screen.BandHeight());
    EXPECT_TRUE(screen.HitsOpenButton(button.x + button.width * 0.5f,
                                      button.y + button.height * 0.5f));
    EXPECT_FALSE(screen.HitsOpenButton(button.x - 6.0f, button.y - 6.0f));
    EXPECT_FALSE(screen.HitsOpenButton(button.x + button.width + 6.0f,
                                       button.y + 4.0f));
    // No card lies under the button.
    EXPECT_EQ(screen.HitTest(button.x + 4.0f, button.y + 4.0f), -1);
}

TEST(WelcomeScreen, TheLockupPutsTheNameBesideTheMark) {
    // The page opens on the site's dark band with the app's mark, its name
    // beside the mark and the line the site leads with under the name.
    WelcomeScreen screen;
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    const RecordingTarget::TextCall* name = nullptr;
    const RecordingTarget::TextCall* line = nullptr;
    for (const RecordingTarget::TextCall& call : target.texts) {
        if (call.text == "MarkDownIt") name = &call;
        if (call.text == "A word processor for Markdown files.") line = &call;
    }
    ASSERT_TRUE(name != nullptr);
    ASSERT_TRUE(line != nullptr);
    EXPECT_TRUE(name->font.bold);
    EXPECT_GT(name->font.size, line->font.size);

    // The mark is a disc at the column's left edge, the size of the
    // lockup, and the name starts to the right of it on its centre line.
    bool sawDisc = false;
    for (const RecordingTarget::FillCall& fill : target.fills) {
        const bool square = fill.rect.width == fill.rect.height;
        // The lockup's mark, in the left part of the band: the buttons are
        // squares too, and they are at the right end.
        if (square && fill.rect.width > 28.0f && fill.rect.width < 40.0f &&
            fill.rect.y < 30.0f && fill.rect.x < 200.0f) {
            sawDisc = true;
            // The disc is white on the dark band, and the M and the arrow
            // in it are the site's teal, so the mark keeps the icon's two
            // colours the other way round.
            EXPECT_NEAR(fill.color.r, 1.0f, 0.01f);
            EXPECT_NEAR(fill.color.g, 1.0f, 0.01f);
            EXPECT_NEAR(fill.color.b, 1.0f, 0.01f);
            // The icon's circle is inset in its 256-unit box, so the disc
            // starts a little inside the column's edge.
            EXPECT_GE(fill.rect.x, screen.GridLeft());
            EXPECT_LT(fill.rect.x - screen.GridLeft(), 2.0f);
            EXPECT_GT(name->x, fill.rect.x + fill.rect.width);
            // The lockup is one block: the name's caps start at the disc's
            // top, so the two lines share the band's middle with the mark.
            EXPECT_GT(name->y, fill.rect.y);
            EXPECT_LT(name->y, fill.rect.y + fill.rect.height);
        }
    }
    EXPECT_TRUE(sawDisc);
    ASSERT_EQ(target.polygons.size(), static_cast<size_t>(1));
    EXPECT_GT(target.polygons[0].color.g, 0.30f);
    EXPECT_LT(target.polygons[0].color.r, 0.15f);
    EXPECT_LT(target.polygons[0].color.r, target.polygons[0].color.g);

    // The tagline hangs under the name, from the same left edge.
    EXPECT_GT(line->y, name->y + 8.0f);
    EXPECT_NEAR(line->x, name->x, 0.01f);

    // The band is the site's dark ground, full width at the top, and both
    // lines are light on it.
    bool sawBand = false;
    for (const RecordingTarget::FillCall& fill : target.fills) {
        if (fill.rect.width == 900.0f && fill.rect.y == 0.0f &&
            fill.color.r < 0.10f && fill.color.g < 0.40f) {
            sawBand = true;
        }
    }
    EXPECT_TRUE(sawBand);
    EXPECT_GT(name->color.r, 0.9f);
    EXPECT_GT(name->color.g, 0.9f);
    EXPECT_GT(name->color.b, 0.9f);

    // The lockup and the actions are one row on the band, the pair to the
    // right of the text, and the grid starts under the band.
    EXPECT_LT(line->y, screen.BandHeight());
    EXPECT_GT(screen.NewButtonRect().x, name->x + 150.0f);
    EXPECT_LT(screen.NewButtonRect().y + screen.NewButtonRect().height,
              screen.BandHeight());
    EXPECT_GT(kCardTop, screen.BandHeight());
}

TEST(WelcomeScreen, TheActionsWearAGlyphAndNoWords) {
    // New is the add sign and Open is a folder, drawn from primitives so
    // both ports draw the same marks; the words live in the tooltip, which
    // is the shell's to show. Nothing is set inside the two squares.
    WelcomeScreen screen;
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    const Rect fresh = screen.NewButtonRect();
    const Rect open = screen.OpenButtonRect();
    EXPECT_NEAR(fresh.width, fresh.height, 0.01f);
    EXPECT_NEAR(open.width, open.height, 0.01f);

    // The add sign: two strokes inside the outline square, square to each
    // other, one flat and one upright.
    int plusStrokes = 0;
    for (const RecordingTarget::LineCall& line : target.lines) {
        const bool inside = line.x0 >= fresh.x && line.x0 <= fresh.x + fresh.width &&
                            line.y0 >= fresh.y && line.y0 <= fresh.y + fresh.height &&
                            line.x1 >= fresh.x && line.x1 <= fresh.x + fresh.width &&
                            line.y1 >= fresh.y && line.y1 <= fresh.y + fresh.height;
        if (!inside) continue;
        const bool flat = line.y0 == line.y1;
        const bool upright = line.x0 == line.x1;
        if (flat || upright) ++plusStrokes;
    }
    EXPECT_EQ(plusStrokes, 2);

    // The folder: an outline with seven corners, inside the solid button.
    const std::vector<platform::Point>* folder = nullptr;
    for (const RecordingTarget::OutlineCall& outline : target.outlines) {
        float minX = outline.points[0].x, maxX = minX;
        float minY = outline.points[0].y, maxY = minY;
        for (const platform::Point& point : outline.points) {
            if (point.x < minX) minX = point.x;
            if (point.x > maxX) maxX = point.x;
            if (point.y < minY) minY = point.y;
            if (point.y > maxY) maxY = point.y;
        }
        if (minX >= open.x - 0.5f && maxX <= open.x + open.width + 0.5f) {
            folder = &outline.points;
            EXPECT_GE(maxY - minY, 8.0f);
            EXPECT_GE(maxX - minX, 12.0f);
        }
    }
    ASSERT_TRUE(folder != nullptr);
    EXPECT_GE(folder->size(), static_cast<size_t>(7));

    // No words inside either square.
    for (const RecordingTarget::TextCall& call : target.texts) {
        const bool inNew = call.x >= fresh.x && call.x <= fresh.x + fresh.width &&
                           call.y >= fresh.y && call.y <= fresh.y + fresh.height;
        const bool inOpen = call.x >= open.x && call.x <= open.x + open.width &&
                            call.y >= open.y && call.y <= open.y + open.height;
        EXPECT_FALSE(inNew);
        EXPECT_FALSE(inOpen);
    }
}

TEST(WelcomeScreen, TheBandKeepsOneRowAndGivesWayWhenThereIsNoRoom) {
    // The band is one row while the pair fits beside the lockup. When the
    // window is too narrow for that, the actions drop under the lockup and
    // the band grows; when it is too narrow for the tagline to stand clear
    // of the pair, the tagline stands down so the row stays whole.
    WelcomeScreen screen;
    screen.Layout(900.0f, 700.0f);
    EXPECT_FALSE(screen.StacksHeader());
    EXPECT_TRUE(screen.ShowsTagline());
    EXPECT_NEAR(screen.BandHeight(), 84.0f, 0.01f);
    EXPECT_NEAR(screen.GridTop(), 134.0f, 0.01f);

    // A half-window keeps the row whole and the tagline with it.
    screen.Layout(700.0f, 700.0f);
    EXPECT_FALSE(screen.StacksHeader());
    EXPECT_TRUE(screen.ShowsTagline());
    EXPECT_NEAR(screen.BandHeight(), 84.0f, 0.01f);
    EXPECT_NEAR(screen.OpenButtonRect().x + screen.OpenButtonRect().width,
                700.0f - 48.0f, 0.01f);

    // Narrower still: the row holds, but the tagline would run under the
    // pair, so it stands down and the name carries the row.
    screen.Layout(440.0f, 700.0f);
    EXPECT_FALSE(screen.StacksHeader());
    EXPECT_FALSE(screen.ShowsTagline());
    EXPECT_NEAR(screen.BandHeight(), 84.0f, 0.01f);

    // A narrow window: the pair goes under the lockup and the band takes
    // the second row, so the grid starts lower.
    screen.Layout(420.0f, 700.0f);
    EXPECT_TRUE(screen.StacksHeader());
    EXPECT_TRUE(screen.ShowsTagline());
    EXPECT_NEAR(screen.BandHeight(), 122.0f, 0.01f);
    EXPECT_NEAR(screen.NewButtonRect().x, screen.GridLeft(), 0.01f);
    EXPECT_GT(screen.NewButtonRect().y, screen.BandHeight() * 0.45f);
    EXPECT_NEAR(screen.GridTop(), 172.0f, 0.01f);
}

TEST(WelcomeScreen, TheGridHangsFromTheColumnAndTakesItsColumnsFromTheWidth) {
    // One left edge for the whole page: the brand, the buttons, the label
    // and the first card. The number of columns follows the column width.
    WelcomeScreen screen;
    std::vector<RecentFile> files(4);
    files[0].path = Widen("/documents/one.md");
    files[1].path = Widen("/documents/two.md");
    files[2].path = Widen("/documents/three.md");
    files[3].path = Widen("/documents/four.md");
    screen.SetRecentFiles(files);

    screen.Layout(900.0f, 700.0f);
    const Rect first = screen.CardRect(0);
    const Rect second = screen.CardRect(1);
    const Rect third = screen.CardRect(2);
    EXPECT_NEAR(first.x, screen.GridLeft(), 0.01f);
    // The band's right end and the grid's right edge are the same line.
    const Rect open = screen.OpenButtonRect();
    EXPECT_NEAR(open.x + open.width, second.x + second.width, 0.01f);
    EXPECT_NEAR(second.x, first.x + first.width + 24.0f, 0.01f);
    EXPECT_NEAR(second.y, first.y, 0.01f);
    EXPECT_GT(third.y, first.y + first.height);

    // A wider window fits one more card in a row, and the column stays
    // whole and centred.
    screen.Layout(1400.0f, 700.0f);
    EXPECT_GT(screen.CardRect(2).y, screen.CardRect(1).y - 0.5f);
    EXPECT_NEAR(screen.CardRect(2).y, screen.CardRect(0).y, 0.01f);
    EXPECT_LT(screen.CardRect(0).x, 240.0f);
}

TEST(WelcomeScreen, TheGridSaysItHoldsTheRecentFiles) {
    // The cards are the recent files, and the page says so above the
    // first one, from the left edge of the grid.
    WelcomeScreen screen;
    std::vector<RecentFile> files(2);
    files[0].path = Widen("/documents/one.md");
    files[1].path = Widen("/documents/two.md");
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    GridEngine engine;
    RecordingTarget target;
    screen.Render(&target, &engine, 900.0f, 700.0f, -1, false);

    const RecordingTarget::TextCall* label = nullptr;
    for (const RecordingTarget::TextCall& call : target.texts) {
        if (call.text == "Recent files") label = &call;
    }
    ASSERT_TRUE(label != nullptr);
    EXPECT_NEAR(label->x, screen.GridLeft(), 0.01f);
    EXPECT_NEAR(label->x, screen.CardRect(0).x, 0.01f);
    EXPECT_LT(label->y, screen.CardRect(0).y);
    EXPECT_GT(label->y,
              screen.NewButtonRect().y + screen.NewButtonRect().height);
}

TEST(WelcomeScreen, TheScreenOffersTheNewDocumentButton) {
    // The two actions share the band's row with the lockup. The pair sits
    // at the band's right end, New to the left of Open, and neither button
    // answers for the other or for a card underneath.
    WelcomeScreen screen;
    screen.Layout(900.0f, 700.0f);

    const Rect fresh = screen.NewButtonRect();
    const Rect open = screen.OpenButtonRect();
    EXPECT_GT(fresh.width, 0.0f);
    EXPECT_GT(fresh.height, 0.0f);
    EXPECT_GT(fresh.y, 8.0f);
    EXPECT_LT(fresh.y + fresh.height, screen.BandHeight());
    EXPECT_TRUE(screen.HitsNewButton(fresh.x + fresh.width * 0.5f,
                                     fresh.y + fresh.height * 0.5f));
    EXPECT_LT(fresh.x, open.x);
    EXPECT_NEAR(fresh.y, open.y, 0.01f);
    // The pair ends at the column's right edge, which is the grid's.
    EXPECT_NEAR(open.x + open.width, 900.0f - 48.0f, 0.01f);
    EXPECT_NEAR(fresh.x + fresh.width + 14.0f, open.x, 0.01f);

    // The gap between them belongs to neither.
    EXPECT_FALSE(screen.HitsNewButton(open.x - 6.0f, open.y + 4.0f));
    EXPECT_FALSE(screen.HitsOpenButton(fresh.x + fresh.width + 6.0f,
                                       fresh.y + 4.0f));
    // A press on either action is not a press on a card.
    EXPECT_EQ(screen.HitTest(fresh.x + 4.0f, fresh.y + 4.0f), -1);
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

TEST(WelcomeScreen, ACardAnswersItsOwnRect) {
    // The grid keeps where each card sits, so the app can press one
    // without measuring the layout itself.
    WelcomeScreen screen;
    std::vector<RecentFile> files(2);
    files[0].path = Widen("/documents/first.md");
    files[0].preview = "# First\n";
    files[1].path = Widen("/documents/second.md");
    files[1].preview = "# Second\n";
    screen.SetRecentFiles(files);
    screen.Layout(900.0f, 700.0f);

    const Rect first = screen.CardRect(0);
    EXPECT_GT(first.width, 0.0f);
    EXPECT_GT(first.height, 0.0f);
    EXPECT_EQ(screen.HitTest(first.x + first.width * 0.5f,
                             first.y + first.height * 0.5f), 0);
    EXPECT_EQ(screen.CardRect(-1).width, 0.0f);
    EXPECT_EQ(screen.CardRect(9).width, 0.0f);
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
