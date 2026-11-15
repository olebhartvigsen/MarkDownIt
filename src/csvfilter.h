// The per-column filters a table document carries.
//
// A filter is what Excel puts behind the arrow in a column header: a text
// that the cell must contain, compared without case. One filter per column,
// no order, no types. The rows that pass every filter are the rows the
// table draws, so filtering is a property of the node list and not of the
// renderer.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "csv.h"

namespace csvfilter {

struct Filter {
    std::size_t column = 0;
    // What the reader typed, as typed. Empty means the column is unfiltered.
    std::string query;
    // A value picked from the column's menu must equal the cell; a query
    // typed by hand only has to be found in it. Excel makes the same
    // distinction between ticking a value and typing in its search box.
    bool exact = false;
};

struct State {
    std::vector<Filter> filters;

    // The filter on one column, or nullptr.
    const Filter* For(std::size_t column) const;
    bool Active(std::size_t column) const;
    // Sets or replaces the column's filter. An empty query clears it, so a
    // column has at most one filter and never an empty one.
    void Set(std::size_t column, const std::string& query, bool exact = false);
    void Clear(std::size_t column);
    void ClearAll();
    bool Any() const { return !filters.empty(); }
};

// The case fold the compare uses: ASCII letters lowered, and every other
// code point through the same fold table find uses. Accents are kept, so
// "ae" does not match "æ" and "a" does not match "å": those are different
// letters here, not the same one written differently.
std::string Fold(const std::string& text);

// Whether a cell passes a folded query: equal to it when the filter is
// exact, containing it otherwise. An empty query passes everything.
bool Matches(const std::string& cell, const std::string& foldedQuery,
             bool exact = false);

// The rows the filters keep, in document order. The header row is kept
// whatever the filters say, because the header is the affordance that
// clears them.
std::vector<std::size_t> VisibleRows(const csv::Table& table,
                                     const State& state);

// One distinct value of a column and how many body rows carry it.
struct Value {
    std::string text;
    std::size_t count = 0;
};

// The distinct values of one column, most frequent first and ties in the
// order they first appear, at most `limit` of them. Numbers sort before
// text when both describe the same column, so a menu of figures reads like
// a column of figures.
std::vector<Value> Values(const csv::Table& table, std::size_t column,
                          std::size_t limit);

}  // namespace csvfilter
