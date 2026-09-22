#pragma once

// The reference document model: the content of tests/office/fixtures/reference.docx
// as an office::DocModel, matching tests/office/golden/reference_docx.json.
//
// One definition drives both directions. The import test asserts that reading the
// fixture produces this model, and the exporter tests take it as their input, so a
// failure on either side points at the same expected content.
//
// Non-ASCII text is written with hex escapes and literal concatenation, following
// the project convention in tests/navigation_test.cpp: the bytes are then the same
// whether the compiler reads the source as UTF-8 or as a legacy code page.

#include "office/office_model.h"

#include <string>
#include <vector>

namespace office {
namespace reference {

inline Run Plain(const std::string& text) { Run r; r.text = text; return r; }
inline Run Bold(const std::string& text) { Run r = Plain(text); r.bold = true; return r; }
inline Run Italic(const std::string& text) { Run r = Plain(text); r.italic = true; return r; }
inline Run Mono(const std::string& text) { Run r = Plain(text); r.mono = true; return r; }
inline Run Underlined(const std::string& text) { Run r = Plain(text); r.underline = true; return r; }
inline Run Hyperlink(const std::string& text, const std::string& url) {
    Run r = Plain(text); r.underline = true; r.link = url; return r;
}
inline Cell TextCell(const std::string& text) { Cell c; c.runs.push_back(Plain(text)); return c; }

// The page setup of the fixture: A4 portrait, 2 cm margins on every side.
inline PageSetup A4Setup() {
    PageSetup page;
    page.width_pt = 595.28;
    page.height_pt = 841.89;
    page.margin_pt = 56.7;
    page.landscape = false;
    return page;
}

// Every content block, in document order.
inline std::vector<Block> Blocks() {
    std::vector<Block> blocks;
    {
        Block b;
        b.kind = BlockKind::Title;
        b.runs.push_back(Plain("MarkDownIt Office Interop Reference"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::Heading;
        b.level = 1;
        b.runs.push_back(Plain("Indledning"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::Paragraph;
        b.runs.push_back(Plain("Dette er "));
        b.runs.push_back(Bold("fed tekst"));
        b.runs.push_back(Plain(", "));
        b.runs.push_back(Italic("kursiv tekst"));
        b.runs.push_back(Plain(" og "));
        b.runs.push_back(Mono("monospace tekst"));
        b.runs.push_back(Plain(". Understreget: "));
        b.runs.push_back(Underlined("understreget tekst"));
        b.runs.push_back(Plain(". Danske tegn: " "\xc3" "\xa6" " " "\xc3" "\xb8" " " "\xc3" "\xa5" " " "\xc3" "\x86" " " "\xc3" "\x98" " " "\xc3" "\x85" "."));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::Heading;
        b.level = 2;
        b.runs.push_back(Plain("Punktliste"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.runs.push_back(Plain("F" "\xc3" "\xb8" "rste punkt"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.runs.push_back(Plain("Andet punkt"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.level = 1;
        b.runs.push_back(Plain("Underelement til andet punkt"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.runs.push_back(Plain("Tredje punkt"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.ordered = true;
        b.runs.push_back(Plain("Trin et"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.ordered = true;
        b.runs.push_back(Plain("Trin to"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::ListItem;
        b.ordered = true;
        b.runs.push_back(Plain("Trin tre"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::Heading;
        b.level = 2;
        b.runs.push_back(Plain("Tabel"));
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::Table;
        { std::vector<Cell> row; row.push_back(TextCell("Navn")); row.push_back(TextCell("Antal")); row.push_back(TextCell("Note")); b.rows.push_back(row); }
        { std::vector<Cell> row; row.push_back(TextCell("\xc3" "\x86" "ble")); row.push_back(TextCell("3")); row.push_back(TextCell("Moden")); b.rows.push_back(row); }
        { std::vector<Cell> row; row.push_back(TextCell("\xc3" "\x98" "l")); row.push_back(TextCell("12")); row.push_back(TextCell("Kold")); b.rows.push_back(row); }
        { std::vector<Cell> row; row.push_back(TextCell("\xc3" "\x85" "l")); row.push_back(TextCell("1")); row.push_back(TextCell("Fersk")); b.rows.push_back(row); }
        blocks.push_back(b);
    }
    {
        Block b;
        b.kind = BlockKind::Paragraph;
        b.runs.push_back(Plain("L" "\xc3" "\xa6" "s mere p" "\xc3" "\xa5" " "));
        b.runs.push_back(Hyperlink("example.com", "https://example.com"));
        b.runs.push_back(Plain(" for detaljer."));
        blocks.push_back(b);
    }
    return blocks;
}

// The model behind reference.docx: the blocks with the fixture's page setup.
inline DocModel DocxModel() {
    DocModel doc;
    doc.title = "MarkDownIt Office Interop Reference";
    doc.page = A4Setup();
    doc.blocks = Blocks();
    return doc;
}

// The model behind reference.pdf: the same content, with an explicit page break
// before the "Tabel" heading (the fixture keeps the table on page 2) and a
// "Side N" footer on every page.
inline DocModel PdfModel() {
    DocModel doc = DocxModel();
    doc.page.footer_page_numbers = true;
    std::vector<Block> blocks;
    for (const Block& b : doc.blocks) {
        if (b.kind == BlockKind::Heading && b.level == 2 && !b.runs.empty() &&
            b.runs[0].text == "Tabel") {
            Block page_break;
            page_break.kind = BlockKind::PageBreak;
            blocks.push_back(page_break);
        }
        blocks.push_back(b);
    }
    doc.blocks.swap(blocks);
    return doc;
}

}  // namespace reference
}  // namespace office
