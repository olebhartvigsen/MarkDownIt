// The welcome screen of the macOS app.
//
// The cards are laid out the way the Windows build lays them out, and
// the drawing goes through the render target, so the screen looks the
// same on the two platforms.

#include "welcome_mac.h"

#include <algorithm>
#include <string>
#include <vector>

#include "textenc.h"

namespace mac {
namespace {

// The size of a card and the space around them.
//
// The numbers are the ones the Windows build uses.
const float kCardW = 280.0f;
const float kCardH = 180.0f;
const float kCardGap = 24.0f;
const float kTopMargin = 80.0f;
const float kSideMargin = 60.0f;

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
        cards_.push_back(card);
    }
}

void WelcomeScreen::Layout(float viewW, float viewH) {
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
        cards_[i].rect.y = kTopMargin + static_cast<float>(row) * (kCardH + kCardGap);
        cards_[i].rect.width = kCardW;
        cards_[i].rect.height = kCardH;
    }
    (void)viewH;
}

void WelcomeScreen::Render(RenderTarget* target, float viewW, float viewH,
                            int hoverIndex) {
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
        const float width =
            static_cast<float>(heading.size()) * font.size * 0.55f;
        target->DrawText(heading, font, (viewW - width) * 0.5f, 52.0f, kTitle);
    }

    // Without any files there is a hint instead of the cards.
    if (cards_.empty()) {
        FontSpec font;
        font.family = "Segoe UI";
        font.size = 15.0f;
        const std::string hint = "Open or drag a .md file to get started.";
        const float width = static_cast<float>(hint.size()) * font.size * 0.5f;
        target->DrawText(hint, font, (viewW - width) * 0.5f,
                         viewH * 0.5f, kHint);
        return;
    }

    for (size_t i = 0; i < cards_.size(); ++i) {
        const WelcomeCard& card = cards_[i];
        const bool hovered = static_cast<int>(i) == hoverIndex;

        // The card itself.
        target->FillRoundedRect(card.rect, 8.0f,
                                hovered ? kHoverBackground : kCardBackground);
        target->StrokeRect(card.rect,
                           hovered ? kHoverBorder : kCardBorder,
                           hovered ? 2.0f : 1.0f);

        // The bar on the left of the card.
        const float barWidth = 4.0f;
        Rect bar;
        bar.x = card.rect.x;
        bar.y = card.rect.y + 8.0f;
        bar.width = barWidth;
        bar.height = card.rect.height - 16.0f;
        target->FillRect(bar, kAccent);

        // The name of the file.
        const float textX = card.rect.x + barWidth + 12.0f;
        const float textWidth = card.rect.width - barWidth - 24.0f;
        float textY = card.rect.y + 26.0f;

        FontSpec titleFont;
        titleFont.family = "Segoe UI";
        titleFont.size = 15.0f;
        titleFont.bold = true;
        target->DrawText(card.fileName, titleFont, textX, textY, kTitle);

        // The folder the file is in.
        if (!card.folder.empty()) {
            textY += 20.0f;
            FontSpec folderFont;
            folderFont.family = "Segoe UI";
            folderFont.size = 12.0f;
            target->DrawText(card.folder, folderFont, textX, textY, kFolder);
        }

        // The first lines of the file, with the headings a little
        // heavier and the lines of a table separator left out.
        const float previewY = card.rect.y + 68.0f;
        const float previewBottom = card.rect.y + card.rect.height - 12.0f;
        float y = previewY;
        size_t at = 0;
        while (at < card.preview.size() && y < previewBottom) {
            size_t eol = card.preview.find('\n', at);
            const size_t end =
                (eol == std::string::npos ? card.preview.size() : eol);
            std::string line = card.preview.substr(at, end - at);
            at = (eol == std::string::npos) ? card.preview.size() : eol + 1;

            if (IsTableSeparator(line)) continue;

            FontSpec font;
            font.family = "Segoe UI";
            font.size = 12.0f;
            Color color = kPreview;
            if (IsHeading(line)) {
                font.bold = true;
                font.size = 13.0f;
                color = kTitle;
            }
            target->DrawText(line, font, textX, y, color);
            y += 16.0f;
        }
        (void)textWidth;
    }
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

}  // namespace mac
