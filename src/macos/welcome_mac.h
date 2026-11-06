#pragma once

// The welcome screen of the macOS app.
//
// The Windows build shows the recent files as cards when no document is
// open. The mac build shows the same cards, with the same layout, and
// the drawing goes through the render target.

#include <string>
#include <vector>

#include "seams.h"
#include "settings.h"

namespace mac {

// One card of the welcome screen.
struct WelcomeCard {
    // The path of the file.
    std::string filePath;
    // The name of the file.
    std::string fileName;
    // The folder the file is in.
    std::string folder;
    // The first lines of the file.
    std::string preview;
    // Where the card sits, in points.
    Rect rect;
};

class WelcomeScreen {
public:
    // Sets the files the cards show.
    void SetRecentFiles(const std::vector<RecentFile>& files);

    // Places the cards for the size of the window.
    void Layout(float viewW, float viewH);

    // Draws the screen.
    void Render(RenderTarget* target, float viewW, float viewH,
                int hoverIndex);

    // Which card is at the point, or -1 when there is none.
    int HitTest(float x, float y) const;

    // The path of a card.
    const std::string& GetPath(int index) const;

    // Whether there are any cards.
    bool HasFiles() const { return !cards_.empty(); }

    // How many cards there are.
    int CardCount() const { return static_cast<int>(cards_.size()); }

private:
    // The cards.
    std::vector<WelcomeCard> cards_;
};

}  // namespace mac
