#pragma once

// Persisted text markers (highlight annotations) of one Markdown document:
// the marker model, anchor relocation against a changed buffer, and the JSON
// sidecar that carries the set across sessions. Portable C++17 only (byte
// offsets, std::string, std::filesystem); no window and no platform headers,
// so the core compiles and is tested under g++ as well as MSVC.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One persisted text marker (a highlight annotation). Offsets are source
// byte offsets in the Markdown buffer; the anchor text relocates them.
struct Marker {
    std::string id;        // "m-0001" style, unique inside one document
    uint32_t start = 0;    // resolved byte range in the current buffer
    uint32_t end = 0;
    std::string exact;     // the marked text itself (anchor)
    std::string prefix;    // up to 32 bytes before it, whole code points only
    std::string suffix;    // up to 32 bytes after it, whole code points only
    std::string created;   // ISO 8601 UTC, e.g. 2026-10-06T08:30:00Z
    bool resolved = true;  // false: anchor not found in this revision
};

// Hex FNV-1a 64 of the bytes, e.g. "a83f91c2d4e5f607".
std::string MarkerFingerprint(const std::string& text);

// Current UTC time as ISO 8601, seconds precision, trailing Z.
std::string MarkerNowIso8601();

// The marker set of one document, with best-effort sidecar persistence.
// The sidecar lives in <OS temp>/MarkDownIt/<hash-of-path>.markers.json;
// the Markdown document itself is never touched. Failures are silent:
// a missing or unreadable sidecar simply means no markers, never an error.
class MarkerStore {
public:
    // <temp>/MarkDownIt via std::filesystem::temp_directory_path().
    static std::string SidecarDirectory();
    // SidecarDirectory(), a separator, MarkerFingerprint(documentPath) and
    // ".markers.json". The separator is the native one, so on Windows the
    // result is exactly SidecarDirectory() + "\\" + hash + ".markers.json".
    static std::string SidecarPathFor(const std::string& documentPath);

    // Test hook only: redirects SidecarDirectory() to a writable directory
    // so tests never touch the real temp tree. Empty restores the default.
    static void SetSidecarDirectoryForTests(const std::string& dir);

    // Load the marker set for a document. Clears this store first.
    // 1) If the path-hash sidecar exists: load it (corrupt file = empty,
    //    keys of individual markers without anchors skipped).
    // 2) Else scan SidecarDirectory() for *.markers.json whose recorded
    //    path field differs, whose fingerprint equals that of documentText,
    //    AND whose recorded path no longer exists on disk (evidence of a
    //    rename or move). The newest by "created" of its markers is adopted.
    // Returns true when any markers were loaded.
    bool LoadFor(const std::string& documentPath,
                 const std::string& documentText);

    // Write the sidecar best-effort (create the directory as needed).
    // Returns false when the write failed; callers ignore that.
    bool Save(const std::string& documentPath,
              const std::string& documentText) const;

    // Recompute start/end/resolved for every marker from its anchor text
    // against the current document text. Called after LoadFor and after
    // any document change. The occurrence whose stored prefix and suffix
    // match best wins (+4 each); ties go to the occurrence nearest the
    // previous start, then to the earliest. An empty or missing anchor
    // stays unresolved and keeps its stored offsets.
    void Resolve(const std::string& documentText);

    // Add a marker for [start, end) of documentText (caller already clipped
    // it to rendered text). Builds exact/prefix/suffix from the text.
    // Refuses (returns false) when the range is empty, out of bounds, or an
    // existing marker has the same start and end. New markers are resolved.
    bool Add(const std::string& documentText, uint32_t start, uint32_t end);

    // Remove every marker that intersects [start, end). Returns the count.
    size_t RemoveIntersecting(uint32_t start, uint32_t end);

    const std::vector<Marker>& Markers() const { return markers_; }
    size_t Count() const { return markers_.size(); }
    size_t UnresolvedCount() const;

private:
    std::vector<Marker> markers_;
};
