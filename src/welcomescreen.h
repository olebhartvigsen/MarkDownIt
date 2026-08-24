#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "settings.h"

struct WelcomeCard {
    std::wstring filePath;   // full path
    std::wstring fileName;   // just the basename
    std::wstring folder;     // parent folder
    std::string  preview;    // raw markdown preview text
    float x = 0, y = 0, w = 0, h = 0;  // card layout rect (DIPs)
};

class WelcomeScreen {
public:
    WelcomeScreen();
    ~WelcomeScreen();

    void Init(IDWriteFactory* dw);

    // Set the list of recent files to display.
    void SetRecentFiles(const std::vector<RecentFile>& files);

    // Layout cards for the given viewport size.
    void Layout(float viewW, float viewH);

    // Render the welcome screen.
    void Render(ID2D1RenderTarget* rt, float viewW, float viewH, int hoverIndex);

    // Hit-test: which card is at (x, y)? Returns -1 if none.
    int HitTest(float x, float y) const;

    // Get the file path for a card index.
    const std::wstring& GetPath(int index) const;

    bool HasFiles() const { return !cards_.empty(); }
    int GetCardCount() const { return static_cast<int>(cards_.size()); }

private:
    IDWriteFactory* dw_ = nullptr;
    IDWriteTextFormat* title_fmt_ = nullptr;
    IDWriteTextFormat* cardTitle_fmt_ = nullptr;
    IDWriteTextFormat* cardPreview_fmt_ = nullptr;
    IDWriteTextFormat* cardFolder_fmt_ = nullptr;

    std::vector<WelcomeCard> cards_;

    static const int kMaxCards = 12;
    static const float kCardW;
    static const float kCardH;
    static const float kCardGap;
    static const float kTopMargin;
    static const float kSideMargin;
};
