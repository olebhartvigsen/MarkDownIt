// PDF import tests (office spec, PDF import). The importer reads what
// the exporter writes, so the reference model round trips through a
// real PDF; the hand-built fixture covers what the writer does not
// produce itself, namely link annotations, an image page and OCR.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "gtest_lite.h"
#include "office/office_model.h"
#include "office/pdf_export.h"
#include "office/pdf_import.h"
#include "office_reference_model.h"
#include "pdfio.h"

using namespace office;

namespace {

// Where the produced files go: the CI output directory, otherwise a
// temporary directory. Never the repository checkout.
std::string OutputDir() {
    const char* value = std::getenv("MDI_OFFICE_OUT_DIR");
    if (value && *value) return std::string(value);
#if defined(_WIN32)
    const char* names[] = {"TEMP", "TMP"};
#else
    const char* names[] = {"TMPDIR", "TMP", "TEMP"};
#endif
    for (const char* name : names) {
        const char* candidate = std::getenv(name);
        if (candidate && *candidate) return std::string(candidate);
    }
    return std::string(".");
}

std::string OutputPath(const char* name) { return OutputDir() + "/" + name; }

bool WriteFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}

// All the model text, whitespace removed, so word joins and line breaks
// cannot hide a match.
std::string ContentChars(const std::string& text) {
    std::string out;
    for (char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f' ||
            byte == '\v')
            continue;
        out += c;
    }
    return out;
}

std::string ModelText(const DocModel& doc) {
    std::string text;
    for (const Block& block : doc.blocks)
        for (const Run& run : block.runs) text += run.text;
    return text;
}

bool HasWarning(const CompatReport& report, const std::string& feature) {
    for (const CompatWarning& warning : report.warnings)
        if (warning.feature == feature) return true;
    return false;
}

bool HasBlockKind(const DocModel& doc, BlockKind kind) {
    for (const Block& block : doc.blocks)
        if (block.kind == kind) return true;
    return false;
}

// The first run that carries a link, empty when none does.
std::string FirstLink(const DocModel& doc) {
    for (const Block& block : doc.blocks)
        for (const Run& run : block.runs)
            if (!run.link.empty()) return run.link;
    return std::string();
}

// A log for the fake OCR so the test can see which pages it was asked
// about.
struct OcrLog {
    int calls = 0;
    size_t last_page = 99;
};

std::string FakeOcr(void* data, const std::string& path, size_t page_index) {
    (void)path;
    OcrLog* log = static_cast<OcrLog*>(data);
    if (log != nullptr) {
        ++log->calls;
        log->last_page = page_index;
    }
    if (page_index == 1) return "OCR linje et\nOCR linje to";
    return std::string();
}

// A hand-built two-page PDF the writer never produces: page one holds
// one word with a link annotation, page two is an image-only page that
// needs OCR. The xref offsets are recorded while the objects are
// appended, so the file is well-formed by construction.
std::string BuildFixture() {
    const std::string content_one = "BT /F1 14 Tf 72 720 Td (Klik her) Tj ET\n";
    const std::string content_two = "q 100 0 0 100 200 500 cm /Im1 Do Q\n";
    const std::string image_data("\x00\x10\x20\x30", 4);

    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    objects.push_back(
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.28 841.89] /Resources "
        "<< /Font << /F1 5 0 R >> >> /Contents 6 0 R /Annots [9 0 R] >>");
    objects.push_back(
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.28 841.89] /Resources "
        "<< /XObject << /Im1 7 0 R >> >> /Contents 8 0 R >>");
    objects.push_back(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objects.push_back("<< /Length " + std::to_string(content_one.size()) + " >>\nstream\n" +
                      content_one + "endstream");
    objects.push_back(
        "<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8 /Length " +
        std::to_string(image_data.size()) + " >>\nstream\n" + image_data + "\nendstream");
    objects.push_back("<< /Length " + std::to_string(content_two.size()) + " >>\nstream\n" +
                      content_two + "endstream");
    objects.push_back("<< /Type /Annot /Subtype /Link /Rect [70 716 160 736] /Border [0 0 0] "
                      "/A << /S /URI /URI (http://example.com/x) >> >>");

    std::string pdf = "%PDF-1.6\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t start = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t offset : offsets) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010llu 00000 n \n",
                      static_cast<unsigned long long>(offset));
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\n";
    pdf += "startxref\n" + std::to_string(start) + "\n%%EOF\n";
    return pdf;
}

// Exports the reference model and writes the file where the import can
// read it back.
std::string WriteReferencePdf(const char* name, bool protect, std::string& error) {
    const DocModel doc = reference::PdfModel();
    PdfExportOptions options;
    if (protect) {
        options.encrypt = true;
        options.owner_password = "owner-secret";
        options.user_password = "user-secret";
    }
    CompatReport report;
    std::string bytes;
    int page_count = 0;
    if (!PdfExport(doc, options, bytes, report, error, &page_count)) return std::string();
    const std::string path = OutputPath(name);
    if (!WriteFile(path, bytes)) return std::string();
    return path;
}

}  // namespace

TEST(OfficePdfImport, ReadsBackWhatTheWriterWrote) {
    std::string error;
    const std::string path = WriteReferencePdf("import_reference.pdf", false, error);
    if (path.empty()) std::cerr << "  export error: " << error << "\n";
    ASSERT_TRUE(!path.empty());

    DocModel doc;
    CompatReport report;
    ASSERT_TRUE(PdfImport(path, PdfImportOptions(), doc, report, error));

    // Page setup travels: the reference model is A4 portrait.
    EXPECT_NEAR(doc.page.width_pt, 595.28, 1.0);
    EXPECT_NEAR(doc.page.height_pt, 841.89, 1.0);
    EXPECT_FALSE(doc.page.landscape);

    // A page break sits between the two source pages, and content from
    // both sides is present.
    EXPECT_TRUE(HasBlockKind(doc, PageBreak));
    const std::string text = ContentChars(ModelText(doc));
    EXPECT_TRUE(text.find("MarkDownItOfficeInteropReference") != std::string::npos);
    EXPECT_TRUE(text.find("Indledning") != std::string::npos);
    // The Danish letters survive the CP1252 mapping as real UTF-8.
    EXPECT_TRUE(text.find("F\xC3\xB8rste") != std::string::npos);
    EXPECT_TRUE(HasBlockKind(doc, Heading) || HasBlockKind(doc, Title));
    EXPECT_TRUE(HasBlockKind(doc, ListItem) || HasBlockKind(doc, Paragraph));

    // What the model cannot hold is named in the report.
    EXPECT_TRUE(HasWarning(report, "font"));
}

TEST(OfficePdfImport, PageOneComesBeforeTheBreakAndPageTwoAfterIt) {
    std::string error;
    const std::string path = WriteReferencePdf("import_reference_order.pdf", false, error);
    ASSERT_TRUE(!path.empty());

    DocModel doc;
    CompatReport report;
    ASSERT_TRUE(PdfImport(path, PdfImportOptions(), doc, report, error));

    size_t break_index = doc.blocks.size();
    for (size_t i = 0; i < doc.blocks.size(); ++i)
        if (doc.blocks[i].kind == PageBreak) break_index = i;
    ASSERT_TRUE(break_index < doc.blocks.size());

    std::string before;
    std::string after;
    for (size_t i = 0; i < doc.blocks.size(); ++i) {
        for (const Run& run : doc.blocks[i].runs) {
            if (i < break_index)
                before += run.text;
            else if (i > break_index)
                after += run.text;
        }
    }
    EXPECT_TRUE(ContentChars(before).find("Indledning") != std::string::npos);
    EXPECT_FALSE(ContentChars(after).find("Indledning") != std::string::npos);
}

TEST(OfficePdfImport, AProtectedPdfAsksForItsPassword) {
    std::string error;
    const std::string path = WriteReferencePdf("import_protected.pdf", true, error);
    ASSERT_TRUE(!path.empty());

    DocModel refused;
    CompatReport refused_report;
    EXPECT_FALSE(PdfImport(path, PdfImportOptions(), refused, refused_report, error));
    EXPECT_TRUE(error.find("password") != std::string::npos);
    EXPECT_TRUE(refused.blocks.empty());

    DocModel doc;
    CompatReport report;
    PdfImportOptions options;
    options.password = "user-secret";
    ASSERT_TRUE(PdfImport(path, options, doc, report, error));
    EXPECT_TRUE(ContentChars(ModelText(doc)).find("Indledning") != std::string::npos);
}

TEST(OfficePdfImport, LinksAndAnImagePageAndOcr) {
    const std::string path = OutputPath("import_fixture.pdf");
    ASSERT_TRUE(WriteFile(path, BuildFixture()));

    DocModel doc;
    CompatReport report;
    PdfImportOptions options;
    OcrLog log;
    options.ocr = FakeOcr;
    options.ocr_data = &log;
    std::string error;
    ASSERT_TRUE(PdfImport(path, options, doc, report, error));

    // The link annotation became a link run over the covered word.
    EXPECT_EQ(FirstLink(doc), std::string("http://example.com/x"));
    EXPECT_TRUE(ContentChars(ModelText(doc)).find("Klikher") != std::string::npos);

    // The image-only page went through the OCR seam, once, for page 2.
    EXPECT_EQ(log.calls, 1);
    EXPECT_EQ(log.last_page, static_cast<size_t>(1));
    const std::string text = ContentChars(ModelText(doc));
    EXPECT_TRUE(text.find("OCRlinjeet") != std::string::npos);
    EXPECT_TRUE(text.find("OCRlinjeto") != std::string::npos);

    // The report names the OCR pass, the image and the fonts.
    EXPECT_TRUE(HasWarning(report, "ocr"));
    EXPECT_TRUE(HasWarning(report, "image"));
    EXPECT_TRUE(HasWarning(report, "font"));
    EXPECT_FALSE(HasWarning(report, "scanned"));
}

TEST(OfficePdfImport, WithoutOcrAScannedPageIsReported) {
    const std::string path = OutputPath("import_fixture_no_ocr.pdf");
    ASSERT_TRUE(WriteFile(path, BuildFixture()));

    DocModel doc;
    CompatReport report;
    std::string error;
    // The first page holds text, so the import itself succeeds, and the
    // scanned second page is named in the report instead.
    ASSERT_TRUE(PdfImport(path, PdfImportOptions(), doc, report, error));
    EXPECT_TRUE(HasWarning(report, "scanned"));
    EXPECT_TRUE(ContentChars(ModelText(doc)).find("Klikher") != std::string::npos);
    EXPECT_FALSE(ContentChars(ModelText(doc)).find("OCRlinje") != std::string::npos);
}
