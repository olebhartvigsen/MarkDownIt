// Outline model: the heading navigation projection of the document DOM.
// Portable and headless (no windows.h) so it builds and tests under g++.
// The pane keeps no second document copy: it borrows the Document after
// each reparse and derives its item list from the heading nodes.

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
