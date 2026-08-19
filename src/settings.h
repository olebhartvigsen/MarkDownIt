#pragma once

#include <windows.h>

// Persisted application settings, stored in HKCU\Software\MarkDownIt.
// Loaded at startup, saved on change and shutdown.

struct AppSettings {
    // Whether .md and .markdown file extensions are registered to MarkDownIt.
    bool fileAssoc = true;

    // Document pane width mode.
    //   0 = Standard (800 DIP, the original default)
    //   1 = 960 DIP
    //   2 = 1600 DIP
    //   3 = Full window width (no cap)
    int contentWidthMode = 0;
};

// Load settings from the registry. Returns defaults if not found.
AppSettings LoadSettings();

// Save settings to the registry.
void SaveSettings(const AppSettings& s);
