#pragma once

// MarkDownIt DOM: the flat node list the renderer consumes.
//
// md4c emits SAX events; the parser bridge (Task 4) pushes them into a flat
// std::vector<Node>. No tree, no ownership complexity. The renderer walks it
// top to bottom and stacks blocks vertically.

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace mermaid { struct Flowchart; }
namespace mermaid { struct LaidOutFlowchart; }
namespace mermaid { struct LaidOutPie; }
namespace mermaid { struct LaidOutSequence; }
namespace mermaid { struct LaidOutGantt; }

// Block-level elements (one per markdown block: heading, paragraph, etc.)
enum class BlockKind {
    Heading,        // level 1-6 stored in Node::level
    Paragraph,
    CodeBlock,      // raw text in Node::raw
    List,           // ordered flag in Node::ordered
    BlockQuote,
    ThematicBreak,
    Table,          // not rendered in v1
    MermaidFlowchart, // fenced ```mermaid block, laid out at parse time
    MermaidPie,       // fenced ```mermaid `pie` block, laid out at parse time
    MermaidSequence,  // fenced ```mermaid `sequenceDiagram` block, laid out at parse time
    MermaidGantt,     // fenced ```mermaid `gantt` block, laid out at parse time
};

// Inline-level elements (within a block's children)
enum class InlineKind {
    Text,           // plain text
    Emphasis,       // *italic* or _italic_
    Strong,         // **bold** or __bold__
    Code,           // `inline code`
    Link,           // [text](url)
    Image,          // ![alt](src)
    LineBreak,      // hard break
    Strike,         // ~~strikethrough~~
    Underline,      // ++underline++ (not standard md4c)
};

// A single inline span within a block.
// UTF-32 text so DirectWrite gets one code point per element (no surrogate
// pair bookkeeping). URL is UTF-8 for Link/Image.
struct InlineBlock {
    InlineKind     kind = InlineKind::Text;
    std::u32string  text;        // UTF-32 code points
    std::string     url;         // for Link/Image (UTF-8)
    int             headingLevel = 0;  // 0 for non-heading (unused; level on Node)
    bool            em     = false;   // emphasis (italic)
    bool            strong = false;   // strong (bold)
    bool            code   = false;   // inline code
    bool            strike = false;   // ~~strikethrough~~
    uint32_t        srcOffset = 0;   // byte offset into the UTF-8 source
    uint32_t        srcLength = 0;   // byte length of this span
};

// Inline formatting span within a table cell.
// u16Start/u16End are UTF-16 code-unit indices into the cell's rendered text.
struct CellInlineSpan {
    uint32_t u16Start = 0;
    uint32_t u16End = 0;
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool strike = false;
};

struct TableLink {
    uint32_t srcOffset = 0;
    uint32_t srcLength = 0;
    std::string url;
};

// A table cell: text plus inline formatting spans.
// Horizontal alignment of a table column, as written in the delimiter row
// (`:---`, `:---:`, `---:`). Parsed from md4c's MD_BLOCK_TD_DETAIL.align.
enum class ColumnAlign { Default, Left, Center, Right };

struct TableCell {
    std::u32string text;
    bool isHeader = false;
    uint32_t srcOffset = 0;  // byte offset into UTF-8 source
    uint32_t srcEnd = 0;     // exclusive end of the source cell contents
    // Maps each UTF-16 code-unit position in the rendered text to its
    // UTF-8 source byte offset. Built during parsing because md4c may
    // split cell text at mark characters (e.g., ':' with permissive URL
    // autolinks), creating gaps in the source that the renderer's
    // contiguity assumption cannot handle.
    std::vector<uint32_t> u16ToSrc;
    std::vector<uint32_t> u16ToSrcEnd;
    // Inline formatting spans (bold, italic, code) within this cell.
    std::vector<CellInlineSpan> inlineSpans;
    std::vector<TableLink> links;
};

// A table row: list of cells.
struct TableRow {
    std::vector<TableCell> cells;
};

// A block-level node. The renderer's input unit.
struct Node {
    BlockKind                  block = BlockKind::Paragraph;
    int                        level = 0;    // heading level 1-6
    std::vector<InlineBlock>   children;     // inline spans
    bool                       ordered = false;  // list: ordered vs unordered
    int                        depth = 0;       // list nesting depth (0=top)
    // A task list item ("- [ ]" or "- [x]"): -1 is not a task, 0 open,
    // 1 done. The checkbox is drawn in the marker gutter and the mark
    // itself is hidden from the rendered text, the way other syntax is.
    int                        taskMark = -1;
    uint32_t                   srcOffset = 0;  // byte offset into UTF-8 source
    uint32_t                   srcLength = 0;  // byte length of this block in source
    uint32_t                   contentOffset = 0; // where editable text starts, after markers
    uint32_t                   contentLength = 0; // byte length of editable text
    // A logical, addressable blank paragraph synthesized from a run of blank
    // source lines. It has a zero-width source range but a real caret layout.
    bool                       virtualEmptyParagraph = false;
    std::u32string             raw;          // code block raw text (UTF-32)
    std::string                lang;         // fence info string, e.g. "mermaid"
    std::vector<TableRow>     rows;         // table rows (for BlockKind::Table)
    // Column alignment, one entry per column, read from the delimiter row.
    // Empty when the table declares no alignment anywhere, so a plain table
    // costs nothing.
    std::vector<ColumnAlign>   aligns;
    // Parsed source and a provisional layout. The renderer replaces the layout
    // with a DirectWrite-measured cached snapshot when its text services exist.
    std::shared_ptr<mermaid::Flowchart> mermaid_flowchart;
    std::string mermaid_source;
    std::shared_ptr<mermaid::LaidOutFlowchart> mermaid_layout;  // populated when block == MermaidFlowchart
    std::shared_ptr<mermaid::LaidOutPie>       mermaid_pie;     // populated when block == MermaidPie
    std::shared_ptr<mermaid::LaidOutSequence>  mermaid_seq;     // populated when block == MermaidSequence
    std::shared_ptr<mermaid::LaidOutGantt>     mermaid_gantt;   // populated when block == MermaidGantt
};

// The full parsed document: a flat list of blocks plus an optional title
// (extracted from the first H1, if present).
struct Document {
    std::vector<Node>  nodes;
    std::u32string     title;    // first H1 text, or empty
};

// The block kinds that hold source rather than prose: a fence, an
// indented block, a diagram. Markdown formatting has no meaning inside
// one.
inline bool IsCodeLikeBlock(BlockKind kind) {
    switch (kind) {
    case BlockKind::CodeBlock:
    case BlockKind::MermaidFlowchart:
    case BlockKind::MermaidPie:
    case BlockKind::MermaidSequence:
    case BlockKind::MermaidGantt:
        return true;
    default:
        return false;
    }
}

// The first byte of the line that holds the offset.
inline uint32_t SourceLineStart(const std::string& text, uint32_t at) {
    if (at > text.size()) at = static_cast<uint32_t>(text.size());
    while (at > 0 && text[at - 1] != '\n') --at;
    return at;
}

// The newline that ends the line that holds the offset, or the end of
// the text.
inline uint32_t SourceLineEnd(const std::string& text, uint32_t at) {
    if (at > text.size()) at = static_cast<uint32_t>(text.size());
    while (at < text.size() && text[at] != '\n') ++at;
    return at;
}

// Whether the line that starts at the offset opens or closes a fence:
// three backticks or three tildes, indented by at most three spaces.
inline bool SourceLineIsFence(const std::string& text, uint32_t lineStart) {
    uint32_t i = lineStart;
    int indent = 0;
    while (i < text.size() && text[i] == ' ' && indent < 4) {
        ++i;
        ++indent;
    }
    if (i + 2 >= text.size()) return false;
    const char c = text[i];
    return (c == '`' || c == '~') && text[i + 1] == c && text[i + 2] == c;
}

// Whether the offset sits in code: a fence, an indented block or a
// diagram. The commands that apply markdown formatting stand down there,
// and the keys keep their literal meaning (soft break, caret placement
// and the command gates read this one answer).
//
// A fence's node covers the lines between its markers, not the marker
// lines themselves, so the answer widens to those two lines when they
// are there: a caret on the opening ``` sits in the block the same way
// a caret in the middle of the code does.
inline bool OffsetInCodeLikeBlock(const Document& doc, const std::string& text,
                                  uint32_t at) {
    for (const Node& node : doc.nodes) {
        if (!IsCodeLikeBlock(node.block)) continue;
        const uint32_t contentStart = node.srcOffset;
        const uint32_t contentEnd = node.srcOffset + node.srcLength;
        uint32_t start = SourceLineStart(text, contentStart);
        uint32_t end = SourceLineEnd(text, contentEnd);
        if (start > 0) {
            const uint32_t previous = SourceLineStart(text, start - 1);
            if (SourceLineIsFence(text, previous)) start = previous;
        }
        if (end < text.size() && SourceLineIsFence(text, end + 1)) {
            end = SourceLineEnd(text, end + 1);
        }
        if (at >= start && at <= end) return true;
    }
    return false;
}
