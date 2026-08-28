#include "diagramcache.h"

#include <cstring>

namespace mermaid {

// Simple hash: FNV-1a of the UTF-8 encoded text.
// Not cryptographically secure, but sufficient for cache keying.
std::string DiagramCache::HashText(const std::u32string& raw) {
    // Encode to UTF-8 first
    std::string utf8;
    for (char32_t c : raw) {
        if (c < 0x80) utf8.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xF0 | (c >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    // FNV-1a 32-bit
    uint32_t h = 2166136261u;
    for (unsigned char c : utf8) {
        h ^= c;
        h *= 16777619u;
    }
    // Return as a 4-byte string (deterministic, no allocation overhead)
    std::string result;
    result.push_back(static_cast<char>(h & 0xFF));
    result.push_back(static_cast<char>((h >> 8) & 0xFF));
    result.push_back(static_cast<char>((h >> 16) & 0xFF));
    result.push_back(static_cast<char>((h >> 24) & 0xFF));
    return result;
}

const Layout* DiagramCache::Get(uint32_t srcOffset,
                                  const std::u32string& raw,
                                  float width,
                                  MeasureFn measure,
                                  void* measureCtx) {
    DiagramCacheKey key;
    key.srcOffset = srcOffset;
    key.srcHash = HashText(raw);
    key.width = width;

    auto it = entries_.find(key);
    if (it != entries_.end()) {
        return &it->second;
    }

    // Cache miss: parse and layout
    // Encode u32string to UTF-8 for the parser
    std::string src;
    for (char32_t c : raw) {
        if (c < 0x80) src.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            src.push_back(static_cast<char>(0xC0 | (c >> 6)));
            src.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            src.push_back(static_cast<char>(0xE0 | (c >> 12)));
            src.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            src.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            src.push_back(static_cast<char>(0xF0 | (c >> 18)));
            src.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            src.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            src.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }

    Diagram diag = Parse(src);
    if (diag.type == DiagramType::Unknown || !diag.error.empty()) {
        return nullptr;  // not cacheable, caller falls back to code block
    }

    Layout lay = ComputeLayout(diag, width, measure, measureCtx);
    if (lay.nodes.empty() && lay.edges.empty()) {
        return nullptr;  // empty diagram, fall back
    }

    auto result = entries_.emplace(key, std::move(lay));
    return &result.first->second;
}

}  // namespace mermaid
