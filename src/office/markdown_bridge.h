#pragma once

// The bridge between MarkDownIt's markdown text and the office document model:
// the .docx importer and exporter and the PDF exporter all speak
// office::DocModel, and this is the only place that knows how that model reads
// as markdown.
//
// The contract for the model -> markdown direction is
// tools/office-oracle/docx_to_md.py, the oracle that produced
// tests/office/golden/reference.md, so OfficeModelToMarkdown() reproduces that
// file byte for byte. The markdown -> model direction parses with the project's
// own parser (src/parser.h) and fills the same model, which makes
// OfficeModelToMarkdown(MarkdownToOfficeModel(reference.md)) == reference.md.
//
// Mapping rules, in the oracle's words
// ------------------------------------
// Blocks
//   title          "# " plus the text
//   heading        ATX heading one level lower than the Word level, because the
//                  document title owns "#": Word "Heading 1" becomes "## ",
//                  Word "Heading 2" becomes "### ", and so on
//   paragraph      the inline text of the paragraph, on one line
//   list item      "- " per bullet, "N. " numbered from 1 inside each list;
//                  nesting is two spaces per level
//   table          GFM pipe table: the first row is the header row, then the
//                  separator row, then the rows; a "|" inside a cell is escaped
//                  and whitespace in a cell is collapsed
//   code block     a fenced block
//   image          its text, as a paragraph
//   thematic break "---"
//   page break     no markdown form: the line is dropped, and reported
// Inline
//   bold           **text**
//   italic         *text*
//   monospace      `text`
//   strike         ~~text~~
//   underline      no markdown form, emitted as plain text
//   link           [text](url)
//   nesting        monospace, then bold, then italic, then the link
// Blank lines separate blocks, except between consecutive list items, which stay
// on adjacent lines. The file ends with a single trailing newline.
//
// MarkdownToOfficeModel() merges a run into its neighbour when the two render
// the same way in markdown (same bold, italic, monospace, strikethrough and
// link), so the parsed model has one run where the markdown has one span.
//
// What markdown cannot carry is listed in the CompatReport by
// MarkdownToOfficeModel(): a block quote and a mermaid diagram have no model
// block, a fence info string has no model field, an image keeps its alt text
// only, and inline formatting inside a table cell is dropped because the table
// rule renders cells as plain text.
//
// Known limitation: the oracle's two spaces per nesting level cannot express a
// list nested inside an ordered item, whose marker is three characters wide, so
// such a nested item comes back one level shallower.

#include "office_model.h"

#include <string>

namespace office {

// The document as markdown, following the oracle contract above. A page break
// has no markdown form, so its line is dropped.
std::string OfficeModelToMarkdown(const DocModel& doc);

// Same, and every block the markdown cannot carry (a page break, a heading
// level outside the ATX range) is added to report. The reference document has
// no page break, so both overloads agree on
// tests/office/golden/reference.md.
std::string OfficeModelToMarkdown(const DocModel& doc, CompatReport& report);

// Parses markdown with the project's parser and fills out: "# " becomes the
// title block, "## " a heading of Word level 1, "### " level 2, and so on; runs
// keep bold, italic, monospace and strikethrough; a link keeps its URL and is
// underlined, the model's hyperlink convention; list items keep their kind and
// their nesting depth; tables, fenced code blocks and thematic breaks become
// their model blocks.
//
// Returns false, with error set and out left empty, when the input is not valid
// UTF-8 text or the parser rejects it. Features the model cannot hold are listed
// in report. Markdown carries no page setup, so out keeps the default A4
// portrait setup, 595.28 x 841.89 pt with 56.7 pt margins: the reference
// document's setup, within the 0.1 pt the importer compares with.
bool MarkdownToOfficeModel(const std::string& markdown, DocModel& out,
                           CompatReport& report, std::string& error);

}  // namespace office
