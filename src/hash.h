// A small content hash for cache keys.
//
// The SVG caches are keyed by where a fence sits *and* by what it says.
// A standalone SVG file becomes a fence at the same offset in every
// document, so an offset-only key handed a newly opened file the drawing
// of the file before it.
//
// FNV-1a is enough here: it is a cache key, not a stored checksum. Two
// equal texts hash equal, and a collision only costs one wrong drawing.
#pragma once

#include <cstdint>
#include <string>

inline uint64_t Fnv1aUtf32(const std::u32string& text) {
    uint64_t hash = 1469598103934665603ULL;
    for (char32_t ch : text) {
        const uint32_t code = static_cast<uint32_t>(ch);
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<uint64_t>((code >> shift) & 0xFFu);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}
