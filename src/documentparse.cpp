#include "documentparse.h"

#include "csvdocument.h"
#include "parser.h"

bool ParseDocument(const std::string& utf8, const std::string& pathUtf8,
                   const csvfilter::State& tableFilters, Document& out) {
    if (csvdoc::IsCsvPath(pathUtf8)) {
        out = csvdoc::BuildDocument(utf8, pathUtf8, tableFilters);
        return true;
    }
    return ParseMarkdown(utf8, out);
}

bool ParseDocument(const std::string& utf8, const std::string& pathUtf8,
                   Document& out) {
    return ParseDocument(utf8, pathUtf8, csvfilter::State{}, out);
}

bool ReparseDocument(const std::string& utf8, const std::string& pathUtf8,
                     const csvfilter::State& tableFilters, Document& doc,
                     const ReparseRange& range) {
    // A CSV table is generated from its columns as a whole, and a buffer with
    // no recorded splice has nothing to stand on: both take the full parse.
    if (!range.valid || csvdoc::IsCsvPath(pathUtf8)) {
        doc = Document{};
        return ParseDocument(utf8, pathUtf8, tableFilters, doc);
    }
    if (ParseMarkdownIncremental(utf8, doc, range)) return true;
    // The incremental path declined, or ran out of memory part way through and
    // left an unknown document behind. The full parse starts from an empty
    // document, so either way what remains is the parse's own answer.
    doc = Document{};
    return ParseDocument(utf8, pathUtf8, tableFilters, doc);
}
