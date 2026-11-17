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
class MacRenderer;
}  // namespace platform

namespace print_mac {

// Opens the system print panel for the window and prints the document, one
// band per page. The panel's paper decides the page; the view fills the
// sheet's imageable area edge to edge and the page model keeps its own
// margin, so no second scale factor ever enters the drawing. Returns false
// when the user cancels.
bool PrintDocument(NSWindow* window, platform::MacRenderer& renderer,
                   const Document& document, const std::string& title);

// Writes the same pages to a PDF file, and reports how many pages it wrote.
// The QA capture uses it, and it is also the one honest way to look at the
// pagination of a document without a printer.
bool WritePdf(const std::string& path, platform::MacRenderer& renderer,
              const Document& document, const layout::PrintPage& page,
              int* pageCount);

}  // namespace print_mac
