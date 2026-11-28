// Printing, macOS half: the print panel through AppKit, and the same pages
// written as a PDF. Plan: .hermes/plans/2026-11-15_204703-printing.md
//
// The page model and the cut are shared (src/layout/print_pages.h). What is
// the mac's own is the panel (NSPrintOperation over a view that answers one
// page per band) and Core Graphics: paper speaks points and the layout speaks
// seam units (1/96 inch), so the conversion happens once, here, at 0.75.
//
// Known gaps, recorded in the plan rather than hidden: a Mermaid diagram and
// an SVG fence print as empty bands, an inline image is not resolved onto the
// page, and a block taller than a page is cut through its line.

#pragma once

#import <Cocoa/Cocoa.h>

#include <string>

#include "layout/print_pages.h"

struct Document;

namespace platform {
class DocumentRenderer;
}  // namespace platform

namespace print_mac {

// Opens the system print panel for the window and prints the document, one
// band per page. The panel's paper decides the page; the view fills the
// sheet's imageable area edge to edge and the page model keeps its own
// margin, so no second scale factor ever enters the drawing. The operation
// owns the panel, so a printer, a Save as PDF destination and a cancel all go
// through AppKit. Returns false when a panel is already up.
bool PrintDocument(NSWindow* window, platform::DocumentRenderer& renderer,
                   const Document& document, const std::string& title);

// What a written print job turned out to contain: the pages the pagination
// produced, and how much of the first three pages carries ink. A page that
// comes back blank here is blank on paper, because the printer's own path
// produced this file.
struct PrintFileReport {
    int pages = 0;
    int ink[3] = {0, 0, 0};
};

// Runs the printer's own path and writes the result to a file instead of a
// printer: AppKit asks the view for its pages and cuts them the way it cuts a
// real job. The QA walk uses it, and it is the one honest way to look at the
// pagination without paper.
bool PrintToFile(const std::string& path, platform::DocumentRenderer& renderer,
                 const Document& document, const std::string& title,
                 PrintFileReport* report);

// Writes the same pages to a PDF file, and reports how many pages it wrote.
// The QA capture uses it, and it is also the one honest way to look at the
// pagination of a document without a printer.
bool WritePdf(const std::string& path, platform::DocumentRenderer& renderer,
              const Document& document, const layout::PrintPage& page,
              int* pageCount);

}  // namespace print_mac
