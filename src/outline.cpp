// Outline model implementation, portable and headless. See outline.h.

#include "outline.h"

std::vector<OutlineItem> CollectHeadings(const Document& doc) {
    std::vector<OutlineItem> items;
    for (const Node& n : doc.nodes) {
        if (n.block != BlockKind::Heading) continue;
        OutlineItem item;
        item.level = n.level;
        // contentOffset is the heading text after the ATX markers (or the
        // title text start in a setext heading): the position a navigation
        // jump wants to land on.
        item.offset = n.contentOffset;
        for (const InlineBlock& ib : n.children) {
            // Inline spans contribute their plain text. Images contribute
            // nothing (their alt text is not navigation text), every other
            // span kind, text, emphasis, code, links, contributes as-is.
            if (ib.kind == InlineKind::Image) continue;
            item.text.append(ib.text);
        }
        items.push_back(std::move(item));
    }
    return items;
}

std::u32string ConcatenateInlineText(const OutlineItem& item) {
    return item.text;
}

std::vector<int> VisibleItems(const std::vector<OutlineItem>& items,
                              const OutlineCollapse& st) {
    std::vector<int> visible;
    const int n = static_cast<int>(items.size());
    for (int i = 0; i < n; ) {
        visible.push_back(i);
        if (st.IsCollapsed(i)) {
            // Skip the subtree: everything until an item at the same or
            // shallower depth.
            const int lvl = items[i].level;
            ++i;
            while (i < n && items[i].level > lvl) ++i;
        } else {
            ++i;
        }
    }
    return visible;
}

int ItemIndexForOffset(const std::vector<OutlineItem>& items, uint32_t offset) {
    // Section ownership: last item whose offset is <= the given offset.
    int idx = -1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (items[i].offset <= offset) idx = i;
        else break;
    }
    return idx;
}
