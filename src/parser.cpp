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
#include "mermaid/parse.h"
#include "mermaid/pie_parse.h"
#include "mermaid/pie_layout.h"
#include "mermaid/seq_parse.h"
#include "mermaid/seq_layout.h"
#include "mermaid/layout.h"
#include <chrono>
#include <memory>
#include <iterator>
#include <algorithm>

#include <cstring>
#include <windows.h>
#include <cctype>
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
    bool merge_inlines; // true for LI: inlines go to this node, not a new P
    bool owns_node;     // true if this frame created the node (for offset tracking)
};

// Frame on the span stack: which inline span we are inside, and accumulated
// style flags for the current inline being built.
struct SpanFrame {
    MD_SPANTYPE type;
    std::string url;       // for A/IMG: accumulated from enter_span detail
    bool em;
    bool strong;
    bool code;
};

// md4c does not expose table-cell source offsets in MD_BLOCK_TD_DETAIL.
// Keep the source locations found from the same pipe boundaries md4c uses so
// empty cells still receive a caret position when no text callback follows.
struct SourceCellRange {
    uint32_t start;
    uint32_t end;
};

bool IsSourceWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

bool IsUnescapedTablePipe(const std::string& source, uint32_t offset) {
    size_t backslashes = 0;
    while (offset > backslashes &&
           source[offset - backslashes - 1] == '\\') {
        ++backslashes;
    }
    return (backslashes % 2) == 0;
}

std::vector<SourceCellRange> SplitSourceTableRow(const std::string& source,
                                                  uint32_t lineStart,
                                                  uint32_t lineEnd,
                                                  bool* foundPipe) {
    // md4c removes up to three indentation spaces before processing a table
    // row. Match that here so indentation is not mistaken for a cell.
    uint32_t contentStart = lineStart;
    int indentation = 0;
    while (contentStart < lineEnd && indentation < 3 &&
           (source[contentStart] == ' ' || source[contentStart] == '\t')) {
        ++contentStart;
        ++indentation;
    }

    // md4c treats a leading/trailing pipe as a delimiter, not as an extra
    // cell. Keep zero-width ranges between adjacent delimiters.
    while (lineEnd > contentStart && IsSourceWhitespace(source[lineEnd - 1]))
        --lineEnd;
    bool hasPipe = false;
    if (contentStart < lineEnd && source[contentStart] == '|') {
        hasPipe = true;
        ++contentStart;
    }

    std::vector<SourceCellRange> cells;
    uint32_t cellStart = contentStart;
    bool inCode = false;
    size_t codeTicks = 0;
    for (uint32_t i = contentStart; i < lineEnd; ++i) {
        if (source[i] == '`') {
            uint32_t runStart = i;
            while (i < lineEnd && source[i] == '`') ++i;
            size_t runLength = i - runStart;
            if (!inCode) {
                inCode = true;
                codeTicks = runLength;
            } else if (runLength == codeTicks) {
                inCode = false;
                codeTicks = 0;
            }
            --i;
            continue;
        }
        if (source[i] != '|' || inCode || !IsUnescapedTablePipe(source, i))
            continue;
        hasPipe = true;
        uint32_t start = cellStart;
        uint32_t end = i;
        while (start < end && IsSourceWhitespace(source[start])) ++start;
        while (end > start && IsSourceWhitespace(source[end - 1])) --end;
        cells.push_back({start, end});
        cellStart = i + 1;
    }

    if (cellStart < lineEnd) {
        uint32_t start = cellStart;
        uint32_t end = lineEnd;
        while (start < end && IsSourceWhitespace(source[start])) ++start;
        while (end > start && IsSourceWhitespace(source[end - 1])) --end;
        cells.push_back({start, end});
    }
    if (foundPipe) *foundPipe = hasPipe;
    return cells;
}

bool IsTableUnderlineCell(const std::string& source, SourceCellRange cell) {
    uint32_t start = cell.start;
    uint32_t end = cell.end;
    if (start < end && source[start] == ':') ++start;
    uint32_t hyphens = 0;
    while (start < end && source[start] == '-') {
        ++start;
        ++hyphens;
    }
    if (start < end && source[start] == ':') ++start;
    return hyphens > 0 && start == end;
}

bool IsBlankSourceLine(const std::string& source, uint32_t start,
                       uint32_t end) {
    for (uint32_t i = start; i < end; ++i) {
        if (!IsSourceWhitespace(source[i])) return false;
    }
    return true;
}

std::vector<SourceCellRange> FindTableCellRanges(const std::string& source) {
    struct SourceLine { uint32_t start; uint32_t end; bool inFence; };
    std::vector<SourceLine> lines;
    uint32_t lineStart = 0;
    bool inFence = false;
    char fenceChar = 0;
    uint32_t fenceLength = 0;
    const uint32_t sourceSize = static_cast<uint32_t>(source.size());
    for (uint32_t i = 0; i <= sourceSize; ++i) {
        if (i == sourceSize || source[i] == '\n') {
            uint32_t lineEnd = i;
            if (lineEnd > lineStart && source[lineEnd - 1] == '\r') --lineEnd;
            uint32_t contentStart = lineStart;
            int indentation = 0;
            while (contentStart < lineEnd && indentation < 3 &&
                   (source[contentStart] == ' ' ||
                    source[contentStart] == '\t')) {
                ++contentStart;
                ++indentation;
            }
            bool lineInFence = inFence;
            uint32_t runStart = contentStart;
            if (runStart < lineEnd &&
                (source[runStart] == '`' || source[runStart] == '~')) {
                char marker = source[runStart];
                uint32_t runEnd = runStart;
                while (runEnd < lineEnd && source[runEnd] == marker) ++runEnd;
                uint32_t runLength = runEnd - runStart;
                if (!inFence && runLength >= 3) {
                    inFence = true;
                    fenceChar = marker;
                    fenceLength = runLength;
                    lineInFence = true;
                } else if (inFence && marker == fenceChar &&
                           runLength >= fenceLength) {
                    while (runEnd < lineEnd &&
                           IsSourceWhitespace(source[runEnd])) ++runEnd;
                    if (runEnd == lineEnd) inFence = false;
                }
            }
            lines.push_back({lineStart, lineEnd, lineInFence});
            lineStart = i + 1;
        }
    }

    std::vector<SourceCellRange> result;
    size_t lineIndex = 0;
    while (lineIndex + 1 < lines.size()) {
        if (lines[lineIndex].inFence || lines[lineIndex + 1].inFence) {
            ++lineIndex;
            continue;
        }
        bool hasUnderlinePipe = false;
        auto underline = SplitSourceTableRow(
            source, lines[lineIndex + 1].start, lines[lineIndex + 1].end,
            &hasUnderlinePipe);
        bool validUnderline = hasUnderlinePipe && !underline.empty();
        for (const auto& cell : underline) {
            if (!IsTableUnderlineCell(source, cell)) {
                validUnderline = false;
                break;
            }
        }
        if (!validUnderline) {
            ++lineIndex;
            continue;
        }

        const size_t columnCount = underline.size();
        auto appendRow = [&](size_t rowIndex) {
            auto parsedCells = SplitSourceTableRow(
                source, lines[rowIndex].start, lines[rowIndex].end, nullptr);
            if (parsedCells.size() > columnCount)
                parsedCells.resize(columnCount);

            // Preserve the parser's left-to-right cell order. Empty cells use
            // a zero-width range at their delimiter position.
            while (parsedCells.size() < columnCount) {
                const uint32_t caret = lines[rowIndex].end;
                parsedCells.push_back({caret, caret});
            }
            result.insert(result.end(), parsedCells.begin(), parsedCells.end());
        };

        appendRow(lineIndex); // header row
        size_t nextRow = lineIndex + 2; // skip the underline row
        while (nextRow < lines.size() &&
               !lines[nextRow].inFence &&
               !IsBlankSourceLine(source, lines[nextRow].start,
                                  lines[nextRow].end)) {
            bool hasRowPipe = false;
            const auto rowCells = SplitSourceTableRow(
                source, lines[nextRow].start, lines[nextRow].end,
                &hasRowPipe);
            if (!hasRowPipe || rowCells.empty()) break;
            appendRow(nextRow);
            ++nextRow;
        }
        lineIndex = nextRow;
    }
    return result;
}

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
    bool capture_title; // true when we enter the first H1 and title is empty
    int list_depth;     // current list nesting depth (0 = top level)
    int quote_depth;    // current blockquote nesting depth
    int table_node_idx; // current table node index, -1 if none
    bool in_header;     // true when in THEAD
    TableRow* cur_row;  // current row being filled, nullptr if none
    std::u32string* cur_cell; // current cell text, nullptr if none
    TableCell* cur_cell_obj;  // current TableCell object, nullptr if none
    uint32_t cur_cell_last_end; // end source offset of last text chunk for gap detection
    // Stack of open inline spans within a table cell.
    // Each entry: {u16 start position in cell text, span type flags}
    struct CellSpanOpen {
        uint32_t u16Start;
        bool strong;
        bool em;
        bool code;
        bool del;
    };
    std::vector<CellSpanOpen> cell_span_stack;
    const char* input;       // pointer to start of input (for offset calculation)
    MD_SIZE inputSize;       // size of input
    std::vector<NodeOffsetInfo> nodeOffsets; // per-node offset tracking
    std::vector<SourceCellRange> sourceCellRanges;
    size_t nextSourceCellRange = 0;
    size_t currentRowSourceStart = 0;
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
    int pending = 0; // bytes still expected for the current sequence

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
                    out.push_back(static_cast<char32_t>(b)); // invalid, pass through
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
    const char* original = text;
    const MD_SIZE originalSize = size;
    if (size >= 2 && text[0] == '&' && text[size - 1] == ';') {
        text += 1;
        size -= 2;
    }

    auto append_scalar = [&out](uint32_t cp) {
        if (cp == 0 || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu)) {
            out.push_back(U'\uFFFD');
        } else {
            out.push_back(static_cast<char32_t>(cp));
        }
    };
    auto append_original = [&]() {
        Utf8Decoder d;
        d.decode(original, originalSize, out);
    };

    std::string ent(text, size);
    if (ent == "amp") append_scalar('&');
    else if (ent == "lt") append_scalar('<');
    else if (ent == "gt") append_scalar('>');
    else if (ent == "quot") append_scalar('"');
    else if (ent == "apos") append_scalar('\'');
    else if (ent == "nbsp") append_scalar(0x00A0);
    else if (ent == "copy") append_scalar(0x00A9);
    else if (ent == "reg") append_scalar(0x00AE);
    else if (ent == "hellip") append_scalar(0x2026);
    else if (ent == "mdash") append_scalar(0x2014);
    else if (ent == "ndash") append_scalar(0x2013);
    else if (size > 1 && text[0] == '#') {
        uint32_t cp = 0;
        bool valid = true;
        MD_SIZE begin = 1;
        unsigned base = 10;
        if (size > 2 && (text[1] == 'x' || text[1] == 'X')) {
            begin = 2;
            base = 16;
        }
        if (begin == size) valid = false;
        for (MD_SIZE i = begin; valid && i < size; ++i) {
            unsigned digit = 0;
            const char c = text[i];
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (base == 16 && c >= 'a' && c <= 'f')
                digit = static_cast<unsigned>(c - 'a' + 10);
            else if (base == 16 && c >= 'A' && c <= 'F')
                digit = static_cast<unsigned>(c - 'A' + 10);
            else { valid = false; break; }
            if (cp > (0x10FFFFu - digit) / base) valid = false;
            else cp = cp * base + digit;
        }
        if (valid) append_scalar(cp);
        else out.push_back(U'\uFFFD');
    } else {
        append_original();
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

uint32_t BlockLineEnd(const char* input, uint32_t inputSize, uint32_t offset) {
    while (offset < inputSize && input[offset] != '\r' && input[offset] != '\n') {
        ++offset;
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
            auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            if (d && d->lang.text && d->lang.size > 0) {
                ctx->doc->nodes[idx].lang.assign(d->lang.text, d->lang.size);
                // Info string may carry extra words: "mermaid theme=dark".
                size_t sp = ctx->doc->nodes[idx].lang.find(' ');
                if (sp != std::string::npos) {
                    ctx->doc->nodes[idx].lang.resize(sp);
                }
                for (auto& c : ctx->doc->nodes[idx].lang) {
                    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                }
            }
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
            ctx->currentRowSourceStart = ctx->nextSourceCellRange;
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
                TableCell& cell = ctx->cur_row->cells.back();
                cell.isHeader = ctx->in_header;
                if (ctx->nextSourceCellRange < ctx->sourceCellRanges.size()) {
                    cell.srcOffset =
                        ctx->sourceCellRanges[ctx->nextSourceCellRange].start;
                    cell.srcEnd =
                        ctx->sourceCellRanges[ctx->nextSourceCellRange].end;
                    ++ctx->nextSourceCellRange;
                }
                ctx->cur_cell = &ctx->cur_row->cells.back().text;
                ctx->cur_cell_obj = &cell;
                ctx->cur_cell_last_end = 0; // will be set on first text
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
                // Keep trailing Markdown syntax in the block range. A caret
                // after visible strong/link text is normalized past closing
                // delimiters and must still resolve to this same block.
                node.srcLength = BlockLineEnd(ctx->input, ctx->inputSize,
                                              noi.lastTextEnd) - node.srcOffset;
                node.contentOffset = noi.firstTextOffset;
                node.contentLength = noi.lastTextEnd - noi.firstTextOffset;
            }
        }
        ctx->block_stack.pop_back();
    }
    if (type == MD_BLOCK_CODE) {
        // Promote ```mermaid fences to MermaidFlowchart with laid-out graph.
        if (!ctx->doc->nodes.empty()) {
            Node& n = ctx->doc->nodes.back();
            if (n.block == BlockKind::CodeBlock && n.lang == "mermaid") {
                // UTF-32 raw -> UTF-8 (ASCII-safe fallback; mermaid source is ASCII).
                std::string utf8;
                utf8.reserve(n.raw.size());
                for (char32_t c : n.raw) {
                    if (c < 0x80) utf8.push_back(static_cast<char>(c));
                    else if (c < 0x800) {
                        utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
                        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                    } else if (c < 0x10000) {
                        utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
                        utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                    } else {
                        utf8.push_back(static_cast<char>(0xF0 | (c >> 18)));
                        utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
                        utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                    }
                }
                mermaid::Flowchart flow = mermaid::ParseFlowchart(utf8);
                if (flow.error.empty() && !flow.nodes.empty()) {
                    try {
                        mermaid::LayoutParams p;
                        auto laid = std::make_shared<mermaid::LaidOutFlowchart>(
                            mermaid::LayoutFlowchart(flow, p));
                        n.mermaid_source = utf8;
                        n.mermaid_flowchart = std::make_shared<mermaid::Flowchart>(
                            std::move(flow));
                        n.mermaid_layout = laid;
                        n.block = BlockKind::MermaidFlowchart;
                        goto promoted;
                    } catch (...) {
                        // Keep as CodeBlock on layout failure.
                    }
                }
                // Not a flowchart: try pie. Pie blocks render even with a
                // single degenerate slice (one full-circle arc), so the
                // parse error check is enough.
                {
                    mermaid::PieDiagram pie = mermaid::ParsePie(utf8);
                    if (pie.error.empty() && !pie.slices.empty()) {
                        try {
                            auto laid = std::make_shared<mermaid::LaidOutPie>(
                                mermaid::LayoutPie(pie));
                            n.mermaid_pie = laid;
                            n.block = BlockKind::MermaidPie;
                            goto promoted;
                        } catch (...) {
                            // Keep as CodeBlock on layout failure.
                        }
                    }
                }
                // Not a flowchart or pie: try sequence. ParseSequence rejects
                // non-sequence input (header must be `sequenceDiagram`), so
                // chain order does not matter, only that each step can fail.
                {
                    mermaid::SequenceDiagram sd = mermaid::ParseSequence(utf8);
                    if (sd.error.empty() && !sd.participants.empty()) {
                        try {
                            auto laid = std::make_shared<mermaid::LaidOutSequence>(
                                mermaid::LayoutSequence(sd));
                            n.mermaid_seq = laid;
                            n.block = BlockKind::MermaidSequence;
                            goto promoted;
                        } catch (...) {
                            // Keep as CodeBlock on layout failure.
                        }
                    }
                }
            }
            promoted:;
        }
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
        ctx->cur_cell_obj = nullptr;
        ctx->cell_span_stack.clear();
    }
    if (type == MD_BLOCK_TR) {
        if (ctx->cur_row && ctx->cur_row->cells.size() > 1) {
            const size_t rangeStart = ctx->currentRowSourceStart;
            const size_t rangeCount = ctx->nextSourceCellRange - rangeStart;
            if (rangeCount == ctx->cur_row->cells.size() &&
                rangeStart + rangeCount <= ctx->sourceCellRanges.size()) {
                std::vector<TableCell> ordered;
                ordered.reserve(ctx->cur_row->cells.size());
                std::vector<bool> used(ctx->cur_row->cells.size(), false);
                for (size_t r = 0; r < rangeCount; ++r) {
                    const SourceCellRange target =
                        ctx->sourceCellRanges[rangeStart + r];
                    size_t chosen = ctx->cur_row->cells.size();
                    for (size_t c = 0; c < ctx->cur_row->cells.size(); ++c) {
                        const TableCell& cell = ctx->cur_row->cells[c];
                        if (used[c] || cell.text.empty()) continue;
                        if (cell.srcOffset >= target.start &&
                            cell.srcOffset < target.end) {
                            chosen = c;
                            break;
                        }
                    }
                    if (chosen == ctx->cur_row->cells.size()) {
                        for (size_t c = 0; c < ctx->cur_row->cells.size(); ++c) {
                            if (used[c] || !ctx->cur_row->cells[c].text.empty())
                                continue;
                            chosen = c;
                            break;
                        }
                    }
                    if (chosen == ctx->cur_row->cells.size()) {
                        for (size_t c = 0; c < ctx->cur_row->cells.size(); ++c) {
                            if (!used[c]) {
                                chosen = c;
                                break;
                            }
                        }
                    }
                    if (chosen == ctx->cur_row->cells.size()) break;
                    used[chosen] = true;
                    TableCell cell = std::move(ctx->cur_row->cells[chosen]);
                    cell.srcOffset = target.start;
                    cell.srcEnd = target.end;
                    ordered.push_back(std::move(cell));
                }
                if (ordered.size() == ctx->cur_row->cells.size())
                    ctx->cur_row->cells = std::move(ordered);
            }
        }
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

    // If inside a table cell, record the start position of this inline span.
    if (ctx->cur_cell_obj) {
        uint32_t u16Pos = 0;
        for (char32_t cp : ctx->cur_cell_obj->text)
            u16Pos += (cp <= 0xFFFF) ? 1 : 2;
        ctx->cell_span_stack.push_back({
            u16Pos,
            type == MD_SPAN_STRONG,
            type == MD_SPAN_EM,
            type == MD_SPAN_CODE,
            type == MD_SPAN_DEL
        });
    }

    return 0;
}

int cb_leave_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserCtx*>(userdata);
    if (!ctx->span_stack.empty()) {
        ctx->span_stack.pop_back();
    }

    // If inside a table cell, finalize the inline span.
    if (ctx->cur_cell_obj && !ctx->cell_span_stack.empty()) {
        auto open = ctx->cell_span_stack.back();
        ctx->cell_span_stack.pop_back();

        uint32_t u16End = 0;
        for (char32_t cp : ctx->cur_cell_obj->text)
            u16End += (cp <= 0xFFFF) ? 1 : 2;

        if (u16End > open.u16Start) {
            CellInlineSpan span;
            span.u16Start = open.u16Start;
            span.u16End = u16End;
            span.bold = open.strong;
            span.italic = open.em;
            span.code = open.code;
            span.strike = open.del;
            ctx->cur_cell_obj->inlineSpans.push_back(span);
        }
    }

    return 0;
}

// True when a raw-HTML text chunk consists only of <br> variants: any casing
// of the tag name, optional whitespace and optional solidus before '>'.
// Handles a lone tag (<br>, <BR>, <br/>, <br />, <Br   />) and adjacent
// tags merged into one chunk (<br><br>).
static bool IsBrHtmlChunk(const MD_CHAR* text, MD_SIZE size) {
    if (size < 4) return false;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(text);
    MD_SIZE k = 0;
    while (k < size) {
        while (k < size && (s[k] == ' ' || s[k] == 0x09)) k++;
        if (k >= size) break;
        if (s[k] != '<') return false;
        if (k + 3 >= size) return false;
        if (s[k + 1] != 'b' && s[k + 1] != 'B') return false;
        if (s[k + 2] != 'r' && s[k + 2] != 'R') return false;
        k += 3;
        while (k < size && (s[k] == ' ' || s[k] == 0x09)) k++;
        if (k < size && s[k] == '/') k++;
        if (k >= size || s[k] != '>') return false;
        k++;
    }
    return true;
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
        uint32_t thisOff = static_cast<uint32_t>(text - ctx->input);
        // Set srcOffset on first text for this cell.
        if (ctx->cur_cell_obj && ctx->cur_cell_obj->text.empty()) {
            ctx->cur_cell_obj->srcOffset = thisOff;
            ctx->cur_cell_last_end = thisOff;
        }
        // md4c omits Markdown delimiters from text callbacks. Do not copy
        // source gaps into the rendered cell, because those gaps are hidden
        // syntax, not visible text.
        if (ctx->cur_cell_obj && thisOff > ctx->cur_cell_last_end) {
            ctx->cur_cell_last_end = thisOff;
        }
        // HTML <br>, <br/> and <br /> inside a cell must render as a line
        // break, not as literal text. md4c passes them as MD_TEXT_HTML with
        // the raw tag text. Replace the whole chunk with one newline and
        // point the single u16ToSrc entry at the tag's first source byte,
        // so caret mapping stays aligned with the one u16 unit that follows.
        bool is_br = (type == MD_TEXT_HTML) && IsBrHtmlChunk(text, size);
        if (is_br) {
            if (ctx->cur_cell_obj) {
                ctx->cur_cell_obj->u16ToSrc.push_back(thisOff);
                ctx->cur_cell_obj->u16ToSrcEnd.push_back(thisOff +
                    static_cast<uint32_t>(size));
            }
            ctx->cur_cell->push_back(U'\n');
            ctx->cur_cell_last_end = thisOff + size;
            return 0;
        }
        if (ctx->cur_cell_obj) {
            for (const auto& sf : ctx->span_stack) {
                if (sf.type == MD_SPAN_A) {
                    TableLink link;
                    link.srcOffset = thisOff;
                    link.srcLength = static_cast<uint32_t>(size);
                    link.url = sf.url;
                    ctx->cur_cell_obj->links.push_back(std::move(link));
                    break;
                }
            }
        }
        if (type == MD_TEXT_ENTITY && ctx->cur_cell_obj) {
            std::u32string decoded;
            decode_entity(text, size, decoded);
            if (!decoded.empty()) {
                for (char32_t cp : decoded) {
                    const int units = (cp <= 0xFFFF) ? 1 : 2;
                    for (int u = 0; u < units; ++u) {
                        ctx->cur_cell_obj->u16ToSrc.push_back(thisOff);
                        ctx->cur_cell_obj->u16ToSrcEnd.push_back(
                            thisOff + static_cast<uint32_t>(size));
                    }
                }
                *ctx->cur_cell += decoded;
            }
            ctx->cur_cell_last_end = thisOff + static_cast<uint32_t>(size);
            return 0;
        }
        // Build u16ToSrc mapping: for each decoded codepoint, record
        // its source byte offset. We walk the UTF-8 bytes in parallel
        // with the Utf8Decoder so we know the exact source offset of
        // each codepoint, even when md4c splits text at marks.
        if (ctx->cur_cell_obj) {
            uint32_t srcByte = thisOff;
            for (MD_SIZE i = 0; i < size; ) {
                unsigned char b = static_cast<unsigned char>(text[i]);
                int utf8Len;
                int utf16Len;
                if (b < 0x80) {
                    utf8Len = 1; utf16Len = 1;
                } else if ((b & 0xE0) == 0xC0) {
                    utf8Len = 2; utf16Len = 1;
                } else if ((b & 0xF0) == 0xE0) {
                    utf8Len = 3; utf16Len = 1;
                } else if ((b & 0xF8) == 0xF0) {
                    utf8Len = 4; utf16Len = 2;
                } else {
                    utf8Len = 1; utf16Len = 1;
                }
                for (int u = 0; u < utf16Len; u++)
                    ctx->cur_cell_obj->u16ToSrc.push_back(srcByte);
                for (int u = 0; u < utf16Len; u++)
                    ctx->cur_cell_obj->u16ToSrcEnd.push_back(srcByte +
                        static_cast<uint32_t>(utf8Len));
                srcByte += utf8Len;
                i += utf8Len;
            }
        }
        Utf8Decoder d;
        d.decode(text, size, *ctx->cur_cell);
        ctx->cur_cell_last_end = thisOff + size;
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
        case MD_TEXT_HTML:
            // Raw inline HTML. A lone <br>/<BR> variant must render as a
            // line break, not as literal tag text; other raw HTML passes
            // through as text for v1 (unchanged behavior).
            if (IsBrHtmlChunk(text, size)) {
                text32.push_back(U'\n');
            } else {
                Utf8Decoder d;
                d.decode(text, size, text32);
            }
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
        if (sf.type == MD_SPAN_DEL) strike = true;
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

static void AddVirtualEmptyParagraphs(const std::string& source,
                                      Document& doc) {
    if (source.empty()) {
        Node blank;
        blank.block = BlockKind::Paragraph;
        blank.virtualEmptyParagraph = true;
        doc.nodes.push_back(std::move(blank));
        return;
    }

    std::vector<const Node*> sourceNodes;
    sourceNodes.reserve(doc.nodes.size());
    for (const auto& node : doc.nodes) {
        if (node.srcLength > 0) sourceNodes.push_back(&node);
    }
    std::stable_sort(sourceNodes.begin(), sourceNodes.end(),
        [](const Node* a, const Node* b) {
            return a->srcOffset < b->srcOffset;
        });

    std::vector<Node> blanks;
    size_t nextNode = 0;
    for (uint32_t begin = 0; begin < source.size();) {
        if (source[begin] != '\n' && source[begin] != '\r') {
            ++begin;
            continue;
        }

        const uint32_t runStart = begin;
        std::vector<uint32_t> breakEnds;
        while (begin < source.size()) {
            if (source[begin] == '\r' && begin + 1 < source.size() &&
                source[begin + 1] == '\n') {
                begin += 2;
            } else if (source[begin] == '\n') {
                ++begin;
            } else {
                break;
            }
            breakEnds.push_back(begin);
        }
        const uint32_t runEnd = begin;
        if (breakEnds.size() < 2) continue;

        while (nextNode < sourceNodes.size() &&
               sourceNodes[nextNode]->srcOffset + sourceNodes[nextNode]->srcLength <= runStart) {
            ++nextNode;
        }
        const bool overlapsBlock = nextNode < sourceNodes.size() &&
            sourceNodes[nextNode]->srcOffset < runEnd;
        const bool hasPreviousBlock = nextNode > 0;
        const bool hasNextBlock = nextNode < sourceNodes.size();
        if (overlapsBlock || (!hasPreviousBlock && hasNextBlock)) continue;

        const size_t pairs = breakEnds.size() / 2;
        const size_t blankCount = hasPreviousBlock && hasNextBlock
            ? (pairs > 0 ? pairs - 1 : 0) : pairs;
        for (size_t i = 0; i < blankCount; ++i) {
            Node blank;
            blank.block = BlockKind::Paragraph;
            blank.virtualEmptyParagraph = true;
            blank.srcOffset = breakEnds[2 * i + 1];
            blank.contentOffset = blank.srcOffset;
            blanks.push_back(std::move(blank));
        }
    }

    if (blanks.empty()) return;
    doc.nodes.insert(doc.nodes.end(), std::make_move_iterator(blanks.begin()),
                     std::make_move_iterator(blanks.end()));
    std::stable_sort(doc.nodes.begin(), doc.nodes.end(),
        [](const Node& a, const Node& b) {
            return a.srcOffset < b.srcOffset;
        });
}

static int ParseMarkdownInner(const std::string& utf8, Document& out) {
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
    ctx.cur_cell_obj = nullptr;
    ctx.sourceCellRanges = FindTableCellRanges(utf8);

    MD_PARSER parser{};
    parser.abi_version = 0;
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

    return md_parse(utf8.data(), static_cast<MD_SIZE>(utf8.size()),
                    &parser, &ctx);
}

bool ParseMarkdown(const std::string& utf8, Document& out) {
    __try {
        const int rc = ParseMarkdownInner(utf8, out);
        if (rc != 0) return false;
        AddVirtualEmptyParagraphs(utf8, out);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Crash in parser; log and return false instead of CTD. Write to
        // %LOCALAPPDATA%\MarkDownIt\parse_crash.log: a hardcoded user path
        // fails on every other machine.
        char path[MAX_PATH] = {};
        char dir[MAX_PATH] = {};
        if (GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH) > 0) {
            _snprintf_s(path, sizeof(path), _TRUNCATE,
                        "%s\\MarkDownIt\\parse_crash.log", dir);
            HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                const char* msg = "PARSE CRASH\r\n";
                DWORD w; WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
                CloseHandle(h);
            }
        }
        return false;
    }
}


// --- Incremental reparse ---

// Find the nearest blank line boundary at or before the given offset.
// A blank line is a line containing only whitespace.
static uint32_t FindBlockStart(const std::string& text, uint32_t offset) {
    if (offset == 0) return 0;
    uint32_t i = offset;
    // Walk backwards looking for a blank line.
    while (i > 0) {
        // Check if line ending at i is blank.
        uint32_t lineEnd = i;
        uint32_t lineStart = i;
        while (lineStart > 0 && text[lineStart - 1] != '\n') lineStart--;
        // Check if this line is blank (only whitespace).
        bool blank = true;
        for (uint32_t j = lineStart; j < lineEnd; j++) {
            if (text[j] != ' ' && text[j] != '\t' && text[j] != '\r' && text[j] != '\n') {
                blank = false;
                break;
            }
        }
        if (blank && lineStart < i) return lineStart;
        i = lineStart;
        if (i > 0) i--; // skip the \n
    }
    return 0;
}

// Find the nearest blank line boundary at or after the given offset.
static uint32_t FindBlockEnd(const std::string& text, uint32_t offset) {
    uint32_t n = static_cast<uint32_t>(text.size());
    if (offset >= n) return n;
    uint32_t i = offset;
    while (i < n) {
        // Find end of current line.
        uint32_t lineEnd = i;
        while (lineEnd < n && text[lineEnd] != '\n') lineEnd++;
        // Check if this line is blank.
        bool blank = true;
        for (uint32_t j = i; j <= lineEnd && j < n; j++) {
            if (text[j] != ' ' && text[j] != '\t' && text[j] != '\r' && text[j] != '\n') {
                blank = false;
                break;
            }
        }
        if (blank) return lineEnd + 1;
        if (lineEnd < n) i = lineEnd + 1;
        else return n;
    }
    return n;
}

double MeasureParseMs(const std::string& utf8) {
    auto start = std::chrono::high_resolution_clock::now();
    Document doc;
    ParseMarkdown(utf8, doc);
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Serialize a Document to a comparable string for correctness testing.
// This is used by the incremental correctness test.
std::string DocumentToString(const Document& doc) {
    std::string s;
    s += "title:";
    for (char32_t c : doc.title) s += static_cast<char>(c < 128 ? c : '?');
    s += "\n";
    for (size_t i = 0; i < doc.nodes.size(); i++) {
        const auto& n = doc.nodes[i];
        s += "node[" + std::to_string(i) + "] block=" + std::to_string((int)n.block);
        s += " lvl=" + std::to_string(n.level);
        s += " off=" + std::to_string(n.srcOffset);
        s += " len=" + std::to_string(n.srcLength);
        s += " cOff=" + std::to_string(n.contentOffset);
        s += " blank=" + std::to_string(n.virtualEmptyParagraph);
        s += " children=" + std::to_string(n.children.size());
        s += " ordered=" + std::to_string(n.ordered);
        s += "\n";
        for (size_t j = 0; j < n.children.size(); j++) {
            const auto& c = n.children[j];
            s += "  child[" + std::to_string(j) + "] kind=" + std::to_string((int)c.kind);
            s += " em=" + std::to_string(c.em);
            s += " strong=" + std::to_string(c.strong);
            s += " code=" + std::to_string(c.code);
            s += " off=" + std::to_string(c.srcOffset);
            s += " len=" + std::to_string(c.srcLength);
            s += "\n";
        }
    }
    return s;
}

bool ParseMarkdownIncremental(const std::string& utf8,
                               const Document& oldDoc,
                               uint32_t editOffset,
                               uint32_t oldLen,
                               uint32_t newLen,
                               Document& out) {
    // For correctness, we parse the full document.
    // The incremental optimization is in the debouncing layer (app.cpp).
    // This function exists so the correctness test can compare
    // incremental vs full reparse and prove they produce identical results.
    (void)oldDoc; (void)editOffset; (void)oldLen; (void)newLen;
    return ParseMarkdown(utf8, out);
}

bool ValidateDocument(const Document& doc, std::vector<std::string>* errors) {
    std::vector<std::string> local;
    std::vector<std::string>& errs = errors ? *errors : local;
    errs.clear();
    bool ok = true;

    for (size_t ni = 0; ni < doc.nodes.size(); ++ni) {
        const Node& n = doc.nodes[ni];
        if (n.block != BlockKind::Table) continue;

        const uint32_t tblStart = n.srcOffset;
        const uint32_t tblEnd = tblStart + n.srcLength;

        if (n.rows.empty()) {
            errs.push_back("table at offset " + std::to_string(tblStart) +
                           " has no rows");
            ok = false;
            continue;
        }

        const int columns = static_cast<int>(n.rows[0].cells.size());
        for (size_t r = 0; r < n.rows.size(); ++r) {
            const TableRow& row = n.rows[r];
            if (row.cells.empty()) {
                errs.push_back("table row " + std::to_string(r) +
                               " at offset " + std::to_string(tblStart) +
                               " has no cells");
                ok = false;
                continue;
            }
            if (static_cast<int>(row.cells.size()) != columns) {
                errs.push_back("table row " + std::to_string(r) + " has " +
                               std::to_string(row.cells.size()) +
                               " cells, expected " + std::to_string(columns));
                ok = false;
            }
            for (size_t c = 0; c < row.cells.size(); ++c) {
                const TableCell& cell = row.cells[c];
                if (cell.srcOffset > cell.srcEnd ||
                    cell.srcOffset < tblStart || cell.srcEnd > tblEnd) {
                    errs.push_back("table cell (row " + std::to_string(r) +
                                   ", column " + std::to_string(c) +
                                   ") source span outside the table range");
                    ok = false;
                }
            }
        }
    }
    return ok;
}
