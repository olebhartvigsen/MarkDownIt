// Modern Windows printing, the app's side of the contract. Plan:
// .hermes/plans/2026-11-19_233000-modern-windows-print-integration.md
//
// The dialog and the print pipeline call the app on their own thread.
// Layout and pagination stay on the window's thread, through the one
// build path the classic print already walks: the source marshals each
// page build to the window (HandleWindowMessage runs the builder inside
// the window's own message loop), keeps the finished page as read-only
// shared state, and paints every medium through the shared
// layout::PaintBandPage.
//
// This header is the whole public surface: app.cpp registers the window
// at startup, forwards the message pump hook, and shows the dialog from
// Print() with a builder that answers from the live document.

#ifndef MARKDOWNIT_SRC_PRINT_WINRT_H_
#define MARKDOWNIT_SRC_PRINT_WINRT_H_

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/print_pages.h"

namespace print_winrt {

// False when the operator asked for the classic panel by setting
// MARKDOWNIT_NO_MODERN_PRINT=1, true otherwise. The availability of the
// print stack itself is answered per window by RegisterWindow.
bool ModernPrintEnabled();

// Registers the window with the modern print stack: one PrintManager per
// window for the process, alive until the window goes away. Call once,
// after the window exists and before the first Print. False means the
// stack is not usable on this machine and Ctrl+P falls back to the
// classic panel.
bool RegisterWindow(HWND window);

// One page build request, answered by the app on the window's own
// thread. The description fields arrive in seam units straight from the
// dialog's PrintPageDescription (a DIP is a seam unit: 96 to the inch),
// sheet measured from its top left, imageable rect inside it.
struct PageBuild {
    float sheetWidth = 793.7f;
    float sheetHeight = 1122.5f;
    float imageableX = 0.0f;
    float imageableY = 0.0f;
    float imageableW = 793.7f;
    float imageableH = 1122.5f;
    uint32_t jobPage = 1;      // the page the dialog is sitting on
    uint32_t rasterDpi = 0;    // the printer's own resolution, for the
                               // pipeline's raster rules
    // The answer. ok=false leaves the preview empty and prints nothing:
    // the honest answer for a document that cannot be laid out.
    bool ok = false;
    layout::PrintPage page;
    std::shared_ptr<const layout::DocumentLayout> laid;
    std::vector<layout::PrintBand> bands;
};

// The builder runs on the window's thread, inside its message loop, and
// fills the request from the live document. The Print path the classic
// panel already uses does this work: the shared page description builder
// (PrintPageFromDescription), the renderer's print layout, the cut, and
// the assignment of page, layout and bands.
using PageBuilder = std::function<void(PageBuild*)>;

// What a dialog needs. The builder is copied and kept until the dialog
// closes; it must answer from whatever is current, so a document edited
// with the dialog open prints as it stands.
struct DialogInputs {
    std::wstring title;     // the print task's name in the queue
    PageBuilder build;
};

// Shows the print dialog for a registered window. False when the dialog
// could not even be asked for (no registration, an earlier dialog still
// open): the caller falls back to the classic panel. The dialog's own
// lifetime is not this call's business; the app learns its end through
// the task's Completed, and the source dissolves then.
bool ShowPrintDialog(HWND window, const DialogInputs& inputs);

// The message-pump hook. Call it from the window's procedure for every
// message; it returns true when the message was one of ours, answered
// inside the call by the registered builder. Answering here is what
// keeps every layout walk on the window's thread: the print thread
// blocks in its message send while the window builds.
bool HandleWindowMessage(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam);

}  // namespace print_winrt

#endif  // MARKDOWNIT_SRC_PRINT_WINRT_H_
