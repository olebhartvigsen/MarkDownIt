#pragma once

#include <string>
#include "office_model.h"

// Import a Word .docx package from raw bytes into the shared document model.
// Returns true on success. On failure it returns false and sets error, and
// report lists any features that were dropped or approximated.
namespace office {

bool DocxImport(const std::string& bytes, DocModel& out, CompatReport& report,
                std::string& error);

}  // namespace office
