// The welcome screen of the macOS app.
//
// The cards are laid out the way the Windows build lays them out, and
// the drawing goes through the render target, so the screen looks the
// same on the two platforms.

#include "welcome_mac.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "svg_mac.h"
#include "textenc.h"

namespace platform {
namespace {

// The size of a card and the space around them.
//
// The numbers are the ones the Windows build uses.
const float kCardW = 280.0f;
const float kCardH = 180.0f;
const float kCardGap = 24.0f;
// The heading, the button under it, and the top of the grid of cards.
const float kHeadingBaseline = 52.0f;
const float kButtonW = 176.0f;
const float kButtonH = 32.0f;
const float kButtonTop = 92.0f;
const float kSideMargin = 60.0f;

// The distance between two rows of the preview, and the width of the
// accent bar down the left of a card.
const float kRowHeight = 16.0f;
const float kBarWidth = 4.0f;

// A diagram is read into memory to be shown on a card, and a runaway
// file should not take the app with it.
const size_t kMaxSvgBytes = 8u * 1024u * 1024u;

// The colors of the screen.
const Color kBackground{0.968f, 0.968f, 0.973f, 1.0f};
const Color kCardBackground{1.0f, 1.0f, 1.0f, 1.0f};
const Color kCardBorder{0.878f, 0.878f, 0.894f, 1.0f};
const Color kHoverBorder{0.0f, 0.471f, 0.831f, 1.0f};
const Color kHoverBackground{0.941f, 0.969f, 1.0f, 1.0f};
const Color kTitle{0.122f, 0.122f, 0.125f, 1.0f};
const Color kPreview{0.380f, 0.380f, 0.380f, 1.0f};
const Color kFolder{0.541f, 0.541f, 0.541f, 1.0f};
const Color kAccent{0.0f, 0.471f, 0.831f, 1.0f};
const Color kHint{0.627f, 0.627f, 0.627f, 1.0f};

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
        WrapLine(engine, line, PreviewFont(heading), maxWidth, &wrapped);
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
            std::ifstream input(card.filePath, std::ios::binary);
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
    // The button belongs to the screen, so it is placed even when there
    // is nothing to show beside it yet.
    openButton_.x = (viewW - kButtonW) * 0.5f;
    if (openButton_.x < kSideMargin) openButton_.x = kSideMargin;
    openButton_.y = kButtonTop;
    openButton_.width = kButtonW;
    openButton_.height = kButtonH;

    if (cards_.empty()) return;

    // The number of columns comes from the width of the window.
    float available = viewW - 2.0f * kSideMargin;
    if (available < kCardW) available = kCardW;
    int columns = static_cast<int>(available / (kCardW + kCardGap));
    if (columns < 1) columns = 1;

    const float gridWidth =
        static_cast<float>(columns) * kCardW +
        static_cast<float>(columns - 1) * kCardGap;
    float startX = (viewW - gridWidth) * 0.5f;
    if (startX < kSideMargin) startX = kSideMargin;

    for (size_t i = 0; i < cards_.size(); ++i) {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        cards_[i].rect.x = startX + static_cast<float>(column) * (kCardW + kCardGap);
        cards_[i].rect.y = WelcomeScreen::kGridTop +
                           static_cast<float>(row) * (kCardH + kCardGap);
        cards_[i].rect.width = kCardW;
        cards_[i].rect.height = kCardH;
    }
    (void)viewH;
}

void WelcomeScreen::Render(RenderTarget* target, TextEngine* engine,
                           float viewW, float viewH, int hoverIndex,
                           bool openHovered) {
    if (!target) return;

    // The background of the screen.
    Rect background;
    background.width = viewW;
    background.height = viewH;
    target->FillRect(background, kBackground);

    // The heading.
    {
        FontSpec font;
        font.family = "Segoe UI";
        font.size = 26.0f;
        font.bold = true;
        const std::string heading = "Recent Documents";
        const float width = TextWidth(engine, heading, font);
        target->DrawText(heading, font, (viewW - width) * 0.5f,
                         kHeadingBaseline, kTitle);
    }

    // The button that opens a document, right under the heading. A
    // reader who lands here with nothing in the list still has a way
    // in, so it is drawn before the empty case returns.
    {
        const Color fill = openHovered ? kHoverBackground : kCardBackground;
        const Color border = openHovered ? kHoverBorder : kCardBorder;
        target->FillRoundedRect(openButton_, 7.0f, fill);
        target->StrokeRect(openButton_, border, 1.0f);

        FontSpec font;
        font.family = "Segoe UI";
        font.size = 14.0f;
        const std::string label = "Open Document\xE2\x80\xA6";
        const float width = TextWidth(engine, label, font);
        target->DrawText(label, font,
                         openButton_.x + (openButton_.width - width) * 0.5f,
                         openButton_.y + 21.0f, kTitle);
    }

    // Without any files there is a hint instead of the cards.
    if (cards_.empty()) {
        FontSpec font;
        font.family = "Segoe UI";
        font.size = 15.0f;
        const std::string hint = "Open or drag a .md file to get started.";
        const float width = TextWidth(engine, hint, font);
        target->DrawText(hint, font, (viewW - width) * 0.5f, viewH * 0.5f,
                         kHint);
        return;
    }

    for (size_t i = 0; i < cards_.size(); ++i) {
        WelcomeCard& card = cards_[i];
        const bool hovered = static_cast<int>(i) == hoverIndex;

        // The card itself.
        target->FillRoundedRect(card.rect, 8.0f,
                                hovered ? kHoverBackground : kCardBackground);
        target->StrokeRect(card.rect,
                           hovered ? kHoverBorder : kCardBorder,
                           hovered ? 2.0f : 1.0f);

        // The bar on the left of the card.
        Rect bar;
        bar.x = card.rect.x;
        bar.y = card.rect.y + 8.0f;
        bar.width = kBarWidth;
        bar.height = card.rect.height - 16.0f;
        target->FillRect(bar, kAccent);

        // The text of the card, fitted to the width of it.
        const float textX = card.rect.x + kBarWidth + 12.0f;
        const float textWidth = card.rect.width - kBarWidth - 24.0f;
        const float previewY = card.rect.y + 68.0f;
        const float previewBottom = card.rect.y + card.rect.height - 12.0f;
        const int maxRows = static_cast<int>(
            (previewBottom - previewY) / kRowHeight);

        if (!card.fitted) FitCard(engine, &card, textWidth, maxRows);

        float textY = card.rect.y + 26.0f;
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

        // A diagram takes the room under the name.
        if (card.image) {
            card.image->Draw(target, engine, textX, previewY, textWidth,
                             previewBottom - previewY);
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

bool WelcomeScreen::HitsOpenButton(float x, float y) const {
    if (openButton_.width <= 0.0f) return false;
    return x >= openButton_.x && x <= openButton_.x + openButton_.width &&
           y >= openButton_.y && y <= openButton_.y + openButton_.height;
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

}  // namespace platform
