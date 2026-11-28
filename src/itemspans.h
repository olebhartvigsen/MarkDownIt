#pragma once

#include "layout/document_layout.h"
#include "layout/displaylist.h"

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

// Which display list items belong to which laid block, shared by both
// renderers.
//
// The walk pushes its items in block order, the same order as the laid
// blocks, so one forward cursor names the range of items whose
// reference y falls inside a block's band. A block with no items keeps
// an empty range. The reference y is the anchored y of the item's own
// shape (a rect's top, a line's start, a text line's top), which is the
// place the walk put the item for that block; PopClip carries no place
// of its own and closes the clip of the block the cursor is already in.
//
// The blocks are in content column coordinates and so is the display
// list, but a shell that keeps its own block records adds the frame's
// top pad to them, so blockYOffset says how far the block list sits
// below the list origin. Windows passes zero, the mac shell passes its
// padTop.
//
// This is pure data: no measurement, no threshold a platform owns, and
// no pixels. Windows reads the answer for the scroll width of pictures
// in Measure, the mac shell stores it for a future per block replay.
namespace layout {

// first is the index of the block's first item, second one past its
// last; the span of a block with no items is {itemCount, itemCount}.
using ItemSpan = std::pair<uint32_t, uint32_t>;

// The anchored y of an item, the value the merge keys on. False when
// the item carries no place.
inline bool ItemRefY(const display::Item& item, float* out) {
    if (const auto* fill = std::get_if<display::FillRect>(&item)) {
        *out = fill->rect.y;
    } else if (const auto* fill =
                   std::get_if<display::FillRoundedRect>(&item)) {
        *out = fill->rect.y;
    } else if (const auto* fill =
                   std::get_if<display::FillRoundedRectSides>(&item)) {
        *out = fill->rect.y;
    } else if (const auto* s = std::get_if<display::StrokeRect>(&item)) {
        *out = s->rect.y;
    } else if (const auto* s =
                   std::get_if<display::StrokeRoundedRect>(&item)) {
        *out = s->rect.y;
    } else if (const auto* s = std::get_if<display::StrokeLine>(&item)) {
        *out = s->y0;
    } else if (const auto* s =
                   std::get_if<display::StrokePolygon>(&item)) {
        if (s->points.empty()) return false;
        *out = s->points[0].y;
    } else if (const auto* f = std::get_if<display::FillPolygon>(&item)) {
        if (f->points.empty()) return false;
        *out = f->points[0].y;
    } else if (const auto* e = std::get_if<display::FillEllipse>(&item)) {
        *out = e->rect.y;
    } else if (const auto* e =
                   std::get_if<display::StrokeEllipse>(&item)) {
        *out = e->rect.y;
    } else if (const auto* pie = std::get_if<display::PieSlice>(&item)) {
        *out = pie->bounds.y;
    } else if (const auto* t = std::get_if<display::Text>(&item)) {
        *out = t->top;
    } else if (const auto* img = std::get_if<display::Image>(&item)) {
        *out = img->rect.y;
    } else if (const auto* clip = std::get_if<display::PushClip>(&item)) {
        *out = clip->rect.y;
    } else {
        return false;
    }
    return true;
}

// The span of items per block, when byBlock is given. When byNode is
// given it is filled with one span per node instead: one node owns one
// record, so a node's span is its record's span, empty ranges included,
// and a node with no record keeps the empty sentinel.
inline void MergeItemSpans(const std::vector<LaidBlock>& blocks,
                           const display::DisplayList& list,
                           float blockYOffset,
                           std::vector<ItemSpan>* byBlock,
                           std::vector<ItemSpan>* byNode = nullptr,
                           size_t nodeCount = 0) {
    const uint32_t none = static_cast<uint32_t>(list.items.size());
    std::vector<ItemSpan> local;
    std::vector<ItemSpan>& out = byBlock ? *byBlock : local;
    out.assign(blocks.size(), {none, none});
    if (byNode) byNode->assign(nodeCount, {none, none});
    if (blocks.empty()) return;
    size_t block = 0;
    for (uint32_t i = 0; i < list.items.size(); ++i) {
        float refY = 0.0f;
        if (!ItemRefY(list.items[i], &refY)) continue;
        while (block + 1 < blocks.size() &&
               blocks[block + 1].y + blockYOffset <= refY) {
            ++block;
        }
        // The record's band owns every item whose extent sits in it.
        if (refY < blocks[block].y + blockYOffset) continue;
        if (out[block].first > i) out[block].first = i;
        out[block].second = i + 1;
    }
    if (!byNode) return;
    for (size_t b = 0; b < blocks.size(); ++b) {
        const uint32_t node = blocks[b].nodeIndex;
        if (node >= nodeCount) continue;
        (*byNode)[node] = out[b];
    }
}

}  // namespace layout
