// Office markdown bridge test.
//
// Proves both directions of src/office/markdown_bridge.cpp:
//   * office::reference::DocxModel() -> OfficeModelToMarkdown() is
//     tests/office/golden/reference.md byte for byte, the golden that the oracle
//     tools/office-oracle/docx_to_md.py produces from the reference .docx.
//   * tests/office/golden/reference.md -> MarkdownToOfficeModel() is
//     office::reference::DocxModel() field by field, and re-emitting the parsed
//     model gives the golden again.
//   * a sample document with nested bullets, an ordered list, a table with an
//     escaped pipe, a link, inline code, a fenced block and a thematic break
//     parses to the expected model and re-emits to the same markdown.
//   * the negative cases: empty markdown is an empty model, malformed UTF-8 is
//     an error, and neither crashes.
//
// The model comparison is markdown-equivalent: underline has no markdown form,
// so underline-only runs merge into their neighbours on both sides before the
// field-by-field comparison, which is what the emitter does with them.
//
// Portable C++17: no windows.h, so it runs in CI (windows-2022) and headless on
// Linux. tests/test_main.cpp owns main().

#include "gtest_lite.h"

#include "office/markdown_bridge.h"
#include "office/office_model.h"
#include "office_reference_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace office;

namespace {

const char kGoldenReference[] = "tests/office/golden/reference.md";

bool ReadFileBytes(const std::string& path, std::string& out) {
    std::ifstream stream(path.c_str(), std::ios::binary);
    if (!stream) return false;
    out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.eof() || stream.good();
}

std::string Quote(const std::string& text) {
    std::string quoted = "\"";
    for (char ch : text) {
        if (ch == '\n') {
            quoted += "<LF>";
        } else if (ch == '\t') {
            quoted += "<TAB>";
        } else if (ch == '\r') {
            quoted += "<CR>";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

const char* KindName(BlockKind kind) {
    switch (kind) {
        case Paragraph: return "paragraph";
        case Heading: return "heading";
        case ListItem: return "list_item";
        case CodeBlock: return "code_block";
        case Table: return "table";
        case Image: return "image";
        case ThematicBreak: return "thematic_break";
        case Title: return "title";
        case PageBreak: return "page_break";
    }
    return "unknown";
}

// Where two strings first differ, with a window around the difference.
std::string Difference(const std::string& got, const std::string& want) {
    size_t index = 0;
    while (index < got.size() && index < want.size() && got[index] == want[index]) ++index;
    const size_t start = index > 40 ? index - 40 : 0;
    std::ostringstream message;
    message << "first difference at byte " << index << " (got " << got.size()
            << " bytes, expected " << want.size() << " bytes)\n";
    message << "    got:      " << Quote(got.substr(start, 90)) << "\n";
    message << "    expected: " << Quote(want.substr(start, 90));
    return message.str();
}

// The markdown form of a run: text plus bold, italic, mono, strike and link.
// Underline is not part of it, because markdown cannot carry underline.
bool SameMarkdownForm(const Run& a, const Run& b) {
    return a.bold == b.bold && a.italic == b.italic && a.mono == b.mono &&
           a.strike == b.strike && a.link == b.link;
}

std::vector<Run> CanonicalRuns(const std::vector<Run>& runs) {
    std::vector<Run> canonical;
    for (const Run& run : runs) {
        if (run.text.empty()) continue;
        if (!canonical.empty() && SameMarkdownForm(canonical.back(), run)) {
            canonical.back().text += run.text;
            continue;
        }
        Run copy = run;
        copy.underline = false;
        canonical.push_back(copy);
    }
    return canonical;
}

// Collects the first few differences between two models, each message naming
// the block index and the field.
struct ModelDiff {
    std::vector<std::string> messages;

    void Add(const std::string& message) {
        if (messages.size() < 8) messages.push_back(message);
    }

    bool Empty() const { return messages.empty(); }

    std::string Text() const {
        std::string text;
        for (const std::string& message : messages) {
            text += "  ";
            text += message;
            text += "\n";
        }
        return text;
    }
};

void CompareFlag(ModelDiff& diff, const std::string& field, bool got, bool want) {
    if (got != want) {
        std::ostringstream message;
        message << field << ": expected " << (want ? "true" : "false") << ", got "
                << (got ? "true" : "false");
        diff.Add(message.str());
    }
}

void CompareNumber(ModelDiff& diff, const std::string& field, double got, double want,
                   double tolerance) {
    if (!(std::fabs(got - want) <= tolerance)) {
        std::ostringstream message;
        message << field << ": expected " << want << ", got " << got << " (tolerance "
                << tolerance << ")";
        diff.Add(message.str());
    }
}

void CompareRuns(ModelDiff& diff, const std::string& where, const std::vector<Run>& got,
                 const std::vector<Run>& want) {
    const std::vector<Run> left = CanonicalRuns(got);
    const std::vector<Run> right = CanonicalRuns(want);
    if (left.size() != right.size()) {
        std::ostringstream message;
        message << where << ": run count expected " << right.size() << ", got " << left.size();
        diff.Add(message.str());
        return;
    }
    for (size_t index = 0; index < right.size(); ++index) {
        std::ostringstream position;
        position << where << " run " << index;
        if (left[index].text != right[index].text) {
            diff.Add(position.str() + " text: expected " + Quote(right[index].text) + ", got " +
                     Quote(left[index].text));
        }
        CompareFlag(diff, position.str() + " bold", left[index].bold, right[index].bold);
        CompareFlag(diff, position.str() + " italic", left[index].italic, right[index].italic);
        CompareFlag(diff, position.str() + " mono", left[index].mono, right[index].mono);
        CompareFlag(diff, position.str() + " strike", left[index].strike, right[index].strike);
        CompareFlag(diff, position.str() + " underline", left[index].underline,
                    right[index].underline);
        if (left[index].link != right[index].link) {
            diff.Add(position.str() + " link: expected " + Quote(right[index].link) + ", got " +
                     Quote(left[index].link));
        }
    }
}

std::string CompareModels(const DocModel& got, const DocModel& want) {
    ModelDiff diff;
    if (got.title != want.title) {
        diff.Add("title: expected " + Quote(want.title) + ", got " + Quote(got.title));
    }
    if (got.blocks.size() != want.blocks.size()) {
        std::ostringstream message;
        message << "block count: expected " << want.blocks.size() << ", got "
                << got.blocks.size();
        diff.Add(message.str());
    }

    const size_t blocks = got.blocks.size() < want.blocks.size() ? got.blocks.size()
                                                                 : want.blocks.size();
    for (size_t index = 0; index < blocks; ++index) {
        std::ostringstream tag;
        tag << "block " << index;
        const Block& expected = want.blocks[index];
        const Block& actual = got.blocks[index];
        if (actual.kind != expected.kind) {
            diff.Add(tag.str() + ": kind expected " + KindName(expected.kind) + ", got " +
                     KindName(actual.kind));
            continue;
        }
        if (expected.kind == Heading || expected.kind == ListItem) {
            if (actual.level != expected.level) {
                std::ostringstream message;
                message << tag.str() << " (" << KindName(expected.kind) << "): level expected "
                        << expected.level << ", got " << actual.level;
                diff.Add(message.str());
            }
        }
        if (expected.kind == ListItem) {
            CompareFlag(diff, tag.str() + " ordered", actual.ordered, expected.ordered);
        }
        if (expected.kind == Table) {
            if (actual.rows.size() != expected.rows.size()) {
                std::ostringstream message;
                message << tag.str() << ": row count expected " << expected.rows.size()
                        << ", got " << actual.rows.size();
                diff.Add(message.str());
                continue;
            }
            for (size_t row = 0; row < expected.rows.size(); ++row) {
                if (actual.rows[row].size() != expected.rows[row].size()) {
                    std::ostringstream message;
                    message << tag.str() << " row " << row << ": cell count expected "
                            << expected.rows[row].size() << ", got " << actual.rows[row].size();
                    diff.Add(message.str());
                    continue;
                }
                for (size_t cell = 0; cell < expected.rows[row].size(); ++cell) {
                    std::ostringstream where;
                    where << tag.str() << " row " << row << " cell " << cell;
                    CompareRuns(diff, where.str(), actual.rows[row][cell].runs,
                                expected.rows[row][cell].runs);
                }
            }
            continue;
        }
        CompareRuns(diff, tag.str(), actual.runs, expected.runs);
    }

    CompareNumber(diff, "page width_pt", got.page.width_pt, want.page.width_pt, 0.1);
    CompareNumber(diff, "page height_pt", got.page.height_pt, want.page.height_pt, 0.1);
    CompareNumber(diff, "page margin_pt", got.page.margin_pt, want.page.margin_pt, 0.1);
    CompareFlag(diff, "page landscape", got.page.landscape, want.page.landscape);
    CompareFlag(diff, "page footer_page_numbers", got.page.footer_page_numbers,
                want.page.footer_page_numbers);
    return diff.Text();
}

// A sample document written in the canonical shape of the mapping, so the
// emitter reproduces it byte for byte: nested bullets, an ordered list, a table
// with an escaped pipe, a link, inline code, a fenced block, a thematic break.
std::string SampleMarkdown() {
    const char* const lines[] = {
        "# Bridge sample",
        "",
        "A paragraph with `code`, **bold**, *italic*, ~~struck~~ and [a link](https://example.org).",
        "",
        "- First bullet",
        "  - Nested bullet",
        "- Second bullet",
        "1. Step one",
        "2. Step two",
        "3. Step three",
        "",
        "| Name | Note |",
        "| --- | --- |",
        "| a\\|b | plain |",
        "| x | y |",
        "",
        "```",
        "code line 1",
        "  code line 2",
        "```",
        "",
        "---",
        "",
        "Closing paragraph.",
    };
    std::string markdown;
    for (const char* line : lines) {
        markdown += line;
        markdown += "\n";
    }
    return markdown;
}

}  // namespace

// ---------------------------------------------------------------------------
// The reference document, model to markdown
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, ModelToMarkdownIsTheGoldenReference) {
    std::string golden;
    ASSERT_TRUE(ReadFileBytes(kGoldenReference, golden));
    ASSERT_TRUE(golden.size() > 0);

    CompatReport report;
    const std::string markdown = OfficeModelToMarkdown(reference::DocxModel(), report);
    if (markdown != golden) {
        std::cerr << "  model -> markdown differs from the golden:\n" << Difference(markdown, golden)
                  << "\n";
    }
    EXPECT_EQ(markdown, golden);

    // The reference content carries nothing the markdown cannot hold.
    EXPECT_TRUE(report.warnings.empty());
    for (const CompatWarning& warning : report.warnings) {
        std::cerr << "  unexpected warning: " << warning.feature << ": " << warning.detail << "\n";
    }

    // The overload without a report produces the same bytes.
    EXPECT_EQ(OfficeModelToMarkdown(reference::DocxModel()), golden);
}

// ---------------------------------------------------------------------------
// The reference document, markdown to model, and back
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, MarkdownToModelMatchesTheReferenceModelAndReEmitsTheGolden) {
    std::string golden;
    ASSERT_TRUE(ReadFileBytes(kGoldenReference, golden));
    ASSERT_TRUE(golden.size() > 0);

    DocModel parsed;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(golden, parsed, report, error));
    EXPECT_EQ(error, std::string(""));
    for (const CompatWarning& warning : report.warnings) {
        std::cerr << "  unexpected warning: " << warning.feature << ": " << warning.detail << "\n";
    }
    EXPECT_TRUE(report.warnings.empty());

    const DocModel expected = reference::DocxModel();
    const std::string difference = CompareModels(parsed, expected);
    if (!difference.empty()) {
        std::cerr << "  parsed model does not match the reference model:\n" << difference;
    }
    EXPECT_TRUE(difference.empty());

    const std::string emitted = OfficeModelToMarkdown(parsed);
    if (emitted != golden) {
        std::cerr << "  round trip: " << Difference(emitted, golden) << "\n";
    }
    EXPECT_EQ(emitted, golden);
}

// ---------------------------------------------------------------------------
// A sample document of our own
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, SampleDocumentShapesTheModelAndRoundTrips) {
    const std::string sample = SampleMarkdown();

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(sample, model, report, error));
    EXPECT_TRUE(report.warnings.empty());
    EXPECT_EQ(model.title, std::string("Bridge sample"));

    ASSERT_EQ(model.blocks.size(), 12u);
    EXPECT_EQ(model.blocks[0].kind, BlockKind::Title);
    ASSERT_EQ(model.blocks[0].runs.size(), 1u);
    EXPECT_EQ(model.blocks[0].runs[0].text, std::string("Bridge sample"));

    // The paragraph: inline code, bold, italic, strikethrough and a link, each
    // with its own flag, and the link keeps its URL.
    const Block& paragraph = model.blocks[1];
    ASSERT_EQ(paragraph.kind, BlockKind::Paragraph);
    ASSERT_EQ(paragraph.runs.size(), 11u);
    EXPECT_EQ(paragraph.runs[0].text, std::string("A paragraph with "));
    EXPECT_TRUE(paragraph.runs[1].mono);
    EXPECT_EQ(paragraph.runs[1].text, std::string("code"));
    EXPECT_TRUE(paragraph.runs[3].bold);
    EXPECT_EQ(paragraph.runs[3].text, std::string("bold"));
    EXPECT_TRUE(paragraph.runs[5].italic);
    EXPECT_EQ(paragraph.runs[5].text, std::string("italic"));
    EXPECT_TRUE(paragraph.runs[7].strike);
    EXPECT_EQ(paragraph.runs[7].text, std::string("struck"));
    EXPECT_EQ(paragraph.runs[9].text, std::string("a link"));
    EXPECT_EQ(paragraph.runs[9].link, std::string("https://example.org"));
    EXPECT_TRUE(paragraph.runs[9].underline);
    EXPECT_EQ(paragraph.runs[10].text, std::string("."));

    // Bullets keep their nesting depth, an ordered list starts a new list.
    EXPECT_EQ(model.blocks[2].kind, BlockKind::ListItem);
    EXPECT_FALSE(model.blocks[2].ordered);
    EXPECT_EQ(model.blocks[2].level, 0);
    EXPECT_EQ(model.blocks[2].runs[0].text, std::string("First bullet"));
    EXPECT_FALSE(model.blocks[3].ordered);
    EXPECT_EQ(model.blocks[3].level, 1);
    EXPECT_EQ(model.blocks[3].runs[0].text, std::string("Nested bullet"));
    EXPECT_FALSE(model.blocks[4].ordered);
    EXPECT_EQ(model.blocks[4].level, 0);
    for (size_t index = 5; index <= 7; ++index) {
        EXPECT_EQ(model.blocks[index].kind, BlockKind::ListItem);
        EXPECT_TRUE(model.blocks[index].ordered);
        EXPECT_EQ(model.blocks[index].level, 0);
    }
    EXPECT_EQ(model.blocks[7].runs[0].text, std::string("Step three"));

    // The table: the header row and two body rows of two cells, and the escaped
    // pipe comes back as a pipe.
    const Block& table = model.blocks[8];
    ASSERT_EQ(table.kind, BlockKind::Table);
    ASSERT_EQ(table.rows.size(), 3u);
    ASSERT_EQ(table.rows[0].size(), 2u);
    EXPECT_EQ(table.rows[0][0].runs[0].text, std::string("Name"));
    EXPECT_EQ(table.rows[0][1].runs[0].text, std::string("Note"));
    EXPECT_EQ(table.rows[1][0].runs[0].text, std::string("a|b"));
    EXPECT_EQ(table.rows[1][1].runs[0].text, std::string("plain"));
    EXPECT_EQ(table.rows[2][1].runs[0].text, std::string("y"));

    // The fenced block keeps its lines, the thematic break has no runs.
    const Block& code = model.blocks[9];
    ASSERT_EQ(code.kind, BlockKind::CodeBlock);
    ASSERT_EQ(code.runs.size(), 1u);
    EXPECT_EQ(code.runs[0].text, std::string("code line 1\n  code line 2\n"));
    EXPECT_TRUE(code.runs[0].mono);
    EXPECT_EQ(model.blocks[10].kind, BlockKind::ThematicBreak);
    EXPECT_EQ(model.blocks[11].kind, BlockKind::Paragraph);
    EXPECT_EQ(model.blocks[11].runs[0].text, std::string("Closing paragraph."));

    // The sample is already canonical, so the emitter reproduces it exactly.
    const std::string emitted = OfficeModelToMarkdown(model);
    if (emitted != sample) {
        std::cerr << "  sample round trip: " << Difference(emitted, sample) << "\n";
    }
    EXPECT_EQ(emitted, sample);

    // And the re-emitted markdown parses back to the same model.
    DocModel again;
    CompatReport second_report;
    std::string second_error;
    ASSERT_TRUE(MarkdownToOfficeModel(emitted, again, second_report, second_error));
    EXPECT_TRUE(second_report.warnings.empty());
    const std::string difference = CompareModels(again, model);
    if (!difference.empty()) {
        std::cerr << "  re-parsed model differs from the parsed one:\n" << difference;
    }
    EXPECT_TRUE(difference.empty());
}

// ---------------------------------------------------------------------------
// What markdown cannot carry is reported
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, FormattedCellsAndFenceLanguagesAreReported) {
    const std::string markdown =
        "| A | B |\n"
        "| --- | --- |\n"
        "| **bold** cell | `code` cell |\n"
        "\n"
        "```c\n"
        "int x = 0;\n"
        "```\n";

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(markdown, model, report, error));
    EXPECT_TRUE(error.empty());

    // The cell text survives, the formatting inside a cell cannot: the table
    // rule renders cells as plain text.
    ASSERT_EQ(model.blocks.size(), 2u);
    ASSERT_EQ(model.blocks[0].kind, BlockKind::Table);
    ASSERT_EQ(model.blocks[0].rows.size(), 2u);
    ASSERT_EQ(model.blocks[0].rows[1].size(), 2u);
    EXPECT_EQ(model.blocks[0].rows[1][0].runs[0].text, std::string("bold cell"));
    EXPECT_FALSE(model.blocks[0].rows[1][0].runs[0].bold);
    EXPECT_EQ(model.blocks[0].rows[1][1].runs[0].text, std::string("code cell"));
    EXPECT_FALSE(model.blocks[0].rows[1][1].runs[0].mono);

    // The code text survives, the fence info string has no model field.
    ASSERT_EQ(model.blocks[1].kind, BlockKind::CodeBlock);
    ASSERT_EQ(model.blocks[1].runs.size(), 1u);
    EXPECT_EQ(model.blocks[1].runs[0].text, std::string("int x = 0;\n"));
    EXPECT_TRUE(model.blocks[1].runs[0].mono);

    ASSERT_EQ(report.warnings.size(), 2u);
    EXPECT_EQ(report.warnings[0].feature, std::string("table cell formatting"));
    EXPECT_EQ(report.warnings[1].feature, std::string("code block language"));
    for (const CompatWarning& warning : report.warnings) {
        EXPECT_TRUE(!warning.detail.empty());
        std::cout << "  reported: " << warning.feature << ": " << warning.detail << "\n";
    }

    // The re-emitted markdown keeps the text and drops the formatting, and it
    // parses back to the same model.
    const std::string emitted = OfficeModelToMarkdown(model);
    EXPECT_EQ(emitted, std::string("| A | B |\n| --- | --- |\n| bold cell | code cell |\n\n"
                                   "```\nint x = 0;\n```\n"));

    DocModel again;
    CompatReport second_report;
    std::string second_error;
    ASSERT_TRUE(MarkdownToOfficeModel(emitted, again, second_report, second_error));
    const std::string difference = CompareModels(again, model);
    if (!difference.empty()) {
        std::cerr << "  re-parsed model differs from the parsed one:\n" << difference;
    }
    EXPECT_TRUE(difference.empty());
}

TEST(OfficeMarkdownBridge, AnImageKeepsItsAltTextAndIsReported) {
    const std::string markdown = "Text before ![alt text](picture.png) text after.\n";

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(markdown, model, report, error));

    ASSERT_EQ(model.blocks.size(), 1u);
    ASSERT_EQ(model.blocks[0].kind, BlockKind::Paragraph);
    // The alt text carries no formatting, so it merges into the plain text
    // around it, exactly as the emitter would write it back out.
    ASSERT_EQ(model.blocks[0].runs.size(), 1u);
    EXPECT_EQ(model.blocks[0].runs[0].text, std::string("Text before alt text text after."));
    EXPECT_TRUE(model.blocks[0].runs[0].link.empty());
    EXPECT_FALSE(model.blocks[0].runs[0].bold);

    ASSERT_EQ(report.warnings.size(), 1u);
    EXPECT_EQ(report.warnings[0].feature, std::string("image"));
    EXPECT_TRUE(!report.warnings[0].detail.empty());

    // Only the alt text is left, so the picture is gone from the markdown too.
    EXPECT_EQ(OfficeModelToMarkdown(model), std::string("Text before alt text text after.\n"));
}

TEST(OfficeMarkdownBridge, BrHtmlTagBecomesLineBreakInExportModel) {
    const std::string markdown = "First line<BR>second line<br />third line\n";

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(markdown, model, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(report.warnings.empty());

    ASSERT_EQ(model.blocks.size(), 1u);
    ASSERT_EQ(model.blocks[0].kind, BlockKind::Paragraph);
    ASSERT_EQ(model.blocks[0].runs.size(), 1u);
    EXPECT_EQ(model.blocks[0].runs[0].text,
              std::string("First line\nsecond line\nthird line"));

    EXPECT_EQ(OfficeModelToMarkdown(model),
              std::string("First line\nsecond line\nthird line\n"));
}

// ---------------------------------------------------------------------------
// Empty and malformed input
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, EmptyMarkdownIsAnEmptyModel) {
    for (const char* input : {"", "\n", "   \n", "\n\n\n"}) {
        DocModel model;
        CompatReport report;
        std::string error;
        ASSERT_TRUE(MarkdownToOfficeModel(input, model, report, error));
        EXPECT_EQ(error, std::string(""));
        EXPECT_TRUE(model.blocks.empty());
        EXPECT_TRUE(model.title.empty());
        EXPECT_TRUE(report.warnings.empty());
    }

    // An empty model is one newline of markdown, which is what the oracle
    // produces for an empty document, and that parses back to an empty model.
    const DocModel empty;
    EXPECT_EQ(OfficeModelToMarkdown(empty), std::string("\n"));

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(OfficeModelToMarkdown(empty), model, report, error));
    EXPECT_TRUE(model.blocks.empty());
    EXPECT_TRUE(model.title.empty());
}

TEST(OfficeMarkdownBridge, MalformedUtf8FailsWithAnErrorAndNoCrash) {
    DocModel model;
    CompatReport report;
    std::string error;

    // A lone continuation byte, a truncated sequence, an overlong form and a
    // surrogate half: all of them are refused, none of them crashes.
    std::string lone_byte = "# Titel\n\n";
    lone_byte.push_back(static_cast<char>(0xFF));
    lone_byte += " tail\n";
    EXPECT_FALSE(MarkdownToOfficeModel(lone_byte, model, report, error));
    EXPECT_TRUE(!error.empty());
    EXPECT_TRUE(model.blocks.empty());
    EXPECT_TRUE(model.title.empty());
    std::cout << "  lone byte: " << error << "\n";

    const std::string truncated = std::string("text ") + static_cast<char>(0xC3);
    EXPECT_FALSE(MarkdownToOfficeModel(truncated, model, report, error));
    EXPECT_TRUE(!error.empty());

    const std::string overlong = std::string("text ") + static_cast<char>(0xC0) +
                                 static_cast<char>(0xAF);
    EXPECT_FALSE(MarkdownToOfficeModel(overlong, model, report, error));
    EXPECT_TRUE(!error.empty());

    const std::string surrogate = std::string("text ") + static_cast<char>(0xED) +
                                  static_cast<char>(0xA0) + static_cast<char>(0x80);
    EXPECT_FALSE(MarkdownToOfficeModel(surrogate, model, report, error));
    EXPECT_TRUE(!error.empty());

    // Real Danish text is valid UTF-8 and passes, unchanged, both ways.
    const std::string danish = std::string("# ") + "\xc3\xa6" + " " + "\xc3\xb8" + " " +
                               "\xc3\xa5" + "\n";
    ASSERT_TRUE(MarkdownToOfficeModel(danish, model, report, error));
    EXPECT_EQ(model.title, std::string("\xc3\xa6") + " " + "\xc3\xb8" + " " + "\xc3\xa5");
    EXPECT_EQ(OfficeModelToMarkdown(model), danish);
}

// ---------------------------------------------------------------------------
// Page breaks and out of range heading levels
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, PageBreakIsDroppedAndReported) {
    std::string golden;
    ASSERT_TRUE(ReadFileBytes(kGoldenReference, golden));

    const DocModel pdf = reference::PdfModel();
    bool has_page_break = false;
    for (const Block& block : pdf.blocks) {
        if (block.kind == BlockKind::PageBreak) has_page_break = true;
    }
    ASSERT_TRUE(has_page_break);

    CompatReport report;
    const std::string markdown = OfficeModelToMarkdown(pdf, report);
    if (markdown != golden) {
        std::cerr << "  pdf model -> markdown: " << Difference(markdown, golden) << "\n";
    }
    EXPECT_EQ(markdown, golden);

    ASSERT_EQ(report.warnings.size(), 1u);
    EXPECT_EQ(report.warnings[0].feature, std::string("page break"));
    EXPECT_TRUE(!report.warnings[0].detail.empty());
    std::cout << "  page break: " << report.warnings[0].detail << "\n";

    // The page break sits before the table, and the blocks around it are glued
    // as if it were not there, so the footer flag changes nothing either.
    EXPECT_EQ(OfficeModelToMarkdown(pdf), golden);
}

TEST(OfficeMarkdownBridge, HeadingLevelsOutsideTheAtxRangeAreClampedAndReported) {
    DocModel doc;
    {
        Block heading;
        heading.kind = Heading;
        heading.level = 9;
        Run run;
        run.text = "Too deep";
        heading.runs.push_back(run);
        doc.blocks.push_back(heading);
    }
    {
        Block heading;
        heading.kind = Heading;
        heading.level = -2;
        Run run;
        run.text = "Negative";
        heading.runs.push_back(run);
        doc.blocks.push_back(heading);
    }

    CompatReport report;
    const std::string markdown = OfficeModelToMarkdown(doc, report);
    EXPECT_EQ(markdown, std::string("###### Too deep\n\n# Negative\n"));
    ASSERT_EQ(report.warnings.size(), 1u);
    EXPECT_EQ(report.warnings[0].feature, std::string("heading level"));
    EXPECT_TRUE(!report.warnings[0].detail.empty());
}

// ---------------------------------------------------------------------------
// The documented limit of the oracle's two spaces per nesting level
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, NestedItemUnderAnOrderedItemComesBackOneLevelShallower) {
    const std::string markdown = "1. Step one\n   - nested under an ordered item\n";

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(markdown, model, report, error));
    ASSERT_EQ(model.blocks.size(), 2u);
    EXPECT_TRUE(model.blocks[0].ordered);
    EXPECT_EQ(model.blocks[0].level, 0);
    EXPECT_FALSE(model.blocks[1].ordered);
    EXPECT_EQ(model.blocks[1].level, 1);

    // The oracle writes two spaces per nesting level, which cannot nest inside
    // an ordered item whose marker is three characters wide, so the second pass
    // reads the nested item as a sibling. See markdown_bridge.h.
    const std::string emitted = OfficeModelToMarkdown(model);
    EXPECT_EQ(emitted, std::string("1. Step one\n  - nested under an ordered item\n"));

    DocModel again;
    CompatReport second_report;
    std::string second_error;
    ASSERT_TRUE(MarkdownToOfficeModel(emitted, again, second_report, second_error));
    ASSERT_EQ(again.blocks.size(), 2u);
    EXPECT_TRUE(again.blocks[0].ordered);
    EXPECT_FALSE(again.blocks[1].ordered);
    EXPECT_EQ(again.blocks[1].level, 0);
}

// ---------------------------------------------------------------------------
// The produced markdown on disk, for the oracle and for CI
// ---------------------------------------------------------------------------

TEST(OfficeMarkdownBridge, WritesTheProducedMarkdownForTheOracle) {
    const char* out_dir = std::getenv("MDI_OFFICE_OUT_DIR");
    if (out_dir == nullptr || *out_dir == '\0') {
        std::cout << "  MDI_OFFICE_OUT_DIR is not set, skipping the markdown dump\n";
        return;
    }

    std::string golden;
    ASSERT_TRUE(ReadFileBytes(kGoldenReference, golden));

    const std::string markdown = OfficeModelToMarkdown(reference::DocxModel());
    const std::string path = std::string(out_dir) + "/reference_from_model.md";
    {
        std::ofstream stream(path.c_str(), std::ios::binary);
        ASSERT_TRUE(stream.good());
        stream << markdown;
    }

    // The bytes on disk are the golden that tools/office-oracle/docx_to_md.py
    // produced from tests/office/fixtures/reference.docx.
    std::string written;
    ASSERT_TRUE(ReadFileBytes(path, written));
    EXPECT_EQ(written, golden);
    std::cout << "  wrote " << path << " (" << written.size() << " bytes); compare with "
              << kGoldenReference << "\n";

    // The other direction on disk: the golden parsed into the model and emitted
    // again is the golden, so the two files are byte for byte the same.
    DocModel parsed;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(MarkdownToOfficeModel(golden, parsed, report, error));
    const std::string round_trip_path = std::string(out_dir) + "/reference_from_markdown.md";
    {
        std::ofstream stream(round_trip_path.c_str(), std::ios::binary);
        ASSERT_TRUE(stream.good());
        stream << OfficeModelToMarkdown(parsed);
    }
    std::string round_trip;
    ASSERT_TRUE(ReadFileBytes(round_trip_path, round_trip));
    EXPECT_EQ(round_trip, golden);
    std::cout << "  wrote " << round_trip_path << " (" << round_trip.size() << " bytes)\n";
}
