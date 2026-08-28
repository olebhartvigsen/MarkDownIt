#pragma once

// Caches parsed and laid out mermaid diagrams.
// Portable C++17: no Windows dependencies, unit testable.

#include "mermaid.h"
#include "mermaidlayout.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace mermaid {

// Key: hash of source text + rounded width.
struct DiagramCacheKey {
    uint32_t srcOffset;
    std::string srcHash;
    float width;

    bool operator==(const DiagramCacheKey& o) const {
        return srcOffset == o.srcOffset && srcHash == o.srcHash &&
               std::abs(width - o.width) < 0.5f;
    }
};

struct DiagramCacheKeyHash {
    size_t operator()(const DiagramCacheKey& k) const {
        size_t h = std::hash<uint32_t>()(k.srcOffset);
        h ^= std::hash<std::string>()(k.srcHash) << 1;
        h ^= std::hash<float>()(k.width) << 2;
        return h;
    }
};

class DiagramCache {
public:
    // Get or compute a layout. Returns nullptr if parse/layout fails.
    // On success, the pointer remains valid until Clear() is called.
    const Layout* Get(uint32_t srcOffset, const std::u32string& raw,
                      float width);

    // Clear all cached entries.
    void Clear() { entries_.clear(); }

    // Number of cached entries (for testing).
    size_t Size() const { return entries_.size(); }

private:
    std::unordered_map<DiagramCacheKey, Layout, DiagramCacheKeyHash> entries_;

    static std::string HashText(const std::u32string& raw);
};

}  // namespace mermaid
