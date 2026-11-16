#pragma once

// Resolves a markdown image or SVG source against the directory that
// holds the open .md file, so `![](images/pic.png)` is read next to the
// document instead of against the process's working directory. Shared
// between the Windows loader (src/imagehelper.cpp, WIC) and the mac one
// (src/macos/image_mac.mm, ImageIO), per the "shared code first" rule
// in AGENTS.md: the two ports must decide this the same way, not grow
// two copies of the same join.

#include <string>

namespace imagepath {

// True for a source the loader never resolves against a directory: a
// remote URL or a data: URI, read exactly as the document embeds it.
bool IsSchemeUrl(const std::string& source);

// True when `path` is already anchored: a POSIX absolute path, a
// Windows drive letter ("C:\..." or "C:/..."), or a path rooted at the
// current drive ("\..."). A document that already names an anchored
// path is left alone; only a path relative to the document gets
// resolved.
bool IsAbsolutePath(const std::string& path);

// The directory holding `filePath` (everything before the last path
// separator), or an empty string when `filePath` carries no separator
// at all (a bare filename) or is itself empty (a new, unsaved
// document). Accepts both '/' and '\\' as a separator, so a path from
// either port's own file-open dialog resolves the same way.
std::string DirectoryOf(const std::string& filePath);

// Joins `docDir` and `source` for a relative image or SVG path. A
// scheme URL, an already-absolute path, an empty `source`, or an empty
// `docDir` (no directory known yet) is returned unchanged; every other
// source is resolved against `docDir`.
std::string Resolve(const std::string& docDir, const std::string& source);

}  // namespace imagepath
