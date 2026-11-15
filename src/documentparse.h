// The one answer to "what does this buffer draw as".
//
// Both ports used to call ParseMarkdown on whatever the buffer held. A file
// kind decides the parse, so the choice lives here: markdown takes the md4c
// path, a .csv or .tsv file takes the table path, and the shells keep calling
// one function whichever file they opened.

#pragma once

#include <string>

#include "dom.h"

// Parse the buffer text of a document into the node list the renderer draws.
// Returns false only when markdown parsing failed; the previous document then
// stays on screen, the way both ports already handle it.
bool ParseDocument(const std::string& utf8, const std::string& pathUtf8,
                   Document& out);
