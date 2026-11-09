#pragma once

// The welcome screen of the macOS app.
//
// The Windows build shows the recent files as cards when no document is
// open. The mac build shows the same cards, with the same layout, and
// the drawing goes through the render target.

#include <memory>
#include <string>
#include <vector>

#include "platform/seams.h"
#include "settings.h"

namespace platform {

// The parsed diagram of a card. The header keeps the SVG reader out of
// it, and a card holds one only when the file is an SVG.
class SvgDoc;

// One card of the welcome screen.
struct WelcomeCard {
    // A row of the preview, wrapped to the width of the card.
    struct Row {
        std::string text;
        // A heading is drawn heavier, the way the preview read before.
        bool heading = false;
    };

    // The path of the file.
    std::string filePath;
    // The name of the file.
    std::string fileName;
    // The folder the file is in.
    std::string folder;
    // The first lines of the file.
    std::string preview;
    // The diagram the card shows instead of the text preview. The first
    // bytes of an SVG are its XML header, so a card for one shows the
    // picture and not the markup.
    std::shared_ptr<SvgDoc> image;
    // What the card draws, fitted to the width of the card. The width
    // and the fonts are fixed, so the fitting runs once and not on every
    // frame.
    std::string titleShown;
    std::string folderShown;
    std::vector<Row> rows;
    bool fitted = false;
    // Where the card sits, in points.
    Rect rect;
};

class WelcomeScreen {
public:
    // Sets the files the cards show.
    void SetRecentFiles(const std::vector<RecentFile>& files);

    // Places the cards for the size of the window.
    void Layout(float viewW, float viewH);

    // Draws the screen. The engine measures a string so a long line of
    // the preview wraps inside its card instead of running over the card
    // next to it.
    void Render(RenderTarget* target, TextEngine* engine, float viewW,
                float viewH, int hoverIndex, bool openHovered);

    // Which card is at the point, or -1 when there is none.
    int HitTest(float x, float y) const;

    // The button that opens a document sits under the heading, on the
    // screen itself and not on a card, so it is asked for on its own.
    // It is placed even when no file has been opened yet.
    bool HitsOpenButton(float x, float y) const;
    Rect OpenButtonRect() const { return openButton_; }

    // The path of a card.
    const std::string& GetPath(int index) const;

    // Where the grid of cards starts: below the heading and the button
    // that opens a document. The tests probe the screen at this line.
    static constexpr float kGridTop = 148.0f;

    // Whether there are any cards.
    bool HasFiles() const { return !cards_.empty(); }

    // How many cards there are.
    int CardCount() const { return static_cast<int>(cards_.size()); }

private:
    // The cards.
    std::vector<WelcomeCard> cards_;
    // Where the Open button sits, in points.
    Rect openButton_;
};

}  // namespace platform
