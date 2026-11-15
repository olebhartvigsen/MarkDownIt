// A CSV file as a Document: one table node, the header row flagged, the
// columns' alignment decided, and a notice when the caps left something out.
//
// This is the whole bridge between the CSV parser and the render engine. The
// table layout, the display list and the two painters never learn that the
// rows came from a file instead of a markdown table.

#pragma once

#include <string>

#include "csv.h"
#include "dom.h"

namespace csvdoc {

// Whether the path names a file the table view reads: .csv, .tsv, .tab.
bool IsCsvPath(const std::string& pathUtf8);

// The delimiter a file's extension asks for, or 0 to sniff: a .tsv or .tab
// file is tab separated by definition, a .csv file may be either comma or
// semicolon in the wild.
char DelimiterHintForPath(const std::string& pathUtf8);

// The document the table view draws. Always at least one node: an empty file
// and an unreadable encoding both say so in a paragraph.
Document BuildDocument(const std::string& utf8Text,
                       const std::string& pathUtf8);

// What the page says about what it left out, in the reader's words, or an
// empty string when nothing was left out.
std::string NoticeText(const csv::Table& table);

}  // namespace csvdoc
