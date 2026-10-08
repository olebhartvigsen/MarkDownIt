#pragma once

// Outline model: the heading navigation projection of the document DOM.
// Portable and headless (no windows.h) so it builds and tests under g++.
// The pane keeps no second document copy; it borrows the Document after
// each reparse and derives its item list.

#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include "dom.h"

// One outline entry, in document order.
struct OutlineItem {
    int              level = 0;   // heading level 1..6
    uint32_t         offset = 0;  // source byte offset of the heading text
    std::u32string   text;        // simplified plain text of the heading
};

// Collect all heading nodes in document order. Simplification: inline
// spans of any kind contribute their plain UTF-32 text only; links
// contribute their label text, images contribute nothing. Returns an
// empty vector when no headings exist.
std::vector<OutlineItem> CollectHeadings(const Document& doc);

// Plain text of one item (helper exposed for tests).
std::u32string ConcatenateInlineText(const OutlineItem& item);

// ---------------------------------------------------------------------
// Collapse state (model half of pane behavior). A collapsed index hides
// its entire subtree: every following item until an item whose level is
// less than or equal to the collapsed one's. The state is keyed by
// position in the current item list, so a reparse that shifts indices
// drops stale entries on re-resolve (see OutlineCollapse::ReResolve).
// ---------------------------------------------------------------------
class OutlineCollapse {
public:
    // Toggle the subtree rooted at item index i.
    void Toggle(int i) { collapsed_.insert_or_assign(i, !collapsed_[i]); }
    bool IsCollapsed(int i) const { return collapsed_.count(i) > 0 && collapsed_.at(i); }
    // Drop entries at or after newCount (list shrank) or beyond the
    // maximum valid index (list grew or shifted: callers re-toggle).
    void ReResolve(size_t newCount) {
        for (auto it = collapsed_.begin(); it != collapsed_.end(); ) {
            if (it->first >= static_cast<int>(newCount)) it = collapsed_.erase(it);
            else ++it;
        }
    }
    bool Empty() const { return collapsed_.empty(); }
    size_t Size() const { return collapsed_.size(); }

private:
    std::map<int, bool> collapsed_;  // index -> is-collapsed
};

// Indices of items visible under the collapse state, in document order.
std::vector<int> VisibleItems(const std::vector<OutlineItem>& items,
                              const OutlineCollapse& st);

// Owner lookup: which item's section contains the given source offset
// (the heading's own start is included, the next heading's start is the
// boundary). -1 when the offset is beyond the last item's section.
int ItemIndexForOffset(const std::vector<OutlineItem>& items, uint32_t offset);
