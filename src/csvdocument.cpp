#include "csvdocument.h"

#include <algorithm>
#include <cstdint>

#include "documentfile.h"

namespace csvdoc {
namespace {

// UTF-8 to the UTF-32 the node list holds. A broken sequence becomes U+FFFD
// rather than vanishing, so a file with one bad byte still shows its shape.
std::u32string ToUtf32(const std::string& utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        uint32_t code = 0;
        std::size_t extra = 0;
        if (b < 0x80) {
            code = b;
        } else if ((b & 0xE0) == 0xC0) {
            code = b & 0x1Fu;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            code = b & 0x0Fu;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            code = b & 0x07u;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        std::size_t taken = 0;
        while (taken < extra && i + 1 + taken < utf8.size() &&
               (static_cast<unsigned char>(utf8[i + 1 + taken]) & 0xC0) == 0x80) {
            code = (code << 6) |
                   (static_cast<unsigned char>(utf8[i + 1 + taken]) & 0x3Fu);
            ++taken;
        }
        if (taken < extra) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(static_cast<char32_t>(code));
        i += 1 + extra;
    }
    return out;
}

// 20000 becomes "20,000", so the notice reads like a person wrote it.
std::string Thousands(std::size_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    int since = 0;
    for (std::size_t i = digits.size(); i > 0; --i) {
        out.push_back(digits[i - 1]);
        if (++since == 3 && i > 1) {
            out.push_back(',');
            since = 0;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

Node ParagraphOf(const std::string& utf8) {
    Node node;
    node.block = BlockKind::Paragraph;
    InlineBlock text;
    text.kind = InlineKind::Text;
    text.text = ToUtf32(utf8);
    node.children.push_back(std::move(text));
    return node;
}

}  // namespace

bool IsCsvPath(const std::string& pathUtf8) {
    return PathHasExtension(pathUtf8, ".csv") ||
           PathHasExtension(pathUtf8, ".tsv") ||
           PathHasExtension(pathUtf8, ".tab");
}

char DelimiterHintForPath(const std::string& pathUtf8) {
    if (PathHasExtension(pathUtf8, ".tsv") ||
        PathHasExtension(pathUtf8, ".tab")) {
        return '\t';
    }
    return 0;  // a .csv file is comma or semicolon, so it is sniffed
}

std::string NoticeText(const csv::Table& table) {
    if (table.unsupportedEncoding) {
        return "This file is not UTF-8. MarkDownIt cannot read a CSV file in "
               "that encoding yet; save it as UTF-8 to see the table.";
    }
    std::string text;
    if (table.rowsTruncated) {
        text += "Showing the first " + Thousands(csv::kMaxRows) + " of " +
                Thousands(table.sourceRows) + " rows.";
    }
    if (table.columnsTruncated) {
        if (!text.empty()) text += " ";
        text += "Showing the first " + Thousands(csv::kMaxColumns) +
                " columns of each row.";
    }
    if (table.cellsTruncated) {
        if (!text.empty()) text += " ";
        text += "Some cells are longer than one megabyte and are cut short.";
    }
    return text;
}

Document BuildDocument(const std::string& utf8Text,
                       const std::string& pathUtf8) {
    Document doc;
    const csv::Table table = csv::Parse(utf8Text, DelimiterHintForPath(pathUtf8));

    if (table.unsupportedEncoding) {
        doc.nodes.push_back(ParagraphOf(NoticeText(table)));
        return doc;
    }
    if (table.rows.empty()) {
        doc.nodes.push_back(ParagraphOf("This file holds no rows."));
        return doc;
    }

    Node node;
    node.block = BlockKind::Table;
    // A file of data is read in a fixed font, so a column of figures lines up
    // whatever the glyphs above it are. This is the one place it is asked for.
    node.tableMonospace = true;
    node.srcOffset = 0;
    node.srcLength = static_cast<uint32_t>(utf8Text.size());
    node.contentOffset = 0;
    node.contentLength = static_cast<uint32_t>(utf8Text.size());

    const std::size_t columns = table.rows[0].size();
    node.aligns.assign(columns, ColumnAlign::Left);

    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        TableRow row;
        const bool header = table.hasHeader && r == 0;
        row.cells.reserve(table.rows[r].size());
        for (const std::string& text : table.rows[r]) {
            TableCell cell;
            cell.text = ToUtf32(text);
            cell.isHeader = header;
            row.cells.push_back(std::move(cell));
        }
        node.rows.push_back(std::move(row));
    }

    // A column of numbers is read with its last digit in line, the way every
    // spreadsheet and data table aligns one: four rows in five is enough to
    // call it numeric, so a stray "n/a" does not turn the column back to text.
    for (std::size_t c = 0; c < columns; ++c) {
        std::size_t filled = 0;
        std::size_t numeric = 0;
        const std::size_t first = table.hasHeader ? 1 : 0;
        for (std::size_t r = first; r < table.rows.size(); ++r) {
            const std::string& cell = table.rows[r][c];
            if (cell.empty()) continue;
            ++filled;
            if (csv::LooksNumeric(cell)) ++numeric;
        }
        if (filled > 0 && numeric * 5 >= filled * 4) {
            node.aligns[c] = ColumnAlign::Right;
        }
    }

    doc.nodes.push_back(std::move(node));

    const std::string notice = NoticeText(table);
    if (!notice.empty()) doc.nodes.push_back(ParagraphOf(notice));
    return doc;
}

}  // namespace csvdoc
