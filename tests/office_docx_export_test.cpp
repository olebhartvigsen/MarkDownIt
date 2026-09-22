// Word .docx writer tests.
//
// The writer is judged by an independent reader, not by itself: the produced
// file is checked against the committed golden content model with
// tools/office-oracle/check_docx.py (python-docx). The structural assertions
// below are the fast local loop around that, and they pin the parts of the
// package the oracle does not look at (content types, styles, numbering).
//
// Portable C++17, no windows.h: the same test binary runs in CI (windows-2022)
// and headless on Linux. The oracle check needs the oracle interpreter, which
// only the Linux container has, so it is skipped where it is not available and
// runs again from the produced file in the CI verify-export job.

#include "gtest_lite.h"

#include "miniz.h"
#include "pugixml.hpp"

#include "office/docx_export.h"
#include "office/office_model.h"
#include "office_reference_model.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace office;

namespace {

// ---------------------------------------------------------------------------
// Environment

std::string EnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string JoinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) return name;
    const char last = directory[directory.size() - 1];
    if (last == '/' || last == '\\') return directory + name;
    return directory + "/" + name;
}

// Where produced files go: MDI_OFFICE_OUT_DIR when the build sets it (CI does,
// so an independent reader can pick the file up), TMPDIR otherwise.
std::string OutputDir() {
    const std::string configured = EnvValue("MDI_OFFICE_OUT_DIR");
    if (!configured.empty()) return configured;
#if defined(_WIN32)
    const char* names[] = {"TEMP", "TMP"};
#else
    const char* names[] = {"TMPDIR", "TMP", "TEMP"};
#endif
    for (const char* name : names) {
        const std::string value = EnvValue(name);
        if (!value.empty()) return value;
    }
#if defined(_WIN32)
    return std::string(".");
#else
    return std::string("/tmp");
#endif
}

// The checkout root, for the oracle scripts. __FILE__ is the test source as the
// compiler was given it, which is the absolute path in every build here; the
// env override is the escape hatch when it is not.
std::string RepoRoot() {
    const std::string configured = EnvValue("MDI_REPO_ROOT");
    if (!configured.empty()) return configured;
    std::string file = __FILE__;
    for (char& character : file) {
        if (character == '\\') character = '/';
    }
    const std::size_t slash = file.find_last_of('/');
    if (slash == std::string::npos) return std::string(".");
    const std::string directory = file.substr(0, slash);          // .../tests
    const std::size_t parent = directory.find_last_of('/');
    if (parent == std::string::npos) return std::string(".");
    return directory.substr(0, parent);
}

std::string OraclePython() {
    const std::string configured = EnvValue("MDI_ORACLE_PY");
    if (!configured.empty()) return configured;
    const std::string root = RepoRoot();
#if defined(_WIN32)
    return JoinPath(JoinPath(root, ".venv-oracle\\Scripts"), "python.exe");
#else
    return JoinPath(JoinPath(root, ".venv-oracle/bin"), "python");
#endif
}

bool FileExists(const std::string& path) {
    std::ifstream stream(path.c_str(), std::ios::binary);
    return stream.good();
}

std::string ReadFile(const std::string& path) {
    std::ifstream stream(path.c_str(), std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool WriteFile(const std::string& path, const std::string& bytes) {
    std::ofstream stream(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    return !stream.fail() && FileExists(path);
}

// Make sure the directory a produced file goes into exists. The build creates
// it, this is only the safety net.
void EnsureDirectory(const std::string& directory) {
    if (directory.empty()) return;
#if defined(_WIN32)
    const std::string command = "mkdir \"" + directory + "\" 2>nul";
#else
    const std::string command = "mkdir -p \"" + directory + "\"";
#endif
    std::system(command.c_str());
}

// ---------------------------------------------------------------------------
// Running the oracle

struct CommandResult {
    bool ran = false;
    int exit_code = -1;
    std::string output;
};

// Quotes for the platform shell. Paths in this workspace carry no quotes, so a
// plain double quoted argument is enough on both sh and cmd.exe.
std::string Quote(const std::string& value) { return "\"" + value + "\""; }

CommandResult RunCommand(const std::string& command, const std::string& log_path) {
    std::remove(log_path.c_str());
    const std::string full = command + " > " + Quote(log_path) + " 2>&1";
    const int raw = std::system(full.c_str());

    CommandResult result;
    result.ran = true;
    if (raw == -1) {
        result.exit_code = -1;
    }
#if defined(_WIN32)
    else {
        result.exit_code = raw;
    }
#else
    else if ((raw & 0x7F) == 0) {
        result.exit_code = (raw >> 8) & 0xFF;
    } else {
        result.exit_code = -1;   // killed by a signal
    }
#endif
    result.output = ReadFile(log_path);
    return result;
}

// The independent validator, when this machine has it. An unavailable oracle is
// not a failure here: CI validates the produced file in the verify-export job.
CommandResult RunOracle(const std::string& script, const std::string& argument,
                        const std::string& log_name) {
    const std::string python = OraclePython();
    const std::string script_path = JoinPath(JoinPath(RepoRoot(), "tools/office-oracle"), script);
    if (!FileExists(python) || !FileExists(script_path)) return CommandResult();
    return RunCommand(Quote(python) + " " + Quote(script_path) + " " + Quote(argument),
                      JoinPath(OutputDir(), log_name));
}

// ---------------------------------------------------------------------------
// Reading the produced package

class Package {
public:
    explicit Package(const std::string& bytes) : bytes_(bytes) {
        std::memset(&zip_, 0, sizeof(zip_));
        open_ = mz_zip_reader_init_mem(&zip_, bytes_.data(), bytes_.size(), 0) == MZ_TRUE;
    }

    ~Package() {
        if (open_) mz_zip_reader_end(&zip_);
    }

    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;

    bool open() { return open_; }
    std::size_t count() {
        return open_ ? static_cast<std::size_t>(mz_zip_reader_get_num_files(&zip_)) : 0;
    }

    bool has(const std::string& name) {
        return open_ && mz_zip_reader_locate_file(&zip_, name.c_str(), nullptr, 0) >= 0;
    }

    std::string name(std::size_t index) {
        char buffer[512];
        std::memset(buffer, 0, sizeof(buffer));
        const mz_uint length =
            mz_zip_reader_get_filename(&zip_, static_cast<mz_uint>(index), buffer, sizeof(buffer));
        if (length == 0) return std::string();
        return std::string(buffer);
    }

    std::string read(const std::string& name) {
        const int index = mz_zip_reader_locate_file(&zip_, name.c_str(), nullptr, 0);
        if (index < 0) return std::string();
        mz_zip_archive_file_stat stat;
        std::memset(&stat, 0, sizeof(stat));
        if (mz_zip_reader_file_stat(&zip_, static_cast<mz_uint>(index), &stat) != MZ_TRUE)
            return std::string();
        std::string bytes(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        if (!bytes.empty() &&
            mz_zip_reader_extract_to_mem(&zip_, static_cast<mz_uint>(index), &bytes[0],
                                         bytes.size(), 0) != MZ_TRUE)
            return std::string();
        return bytes;
    }

    bool parses(const std::string& name, pugi::xml_document& document) {
        const std::string bytes = read(name);
        if (bytes.empty()) return false;
        const pugi::xml_parse_result result =
            document.load_buffer(bytes.data(), bytes.size(), pugi::parse_default);
        if (!result) {
            std::cerr << "  " << name << " does not parse: " << result.description() << "\n";
            return false;
        }
        return true;
    }

private:
    std::string bytes_;
    mz_zip_archive zip_;
    bool open_ = false;
};

// ---------------------------------------------------------------------------
// Small XML helpers

std::string Attr(const pugi::xml_node& node, const char* name) {
    return std::string(node.attribute(name).value());
}

std::string TextOf(const pugi::xml_node& node) { return std::string(node.text().get()); }

std::string ExtensionOf(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : name.substr(dot + 1);
}

// Style id of a paragraph, empty when it carries none.
std::string StyleOf(const pugi::xml_node& paragraph) {
    const pugi::xml_node properties = paragraph.child("w:pPr");
    if (!properties) return std::string();
    const pugi::xml_node style = properties.child("w:pStyle");
    if (!style) return std::string();
    return Attr(style, "w:val");
}

std::size_t CountDescendants(const pugi::xml_node& node, const char* name) {
    std::size_t count = 0;
    for (const pugi::xml_node& child : node.children()) {
        if (std::strcmp(child.name(), name) == 0) ++count;
        count += CountDescendants(child, name);
    }
    return count;
}

// The numFmt a numId reports for one list level, read out of numbering.xml.
std::string NumberFormatFor(Package& package, const std::string& number_id, int level) {
    pugi::xml_document numbering;
    if (!package.parses("word/numbering.xml", numbering)) return std::string();
    const pugi::xml_node root = numbering.child("w:numbering");

    std::string abstract_id;
    for (const pugi::xml_node& num : root.children("w:num")) {
        if (Attr(num, "w:numId") == number_id)
            abstract_id = Attr(num.child("w:abstractNumId"), "w:val");
    }
    if (abstract_id.empty()) return std::string();

    for (const pugi::xml_node& abstract : root.children("w:abstractNum")) {
        if (Attr(abstract, "w:abstractNumId") != abstract_id) continue;
        for (const pugi::xml_node& definition : abstract.children("w:lvl")) {
            if (Attr(definition, "w:ilvl") != std::to_string(level)) continue;
            return Attr(definition.child("w:numFmt"), "w:val");
        }
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Export helpers

struct Export {
    bool ok = false;
    std::string bytes;
    CompatReport report;
    std::string error;
};

Export ExportModel(const DocModel& doc) {
    Export out;
    out.ok = DocxExport(doc, out.bytes, out.report, out.error);
    return out;
}

bool HasWarning(const Export& out, const std::string& feature) {
    for (const CompatWarning& warning : out.report.warnings) {
        if (warning.feature == feature) return true;
    }
    return false;
}

// The body of the produced document.xml, or an empty node when it is missing.
pugi::xml_node BodyOf(pugi::xml_document& document) {
    return document.child("w:document").child("w:body");
}

// Every w:p that is a direct child of the body, in document order.
std::vector<pugi::xml_node> BodyParagraphs(const pugi::xml_node& body) {
    std::vector<pugi::xml_node> paragraphs;
    for (pugi::xml_node node = body.child("w:p"); node; node = node.next_sibling("w:p"))
        paragraphs.push_back(node);
    return paragraphs;
}

std::size_t ModelParagraphCount(const DocModel& doc) {
    std::size_t count = 0;
    for (const Block& block : doc.blocks) {
        if (block.kind != BlockKind::Table) ++count;
    }
    return count;
}

std::size_t ModelTableCount(const DocModel& doc) {
    std::size_t count = 0;
    for (const Block& block : doc.blocks) {
        if (block.kind == BlockKind::Table) ++count;
    }
    return count;
}

std::size_t ModelRunCount(const DocModel& doc) {
    std::size_t count = 0;
    for (const Block& block : doc.blocks) {
        count += block.runs.size();
        for (const std::vector<Cell>& row : block.rows) {
            for (const Cell& cell : row) count += cell.runs.size();
        }
    }
    return count;
}

const char* const kRequiredParts[] = {
    "[Content_Types].xml", "_rels/.rels", "word/document.xml",
    "word/_rels/document.xml.rels", "word/styles.xml", "word/numbering.xml",
};

// Writes a produced file where the build wants it, creating the directory when
// the build did not. The path is empty when the file could not be written.
std::string WriteProduced(const std::string& bytes, const std::string& file_name) {
    const std::string path = JoinPath(OutputDir(), file_name);
    if (!WriteFile(path, bytes)) {
        EnsureDirectory(OutputDir());
        if (!WriteFile(path, bytes)) {
            std::cerr << "  cannot write " << path << "\n";
            return std::string();
        }
    }
    return path;
}

// Writes the produced file where the build wants it and runs the independent
// validator on it. Returns false when the oracle is not available here.
bool CheckWithOracle(const std::string& bytes, const std::string& file_name) {
    const std::string path = WriteProduced(bytes, file_name);
    if (path.empty()) return false;

    const CommandResult result = RunOracle("check_docx.py", path, "check_docx.log");
    if (!result.ran) {
        std::cout << "  oracle unavailable here; wrote " << path << " for the CI verify job\n";
        return false;
    }
    std::cout << "  check_docx.py " << path << "\n" << result.output;
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("OK:") != std::string::npos);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Tests

TEST(OfficeDocxExport, ReferenceModelPackageIsComplete) {
    const DocModel doc = reference::DocxModel();
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);
    EXPECT_TRUE(out.error.empty());
    ASSERT_TRUE(out.bytes.size() > 0);

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    EXPECT_EQ(package.count(), 6u);   // the six parts, no footer was asked for

    for (const char* name : kRequiredParts) {
        EXPECT_TRUE(package.has(name));
        pugi::xml_document parsed;
        EXPECT_TRUE(package.parses(name, parsed));
    }

    // Every part must be named in [Content_Types].xml, or Word offers to repair
    // the file.
    pugi::xml_document content_types;
    ASSERT_TRUE(package.parses("[Content_Types].xml", content_types));
    const pugi::xml_node types = content_types.child("Types");
    ASSERT_TRUE(static_cast<bool>(types));
    for (std::size_t index = 0; index < package.count(); ++index) {
        const std::string name = package.name(index);
        if (name == "[Content_Types].xml") continue;
        bool listed = false;
        for (pugi::xml_node node = types.child("Override"); node && !listed;
             node = node.next_sibling("Override")) {
            if (Attr(node, "PartName") == "/" + name) listed = true;
        }
        for (pugi::xml_node node = types.child("Default"); node && !listed;
             node = node.next_sibling("Default")) {
            if (Attr(node, "Extension") == ExtensionOf(name)) listed = true;
        }
        if (!listed) std::cerr << "  part missing from [Content_Types].xml: " << name << "\n";
        EXPECT_TRUE(listed);
    }
    EXPECT_EQ(Attr(types.child("Override"), "PartName"), std::string("/word/document.xml"));
}

TEST(OfficeDocxExport, BlocksBecomeParagraphsWithTheModelStyles) {
    const DocModel doc = reference::DocxModel();
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);

    Package package(out.bytes);
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));

    // One paragraph per block, one table per table block: the reader walks the
    // body, so a missing or extra paragraph shifts everything after it.
    EXPECT_EQ(BodyParagraphs(body).size(), ModelParagraphCount(doc));
    EXPECT_EQ(CountDescendants(body, "w:tbl"), ModelTableCount(doc));

    // One Word run per model run, never merged.
    EXPECT_EQ(CountDescendants(body, "w:r"), ModelRunCount(doc));

    // Every w:t keeps its spaces.
    std::size_t texts = 0;
    std::vector<pugi::xml_node> pending;
    pending.push_back(body);
    while (!pending.empty()) {
        const pugi::xml_node node = pending.back();
        pending.pop_back();
        for (const pugi::xml_node& child : node.children()) {
            if (std::strcmp(child.name(), "w:t") == 0) {
                ++texts;
                if (Attr(child, "xml:space") != "preserve")
                    std::cerr << "  w:t without xml:space=preserve\n";
                EXPECT_EQ(Attr(child, "xml:space"), std::string("preserve"));
            }
            pending.push_back(child);
        }
    }
    EXPECT_GT(texts, 0u);

    // Style, list numbering and heading level per block, in model order.
    const std::vector<pugi::xml_node> paragraphs = BodyParagraphs(body);
    std::size_t index = 0;
    for (const Block& block : doc.blocks) {
        if (block.kind == BlockKind::Table) continue;
        ASSERT_GE(paragraphs.size(), index + 1);
        const pugi::xml_node& paragraph = paragraphs[index];
        ++index;

        const std::string style = StyleOf(paragraph);
        if (block.kind == BlockKind::Title) {
            EXPECT_EQ(style, std::string("Title"));
        } else if (block.kind == BlockKind::Heading) {
            EXPECT_EQ(style, "Heading" + std::to_string(block.level));
        } else if (block.kind == BlockKind::ListItem) {
            EXPECT_EQ(style, std::string("ListParagraph"));
            const pugi::xml_node number_properties = paragraph.child("w:pPr").child("w:numPr");
            EXPECT_EQ(Attr(number_properties.child("w:ilvl"), "w:val"),
                      std::to_string(block.level));
            const std::string number_id = block.ordered ? "11" : "10";
            EXPECT_EQ(Attr(number_properties.child("w:numId"), "w:val"), number_id);
            EXPECT_EQ(NumberFormatFor(package, number_id, block.level),
                      std::string(block.ordered ? "decimal" : "bullet"));
        } else {
            EXPECT_TRUE(style.empty());
        }
    }
    EXPECT_EQ(index, paragraphs.size());

    // The section carries the model's page setup: A4 portrait, 2 cm margins.
    const pugi::xml_node section = body.child("w:sectPr");
    ASSERT_TRUE(static_cast<bool>(section));
    EXPECT_FALSE(static_cast<bool>(section.next_sibling()));   // the section ends the body
    EXPECT_EQ(Attr(section.child("w:pgSz"), "w:w"), std::string("11906"));
    EXPECT_EQ(Attr(section.child("w:pgSz"), "w:h"), std::string("16838"));
    for (const char* side : {"w:top", "w:right", "w:bottom", "w:left"}) {
        EXPECT_EQ(Attr(section.child("w:pgMar"), side), std::string("1134"));
    }
}

TEST(OfficeDocxExport, MonoRunKeepsConsolasAndInlineFormatting) {
    const DocModel doc = reference::DocxModel();
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);

    Package package(out.bytes);
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));

    // The model has exactly one mono run; the file must have exactly one run
    // naming a monospace font, and it must be the same text.
    std::string model_mono;
    for (const Block& block : doc.blocks) {
        for (const Run& run : block.runs) {
            if (run.mono) model_mono = run.text;
        }
    }
    ASSERT_TRUE(!model_mono.empty());

    std::string found_text;
    int mono_runs = 0;
    std::vector<pugi::xml_node> pending;
    pending.push_back(body);
    while (!pending.empty()) {
        const pugi::xml_node node = pending.back();
        pending.pop_back();
        if (std::strcmp(node.name(), "w:r") == 0) {
            const pugi::xml_node fonts = node.child("w:rPr").child("w:rFonts");
            if (fonts && Attr(fonts, "w:ascii") == "Consolas") {
                ++mono_runs;
                found_text = TextOf(node.child("w:t"));
                EXPECT_EQ(Attr(fonts, "w:hAnsi"), std::string("Consolas"));
                EXPECT_EQ(Attr(fonts, "w:cs"), std::string("Consolas"));
            }
        }
        for (const pugi::xml_node& child : node.children()) pending.push_back(child);
    }
    EXPECT_EQ(mono_runs, 1);
    EXPECT_EQ(found_text, model_mono);

    // Bold, italic and underline are direct run formatting: the reader does not
    // resolve styles, so they have to be on the run itself.
    EXPECT_EQ(CountDescendants(body, "w:b"), 1u);
    EXPECT_EQ(CountDescendants(body, "w:i"), 1u);
    EXPECT_EQ(CountDescendants(body, "w:u"), 2u);   // the underlined run and the link
}

TEST(OfficeDocxExport, HyperlinkRunBecomesAnExternalRelationship) {
    const DocModel doc = reference::DocxModel();
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);

    Package package(out.bytes);
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));

    // Find the link run in the model: text plus target.
    std::string link_text;
    std::string link_target;
    for (const Block& block : doc.blocks) {
        for (const Run& run : block.runs) {
            if (!run.link.empty()) {
                link_text = run.text;
                link_target = run.link;
            }
        }
    }
    ASSERT_TRUE(!link_target.empty());

    // The relationship: external, and pointing at the target URL.
    pugi::xml_document relationships;
    ASSERT_TRUE(package.parses("word/_rels/document.xml.rels", relationships));
    std::string relationship_id;
    for (pugi::xml_node node = relationships.child("Relationships").child("Relationship"); node;
         node = node.next_sibling("Relationship")) {
        if (Attr(node, "Target") != link_target) continue;
        relationship_id = Attr(node, "Id");
        EXPECT_EQ(Attr(node, "TargetMode"), std::string("External"));
        EXPECT_TRUE(Attr(node, "Type").find("/hyperlink") != std::string::npos);
    }
    if (relationship_id.empty()) std::cerr << "  no relationship for " << link_target << "\n";
    ASSERT_TRUE(!relationship_id.empty());

    // The run sits inside the hyperlink, keeps its underline, and no other run
    // claims the relationship.
    int hyperlinks = 0;
    bool matched = false;
    std::vector<pugi::xml_node> pending;
    pending.push_back(body);
    while (!pending.empty()) {
        const pugi::xml_node node = pending.back();
        pending.pop_back();
        if (std::strcmp(node.name(), "w:hyperlink") == 0) {
            ++hyperlinks;
            EXPECT_EQ(Attr(node, "r:id"), relationship_id);
            const pugi::xml_node run = node.child("w:r");
            ASSERT_TRUE(static_cast<bool>(run));
            EXPECT_EQ(TextOf(run.child("w:t")), link_text);
            EXPECT_TRUE(static_cast<bool>(run.child("w:rPr").child("w:u")));
            matched = true;
        }
        for (const pugi::xml_node& child : node.children()) pending.push_back(child);
    }
    EXPECT_EQ(hyperlinks, 1);
    EXPECT_TRUE(matched);
}

TEST(OfficeDocxExport, TableKeepsItsShapeAndCellText) {
    const DocModel doc = reference::DocxModel();
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);

    Package package(out.bytes);
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));

    const Block* model_table = nullptr;
    for (const Block& block : doc.blocks) {
        if (block.kind == BlockKind::Table) model_table = &block;
    }
    ASSERT_TRUE(model_table != nullptr);

    const pugi::xml_node table = body.child("w:tbl");
    ASSERT_TRUE(static_cast<bool>(table));

    // Grid columns match the widest model row.
    std::size_t columns = 0;
    for (const std::vector<Cell>& row : model_table->rows) columns = std::max(columns, row.size());
    EXPECT_EQ(CountDescendants(table.child("w:tblGrid"), "w:gridCol"), columns);

    // Rows and cells, in order, with the model's text.
    std::size_t row_index = 0;
    for (pugi::xml_node row = table.child("w:tr"); row; row = row.next_sibling("w:tr")) {
        ASSERT_TRUE(row_index < model_table->rows.size());
        const std::vector<Cell>& model_row = model_table->rows[row_index];
        std::size_t cell_index = 0;
        for (pugi::xml_node cell = row.child("w:tc"); cell; cell = cell.next_sibling("w:tc")) {
            ASSERT_TRUE(cell_index < model_row.size());
            const Cell& model_cell = model_row[cell_index];
            std::string expected;
            for (const Run& run : model_cell.runs) expected += run.text;
            EXPECT_EQ(TextOf(cell.child("w:p").child("w:r").child("w:t")), expected);
            ++cell_index;
        }
        EXPECT_EQ(cell_index, model_row.size());
        ++row_index;
    }
    EXPECT_EQ(row_index, model_table->rows.size());
    EXPECT_EQ(row_index, 4u);
    EXPECT_EQ(columns, 3u);

    // The table carries the grid style it names, or Word shows no borders.
    EXPECT_EQ(Attr(table.child("w:tblPr").child("w:tblStyle"), "w:val"),
              std::string("TableGrid"));
    pugi::xml_document styles;
    ASSERT_TRUE(package.parses("word/styles.xml", styles));
    bool grid_style = false;
    for (pugi::xml_node node = styles.child("w:styles").child("w:style"); node;
         node = node.next_sibling("w:style")) {
        if (Attr(node, "w:styleId") == "TableGrid") grid_style = true;
    }
    EXPECT_TRUE(grid_style);
}

TEST(OfficeDocxExport, ReferenceModelMatchesTheGoldenContentModel) {
    const DocModel doc = reference::DocxModel();
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);
    // The reference model needs nothing dropped.
    for (const CompatWarning& warning : out.report.warnings)
        std::cerr << "  unexpected compat warning: " << warning.feature << "\n";
    EXPECT_TRUE(out.report.warnings.empty());
    CheckWithOracle(out.bytes, "reference_from_model.docx");
}

TEST(OfficeDocxExport, ImageBlockWarnsButStillExports) {
    DocModel doc;
    doc.title = "Billede";
    Block paragraph;
    paragraph.kind = BlockKind::Paragraph;
    paragraph.runs.push_back(reference::Plain("Foer tekst."));
    doc.blocks.push_back(paragraph);

    Block image;
    image.kind = BlockKind::Image;
    image.runs.push_back(reference::Plain("Et billede"));
    doc.blocks.push_back(image);

    const Export out = ExportModel(doc);
    EXPECT_TRUE(out.ok);
    EXPECT_TRUE(out.error.empty());
    EXPECT_TRUE(HasWarning(out, "Image"));
    ASSERT_TRUE(out.bytes.size() > 0);

    // The export still produces a package Word can open, with the block text
    // kept: the picture is what cannot be carried, not the words.
    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));
    EXPECT_EQ(BodyParagraphs(body).size(), 2u);
    EXPECT_EQ(TextOf(BodyParagraphs(body)[1].child("w:r").child("w:t")),
              std::string("Et billede"));

    // The model title has no Title block to carry it, and that is reported too.
    EXPECT_TRUE(HasWarning(out, "DocumentTitle"));
}

TEST(OfficeDocxExport, EmptyModelStillProducesAPackage) {
    const DocModel doc;
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);
    EXPECT_TRUE(out.error.empty());
    EXPECT_TRUE(out.report.warnings.empty());

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    for (const char* name : kRequiredParts) EXPECT_TRUE(package.has(name));

    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));
    EXPECT_TRUE(BodyParagraphs(body).empty());
    EXPECT_TRUE(static_cast<bool>(body.child("w:sectPr")));

    // An empty document is still a document: the reader has to accept it.
    const std::string path = WriteProduced(out.bytes, "empty_model.docx");
    ASSERT_TRUE(!path.empty());
    const CommandResult result = RunOracle("dump_docx.py", path, "dump_docx.log");
    if (result.ran) {
        std::cout << "  dump_docx.py " << path << "\n" << result.output;
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("\"blocks\": []") != std::string::npos);
    }
}

TEST(OfficeDocxExport, PageNumbersAddAFooterPart) {
    DocModel doc = reference::DocxModel();
    doc.page.footer_page_numbers = true;
    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    EXPECT_EQ(package.count(), 7u);   // the six parts plus the footer
    EXPECT_TRUE(package.has("word/footer1.xml"));

    // A part Word cannot find, or cannot type, is a repair dialog.
    pugi::xml_document content_types;
    ASSERT_TRUE(package.parses("[Content_Types].xml", content_types));
    bool footer_typed = false;
    for (pugi::xml_node node = content_types.child("Types").child("Override"); node;
         node = node.next_sibling("Override")) {
        if (Attr(node, "PartName") == "/word/footer1.xml") {
            footer_typed = Attr(node, "ContentType").find("footer+xml") != std::string::npos;
        }
    }
    EXPECT_TRUE(footer_typed);

    // The section points at the footer through the relationship.
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));
    const std::string footer_id = Attr(body.child("w:sectPr").child("w:footerReference"), "r:id");
    ASSERT_TRUE(!footer_id.empty());
    EXPECT_EQ(Attr(body.child("w:sectPr").child("w:footerReference"), "w:type"),
              std::string("default"));

    pugi::xml_document relationships;
    ASSERT_TRUE(package.parses("word/_rels/document.xml.rels", relationships));
    bool linked = false;
    for (pugi::xml_node node = relationships.child("Relationships").child("Relationship"); node;
         node = node.next_sibling("Relationship")) {
        if (Attr(node, "Id") == footer_id) {
            linked = Attr(node, "Target") == "footer1.xml" &&
                     Attr(node, "Type").find("/footer") != std::string::npos;
        }
    }
    EXPECT_TRUE(linked);

    // "Side N": the fixed text plus a PAGE field.
    pugi::xml_document footer;
    ASSERT_TRUE(package.parses("word/footer1.xml", footer));
    const pugi::xml_node footer_body = footer.child("w:ftr");
    ASSERT_TRUE(static_cast<bool>(footer_body));
    EXPECT_EQ(TextOf(footer_body.child("w:p").child("w:r").child("w:t")), std::string("Side "));
    bool page_field = false;
    for (pugi::xml_node node = footer_body.child("w:p").child("w:r"); node;
         node = node.next_sibling("w:r")) {
        const pugi::xml_node instruction = node.child("w:instrText");
        if (instruction && TextOf(instruction).find("PAGE") != std::string::npos)
            page_field = true;
    }
    EXPECT_TRUE(page_field);

    // The footer does not disturb the content model the golden describes.
    CheckWithOracle(out.bytes, "reference_from_model_with_footer.docx");

#if !defined(_WIN32)
    // python-docx reads footers lazily, so the oracle dump above never touches
    // the part. Ask it directly, once, where the oracle interpreter lives.
    const std::string python = OraclePython();
    if (FileExists(python)) {
        const std::string path = JoinPath(OutputDir(), "reference_from_model_with_footer.docx");
        const std::string probe =
            "'import sys; from docx import Document; "
            "print(Document(sys.argv[1]).sections[0].footer.paragraphs[0].text)'";
        const CommandResult result =
            RunCommand(Quote(python) + " -c " + probe + " " + Quote(path),
                       JoinPath(OutputDir(), "footer_probe.log"));
        std::cout << "  footer text as python-docx sees it: " << result.output;
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("Side 1") != std::string::npos);
    }
#endif
}

TEST(OfficeDocxExport, HyperlinkInsideATableCellKeepsItsRelationship) {
    DocModel doc;
    Block table;
    table.kind = BlockKind::Table;
    std::vector<Cell> row;
    Cell cell;
    cell.runs.push_back(reference::Plain("Se "));
    cell.runs.push_back(reference::Hyperlink("example.com", "https://example.com"));
    row.push_back(cell);
    table.rows.push_back(row);
    doc.blocks.push_back(table);

    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);
    EXPECT_TRUE(out.report.warnings.empty());

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));

    const pugi::xml_node paragraph = body.child("w:tbl").child("w:tr").child("w:tc").child("w:p");
    ASSERT_TRUE(static_cast<bool>(paragraph));
    const pugi::xml_node hyperlink = paragraph.child("w:hyperlink");
    ASSERT_TRUE(static_cast<bool>(hyperlink));
    EXPECT_EQ(TextOf(hyperlink.child("w:r").child("w:t")), std::string("example.com"));

    pugi::xml_document relationships;
    ASSERT_TRUE(package.parses("word/_rels/document.xml.rels", relationships));
    bool resolves = false;
    for (pugi::xml_node node = relationships.child("Relationships").child("Relationship"); node;
         node = node.next_sibling("Relationship")) {
        if (Attr(node, "Id") == Attr(hyperlink, "r:id") &&
            Attr(node, "Target") == "https://example.com" &&
            Attr(node, "TargetMode") == "External")
            resolves = true;
    }
    EXPECT_TRUE(resolves);

    // A link in a cell is still one paragraph with two runs, one of them inside
    // the hyperlink element.
    EXPECT_EQ(CountDescendants(paragraph, "w:r"), 2u);
}

TEST(OfficeDocxExport, TextXmlCannotCarryIsCleanedUpAndReported) {
    DocModel doc;
    Block paragraph;
    paragraph.kind = BlockKind::Paragraph;
    // A line break and a tab inside a run: they stay inside the same Word run,
    // so the reader reports the model's run split and text back.
    paragraph.runs.push_back(reference::Plain("linje et\nlinje to\tmed tab"));
    // A control character XML cannot carry, and two bytes that are not UTF-8.
    // Written as a byte string so the malformed input is deliberate.
    paragraph.runs.push_back(reference::Plain(std::string("kontrol\x01""tegn \xff\xfe slut")));
    doc.blocks.push_back(paragraph);

    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);
    EXPECT_TRUE(HasWarning(out, "InvalidText"));

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));
    const std::vector<pugi::xml_node> paragraphs = BodyParagraphs(body);
    ASSERT_EQ(paragraphs.size(), 1u);

    // Two model runs, two Word runs.
    const pugi::xml_node first = paragraphs[0].child("w:r");
    ASSERT_TRUE(static_cast<bool>(first));
    EXPECT_EQ(CountDescendants(paragraphs[0], "w:r"), 2u);
    EXPECT_EQ(CountDescendants(first, "w:br"), 1u);
    EXPECT_EQ(CountDescendants(first, "w:tab"), 1u);
    EXPECT_EQ(TextOf(first.child("w:t")), std::string("linje et"));

    // The malformed run kept its text, cleaned up.
    const pugi::xml_node second = first.next_sibling("w:r");
    ASSERT_TRUE(static_cast<bool>(second));
    EXPECT_EQ(TextOf(second.child("w:t")), std::string("kontrol tegn  slut"));

    // The reader accepts the file: a raw control byte would have made the whole
    // document unreadable, which is why the writer cleans it up and says so.
    const std::string path = WriteProduced(out.bytes, "cleaned_up_text.docx");
    ASSERT_TRUE(!path.empty());
    const CommandResult result = RunOracle("dump_docx.py", path, "dump_docx_text.log");
    if (result.ran) {
        std::cout << "  dump_docx.py " << path << "\n" << result.output;
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("kontrol tegn") != std::string::npos);
        EXPECT_TRUE(result.output.find("linje et") != std::string::npos);
    }
}

TEST(OfficeDocxExport, LandscapePageSwapsTheDimensions) {
    DocModel doc;
    doc.page = reference::A4Setup();
    doc.page.landscape = true;
    Block table;
    table.kind = BlockKind::Table;
    std::vector<Cell> row;
    row.push_back(reference::TextCell("Venstre"));
    row.push_back(reference::TextCell("H" "\xc3" "\xb8" "jre"));
    table.rows.push_back(row);
    doc.blocks.push_back(table);

    const Export out = ExportModel(doc);
    ASSERT_TRUE(out.ok);

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));

    // A landscape A4 page is wider than it is tall: the model's width and
    // height trade places, the way Word writes a rotated page.
    const pugi::xml_node page_size = body.child("w:sectPr").child("w:pgSz");
    EXPECT_EQ(Attr(page_size, "w:w"), std::string("16838"));
    EXPECT_EQ(Attr(page_size, "w:h"), std::string("11906"));
    EXPECT_EQ(Attr(page_size, "w:orient"), std::string("landscape"));

    // The table grid follows the wider text width: 16838 - 2 * 1134 twips over
    // two columns.
    const pugi::xml_node grid = body.child("w:tbl").child("w:tblGrid");
    EXPECT_EQ(CountDescendants(grid, "w:gridCol"), 2u);
    EXPECT_EQ(Attr(grid.child("w:gridCol"), "w:w"), std::string("7285"));

    const std::string path = WriteProduced(out.bytes, "landscape.docx");
    ASSERT_TRUE(!path.empty());
    const CommandResult result = RunOracle("dump_docx.py", path, "dump_docx_landscape.log");
    if (result.ran) {
        // python-docx reports w:pgSz as written and does not swap it back, so a
        // landscape section reads as 841.9 x 595.3 pt.
        std::cout << "  dump_docx.py " << path << "\n" << result.output;
        EXPECT_EQ(result.exit_code, 0);
        EXPECT_TRUE(result.output.find("\"page_width_pt\": 841.9") != std::string::npos);
        EXPECT_TRUE(result.output.find("\"page_height_pt\": 595.3") != std::string::npos);
    }
}

TEST(OfficeDocxExport, LargeModelExportsWithoutBlowup) {
    // 2000 blocks with a mix of kinds and Danish text in every run.
    DocModel doc;
    doc.title = "Stor";
    for (int index = 0; index < 2000; ++index) {
        Block block;
        if (index % 20 == 0) {
            block.kind = BlockKind::Table;
            for (int row = 0; row < 4; ++row) {
                std::vector<Cell> cells;
                for (int column = 0; column < 3; ++column) {
                    cells.push_back(reference::TextCell("Celle " + std::to_string(column)));
                }
                block.rows.push_back(cells);
            }
        } else if (index % 7 == 0) {
            block.kind = BlockKind::Heading;
            block.level = 2;
            block.runs.push_back(reference::Plain("Overskrift " + std::to_string(index)));
        } else if (index % 5 == 0) {
            block.kind = BlockKind::ListItem;
            block.ordered = (index % 10 == 0);
            block.level = index % 3;
            block.runs.push_back(reference::Plain("Punkt " + std::to_string(index)));
        } else {
            block.kind = BlockKind::Paragraph;
            block.runs.push_back(reference::Plain("Tekst " + std::to_string(index) + " med "));
            block.runs.push_back(reference::Bold("f" "\xc3" "\xb8" "rste"));
            block.runs.push_back(reference::Plain(" og "));
            block.runs.push_back(reference::Mono("kode"));
            block.runs.push_back(reference::Plain(" og et link "));
            block.runs.push_back(reference::Hyperlink("her", "https://example.com/" +
                                                                std::to_string(index % 50)));
        }
        doc.blocks.push_back(block);
    }

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const Export out = ExportModel(doc);
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const long long milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "  2000 blocks exported in " << milliseconds << " ms, "
              << out.bytes.size() << " bytes\n";

    ASSERT_TRUE(out.ok);
    ASSERT_TRUE(out.bytes.size() > 0);
    // Generous, but it does catch a writer that goes quadratic on block count.
    EXPECT_LT(milliseconds, 10000);

    Package package(out.bytes);
    ASSERT_TRUE(package.open());
    pugi::xml_document document;
    ASSERT_TRUE(package.parses("word/document.xml", document));
    const pugi::xml_node body = BodyOf(document);
    ASSERT_TRUE(static_cast<bool>(body));
    EXPECT_EQ(BodyParagraphs(body).size(), ModelParagraphCount(doc));
    EXPECT_EQ(CountDescendants(body, "w:tbl"), ModelTableCount(doc));
    EXPECT_EQ(CountDescendants(body, "w:r"), ModelRunCount(doc));

    // Equal link targets share one relationship; the ids stay unique.
    std::vector<std::string> targets;
    for (const Block& block : doc.blocks) {
        for (const Run& run : block.runs) {
            if (run.link.empty()) continue;
            if (std::find(targets.begin(), targets.end(), run.link) == targets.end())
                targets.push_back(run.link);
        }
    }
    EXPECT_GT(targets.size(), 0u);

    pugi::xml_document relationships;
    ASSERT_TRUE(package.parses("word/_rels/document.xml.rels", relationships));
    std::vector<std::string> ids;
    std::size_t hyperlink_relationships = 0;
    for (pugi::xml_node node = relationships.child("Relationships").child("Relationship"); node;
         node = node.next_sibling("Relationship")) {
        if (Attr(node, "Type").find("/hyperlink") == std::string::npos) continue;
        ++hyperlink_relationships;
        ids.push_back(Attr(node, "Id"));
    }
    EXPECT_EQ(hyperlink_relationships, targets.size());
    for (std::size_t left = 0; left < ids.size(); ++left) {
        for (std::size_t right = left + 1; right < ids.size(); ++right) {
            EXPECT_TRUE(ids[left] != ids[right]);
        }
    }
}
