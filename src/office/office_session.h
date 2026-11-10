#pragma once

// The one Office conversion pipeline both ports call (plan 4.1, batch 4
// family 6, the import and export half). Windows and macOS each ran the
// same chain by hand: bytes to model to markdown, markdown to model to
// bytes, the failure texts, and the compatibility notes. The chain lives
// here once. The dialogs, the file panels and the PDF options UI stay
// native; what they pass in and what they get back is plain data.

#include <string>

#include "office/office_model.h"
#include "office/pdf_export.h"

namespace office {

struct ImportResult {
    bool ok = false;
    std::string markdown;
    CompatReport report;
    // The user-facing failure text, headline plus the importer's own
    // description of the file. Empty when ok.
    std::string message;
};

struct ExportResult {
    bool ok = false;
    std::string bytes;
    int pageCount = 0;
    CompatReport report;
    std::string message;
};

// A .docx package as markdown. A legacy .doc, an empty or an encrypted
// package fails with the importer's description in message.
ImportResult ImportWordBytes(const std::string& bytes);

// The markdown as a .docx package.
ExportResult ExportWordBytes(const std::string& markdown);

// The markdown as a PDF, with the scope and protection in options.
ExportResult ExportPdfBytes(const std::string& markdown,
                            const PdfExportOptions& options);

// The compatibility report as dialog text: one "- feature: detail" line
// per dropped or approximated feature. Empty when there is nothing to
// report.
std::string CompatNotesText(const CompatReport& report);

// The words of the report after a successful export: where the file went,
// the page count for a PDF (pageCount > 0), and the notes.
std::string ExportDoneText(const std::string& pathUtf8, int pageCount,
                           const CompatReport& report);

}  // namespace office
