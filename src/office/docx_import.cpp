#include "docx_import.h"

// Stub: the OOXML reader lands in a later task. Until then the importer
// reports the feature as unavailable instead of pretending to succeed.
namespace office {

bool DocxImport(const std::string& bytes, DocModel& out, CompatReport& report,
                std::string& error) {
    (void)bytes;
    (void)out;
    (void)report;
    error = "not implemented";
    return false;
}

}  // namespace office
