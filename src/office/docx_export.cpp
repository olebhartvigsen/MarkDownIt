#include "docx_export.h"

// Stub: the OOXML writer lands in a later task.
namespace office {

bool DocxExport(const DocModel& doc, std::string& out_bytes, CompatReport& report,
                std::string& error) {
    (void)doc;
    (void)report;
    out_bytes.clear();
    error = "not implemented";
    return false;
}

}  // namespace office
