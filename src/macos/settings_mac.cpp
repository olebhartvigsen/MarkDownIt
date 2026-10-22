// The macOS settings.
//
// The Windows build keeps its settings in the registry. This one keeps
// the SAME AppSettings struct as JSON in the directory macOS expects an
// application to use:
//
//   ~/Library/Application Support/MarkDownIt/settings.json
//
// The struct is shared, so only the reading and the writing are new here.
//
// The JSON is written by hand rather than with a library, because the
// project ships without third party dependencies. The writer is
// deliberately conservative: it only ever writes the fields it knows,
// and the reader ignores anything it does not understand, so an old
// settings file still opens after the app grows a new field.

#include "settings_mac.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace mac {

namespace {

// The directory the settings live in, relative to the home directory.
constexpr const char* kApplicationSupport = "Library/Application Support";
constexpr const char* kApplicationName = "MarkDownIt";
constexpr const char* kSettingsFile = "settings.json";

// The maximum number of entries in the recent files list.
constexpr size_t kMaxRecentEntries = 10;

// The home directory of the user, with a fallback so the app still
// works when the environment does not have one.
std::string HomeDirectory() {
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home);

    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *pw->pw_dir) return std::string(pw->pw_dir);
#endif
    return std::string(".");
}

// The path of the settings file.
std::string SettingsPath() {
    return HomeDirectory() + "/" + kApplicationSupport + "/" +
           kApplicationName + "/" + kSettingsFile;
}

// Escapes a string for a JSON string literal.
std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

// Finds the value of a string field in a flat JSON object.
//
// The reader is a small scanner rather than a full parser. It looks for
// the field name and reads the string that follows it, which is enough
// for the flat object this file holds.
bool FindJsonString(const std::string& text, const std::string& key,
                    std::string* out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;

    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;

    // Skip the whitespace.
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }
    if (pos >= text.size() || text[pos] != '"') return false;
    ++pos;

    std::string value;
    while (pos < text.size() && text[pos] != '"') {
        if (text[pos] == '\\' && pos + 1 < text.size()) {
            ++pos;
            switch (text[pos]) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                default: value += text[pos]; break;
            }
            ++pos;
            continue;
        }
        value += text[pos];
        ++pos;
    }
    if (pos >= text.size()) return false;

    *out = value;
    return true;
}

// Finds the value of a number field in a flat JSON object.
bool FindJsonNumber(const std::string& text, const std::string& key,
                    double* out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;

    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;

    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }

    const char* start = text.c_str() + pos;
    char* end = nullptr;
    const double value = std::strtod(start, &end);
    if (end == start) return false;

    *out = value;
    return true;
}

// Finds the value of a boolean field in a flat JSON object.
bool FindJsonBool(const std::string& text, const std::string& key, bool* out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;

    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;

    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }

    if (text.compare(pos, 4, "true") == 0) {
        *out = true;
        return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

// Reads the "recentFiles" array of strings.
//
// The array is scanned for every quoted string between the brackets.
std::vector<std::string> FindJsonStringArray(const std::string& text,
                                              const std::string& key) {
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return values;

    pos = text.find('[', pos + needle.size());
    if (pos == std::string::npos) return values;

    const size_t end = text.find(']', pos);
    if (end == std::string::npos) return values;

    size_t cursor = pos + 1;
    while (cursor < end) {
        const size_t open = text.find('"', cursor);
        if (open == std::string::npos || open >= end) break;

        std::string value;
        size_t scan = open + 1;
        while (scan < end && text[scan] != '"') {
            if (text[scan] == '\\' && scan + 1 < end) {
                ++scan;
                switch (text[scan]) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    default: value += text[scan]; break;
                }
                ++scan;
                continue;
            }
            value += text[scan];
            ++scan;
        }
        values.push_back(value);
        cursor = scan + 1;
    }
    return values;
}

// Converts a narrow path to the wide path the settings struct uses.
std::wstring ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const size_t count = ::mbstowcs(nullptr, text.c_str(), 0);
    if (count == static_cast<size_t>(-1)) return std::wstring();
    std::wstring wide(count, L'\0');
    ::mbstowcs(&wide[0], text.c_str(), count + 1);
    return wide;
}

// Converts a wide path back to a narrow one.
std::string ToNarrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const size_t count = ::wcstombs(nullptr, text.c_str(), 0);
    if (count == static_cast<size_t>(-1)) return std::string();
    std::string narrow(count, '\0');
    ::wcstombs(&narrow[0], text.c_str(), count + 1);
    return narrow;
}

}  // namespace

MacSettings::MacSettings() {
    path_ = SettingsPath();
}

std::string MacSettings::EnsureDirectory() const {
    const std::string directory =
        HomeDirectory() + "/" + kApplicationSupport + "/" + kApplicationName;

    // Create the directories one level at a time, because mkdir does not
    // create the parents.
    std::string current;
    const size_t start = HomeDirectory().size();
    current = HomeDirectory();

    size_t pos = start;
    while (pos < directory.size()) {
        const size_t slash = directory.find('/', pos);
        if (slash == std::string::npos) break;
        current = directory.substr(0, slash);
        ::mkdir(current.c_str(), 0755);
        pos = slash + 1;
    }
    ::mkdir(directory.c_str(), 0755);
    return directory;
}

void MacSettings::Load() {
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        // A missing settings file is not an error. The defaults stand.
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    bool fileAssoc = settings_.fileAssoc;
    if (FindJsonBool(text, "fileAssoc", &fileAssoc)) {
        settings_.fileAssoc = fileAssoc;
    }

    int contentWidthMode = settings_.contentWidthMode;
    double number = 0.0;
    if (FindJsonNumber(text, "contentWidthMode", &number)) {
        contentWidthMode = static_cast<int>(number);
    }
    settings_.contentWidthMode = contentWidthMode;

    if (FindJsonNumber(text, "zoomFactor", &number)) {
        settings_.zoomFactor = static_cast<float>(number);
    }

    if (FindJsonNumber(text, "outlineWidthDip", &number)) {
        settings_.outlineWidthDip = static_cast<float>(number);
    }

    bool outlineVisible = settings_.outlineVisible;
    if (FindJsonBool(text, "outlineVisible", &outlineVisible)) {
        settings_.outlineVisible = outlineVisible;
    }

    recentFiles_.clear();
    for (const std::string& path : FindJsonStringArray(text, "recentFiles")) {
        recentFiles_.push_back(path);
        if (recentFiles_.size() >= kMaxRecentEntries) break;
    }
}

bool MacSettings::Save() const {
    if (path_.empty()) return false;

    // The directory has to exist before the file can be written.
    EnsureDirectory();

    std::ostringstream out;
    out << "{\n";
    out << "  \"fileAssoc\": " << (settings_.fileAssoc ? "true" : "false")
        << ",\n";
    out << "  \"contentWidthMode\": " << settings_.contentWidthMode << ",\n";
    out << "  \"zoomFactor\": " << settings_.zoomFactor << ",\n";
    out << "  \"outlineWidthDip\": " << settings_.outlineWidthDip << ",\n";
    out << "  \"outlineVisible\": "
        << (settings_.outlineVisible ? "true" : "false") << ",\n";
    out << "  \"recentFiles\": [";
    for (size_t i = 0; i < recentFiles_.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << JsonEscape(recentFiles_[i]) << "\"";
    }
    out << "]\n";
    out << "}\n";

    // Write to a temporary file and rename it, so a crash halfway
    // through cannot leave a half written settings file behind.
    const std::string temporary = path_ + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << out.str();
        if (!file) return false;
    }
    return std::rename(temporary.c_str(), path_.c_str()) == 0;
}

void MacSettings::AddRecentFile(const std::string& path) {
    if (path.empty()) return;

    // The file moves to the front, and it is not in the list twice.
    RemoveRecentFile(path);
    recentFiles_.insert(recentFiles_.begin(), path);

    if (recentFiles_.size() > kMaxRecentEntries) {
        recentFiles_.resize(kMaxRecentEntries);
    }
}

void MacSettings::RemoveRecentFile(const std::string& path) {
    for (size_t i = 0; i < recentFiles_.size(); ++i) {
        if (recentFiles_[i] == path) {
            recentFiles_.erase(recentFiles_.begin() + static_cast<long>(i));
            return;
        }
    }
}

}  // namespace mac
