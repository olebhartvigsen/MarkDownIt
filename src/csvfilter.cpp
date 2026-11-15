#include "csvfilter.h"

#include <algorithm>
#include <cstdint>

#include "unicode_fold.h"

namespace csvfilter {
namespace {

void AppendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// One code point of UTF-8, or the byte itself when the sequence is broken.
std::uint32_t DecodeOne(const std::string& text, std::size_t at,
                        std::size_t* length) {
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    *length = 1;
    if (lead < 0x80) return lead;
    if ((lead >> 5) == 0x06 && at + 1 < text.size()) {
        *length = 2;
        return ((lead & 0x1Fu) << 6) |
               (static_cast<unsigned char>(text[at + 1]) & 0x3Fu);
    }
    if ((lead >> 4) == 0x0E && at + 2 < text.size()) {
        *length = 3;
        return ((lead & 0x0Fu) << 12) |
               ((static_cast<unsigned char>(text[at + 1]) & 0x3Fu) << 6) |
               (static_cast<unsigned char>(text[at + 2]) & 0x3Fu);
    }
    if ((lead >> 3) == 0x1E && at + 3 < text.size()) {
        *length = 4;
        return ((lead & 0x07u) << 18) |
               ((static_cast<unsigned char>(text[at + 1]) & 0x3Fu) << 12) |
               ((static_cast<unsigned char>(text[at + 2]) & 0x3Fu) << 6) |
               (static_cast<unsigned char>(text[at + 3]) & 0x3Fu);
    }
    return lead;
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        --end;
    return text.substr(begin, end - begin);
}

// Whether a column's values are numbers, which decides how a menu of them
// is ordered.
bool AllNumeric(const std::vector<Value>& values) {
    if (values.empty()) return false;
    for (const Value& value : values) {
        if (!csv::LooksNumeric(value.text)) return false;
    }
    return true;
}

}  // namespace

const Filter* State::For(std::size_t column) const {
    for (const Filter& filter : filters) {
        if (filter.column == column) return &filter;
    }
    return nullptr;
}

bool State::Active(std::size_t column) const {
    const Filter* filter = For(column);
    return filter != nullptr && !filter->query.empty();
}

void State::Set(std::size_t column, const std::string& query, bool exact) {
    if (query.empty()) {
        Clear(column);
        return;
    }
    for (Filter& filter : filters) {
        if (filter.column == column) {
            filter.query = query;
            filter.exact = exact;
            return;
        }
    }
    filters.push_back(Filter{column, query, exact});
}

void State::Clear(std::size_t column) {
    filters.erase(std::remove_if(filters.begin(), filters.end(),
                                 [column](const Filter& filter) {
                                     return filter.column == column;
                                 }),
                  filters.end());
}

void State::ClearAll() { filters.clear(); }

std::string Fold(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t length = 1;
        std::uint32_t code = DecodeOne(text, at, &length);
        if (code >= 'A' && code <= 'Z') {
            AppendUtf8(out, code + 32);
        } else {
            std::uint32_t folded[4] = {0, 0, 0, 0};
            const std::size_t count = unicode::FoldInto(code, folded, 4);
            AppendUtf8(out, count == 0 ? code : folded[0]);
        }
        at += length;
    }
    return out;
}

bool Matches(const std::string& cell, const std::string& foldedQuery,
             bool exact) {
    if (foldedQuery.empty()) return true;
    const std::string folded = Fold(cell);
    if (exact) return folded == foldedQuery;
    return folded.find(foldedQuery) != std::string::npos;
}

std::vector<std::size_t> VisibleRows(const csv::Table& table,
                                     const State& state) {
    std::vector<std::size_t> rows;
    rows.reserve(table.rows.size());
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        // The header is the way back out of a filter, so a filter never
        // hides it.
        if (table.hasHeader && r == 0) {
            rows.push_back(r);
            continue;
        }
        bool keep = true;
        for (const Filter& filter : state.filters) {
            if (filter.query.empty()) continue;
            const std::string folded = Fold(filter.query);
            const std::size_t here = filter.column;
            const std::string cell =
                here < table.rows[r].size() ? table.rows[r][here] : std::string();
            if (!Matches(cell, folded, filter.exact)) {
                keep = false;
                break;
            }
        }
        if (keep) rows.push_back(r);
    }
    return rows;
}

std::vector<Value> Values(const csv::Table& table, std::size_t column,
                          std::size_t limit) {
    std::vector<Value> values;
    const std::size_t first = table.hasHeader ? 1 : 0;
    for (std::size_t r = first; r < table.rows.size(); ++r) {
        if (column >= table.rows[r].size()) continue;
        const std::string text = Trim(table.rows[r][column]);
        if (text.empty()) continue;
        bool found = false;
        for (Value& value : values) {
            if (value.text == text) {
                ++value.count;
                found = true;
                break;
            }
        }
        if (!found) values.push_back(Value{text, 1});
    }
    const bool numeric = AllNumeric(values);
    std::stable_sort(values.begin(), values.end(),
                     [numeric](const Value& a, const Value& b) {
                         if (a.count != b.count) return a.count > b.count;
                         if (numeric) {
                             const double x = std::strtod(a.text.c_str(), nullptr);
                             const double y = std::strtod(b.text.c_str(), nullptr);
                             if (x != y) return x < y;
                         }
                         return a.text < b.text;
                     });
    if (values.size() > limit) values.resize(limit);
    return values;
}

}  // namespace csvfilter
