// Printing: the print bands written as a PDF file.
//
// The mac panel carries its own Save as PDF (AppKit, src/macos/print_mac.mm),
// and the Windows preview window needs the same thing plus something a CI
// runner can check. This writer is the portable answer: it takes the same
// `PrintPage`, `DocumentLayout` and `PrintBand` list the print drivers cut
// the paper with, and replays each band's `PageItems` onto pdfio through the
// RenderTarget seam. The pages are therefore exactly what File > Print
// prints, by construction, on both ports.
//
// The base-14 fonts are used without embedding, the same compromise the
// office PDF export makes (references/office-import-export-spec.md): the file
// stays small and every reader opens it, and glyphs outside the WinAnsi
// encoding are dropped and counted rather than written as '?'. Plan:
// .hermes/plans/2026-11-15_204703-printing.md.

#ifndef MARKDOWNIT_SRC_PRINT_PDF_H_
#define MARKDOWNIT_SRC_PRINT_PDF_H_

#include "layout/print_pages.h"

#include <string>
#include <vector>

namespace print_pdf {

// What the writer observed while it worked.
struct WriteReport {
    // The number of pages written, one per band.
    int pages = 0;
    // Code points the base-14 font encoding cannot show, dropped and
    // counted. 0 for plain text and Danish.
    size_t droppedGlyphs = 0;
};

// Writes the bands as one PDF and returns its bytes, empty on failure. The
// caller owns the file: a save dialog and a write on Windows, a scratch file
// in the QA walk, a direct write in the tests.
std::string WriteBytes(const layout::DocumentLayout& laid,
                       const layout::PrintPage& page,
                       const std::vector<layout::PrintBand>& bands,
                       WriteReport* report);

}  // namespace print_pdf

#endif  // MARKDOWNIT_SRC_PRINT_PDF_H_
