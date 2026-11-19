// The print PDF writer suite (src/print_pdf.cpp): the pages File > Print
// sends to paper, written as bytes. The geometry and the cut are already
// held by tests/print_pages_test.cpp; this suite holds the writer itself:
// one page per band read back through pdfio, stable bytes once the file's
// own timestamp is taken out, the fonts' WinAnsi limits made visible, and
// the table words the page list carries.

#include "fake_text_engine.h"
#include "gtest_lite.h"
#include "layout/document_layout.h"
#include "layout/print_pages.h"
#include "parser.h"
#include "pdfio.h"
#include "print_pdf.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const LayoutMetrics kM = BaseMetrics();
const Palette kPal = BasePalette();
const float kW = layout::PrintPage().ContentWidth();

layout::DocumentLayout LayOut(const char* markdown) {
    Document doc;
    ParseMarkdown(markdown, doc);
    fake::FakeTextEngine engine;
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &kM;
    in.palette = &kPal;
    in.contentWidth = kW;
    in.zoom = 1.0f;
    in.editing = false;
    in.engine = &engine;
    return layout::LayoutDocument(in);
}

std::string ThirtyParagraphs() {
    std::string text;
    for (int i = 0; i < 30; ++i) {
        text +=
            "Paragraph " + std::to_string(i) +
            " fills the sheet with ordinary words, so the cut has work.\n\n";
    }
    return text;
}

std::string TempPath() {
    const char* dir = getenv("TMPDIR");
    return std::string(dir != nullptr && *dir != '\0' ? dir : ".") +
           "/markdownit_print_pdf_test.pdf";
}

// Reads a written PDF back through pdfio itself, one decompressed content
// stream per page, so the assertions hold what a reader will see rather
// than the compressed bytes on disk.
std::vector<std::string> ReadPages(const std::string& bytes,
                                   const std::string& path) {
    std::vector<std::string> pages;
    FILE* file = fopen(path.c_str(), "wb");
    if (file == nullptr) return pages;
    fwrite(bytes.data(), 1, bytes.size(), file);
    fclose(file);

    pdfio_file_t* pdf = pdfioFileOpen(path.c_str(), nullptr, nullptr,
                                      nullptr, nullptr);
    if (pdf == nullptr) return pages;
    const size_t count = pdfioFileGetNumPages(pdf);
    for (size_t i = 0; i < count; ++i) {
        pdfio_obj_t* page = pdfioFileGetPage(pdf, i);
        if (page == nullptr) continue;
        std::string content;
        const size_t streams = pdfioPageGetNumStreams(page);
        for (size_t s = 0; s < streams; ++s) {
            pdfio_stream_t* stream = pdfioPageOpenStream(page, s, true);
            if (stream == nullptr) continue;
            char buffer[4096];
            ssize_t got = 0;
            while ((got = pdfioStreamRead(stream, buffer,
                                          sizeof(buffer))) > 0) {
                content.append(buffer, static_cast<size_t>(got));
            }
            pdfioStreamClose(stream);
        }
        pages.push_back(content);
    }
    pdfioFileClose(pdf);
    remove(path.c_str());
    return pages;
}

// Takes the file's own metadata out, which is the only thing that changes
// between two writes of the same pages: the creation stamps and the
// trailer's file ID, both of which pdfio derives from the clock.
std::string StripDates(std::string text) {
    const char* const keys[2] = {"/CreationDate", "/ModDate"};
    for (const char* key : keys) {
        const std::string needle(key);
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            const size_t open = text.find('(', pos + needle.size());
            const size_t close = text.find(')', open == std::string::npos
                                                       ? pos
                                                       : open);
            if (open == std::string::npos || close == std::string::npos ||
                open > pos + needle.size() + 4) {
                ++pos;
                continue;
            }
            text.erase(pos, close - pos + 1);
        }
    }
    return text;
}

// The trailer's /ID, two hex strings in angle brackets.
std::string StripFileId(std::string text) {
    size_t pos = 0;
    while ((pos = text.find('<', pos)) != std::string::npos) {
        const size_t close = text.find('>', pos);
        if (close == std::string::npos) break;
        if (close - pos != 33) {
            // Not a 32-digit hex string, maybe one of the dict's own '<<'.
            // Advance one character, so the second bracket is still seen.
            ++pos;
            continue;
        }
        bool hex = true;
        for (size_t i = pos + 1; i < close; ++i) {
            const char c = text[i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                hex = false;
                break;
            }
        }
        if (hex) {
            text.erase(pos, close - pos + 1);
        } else {
            pos = close + 1;
        }
    }
    return text;
}

std::string StripMetadata(std::string text) {
    return StripFileId(StripDates(std::move(text)));
}

}  // namespace

TEST(PrintPdf, OnePagePerBandReadableByPdfio) {
    const layout::DocumentLayout laid = LayOut(ThirtyParagraphs().c_str());
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, layout::PrintPage());
    EXPECT_TRUE(bands.size() > 1);
    if (bands.size() < 2) return;

    // The path is held in a string for as long as ReadPages uses it: a
    // temporary's c_str() would dangle, and the allocator on the mac
    // poisoned it before the file could even be opened.
    const std::string path = TempPath();
    print_pdf::WriteReport report;
    const std::string bytes =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &report);
    EXPECT_TRUE(!bytes.empty());
    EXPECT_EQ(bytes.compare(0, 4, "%PDF"), 0);
    EXPECT_EQ(report.pages, static_cast<int>(bands.size()));

    const std::vector<std::string> pages = ReadPages(bytes, path);
    EXPECT_EQ(pages.size(), bands.size());
}

TEST(PrintPdf, TheBytesAreStable) {
    const std::string source =
        "# Overskrift\n\n"
        "R\xC3\xB8" "dgr\xC3\xB8" "d med fl\xC3\xB8" "de.\n\n"
        "A fenced block:\n\n```\nint x = 42;\n```\n";
    const layout::DocumentLayout laid = LayOut(source.c_str());
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, layout::PrintPage());

    print_pdf::WriteReport one;
    print_pdf::WriteReport two;
    const std::string first =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &one);
    const std::string second =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &two);
    EXPECT_TRUE(!first.empty());
    EXPECT_EQ(one.pages, two.pages);
    EXPECT_EQ(one.droppedGlyphs, two.droppedGlyphs);
    // Everything but the file's own stamps and file ID comes out identical.
    EXPECT_EQ(StripMetadata(first), StripMetadata(second));
}

TEST(PrintPdf, DanishTextSurvivesTheFontEncoding) {
    // The oe with a slash is inside WinAnsi, so nothing is dropped, and the
    // byte it becomes is followed by the letters, which read back in the
    // decompressed page content.
    const std::string source = "R\xC3\xB8" "dgr\xC3\xB8" "d med fl\xC3\xB8" "de\n";
    const layout::DocumentLayout laid = LayOut(source.c_str());
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, layout::PrintPage());
    // The path is held in a string for as long as ReadPages uses it: a
    // temporary's c_str() would dangle, and the allocator on the mac
    // poisoned it before the file could even be opened.
    const std::string path = TempPath();
    print_pdf::WriteReport report;
    const std::string bytes =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &report);
    EXPECT_TRUE(!bytes.empty());
    EXPECT_EQ(report.droppedGlyphs, size_t(0));

    const std::vector<std::string> pages = ReadPages(bytes, path);
    EXPECT_TRUE(!pages.empty());
    if (pages.empty()) return;
    EXPECT_TRUE(pages[0].find(std::string("\xF8") + "dgr") !=
                std::string::npos);
}

TEST(PrintPdf, UnwritableGlyphsAreCountedNotHidden) {
    // The crab emoji is outside WinAnsi, so the writer drops it and says so
    // instead of writing a silent '?'.
    const layout::DocumentLayout laid =
        LayOut("Krabbe \xF0\x9F\xA6\x80 i teksten\n");
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, layout::PrintPage());
    print_pdf::WriteReport report;
    const std::string bytes =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &report);
    EXPECT_TRUE(!bytes.empty());
    EXPECT_TRUE(report.droppedGlyphs >= size_t(1));
}

TEST(PrintPdf, ATablesWordsAreOnThePage) {
    // The display list carries a table's washes but not its words; the page
    // list brings the record's copy, so the saved page has the text.
    const layout::DocumentLayout laid = LayOut(
        "Intro line.\n\n"
        "| Column one | Column two |\n"
        "|---|---|\n"
        "| A cell with words | Another cell |\n"
        "| Second row | More words |\n");
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, layout::PrintPage());
    // The path is held in a string for as long as ReadPages uses it: a
    // temporary's c_str() would dangle, and the allocator on the mac
    // poisoned it before the file could even be opened.
    const std::string path = TempPath();
    print_pdf::WriteReport report;
    const std::string bytes =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &report);
    EXPECT_TRUE(!bytes.empty());

    const std::vector<std::string> pages = ReadPages(bytes, path);
    EXPECT_TRUE(!pages.empty());
    if (pages.empty()) return;
    const std::string all = pages[0];
    EXPECT_TRUE(all.find("Column") != std::string::npos);
    EXPECT_TRUE(all.find("Second") != std::string::npos);
}

TEST(PrintPdf, EverySheetIsLaidWhiteFirst) {
    // A PDF reader does not lay a page background down for a page, so the
    // writer paints the sheet before anything else. Night mode must not
    // save a dark page.
    const layout::DocumentLayout laid = LayOut("One dark screen, one white sheet.\n");
    const std::vector<layout::PrintBand> bands =
        layout::PaginateDocument(laid, layout::PrintPage());
    // The path is held in a string for as long as ReadPages uses it: a
    // temporary's c_str() would dangle, and the allocator on the mac
    // poisoned it before the file could even be opened.
    const std::string path = TempPath();
    print_pdf::WriteReport report;
    const std::string bytes =
        print_pdf::WriteBytes(laid, layout::PrintPage(), bands, &report);
    EXPECT_TRUE(!bytes.empty());
    const std::vector<std::string> pages = ReadPages(bytes, path);
    EXPECT_TRUE(!pages.empty());
    if (pages.empty()) return;
    // White is laid with the RGB fill operator before any content.
    EXPECT_TRUE(pages[0].find("1 1 1 rg") != std::string::npos);
}

TEST(PrintPdf, NoBandsNoFile) {
    const layout::DocumentLayout laid = LayOut("Just one line.\n");
    print_pdf::WriteReport report;
    const std::string bytes =
        print_pdf::WriteBytes(laid, layout::PrintPage(), {}, &report);
    EXPECT_TRUE(bytes.empty());
    EXPECT_EQ(report.pages, 0);
}
