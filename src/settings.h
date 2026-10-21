#pragma once

#include <string>
#include <vector>
#include "zoommodel.h"

// Persisted application settings.
//
// The Windows build keeps these in HKCU\Software\MarkDownIt. The macOS
// build keeps the SAME struct as JSON in
// ~/Library/Application Support/MarkDownIt/settings.json. The struct is
// therefore platform independent, and only the two functions that load
// and save it are platform specific.
//
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

    // Zoom factor for document content (1.0 = 100%, range 25%..400%,
    // constants and clamp in zoommodel.h). Zoom is a GLOBAL view
    // setting, not per-document state: every open document shares the
    // factor within a session, and the last one used is persisted here
    // so it is restored on the next launch. ResetZoom() stores 1.0.
    float zoomFactor = zoom::kDefaultZoom;

    // Outline pane geometry: width in DIP (clamped 140..420) and the
    // last visible state. Persistence is session state, not document
    // state: the pane re-opens as it was left.
    float outlineWidthDip = 200.0f;
    bool  outlineVisible = false;

    // Recent files (most-recent first, max 12).
    std::vector<RecentFile> recentFiles;
};

// The maximum number of recent files kept.
inline constexpr size_t kMaxRecentFiles = 12;

// Add a file to the recent list (moves to front, dedupes, trims to 12).
// Reads up to 300 bytes of the file for the preview text.
void AddRecentFile(AppSettings& s, const std::wstring& path);

#ifdef _WIN32
// Load settings from the registry. Returns defaults if not found.
AppSettings LoadSettings();

// Save settings to the registry.
void SaveSettings(const AppSettings& s);
#endif  // _WIN32
