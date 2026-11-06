#pragma once

// The macOS settings.
//
// The Windows build keeps its settings in an INI file next to the
// executable. The mac build keeps the same AppSettings struct as JSON in
// ~/Library/Application Support/MarkDownIt/settings.json, which is where
// macOS expects an application to keep its preferences.

#include <string>
#include <vector>

#include "settings.h"

namespace mac {

// Reads and writes the settings file.
class MacSettings {
public:
    MacSettings();

    // Loads the settings from the user's application support directory.
    // A missing file is not an error: the defaults are used.
    void Load();

    // Writes the settings back. Returns false when the file cannot be
    // written.
    bool Save() const;

    // The settings themselves.
    AppSettings& settings() { return settings_; }
    const AppSettings& settings() const { return settings_; }

    // The path of the settings file.
    const std::string& path() const { return path_; }

    // The recent files, most recent first.
    const std::vector<std::string>& recentFiles() const { return recentFiles_; }

    // The recent files with the first lines of each one, which the
    // welcome screen shows.
    std::vector<RecentFile> recentFilesWithPreview() const;

    // Adds a file to the recent list. The list keeps at most ten entries
    // and never has the same file twice.
    void AddRecentFile(const std::string& path);

    // Removes a file from the recent list.
    void RemoveRecentFile(const std::string& path);

private:
    // The directory the settings live in, created if it has to be.
    std::string EnsureDirectory() const;

    AppSettings settings_;
    std::vector<std::string> recentFiles_;
    std::string path_;
};

}  // namespace mac
