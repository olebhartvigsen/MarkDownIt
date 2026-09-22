
// PDF export test.
//
// Renders the reference document model (tests/office_reference_model.h) to PDF
// bytes and checks the produced file two ways:
//
//   * in this binary: the structural facts read back with pdfio (page count,
//     page size, options behaviour) plus the page text rebuilt from the text
//     shows of the content stream.
//   * outside this binary: the independent reader in the oracle,
//
//       python tools/office-oracle/check_pdf.py "$MDI_OFFICE_OUT_DIR/reference_from_model.pdf"
//
//     which extracts the pages with pypdf and compares them against
//     tests/office/golden/reference_pdf.json. That checker is the authority on
//     the text: pdfio writes one show per word, so a test that looks for a
//     phrase in the raw content stream would fail on a correct file, and only a
//     real reader can say what a page says. The checks here mirror what the
//     oracle compares, so a failure points at the same thing.
//
// The test writes the produced files into MDI_OFFICE_OUT_DIR when it is set, so
// the CI verify-export job can open them with pypdf.
//
// Portable C++17: no windows.h, so it runs both in CI (windows-2022) and
// headless on Linux.

#include "gtest_lite.h"

#include "pdfio.h"
#include "pdfio-content.h"

#include "office/office_model.h"
#include "office/pdf_export.h"
#include "office_reference_model.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace office;

namespace {

// Where the produced files go: the CI output directory, otherwise a temporary
// directory. Never the repository checkout.
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

bool CollectPdfMessage(pdfio_file_t* pdf, const char* message, void* data) {
    (void)pdf;
    if (data && message) static_cast<std::string*>(data)->append(message).append("\n");
    return true;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// A PDF file ends with %%EOF, optionally followed by whitespace.
bool EndsWithEof(const std::string& bytes) {
    size_t end = bytes.size();
    while (end > 0 && (bytes[end - 1] == '\n' || bytes[end - 1] == '\r' || bytes[end - 1] == ' '))
        --end;
    return end >= 5 && bytes.compare(end - 5, 5, "%%EOF") == 0;
}

size_t CountChar(const std::string& text, char value) {
    size_t count = 0;
    for (char c : text) if (c == value) ++count;
    return count;
}

// The decoded content stream of a page.
std::string PageContent(pdfio_file_t* pdf, size_t page_index) {
    pdfio_obj_t* page = pdfioFileGetPage(pdf, page_index);
    if (page == nullptr) return std::string();

    pdfio_stream_t* stream = pdfioPageOpenStream(page, 0, true);
    if (stream == nullptr) return std::string();

    std::string text;
    char buffer[512];
    long long got = 0;
    while ((got = static_cast<long long>(pdfioStreamRead(stream, buffer, sizeof(buffer)))) > 0)
        text.append(buffer, static_cast<size_t>(got));
    pdfioStreamClose(stream);
    return text;
}

// Decode one PDF literal string, starting at the '(' in stream[at].
std::string DecodeLiteral(const std::string& stream, size_t& at) {
    std::string out;
    ++at;                       // the opening parenthesis
    int depth = 1;
    while (at < stream.size()) {
        const char c = stream[at++];
        if (c == '\\') {
            if (at >= stream.size()) break;
            const char escape = stream[at++];
            switch (escape) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '(': out += '('; break;
                case ')': out += ')'; break;
                case '\\': out += '\\'; break;
                default:
                    if (escape >= '0' && escape <= '7') {
                        int value = escape - '0';
                        for (int i = 0; i < 2 && at < stream.size() && stream[at] >= '0' && stream[at] <= '7'; ++i)
                            value = value * 8 + (stream[at++] - '0');
                        out += static_cast<char>(value & 0xff);
                    } else {
                        out += escape;
                    }
                    break;
            }
            continue;
        }
        if (c == '(') {
            ++depth;
            out += c;
            continue;
        }
        if (c == ')') {
            --depth;
            if (depth == 0) break;
            out += c;
            continue;
        }
        out += c;
    }
    return out;
}

// The text shows of a page, in content-stream order. pypdf (and therefore the
// oracle) reads the same shows in the same order.
std::vector<std::string> PageShows(const std::string& stream) {
    std::vector<std::string> shows;
    size_t at = 0;
    while (at < stream.size()) {
        if (stream[at] != '(') {
            ++at;
            continue;
        }
        const std::string literal = DecodeLiteral(stream, at);

        size_t next = at;
        while (next < stream.size() && (stream[next] == ' ' || stream[next] == '\n' ||
                                        stream[next] == '\r' || stream[next] == '\t'))
            ++next;
        if (stream.compare(next, 2, "Tj") == 0 && !literal.empty()) shows.push_back(literal);
    }
    return shows;
}

// The page text as a reader sees it: the shows joined with single spaces. The
// readers disagree about whitespace, so only whitespace insensitive checks
// belong here; the oracle compares the same characters without whitespace.
std::string PageText(const std::string& stream) {
    std::string text;
    for (const std::string& show : PageShows(stream)) {
        if (!text.empty()) text += ' ';
        text += show;
    }
    return text;
}

// The content characters of a page: no whitespace, which is how the oracle
// compares pages.
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

// Whitespace insensitive phrase check: the writer emits one show per word, so a
// phrase can be split across shows, but the content characters are stable.
bool ContainsContent(const std::string& text, const std::string& phrase) {
    return Contains(ContentChars(text), ContentChars(phrase));
}

// The distinct text baselines of a page: one per rendered line.
size_t CountTextRows(const std::string& stream) {
    std::vector<std::string> rows;
    size_t at = 0;
    while ((at = stream.find(" Td", at)) != std::string::npos) {
        size_t end = at;
        while (end > 0 && stream[end - 1] == ' ') --end;
        size_t start = end;
        while (start > 0 && (stream[start - 1] == '.' || stream[start - 1] == '-' ||
                             (stream[start - 1] >= '0' && stream[start - 1] <= '9')))
            --start;
        const std::string row = stream.substr(start, end - start);
        bool seen = false;
        for (const std::string& known : rows)
            if (known == row) seen = true;
        if (!seen) rows.push_back(row);
        at = end + 3;
    }
    return rows.size();
}



bool HasWarning(const CompatReport& report, const std::string& feature) {
    for (const CompatWarning& warning : report.warnings)
        if (warning.feature == feature) return true;
    return false;
}

void CheckA4Page(pdfio_file_t* pdf, size_t page_index) {
    pdfio_obj_t* page = pdfioFileGetPage(pdf, page_index);
    ASSERT_TRUE(page != nullptr);

    pdfio_dict_t* dict = pdfioObjGetDict(page);
    ASSERT_TRUE(dict != nullptr);

    pdfio_rect_t box;
    std::memset(&box, 0, sizeof(box));
    ASSERT_TRUE(pdfioDictGetRect(dict, "MediaBox", &box) != nullptr);
    EXPECT_NEAR(box.x1, 0.0, 1.0);
    EXPECT_NEAR(box.y1, 0.0, 1.0);
    EXPECT_NEAR(box.x2, 595.28, 1.0);
    EXPECT_NEAR(box.y2, 841.89, 1.0);
}

}  // namespace

TEST(OfficePdfExport, ReferenceModelProducesTheGoldenPages) {
    const DocModel doc = reference::PdfModel();
    PdfExportOptions options;
    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;

    if (!PdfExport(doc, options, bytes, report, error, &page_count))
        std::cerr << "  pdf export error: " << error << "\n";
    ASSERT_TRUE(!bytes.empty());
    EXPECT_TRUE(bytes.compare(0, 5, "%PDF-") == 0);
    EXPECT_TRUE(EndsWithEof(bytes));
    EXPECT_EQ(page_count, 2);

    const std::string path = OutputPath("reference_from_model.pdf");
    ASSERT_TRUE(WriteFile(path, bytes));

    std::string messages;
    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr, CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);
    ASSERT_EQ(pdfioFileGetNumPages(pdf), 2u);
    CheckA4Page(pdf, 0);
    CheckA4Page(pdf, 1);

    const std::string page_one = PageText(PageContent(pdf, 0));
    const std::string page_two = PageText(PageContent(pdf, 1));

    // The footer of a page is drawn before its body, so a reader extracts
    // "Side N" at the start of the page, exactly like the golden file.
    EXPECT_TRUE(StartsWith(ContentChars(page_one), "Side1"));
    EXPECT_TRUE(StartsWith(ContentChars(page_two), "Side2"));

    // Page 1: title, Danish paragraph and the lists. The explicit page break in
    // the reference model keeps the table on page 2, and page 1 ends with the
    // last numbered item.
    EXPECT_TRUE(ContainsContent(page_one, "MarkDownIt Office Interop Reference"));
    EXPECT_TRUE(ContainsContent(page_one, "Indledning"));
    EXPECT_TRUE(ContainsContent(page_one, "Punktliste F" "\xf8" "rste punkt Andet punkt"));
    EXPECT_TRUE(ContainsContent(page_one, "o Underelement til andet punkt Tredje punkt"));
    EXPECT_TRUE(ContainsContent(page_one, "1. Trin et 2. Trin to 3. Trin tre"));
    EXPECT_TRUE(ContainsContent(page_one, "3. Trin tre"));
    EXPECT_FALSE(ContainsContent(page_one, "Tabel"));

    // The Danish characters are WinAnsi bytes, not mojibake: æ ø å Æ Ø Å are
    // 0xE6 0xF8 0xE5 0xC6 0xD8 0xC5 in that encoding. pypdf reads those bytes
    // back as U+00E6 and friends, which is what the golden compares.
    EXPECT_TRUE(ContainsContent(page_one, "Danske tegn: " "\xe6" " " "\xf8" " " "\xe5" " " "\xc6" " " "\xd8" " " "\xc5" "."));
    EXPECT_TRUE(ContainsContent(page_one, "Understreget: understreget tekst."));

    // The level 0 list marker is a drawn bullet, so no bullet character ends up
    // in the page text (the reference pages have none either).
    EXPECT_FALSE(ContainsContent(page_one, "\x95"));

    // Page 2: the heading, the table row by row, and the paragraph with the
    // link text last.
    EXPECT_TRUE(ContainsContent(page_two, "Tabel Navn Antal Note " "\xc6" "ble 3 Moden " "\xd8" "l 12 Kold " "\xc5" "l 1 Fersk"));
    EXPECT_TRUE(ContainsContent(page_two, "L" "\xe6" "s mere p" "\xe5" " example.com for detaljer."));

    EXPECT_TRUE(pdfioFileClose(pdf));
}

TEST(OfficePdfExport, PageRangeSelectionProducesOnePage) {
    const DocModel doc = reference::PdfModel();
    PdfExportOptions options;
    options.all_pages = false;
    options.first_page = 2;
    options.last_page = 2;

    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;
    ASSERT_TRUE(PdfExport(doc, options, bytes, report, error, &page_count));
    EXPECT_TRUE(bytes.compare(0, 5, "%PDF-") == 0);
    EXPECT_TRUE(EndsWithEof(bytes));
    EXPECT_EQ(page_count, 1);

    const std::string path = OutputPath("reference_page2.pdf");
    ASSERT_TRUE(WriteFile(path, bytes));

    std::string messages;
    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr, CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);
    EXPECT_EQ(pdfioFileGetNumPages(pdf), 1u);
    CheckA4Page(pdf, 0);

    // The exported page keeps the document's own page number in the footer, and
    // carries the content of page 2 only.
    const std::string page = PageText(PageContent(pdf, 0));
    EXPECT_TRUE(StartsWith(ContentChars(page), "Side2"));
    EXPECT_FALSE(ContainsContent(page, "Side 1"));
    EXPECT_TRUE(ContainsContent(page, "Tabel"));
    EXPECT_TRUE(ContainsContent(page, "Fersk"));
    EXPECT_FALSE(ContainsContent(page, "Punktliste"));

    EXPECT_TRUE(pdfioFileClose(pdf));
}

TEST(OfficePdfExport, EmptyModelIsStillAValidOnePagePdf) {
    DocModel doc;   // no blocks, default A4 page setup, no footer
    PdfExportOptions options;
    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;

    ASSERT_TRUE(PdfExport(doc, options, bytes, report, error, &page_count));
    EXPECT_TRUE(bytes.compare(0, 5, "%PDF-") == 0);
    EXPECT_TRUE(EndsWithEof(bytes));
    EXPECT_EQ(page_count, 1);

    const std::string path = OutputPath("empty_model.pdf");
    ASSERT_TRUE(WriteFile(path, bytes));

    std::string messages;
    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr, CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);
    EXPECT_EQ(pdfioFileGetNumPages(pdf), 1u);
    CheckA4Page(pdf, 0);
    EXPECT_TRUE(pdfioFileClose(pdf));
}

TEST(OfficePdfExport, PageRangeOutsideTheDocumentIsAnError) {
    const DocModel doc = reference::PdfModel();
    PdfExportOptions options;
    options.all_pages = false;
    options.first_page = 7;
    options.last_page = 0;   // 0 means "through the last page"

    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;
    EXPECT_FALSE(PdfExport(doc, options, bytes, report, error, &page_count));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(bytes.empty());
}

TEST(OfficePdfExport, LargeDocumentPaginatesAndReportsItsPageCount) {
    DocModel doc;
    doc.page.footer_page_numbers = true;
    for (int i = 0; i < 200; ++i) {
        Block block;
        block.kind = BlockKind::Paragraph;
        block.runs.push_back(reference::Plain("Afsnit nummer " + std::to_string(i + 1) +
                                             " fylder en linje eller to i eksporten."));
        doc.blocks.push_back(block);
    }

    PdfExportOptions options;
    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;
    ASSERT_TRUE(PdfExport(doc, options, bytes, report, error, &page_count));

    const std::string path = OutputPath("large_model.pdf");
    ASSERT_TRUE(WriteFile(path, bytes));

    std::string messages;
    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr, CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);

    const size_t pages = pdfioFileGetNumPages(pdf);
    EXPECT_GT(pages, 5u);
    EXPECT_EQ(page_count, static_cast<int>(pages));

    // Every page carries its own footer number, and the flow continues across
    // the page boundary.
    const std::string second = PageText(PageContent(pdf, 1));
    EXPECT_TRUE(StartsWith(ContentChars(second), "Side2"));
    EXPECT_TRUE(ContainsContent(second, "Afsnit nummer"));

    EXPECT_TRUE(pdfioFileClose(pdf));
}

TEST(OfficePdfExport, LandscapePageSetupSwapsThePageSize) {
    DocModel doc = reference::PdfModel();
    doc.page.landscape = true;

    PdfExportOptions options;
    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;
    ASSERT_TRUE(PdfExport(doc, options, bytes, report, error, &page_count));

    const std::string path = OutputPath("landscape_model.pdf");
    ASSERT_TRUE(WriteFile(path, bytes));

    std::string messages;
    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr, CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);
    EXPECT_EQ(pdfioFileGetNumPages(pdf), 2u);

    pdfio_obj_t* page = pdfioFileGetPage(pdf, 0);
    ASSERT_TRUE(page != nullptr);
    pdfio_dict_t* dict = pdfioObjGetDict(page);
    ASSERT_TRUE(dict != nullptr);

    pdfio_rect_t box;
    std::memset(&box, 0, sizeof(box));
    ASSERT_TRUE(pdfioDictGetRect(dict, "MediaBox", &box) != nullptr);
    EXPECT_NEAR(box.x2, 841.89, 1.0);   // landscape A4: the page sides swap
    EXPECT_NEAR(box.y2, 595.28, 1.0);

    EXPECT_TRUE(pdfioFileClose(pdf));
}

TEST(OfficePdfExport, WrapsLongWordsAndReportsWhatItDrops) {
    DocModel doc;
    doc.page.footer_page_numbers = true;
    {
        Block block;
        block.kind = BlockKind::Image;
        doc.blocks.push_back(block);
    }
    {
        Block block;
        block.kind = BlockKind::Paragraph;
        block.runs.push_back(reference::Hyperlink("example.com", "https://example.com"));
        doc.blocks.push_back(block);
    }
    {
        Block block;
        block.kind = BlockKind::Paragraph;
        // A character outside WinAnsi (U+4E2D) is dropped and reported instead
        // of being written as a silent '?'.
        block.runs.push_back(reference::Plain("A" "\xe4" "\xb8" "\xad" "B"));
        doc.blocks.push_back(block);
    }
    {
        Block block;
        block.kind = BlockKind::Paragraph;
        std::string word;
        for (int i = 0; i < 240; ++i) word += "z";
        block.runs.push_back(reference::Plain(word));
        doc.blocks.push_back(block);
    }

    PdfExportOptions options;
    CompatReport report;
    std::string error;
    std::string bytes;
    int page_count = 0;
    ASSERT_TRUE(PdfExport(doc, options, bytes, report, error, &page_count));
    EXPECT_TRUE(HasWarning(report, "image"));
    EXPECT_TRUE(HasWarning(report, "link"));
    EXPECT_TRUE(HasWarning(report, "character"));

    const std::string path = OutputPath("wrapped_words.pdf");
    ASSERT_TRUE(WriteFile(path, bytes));

    std::string messages;
    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr, CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);
    EXPECT_EQ(pdfioFileGetNumPages(pdf), 1u);

    const std::string stream = PageContent(pdf, 0);
    const std::string page = PageText(stream);

    // The unbreakable word is split over several display lines instead of
    // running off the page, and every character of it survives ("z" appears
    // nowhere else in this document).
    EXPECT_GT(CountTextRows(stream), 4u);
    EXPECT_EQ(CountChar(page, 'z'), 240u);

    // The dropped character left no trace, and the text around it survived.
    EXPECT_TRUE(ContainsContent(page, "AB"));
    EXPECT_FALSE(ContainsContent(page, "\xe4"));

    EXPECT_TRUE(pdfioFileClose(pdf));
}

TEST(OfficePdfExport, BaseFontWidthTablesDriveTheWrapping) {
    const std::string path = OutputPath("width_probe.pdf");
    std::remove(path.c_str());

    std::string messages;
    pdfio_rect_t media_box = {0.0, 0.0, 595.28, 841.89};
    pdfio_file_t* pdf = pdfioFileCreate(path.c_str(), "1.7", &media_box, nullptr,
                                        CollectPdfMessage, &messages);
    ASSERT_TRUE(pdf != nullptr);

    pdfio_obj_t* font = pdfioFileCreateFontObjFromBase(pdf, "Helvetica");
    ASSERT_TRUE(font != nullptr);
    pdfio_stream_t* stream = pdfioFileCreatePage(pdf, nullptr);
    ASSERT_TRUE(stream != nullptr);
    EXPECT_TRUE(pdfioStreamClose(stream));

    const double wide = pdfioContentTextMeasure(font, "MMMM", 11.0);
    const double narrow = pdfioContentTextMeasure(font, "i", 11.0);
    EXPECT_GT(wide, narrow);
    EXPECT_GT(narrow, 0.0);
    EXPECT_GT(pdfioContentTextMeasure(font, " ", 11.0), 0.0);

    EXPECT_TRUE(pdfioFileClose(pdf));
    std::remove(path.c_str());
}
