#pragma once

// The macOS menu bar.
//
// The Windows build has a ribbon with tabs, groups and commands. macOS
// has a menu bar, so the same commands live in the menu the way a Mac
// user expects: File, Edit, View and Format.
//
// The actions are declared as a plain C++ interface, so the menu does
// not depend on the app and the commands can be tested.

#include <string>
#include <vector>

@class NSMenu;

namespace mac {

// What the menu can ask the app to do.
//
// Every method is one command. The menu calls them, and the app decides
// what happens.
class MenuActions {
public:
    virtual ~MenuActions() = default;

    // File.
    virtual void OnNewDocument() = 0;
    virtual void OnOpen() = 0;
    virtual void OnReload() = 0;
    virtual void OnSave() = 0;
    virtual void OnSaveAs() = 0;
    virtual void OnClose() = 0;

    // The application menu.
    virtual void OnOpenSettings() = 0;

    // Edit.
    virtual void OnFind() = 0;
    virtual void OnReplace() = 0;
    virtual void OnUndo() = 0;
    virtual void OnRedo() = 0;
    virtual void OnCut() = 0;
    virtual void OnCopy() = 0;
    virtual void OnPaste() = 0;
    virtual void OnSelectAll() = 0;

    // View.
    virtual void OnZoomIn() = 0;
    virtual void OnZoomOut() = 0;
    virtual void OnZoomReset() = 0;
    virtual void OnToggleSourceView() = 0;
    virtual void OnToggleOutline() = 0;
    virtual void OnToggleWrap() = 0;
    virtual void OnCycleContentWidth() = 0;

    // Format.
    virtual void OnToggleBold() = 0;
    virtual void OnToggleItalic() = 0;
    virtual void OnToggleCode() = 0;
    virtual void OnToggleLink() = 0;
};

// Builds the main menu bar and returns it. The menu items are wired to
// the given actions.
//
// The returned menu is owned by the caller, which hands it to NSApp.
NSMenu* BuildMainMenu(MenuActions* actions);

}  // namespace mac
