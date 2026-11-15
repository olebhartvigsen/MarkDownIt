// The welcome screen, shared by the Windows and the macOS app.
//
// The cards are laid out, fitted and drawn here, once, through the
// RenderTarget and TextEngine seams, so the two apps cannot drift. Each
// platform only supplies the target and the engine.

#include "welcome_screen.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "macos/svg_mac.h"
#include "textenc.h"

// A Windows header drags DrawText in as a macro, which would rename the
// seam's method at every call below.
#ifdef DrawText
#undef DrawText
#endif

namespace platform {
namespace {

// The size of a card and the space around them.
//
// The numbers are the ones the Windows build uses.
// A card never gets narrower than this; the grid takes as many columns as
// the content column can hold and shares the width out evenly, so the last
// card ends at the column's right edge.
const float kCardW = 280.0f;
const float kCardH = 180.0f;
// The room inside a card, left and right.
const float kPad = 16.0f;
// The corner of a card: the site's radius.
const float kCardRadius = 14.0f;
const float kCardGap = 24.0f;
// The page has one content column, and everything on the page starts at
// its left edge: the brand block, the two actions, the label and the
// first card of the grid. A window wider than the column keeps the
// column centred and lets the ground run full width.
const float kSideMargin = 48.0f;
const float kContentMax = 980.0f;

// The band at the top borrows the site's hero, and the page under it is
// the site's ground: a dark teal gradient over a soft mint page. The band
// is a masthead and not a landing page. A start screen is read for the
// work it gets you back to, so the brand and the actions share one row and
// the recent files take the room under it (references/home-screen.md).
//
// The row is the lockup on the left and the two actions on the right. When
// the window is too narrow for both, the actions drop under the lockup and
// the band grows to hold them; when it is too narrow for the tagline to
// stand clear of the actions, the tagline stands down.
const float kBandHeight = 84.0f;
const float kBandHeightStacked = 122.0f;
// Below this column width the pair does not fit beside the lockup.
const float kInlineRowMin = 340.0f;
// Below this one the tagline would run under the pair.
const float kTaglineMin = 380.0f;

// The lockup: the mark, then the name beside it, then the tagline under the
// name. The mark is the icon's geometry, not a lookalike: a disc, the M and
// the arrow down, from the same 256-unit box the SVG uses.
const float kMarkSize = 34.0f;
const float kLockupGap = 12.0f;
const float kWordmarkSize = 19.0f;
const float kTaglineSize = 12.5f;

// The two actions, side by side on the band's right end. They carry a
// standard glyph and no words: New is the add sign, Open is a folder, and
// the words live in the tooltip each shell shows.
const float kButtonW = 34.0f;
const float kButtonH = 34.0f;
const float kButtonGap = 14.0f;
const float kButtonRadius = 9.0f;
// The band's rows, one set per case: one row on a wide window, two on a
// narrow one.
const float kRowMarkTop = 25.0f;
const float kRowWordmarkBaseline = 39.0f;
const float kRowTaglineBaseline = 55.0f;
const float kRowButtonTop = 25.0f;
const float kStackedMarkTop = 20.0f;
const float kStackedWordmarkBaseline = 34.0f;
const float kStackedTaglineBaseline = 50.0f;
const float kStackedButtonTop = 66.0f;

// The small name under each action button. The mark says what kind of action
// it is, the word says which one; the band still reads as one row because the
// label sits under the square, not beside it.
const float kButtonLabelSize = 10.5f;
const float kButtonLabelGap = 3.0f;

// The label over the grid, and the grid itself. The grid starts at
// kGridTop (the header names it, because the tests probe that line).
const float kSectionLabelSize = 12.5f;
// The label and the grid hang under the band, whichever height it has.
const float kLabelLift = 34.0f;
const float kGridLift = 50.0f;

// The distance between two rows of the preview.
const float kRowHeight = 16.0f;

// A diagram is read into memory to be shown on a card, and a runaway
// file should not take the app with it.
const size_t kMaxSvgBytes = 8u * 1024u * 1024u;

// The colors of the screen.
// The colors are the ones of the MarkDownIt site, a teal on cool white,
// so the app and the site read as one product.
//
// The page is the site's ground: the soft mint under the hero, and the
// site's white cards on it.
const Color kPageBackground{0.957f, 0.973f, 0.969f, 1.000f};
const Color kCardBackground{1.000f, 1.000f, 1.000f, 1.000f};
const Color kCardBorder{0.886f, 0.910f, 0.910f, 1.000f};
const Color kDivider{0.886f, 0.910f, 0.910f, 1.000f};
const Color kShadow{0.043f, 0.118f, 0.133f, 1.000f};
// The band, from the site's hero: the navy at the top, the deep teal at
// the bottom, with the mint of the announce bar for the quiet line.
const Color kBandTop{0.043f, 0.118f, 0.133f, 1.000f};
const Color kBandMid{0.071f, 0.227f, 0.204f, 1.000f};
const Color kBandBottom{0.059f, 0.369f, 0.341f, 1.000f};
const Color kBandText{1.000f, 1.000f, 1.000f, 1.000f};
const Color kBandMint{0.851f, 0.949f, 0.933f, 1.000f};
const Color kBandGhost{1.000f, 1.000f, 1.000f, 0.42f};
const Color kBandGhostFill{1.000f, 1.000f, 1.000f, 0.06f};
const Color kBandGhostHover{1.000f, 1.000f, 1.000f, 0.72f};
const Color kBandGhostHoverFill{1.000f, 1.000f, 1.000f, 0.14f};
// The one solid action is the site's solid button: bright teal with the
// dark ink on it.
const Color kSolidButton{0.078f, 0.722f, 0.651f, 1.000f};
const Color kSolidButtonHover{0.051f, 0.580f, 0.533f, 1.000f};
const Color kSolidInk{0.016f, 0.188f, 0.169f, 1.000f};
// The ink of the page under the band.
const Color kTitle{0.133f, 0.188f, 0.227f, 1.000f};
const Color kPreview{0.357f, 0.420f, 0.478f, 1.000f};
const Color kFolder{0.494f, 0.580f, 0.569f, 1.000f};
const Color kAccent{0.059f, 0.463f, 0.431f, 1.000f};
const Color kBrightTeal{0.078f, 0.722f, 0.651f, 1.0f};
const Color kHint{0.357f, 0.420f, 0.478f, 1.000f};
// The mark: the accent teal of the icon and the site, and the icon's white.
const Color kMarkDisc = Color{1.0f, 1.0f, 1.0f};
const Color kMarkInk = kAccent;

// The ellipsis that marks a string the card had to cut.
const char kEllipsis[] = "\xE2\x80\xA6";

// The name of the file, without the folders in front of it.
std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

// The folder the file is in.
std::string FolderName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return std::string();
    return path.substr(0, slash);
}

// Whether the path names a diagram, which the card shows as a picture.
bool EndsWithSvg(const std::string& path) {
    if (path.size() < 4) return false;
    std::string tail = path.substr(path.size() - 4);
    for (char& c : tail) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return tail == ".svg";
}

// The first lines of the file, which the card shows.
std::string PreviewText(const std::string& text) {
    std::string preview;
    size_t lines = 0;
    size_t at = 0;
    while (at < text.size() && lines < 6) {
        size_t eol = text.find('\n', at);
        const size_t end = (eol == std::string::npos ? text.size() : eol);
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        preview += line;
        preview += '\n';
        ++lines;
        if (eol == std::string::npos) break;
        at = eol + 1;
    }
    return preview;
}

// Whether a line of the preview is the separator of a table.
bool IsTableSeparator(const std::string& line) {
    if (line.empty() || line[0] != '|') return false;
    for (const char c : line) {
        if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t') {
            return false;
        }
    }
    return true;
}

// Whether a line of the preview is a heading.
bool IsHeading(const std::string& line) {
    size_t at = 0;
    while (at < line.size() && line[at] == ' ') ++at;
    if (at >= line.size() || line[at] != '#') return false;
    return true;
}

// The text of a preview line the way a reader sees it: a heading
// without its hashes, a bullet as a bullet, and a table row as cells
// divided by a bar. The line is raw markdown, and the marks are noise on
// a card.
std::string ReadableLine(const std::string& line) {
    size_t at = 0;
    while (at < line.size() && line[at] == ' ') ++at;
    if (at < line.size() && line[at] == '#') {
        size_t hashes = at;
        while (hashes < line.size() && line[hashes] == '#') ++hashes;
        while (hashes < line.size() && line[hashes] == ' ') ++hashes;
        return line.substr(hashes);
    }
    if (at + 1 < line.size() && (line[at] == '-' || line[at] == '*') &&
        line[at + 1] == ' ') {
        return std::string("\xE2\x80\xA2 ") + line.substr(at + 2);
    }
    if (at < line.size() && line[at] == '|') {
        std::string out;
        std::string cell;
        auto flush = [&]() {
            size_t b = 0;
            size_t e = cell.size();
            while (b < e && (cell[b] == ' ' || cell[b] == '\t')) ++b;
            while (e > b && (cell[e - 1] == ' ' || cell[e - 1] == '\t')) --e;
            if (e > b) {
                if (!out.empty()) out += "  \xE2\x94\x82  ";
                out += cell.substr(b, e - b);
            }
            cell.clear();
        };
        for (size_t i = at; i < line.size(); ++i) {
            if (line[i] == '|') flush();
            else cell += line[i];
        }
        flush();
        return out;
    }
    return line;
}

// The font a row of the preview is drawn with.
FontSpec PreviewFont(bool heading) {
    FontSpec font;
    font.family = "Segoe UI";
    font.size = heading ? 13.0f : 12.0f;
    font.bold = heading;
    return font;
}

// The width of a string, measured by the engine.
//
// Without an engine the estimate keeps the layout sane instead of
// collapsing every string to nothing; the app always passes one.
float TextWidth(TextEngine* engine, const std::string& utf8,
                const FontSpec& font) {
    if (utf8.empty()) return 0.0f;
    if (!engine) return static_cast<float>(utf8.size()) * font.size * 0.55f;
    GlyphRun run;
    run.text = utf8;
    run.font = font;
    const std::vector<TextLine> lines =
        engine->MeasureWrapped({run}, 1.0e9f, 0.0f);
    float width = 0.0f;
    for (const TextLine& line : lines) width = std::max(width, line.width);
    return width;
}

// The byte ends of the characters of a string, so a cut lands between
// two characters and never inside a UTF-8 sequence.
std::vector<size_t> CharacterEnds(const std::string& text) {
    std::vector<size_t> ends;
    for (size_t at = 0; at < text.size();) {
        size_t step = 1;
        const unsigned char c = static_cast<unsigned char>(text[at]);
        if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        at = std::min(text.size(), at + step);
        ends.push_back(at);
    }
    return ends;
}

// How much of a string fits the width, as a byte count. The search is
// over characters and measured, so it stops at the last one that fits.
size_t FittingPrefix(TextEngine* engine, const std::string& text,
                     const FontSpec& font, float maxWidth) {
    const std::vector<size_t> ends = CharacterEnds(text);
    if (ends.empty() || maxWidth <= 0.0f) return 0;
    size_t low = 0;
    size_t high = ends.size();
    while (low < high) {
        const size_t mid = (low + high + 1) / 2;
        if (TextWidth(engine, text.substr(0, ends[mid - 1]), font) <=
            maxWidth) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low == 0 ? 0 : ends[low - 1];
}

// Cuts a string until it fits, and marks the cut with an ellipsis.
//
// A file name or a folder deeper than the card has to end inside the
// card instead of running over the one next to it.
std::string Ellipsize(TextEngine* engine, const std::string& text,
                      const FontSpec& font, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) return std::string();
    if (TextWidth(engine, text, font) <= maxWidth) return text;

    // The ellipsis takes room of its own, so every candidate carries it
    // and the fitted string ends inside the width, not past it.
    const std::vector<size_t> ends = CharacterEnds(text);
    size_t low = 0;
    size_t high = ends.size();
    while (low < high) {
        const size_t mid = (low + high + 1) / 2;
        if (TextWidth(engine, text.substr(0, ends[mid - 1]) + kEllipsis,
                      font) <= maxWidth) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    if (low == 0) return kEllipsis;
    return text.substr(0, ends[low - 1]) + kEllipsis;
}

// Breaks one line of the preview into the rows that fit the width.
void WrapLine(TextEngine* engine, const std::string& line,
              const FontSpec& font, float maxWidth,
              std::vector<std::string>* rows) {
    std::string current;
    size_t at = 0;
    while (at < line.size()) {
        size_t space = line.find(' ', at);
        if (space == std::string::npos) space = line.size();
        std::string word = line.substr(at, space - at);
        at = space + 1;

        while (!word.empty()) {
            const std::string candidate =
                current.empty() ? word : current + " " + word;
            if (TextWidth(engine, candidate, font) <= maxWidth) {
                current = candidate;
                break;
            }
            if (!current.empty()) {
                // The word starts the row after this one.
                rows->push_back(current);
                current.clear();
                continue;
            }
            // One word wider than the card: take the part that fits and
            // carry the rest to the next row.
            const size_t take = FittingPrefix(engine, word, font, maxWidth);
            if (take == 0) break;
            rows->push_back(word.substr(0, take));
            word = word.substr(take);
        }
    }
    if (!current.empty()) rows->push_back(current);
}

// A soft shadow under a rounded rectangle. The seam has no blur, so the
// shadow is a few translucent layers that grow outward and fade, which
// reads as a diffuse shadow on both platforms.
void DrawShadow(RenderTarget* target, const Rect& rect, float radius,
                float lift, float strength) {
    const int layers = 6;
    for (int i = layers; i >= 1; --i) {
        const float grow = static_cast<float>(i) * (1.0f + lift * 0.25f);
        Rect layer;
        layer.x = rect.x - grow;
        layer.y = rect.y - grow + lift + 2.0f;
        layer.width = rect.width + 2.0f * grow;
        layer.height = rect.height + 2.0f * grow;
        Color color = kShadow;
        color.a = strength / static_cast<float>(layers) *
                  (1.0f - static_cast<float>(i - 1) /
                              static_cast<float>(layers + 1));
        target->FillRoundedRect(layer, radius + grow, color);
    }
}

// Where the content column starts and how wide it is. One column for the
// whole page, so the brand, the actions, the label and the grid share a
// left edge.
float ContentWidth(float viewW) {
    float w = viewW - 2.0f * kSideMargin;
    if (w > kContentMax) w = kContentMax;
    if (w < kCardW) w = kCardW;
    return w;
}

float ContentLeft(float viewW) {
    const float left = (viewW - ContentWidth(viewW)) * 0.5f;
    return left < 0.0f ? 0.0f : left;
}

// The ground under everything: the site's soft mint, with the pool of
// teal the site keeps at the top of its content. The band covers the
// part of it above kBandHeight, so the glow reads as light coming out
// from under the band.
void DrawPage(RenderTarget* target, float viewW, float viewH,
              float bandHeight) {
    Rect page;
    page.x = 0.0f;
    page.y = 0.0f;
    page.width = viewW;
    page.height = viewH;
    target->FillRect(page, kPageBackground);

    for (int i = 0; i < 8; ++i) {
        const float w = viewW * (1.12f - 0.105f * static_cast<float>(i));
        const float h = 330.0f - 34.0f * static_cast<float>(i);
        Rect glow;
        glow.x = (viewW - w) * 0.5f;
        glow.y = bandHeight - h * 0.62f;
        glow.width = w;
        glow.height = h;
        Color color = kAccent;
        color.a = 0.030f;
        target->FillEllipse(glow, color);
    }
}

// The band at the top: the site's hero, running from the navy at the top
// to the deep teal at the bottom. The seam fills shapes and does not
// interpolate a gradient, so the band is laid down in strips.
void DrawBand(RenderTarget* target, float viewW, float bandHeight) {
    const int strips = 40;
    const float step = bandHeight / static_cast<float>(strips);
    for (int i = 0; i < strips; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(strips - 1);
        Color color;
        if (t < 0.55f) {
            const float k = t / 0.55f;
            color.r = kBandTop.r + (kBandMid.r - kBandTop.r) * k;
            color.g = kBandTop.g + (kBandMid.g - kBandTop.g) * k;
            color.b = kBandTop.b + (kBandMid.b - kBandTop.b) * k;
        } else {
            const float k = (t - 0.55f) / 0.45f;
            color.r = kBandMid.r + (kBandBottom.r - kBandMid.r) * k;
            color.g = kBandMid.g + (kBandBottom.g - kBandMid.g) * k;
            color.b = kBandMid.b + (kBandBottom.b - kBandMid.b) * k;
        }
        color.a = 1.0f;
        Rect strip;
        strip.x = 0.0f;
        strip.y = static_cast<float>(i) * step;
        strip.width = viewW;
        strip.height = step + 1.0f;
        target->FillRect(strip, color);
    }
}

// The app mark, from the icon the site and the window carry: a teal
// disc, the white M and the arrow down beside it. The numbers are the
// icon's own 256-unit box, scaled here, so the page draws the real mark
// and not an approximation of it.
void DrawMark(RenderTarget* target, float x, float y, float size) {
    const float s = size / 256.0f;
    const auto px = [x, s](float v) { return x + v * s; };
    const auto py = [y, s](float v) { return y + v * s; };

    // The disc: the icon's circle is 240 of its 256 units wide.
    Rect disc;
    disc.x = px(8.0f);
    disc.y = py(8.0f);
    disc.width = 240.0f * s;
    disc.height = 240.0f * s;
    target->FillEllipse(disc, kMarkDisc);

    // The M, as the icon's path walks it: five down strokes and the two
    // valleys between them.
    const float letter[24] = {52.0f, 178.0f, 52.0f,  78.0f,  76.0f,  78.0f,
                              104.0f, 120.0f, 132.0f, 78.0f,  156.0f, 78.0f,
                              156.0f, 178.0f, 130.0f, 178.0f, 130.0f, 120.0f,
                              104.0f, 156.0f, 78.0f,  120.0f, 78.0f,  178.0f};
    std::vector<Point> m;
    m.reserve(12);
    for (int i = 0; i < 24; i += 2) m.push_back(Point{px(letter[i]), py(letter[i + 1])});
    target->FillPolygon(m, kMarkInk);

    // The arrow down: the shaft, then the chevron on its tip.
    target->StrokeLine(px(196.0f), py(86.0f), px(196.0f), py(144.0f),
                       kMarkInk, 16.0f * s);
    const std::vector<Point> chevron = {Point{px(172.0f), py(122.0f)},
                                        Point{px(196.0f), py(148.0f)},
                                        Point{px(220.0f), py(122.0f)}};
    target->StrokePolygon(chevron, kMarkInk, 16.0f * s, false);
}

// Fits the strings of a card to its width, once.
//
// The card width and the fonts are fixed, so a wrap never changes and
// the measuring does not have to run on every frame.
void FitCard(TextEngine* engine, WelcomeCard* card, float maxWidth,
             int maxRows) {
    card->fitted = true;

    FontSpec titleFont;
    titleFont.family = "Segoe UI";
    titleFont.size = 15.0f;
    titleFont.bold = true;
    card->titleShown = Ellipsize(engine, card->fileName, titleFont, maxWidth);

    FontSpec folderFont;
    folderFont.family = "Segoe UI";
    folderFont.size = 12.0f;
    card->folderShown = Ellipsize(engine, card->folder, folderFont, maxWidth);

    card->rows.clear();
    if (card->image || maxRows <= 0) return;

    size_t at = 0;
    while (at < card->preview.size() &&
           static_cast<int>(card->rows.size()) < maxRows) {
        size_t eol = card->preview.find('\n', at);
        if (eol == std::string::npos) eol = card->preview.size();
        const std::string line = card->preview.substr(at, eol - at);
        at = eol + 1;
        if (line.empty() || IsTableSeparator(line)) continue;

        const bool heading = IsHeading(line);
        std::vector<std::string> wrapped;
        WrapLine(engine, ReadableLine(line), PreviewFont(heading), maxWidth,
                 &wrapped);
        for (const std::string& text : wrapped) {
            if (static_cast<int>(card->rows.size()) >= maxRows) break;
            WelcomeCard::Row row;
            row.text = text;
            row.heading = heading;
            card->rows.push_back(row);
        }
    }

    // Mark the last row when the file has more to show than the card
    // has room for.
    if (at < card->preview.size() && !card->rows.empty()) {
        WelcomeCard::Row& last = card->rows.back();
        const FontSpec font = PreviewFont(last.heading);
        const std::vector<size_t> ends = CharacterEnds(last.text);
        size_t keep = ends.size();
        while (keep > 0 &&
               TextWidth(engine,
                         last.text.substr(0, ends[keep - 1]) + kEllipsis,
                         font) > maxWidth) {
            --keep;
        }
        last.text =
            last.text.substr(0, keep == 0 ? 0 : ends[keep - 1]) + kEllipsis;
    }
}

}  // namespace

namespace {

// The add sign in the New button: two strokes, square to each other.
void DrawPlusMark(RenderTarget* target, const Rect& button, const Color& color) {
    const float cx = button.x + button.width * 0.5f;
    const float cy = button.y + button.height * 0.5f;
    const float arm = 6.0f;
    target->StrokeLine(cx - arm, cy, cx + arm, cy, color, 2.0f);
    target->StrokeLine(cx, cy - arm, cx, cy + arm, color, 2.0f);
}

// The folder in the Open button: one outline with the tab on its left.
void DrawFolderMark(RenderTarget* target, const Rect& button,
                    const Color& color) {
    const float x = button.x + button.width * 0.5f;
    const float y = button.y + button.height * 0.5f;
    const std::vector<Point> folder = {Point{x - 9.0f, y - 4.0f},
                                       Point{x - 9.0f, y + 7.0f},
                                       Point{x + 9.0f, y + 7.0f},
                                       Point{x + 9.0f, y - 4.0f},
                                       Point{x - 1.0f, y - 4.0f},
                                       Point{x - 3.0f, y - 7.0f},
                                       Point{x - 9.0f, y - 7.0f}};
    target->StrokePolygon(folder, color, 1.7f, true);
}

}  // namespace

void WelcomeScreen::DrawBrand(RenderTarget* target, TextEngine* engine) const {
    DrawMark(target, nameX_ - kMarkSize - kLockupGap, markTop_, kMarkSize);

    FontSpec name;
    name.family = "Segoe UI";
    name.size = kWordmarkSize;
    name.bold = true;
    target->DrawText("MarkDownIt", name, nameX_, wordmarkBaseline_, kBandText);

    if (!taglineShown_) return;

    FontSpec line;
    line.family = "Segoe UI";
    line.size = kTaglineSize;
    target->DrawText("A word processor for Markdown files.", line, nameX_,
                     taglineBaseline_, kBandMint);
}

void WelcomeScreen::SetRecentFiles(const std::vector<RecentFile>& files) {
    cards_.clear();
    for (const RecentFile& file : files) {
        if (cards_.size() >= 12) break;
        WelcomeCard card;
        card.filePath = textenc::WideToUtf8(file.path);
        card.fileName = BaseName(card.filePath);
        card.folder = FolderName(card.filePath);
        card.preview = PreviewText(file.preview);

        // A diagram shows itself. The first bytes of an SVG are its XML
        // header, which reads as noise on a card.
        if (EndsWithSvg(card.filePath)) {
            // The wide path opens the file on every platform. A UTF-8 string
            // is not a path on Windows.
            std::ifstream input(std::filesystem::path(file.path),
                                std::ios::binary);
            if (input) {
                std::string xml;
                char buffer[8192];
                while (xml.size() < kMaxSvgBytes) {
                    input.read(buffer, sizeof(buffer));
                    const std::streamsize got = input.gcount();
                    if (got <= 0) break;
                    xml.append(buffer, static_cast<size_t>(got));
                }
                std::shared_ptr<SvgDoc> doc = std::make_shared<SvgDoc>();
                if (doc->Load(xml)) {
                    card.image = doc;
                    card.preview.clear();
                }
            }
        }
        cards_.push_back(card);
    }
}

void WelcomeScreen::Layout(float viewW, float viewH) {
    // The band is one row when the pair fits beside the lockup, and two
    // when it does not: the lockup keeps the left edge either way, and the
    // actions sit at the band's right end or under it.
    const float left = ContentLeft(viewW);
    const float contentW = ContentWidth(viewW);
    stacked_ = contentW < kInlineRowMin;
    taglineShown_ = stacked_ || contentW >= kTaglineMin;
    bandHeight_ = stacked_ ? kBandHeightStacked : kBandHeight;

    const float markTop = stacked_ ? kStackedMarkTop : kRowMarkTop;
    markTop_ = markTop;
    wordmarkBaseline_ =
        stacked_ ? kStackedWordmarkBaseline : kRowWordmarkBaseline;
    taglineBaseline_ = stacked_ ? kStackedTaglineBaseline : kRowTaglineBaseline;
    nameX_ = left + kMarkSize + kLockupGap;

    // The two actions: start a document, or open one. Reading is what the
    // app is for, so Open is the solid one, and on one row it is the
    // rightmost of the pair.
    const float buttonTop = stacked_ ? kStackedButtonTop : kRowButtonTop;
    newButton_.width = kButtonW;
    newButton_.height = kButtonH;
    newButton_.y = buttonTop;
    openButton_.width = kButtonW;
    openButton_.height = kButtonH;
    openButton_.y = buttonTop;
    if (stacked_) {
        newButton_.x = left;
        openButton_.x = left + kButtonW + kButtonGap;
    } else {
        openButton_.x = left + contentW - kButtonW;
        newButton_.x = openButton_.x - kButtonGap - kButtonW;
    }
    // The small names under the two squares, centred on them at draw time.
    const float labelTop = buttonTop + kButtonH + kButtonLabelGap;
    newLabel_.x = newButton_.x;
    newLabel_.y = labelTop;
    newLabel_.width = kButtonW;
    newLabel_.height = kButtonLabelSize + 2.0f;
    openLabel_.x = openButton_.x;
    openLabel_.y = labelTop;
    openLabel_.width = kButtonW;
    openLabel_.height = kButtonLabelSize + 2.0f;
    gridLeft_ = left;

    if (cards_.empty()) return;

    // The number of columns comes from the width of the column, and the
    // grid hangs from its left edge, so the brand, the actions, the label
    // and the first card share one line down the page. The cards keep one
    // size, so two previews are read the same way.
    int columns = static_cast<int>((contentW + kCardGap) / (kCardW + kCardGap));
    if (columns < 1) columns = 1;
    const float cardW =
        (contentW - static_cast<float>(columns - 1) * kCardGap) /
        static_cast<float>(columns);

    for (size_t i = 0; i < cards_.size(); ++i) {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        cards_[i].rect.x = left + static_cast<float>(column) * (cardW + kCardGap);
        cards_[i].rect.y =
            GridTop() + static_cast<float>(row) * (kCardH + kCardGap);
        // A card whose width changed is fitted again: the preview wraps to
        // the card it is drawn in, not to the card it had.
        if (cards_[i].rect.width != cardW) cards_[i].fitted = false;
        cards_[i].rect.width = cardW;
        cards_[i].rect.height = kCardH;
    }
    (void)viewH;
}

void WelcomeScreen::Render(RenderTarget* target, TextEngine* engine,
                           float viewW, float viewH, int hoverIndex,
                           bool openHovered, bool newHovered) {
    if (!target) return;

    DrawPage(target, viewW, viewH, bandHeight_);
    DrawBand(target, viewW, bandHeight_);

    // The lockup on the band, on the row Layout placed it.
    DrawBrand(target, engine);

    // The quiet action: the site's outline button on the band, so the row
    // still reads as two answers with one of them in front.
    {
        target->FillRoundedRect(newButton_, kButtonRadius,
                                newHovered ? kBandGhostHoverFill
                                           : kBandGhostFill);
        target->StrokeRoundedRect(newButton_, kButtonRadius,
                                  newHovered ? kBandGhostHover : kBandGhost,
                                  newHovered ? 1.6f : 1.2f);

        DrawPlusMark(target, newButton_, kBandText);
    }
    {
        FontSpec label;
        label.family = "Segoe UI";
        label.size = kButtonLabelSize;
        const float width = TextWidth(engine, "New", label);
        target->DrawText("New", label,
                         newLabel_.x + (newLabel_.width - width) * 0.5f,
                         newLabel_.y + kButtonLabelSize, kBandMint);
    }

    // The button that opens a document: the site's solid button, bright
    // teal with the dark ink on it, the one prominent action of the page.
    // A reader who lands here with nothing in the list still has a way in,
    // so it is drawn before the empty case returns.
    {
        target->FillRoundedRect(openButton_, kButtonRadius,
                                openHovered ? kSolidButtonHover : kSolidButton);

        DrawFolderMark(target, openButton_, kSolidInk);
    }
    {
        FontSpec label;
        label.family = "Segoe UI";
        label.size = kButtonLabelSize;
        const float width = TextWidth(engine, "Open", label);
        target->DrawText("Open", label,
                         openLabel_.x + (openLabel_.width - width) * 0.5f,
                         openLabel_.y + kButtonLabelSize, kBandMint);
    }

    // Without any files there is a hint under the label line, at the left
    // edge of the column, instead of the cards.
    if (cards_.empty()) {
        FontSpec font;
        font.family = "Segoe UI";
        font.size = 14.0f;
        target->DrawText("Open or drag a .md file to get started.", font,
                         ContentLeft(viewW), GridTop() + 26.0f,
                         kHint);
        return;
    }

    // The label over the grid. It is written from the left edge of the
    // first card, so the two read as one column.
    {
        FontSpec font;
        font.family = "Segoe UI";
        font.size = kSectionLabelSize;
        font.bold = true;
        target->DrawText("Recent files", font, gridLeft_,
                         bandHeight_ + kLabelLift,
                         kFolder);
    }

    for (size_t i = 0; i < cards_.size(); ++i) {
        WelcomeCard& card = cards_[i];
        const bool hovered = static_cast<int>(i) == hoverIndex;

        // The hovered card rises a little, with a deeper shadow. The
        // hit area stays where the card rests.
        Rect rect = card.rect;
        if (hovered) rect.y -= 2.0f;

        DrawShadow(target, rect, kCardRadius, hovered ? 4.0f : 0.0f,
                   hovered ? 0.20f : 0.12f);
        target->FillRoundedRect(rect, kCardRadius, kCardBackground);
        target->StrokeRoundedRect(rect, kCardRadius,
                                  hovered ? kBrightTeal : kCardBorder,
                                  hovered ? 1.5f : 1.0f);

        // The text of the card, fitted to the width of it.
        const float textX = rect.x + kPad;
        const float textWidth = rect.width - 2.0f * kPad;
        const float previewY = rect.y + 80.0f;
        const float previewBottom = rect.y + rect.height - 8.0f;
        const int maxRows = static_cast<int>(
            (previewBottom - previewY) / kRowHeight);

        if (!card.fitted) FitCard(engine, &card, textWidth, maxRows);

        float textY = rect.y + 32.0f;
        FontSpec titleFont;
        titleFont.family = "Segoe UI";
        titleFont.size = 15.0f;
        titleFont.bold = true;
        target->DrawText(card.titleShown, titleFont, textX, textY, kTitle);

        // The folder the file is in.
        if (!card.folderShown.empty()) {
            textY += 20.0f;
            FontSpec folderFont;
            folderFont.family = "Segoe UI";
            folderFont.size = 12.0f;
            target->DrawText(card.folderShown, folderFont, textX, textY,
                             kFolder);
        }

        // A hairline between the name and the contents.
        target->StrokeLine(textX, rect.y + 60.0f, textX + textWidth,
                           rect.y + 60.0f, kDivider, 1.0f);

        // A diagram takes the room under the name.
        if (card.image) {
            card.image->Draw(target, engine, textX, previewY - 12.0f,
                             textWidth, previewBottom - previewY + 12.0f);
            continue;
        }

        // The first lines of the file, wrapped to the card, with the
        // headings a little heavier and the lines of a table separator
        // left out.
        float y = previewY;
        for (const WelcomeCard::Row& row : card.rows) {
            const FontSpec font = PreviewFont(row.heading);
            target->DrawText(row.text, font, textX, y,
                             row.heading ? kTitle : kPreview);
            y += kRowHeight;
        }
    }
}

namespace {

// Whether a point is on a rect.
bool Inside(const Rect& rect, float x, float y) {
    return rect.width > 0.0f && x >= rect.x && x <= rect.x + rect.width &&
           y >= rect.y && y <= rect.y + rect.height;
}

}  // namespace

bool WelcomeScreen::HitsNewButton(float x, float y) const {
    if (newButton_.width <= 0.0f) return false;
    // The word under the square is part of the button: a reader who aims at
    // "New" means the button, not the band.
    return Inside(newButton_, x, y) || Inside(newLabel_, x, y);
}

bool WelcomeScreen::HitsOpenButton(float x, float y) const {
    if (openButton_.width <= 0.0f) return false;
    return Inside(openButton_, x, y) || Inside(openLabel_, x, y);
}

int WelcomeScreen::HitTest(float x, float y) const {
    for (size_t i = 0; i < cards_.size(); ++i) {
        const Rect& rect = cards_[i].rect;
        if (x >= rect.x && x <= rect.x + rect.width && y >= rect.y &&
            y <= rect.y + rect.height) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const std::string& WelcomeScreen::GetPath(int index) const {
    static const std::string kEmpty;
    if (index < 0 || index >= static_cast<int>(cards_.size())) return kEmpty;
    return cards_[index].filePath;
}

Rect WelcomeScreen::CardRect(int index) const {
    if (index < 0 || index >= static_cast<int>(cards_.size())) return Rect{};
    return cards_[static_cast<size_t>(index)].rect;
}

}  // namespace platform
