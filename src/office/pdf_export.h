#pragma once

#include <string>
#include "office_model.h"

namespace office {

struct PdfExportOptions {
    bool all_pages = true;
    int first_page = 1;
    int last_page = 0;   // 0 means "through the last page"

    // A protected PDF (office spec, PDF export): the standard
    // security handler with an owner and a user password, plus the
    // permission flags for printing, copying and changing. The
    // protection applies when encrypt is set; an empty owner
    // password is refused by the writer.
    bool encrypt = false;
    bool allow_printing = true;
    bool allow_copying = true;
    bool allow_modifying = true;
    std::string owner_password;
    std::string user_password;
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
