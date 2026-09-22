#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "dom.h"
// Minimal layoutcache shim: only the API WordSpanAt/MoveWord* need (none of it).
// TableCellAtOffset needs dom.h only.
struct BlockLayout { int dummy = 0; };
class LayoutCache {
public:
    void Clear() {}
    void Add(BlockLayout) {}
    const std::vector<BlockLayout>& Blocks() const { static std::vector<BlockLayout> v; return v; }
    bool OffsetIsRendered(uint32_t) const { return true; }
    void SetSourceText(const std::string*) {}
};
