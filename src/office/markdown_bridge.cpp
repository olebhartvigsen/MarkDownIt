// Markdown bridge: office::DocModel <-> MarkDownIt markdown.
//
// The mapping is defined by tools/office-oracle/docx_to_md.py (see
// markdown_bridge.h for the rules), so the model -> markdown direction
// reproduces tests/office/golden/reference.md byte for byte. The markdown ->
// model direction reuses the project's parser, src/parser.h, and never
// re-implements markdown.

#include "markdown_bridge.h"

#include "../dom.h"
#include "../parser.h"

#include <map>
#include <string>
#include <vector>

namespace office {
namespace {

// --- helpers shared by both directions ------------------------------------

bool IsSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

// One entry per feature, the way the .docx importer reports them.
void AddWarning(CompatReport& report, const std::string& feature, const std::string& detail) {
    for (const CompatWarning& warning : report.warnings) {
        if (warning.feature == feature) return;
    }
    CompatWarning warning;
    warning.feature = feature;
    warning.detail = detail;
    report.warnings.push_back(warning);
}

std::string FromUtf32(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t code_point : text) {
        if (code_point < 0x80) {
            out += static_cast<char>(code_point);
        } else if (code_point < 0x800) {
            out += static_cast<char>(0xC0 | (code_point >> 6));
            out += static_cast<char>(0x80 | (code_point & 0x3F));
        } else if (code_point < 0x10000) {
            out += static_cast<char>(0xE0 | (code_point >> 12));
            out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code_point & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code_point >> 18));
            out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code_point & 0x3F));
        }
    }
    return out;
}

// Strict UTF-8: the model's text ends up in XML parts and in PDF text
// strings, so a malformed byte sequence is refused instead of carried.
bool IsValidUtf8(const std::string& text, size_t& bad_offset) {
    size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80) {
            index += 1;
            continue;
        }
        size_t length = 0;
        unsigned int code_point = 0;
        if ((lead & 0xE0) == 0xC0) {
            length = 2;
            code_point = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            code_point = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            code_point = lead & 0x07u;
        } else {
            bad_offset = index;
            return false;
        }
        if (index + length > text.size()) {
            bad_offset = index;
            return false;
        }
        for (size_t i = 1; i < length; ++i) {
            const unsigned char next = static_cast<unsigned char>(text[index + i]);
            if ((next & 0xC0) != 0x80) {
                bad_offset = index;
                return false;
            }
            code_point = (code_point << 6) | (next & 0x3Fu);
        }
        // Overlong forms, surrogates and code points above U+10FFFF are not text.
        const unsigned int minimum = (length == 2) ? 0x80u : (length == 3 ? 0x800u : 0x10000u);
        if (code_point < minimum || code_point > 0x10FFFFu) {
            bad_offset = index;
            return false;
        }
        if (code_point >= 0xD800u && code_point <= 0xDFFFu) {
            bad_offset = index;
            return false;
        }
        index += length;
    }
    return true;
}

// --- model -> markdown ----------------------------------------------------

// The oracle's inline_markdown(): skip empty runs, then nest inside out.
std::string InlineMarkdown(const std::vector<Run>& runs) {
    std::string out;
    for (const Run& run : runs) {
        if (run.text.empty()) continue;
        std::string text = run.text;
        if (run.mono) text = "`" + text + "`";
        if (run.bold) text = "**" + text + "**";
        if (run.italic) text = "*" + text + "*";
        if (run.strike) text = "~~" + text + "~~";
        if (!run.link.empty()) text = "[" + text + "](" + run.link + ")";
        out += text;
    }
    return out;
}

// The oracle's cell_text(): collapse whitespace, strip, escape the pipe.
std::string CellText(const std::vector<Run>& runs) {
    std::string text;
    for (const Run& run : runs) text += run.text;

    std::string out;
    bool pending_space = false;
    for (char ch : text) {
        if (IsSpace(ch)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out += ' ';
            pending_space = false;
        }
        if (ch == '|') out += '\\';
        out += ch;
    }
    return out;
}

std::string TableRowLine(const std::vector<Cell>& row) {
    std::string line = "|";
    for (const Cell& cell : row) {
        line += " ";
        line += CellText(cell.runs);
        line += " |";
    }
    return line;
}

// The oracle's table_lines(): header row, separator row, then the rows.
std::string TableLines(const Block& block) {
    std::string out;
    if (block.rows.empty()) return out;
    out += TableRowLine(block.rows[0]);
    out += "\n|";
    for (size_t cell = 0; cell < block.rows[0].size(); ++cell) out += " --- |";
    for (size_t row = 1; row < block.rows.size(); ++row) {
        out += "\n";
        out += TableRowLine(block.rows[row]);
    }
    return out;
}

std::string RunsText(const std::vector<Run>& runs) {
    std::string text;
    for (const Run& run : runs) text += run.text;
    return text;
}

}  // namespace

std::string OfficeModelToMarkdown(const DocModel& doc, CompatReport& report) {
    std::string out;
    bool has_previous = false;
    BlockKind previous_kind = Paragraph;
    bool previous_ordered = false;
    std::map<int, int> counters;

    for (const Block& block : doc.blocks) {
        // A page break is layout, not content: it has no markdown form, so its
        // line is dropped and the blocks around it stay glued as if it were not
        // there.
        if (block.kind == PageBreak) {
            AddWarning(report, "page break",
                       "a page break has no markdown form; the break is dropped");
            continue;
        }

        std::string chunk;
        std::string glue = has_previous ? "\n\n" : "";

        if (block.kind == ListItem) {
            const int level = block.level > 0 ? block.level : 0;
            // The oracle restarts the numbering when the list kind changes and
            // numbers from 1 inside each list.
            if (!has_previous || previous_kind != ListItem || previous_ordered != block.ordered) {
                counters.clear();
            }
            for (std::map<int, int>::iterator it = counters.begin(); it != counters.end();) {
                if (it->first > level) {
                    it = counters.erase(it);
                } else {
                    ++it;
                }
            }
            std::string marker = "- ";
            if (block.ordered) {
                const int number = counters[level] + 1;
                counters[level] = number;
                marker = std::to_string(number) + ". ";
            }
            chunk = std::string(static_cast<size_t>(level) * 2, ' ') + marker +
                    InlineMarkdown(block.runs);
            glue = has_previous ? (previous_kind == ListItem ? "\n" : "\n\n") : "";
        } else if (block.kind == Table) {
            chunk = TableLines(block);
        } else if (block.kind == Title) {
            chunk = "# " + InlineMarkdown(block.runs);
        } else if (block.kind == Heading) {
            int level = block.level;
            if (level < 0 || level > 5) {
                AddWarning(report, "heading level",
                           "a heading level outside 0..5 has no ATX form; it is clamped");
                level = level < 0 ? 0 : 5;
            }
            chunk = std::string(static_cast<size_t>(level) + 1, '#') + " " +
                    InlineMarkdown(block.runs);
        } else if (block.kind == CodeBlock) {
            std::string text = RunsText(block.runs);
            if (text.empty() || text[text.size() - 1] != '\n') text += "\n";
            chunk = "```\n" + text + "```";
        } else if (block.kind == ThematicBreak) {
            chunk = "---";
        } else {
            // A paragraph, and an image, whose text is all the model carries.
            chunk = InlineMarkdown(block.runs);
        }

        out += glue;
        out += chunk;
        has_previous = true;
        previous_kind = block.kind;
        previous_ordered = block.ordered;
    }

    out += "\n";
    return out;
}

std::string OfficeModelToMarkdown(const DocModel& doc) {
    CompatReport report;
    return OfficeModelToMarkdown(doc, report);
}

// --- markdown -> model ----------------------------------------------------

namespace {

// The markdown form of a run: text plus bold, italic, mono, strike and link.
// Underline is not part of it, because markdown cannot carry underline, so the
// parser merges a run into its neighbour when only underline differs.
bool SameMarkdownForm(const Run& a, const Run& b) {
    return a.bold == b.bold && a.italic == b.italic && a.mono == b.mono &&
           a.strike == b.strike && a.link == b.link;
}

void AppendRuns(std::vector<Run>& runs, const std::vector<InlineBlock>& children,
                CompatReport& report) {
    for (const InlineBlock& child : children) {
        // A picture is reported even when its alt text is empty: the block text
        // is all the model can carry.
        if (child.kind == InlineKind::Image) {
            AddWarning(report, "image",
                       "the model carries no picture data; only the alt text is kept");
        }
        if (child.text.empty()) continue;   // the oracle skips empty runs
        Run run;
        run.text = FromUtf32(child.text);
        run.bold = child.strong;
        run.italic = child.em;
        run.mono = child.code;
        run.strike = child.strike || child.kind == InlineKind::Strike;
        run.underline = child.kind == InlineKind::Underline;
        if (child.kind == InlineKind::Link) {
            run.link = child.url;
            // Word writes hyperlinks underlined, so the model's hyperlink runs
            // are underlined too; a markdown link has to come back the same way.
            run.underline = true;
        }
        if (!runs.empty() && SameMarkdownForm(runs.back(), run)) {
            runs.back().text += run.text;
            continue;
        }
        runs.push_back(run);
    }
}

}  // namespace

bool MarkdownToOfficeModel(const std::string& markdown, DocModel& out,
                           CompatReport& report, std::string& error) {
    out = DocModel();
    report = CompatReport();
    error.clear();

    size_t bad_offset = 0;
    if (!IsValidUtf8(markdown, bad_offset)) {
        error = "the markdown is not valid UTF-8 text: malformed byte sequence at offset " +
                std::to_string(bad_offset);
        return false;
    }

    Document document;
    if (!ParseMarkdown(markdown, document)) {
        error = "the markdown parser rejected the input";
        return false;
    }

    for (const Node& node : document.nodes) {
        // A run of blank source lines is layout, not content: the parser
        // synthesises a zero-width paragraph for it.
        if (node.virtualEmptyParagraph) continue;

        Block block;
        switch (node.block) {
            case ::BlockKind::Heading:
                // The document title owns "#": ATX level 1 is the title block,
                // ATX level 2 is Word "Heading 1", and so on.
                if (node.level <= 1) {
                    block.kind = Title;
                } else {
                    block.kind = Heading;
                    block.level = node.level - 1;
                }
                AppendRuns(block.runs, node.children, report);
                break;
            case ::BlockKind::Paragraph:
                block.kind = Paragraph;
                AppendRuns(block.runs, node.children, report);
                break;
            case ::BlockKind::List:
                block.kind = ListItem;
                block.ordered = node.ordered;
                block.level = node.depth > 0 ? node.depth : 0;
                AppendRuns(block.runs, node.children, report);
                break;
            case ::BlockKind::CodeBlock: {
                block.kind = CodeBlock;
                if (!node.lang.empty()) {
                    AddWarning(report, "code block language",
                               "the model has no fence info string; the language is dropped");
                }
                Run run;
                run.text = FromUtf32(node.raw);
                run.mono = true;
                block.runs.push_back(run);
                break;
            }
            case ::BlockKind::ThematicBreak:
                block.kind = ThematicBreak;
                break;
            case ::BlockKind::Table: {
                block.kind = Table;
                for (const TableRow& source_row : node.rows) {
                    std::vector<Cell> row;
                    for (const TableCell& source_cell : source_row.cells) {
                        if (!source_cell.inlineSpans.empty() || !source_cell.links.empty()) {
                            AddWarning(report, "table cell formatting",
                                       "the table rule renders cells as plain text; formatting "
                                       "inside a cell is dropped");
                        }
                        Cell cell;
                        Run run;
                        run.text = FromUtf32(source_cell.text);
                        cell.runs.push_back(run);
                        row.push_back(cell);
                    }
                    block.rows.push_back(row);
                }
                break;
            }
            case ::BlockKind::BlockQuote:
                AddWarning(report, "block quote",
                           "the model has no block quote; the quote is dropped");
                continue;
            case ::BlockKind::MermaidFlowchart:
            case ::BlockKind::MermaidPie:
            case ::BlockKind::MermaidSequence:
                AddWarning(report, "mermaid diagram",
                           "the model has no diagram block; the diagram is dropped");
                continue;
        }
        out.blocks.push_back(block);
    }

    // The title is the first title block, which is what the parser already
    // reports as the document title (the first level 1 heading).
    for (const Block& block : out.blocks) {
        if (block.kind != Title) continue;
        out.title = RunsText(block.runs);
        break;
    }
    return true;
}

}  // namespace office
