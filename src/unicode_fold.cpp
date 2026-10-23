// The lookups behind the search's Unicode folding. Both tables are
// sorted by code point, so the lookups are binary searches with a cheap
// ASCII fast path in front.
#include "unicode_fold.h"

#include "unicode_fold_tables.h"

namespace unicode {
namespace {

const NfdEntry* FindNfd(std::uint32_t code) {
    if (code < kNfdEntries[0].code || code > kNfdEntries[kNfdEntryCount - 1].code)
        return nullptr;
    std::size_t lo = 0;
    std::size_t hi = kNfdEntryCount;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kNfdEntries[mid].code < code)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < kNfdEntryCount && kNfdEntries[lo].code == code) return &kNfdEntries[lo];
    return nullptr;
}

const FoldEntry* FindFold(std::uint32_t code) {
    if (kFoldEntryCount == 0 || code < kFoldEntries[0].code ||
        code > kFoldEntries[kFoldEntryCount - 1].code)
        return nullptr;
    std::size_t lo = 0;
    std::size_t hi = kFoldEntryCount;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kFoldEntries[mid].code < code)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < kFoldEntryCount && kFoldEntries[lo].code == code) return &kFoldEntries[lo];
    return nullptr;
}

}  // namespace

std::size_t DecomposeInto(std::uint32_t code, std::uint32_t* out, std::size_t capacity) {
    if (capacity == 0) return 0;
    if (code < 0x80) {
        out[0] = code;
        return 1;
    }
    const NfdEntry* entry = FindNfd(code);
    if (entry == nullptr) {
        out[0] = code;
        return 1;
    }
    const std::size_t count = entry->count < capacity ? entry->count : capacity;
    for (std::size_t i = 0; i < count; ++i) out[i] = entry->seq[i];
    return count;
}

std::size_t FoldInto(std::uint32_t code, std::uint32_t* out, std::size_t capacity) {
    if (capacity == 0) return 0;
    if (code >= 0x41 && code <= 0x5A) {
        out[0] = code + 0x20;
        return 1;
    }
    if (code < 0x80) {
        out[0] = code;
        return 1;
    }
    const FoldEntry* entry = FindFold(code);
    if (entry == nullptr) {
        out[0] = code;
        return 1;
    }
    const std::size_t count = entry->count < capacity ? entry->count : capacity;
    for (std::size_t i = 0; i < count; ++i) out[i] = entry->seq[i];
    return count;
}

}  // namespace unicode
