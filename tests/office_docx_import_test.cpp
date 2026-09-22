// Office .docx import test.
//
// Proves that src/office/docx_import.cpp reads tests/office/fixtures/reference.docx
// into exactly the model in tests/office_reference_model.h: block kinds,
// heading levels, list kinds and levels, every run flag, links, table cells
// and the page setup. It also covers the 200-page fixture (block and heading
// count, first and last text, import time), the failure cases (random bytes,
// a legacy .doc / encrypted CFB container, a package without
// word/document.xml, unparseable XML, a truncated archive), the compatibility
// report for features the model cannot carry, and the model dump in the
// schema of tools/office-oracle/dump_docx.py that check_model_json.py
// validates against the golden.
//
// Portable C++17: no windows.h, so it runs in CI (windows-2022) and headless
// on Linux. tests/test_main.cpp owns main().

#include "gtest_lite.h"

#include "miniz.h"

#include "office/docx_import.h"
#include "office/office_model.h"
#include "office_reference_model.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace office;

namespace {

const char kReferenceFixture[] = "tests/office/fixtures/reference.docx";
const char kLargeFixture[] = "tests/office/fixtures/large.docx";

// tests/office/golden/large_docx.json: 7440 paragraph blocks, 240 of them
// headings, and the text of the first and the last block.
const size_t kLargeBlockCount = 7440;
const size_t kLargeHeadingCount = 240;
const char kLargeFirstText[] = "Afsnit 1";
const char kLargeLastText[] = "Side 240, linje 30: MarkDownIt 200-side interop test.";

// The measured import speed must stay far below this; the point of the test
// is to report the number, not to fail a slow machine.
const double kSecondsPerMegabyteBudget = 5.0;

// The page setup is compared within a tenth of a point.
const double kPageTolerancePt = 0.1;

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

// Collects the first few differences between the imported model and the
// expected one, each message naming the block index and the field.
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

void CompareFlag(ModelDiff& diff, const std::string& field, bool imported, bool expected) {
    if (imported != expected) {
        std::ostringstream message;
        message << field << ": expected " << (expected ? "true" : "false") << ", imported "
                << (imported ? "true" : "false");
        diff.Add(message.str());
    }
}

void CompareNumber(ModelDiff& diff, const std::string& field, double imported,
                   double expected, double tolerance) {
    if (!(std::fabs(imported - expected) <= tolerance)) {
        std::ostringstream message;
        message << field << ": expected " << expected << ", imported " << imported
                << " (tolerance " << tolerance << ")";
        diff.Add(message.str());
    }
}

void CompareRuns(ModelDiff& diff, const std::string& where, const std::vector<Run>& imported,
                 const std::vector<Run>& expected) {
    if (imported.size() != expected.size()) {
        std::ostringstream message;
        message << where << ": run count expected " << expected.size() << ", imported "
                << imported.size();
        diff.Add(message.str());
        return;
    }
    for (size_t index = 0; index < expected.size(); ++index) {
        std::ostringstream position;
        position << where << " run " << index;
        const Run& want = expected[index];
        const Run& got = imported[index];
        if (got.text != want.text) {
            diff.Add(position.str() + " text: expected " + Quote(want.text) + ", imported " +
                     Quote(got.text));
        }
        CompareFlag(diff, position.str() + " bold", got.bold, want.bold);
        CompareFlag(diff, position.str() + " italic", got.italic, want.italic);
        CompareFlag(diff, position.str() + " mono", got.mono, want.mono);
        CompareFlag(diff, position.str() + " underline", got.underline, want.underline);
        CompareFlag(diff, position.str() + " strike", got.strike, want.strike);
        if (got.link != want.link) {
            diff.Add(position.str() + " link: expected " + Quote(want.link) + ", imported " +
                     Quote(got.link));
        }
    }
}

std::string CompareModels(const DocModel& imported, const DocModel& expected) {
    ModelDiff diff;
    if (imported.title != expected.title) {
        diff.Add("title: expected " + Quote(expected.title) + ", imported " + Quote(imported.title));
    }
    if (imported.blocks.size() != expected.blocks.size()) {
        std::ostringstream message;
        message << "block count: expected " << expected.blocks.size() << ", imported "
                << imported.blocks.size();
        diff.Add(message.str());
    }

    const size_t blocks = imported.blocks.size() < expected.blocks.size()
                              ? imported.blocks.size()
                              : expected.blocks.size();
    for (size_t index = 0; index < blocks; ++index) {
        std::ostringstream tag;
        tag << "block " << index;
        const Block& want = expected.blocks[index];
        const Block& got = imported.blocks[index];
        if (got.kind != want.kind) {
            diff.Add(tag.str() + ": kind expected " + KindName(want.kind) + ", imported " +
                     KindName(got.kind));
            continue;
        }
        if (want.kind == Heading || want.kind == ListItem) {
            if (got.level != want.level) {
                std::ostringstream message;
                message << tag.str() << " (" << KindName(want.kind) << "): level expected "
                        << want.level << ", imported " << got.level;
                diff.Add(message.str());
            }
        }
        if (want.kind == ListItem) {
            CompareFlag(diff, tag.str() + " ordered", got.ordered, want.ordered);
        }
        if (want.kind == Table) {
            if (got.rows.size() != want.rows.size()) {
                std::ostringstream message;
                message << tag.str() << ": row count expected " << want.rows.size()
                        << ", imported " << got.rows.size();
                diff.Add(message.str());
                continue;
            }
            for (size_t row = 0; row < want.rows.size(); ++row) {
                if (got.rows[row].size() != want.rows[row].size()) {
                    std::ostringstream message;
                    message << tag.str() << " row " << row << ": cell count expected "
                            << want.rows[row].size() << ", imported " << got.rows[row].size();
                    diff.Add(message.str());
                    continue;
                }
                for (size_t cell = 0; cell < want.rows[row].size(); ++cell) {
                    std::ostringstream where;
                    where << tag.str() << " row " << row << " cell " << cell;
                    CompareRuns(diff, where.str(), got.rows[row][cell].runs,
                                want.rows[row][cell].runs);
                }
            }
            continue;
        }
        CompareRuns(diff, tag.str(), got.runs, want.runs);
    }

    CompareNumber(diff, "page width_pt", imported.page.width_pt, expected.page.width_pt,
                  kPageTolerancePt);
    CompareNumber(diff, "page height_pt", imported.page.height_pt, expected.page.height_pt,
                  kPageTolerancePt);
    CompareNumber(diff, "page margin_pt", imported.page.margin_pt, expected.page.margin_pt,
                  kPageTolerancePt);
    CompareFlag(diff, "page landscape", imported.page.landscape, expected.page.landscape);
    CompareFlag(diff, "page footer_page_numbers", imported.page.footer_page_numbers,
                expected.page.footer_page_numbers);
    return diff.Text();
}

// A ZIP container built in memory, for the packages the test makes itself.
class ZipBuilder {
public:
    ZipBuilder() {
        std::memset(&writer_, 0, sizeof(writer_));
        ok_ = mz_zip_writer_init_heap(&writer_, 0, 0) == MZ_TRUE;
    }

    ~ZipBuilder() {
        if (ok_) mz_zip_writer_end(&writer_);
    }

    ZipBuilder(const ZipBuilder&) = delete;
    ZipBuilder& operator=(const ZipBuilder&) = delete;

    bool Ok() const { return ok_; }

    bool Add(const std::string& name, const std::string& payload) {
        if (!ok_ || finished_) return false;
        if (mz_zip_writer_add_mem(&writer_, name.c_str(), payload.data(), payload.size(),
                                  MZ_DEFAULT_LEVEL) != MZ_TRUE) {
            ok_ = false;
            return false;
        }
        return true;
    }

    std::string Finish() {
        std::string bytes;
        if (!ok_ || finished_) return bytes;
        finished_ = true;
        void* archive = nullptr;
        size_t archive_size = 0;
        if (mz_zip_writer_finalize_heap_archive(&writer_, &archive, &archive_size) == MZ_TRUE &&
            archive != nullptr) {
            bytes.assign(static_cast<const char*>(archive), archive_size);
            mz_free(archive);
        } else {
            ok_ = false;
        }
        return bytes;
    }

private:
    mz_zip_archive writer_;
    bool ok_ = false;
    bool finished_ = false;
};

int CountFeature(const CompatReport& report, const char* feature) {
    int count = 0;
    for (const CompatWarning& warning : report.warnings) {
        if (warning.feature == feature) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// The content model dump, in the schema of tools/office-oracle/dump_docx.py.
// check_model_json.py reads this file and compares it with
// tests/office/golden/reference_docx.json.
// ---------------------------------------------------------------------------

// JSON string escaping without a single backslash literal in the source: the
// workspace tooling rewrites backslash escapes in C++ sources.
void AppendJsonString(std::string& out, const std::string& text) {
    const char kSlash = static_cast<char>(92);
    const char kQuote = static_cast<char>(34);
    out += kQuote;
    for (size_t index = 0; index < text.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch == static_cast<unsigned char>(kQuote)) {
            out += kSlash;
            out += kQuote;
        } else if (ch == static_cast<unsigned char>(kSlash)) {
            out += kSlash;
            out += kSlash;
        } else if (ch == 10) {
            out += kSlash;
            out += 'n';
        } else if (ch == 13) {
            out += kSlash;
            out += 'r';
        } else if (ch == 9) {
            out += kSlash;
            out += 't';
        } else if (ch < 32) {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "u%04x", static_cast<unsigned int>(ch));
            out += kSlash;
            out += buffer;
        } else {
            out += static_cast<char>(ch);
        }
    }
    out += kQuote;
}

std::string FormatPoints(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    std::string text = buffer;
    while (text.size() > 1 && text[text.size() - 1] == '0') text.erase(text.size() - 1);
    if (!text.empty() && text[text.size() - 1] == '.') text.erase(text.size() - 1);
    return text;
}

void AppendJsonFlag(std::string& out, const char* name, bool value) {
    out += "          \"";
    out += name;
    out += "\": ";
    out += value ? "true" : "false";
    out += ",\n";
}

std::string ModelJson(const DocModel& model) {
    std::string out;
    out += "{\n";
    out += "  \"format\": \"markdownit-office-docx-dump\",\n";
    out += "  \"version\": 1,\n";
    out += "  \"blocks\": [\n";
    for (size_t index = 0; index < model.blocks.size(); ++index) {
        const Block& block = model.blocks[index];
        out += "    {\n      \"kind\": ";
        AppendJsonString(out, KindName(block.kind));
        out += ",\n";
        if (block.kind == Heading) {
            out += "      \"level\": ";
            out += std::to_string(block.level);
            out += ",\n";
        }
        if (block.kind != Table) {
            std::string text;
            for (const Run& run : block.runs) text += run.text;
            out += "      \"text\": ";
            AppendJsonString(out, text);
            out += ",\n";
        }
        if (block.kind == ListItem) {
            out += "      \"list\": {\"kind\": ";
            AppendJsonString(out, block.ordered ? "ordered" : "bullet");
            out += ", \"level\": ";
            out += std::to_string(block.level);
            out += ", \"num_fmt\": ";
            AppendJsonString(out, block.ordered ? "decimal" : "bullet");
            out += "},\n";
        }
        if (block.kind == Table) {
            out += "      \"rows\": [\n";
            for (size_t row = 0; row < block.rows.size(); ++row) {
                out += "        [";
                for (size_t cell = 0; cell < block.rows[row].size(); ++cell) {
                    std::string text;
                    for (const Run& run : block.rows[row][cell].runs) text += run.text;
                    if (cell > 0) out += ", ";
                    out += "{\"text\": ";
                    AppendJsonString(out, text);
                    out += "}";
                }
                out += "]";
                out += row + 1 < block.rows.size() ? ",\n" : "\n";
            }
            out += "      ]\n";
        } else {
            out += "      \"runs\": [\n";
            for (size_t run_index = 0; run_index < block.runs.size(); ++run_index) {
                const Run& run = block.runs[run_index];
                out += "        {\n          \"text\": ";
                AppendJsonString(out, run.text);
                out += ",\n";
                AppendJsonFlag(out, "bold", run.bold);
                AppendJsonFlag(out, "italic", run.italic);
                AppendJsonFlag(out, "mono", run.mono);
                AppendJsonFlag(out, "underline", run.underline);
                out += "          \"link\": ";
                if (run.link.empty()) {
                    out += "null\n";
                } else {
                    AppendJsonString(out, run.link);
                    out += "\n";
                }
                out += "        }";
                out += run_index + 1 < block.runs.size() ? ",\n" : "\n";
            }
            out += "      ]\n";
        }
        out += "    }";
        out += index + 1 < model.blocks.size() ? ",\n" : "\n";
    }
    out += "  ],\n";
    // The model carries one page setup and one margin, so the four margins of
    // the section are the same value.
    out += "  \"sections\": [\n    {\n      \"page_width_pt\": ";
    out += FormatPoints(model.page.width_pt);
    out += ",\n      \"page_height_pt\": ";
    out += FormatPoints(model.page.height_pt);
    out += ",\n      \"margins_pt\": {\"top\": ";
    out += FormatPoints(model.page.margin_pt);
    out += ", \"right\": ";
    out += FormatPoints(model.page.margin_pt);
    out += ", \"bottom\": ";
    out += FormatPoints(model.page.margin_pt);
    out += ", \"left\": ";
    out += FormatPoints(model.page.margin_pt);
    out += "}\n    }\n  ]\n}\n";
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The reference document
// ---------------------------------------------------------------------------

TEST(DocxImport, ReferenceFixtureMatchesTheReferenceModel) {
    std::string bytes;
    ASSERT_TRUE(ReadFileBytes(kReferenceFixture, bytes));
    ASSERT_TRUE(bytes.size() > 0);

    DocModel imported;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(DocxImport(bytes, imported, report, error));
    EXPECT_EQ(error, std::string(""));

    // reference.docx carries only features the model can hold, so nothing may
    // be reported as dropped.
    for (const CompatWarning& warning : report.warnings) {
        std::cerr << "  unexpected warning: " << warning.feature << ": " << warning.detail
                  << "\n";
    }
    EXPECT_TRUE(report.warnings.empty());

    const DocModel expected = reference::DocxModel();
    const std::string difference = CompareModels(imported, expected);
    if (!difference.empty()) {
        std::cerr << "  imported model does not match the reference model:\n" << difference;
    }
    EXPECT_TRUE(difference.empty());
}

// ---------------------------------------------------------------------------
// The 200-page document
// ---------------------------------------------------------------------------

TEST(DocxImport, LargeFixtureImportsFastAndComplete) {
    std::string bytes;
    ASSERT_TRUE(ReadFileBytes(kLargeFixture, bytes));
    ASSERT_TRUE(bytes.size() > 0);

    DocModel imported;
    CompatReport report;
    std::string error;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    ASSERT_TRUE(DocxImport(bytes, imported, report, error));
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double megabytes = static_cast<double>(bytes.size()) / (1024.0 * 1024.0);
    const double seconds_per_megabyte = megabytes > 0.0 ? seconds / megabytes : 0.0;

    std::cout << "  large.docx: " << bytes.size() << " bytes imported in " << seconds << " s ("
              << seconds_per_megabyte << " s/MB, budget " << kSecondsPerMegabyteBudget
              << " s/MB)\n";
    std::cout << "  large.docx: " << imported.blocks.size() << " blocks\n";

    // The counts and the first and last text come from
    // tests/office/golden/large_docx.json.
    EXPECT_EQ(imported.blocks.size(), kLargeBlockCount);
    size_t headings = 0;
    for (const Block& block : imported.blocks) {
        if (block.kind == Heading) ++headings;
    }
    EXPECT_EQ(headings, kLargeHeadingCount);
    EXPECT_TRUE(report.warnings.empty());

    ASSERT_TRUE(!imported.blocks.empty());
    EXPECT_EQ(imported.blocks.front().kind, BlockKind::Heading);
    EXPECT_EQ(imported.blocks.front().level, 1);
    ASSERT_TRUE(!imported.blocks.front().runs.empty());
    EXPECT_EQ(imported.blocks.front().runs[0].text, std::string(kLargeFirstText));
    ASSERT_TRUE(!imported.blocks.back().runs.empty());
    EXPECT_EQ(imported.blocks.back().runs[0].text, std::string(kLargeLastText));

    EXPECT_TRUE(seconds_per_megabyte < kSecondsPerMegabyteBudget);
}

// ---------------------------------------------------------------------------
// Failure cases: every one of them returns false with an error, and none of
// them crashes.
// ---------------------------------------------------------------------------

TEST(DocxImport, RandomBytesFailWithAnError) {
    std::string bytes;
    for (int index = 0; index < 512; ++index) {
        bytes.push_back(static_cast<char>((index * 37 + 11) % 251));
    }

    DocModel model;
    CompatReport report;
    std::string error;
    EXPECT_FALSE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(!error.empty());
    EXPECT_TRUE(model.blocks.empty());
    std::cout << "  random bytes: " << error << "\n";
}

TEST(DocxImport, LegacyDocAndProtectedPackagesAreNamedInTheError) {
    // The CFB signature of a legacy .doc file, or of an encrypted OOXML
    // package, which is also a CFB container.
    std::string bytes;
    const unsigned char header[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    for (unsigned char byte : header) bytes.push_back(static_cast<char>(byte));
    for (int index = 0; index < 120; ++index) bytes.push_back('\0');

    DocModel model;
    CompatReport report;
    std::string error;
    EXPECT_FALSE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(!error.empty());
    // The message has to say what the file is: a legacy .doc or a protected
    // package.
    EXPECT_TRUE(error.find(".doc") != std::string::npos);
    EXPECT_TRUE(error.find("password") != std::string::npos);
    std::cout << "  CFB container: " << error << "\n";
}

TEST(DocxImport, PackageWithoutDocumentXmlFails) {
    ZipBuilder builder;
    ASSERT_TRUE(builder.Ok());
    ASSERT_TRUE(builder.Add("word/styles.xml",
                           "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/"
                           "wordprocessingml/2006/main\"/>"));
    ASSERT_TRUE(builder.Add("_rels/.rels",
                           "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                           "package/2006/relationships\"/>"));
    const std::string bytes = builder.Finish();
    ASSERT_TRUE(bytes.size() > 0);

    DocModel model;
    CompatReport report;
    std::string error;
    EXPECT_FALSE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(!error.empty());
    EXPECT_TRUE(error.find("word/document.xml") != std::string::npos);
    std::cout << "  no document part: " << error << "\n";
}

TEST(DocxImport, UnparseableDocumentXmlFails) {
    ZipBuilder builder;
    ASSERT_TRUE(builder.Ok());
    ASSERT_TRUE(builder.Add("word/document.xml", "<w:document><w:body><w:p>"));
    const std::string bytes = builder.Finish();
    ASSERT_TRUE(bytes.size() > 0);

    DocModel model;
    CompatReport report;
    std::string error;
    EXPECT_FALSE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(!error.empty());
    std::cout << "  malformed XML: " << error << "\n";
}

TEST(DocxImport, TruncatedPackageFails) {
    std::string bytes;
    ASSERT_TRUE(ReadFileBytes(kReferenceFixture, bytes));
    ASSERT_TRUE(bytes.size() > 100);
    bytes.resize(bytes.size() / 2);

    DocModel model;
    CompatReport report;
    std::string error;
    EXPECT_FALSE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(!error.empty());
    std::cout << "  truncated package: " << error << "\n";
}

TEST(DocxImport, EmptyInputFails) {
    const std::string bytes;
    DocModel model;
    CompatReport report;
    std::string error;
    EXPECT_FALSE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(!error.empty());
}

// ---------------------------------------------------------------------------
// The compatibility report
// ---------------------------------------------------------------------------

TEST(DocxImport, UnsupportedFeaturesAreReportedOnceEach) {
    // One package holding every feature the model cannot carry.
    const std::string document =
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\" "
        "xmlns:v=\"urn:schemas-microsoft-com:vml\">"
        "<w:body>"
        "<w:p><w:r><w:t>Hello from the importer</w:t></w:r></w:p>"
        "<w:p><w:r><w:drawing><w:inline/></w:drawing></w:r></w:p>"
        "<w:p><w:r><w:pict><v:shape><v:textbox><w:txbxContent><w:p><w:r><w:t>box</w:t>"
        "</w:r></w:p></w:txbxContent></v:textbox></v:shape></w:pict></w:r></w:p>"
        "<w:p><w:r><w:t>notes </w:t></w:r><w:r><w:footnoteReference w:id=\"1\"/></w:r>"
        "<w:r><w:endnoteReference w:id=\"2\"/></w:r><w:r><w:commentReference w:id=\"3\"/>"
        "</w:r></w:p>"
        "<w:p><w:ins w:id=\"4\"><w:r><w:t>inserted</w:t></w:r></w:ins>"
        "<w:del w:id=\"5\"><w:r><w:delText>removed</w:delText></w:r></w:del></w:p>"
        "<w:p><w:fldSimple w:instr=\" TOC 1-3 \"><w:r><w:t>Contents</w:t></w:r>"
        "</w:fldSimple></w:p>"
        "<w:p><m:oMath><m:r><m:t>x</m:t></m:r></m:oMath></w:p>"
        "<w:sectPr><w:headerReference w:type=\"default\" r:id=\"rId1\"/>"
        "<w:footerReference w:type=\"default\" r:id=\"rId2\"/></w:sectPr>"
        "</w:body></w:document>";

    ZipBuilder builder;
    ASSERT_TRUE(builder.Ok());
    ASSERT_TRUE(builder.Add("word/document.xml", document));
    const std::string bytes = builder.Finish();
    ASSERT_TRUE(bytes.size() > 0);

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(DocxImport(bytes, model, report, error));
    EXPECT_EQ(error, std::string(""));

    // The supported content still imports.
    ASSERT_TRUE(!model.blocks.empty());
    ASSERT_TRUE(!model.blocks[0].runs.empty());
    EXPECT_EQ(model.blocks[0].runs[0].text, std::string("Hello from the importer"));

    const char* const expected_features[] = {
        "image", "text box", "footnote", "endnote", "comment",
        "tracked changes", "header/footer", "table of contents", "equation",
    };
    for (const char* feature : expected_features) {
        const int count = CountFeature(report, feature);
        if (count != 1) {
            std::cerr << "  feature " << feature << " reported " << count << " times\n";
        }
        EXPECT_EQ(count, 1);
    }
    // One entry per feature, no more.
    EXPECT_EQ(report.warnings.size(), sizeof(expected_features) / sizeof(expected_features[0]));
    for (const CompatWarning& warning : report.warnings) {
        EXPECT_TRUE(!warning.detail.empty());
    }
}

// ---------------------------------------------------------------------------
// The mapping rules on a package built for the test: heading styles found by
// name and by styleId, numbering from a style chain, a page break, a
// hyperlink, a multi-paragraph cell and a landscape section.
// ---------------------------------------------------------------------------

TEST(DocxImport, MapsStylesListsTablesAndPageBreaks) {
    const std::string styles =
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\">"
        "<w:name w:val=\"heading 2\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"CustomHeading\">"
        "<w:name w:val=\"Heading 3\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"BulletStyle\">"
        "<w:name w:val=\"List Bullet Custom\"/><w:basedOn w:val=\"Normal\"/>"
        "<w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"7\"/></w:numPr></w:pPr>"
        "</w:style>"
        "<w:style w:type=\"character\" w:styleId=\"Ignored\"><w:name w:val=\"Title\"/></w:style>"
        "</w:styles>";
    const std::string numbering =
        "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:abstractNum w:abstractNumId=\"4\">"
        "<w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/></w:lvl>"
        "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"decimal\"/></w:lvl>"
        "</w:abstractNum>"
        "<w:num w:numId=\"7\"><w:abstractNumId w:val=\"4\"/></w:num>"
        "</w:numbering>";
    const std::string relationships =
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId9\" Type=\"http://schemas.openxmlformats.org/officeDocument/"
        "2006/relationships/hyperlink\" Target=\"https://synthetic.example\" "
        "TargetMode=\"External\"/></Relationships>";
    const std::string document =
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<w:body>"
        "<w:p><w:pPr><w:pStyle w:val=\"Title\"/></w:pPr>"
        "<w:r><w:t>Synthetic title</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"Heading2\"/></w:pPr>"
        "<w:r><w:t>By style id</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"CustomHeading\"/></w:pPr>"
        "<w:r><w:t>By style name</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:b/></w:rPr><w:t>before</w:t>"
        "<w:br w:type=\"page\"/><w:t>after</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"BulletStyle\"/></w:pPr>"
        "<w:r><w:t>styled bullet</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val=\"ListParagraph\"/>"
        "<w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"7\"/></w:numPr></w:pPr>"
        "<w:r><w:t>direct numbered</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>go </w:t></w:r>"
        "<w:hyperlink r:id=\"rId9\"><w:r><w:rPr><w:u w:val=\"single\"/></w:rPr>"
        "<w:t>here</w:t></w:r></w:hyperlink></w:p>"
        "<w:tbl><w:tr>"
        "<w:tc><w:p><w:r><w:t>a</w:t></w:r></w:p><w:p><w:r><w:t>b</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:p/></w:tc>"
        "</w:tr></w:tbl>"
        "<w:sectPr><w:pgSz w:w=\"16838\" w:h=\"11906\"/>"
        "<w:pgMar w:top=\"720\" w:right=\"720\" w:bottom=\"720\" w:left=\"1440\"/></w:sectPr>"
        "</w:body></w:document>";

    ZipBuilder builder;
    ASSERT_TRUE(builder.Ok());
    ASSERT_TRUE(builder.Add("word/document.xml", document));
    ASSERT_TRUE(builder.Add("word/styles.xml", styles));
    ASSERT_TRUE(builder.Add("word/numbering.xml", numbering));
    ASSERT_TRUE(builder.Add("word/_rels/document.xml.rels", relationships));
    const std::string bytes = builder.Finish();
    ASSERT_TRUE(bytes.size() > 0);

    DocModel model;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(DocxImport(bytes, model, report, error));
    EXPECT_TRUE(report.warnings.empty());
    EXPECT_EQ(model.title, std::string("Synthetic title"));

    ASSERT_EQ(model.blocks.size(), 10u);
    EXPECT_EQ(model.blocks[0].kind, BlockKind::Title);
    EXPECT_EQ(model.blocks[0].runs[0].text, std::string("Synthetic title"));

    // "Heading 2" is the style name Word writes, "Heading2" the styleId.
    EXPECT_EQ(model.blocks[1].kind, BlockKind::Heading);
    EXPECT_EQ(model.blocks[1].level, 2);
    EXPECT_EQ(model.blocks[2].kind, BlockKind::Heading);
    EXPECT_EQ(model.blocks[2].level, 3);

    // A page break splits the paragraph into two blocks around a PageBreak,
    // also when the break sits inside a run that holds text on both sides of
    // it; both halves keep the run formatting.
    EXPECT_EQ(model.blocks[3].kind, BlockKind::Paragraph);
    ASSERT_EQ(model.blocks[3].runs.size(), 1u);
    EXPECT_EQ(model.blocks[3].runs[0].text, std::string("before"));
    EXPECT_TRUE(model.blocks[3].runs[0].bold);
    EXPECT_EQ(model.blocks[4].kind, BlockKind::PageBreak);
    EXPECT_TRUE(model.blocks[4].runs.empty());
    EXPECT_EQ(model.blocks[5].kind, BlockKind::Paragraph);
    ASSERT_EQ(model.blocks[5].runs.size(), 1u);
    EXPECT_EQ(model.blocks[5].runs[0].text, std::string("after"));
    EXPECT_TRUE(model.blocks[5].runs[0].bold);

    // Numbering from the style chain, and from the paragraph itself.
    EXPECT_EQ(model.blocks[6].kind, BlockKind::ListItem);
    EXPECT_EQ(model.blocks[6].level, 0);
    EXPECT_FALSE(model.blocks[6].ordered);
    EXPECT_EQ(model.blocks[7].kind, BlockKind::ListItem);
    EXPECT_EQ(model.blocks[7].level, 1);
    EXPECT_TRUE(model.blocks[7].ordered);

    // The hyperlink run keeps its own formatting and carries the target.
    ASSERT_EQ(model.blocks[8].runs.size(), 2u);
    EXPECT_EQ(model.blocks[8].runs[1].text, std::string("here"));
    EXPECT_EQ(model.blocks[8].runs[1].link, std::string("https://synthetic.example"));
    EXPECT_TRUE(model.blocks[8].runs[1].underline);

    // Cells report text; the paragraphs of one cell are joined with a newline.
    ASSERT_EQ(model.blocks[9].kind, BlockKind::Table);
    ASSERT_EQ(model.blocks[9].rows.size(), 1u);
    ASSERT_EQ(model.blocks[9].rows[0].size(), 2u);
    ASSERT_EQ(model.blocks[9].rows[0][0].runs.size(), 1u);
    EXPECT_EQ(model.blocks[9].rows[0][0].runs[0].text, std::string("a\nb"));
    ASSERT_EQ(model.blocks[9].rows[0][1].runs.size(), 1u);
    EXPECT_EQ(model.blocks[9].rows[0][1].runs[0].text, std::string(""));

    // A wider than tall page is landscape, and the left margin is 1440 twips.
    EXPECT_TRUE(model.page.landscape);
    EXPECT_NEAR(model.page.width_pt, 841.9, kPageTolerancePt);
    EXPECT_NEAR(model.page.height_pt, 595.3, kPageTolerancePt);
    EXPECT_NEAR(model.page.margin_pt, 72.0, kPageTolerancePt);
}

// ---------------------------------------------------------------------------
// The model dump for tools/office-oracle/check_model_json.py
// ---------------------------------------------------------------------------

TEST(DocxImport, WritesTheModelJsonForTheOracle) {
    const char* out_dir = std::getenv("MDI_OFFICE_OUT_DIR");
    if (out_dir == nullptr || *out_dir == '\0') {
        std::cout << "  MDI_OFFICE_OUT_DIR is not set, skipping the model dump\n";
        return;
    }

    std::string bytes;
    ASSERT_TRUE(ReadFileBytes(kReferenceFixture, bytes));
    ASSERT_TRUE(bytes.size() > 0);

    DocModel imported;
    CompatReport report;
    std::string error;
    ASSERT_TRUE(DocxImport(bytes, imported, report, error));

    const std::string payload = ModelJson(imported);
    const std::string path = std::string(out_dir) + "/imported_docx_model.json";
    {
        std::ofstream stream(path.c_str(), std::ios::binary);
        ASSERT_TRUE(stream.good());
        stream << payload;
    }

    std::string written;
    ASSERT_TRUE(ReadFileBytes(path, written));
    EXPECT_EQ(written, payload);
    std::cout << "  wrote " << path << " (" << payload.size() << " bytes); validate with "
              << "tools/office-oracle/check_model_json.py\n";
}
