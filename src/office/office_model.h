#pragma once

// Shared document model for the office interop code (Word .docx import and
// export, PDF export). Portable C++17 with no Windows dependency, so the
// model can be built and tested headless.
//
// The model lives in namespace office because src/dom.h already declares a
// global scoped enum named BlockKind for the markdown model. Namespacing here
// keeps both models usable in the same translation unit.

#include <string>
#include <vector>

namespace office {

enum BlockKind {
    Paragraph,
    Heading,
    ListItem,
    CodeBlock,
    Table,
    Image,
    ThematicBreak,
    Title,       // Word's "Title" style; the reference dump calls this a title block
    PageBreak    // an explicit page break, carries no runs
};

// Heading levels use Word's numbering: Word "Heading 1" has level 1. The
// markdown mapping emits one more "#" than that, because the document title
// owns the first level.

struct Run {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool mono = false;
    bool strike = false;
    bool underline = false;
    std::string link;   // target URL for a hyperlink run, empty otherwise
};

struct Cell {
    std::vector<Run> runs;
};

struct Block {
    BlockKind kind = Paragraph;
    int level = 0;
    std::vector<Run> runs;
    std::vector<std::vector<Cell>> rows;
    bool ordered = false;
};

struct PageSetup {
    double width_pt = 595.28;
    double height_pt = 841.89;
    double margin_pt = 56.7;
    bool landscape = false;
    bool footer_page_numbers = false;   // draw "Side N" in the footer of every page
};

struct CompatWarning {
    std::string feature;
    std::string detail;
};

struct CompatReport {
    std::vector<CompatWarning> warnings;
};

struct DocModel {
    std::vector<Block> blocks;
    PageSetup page;
    std::string title;
};

}  // namespace office
