#include "csv.h"

#include <algorithm>
#include <cstdint>

namespace csv {
namespace {

bool IsDelimiter(char c) {
    return c == ',' || c == ';' || c == '\t' || c == '|';
}

// One headerless reader over the whole text: a record ends outside quotes,
// and inside them every byte is data, including the delimiters and the line
// ends. A doubled quote is one quote.
class Reader {
public:
    Reader(const std::string& text, char delimiter)
        : text_(text), delimiter_(delimiter) {}

    // False when the text is used up.
    bool ReadRecord(std::vector<std::string>* fields) {
        fields->clear();
        if (at_ >= text_.size()) return false;

        std::string field;
        bool quoted = false;
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (quoted) {
                if (c == '"') {
                    if (at_ + 1 < text_.size() && text_[at_ + 1] == '"') {
                        field += '"';
                        at_ += 2;
                        continue;
                    }
                    quoted = false;
                    ++at_;
                    continue;
                }
                field += c;
                ++at_;
                continue;
            }
            if (c == '"') {
                quoted = true;
                ++at_;
                continue;
            }
            if (c == delimiter_) {
                fields->push_back(std::move(field));
                field.clear();
                ++at_;
                continue;
            }
            if (c == '\r' || c == '\n') {
                ++at_;
                if (c == '\r' && at_ < text_.size() && text_[at_] == '\n') ++at_;
                fields->push_back(std::move(field));
                return true;
            }
            field += c;
            ++at_;
        }
        fields->push_back(std::move(field));
        return true;
    }

private:
    const std::string& text_;
    char delimiter_;
    std::size_t at_ = 0;
};

// The field count most of the first records agree on, and how many records
// agree on it.
void FieldCountMode(const std::string& text, char delimiter,
                    std::size_t* mode, std::size_t* agreed) {
    Reader reader(text, delimiter);
    std::vector<std::size_t> counts;
    std::vector<std::string> fields;
    for (int n = 0; n < 20 && reader.ReadRecord(&fields); ++n) {
        if (fields.size() == 1 && fields[0].empty()) continue;
        counts.push_back(fields.size());
    }
    *mode = 0;
    *agreed = 0;
    for (std::size_t count : counts) {
        const std::size_t here =
            static_cast<std::size_t>(std::count(counts.begin(), counts.end(),
                                                count));
        if (here > *agreed) {
            *agreed = here;
            *mode = count;
        }
    }
}

bool StartsWith(const std::string& text, const unsigned char* bytes,
                std::size_t size) {
    if (text.size() < size) return false;
    for (std::size_t i = 0; i < size; ++i) {
        if (static_cast<unsigned char>(text[i]) != bytes[i]) return false;
    }
    return true;
}

}  // namespace

char SniffDelimiter(const std::string& utf8Text) {
    const char candidates[] = {',', ';', '\t', '|'};
    char best = ',';
    std::size_t bestAgreed = 0;
    for (char candidate : candidates) {
        std::size_t mode = 0;
        std::size_t agreed = 0;
        FieldCountMode(utf8Text, candidate, &mode, &agreed);
        // One field everywhere is no table at all: the next candidate may
        // still find a real split.
        if (mode < 2) continue;
        if (agreed > bestAgreed) {
            bestAgreed = agreed;
            best = candidate;
        }
    }
    return best;
}

bool LooksNumeric(const std::string& cell) {
    std::size_t begin = 0;
    std::size_t end = cell.size();
    while (begin < end && (cell[begin] == ' ' || cell[begin] == '\t')) ++begin;
    while (end > begin && (cell[end - 1] == ' ' || cell[end - 1] == '\t')) --end;
    if (begin == end) return false;

    std::size_t i = begin;
    if (cell[i] == '+' || cell[i] == '-') ++i;
    bool digits = false;
    bool point = false;
    for (; i < end; ++i) {
        const char c = cell[i];
        if (c >= '0' && c <= '9') {
            digits = true;
            continue;
        }
        if (c == '.' && !point) {
            point = true;
            continue;
        }
        if ((c == 'e' || c == 'E') && digits) {
            ++i;
            if (i < end && (cell[i] == '+' || cell[i] == '-')) ++i;
            bool exponent = false;
            for (; i < end; ++i) {
                if (cell[i] < '0' || cell[i] > '9') return false;
                exponent = true;
            }
            return exponent;
        }
        return false;
    }
    return digits;
}

bool LooksLikeHeader(const std::vector<std::vector<std::string>>& rows) {
    // A data file almost always names its columns, and a header that is
    // missing costs the reader the name and the filter menu. So the first
    // record is a header unless it cannot be one: fewer than two records,
    // no text in it at all, or every filled cell a number (then it is a
    // record like the rest). The strict guess this replaces asked for
    // unique, filled, non-numeric cells and turned real headers down.
    if (rows.size() < 2) return false;
    for (const std::string& cell : rows[0]) {
        if (cell.empty()) continue;
        if (!LooksNumeric(cell)) return true;
    }
    return false;
}

Table Parse(const std::string& utf8Text, char hint) {
    Table table;

    std::string text = utf8Text;
    static const unsigned char kUtf8Bom[] = {0xEF, 0xBB, 0xBF};
    static const unsigned char kUtf16Le[] = {0xFF, 0xFE};
    static const unsigned char kUtf16Be[] = {0xFE, 0xFF};
    static const unsigned char kUtf32Le[] = {0xFF, 0xFE, 0x00, 0x00};
    static const unsigned char kUtf32Be[] = {0x00, 0x00, 0xFE, 0xFF};
    if (StartsWith(text, kUtf32Le, sizeof(kUtf32Le)) ||
        StartsWith(text, kUtf32Be, sizeof(kUtf32Be)) ||
        StartsWith(text, kUtf16Le, sizeof(kUtf16Le)) ||
        StartsWith(text, kUtf16Be, sizeof(kUtf16Be))) {
        table.unsupportedEncoding = true;
        return table;
    }
    if (StartsWith(text, kUtf8Bom, sizeof(kUtf8Bom))) text.erase(0, 3);

    table.delimiter = IsDelimiter(hint) ? hint : SniffDelimiter(text);

    Reader reader(text, table.delimiter);
    std::vector<std::string> fields;
    while (reader.ReadRecord(&fields)) {
        // Blank lines before the first record are formatting, not data.
        if (table.rows.empty() && fields.size() == 1 && fields[0].empty()) {
            continue;
        }
        ++table.sourceRows;
        if (table.rows.size() >= kMaxRows) {
            table.rowsTruncated = true;
            continue;  // keep counting, so the notice can name the total
        }

        std::vector<std::string> row;
        row.reserve((std::min)(fields.size(), kMaxColumns));
        for (const std::string& field : fields) {
            if (row.size() >= kMaxColumns) {
                table.columnsTruncated = true;
                break;
            }
            std::string cell = field;
            if (cell.size() > kMaxCellBytes) {
                cell.resize(kMaxCellBytes);
                table.cellsTruncated = true;
            }
            row.push_back(std::move(cell));
        }
        table.rows.push_back(std::move(row));
    }

    std::size_t widest = 0;
    for (const std::vector<std::string>& row : table.rows) {
        widest = (std::max)(widest, row.size());
    }
    for (std::vector<std::string>& row : table.rows) row.resize(widest);

    table.hasHeader = LooksLikeHeader(table.rows);
    return table;
}

}  // namespace csv
