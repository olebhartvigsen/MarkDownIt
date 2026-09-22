#include "pdf_export.h"

// Stub: the pdfio-backed writer lands in a later task.
namespace office {

bool PdfExport(const DocModel& doc, const PdfExportOptions& opt,
               std::string& out_bytes, CompatReport& report, std::string& error) {
    (void)doc;
    (void)opt;
    (void)report;
    out_bytes.clear();
    error = "not implemented";
    return false;
}

}  // namespace office
