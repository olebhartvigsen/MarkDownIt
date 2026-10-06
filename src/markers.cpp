// Marker storage: the persisted highlight annotations of one document plus
// the JSON sidecar that carries them across sessions. Everything here is
// portable C++17 (byte offsets, std::string, std::filesystem), so the core
// compiles and is tested under g++ as well as MSVC. No UI, no platform
// headers.

#include "markers.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace {

// Bytes of context stored on each side of a marker anchor.
constexpr size_t kContextBytes = 32;

bool IsContinuationByte(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

// The bytes immediately before `start`, at most 32 and never starting in the
// middle of a code point: a continuation byte at the window edge is skipped,
// so the window begins at a character lead.
std::string ExtractPrefix(const std::string& text, size_t start) {
    const size_t begin = start >= kContextBytes ? start - kContextBytes : 0;
    size_t from = begin;
    while (from < start &&
           IsContinuationByte(static_cast<unsigned char>(text[from]))) {
        ++from;
    }
    return text.substr(from, start - from);
}

// The bytes immediately after `end`, at most 32 and never cutting a code
// point in half: when the window's right edge lands inside a multi byte
// character, that character is dropped whole.
std::string ExtractSuffix(const std::string& text, size_t end) {
    if (end >= text.size()) return std::string();
    size_t cut = end + kContextBytes;
    if (cut > text.size()) cut = text.size();
    if (cut < text.size() &&
        IsContinuationByte(static_cast<unsigned char>(text[cut]))) {
        size_t lead = cut - 1;
        while (lead > end &&
               IsContinuationByte(static_cast<unsigned char>(text[lead]))) {
            --lead;
        }
        cut = lead;
    }
    return text.substr(end, cut - end);
}

// True when the context recorded in a marker still matches the document at
// `pos`. `before` selects the bytes before the occurrence (prefix) or the
// bytes after it (suffix). When the stored context is longer than the text
// that exists there, only the available side is compared, aligned at the
// anchor. A non empty context with zero bytes available is not evidence of a
// match; an empty context is met trivially.
bool ContextMatches(const std::string& text, size_t pos,
                    const std::string& context, bool before) {
    const size_t available = before ? pos : text.size() - pos;
    const size_t compare = std::min(context.size(), available);
    if (compare == 0) return context.empty();
    if (before) {
        return text.compare(pos - compare, compare, context,
                            context.size() - compare, compare) == 0;
    }
    return text.compare(pos, compare, context, 0, compare) == 0;
}

// "m-" plus the zero padded decimal of the highest existing numeric suffix
// plus one, four digits minimum: the first id is "m-0001".
std::string NextMarkerId(const std::vector<Marker>& markers) {
    uint64_t highest = 0;
    for (const Marker& marker : markers) {
        if (marker.id.size() <= 2 || marker.id[0] != 'm' || marker.id[1] != '-') {
            continue;
        }
        uint64_t value = 0;
        const char* first = marker.id.data() + 2;
        const char* last = marker.id.data() + marker.id.size();
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc() || result.ptr != last) continue;
        if (value > highest) highest = value;
    }
    std::string digits = std::to_string(highest + 1);
    while (digits.size() < 4u) digits.insert(digits.begin(), '0');
    return "m-" + digits;
}

// The newest "created" in a marker set. ISO 8601 UTC strings of the fixed
// shape sort correctly as text, which is all the adoption scan needs.
std::string NewestCreated(const std::vector<Marker>& markers) {
    std::string newest;
    for (const Marker& marker : markers) {
        if (marker.created > newest) newest = marker.created;
    }
    return newest;
}

// JSON string writing: quote and backslash are escaped, control bytes below
// 0x20 become \n, \r, \t or \u00XX; bytes above 0x7F (UTF-8) pass through.
void AppendJsonString(std::string& out, const std::string& value) {
    out.push_back('"');
    for (const char raw : value) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\u%04x",
                                  static_cast<unsigned>(byte));
                    out += escape;
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
}

bool ReadFileToString(const std::string& path, std::string* out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out->assign(std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
    return !file.bad();
}

// Reads one sidecar document. Tolerates extra whitespace and unknown keys,
// understands the escapes AppendJsonString writes plus \b \f \/, and gives
// up (returns false, the file is then treated as absent) on structural
// garbage. Never throws.
class SidecarJsonReader {
public:
    explicit SidecarJsonReader(const std::string& text) : text_(text) {}

    bool Read(std::string* outPath, std::string* outFingerprint,
              std::vector<Marker>* outMarkers) {
        SkipWhitespace();
        if (!Consume('{')) return false;
        SkipWhitespace();
        if (Consume('}')) return AtEnd();
        for (;;) {
            std::string key;
            if (!ReadString(&key)) return false;
            SkipWhitespace();
            if (!Consume(':')) return false;
            SkipWhitespace();
            if (key == "path") {
                if (!ReadString(outPath)) return false;
            } else if (key == "fingerprint") {
                if (!ReadString(outFingerprint)) return false;
            } else if (key == "markers") {
                if (!ReadMarkers(outMarkers)) return false;
            } else {
                if (!SkipValue()) return false;
            }
            SkipWhitespace();
            if (Consume(',')) {
                SkipWhitespace();
                continue;
            }
            if (Consume('}')) return AtEnd();
            return false;
        }
    }

private:
    bool AtEnd() {
        SkipWhitespace();
        return pos_ == text_.size();
    }

    bool Consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool ReadString(std::string* out) {
        SkipWhitespace();
        if (!Consume('"')) return false;
        out->clear();
        while (pos_ < text_.size()) {
            const unsigned char byte = static_cast<unsigned char>(text_[pos_++]);
            if (byte == '"') return true;
            if (byte == '\\') {
                if (pos_ >= text_.size()) return false;
                const unsigned char escape =
                    static_cast<unsigned char>(text_[pos_++]);
                if (!DecodeEscape(escape, out)) return false;
                continue;
            }
            if (byte < 0x20) return false;  // raw control bytes are not JSON
            out->push_back(static_cast<char>(byte));
        }
        return false;  // unterminated string
    }

    bool DecodeEscape(unsigned char escape, std::string* out) {
        switch (escape) {
            case '"': out->push_back('"'); return true;
            case '\\': out->push_back('\\'); return true;
            case '/': out->push_back('/'); return true;
            case 'n': out->push_back('\n'); return true;
            case 'r': out->push_back('\r'); return true;
            case 't': out->push_back('\t'); return true;
            case 'b': out->push_back('\b'); return true;
            case 'f': out->push_back('\f'); return true;
            case 'u': {
                uint32_t codePoint = 0;
                if (!ReadHex4(&codePoint)) return false;
                AppendCodePointUtf8(codePoint, out);
                return true;
            }
            default:
                return false;
        }
    }

    bool ReadHex4(uint32_t* out) {
        if (text_.size() - pos_ < 4u) return false;
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        *out = value;
        return true;
    }

    static void AppendCodePointUtf8(uint32_t codePoint, std::string* out) {
        if (codePoint <= 0x7F) {
            out->push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            out->push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            out->push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out->push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out->push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool ReadU32(uint32_t* out) {
        SkipWhitespace();
        const size_t begin = pos_;
        uint64_t value = 0;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c < '0' || c > '9') break;
            value = value * 10 + static_cast<uint64_t>(c - '0');
            if (value > 0xFFFFFFFFULL) return false;
            ++pos_;
        }
        if (pos_ == begin) return false;
        *out = static_cast<uint32_t>(value);
        return true;
    }

    bool ReadMarkers(std::vector<Marker>* out) {
        if (!Consume('[')) return false;
        SkipWhitespace();
        if (Consume(']')) return true;
        for (;;) {
            Marker marker;
            bool anchored = false;
            if (!ReadMarkerEntry(&marker, &anchored)) return false;
            if (anchored) out->push_back(std::move(marker));
            SkipWhitespace();
            if (Consume(',')) {
                SkipWhitespace();
                continue;
            }
            if (Consume(']')) return true;
            return false;
        }
    }

    bool ReadMarkerEntry(Marker* out, bool* anchored) {
        *anchored = false;
        SkipWhitespace();
        if (!Consume('{')) return false;
        SkipWhitespace();
        if (Consume('}')) return true;
        bool hasAnchor = false;
        for (;;) {
            std::string key;
            if (!ReadString(&key)) return false;
            SkipWhitespace();
            if (!Consume(':')) return false;
            SkipWhitespace();
            if (key == "id") {
                if (!ReadString(&out->id)) return false;
            } else if (key == "start") {
                if (!ReadU32(&out->start)) return false;
            } else if (key == "end") {
                if (!ReadU32(&out->end)) return false;
            } else if (key == "exact") {
                if (!ReadString(&out->exact)) return false;
                hasAnchor = true;
            } else if (key == "prefix") {
                if (!ReadString(&out->prefix)) return false;
            } else if (key == "suffix") {
                if (!ReadString(&out->suffix)) return false;
            } else if (key == "created") {
                if (!ReadString(&out->created)) return false;
            } else {
                if (!SkipValue()) return false;
            }
            SkipWhitespace();
            if (Consume(',')) {
                SkipWhitespace();
                continue;
            }
            if (Consume('}')) break;
            return false;
        }
        *anchored = hasAnchor;
        return true;
    }

    bool SkipValue() {
        SkipWhitespace();
        if (pos_ >= text_.size()) return false;
        const char c = text_[pos_];
        if (c == '"') {
            std::string ignored;
            return ReadString(&ignored);
        }
        if (c == '{') return SkipObject();
        if (c == '[') return SkipArray();
        if (c == 't') return ConsumeWord("true");
        if (c == 'f') return ConsumeWord("false");
        if (c == 'n') return ConsumeWord("null");
        return SkipNumber();
    }

    bool SkipObject() {
        ++pos_;  // the '{'
        SkipWhitespace();
        if (Consume('}')) return true;
        for (;;) {
            std::string key;
            if (!ReadString(&key)) return false;
            SkipWhitespace();
            if (!Consume(':')) return false;
            if (!SkipValue()) return false;
            SkipWhitespace();
            if (Consume(',')) {
                SkipWhitespace();
                continue;
            }
            if (Consume('}')) return true;
            return false;
        }
    }

    bool SkipArray() {
        ++pos_;  // the '['
        SkipWhitespace();
        if (Consume(']')) return true;
        for (;;) {
            if (!SkipValue()) return false;
            SkipWhitespace();
            if (Consume(',')) {
                SkipWhitespace();
                continue;
            }
            if (Consume(']')) return true;
            return false;
        }
    }

    bool ConsumeWord(const char* word) {
        const size_t length = std::strlen(word);
        if (text_.size() - pos_ < length) return false;
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        return true;
    }

    bool SkipNumber() {
        const size_t begin = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        bool hasDigit = false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c >= '0' && c <= '9') {
                hasDigit = true;
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                ++pos_;
            } else {
                break;
            }
        }
        return hasDigit && pos_ > begin;
    }

    const std::string& text_;
    size_t pos_ = 0;
};

bool ParseSidecarText(const std::string& text, std::string* outPath,
                      std::string* outFingerprint,
                      std::vector<Marker>* outMarkers) {
    SidecarJsonReader reader(text);
    return reader.Read(outPath, outFingerprint, outMarkers);
}

// Recovers the markers of a document that was moved or renamed: the sidecar
// is still filed under the hash of the previous path, and the previous file
// is gone from disk. Adopted only when the recorded bytes (the fingerprint)
// match the document in hand and the recorded path no longer exists, so a
// copy under a new name never steals another document's markers. The newest
// candidate by "created" wins.
bool AdoptMovedSidecar(const std::string& documentPath,
                       const std::string& documentText,
                       std::vector<Marker>* outMarkers) {
    std::error_code ec;
    std::filesystem::directory_iterator it(MarkerStore::SidecarDirectory(), ec);
    if (ec) return false;

    const std::string fingerprint = MarkerFingerprint(documentText);
    std::vector<Marker> best;
    std::string bestCreated;
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const std::filesystem::directory_entry& entry = *it;
        std::error_code typeError;
        if (!entry.is_regular_file(typeError) || typeError) continue;
        const std::string name = entry.path().filename().string();
        const size_t suffixLength = std::strlen(".markers.json");
        if (name.size() < suffixLength ||
            name.compare(name.size() - suffixLength, suffixLength,
                         ".markers.json") != 0) {
            continue;
        }
        std::string data;
        if (!ReadFileToString(entry.path().string(), &data)) continue;
        std::string storedPath;
        std::string storedFingerprint;
        std::vector<Marker> loaded;
        if (!ParseSidecarText(data, &storedPath, &storedFingerprint, &loaded)) {
            continue;
        }
        if (storedPath == documentPath) continue;
        if (storedFingerprint != fingerprint) continue;
        if (loaded.empty()) continue;
        std::error_code existsError;
        const bool oldPathExists = std::filesystem::exists(storedPath, existsError);
        if (existsError || oldPathExists) continue;
        const std::string created = NewestCreated(loaded);
        if (best.empty() || created > bestCreated) {
            best = std::move(loaded);
            bestCreated = created;
        }
    }
    if (best.empty()) return false;
    *outMarkers = std::move(best);
    return true;
}

std::string& SidecarDirectoryOverride() {
    static std::string directory;
    return directory;
}

}  // namespace

std::string MarkerFingerprint(const std::string& text) {
    // FNV-1a 64: offset basis 14695981039346656037, prime 1099511628211.
    uint64_t hash = 14695981039346656037ULL;
    for (const char raw : text) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(raw));
        hash *= 1099511628211ULL;
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(hex);
}

std::string MarkerNowIso8601() {
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(text);
}

void MarkerStore::SetSidecarDirectoryForTests(const std::string& dir) {
    SidecarDirectoryOverride() = dir;
}

std::string MarkerStore::SidecarDirectory() {
    if (!SidecarDirectoryOverride().empty()) return SidecarDirectoryOverride();
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec) return "MarkDownIt";
    return (temp / "MarkDownIt").string();
}

std::string MarkerStore::SidecarPathFor(const std::string& documentPath) {
    const std::filesystem::path directory(SidecarDirectory());
    const std::string name = MarkerFingerprint(documentPath) + ".markers.json";
    return (directory / name).string();
}

bool MarkerStore::LoadFor(const std::string& documentPath,
                          const std::string& documentText) {
    markers_.clear();

    const std::string directPath = SidecarPathFor(documentPath);
    std::error_code existsError;
    const bool hasDirect =
        std::filesystem::exists(directPath, existsError) && !existsError;
    if (hasDirect) {
        std::string data;
        std::string storedPath;
        std::string storedFingerprint;
        std::vector<Marker> loaded;
        if (!ReadFileToString(directPath, &data)) return false;
        if (!ParseSidecarText(data, &storedPath, &storedFingerprint, &loaded)) {
            return false;
        }
        markers_ = std::move(loaded);
        return !markers_.empty();
    }

    // No sidecar under this path hash: a renamed or moved document can still
    // be recovered from the sidecar filed under its previous path.
    return AdoptMovedSidecar(documentPath, documentText, &markers_);
}

bool MarkerStore::Save(const std::string& documentPath,
                       const std::string& documentText) const {
    std::string json;
    json.reserve(256u + 256u * markers_.size());
    json += "{\"version\":1,\"path\":";
    AppendJsonString(json, documentPath);
    json += ",\"fingerprint\":";
    AppendJsonString(json, MarkerFingerprint(documentText));
    json += ",\"markers\":[";
    for (size_t i = 0; i < markers_.size(); ++i) {
        if (i > 0) json += ',';
        const Marker& marker = markers_[i];
        json += "{\"id\":";
        AppendJsonString(json, marker.id);
        json += ",\"start\":";
        json += std::to_string(marker.start);
        json += ",\"end\":";
        json += std::to_string(marker.end);
        json += ",\"exact\":";
        AppendJsonString(json, marker.exact);
        json += ",\"prefix\":";
        AppendJsonString(json, marker.prefix);
        json += ",\"suffix\":";
        AppendJsonString(json, marker.suffix);
        json += ",\"created\":";
        AppendJsonString(json, marker.created);
        json += '}';
    }
    json += "]}\n";

    std::error_code ec;
    std::filesystem::create_directories(SidecarDirectory(), ec);
    std::ofstream file(SidecarPathFor(documentPath),
                       std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    file.flush();
    return static_cast<bool>(file);
}

void MarkerStore::Resolve(const std::string& documentText) {
    for (Marker& marker : markers_) {
        if (marker.exact.empty()) {
            marker.resolved = false;
            continue;
        }
        size_t bestPos = std::string::npos;
        int bestScore = -1;
        size_t bestDistance = 0;
        size_t pos = documentText.find(marker.exact, 0);
        while (pos != std::string::npos) {
            int score = 0;
            if (ContextMatches(documentText, pos, marker.prefix, true)) score += 4;
            if (ContextMatches(documentText, pos + marker.exact.size(),
                               marker.suffix, false)) {
                score += 4;
            }
            const size_t previous = static_cast<size_t>(marker.start);
            const size_t distance = pos > previous ? pos - previous : previous - pos;
            if (score > bestScore || (score == bestScore && distance < bestDistance)) {
                bestScore = score;
                bestDistance = distance;
                bestPos = pos;
            }
            pos = documentText.find(marker.exact, pos + 1);
        }
        if (bestPos == std::string::npos) {
            // The anchor is gone from this revision. Keep the stored offsets
            // and the anchor itself so the marker can come back if the text
            // returns; drawing skips unresolved markers.
            marker.resolved = false;
            continue;
        }
        marker.start = static_cast<uint32_t>(bestPos);
        marker.end = static_cast<uint32_t>(bestPos + marker.exact.size());
        marker.resolved = true;
    }
}

bool MarkerStore::Add(const std::string& documentText, uint32_t start,
                      uint32_t end) {
    if (start >= end) return false;
    if (static_cast<size_t>(end) > documentText.size()) return false;
    for (const Marker& existing : markers_) {
        if (existing.start == start && existing.end == end) return false;
    }
    Marker marker;
    marker.id = NextMarkerId(markers_);
    marker.start = start;
    marker.end = end;
    marker.exact = documentText.substr(start, end - start);
    marker.prefix = ExtractPrefix(documentText, start);
    marker.suffix = ExtractSuffix(documentText, end);
    marker.created = MarkerNowIso8601();
    marker.resolved = true;
    markers_.push_back(std::move(marker));
    return true;
}

size_t MarkerStore::RemoveIntersecting(uint32_t start, uint32_t end) {
    if (start >= end) return 0;
    size_t removed = 0;
    std::vector<Marker> kept;
    kept.reserve(markers_.size());
    for (const Marker& marker : markers_) {
        if (marker.start < end && start < marker.end) {
            ++removed;
        } else {
            kept.push_back(marker);
        }
    }
    markers_.swap(kept);
    return removed;
}

size_t MarkerStore::UnresolvedCount() const {
    size_t count = 0;
    for (const Marker& marker : markers_) {
        if (!marker.resolved) ++count;
    }
    return count;
}
