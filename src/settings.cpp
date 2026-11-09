#include "settings.h"
#include <filesystem>
#include <fstream>

namespace {

// Whether two paths are the same, ignoring case. The comparison stops at
// the first difference and treats a shorter path as different from a
// longer one, so it never reads past the end of either string.
bool EqualPathCaseInsensitive(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i];
        wchar_t cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb - L'A' + L'a');
        if (ca != cb) return false;
    }
    return true;
}

}  // namespace

void AddRecentFile(AppSettings& s, const std::wstring& path) {
    // Remove existing entry for the same path (dedupe). The comparison
    // is case insensitive, because a path is the same file whichever
    // case it is typed in, and the Win32 call that used to do this is
    // not available on every platform.
    for (auto it = s.recentFiles.begin(); it != s.recentFiles.end(); ++it) {
        if (EqualPathCaseInsensitive(it->path, path)) {
            s.recentFiles.erase(it);
            break;
        }
    }

    // Read first ~300 bytes for preview.
    //
    // The path is a wide string, and only the Visual C++ library takes
    // one in a file stream. The file system path works on every
    // platform, because it knows the encoding of the platform.
    std::string preview;
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (f.is_open()) {
        char buf[310] = {};
        f.read(buf, 300);
        preview.assign(buf, static_cast<size_t>(f.gcount()));
    }

    s.recentFiles.insert(s.recentFiles.begin(), {path, preview});
    if (s.recentFiles.size() > kMaxRecentFiles)
        s.recentFiles.resize(kMaxRecentFiles);
}
