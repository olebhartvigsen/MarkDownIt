#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Persisted application settings, stored in HKCU\Software\MarkDownIt.
// Loaded at startup, saved on change and shutdown.

struct RecentFile {
    std::wstring path;
    std::string  preview;   // first ~300 chars of content for card preview
};

struct AppSettings {
    // Whether .md and .markdown file extensions are registered to MarkDownIt.
    bool fileAssoc = true;

    // Document pane width mode.
    //   0 = Standard (800 DIP, the original default)
    //   1 = 960 DIP
    //   2 = 1600 DIP
    //   3 = Full window width (no cap)
    int contentWidthMode = 0;

    // Recent files (most-recent first, max 12).
    std::vector<RecentFile> recentFiles;
};

// Load settings from the registry. Returns defaults if not found.
AppSettings LoadSettings();

// Save settings to the registry.
void SaveSettings(const AppSettings& s);

// Add a file to the recent list (moves to front, dedupes, trims to 12).
// Reads up to 300 bytes of the file for the preview text.
void AddRecentFile(AppSettings& s, const std::wstring& path);
