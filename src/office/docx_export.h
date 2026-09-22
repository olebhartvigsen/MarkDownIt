#pragma once

#include <string>
#include "office_model.h"

// Write the shared document model to a .docx package in out_bytes.
// Returns true on success. On failure it returns false and sets error, and
// report lists any model features the writer had to drop.
namespace office {

bool DocxExport(const DocModel& doc, std::string& out_bytes, CompatReport& report,
                std::string& error);

}  // namespace office
