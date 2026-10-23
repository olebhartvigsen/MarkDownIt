#pragma once

// The macOS find bar.
//
// The Windows build has a find bar made of Win32 child controls. This
// one is an AppKit view, and both sit on the same model, so a search
// behaves the same on the two platforms.
//
// The model owns the search state. The bar only collects the query, the
// replacement and the options, and reports them. The owner refreshes
// the model against the document and turns the result into a selection.

#include <string>

#include "findbar_model.h"

namespace mac {

class FindBar {
public:
    FindBar();
    ~FindBar();

    FindBar(const FindBar&) = delete;
    FindBar& operator=(const FindBar&) = delete;

    // The model, which the owner drives.
    FindBarModel& model() { return model_; }

    // Shows the bar in the given mode. Both rows stay on screen; the
    // mode only enables or greys out the replace half.
    void Show(FindBarMode mode);

    // Re-reads the model into the controls, so a fresh count, a fresh
    // enable state and a status message show up right away. The owner
    // calls this after it refreshed the search.
    void SyncControls();

    // Hides the bar.
    void Hide();

    // Whether the bar is on screen.
    bool IsVisible() const { return visible_; }

    // The AppKit view of the bar, which the window puts above the
    // document.
    //
    // The view is an Objective-C object, and this header is also read
    // by the plain C++ files, so the pointer stays opaque here. Only
    // the Objective-C++ files know what it really is.
    void* view() const { return view_; }

    // Puts the focus in the search field.
    void FocusSearchField();

    // The height the bar takes, in points. Zero when it is hidden.
    float Height() const;

private:
    // The model with the search state.
    FindBarModel model_;
    // The AppKit view.
    void* view_ = nullptr;
    // Whether the bar is on screen.
    bool visible_ = false;
};

}  // namespace mac
