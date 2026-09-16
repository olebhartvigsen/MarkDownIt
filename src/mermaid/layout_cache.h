// Shared Mermaid layout helpers and a bounded source-plus-zoom flowchart cache.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "model.h"
#include "mermaid/pie_parse.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_layout.h"

namespace mermaid {

enum class MermaidKind { Flowchart, Pie, Sequence };
struct MermaidRender {
    MermaidKind kind = MermaidKind::Flowchart;
    std::shared_ptr<LaidOutFlowchart> flow;
    std::shared_ptr<LaidOutPie> pie;
    std::shared_ptr<LaidOutSequence> seq;
};

constexpr float kMermaidBlockPad = 12.0f;

inline float MeasureLayoutHeight(const LaidOutFlowchart& lo, float zoom,
                                 float avail_w) {
    float scale = zoom;
    if (lo.width > 0 && avail_w > 0) {
        const float fit = avail_w / static_cast<float>(lo.width);
        if (fit < scale) scale = fit;
    }
    return static_cast<float>(lo.height) * scale + 2.0f * kMermaidBlockPad;
}
inline float MeasurePieHeight(const LaidOutPie& lp, float zoom, float avail_w) {
    float scale = zoom;
    if (lp.width > 0 && avail_w > 0) {
        const float fit = avail_w / static_cast<float>(lp.width);
        if (fit < scale) scale = fit;
    }
    return static_cast<float>(lp.height) * scale + 2.0f * kMermaidBlockPad;
}
inline float MeasureSequenceHeight(const LaidOutSequence& ls, float zoom,
                                   float avail_w) {
    float scale = zoom;
    if (ls.width > 0 && avail_w > 0) {
        const float fit = avail_w / static_cast<float>(ls.width);
        if (fit < scale) scale = fit;
    }
    return static_cast<float>(ls.vbheight) * scale + 2.0f * kMermaidBlockPad;
}
inline float MeasureMermaidHeight(const MermaidRender& mr, float zoom,
                                  float avail_w) {
    switch (mr.kind) {
        case MermaidKind::Pie: return MeasurePieHeight(*mr.pie, zoom, avail_w);
        case MermaidKind::Sequence: return MeasureSequenceHeight(*mr.seq, zoom, avail_w);
        case MermaidKind::Flowchart: default:
            return MeasureLayoutHeight(*mr.flow, zoom, avail_w);
    }
}

inline uint64_t HashFenceSource(const std::string& src, float zoom) {
    uint64_t h = static_cast<uint64_t>(std::hash<std::string>{}(src));
    uint32_t zbits = 0;
    std::memcpy(&zbits, &zoom, sizeof(zbits));
    uint64_t k = h ^ (static_cast<uint64_t>(zbits) * 0x9E3779B97F4A7C15ULL);
    k ^= k >> 30; k *= 0xBF58476D1CE4E5B9ULL;
    k ^= k >> 27; k *= 0x94D049BB133111EBULL;
    return k ^ (k >> 31);
}

struct MermaidLayoutKey {
    std::string source;
    uint32_t zoom_bits = 0;
    uint64_t hash = 0;

    MermaidLayoutKey() = default;
    MermaidLayoutKey(const std::string& value, float zoom) : source(value) {
        std::memcpy(&zoom_bits, &zoom, sizeof(zoom_bits));
        hash = HashFenceSource(source, zoom);
    }
    bool operator==(const MermaidLayoutKey& other) const {
        return zoom_bits == other.zoom_bits && source == other.source;
    }
};

// Small FIFO cache. Equality checks the complete source and zoom bit pattern,
// so a hash collision cannot return a layout for another diagram.
class MermaidLayoutCache {
public:
    static constexpr size_t kCapacity = 128;

    std::shared_ptr<LaidOutFlowchart> Find(const std::string& source, float zoom) const {
        const MermaidLayoutKey key(source, zoom);
        for (const auto& entry : entries_) {
            if (entry.key.hash == key.hash && entry.key == key) return entry.layout;
        }
        return nullptr;
    }
    void Put(const std::string& source, float zoom,
             std::shared_ptr<LaidOutFlowchart> layout) {
        if (!layout) return;
        const MermaidLayoutKey key(source, zoom);
        for (auto& entry : entries_) {
            if (entry.key.hash == key.hash && entry.key == key) {
                entry.layout = std::move(layout);
                return;
            }
        }
        if (entries_.size() == kCapacity) entries_.erase(entries_.begin());
        entries_.push_back({key, std::move(layout)});
    }
    void Clear() { entries_.clear(); }
    size_t Size() const { return entries_.size(); }

private:
    struct Entry { MermaidLayoutKey key; std::shared_ptr<LaidOutFlowchart> layout; };
    std::vector<Entry> entries_;
};

}  // namespace mermaid
