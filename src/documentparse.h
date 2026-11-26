// The one answer to "what does this buffer draw as".
//
// Both ports used to call ParseMarkdown on whatever the buffer held. A file
// kind decides the parse, so the choice lives here: markdown takes the md4c
// path, a .csv or .tsv file takes the table path, and the shells keep calling
// one function whichever file they opened.

#pragma once

#include <string>

#include "csvfilter.h"
#include "dom.h"
#include "textbuffer.h"

// Parse the buffer text of a document into the node list the renderer draws.
// Returns false only when markdown parsing failed; the previous document then
// stays on screen, the way both ports already handle it.
bool ParseDocument(const std::string& utf8, const std::string& pathUtf8,
                   const csvfilter::State& tableFilters, Document& out);

// The reparse after an edit: the incremental path when the buffer knows which
// byte range its splices covered, the full parse otherwise.
//
// This decision lives here, not in a shell. Both ports edit through the same
// TextBuffer, so both can answer "what changed", and a shell that made the
// choice itself would be the second copy of one policy: the macOS app would
// keep full-parsing every keystroke while Windows took the fast path. A CSV
// table is regenerated from its columns as a whole, so it always takes the
// full parse; the incremental path also stands down by itself when the edit
// reaches the title, a link reference definition, or a block's shape, and
// then the full parse answers instead.
bool ReparseDocument(const std::string& utf8, const std::string& pathUtf8,
                     const csvfilter::State& tableFilters, Document& doc,
                     const ReparseRange& range);

// The same parse with no column filtered.
bool ParseDocument(const std::string& utf8, const std::string& pathUtf8,
                   Document& out);
