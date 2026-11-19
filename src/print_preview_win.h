// The Windows print preview: the native window Ctrl+P opens, showing the
// pages File > Print will print before anything is sent to paper.
//
// The macOS print panel carries its own preview (AppKit), and the common
// PrintDlg on Windows carries none, so this window is the Windows half of
// that one panel: the same shared bands (src/layout/print_pages.h) at the
// default A4 page the QA walk reports, a Print button that opens the
// classic panel, and a Save as PDF button that writes the same bands
// through the shared pdfio writer (src/print_pdf.cpp), the answer to the
// mac panel's own Save as PDF.
//
// The window is chrome: it draws through the RenderTarget seam like every
// other surface, and only the frame, the buttons and the modal loop are
// Win32. Plan: .hermes/plans/2026-11-15_204703-printing.md.

#ifndef MARKDOWNIT_SRC_PRINT_PREVIEW_WIN_H_
#define MARKDOWNIT_SRC_PRINT_PREVIEW_WIN_H_

#include <windows.h>

#include <string>

struct Document;
class Renderer;
struct IDWriteFactory;

namespace print_preview {

// What the reader chose in the preview window. The shell owns the
// consequences: Print opens the print panel and prints, SavePdf asks for
// a file, None prints nothing at all.
enum class Action {
    None,
    Print,
    SavePdf,
};

// Shows the pages in a preview window modal to the owner, and returns what
// the reader chose. The paper is the default A4 page; the print panel may
// offer a different one, and a paper that changes the cut changes the pages,
// which references/printing.md records as the one honest difference between
// the preview and the sheet.
Action ShowPreview(HWND owner, Renderer& renderer, IDWriteFactory* dwrite,
                   bool layoutReady, const Document& document,
                   const std::wstring& documentName);

}  // namespace print_preview

#endif  // MARKDOWNIT_SRC_PRINT_PREVIEW_WIN_H_
