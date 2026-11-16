#include "imagepath.h"

#include <cctype>

namespace imagepath {

namespace {

bool StartsWithCI(const std::string& s, const char* prefix, size_t n) {
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

}  // namespace

bool IsSchemeUrl(const std::string& source) {
    return StartsWithCI(source, "http://", 7) ||
           StartsWithCI(source, "https://", 8) ||
           StartsWithCI(source, "data:", 5);
}

bool IsAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    // A Windows drive letter: "C:\..." or "C:/...".
    if (path.size() >= 2 &&
        std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':') {
        return true;
    }
    return false;
}

std::string DirectoryOf(const std::string& filePath) {
    const size_t slash = filePath.find_last_of("/\\");
    if (slash == std::string::npos) return std::string();
    return filePath.substr(0, slash);
}

std::string Resolve(const std::string& docDir, const std::string& source) {
    if (source.empty() || docDir.empty()) return source;
    if (IsSchemeUrl(source)) return source;
    if (IsAbsolutePath(source)) return source;

    // Join with '/': both WIC's CreateDecoderFromFilename on Windows and
    // fopen/ImageIO on macOS accept a forward slash inside a path, so
    // one join rule serves both ports without a per-platform separator.
    std::string dir = docDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        dir.pop_back();
    }
    return dir + "/" + source;
}

}  // namespace imagepath
