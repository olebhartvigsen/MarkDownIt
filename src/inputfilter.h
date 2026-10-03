#pragma once
#include "textbuffer.h"
#include <string>
#include <cstdint>

// Return the bytes to splice for a typed character at this offset,
// escaped if markdown would otherwise treat it as syntax.
// The set is: * _ ` # > [ ] ( ) ! \ and only when the position
// makes it meaningful (at line start for #, >, etc.).
std::string EscapeForInsert(const TextBuffer& buf, uint32_t offset,
                            const std::string& typed,
                            bool inTableCell = false);

std::string EscapeForPaste(const TextBuffer& buf, uint32_t offset,
                           const std::string& text,
                           bool inTableCell = false);
