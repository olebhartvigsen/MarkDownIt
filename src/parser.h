#pragma once

// Parser bridge: drives md4c SAX callbacks to populate a Document.
// md4c parses UTF-8; we convert to UTF-32 for DirectWrite in the text callback.

#include <string>
#include "dom.h"

// Parse UTF-8 markdown text into out.
// Returns true on success, false on parse error.
bool ParseMarkdown(const std::string& utf8, Document& out);
