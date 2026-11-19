// Printing, Windows half: the printer panel, and one page per band through a
// Direct2D DC render target. Plan: .hermes/plans/2026-11-15_204703-printing.md
//
// The page model and the cut are shared (src/layout/print_pages.h). What is
// Windows' own is the panel (PrintDlg), the printer DC, and the mapping
// from seam units onto the printable area: the target carries the printer's
// dpi, so one seam unit is one DIP on paper and no scale factor is needed.
//
// Known gaps, recorded in the plan rather than hidden here: a Mermaid diagram
// and an SVG fence print as empty bands, an inline image is not resolved onto
// the page, and a block taller than a page is cut through its line.

#ifndef MARKDOWNIT_SRC_PRINT_WIN_H_
#define MARKDOWNIT_SRC_PRINT_WIN_H_

#include <windows.h>

#include <string>

struct Document;
class Renderer;
struct IDWriteFactory;

namespace print_win {

// Opens the print panel for the window, lays the document out for the paper
// the user picked, and prints one page per band. Returns false when the panel
// is cancelled, when the printer cannot be opened, or when a page fails to
// draw; cancelling is not an error and says nothing to the user, and the
// message stays empty. A drawn job that failed leaves the reason in message
// (UTF-8) for the caller's MessageBox, because a print that dies quietly
// reads as "this app cannot print".
bool PrintDocument(HWND owner, Renderer& renderer, IDWriteFactory* dwrite,
                   const Document& document, const std::wstring& title,
                   std::string* message = nullptr);

}  // namespace print_win

#endif  // MARKDOWNIT_SRC_PRINT_WIN_H_
