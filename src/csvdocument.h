// A CSV file as a Document: one table node, the header row flagged, the
// columns' alignment decided, the rows the filters keep, and a notice when
// the caps or the filters left something out.
//
// This is the whole bridge between the CSV parser and the render engine. The
// table layout, the display list and the two painters never learn that the
// rows came from a file instead of a markdown table.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "csv.h"
#include "csvfilter.h"
#include "dom.h"

namespace csvdoc {

// Whether the path names a file the table view reads: .csv, .tsv, .tab.
bool IsCsvPath(const std::string& pathUtf8);

// The delimiter a file's extension asks for, or 0 to sniff: a .tsv or .tab
// file is tab separated by definition, a .csv file may be either comma or
// semicolon in the wild.
char DelimiterHintForPath(const std::string& pathUtf8);

// The document the table view draws, with the filters applied. Always at
// least one node: an empty file, an unreadable encoding and a filter set
// that matches nothing all say so in a paragraph.
Document BuildDocument(const std::string& utf8Text,
                       const std::string& pathUtf8,
                       const csvfilter::State& filters);

// The same document with every row visible.
Document BuildDocument(const std::string& utf8Text,
                       const std::string& pathUtf8);

// What the page says about what it left out, in the reader's words, or an
// empty string when nothing was left out.
std::string NoticeText(const csv::Table& table);

// The line under a filtered table: how many rows of how many are shown.
std::string FilterNotice(const csv::Table& table, std::size_t shownRows,
                         const csvfilter::State& filters);

// The distinct values of one column, for the menu behind its filter button.
std::vector<csvfilter::Value> ColumnValues(const std::string& utf8Text,
                                          const std::string& pathUtf8,
                                          std::size_t column,
                                          std::size_t limit);

// One row of a column's filter menu, in the form both ports build their own
// menu from: a Windows popup and a mac NSMenu read the same list.
struct FilterMenuItem {
    std::string label;    // what the menu shows
    std::string query;    // what picking it sets, empty for the clear row
    bool exact = true;    // a value, not a typed query
    bool checked = false; // this value is the column's filter now
    bool clear = false;   // the row that clears the column
    bool prompt = false;  // the row that asks for a typed query
    bool separator = false;  // a divider before this row
};

// The menu for one column: the row that clears it, the row that asks for a
// typed query, then its values with the active one ticked. At most `limit`
// values, since a menu is not a table; the typed row is the way past the
// limit, and the way to ask for a part of a value.
std::vector<FilterMenuItem> FilterMenu(const std::string& utf8Text,
                                       const std::string& pathUtf8,
                                       std::size_t column,
                                       const csvfilter::State& filters,
                                       std::size_t limit);

}  // namespace csvdoc
