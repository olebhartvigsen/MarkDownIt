#pragma once

// The home screen, shared by the Windows and the macOS app.
//
// It is where the app stands when no document is open: two actions at the
// top (start a document, or open one) and the recent files as cards under
// them. The layout, the fitting of the text and the drawing live here and
// go through the render target and the text engine, so the two platforms
// show the same screen. The app only passes its own target and engine.

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
                float viewH, int hoverIndex, bool openHovered,
                bool newHovered = false);

    // Which card is at the point, or -1 when there is none.
    int HitTest(float x, float y) const;

    // The two actions sit under the heading, on the screen itself and not
    // on a card, so each is asked for on its own. They are placed even
    // when no file has been opened yet: a reader who lands here with an
    // empty list still has both ways in.
    bool HitsOpenButton(float x, float y) const;
    Rect OpenButtonRect() const { return openButton_; }
    bool HitsNewButton(float x, float y) const;
    Rect NewButtonRect() const { return newButton_; }

    // The path of a card.
    const std::string& GetPath(int index) const;

    // Where a card sits, in points, after Layout. A zero rect when the
    // index is out of range.
    Rect CardRect(int index) const;

    // The left edge of the grid, where its label starts. Zero until
    // Layout has run with at least one card.
    float GridLeft() const { return gridLeft_; }

    // The band is one row when the pair fits beside the lockup, two when it
    // does not. Both numbers come from Layout, and the grid hangs under
    // whichever height the band has.
    float BandHeight() const { return bandHeight_; }
    float GridTop() const { return bandHeight_ + 50.0f; }
    bool ShowsTagline() const { return taglineShown_; }
    bool StacksHeader() const { return stacked_; }

    // The wide-window case, for a caller that has not laid a screen out.
    static constexpr float kGridTop = 134.0f;

    // Whether there are any cards.
    bool HasFiles() const { return !cards_.empty(); }

    // How many cards there are.
    int CardCount() const { return static_cast<int>(cards_.size()); }

private:
    // The cards.
    std::vector<WelcomeCard> cards_;
    // Where the Open button sits, in points.
    Rect openButton_;
    Rect newButton_;

    // The band, as Layout found it.
    float bandHeight_ = 84.0f;
    bool stacked_ = false;
    bool taglineShown_ = true;
    float markTop_ = 25.0f;
    float wordmarkBaseline_ = 39.0f;
    float taglineBaseline_ = 55.0f;
    float nameX_ = 0.0f;

    void DrawBrand(RenderTarget* target, TextEngine* engine) const;
    // Where the grid of cards starts, left edge.
    float gridLeft_ = 0.0f;
};

}  // namespace platform
