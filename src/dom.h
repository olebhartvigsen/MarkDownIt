#pragma once

// MarkDownIt DOM: the flat node list the renderer consumes.
//
// md4c emits SAX events; the parser bridge (Task 4) pushes them into a flat
// std::vector<Node>. No tree, no ownership complexity. The renderer walks it
// top to bottom and stacks blocks vertically.

#include <string>
#include <vector>
#include <cstdint>

// Block-level elements (one per markdown block: heading, paragraph, etc.)
enum class BlockKind {
    Heading,        // level 1-6 stored in Node::level
    Paragraph,
    CodeBlock,      // raw text in Node::raw
    List,           // ordered flag in Node::ordered
    BlockQuote,
    ThematicBreak,
    Table,          // not rendered in v1
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
};

// A table cell: plain text (no inline spans in cells for now).
struct TableCell {
    std::u32string text;
    bool isHeader = false;
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
    std::u32string             raw;          // code block raw text (UTF-32)
    std::vector<TableRow>     rows;         // table rows (for BlockKind::Table)
};

// The full parsed document: a flat list of blocks plus an optional title
// (extracted from the first H1, if present).
struct Document {
    std::vector<Node>  nodes;
    std::u32string     title;    // first H1 text, or empty
};
