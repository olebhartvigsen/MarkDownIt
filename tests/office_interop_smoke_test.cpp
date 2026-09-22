// Office interop smoke test.
//
// Proves the vendored miniz, pugixml and pdfio libraries build and work in
// this project, that the shared office document model compiles, and that the
// docx/pdf stub modules link into the test binary.
//
// Portable C++17: no windows.h, so it runs both in CI (windows-2022) and
// headless on Linux.

#include "gtest_lite.h"

#include "miniz.h"
#include "pugixml.hpp"
#include "pdfio.h"
#include "pdfio-content.h"

#include "office/docx_export.h"
#include "office/docx_import.h"
#include "office/office_model.h"
#include "office/pdf_export.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace office;

namespace {

const char kZipEntryName[] = "office/hello.txt";
const char kZipPayload[] = "MarkDownIt office interop payload.\n";

// Temporary directory for the PDF file. Never the repository checkout.
std::string TempDir() {
#if defined(_WIN32)
    const char* names[] = {"TEMP", "TMP"};
#else
    const char* names[] = {"TMPDIR", "TMP", "TEMP"};
#endif
    for (const char* name : names) {
        const char* value = std::getenv(name);
        if (value && *value) return std::string(value);
    }
#if defined(_WIN32)
    return std::string(".");
#else
    return std::string("/tmp");
#endif
}

// pdfio error callback: collect messages so a failure can be reported by the
// test instead of being written straight to stderr.
bool CollectPdfMessage(pdfio_file_t* pdf, const char* message, void* data) {
    (void)pdf;
    if (data && message)
        static_cast<std::string*>(data)->append(message).append("\n");
    return true;
}

}  // namespace

TEST(OfficeInterop, MinizZipRoundTrip) {
    const size_t payload_len = sizeof(kZipPayload) - 1;

    mz_zip_archive writer;
    std::memset(&writer, 0, sizeof(writer));
    ASSERT_TRUE(mz_zip_writer_init_heap(&writer, 0, 0) == MZ_TRUE);
    ASSERT_TRUE(mz_zip_writer_add_mem(&writer, kZipEntryName, kZipPayload,
                                      payload_len, MZ_DEFAULT_LEVEL) == MZ_TRUE);

    void* archive = nullptr;
    size_t archive_size = 0;
    ASSERT_TRUE(mz_zip_writer_finalize_heap_archive(&writer, &archive, &archive_size) == MZ_TRUE);
    ASSERT_TRUE(archive != nullptr);
    EXPECT_GT(archive_size, 0u);
    EXPECT_TRUE(mz_zip_writer_end(&writer) == MZ_TRUE);

    // Read the archive back out of the finished bytes.
    mz_zip_archive reader;
    std::memset(&reader, 0, sizeof(reader));
    ASSERT_TRUE(mz_zip_reader_init_mem(&reader, archive, archive_size, 0) == MZ_TRUE);
    EXPECT_EQ(mz_zip_reader_get_num_files(&reader), 1u);

    mz_zip_archive_file_stat stat;
    std::memset(&stat, 0, sizeof(stat));
    ASSERT_TRUE(mz_zip_reader_file_stat(&reader, 0, &stat) == MZ_TRUE);
    EXPECT_EQ(std::string(stat.m_filename), std::string(kZipEntryName));
    EXPECT_EQ(static_cast<size_t>(stat.m_uncomp_size), payload_len);

    std::vector<char> extracted(sizeof(kZipPayload), 0);
    ASSERT_TRUE(mz_zip_reader_extract_to_mem(&reader, 0, extracted.data(),
                                             extracted.size(), 0) == MZ_TRUE);
    EXPECT_EQ(std::memcmp(extracted.data(), kZipPayload, payload_len), 0);

    EXPECT_TRUE(mz_zip_reader_end(&reader) == MZ_TRUE);
    mz_free(archive);
}

TEST(OfficeInterop, PugixmlParseAndRoundTrip) {
    const char* xml =
        "<doc title=\"spec\">"
        "<section><item bold=\"true\">First</item><item>Second</item></section>"
        "</doc>";

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml);
    ASSERT_TRUE(static_cast<bool>(result));

    pugi::xml_node root = doc.child("doc");
    ASSERT_TRUE(static_cast<bool>(root));
    EXPECT_EQ(std::string(root.attribute("title").value()), std::string("spec"));

    pugi::xml_node section = root.child("section");
    ASSERT_TRUE(static_cast<bool>(section));
    pugi::xml_node first = section.child("item");
    ASSERT_TRUE(static_cast<bool>(first));
    EXPECT_EQ(std::string(first.attribute("bold").value()), std::string("true"));
    EXPECT_EQ(std::string(first.text().get()), std::string("First"));
    EXPECT_EQ(std::string(section.child("item").next_sibling("item").text().get()),
              std::string("Second"));

    // Build a document programmatically, serialize it, parse it again.
    pugi::xml_document built;
    pugi::xml_node declaration = built.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    pugi::xml_node built_root = built.append_child("doc");
    built_root.append_attribute("title") = "built";
    pugi::xml_node paragraph = built_root.append_child("paragraph");
    paragraph.append_attribute("bold") = "false";
    paragraph.append_child(pugi::node_pcdata).set_value("Hello office");

    std::ostringstream stream;
    built.save(stream, "  ");
    const std::string serialized = stream.str();
    EXPECT_GT(serialized.size(), 0u);
    EXPECT_TRUE(serialized.find("Hello office") != std::string::npos);

    pugi::xml_document round_trip;
    ASSERT_TRUE(static_cast<bool>(round_trip.load_string(serialized.c_str())));
    pugi::xml_node round_root = round_trip.child("doc");
    ASSERT_TRUE(static_cast<bool>(round_root));
    EXPECT_EQ(std::string(round_root.attribute("title").value()), std::string("built"));
    pugi::xml_node round_para = round_root.child("paragraph");
    ASSERT_TRUE(static_cast<bool>(round_para));
    EXPECT_EQ(std::string(round_para.attribute("bold").value()), std::string("false"));
    EXPECT_EQ(std::string(round_para.text().get()), std::string("Hello office"));
}

TEST(OfficeInterop, PdfioWritesAndReadsA4Page) {
    const std::string path = TempDir() + "/markdownit_pdfio_smoke.pdf";
    std::remove(path.c_str());

    // A4 page from the shared model defaults.
    const PageSetup page_setup;
    pdfio_rect_t media_box = {0.0, 0.0, page_setup.width_pt, page_setup.height_pt};

    std::string pdf_messages;
    pdfio_file_t* pdf = pdfioFileCreate(path.c_str(), "1.7", &media_box, nullptr,
                                        CollectPdfMessage, &pdf_messages);
    ASSERT_TRUE(pdf != nullptr);

    // A base-14 font so the page content stream has a real resource.
    pdfio_dict_t* font_dict = pdfioDictCreate(pdf);
    ASSERT_TRUE(font_dict != nullptr);
    EXPECT_TRUE(pdfioDictSetName(font_dict, "Type", "Font"));
    EXPECT_TRUE(pdfioDictSetName(font_dict, "Subtype", "Type1"));
    EXPECT_TRUE(pdfioDictSetName(font_dict, "BaseFont", "Helvetica"));
    pdfio_obj_t* font = pdfioFileCreateObj(pdf, font_dict);
    ASSERT_TRUE(font != nullptr);
    EXPECT_TRUE(pdfioObjClose(font));

    pdfio_dict_t* page_dict = pdfioDictCreate(pdf);
    ASSERT_TRUE(page_dict != nullptr);
    EXPECT_TRUE(pdfioPageDictAddFont(page_dict, "F1", font));

    pdfio_stream_t* contents = pdfioFileCreatePage(pdf, page_dict);
    ASSERT_TRUE(contents != nullptr);
    EXPECT_TRUE(pdfioStreamPuts(contents,
                                "BT\n/F1 24 Tf\n72 720 Td\n(MarkDownIt office interop) Tj\nET\n"));
    EXPECT_TRUE(pdfioStreamClose(contents));
    EXPECT_TRUE(pdfioFileClose(pdf));

    // Reopen the file from disk and check the page tree and the media box.
    pdfio_file_t* reopened = pdfioFileOpen(path.c_str(), nullptr, nullptr,
                                           CollectPdfMessage, &pdf_messages);
    ASSERT_TRUE(reopened != nullptr);
    EXPECT_EQ(pdfioFileGetNumPages(reopened), 1u);

    // pdfio 1.6.5 indexes pages from 0: pdfioFileGetPage() bounds-checks n
    // against num_pages and returns pdf->pages[n] (pdfio-file.c).
    EXPECT_TRUE(pdfioFileGetPage(reopened, 1) == nullptr);
    pdfio_obj_t* page = pdfioFileGetPage(reopened, 0);
    ASSERT_TRUE(page != nullptr);
    pdfio_dict_t* dict = pdfioObjGetDict(page);
    ASSERT_TRUE(dict != nullptr);

    pdfio_rect_t box;
    std::memset(&box, 0, sizeof(box));
    ASSERT_TRUE(pdfioDictGetRect(dict, "MediaBox", &box) != nullptr);
    EXPECT_NEAR(box.x1, 0.0, 1.0);
    EXPECT_NEAR(box.y1, 0.0, 1.0);
    EXPECT_NEAR(box.x2, page_setup.width_pt, 1.0);
    EXPECT_NEAR(box.y2, page_setup.height_pt, 1.0);
    // A4 is 595.28 x 841.89 points, so the box above is A4 within a point.
    EXPECT_NEAR(box.x2, 595.28, 1.0);
    EXPECT_NEAR(box.y2, 841.89, 1.0);

    // The content stream was written with FlateDecode, so decoding it back
    // exercises the miniz zlib shim that pdfio compiles against.
    pdfio_stream_t* stream = pdfioPageOpenStream(page, 0, true);
    ASSERT_TRUE(stream != nullptr);
    std::string text;
    char buffer[256];
    long long got = 0;
    while ((got = static_cast<long long>(pdfioStreamRead(stream, buffer, sizeof(buffer)))) > 0)
        text.append(buffer, static_cast<size_t>(got));
    EXPECT_TRUE(pdfioStreamClose(stream));
    EXPECT_TRUE(text.find("MarkDownIt office interop") != std::string::npos);

    EXPECT_TRUE(pdfioFileClose(reopened));
    std::remove(path.c_str());
}

TEST(OfficeInterop, SharedModelDefaults) {
    DocModel doc;
    EXPECT_TRUE(doc.blocks.empty());
    EXPECT_NEAR(doc.page.width_pt, 595.28, 0.01);
    EXPECT_NEAR(doc.page.height_pt, 841.89, 0.01);
    EXPECT_NEAR(doc.page.margin_pt, 56.7, 0.01);
    EXPECT_FALSE(doc.page.landscape);

    Block heading;
    heading.kind = Heading;
    heading.level = 2;
    Run run;
    run.text = "Titel";
    run.bold = true;
    heading.runs.push_back(run);
    doc.blocks.push_back(heading);

    ASSERT_EQ(doc.blocks.size(), 1u);
    EXPECT_EQ(doc.blocks[0].kind, BlockKind::Heading);
    EXPECT_EQ(doc.blocks[0].level, 2);
    ASSERT_EQ(doc.blocks[0].runs.size(), 1u);
    EXPECT_EQ(doc.blocks[0].runs[0].text, std::string("Titel"));
    EXPECT_TRUE(doc.blocks[0].runs[0].bold);
    EXPECT_FALSE(doc.blocks[0].runs[0].italic);

    Block table;
    table.kind = Table;
    table.rows.resize(2);
    table.rows[0].resize(2);
    table.rows[0][0].runs.push_back(run);
    EXPECT_EQ(table.rows[0][0].runs.size(), 1u);
    EXPECT_TRUE(table.rows[0][1].runs.empty());
}

TEST(OfficeInterop, StubsReportNotImplemented) {
    DocModel doc;
    CompatReport report;
    std::string error;

    // The importer is implemented: a buffer that is not a .docx package is
    // rejected with an error instead of reporting a missing feature.
    EXPECT_FALSE(DocxImport(std::string("PK"), doc, report, error));
    EXPECT_FALSE(error.empty());

    // The writer is implemented: even an empty model produces a package.
    std::string docx_bytes;
    EXPECT_TRUE(DocxExport(doc, docx_bytes, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(docx_bytes.empty());

    // The PDF writer is implemented: even an empty model produces a valid,
    // one page PDF.
    PdfExportOptions options;
    std::string pdf_bytes;
    EXPECT_TRUE(PdfExport(doc, options, pdf_bytes, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(pdf_bytes.empty());
    EXPECT_EQ(pdf_bytes.compare(0, 5, "%PDF-"), 0);
    EXPECT_TRUE(options.all_pages);
    EXPECT_EQ(options.first_page, 1);
    EXPECT_EQ(options.last_page, 0);
}
