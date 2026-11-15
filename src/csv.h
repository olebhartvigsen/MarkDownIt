// Reading a CSV file into rows (plan: .hermes/plans/2026-11-15_160657-csv-support.md).
//
// RFC 4180 quoting, the line ends and byte order marks files actually carry,
// and delimiter sniffing. No file API, no window and no platform header, so
// the parser runs in the portable tests and in both ports unchanged.
//
// A cap is a product decision and every one of them is reported: a reader who
// is shown 20,000 of 120,000 rows is told so, and nothing is dropped in
// silence.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace csv {

// 20,000 rows of 64 columns still lays out inside a second in the current
// engine, which measures every cell of a table block up front.
constexpr std::size_t kMaxRows = 20000;
constexpr std::size_t kMaxColumns = 64;
constexpr std::size_t kMaxCellBytes = 1024 * 1024;

struct Table {
    // UTF-8 cell text, every row padded to the widest row.
    std::vector<std::vector<std::string>> rows;
    char delimiter = ',';
    bool hasHeader = false;
    // How many records the file held, which can be more than rows.size().
    std::size_t sourceRows = 0;
    bool rowsTruncated = false;
    bool columnsTruncated = false;
    bool cellsTruncated = false;
    // The file starts with a UTF-16 or UTF-32 mark: rows is empty and the
    // window says the encoding is not readable yet, rather than showing noise.
    bool unsupportedEncoding = false;
};

// The delimiter the first records use: comma, semicolon, tab or bar, taken by
// how consistent a field count it produces. Comma wins a tie.
char SniffDelimiter(const std::string& utf8Text);

// Whether a cell holds a plain number: an optional sign, digits, at most one
// decimal point, an optional exponent. A thousands separator or a currency
// mark makes it text, which is honest and can be taught later.
bool LooksNumeric(const std::string& cell);

// Whether the first row names the columns: every cell non-empty, none of them
// a number, and no name repeated. A guess, and the page lets it be flipped.
bool LooksLikeHeader(const std::vector<std::vector<std::string>>& rows);

// Parse the text of a file. hint is 0 to sniff the delimiter, or the
// delimiter to force (a .tsv file passes the tab).
Table Parse(const std::string& utf8Text, char hint = 0);

}  // namespace csv
