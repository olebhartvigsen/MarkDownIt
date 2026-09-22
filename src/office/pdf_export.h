#pragma once

#include <string>
#include "office_model.h"

namespace office {

struct PdfExportOptions {
    bool all_pages = true;
    int first_page = 1;
    int last_page = 0;   // 0 means "through the last page"
};

// Render the shared document model to PDF bytes with pdfio.
// Returns true on success. On failure it returns false and sets error, and
// report lists any model features the writer had to drop (it carries no
// informational entries). When page_count is not null it receives the number
// of pages in the produced file.
bool PdfExport(const DocModel& doc, const PdfExportOptions& opt,
               std::string& out_bytes, CompatReport& report, std::string& error,
               int* page_count = nullptr);

}  // namespace office
