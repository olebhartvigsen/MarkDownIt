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
