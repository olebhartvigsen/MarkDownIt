#include "office/office_session.h"

#include "office/docx_export.h"
#include "office/docx_import.h"
#include "office/markdown_bridge.h"

namespace office {

namespace {

std::string Failure(const char* headline, const std::string& error) {
    std::string text = headline;
    if (!error.empty()) {
        text += "\n\n";
        text += error;
    }
    return text;
}

}  // namespace

ImportResult ImportWordBytes(const std::string& bytes) {
    ImportResult result;
    DocModel model;
    std::string error;
    if (!DocxImport(bytes, model, result.report, error)) {
        result.message = Failure("The Word file could not be read.", error);
        return result;
    }
    // The report gains everything markdown has no form for.
    result.markdown = OfficeModelToMarkdown(model, result.report);
    result.ok = true;
    return result;
}

ExportResult ExportWordBytes(const std::string& markdown) {
    ExportResult result;
    DocModel model;
    std::string error;
    if (!MarkdownToOfficeModel(markdown, model, result.report, error)) {
        result.message = Failure("The document could not be read.", error);
        return result;
    }
    if (!DocxExport(model, result.bytes, result.report, error) ||
        result.bytes.empty()) {
        result.bytes.clear();
        result.message = Failure("The Word file could not be written.", error);
        return result;
    }
    result.ok = true;
    return result;
}

ExportResult ExportPdfBytes(const std::string& markdown,
                            const PdfExportOptions& options) {
    ExportResult result;
    DocModel model;
    std::string error;
    if (!MarkdownToOfficeModel(markdown, model, result.report, error)) {
        result.message = Failure("The document could not be read.", error);
        return result;
    }
    if (!PdfExport(model, options, result.bytes, result.report, error,
                   &result.pageCount) ||
        result.bytes.empty()) {
        result.bytes.clear();
        result.message = Failure("The PDF could not be written.", error);
        return result;
    }
    result.ok = true;
    return result;
}

std::string CompatNotesText(const CompatReport& report) {
    std::string text;
    for (const CompatWarning& warning : report.warnings) {
        text += "- ";
        text += warning.feature;
        if (!warning.detail.empty()) {
            text += ": ";
            text += warning.detail;
        }
        text += "\n";
    }
    return text;
}

std::string ExportDoneText(const std::string& pathUtf8, int pageCount,
                           const CompatReport& report) {
    std::string text = "The document was exported to:\n" + pathUtf8;
    if (pageCount > 0) {
        text += "\n\nPages: " + std::to_string(pageCount);
    }
    const std::string notes = CompatNotesText(report);
    if (!notes.empty()) {
        text += "\n\nCompatibility notes:\n\n";
        text += notes;
    }
    return text;
}

}  // namespace office
