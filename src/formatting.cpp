#include "formatting.h"

bool IsWrappedIn(const std::string& text, uint32_t start, uint32_t end,
                 const std::string& marker) {
    if (start + marker.size() * 2 > end) return false;
    // Check if text[start..start+m] == marker and text[end-m..end] == marker.
    for (size_t i = 0; i < marker.size(); i++) {
        if (start + i >= text.size() || text[start + i] != marker[i]) return false;
        if (end - marker.size() + i >= text.size() || text[end - marker.size() + i] != marker[i])
            return false;
    }
    return true;
}

void ToggleInlineMarker(TextBuffer* buf, Selection* sel, const std::string& marker) {
    if (sel->Empty()) {
        // Empty selection: insert marker pair and put caret between them.
        uint32_t at = sel->active.offset;
        buf->Splice(at, 0, marker + marker);
        // Caret goes to position after first marker.
        sel->Collapse({at + static_cast<uint32_t>(marker.size())});
        return;
    }

    uint32_t start = sel->Start();
    uint32_t end = start + sel->Length();
    const std::string& text = buf->Text();

    if (IsWrappedIn(text, start, end, marker)) {
        // Remove the markers.
        uint32_t mlen = static_cast<uint32_t>(marker.size());
        // Remove end marker first (so offsets don't shift).
        buf->Splice(end - mlen, mlen, "");
        buf->Splice(start, mlen, "");
        // Adjust selection: the inner text remains, markers removed.
        sel->anchor = {start};
        sel->active = {end - mlen * 2};
    } else {
        // Add the markers around the selection.
        buf->Splice(end, 0, marker);     // insert after end first
        buf->Splice(start, 0, marker);   // then insert before start
        // Selection now covers the same text plus the markers.
        sel->anchor = {start};
        sel->active = {end + static_cast<uint32_t>(marker.size() * 2)};
    }
}

void InsertLink(TextBuffer* buf, Selection* sel, const std::string& url) {
    if (sel->Empty()) {
        // Insert [](url) and place caret between [ and ].
        uint32_t at = sel->active.offset;
        std::string link = "[](" + url + ")";
        buf->Splice(at, 0, link);
        sel->anchor = {at + 1};
        sel->active = {at + 1};
    } else {
        uint32_t start = sel->Start();
        uint32_t end = start + sel->Length();
        // Insert ](url) after end, then [ before start.
        buf->Splice(end, 0, "](" + url + ")");
        buf->Splice(start, 0, "[");
        sel->anchor = {start};
        sel->active = {end + 1 + static_cast<uint32_t>(url.size()) + 3};
    }
}
