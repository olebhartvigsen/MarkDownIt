#pragma once
#include <dwrite.h>
#include <vector>
#include <cstdint>
#include "dom.h"

// One entry per rendered block: the live layout plus the rect it
// occupied, in document coordinates (before scroll translation).
struct BlockLayout {
    IDWriteTextLayout* layout = nullptr;  // owned, may be null
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t srcOffset = 0;    // copied from Node
    uint32_t srcLength = 0;
    uint32_t textStartOffset = 0;  // source offset of layout char 0
    size_t nodeIndex = 0;
};

class LayoutCache {
public:
    ~LayoutCache();
    void Clear();
    void Add(BlockLayout bl);
    const std::vector<BlockLayout>& Blocks() const { return blocks_; }

    // Find the block whose rect contains (x, y). Returns -1 if none.
    int HitTestBlock(float x, float y) const;

    // Find the block owning a source offset. Returns -1 if none.
    int BlockForOffset(uint32_t offset) const;

private:
    std::vector<BlockLayout> blocks_;
};
