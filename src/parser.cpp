// Parser bridge: md4c SAX callbacks -> Document (flat node list).
//
// md4c emits block/span/text events. We push blocks into a flat vector and
// track a small stack for nesting. Inlines (text, em, strong, code, links)
// are appended to the current block's children as InlineBlock structs with
// UTF-32 text.
//
// v1 scope: headings, paragraphs, code blocks, blockquotes, thematic breaks,
// lists (flat, one node per item), and inline spans (em, strong, code, links,
// images). Tables are parsed but stored as paragraphs (Task 10+).

#include "parser.h"

#include <cstring>
#include <stack>
#include <string>
#include <vector>

extern "C" {
#include "md4c.h"
}

namespace {

// Frame on the block stack: which md4c block we are inside, and which node
// index in Document::nodes we are currently filling (-1 for DOC container).
struct Frame {
    MD_BLOCKTYPE type;
    int node_index;
    bool merge_inlines;  // true for LI: inlines go to this node, not a new P
    bool owns_node;      // true if this frame created the node (for offset tracking)
};

// Frame on the span stack: which inline span we are inside, and accumulated
// style flags for the current inline being built.
struct SpanFrame {
    MD_SPANTYPE type;
    std::string url;        // for A/IMG: accumulated from enter_span detail
    bool em;
    bool strong;
    bool code;
};

// Tracks source offset accumulation per node.
struct NodeOffsetInfo {
    uint32_t firstTextOffset = 0;
    uint32_t lastTextEnd = 0;
    bool hasText = false;
};

struct ParserCtx {
    Document* doc;
    std::vector<Frame> block_stack;
    std::vector<SpanFrame> span_stack;
    bool capture_title;  // true when we enter the first H1 and title is empty
    int list_depth;      // current list nesting depth (0 = top level)
    int quote_depth;     // current blockquote nesting depth
    int table_node_idx;  // current table node index, -1 if none
    bool in_header;      // true when in THEAD
    TableRow* cur_row;   // current row being filled, nullptr if none
    std::u32string* cur_cell;  // current cell text, nullptr if none
    const char* input;        // pointer to start of input (for offset calculation)
    MD_SIZE inputSize;        // size of input
    std::vector<NodeOffsetInfo> nodeOffsets;  // per-node offset tracking
};

// Append a new node to the document and return its index.
int push_node(ParserCtx& ctx, Node n) {
    int idx = static_cast<int>(ctx.doc->nodes.size());
    ctx.doc->nodes.push_back(std::move(n));
    ctx.nodeOffsets.push_back(NodeOffsetInfo{});
    return idx;
}

// Get the current node we are adding inlines to (-1 if none).
int current_node(const ParserCtx& ctx) {
    if (ctx.block_stack.empty()) return -1;
    // Find the topmost frame that owns inlines
    for (auto it = ctx.block_stack.rbegin(); it != ctx.block_stack.rend(); ++it) {
        if (it->node_index >= 0) return it->node_index;
    }
    return -1;
}

// UTF-8 to UTF-32 decoder state. Appends decoded code points to out.
// md4c gives us valid UTF-8; this is a simple per-call incremental decoder.
struct Utf8Decoder {
    uint32_t codepoint = 0;
    int pending = 0;  // bytes still expected for the current sequence

    void reset() { codepoint = 0; pending = 0; }

    void decode(const char* data, MD_SIZE size, std::u32string& out) {
        for (MD_SIZE i = 0; i < size; ++i) {
            unsigned char b = static_cast<unsigned char>(data[i]);
            if (pending == 0) {
                if (b < 0x80) {
                    out.push_back(static_cast<char32_t>(b));
                } else if ((b & 0xE0) == 0xC0) {
                    codepoint = (b & 0x1F); pending = 1;
                } else if ((b & 0xF0) == 0xE0) {
                    codepoint = (b & 0x0F); pending = 2;
                } else if ((b & 0xF8) == 0xF0) {
                    codepoint = (b & 0x07); pending = 3;
                } else {
                    out.push_back(static_cast<char32_t>(b));  // invalid, pass through
                }
            } else {
                if ((b & 0xC0) == 0x80) {
                    codepoint = (codepoint << 6) | (b & 0x3F);
                    if (--pending == 0) {
                        out.push_back(static_cast<char32_t>(codepoint));
                    }
                } else {
                    // invalid sequence; flush what we have
                    out.push_back(static_cast<char32_t>(codepoint));
                    out.push_back(static_cast<char32_t>(b));
                    pending = 0;
                }
            }
        }
    }
};

// Decode common HTML entities to UTF-32.
void decode_entity(const char* text, MD_SIZE size, std::u32string& out) {
    // Known named entities (v1 set; md4c passes entity text including the & and ;).
    // md4c gives the entity WITHOUT & and ;? Check: MD_TEXT_ENTITY provides the
    // full entity text. We handle the common ones.
    std::string ent(text, size);
    if (ent == "amp") out.push_back(U'&');
    else if (ent == "lt") out.push_back(U'<');
    else if (ent == "gt") out.push_back(U'>');
    else if (ent == "quot") out.push_back(U'"');
    else if (ent == "apos") out.push_back(U'\'');
    else if (ent == "nbsp") out.push_back(U' ');
    else if (ent == "mdash") out.push_back(U'\u2014');
    else if (ent == "ndash") out.push_back(U'\u2013');
    else if (size > 1 && text[0] == '#') {
        // Numeric entity: &#39; or &#x27;
        if (size > 2 && (text[1] == 'x' || text[1] == 'X')) {
            uint32_t cp = 0;
            for (MD_SIZE i = 2; i < size; ++i) {
                char c = text[i];
                cp <<= 4;
                if (c >= '0' && c <= '9') cp |= (c - '0');
                else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
            }
            out.push_back(static_cast<char32_t>(cp));
        } else {
            uint32_t cp = 0;
            for (MD_SIZE i = 1; i < size; ++i) {
                if (text[i] >= '0' && text[i] <= '9') cp = cp * 10 + (text[i] - '0');
            }
            out.push_back(static_cast<char32_t>(cp));
        }
    } else {
        // Unknown: pass through as literal text (best effort)
        Utf8Decoder d;
        d.decode(text, size, out);
    }
}

// Build a URL string from an MD_ATTRIBUTE (text + size).
std::string attr_to_string(const MD_ATTRIBUTE& attr) {
    if (attr.text == nullptr || attr.size == 0) return {};
    return std::string(attr.text, attr.size);
}

// Find the start of the line containing the given byte offset.
uint32_t BlockLineStart(const char* input, uint32_t offset) {
    while (offset > 0 && input[offset - 1] != '\n') {
        offset--;
    }
    return offset;
}

// --- md4c callbacks ---

int cb_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserCtx*>(userdata);

    switch (type) {
        case MD_BLOCK_DOC:
            ctx->block_stack.push_back({type, -1, false, false});
            break;

        case MD_BLOCK_P: {
            // If inside a LI with merge_inlines, reuse the LI node.
            if (!ctx->block_stack.empty() && ctx->block_stack.back().merge_inlines) {
                int li_idx = ctx->block_stack.back().node_index;
                ctx->block_stack.push_back({type, li_idx, false, false});
            } else {
                int idx = push_node(*ctx, Node{});
                ctx->doc->nodes[idx].block = BlockKind::Paragraph;
                ctx->block_stack.push_back({type, idx, false, true});
            }
            break;
        }

        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::Heading;
            ctx->doc->nodes[idx].level = static_cast<int>(h->level);
            // Capture title from first H1 if title is empty
            if (h->level == 1 && ctx->doc->title.empty()) {
                ctx->capture_title = true;
            }
            ctx->block_stack.push_back({type, idx, false, true});
            break;
        }

        case MD_BLOCK_CODE: {
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::CodeBlock;
            ctx->block_stack.push_back({type, idx, false, true});
            break;
        }

        case MD_BLOCK_QUOTE: {
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::BlockQuote;
            ctx->doc->nodes[idx].depth = ctx->quote_depth;
            ctx->quote_depth++;
            ctx->block_stack.push_back({type, idx, true, true});
            break;
        }

        case MD_BLOCK_HR: {
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::ThematicBreak;
            ctx->block_stack.push_back({type, idx, false, true});
            break;
        }

        case MD_BLOCK_UL:
        case MD_BLOCK_OL: {
            // The list container does NOT create a renderable node.
            // Only LI nodes are rendered. Track ordered flag on the stack.
            ctx->list_depth++;
            ctx->block_stack.push_back({type, -1, false, false});
            break;
        }

        case MD_BLOCK_LI: {
            // Each list item: push a node so the renderer draws a bullet/number.
            // Reuse the parent list's ordered flag.
            bool ordered = false;
            for (auto it = ctx->block_stack.rbegin(); it != ctx->block_stack.rend(); ++it) {
                if (it->type == MD_BLOCK_OL) { ordered = true; break; }
                if (it->type == MD_BLOCK_UL) { ordered = false; break; }
            }
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::List;
            ctx->doc->nodes[idx].ordered = ordered;
            ctx->doc->nodes[idx].depth = ctx->list_depth - 1;
            // merge_inlines: subsequent P inside this LI adds to this node
            ctx->block_stack.push_back({type, idx, true, true});
            break;
        }

        case MD_BLOCK_TABLE: {
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::Table;
            ctx->table_node_idx = idx;
            ctx->block_stack.push_back({type, idx, false, true});
            break;
        }
        case MD_BLOCK_THEAD:
            ctx->in_header = true;
            ctx->block_stack.push_back({type, -1, false, false});
            break;
        case MD_BLOCK_TBODY:
            ctx->in_header = false;
            ctx->block_stack.push_back({type, -1, false, false});
            break;
        case MD_BLOCK_TR: {
            if (ctx->table_node_idx >= 0) {
                ctx->doc->nodes[ctx->table_node_idx].rows.push_back(TableRow{});
                ctx->cur_row = &ctx->doc->nodes[ctx->table_node_idx].rows.back();
            }
            ctx->block_stack.push_back({type, -1, false, false});
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            if (ctx->cur_row) {
                ctx->cur_row->cells.push_back(TableCell{});
                ctx->cur_row->cells.back().isHeader = ctx->in_header;
                ctx->cur_cell = &ctx->cur_row->cells.back().text;
            }
            ctx->block_stack.push_back({type, -1, false, false});
            break;
        }
        default:
            // HTML, detail structs we do not use.
            ctx->block_stack.push_back({type, -1, false, false});
            break;
    }
    return 0;
}

int cb_leave_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserCtx*>(userdata);
    // Compute source offsets for the leaving block before popping the frame.
    if (!ctx->block_stack.empty()) {
        Frame& frame = ctx->block_stack.back();
        if (frame.owns_node && frame.node_index >= 0) {
            auto& noi = ctx->nodeOffsets[frame.node_index];
            if (noi.hasText) {
                Node& node = ctx->doc->nodes[frame.node_index];
                node.srcOffset = BlockLineStart(ctx->input, noi.firstTextOffset);
                node.srcLength = noi.lastTextEnd - node.srcOffset;
                node.contentOffset = noi.firstTextOffset;
            }
        }
        ctx->block_stack.pop_back();
    }
    if (type == MD_BLOCK_H) {
        ctx->capture_title = false;
    }
    if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
        if (ctx->list_depth > 0) ctx->list_depth--;
    }
    if (type == MD_BLOCK_QUOTE) {
        if (ctx->quote_depth > 0) ctx->quote_depth--;
    }
    if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
        ctx->cur_cell = nullptr;
    }
    if (type == MD_BLOCK_TR) {
        ctx->cur_row = nullptr;
    }
    if (type == MD_BLOCK_THEAD) {
        ctx->in_header = false;
    }
    if (type == MD_BLOCK_TABLE) {
        ctx->table_node_idx = -1;
    }
    return 0;
}

int cb_enter_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserCtx*>(userdata);

    SpanFrame frame{};
    frame.type = type;

    switch (type) {
        case MD_SPAN_EM:
            frame.em = true;
            break;
        case MD_SPAN_STRONG:
            frame.strong = true;
            break;
        case MD_SPAN_CODE:
            frame.code = true;
            break;
        case MD_SPAN_A: {
            auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            frame.url = attr_to_string(a->href);
            break;
        }
        case MD_SPAN_IMG: {
            auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            frame.url = attr_to_string(img->src);
            break;
        }
        case MD_SPAN_DEL:
            break;
        default:
            break;
    }

    ctx->span_stack.push_back(std::move(frame));
    return 0;
}

int cb_leave_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserCtx*>(userdata);
    if (!ctx->span_stack.empty()) {
        ctx->span_stack.pop_back();
    }
    return 0;
}

int cb_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* ctx = static_cast<ParserCtx*>(userdata);
    if (size == 0) return 0;

    int idx = current_node(*ctx);
    if (idx < 0) return 0;

    Node& node = ctx->doc->nodes[idx];

    // Track source offsets for this node.
    if (ctx->input) {
        uint32_t off = static_cast<uint32_t>(text - ctx->input);
        auto& noi = ctx->nodeOffsets[idx];
        if (!noi.hasText) {
            noi.firstTextOffset = off;
            noi.hasText = true;
        }
        noi.lastTextEnd = off + static_cast<uint32_t>(size);
    }

    // Table cells: raw text goes to cur_cell (UTF-32).
    if (ctx->cur_cell) {
        Utf8Decoder d;
        d.decode(text, size, *ctx->cur_cell);
        return 0;
    }

    // Code blocks: raw text goes to node.raw (no inline spans in code).
    if (!ctx->block_stack.empty() && ctx->block_stack.back().type == MD_BLOCK_CODE) {
        Utf8Decoder d;
        d.decode(text, size, node.raw);
        return 0;
    }

    // Build the inline text (UTF-32).
    std::u32string text32;
    switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE: {
            Utf8Decoder d;
            d.decode(text, size, text32);
            break;
        }
        case MD_TEXT_ENTITY:
            decode_entity(text, size, text32);
            break;
        case MD_TEXT_BR:
            text32.push_back(U'\n');
            break;
        case MD_TEXT_SOFTBR:
            text32.push_back(U' ');
            break;
        case MD_TEXT_NULLCHAR:
            // Replace NUL with Unicode replacement char to avoid C-string issues
            text32.push_back(U'\uFFFD');
            break;
        default:
            // HTML, LATEXMATH: pass through as text for v1
        {
            Utf8Decoder d;
            d.decode(text, size, text32);
            break;
        }
    }

    if (text32.empty()) return 0;

    // Determine inline style from span stack.
    bool em = false, strong = false, code = false, strike = false;
    InlineKind kind = InlineKind::Text;
    std::string url;
    for (const auto& sf : ctx->span_stack) {
        if (sf.em) em = true;
        if (sf.strong) strong = true;
        if (sf.code) code = true;
        if (sf.type == MD_SPAN_A) { kind = InlineKind::Link; url = sf.url; }
        if (sf.type == MD_SPAN_IMG) { kind = InlineKind::Image; url = sf.url; }
    }

    InlineBlock ib;
    ib.kind = kind;
    ib.text = std::move(text32);
    ib.url = std::move(url);
    ib.em = em;
    ib.strong = strong;
    ib.code = code;
ib.strike = strike;

    // Set source offset for this inline span.
    if (ctx->input) {
        ib.srcOffset = static_cast<uint32_t>(text - ctx->input);
        ib.srcLength = static_cast<uint32_t>(size);
    }

    // Capture title: if we are inside the first H1, append to doc.title.
    // Must do this BEFORE the std::move(ib) below, otherwise ib.text is moved-from.
    if (ctx->capture_title) {
        ctx->doc->title += ib.text;
    }

    node.children.push_back(std::move(ib));

    return 0;
}

}  // namespace

bool ParseMarkdown(const std::string& utf8, Document& out) {
    ParserCtx ctx;
    ctx.doc = &out;
    ctx.input = utf8.data();
    ctx.inputSize = static_cast<MD_SIZE>(utf8.size());
    ctx.capture_title = false;
    ctx.list_depth = 0;
    ctx.quote_depth = 0;
    ctx.table_node_idx = -1;
    ctx.in_header = false;
    ctx.cur_row = nullptr;
    ctx.cur_cell = nullptr;

    MD_PARSER parser{};
    parser.abi_version = 0;
    // Enable common extensions for good markdown coverage.
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                   MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS |
                   MD_FLAG_PERMISSIVEEMAILAUTOLINKS |
                   MD_FLAG_PERMISSIVEWWWAUTOLINKS;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span = cb_enter_span;
    parser.leave_span = cb_leave_span;
    parser.text = cb_text;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    int rc = md_parse(utf8.data(), static_cast<MD_SIZE>(utf8.size()),
                      &parser, &ctx);
    return rc == 0;
}
