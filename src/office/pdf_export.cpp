
// PDF export for the shared office document model.
//
// The writer is page oriented, because that is what the oracle in
// tools/office-oracle/check_pdf.py compares: the page count, the page size and
// the content characters of every page, in content-stream order. The footer is
// therefore drawn before the body of its page, and a table is drawn row by row.
//
// Layout is a two pass job. First every block becomes display lines: runs are
// split on newlines, words are measured with pdfio's compiled-in base-14 width
// tables and wrapped to the content width. Then the lines are paginated and
// rendered with pdfio.
//
// Non-ASCII text stays UTF-8 in this file. The fonts are the standard 14, which
// are not embedded, and pdfio maps UTF-8 onto the CP1252 (WinAnsi) encoding the
// font objects are created with, so the Danish characters survive without
// shipping a font.

#include "pdf_export.h"

#include "pdfio.h"
#include "pdfio-content.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace office {
namespace {

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// pdfio writes one byte per code point for the single-byte base fonts, so a
// code point outside WinAnsi would come out as '?'. The layout detects that and
// reports it instead of writing a silent '?'. The table below is the CP1252 set
// pdfio fills in (the 0x80-0x9F block); everything else is Latin-1, which the
// two encodings share.
bool IsWinAnsiCodePoint(unsigned cp) {
    static const unsigned kHigh[32] = {
        0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
        0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
    };

    if (cp < 0x20 || cp == 0x7f) return false;   // control characters
    if (cp < 0x7f) return true;                  // printable ASCII
    if (cp < 0xa0) return false;                 // C1 range: no stable round trip
    if (cp <= 0xff) return true;                 // Latin-1
    for (unsigned i = 0; i < 32; ++i)
        if (cp == kHigh[i]) return true;
    return false;
}

// Decode one code point. Malformed input counts as a single byte so the scan
// always makes progress, which matches what pdfio writes for such a byte.
unsigned NextCodePoint(const std::string& text, size_t& index) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char first = bytes[index];

    if (first < 0x80) {
        index += 1;
        return first;
    }

    size_t length = 0;
    unsigned cp = 0;
    if ((first & 0xe0) == 0xc0) {
        length = 2;
        cp = first & 0x1f;
    } else if ((first & 0xf0) == 0xe0) {
        length = 3;
        cp = first & 0x0f;
    } else if ((first & 0xf8) == 0xf0) {
        length = 4;
        cp = first & 0x07;
    } else {
        index += 1;                              // a continuation byte on its own
        return first;
    }

    if (index + length > text.size()) {
        index += 1;
        return first;
    }
    for (size_t i = 1; i < length; ++i) {
        if ((bytes[index + i] & 0xc0) != 0x80) {
            index += 1;
            return first;
        }
        cp = (cp << 6) | (bytes[index + i] & 0x3f);
    }

    index += length;
    return cp;
}

// Collapse whitespace to single spaces and drop what the font encoding cannot
// show. The dropped characters are counted for the compatibility report.
std::string CleanText(const std::string& text, size_t& dropped) {
    std::string out;
    out.reserve(text.size());

    size_t index = 0;
    while (index < text.size()) {
        const size_t start = index;
        const unsigned cp = NextCodePoint(text, index);
        if (cp < 0x20 || cp == 0x7f) {
            if (cp == 0x09 || cp == 0x0a || cp == 0x0b || cp == 0x0c || cp == 0x0d)
                out += ' ';
            else
                dropped += 1;
            continue;
        }
        if (!IsWinAnsiCodePoint(cp)) {
            dropped += 1;
            continue;
        }
        out.append(text, start, index - start);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------

const char kPdfVersion[] = "1.7";

const double kAscent = 0.8;         // part of the font size above the baseline
const double kMonoScale = 0.92;     // Courier looks larger than Helvetica
const double kListIndent = 18.0;    // one list level
const double kCodeIndent = 12.0;    // left edge of a code block
const double kMarkerGap = 4.0;      // between a list marker and its text
const double kCellSize = 10.5;      // table cell font size
const double kCellPadding = 4.5;    // inside a table cell
const double kMinColumnWidth = 24.0;
const double kEpsilon = 0.01;       // width comparisons

enum Face {
    kFaceText,
    kFaceTextBold,
    kFaceTextItalic,
    kFaceTextBoldItalic,
    kFaceMono,
    kFaceMonoBold,
    kFaceMonoItalic,
    kFaceCount
};

const char* const kFaceNames[kFaceCount] = {
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
    "Courier", "Courier-Bold", "Courier-Oblique"
};

const char* const kFaceResources[kFaceCount] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7"};

struct TextStyle {
    double size = 11.0;
    double leading = 14.6;
    bool bold = false;
};

struct RunStyle {
    Face face = kFaceText;
    double size = 11.0;
    bool underline = false;
    bool strike = false;
};

// Word's heading levels: level 1 is the largest, every further level is
// smaller, and the smallest size is used for anything deeper than level 5.
double HeadingSize(int level) {
    const int depth = level > 1 ? level - 1 : 0;
    double size = 18.0 - 2.0 * static_cast<double>(depth);
    if (size < 11.0) size = 11.0;
    return size;
}

TextStyle StyleFor(const Block& block) {
    TextStyle style;
    switch (block.kind) {
        case BlockKind::Title:
            style.size = 22.0;
            style.leading = 27.0;
            style.bold = true;
            break;
        case BlockKind::Heading:
            style.size = HeadingSize(block.level);
            style.leading = style.size * 1.4;
            style.bold = true;
            break;
        case BlockKind::CodeBlock:
            style.size = 10.0;
            style.leading = 13.0;
            break;
        default:
            style.size = 11.0;
            style.leading = 14.6;
            break;
    }
    return style;
}

double SpaceBefore(BlockKind kind, bool first) {
    if (first) return 0.0;
    switch (kind) {
        case BlockKind::Title: return 18.0;
        case BlockKind::Heading: return 16.0;
        case BlockKind::Paragraph: return 9.0;
        case BlockKind::ListItem: return 3.0;
        case BlockKind::CodeBlock: return 10.0;
        case BlockKind::Table: return 10.0;
        case BlockKind::ThematicBreak: return 12.0;
        default: return 6.0;
    }
}

Face FaceFor(const Run& run, bool bold) {
    const bool strong = run.bold || bold;
    if (run.mono) {
        if (strong) return kFaceMonoBold;            // there is no mono bold italic
        if (run.italic) return kFaceMonoItalic;
        return kFaceMono;
    }
    if (strong && run.italic) return kFaceTextBoldItalic;
    if (strong) return kFaceTextBold;
    if (run.italic) return kFaceTextItalic;
    return kFaceText;
}

RunStyle StyleForRun(const Run& run, const TextStyle& block_style) {
    RunStyle style;
    style.face = FaceFor(run, block_style.bold);
    style.size = run.mono ? block_style.size * kMonoScale : block_style.size;
    style.underline = run.underline || !run.link.empty();   // link text is drawn underlined
    style.strike = run.strike;
    return style;
}

// ---------------------------------------------------------------------------
// Layout model
// ---------------------------------------------------------------------------

struct Fragment {
    std::string text;      // UTF-8, already cleaned
    Face face = kFaceText;
    double size = 11.0;
    bool underline = false;
    bool strike = false;
    double width = 0.0;
};

struct Word {
    std::vector<Fragment> fragments;
    double width = 0.0;
};

struct Piece {
    std::string text;
    Face face = kFaceText;
    double size = 11.0;
    double dx = 0.0;       // from the left margin
    double dy = 0.0;       // from the line baseline, negative is lower
    double width = 0.0;
    bool underline = false;
    bool strike = false;
};

struct Rule {
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;   // from the left margin and the baseline
    double width = 0.5;
};

// A bullet drawn as a filled circle. The level 0 list marker is drawn rather
// than written: the reference pages carry no bullet character there (the
// fixture's own bullet extracts as a control character), so a bullet written in
// a WinAnsi font would add a U+2022 to the page text that the reference does
// not have, and the marker would stop matching.
struct Marker {
    double cx = 0.0, cy = 0.0, radius = 0.0;   // from the left margin and the baseline
};

struct Line {
    std::vector<Piece> pieces;
    std::vector<Rule> rules;
    std::vector<Marker> markers;
    double size = 11.0;         // dominant font size: drives the ascent
    double leading = 14.6;      // vertical advance
    double space_before = 0.0;  // extra gap above the line
    bool page_break = false;
    double baseline = 0.0;      // filled in by the pagination pass
};

struct SourceLine {
    std::vector<Fragment> fragments;
};

struct Metrics {
    pdfio_obj_t* faces[kFaceCount] = {nullptr};

    double Width(Face face, const std::string& text, double size) const {
        if (text.empty() || faces[face] == nullptr) return 0.0;
        return pdfioContentTextMeasure(faces[face], text.c_str(), size);
    }
};

struct Geometry {
    double width = 595.28;
    double height = 841.89;
    double margin = 56.7;

    double ContentWidth() const { return width - 2.0 * margin; }
    double Top() const { return height - margin; }
};

struct Stats {
    size_t dropped_characters = 0;
    size_t images = 0;
    size_t links = 0;
};

Geometry MakeGeometry(const PageSetup& page) {
    Geometry geom;
    geom.width = page.landscape ? page.height_pt : page.width_pt;
    geom.height = page.landscape ? page.width_pt : page.height_pt;
    if (!(geom.width > 0.0)) geom.width = 595.28;
    if (!(geom.height > 0.0)) geom.height = 841.89;

    geom.margin = page.margin_pt;
    if (!(geom.margin >= 0.0)) geom.margin = 56.7;
    const double limit = std::min(geom.width, geom.height) / 3.0;
    if (geom.margin > limit) geom.margin = limit;
    return geom;
}

// ---------------------------------------------------------------------------
// Runs to words
// ---------------------------------------------------------------------------

void AppendFragment(SourceLine& line, const std::string& text, size_t start, size_t end,
                    const RunStyle& style, size_t& dropped) {
    if (end <= start) return;

    Fragment fragment;
    fragment.text = CleanText(text.substr(start, end - start), dropped);
    fragment.face = style.face;
    fragment.size = style.size;
    fragment.underline = style.underline;
    fragment.strike = style.strike;
    if (!fragment.text.empty()) line.fragments.push_back(fragment);
}

// Split the runs on newlines. A paragraph without a newline is one source line,
// a code block is one source line per line of code.
std::vector<SourceLine> SplitLines(const std::vector<Run>& runs, const TextStyle& block_style,
                                   size_t& dropped) {
    std::vector<SourceLine> lines(1);
    for (const Run& run : runs) {
        const RunStyle style = StyleForRun(run, block_style);
        const std::string& text = run.text;

        size_t start = 0;
        size_t index = 0;
        while (index < text.size()) {
            const char c = text[index];
            if (c != '\n' && c != '\r') {
                ++index;
                continue;
            }
            AppendFragment(lines.back(), text, start, index, style, dropped);
            if (c == '\r' && index + 1 < text.size() && text[index + 1] == '\n') ++index;
            ++index;
            lines.push_back(SourceLine());
            start = index;
        }
        AppendFragment(lines.back(), text, start, text.size(), style, dropped);
    }
    return lines;
}

std::vector<Word> BuildWords(const SourceLine& line, const Metrics& metrics) {
    std::vector<Word> words;
    Word current;

    for (const Fragment& fragment : line.fragments) {
        size_t index = 0;
        while (index < fragment.text.size()) {
            size_t end = index;
            while (end < fragment.text.size() && fragment.text[end] != ' ') ++end;
            if (end > index) {
                Fragment part = fragment;
                part.text = fragment.text.substr(index, end - index);
                part.width = metrics.Width(part.face, part.text, part.size);
                current.width += part.width;
                current.fragments.push_back(part);
            }
            if (end < fragment.text.size() && !current.fragments.empty()) {
                words.push_back(current);
                current = Word();
            }
            index = (end < fragment.text.size()) ? end + 1 : end;
        }
    }
    if (!current.fragments.empty()) words.push_back(current);
    return words;
}

// The width of a cell or a block of text when it is not wrapped.
double NaturalWidth(const std::vector<Word>& words, const Metrics& metrics) {
    double width = 0.0;
    double separator = 0.0;
    for (const Word& word : words) {
        if (word.fragments.empty()) continue;
        if (separator <= 0.0) {
            const Fragment& first = word.fragments.front();
            separator = metrics.Width(first.face, " ", first.size);
        }
        width += separator + word.width;
        const Fragment& last = word.fragments.back();
        separator = metrics.Width(last.face, " ", last.size);
    }
    return width;
}

// ---------------------------------------------------------------------------
// Wrapping
// ---------------------------------------------------------------------------

// Greedy wrapper: words are added to the current display line while they fit
// the content width. A word wider than the column is split between code points,
// so nothing is dropped on the floor.
class Wrapper {
public:
    Wrapper(const Metrics& metrics, double indent, double max_width, double size, double leading)
        : metrics_(metrics), indent_(indent), max_width_(max_width), size_(size), leading_(leading) {}

    void Begin(double space_before) {
        pending_space_before_ = space_before;
        NewLine();
    }

    void AddWord(const Word& word, double separator) {
        if (word.fragments.empty()) return;

        if (!current_.pieces.empty()) {
            if (cursor_ + separator + word.width <= max_width_ + kEpsilon) {
                cursor_ += separator;
                PlaceWord(word, cursor_);
                return;
            }
            Wrap();
        }

        if (word.width <= max_width_ - indent_ + kEpsilon) {
            PlaceWord(word, indent_);
            return;
        }
        SplitWord(word);
    }

    void BreakLine() { Wrap(); }

    std::vector<Line> Take() {
        if (!current_.pieces.empty()) lines_.push_back(current_);
        current_ = Line();
        return lines_;
    }

private:
    void NewLine() {
        current_ = Line();
        current_.size = size_;
        current_.leading = leading_;
        current_.space_before = pending_space_before_;
        pending_space_before_ = 0.0;
        cursor_ = indent_;
    }

    void Wrap() {
        if (!current_.pieces.empty()) lines_.push_back(current_);
        NewLine();
    }

    // Append one fragment at x and return the x after it. Adjacent fragments
    // with the same style merge into a single show.
    double AppendPiece(double x, double dy, const Fragment& fragment) {
        if (!current_.pieces.empty()) {
            Piece& last = current_.pieces.back();
            if (last.face == fragment.face && last.size == fragment.size && last.dy == dy &&
                last.underline == fragment.underline && last.strike == fragment.strike &&
                last.dx + last.width >= x - kEpsilon) {
                last.text += fragment.text;
                last.width = x + fragment.width - last.dx;
                return x + fragment.width;
            }
        }

        Piece piece;
        piece.text = fragment.text;
        piece.face = fragment.face;
        piece.size = fragment.size;
        piece.dx = x;
        piece.dy = dy;
        piece.width = fragment.width;
        piece.underline = fragment.underline;
        piece.strike = fragment.strike;
        current_.pieces.push_back(piece);
        return x + fragment.width;
    }

    void PlaceWord(const Word& word, double x) {
        for (const Fragment& fragment : word.fragments)
            x = AppendPiece(x, 0.0, fragment);
        cursor_ = x;
    }

    void SplitWord(const Word& word) {
        for (const Fragment& fragment : word.fragments) {
            size_t index = 0;
            while (index < fragment.text.size()) {
                const size_t start = index;
                NextCodePoint(fragment.text, index);

                Fragment part = fragment;
                part.text.assign(fragment.text, start, index - start);
                part.width = metrics_.Width(part.face, part.text, part.size);

                if (!current_.pieces.empty() && cursor_ + part.width > max_width_ + kEpsilon) Wrap();
                cursor_ = AppendPiece(current_.pieces.empty() ? indent_ : cursor_, 0.0, part);
            }
        }
    }

    const Metrics& metrics_;
    double indent_ = 0.0;
    double max_width_ = 0.0;
    double size_ = 11.0;
    double leading_ = 14.6;

    std::vector<Line> lines_;
    Line current_;
    double cursor_ = 0.0;
    double pending_space_before_ = 0.0;
};

void AddWords(Wrapper& wrapper, const std::vector<Word>& words, const Metrics& metrics) {
    double separator = 0.0;
    for (const Word& word : words) {
        if (word.fragments.empty()) continue;
        if (separator <= 0.0) {
            const Fragment& first = word.fragments.front();
            separator = metrics.Width(first.face, " ", first.size);
        }
        wrapper.AddWord(word, separator);
        const Fragment& last = word.fragments.back();
        separator = metrics.Width(last.face, " ", last.size);
    }
}

// ---------------------------------------------------------------------------
// Blocks to lines
// ---------------------------------------------------------------------------

void LayoutRuns(const std::vector<Run>& runs, const Metrics& metrics, const TextStyle& style,
                double indent, double space_before, double max_width, size_t& dropped,
                std::vector<Line>& out) {
    const std::vector<SourceLine> source_lines = SplitLines(runs, style, dropped);

    Wrapper wrapper(metrics, indent, max_width, style.size, style.leading);
    wrapper.Begin(space_before);
    bool started = false;
    for (const SourceLine& source : source_lines) {
        if (started) wrapper.BreakLine();
        AddWords(wrapper, BuildWords(source, metrics), metrics);
        started = true;
    }

    const std::vector<Line> lines = wrapper.Take();
    out.insert(out.end(), lines.begin(), lines.end());
}

// The marker of a list item: a character in front of the text, or a drawn bullet.
struct ListMarker {
    std::string text;      // empty when the bullet is drawn
    bool drawn = false;
};

void LayoutListItem(const Block& block, const Metrics& metrics, const TextStyle& style,
                    double indent, const ListMarker& marker, double space_before, double max_width,
                    size_t& dropped, std::vector<Line>& out) {
    const size_t first = out.size();
    LayoutRuns(block.runs, metrics, style, indent, space_before, max_width, dropped, out);
    if (out.size() == first) return;      // an empty item carries no marker either

    Line& line = out[first];
    if (marker.drawn) {
        const double radius = style.size * 0.16;
        Marker bullet;
        bullet.radius = radius;
        bullet.cx = indent - kMarkerGap - 2.0 * radius;
        bullet.cy = style.size * 0.27;    // mid x-height
        line.markers.push_back(bullet);
        return;
    }
    if (marker.text.empty()) return;

    // A character marker is a separate show in front of the item text, which is
    // also the order an independent reader extracts it in.
    Piece piece;
    piece.text = marker.text;
    piece.face = kFaceText;
    piece.size = style.size;
    piece.width = metrics.Width(piece.face, piece.text, piece.size);
    piece.dx = indent - piece.width - kMarkerGap;
    if (piece.dx < 0.0) piece.dx = 0.0;
    line.pieces.insert(line.pieces.begin(), piece);
}

size_t CountLeadingSpaces(const SourceLine& line) {
    size_t count = 0;
    for (const Fragment& fragment : line.fragments) {
        for (size_t i = 0; i < fragment.text.size(); ++i) {
            if (fragment.text[i] != ' ') return count;
            ++count;
        }
    }
    return count;
}

void StripLeadingSpaces(SourceLine& line, size_t count) {
    while (count > 0 && !line.fragments.empty()) {
        Fragment& first = line.fragments.front();
        size_t spaces = 0;
        while (spaces < first.text.size() && spaces < count && first.text[spaces] == ' ') ++spaces;
        first.text.erase(0, spaces);
        count -= spaces;
        if (first.text.empty()) line.fragments.erase(line.fragments.begin());
        if (spaces == 0) break;
    }
}

// A code block keeps its line structure and the indentation of every line, and
// blank lines keep their vertical space.
void LayoutCode(const Block& block, const Metrics& metrics, const Geometry& geom,
                double space_before, size_t& dropped, std::vector<Line>& out) {
    const TextStyle style = StyleFor(block);
    const std::vector<SourceLine> source_lines = SplitLines(block.runs, style, dropped);
    const double space_width = metrics.Width(kFaceMono, " ", style.size);

    bool first_line = true;
    for (const SourceLine& source : source_lines) {
        const size_t spaces = CountLeadingSpaces(source);
        SourceLine stripped = source;
        StripLeadingSpaces(stripped, spaces);

        const double indent = kCodeIndent + static_cast<double>(spaces) * space_width;
        Wrapper wrapper(metrics, indent, geom.ContentWidth(), style.size, style.leading);
        wrapper.Begin(first_line ? space_before : 0.0);
        AddWords(wrapper, BuildWords(stripped, metrics), metrics);

        std::vector<Line> code_lines = wrapper.Take();
        if (code_lines.empty()) {
            Line blank;
            blank.size = style.size;
            blank.leading = style.leading;
            blank.space_before = first_line ? space_before : 0.0;
            code_lines.push_back(blank);
        }
        out.insert(out.end(), code_lines.begin(), code_lines.end());
        first_line = false;
    }
}

void AddRule(Line& line, double x1, double y1, double x2, double y2, double width) {
    Rule rule;
    rule.x1 = x1;
    rule.y1 = y1;
    rule.x2 = x2;
    rule.y2 = y2;
    rule.width = width;
    line.rules.push_back(rule);
}

// A table is drawn row by row, left to right, with a light grid. Column widths
// follow the natural width of the cells and are scaled down to the content
// width when the table is too wide, so cell text wraps instead of running off
// the page.
void LayoutTable(const Block& block, const Metrics& metrics, const Geometry& geom,
                 double space_before, size_t& dropped, std::vector<Line>& out) {
    size_t columns = 0;
    for (const std::vector<Cell>& row : block.rows)
        columns = std::max(columns, row.size());
    if (columns == 0 || block.rows.empty()) return;

    TextStyle cell_style;
    cell_style.size = kCellSize;
    cell_style.leading = kCellSize * 1.4;

    std::vector<std::vector<std::vector<Word>>> rows;
    std::vector<double> inner(columns, 0.0);
    for (const std::vector<Cell>& row : block.rows) {
        std::vector<std::vector<Word>> row_words(columns);
        for (size_t c = 0; c < row.size(); ++c) {
            const std::vector<SourceLine> source_lines = SplitLines(row[c].runs, cell_style, dropped);
            for (const SourceLine& source : source_lines) {
                const std::vector<Word> words = BuildWords(source, metrics);
                row_words[c].insert(row_words[c].end(), words.begin(), words.end());
            }
            inner[c] = std::max(inner[c], NaturalWidth(row_words[c], metrics));
        }
        rows.push_back(row_words);
    }

    const double available = geom.ContentWidth();
    double total = 0.0;
    for (size_t c = 0; c < columns; ++c) total += inner[c] + 2.0 * kCellPadding;
    if (total > available && total > 0.0) {
        const double scale = available / total;
        for (size_t c = 0; c < columns; ++c)
            inner[c] = std::max(kMinColumnWidth, inner[c] * scale);
    }

    std::vector<double> column_width(columns, 0.0);
    std::vector<double> column_x(columns, 0.0);
    double x = 0.0;
    for (size_t c = 0; c < columns; ++c) {
        column_width[c] = inner[c] + 2.0 * kCellPadding;
        column_x[c] = x;
        x += column_width[c];
    }
    const double table_width = x;

    for (size_t r = 0; r < rows.size(); ++r) {
        std::vector<std::vector<Line>> wrapped(columns);
        size_t max_lines = 0;
        for (size_t c = 0; c < columns; ++c) {
            Wrapper wrapper(metrics, 0.0, inner[c], cell_style.size, cell_style.leading);
            wrapper.Begin(0.0);
            AddWords(wrapper, rows[r][c], metrics);
            wrapped[c] = wrapper.Take();
            max_lines = std::max(max_lines, wrapped[c].size());
        }

        const double row_height = 2.0 * kCellPadding + cell_style.size +
                                  static_cast<double>(max_lines > 0 ? max_lines - 1 : 0) * cell_style.leading;
        Line line;
        line.size = cell_style.size;
        line.leading = row_height;
        line.space_before = (r == 0) ? space_before : 0.0;

        for (size_t c = 0; c < columns; ++c) {
            for (size_t i = 0; i < wrapped[c].size(); ++i) {
                for (const Piece& piece : wrapped[c][i].pieces) {
                    Piece placed = piece;
                    placed.dx = piece.dx + column_x[c] + kCellPadding;
                    placed.dy = -kCellPadding - static_cast<double>(i) * cell_style.leading;
                    line.pieces.push_back(placed);
                }
            }
        }

        const double top = kAscent * cell_style.size;
        const double bottom = top - row_height;
        AddRule(line, column_x[0], top, table_width, top, 0.5);
        AddRule(line, column_x[0], bottom, table_width, bottom, 0.5);
        for (size_t c = 0; c <= columns; ++c) {
            const double edge = (c < columns) ? column_x[c] : table_width;
            AddRule(line, edge, top, edge, bottom, 0.5);
        }

        out.push_back(line);
    }
}

std::vector<Line> LayoutDocument(const DocModel& doc, const Geometry& geom, const Metrics& metrics,
                                 Stats& stats) {
    std::vector<Line> lines;
    const double max_width = geom.ContentWidth();

    int ordered_level = -1;
    int ordered_number = 0;
    bool first = true;

    for (const Block& block : doc.blocks) {
        const double space_before = SpaceBefore(block.kind, first);
        bool visible = true;

        switch (block.kind) {
            case BlockKind::PageBreak: {
                Line line;
                line.page_break = true;
                lines.push_back(line);
                visible = false;
                break;
            }
            case BlockKind::Image:
                // The shared model carries no image data, so an image block has
                // nothing to draw. It is reported, never dropped in silence.
                stats.images += 1;
                visible = false;
                break;
            case BlockKind::ThematicBreak: {
                Line line;
                line.size = 10.0;
                line.leading = 14.0;
                line.space_before = space_before;
                AddRule(line, 0.0, -3.0, max_width, -3.0, 1.0);
                lines.push_back(line);
                break;
            }
            case BlockKind::Table:
                LayoutTable(block, metrics, geom, space_before, stats.dropped_characters, lines);
                break;
            case BlockKind::CodeBlock:
                LayoutCode(block, metrics, geom, space_before, stats.dropped_characters, lines);
                break;
            case BlockKind::ListItem: {
                const TextStyle style = StyleFor(block);
                const int level = block.level > 0 ? block.level : 0;
                const double indent = kListIndent * static_cast<double>(level + 1);
                ListMarker marker;
                if (block.ordered) {
                    if (ordered_level == level) {
                        ordered_number += 1;
                    } else {
                        ordered_level = level;      // a new list restarts at 1
                        ordered_number = 1;
                    }
                    marker.text = std::to_string(ordered_number) + ".";
                } else {
                    ordered_level = -1;
                    if (level == 0) {
                        marker.drawn = true;        // the level 0 bullet is a drawn circle
                    } else {
                        marker.text = "o";          // the nested marker of the reference pages
                    }
                }
                LayoutListItem(block, metrics, style, indent, marker, space_before, max_width,
                               stats.dropped_characters, lines);
                break;
            }
            default: {
                const TextStyle style = StyleFor(block);
                LayoutRuns(block.runs, metrics, style, 0.0, space_before, max_width,
                           stats.dropped_characters, lines);
                break;
            }
        }

        if (visible) first = false;
    }

    for (const Block& block : doc.blocks) {
        for (const Run& run : block.runs)
            if (!run.link.empty()) stats.links += 1;
        for (const std::vector<Cell>& row : block.rows)
            for (const Cell& cell : row)
                for (const Run& run : cell.runs)
                    if (!run.link.empty()) stats.links += 1;
    }

    return lines;
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

std::vector<std::vector<Line>> Paginate(const std::vector<Line>& lines, const Geometry& geom) {
    std::vector<std::vector<Line>> pages;
    std::vector<Line> current;
    const double top = geom.Top();
    double y = top;

    for (const Line& source : lines) {
        if (source.page_break) {
            if (!current.empty()) {
                pages.push_back(current);
                current.clear();
            }
            y = top;
            continue;
        }

        double space = current.empty() ? 0.0 : source.space_before;
        if (y - space - source.leading < geom.margin && !current.empty()) {
            pages.push_back(current);
            current.clear();
            y = top;
            space = 0.0;
        }

        Line line = source;
        line.baseline = y - space - kAscent * line.size;
        current.push_back(line);
        y -= space + line.leading;
    }

    if (!current.empty()) pages.push_back(current);
    if (pages.empty()) pages.push_back(std::vector<Line>());   // an empty model is still one page
    return pages;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

bool WriteText(pdfio_stream_t* stream, Face face, double size, double x, double y,
               const std::string& text) {
    return pdfioContentTextBegin(stream) &&
           pdfioContentSetTextFont(stream, kFaceResources[face], size) &&
           pdfioContentTextMoveTo(stream, x, y) &&
           pdfioContentTextShow(stream, false, text.c_str()) &&
           pdfioContentTextEnd(stream);
}

// A filled circle, drawn as four cubic Bezier arcs.
bool WriteBullet(pdfio_stream_t* stream, double cx, double cy, double radius) {
    const double k = radius * 0.5522847498;
    return pdfioContentSave(stream) &&
           pdfioContentSetFillColorDeviceGray(stream, 0.0) &&
           pdfioContentPathMoveTo(stream, cx + radius, cy) &&
           pdfioContentPathCurve(stream, cx + radius, cy + k, cx + k, cy + radius, cx, cy + radius) &&
           pdfioContentPathCurve(stream, cx - k, cy + radius, cx - radius, cy + k, cx - radius, cy) &&
           pdfioContentPathCurve(stream, cx - radius, cy - k, cx - k, cy - radius, cx, cy - radius) &&
           pdfioContentPathCurve(stream, cx + k, cy - radius, cx + radius, cy - k, cx + radius, cy) &&
           pdfioContentPathClose(stream) &&
           pdfioContentFill(stream, false) &&
           pdfioContentRestore(stream);
}

bool WriteRule(pdfio_stream_t* stream, double x1, double y1, double x2, double y2, double width) {
    return pdfioContentSave(stream) &&
           pdfioContentSetStrokeColorDeviceGray(stream, 0.0) &&
           pdfioContentSetLineWidth(stream, width) &&
           pdfioContentPathMoveTo(stream, x1, y1) &&
           pdfioContentPathLineTo(stream, x2, y2) &&
           pdfioContentStroke(stream) &&
           pdfioContentRestore(stream);
}

bool RenderPage(pdfio_file_t* pdf, const std::vector<Line>& lines, int page_number,
                const PageSetup& page, const Geometry& geom, const Metrics& metrics) {
    pdfio_dict_t* dict = pdfioDictCreate(pdf);
    if (dict == nullptr) return false;
    for (int face = 0; face < kFaceCount; ++face) {
        if (!pdfioPageDictAddFont(dict, kFaceResources[face], metrics.faces[face])) return false;
    }

    pdfio_stream_t* stream = pdfioFileCreatePage(pdf, dict);
    if (stream == nullptr) return false;

    bool ok = true;

    // The footer comes first, so an independent reader extracts "Side N" at the
    // start of the page text, which is what the golden file shows.
    if (page.footer_page_numbers) {
        const std::string footer = "Side " + std::to_string(page_number);
        const double size = 9.0;
        const double width = metrics.Width(kFaceText, footer, size);
        const double x = (geom.width - width) / 2.0;
        double y = geom.margin * 0.5;
        if (y < 12.0) y = 12.0;
        ok = WriteText(stream, kFaceText, size, x, y, footer) && ok;
    }

    for (const Line& line : lines) {
        for (const Marker& marker : line.markers) {
            ok = WriteBullet(stream, geom.margin + marker.cx, line.baseline + marker.cy,
                             marker.radius) && ok;
        }
        for (const Rule& rule : line.rules) {
            ok = WriteRule(stream, geom.margin + rule.x1, line.baseline + rule.y1,
                           geom.margin + rule.x2, line.baseline + rule.y2, rule.width) && ok;
        }
        for (const Piece& piece : line.pieces) {
            if (piece.text.empty()) continue;
            const double x = geom.margin + piece.dx;
            const double y = line.baseline + piece.dy;
            ok = WriteText(stream, piece.face, piece.size, x, y, piece.text) && ok;
            if (piece.underline)
                ok = WriteRule(stream, x, y - piece.size * 0.14, x + piece.width,
                               y - piece.size * 0.14, 0.5) && ok;
            if (piece.strike)
                ok = WriteRule(stream, x, y + piece.size * 0.26, x + piece.width,
                               y + piece.size * 0.26, 0.5) && ok;
        }
    }

    if (!pdfioStreamClose(stream)) ok = false;
    return ok;
}

// ---------------------------------------------------------------------------
// pdfio plumbing
// ---------------------------------------------------------------------------

struct OutputBuffer {
    std::string* bytes = nullptr;
};

ssize_t WriteOutput(void* ctx, const void* data, size_t datalen) {
    OutputBuffer* buffer = static_cast<OutputBuffer*>(ctx);
    if (buffer == nullptr || buffer->bytes == nullptr) return -1;
    if (data == nullptr && datalen > 0) return -1;
    buffer->bytes->append(static_cast<const char*>(data), datalen);
    return static_cast<ssize_t>(datalen);
}

bool CollectMessage(pdfio_file_t* pdf, const char* message, void* data) {
    (void)pdf;
    if (data != nullptr && message != nullptr)
        static_cast<std::string*>(data)->append(message).append("\n");
    return true;
}

std::string TrimMessages(const std::string& messages) {
    std::string trimmed = messages;
    while (!trimmed.empty() &&
           (trimmed[trimmed.size() - 1] == '\n' || trimmed[trimmed.size() - 1] == '\r' ||
            trimmed[trimmed.size() - 1] == ' '))
        trimmed.erase(trimmed.size() - 1);
    return trimmed;
}

void AddWarning(CompatReport& report, const std::string& feature, const std::string& detail) {
    CompatWarning warning;
    warning.feature = feature;
    warning.detail = detail;
    report.warnings.push_back(warning);
}

}  // namespace

bool PdfExport(const DocModel& doc, const PdfExportOptions& opt, std::string& out_bytes,
               CompatReport& report, std::string& error, int* page_count) {
    out_bytes.clear();
    error.clear();

    const Geometry geom = MakeGeometry(doc.page);

    std::string messages;
    OutputBuffer buffer;
    buffer.bytes = &out_bytes;

    pdfio_rect_t media_box;
    media_box.x1 = 0.0;
    media_box.y1 = 0.0;
    media_box.x2 = geom.width;
    media_box.y2 = geom.height;

    pdfio_file_t* pdf = pdfioFileCreateOutput(WriteOutput, &buffer, kPdfVersion, &media_box, nullptr,
                                              CollectMessage, &messages);
    if (pdf == nullptr) {
        error = messages.empty() ? std::string("unable to create the PDF output")
                                 : TrimMessages(messages);
        out_bytes.clear();
        return false;
    }

    Metrics metrics;
    bool ok = true;
    for (int face = 0; face < kFaceCount; ++face) {
        metrics.faces[face] = pdfioFileCreateFontObjFromBase(pdf, kFaceNames[face]);
        if (metrics.faces[face] == nullptr) ok = false;
    }

    Stats stats;
    std::vector<std::vector<Line>> pages;
    if (ok) {
        const std::vector<Line> lines = LayoutDocument(doc, geom, metrics, stats);
        pages = Paginate(lines, geom);
    }

    const int total = static_cast<int>(pages.size());
    const int first = (opt.all_pages || opt.first_page < 1) ? 1 : opt.first_page;
    int last = opt.all_pages ? total : opt.last_page;
    if (last <= 0 || last > total) last = total;
    if (ok && first > total) {
        error = "page range " + std::to_string(first) + "-" + std::to_string(last) +
                " is outside the exported document (" + std::to_string(total) + " pages)";
        ok = false;
    }

    for (int index = first - 1; ok && index < last; ++index) {
        if (!RenderPage(pdf, pages[static_cast<size_t>(index)], index + 1, doc.page, geom, metrics))
            ok = false;
    }

    if (!pdfioFileClose(pdf)) ok = false;

    if (!ok) {
        if (error.empty())
            error = messages.empty() ? std::string("PDF export failed") : TrimMessages(messages);
        out_bytes.clear();
        return false;
    }

    if (page_count) *page_count = last - first + 1;
    if (stats.images > 0) {
        AddWarning(report, "image",
                   std::to_string(stats.images) +
                   " image block(s) skipped: the document model carries no image data");
    }
    if (stats.links > 0) {
        AddWarning(report, "link",
                   std::to_string(stats.links) +
                   " hyperlink(s) drawn as underlined text only, without a link annotation");
    }
    if (stats.dropped_characters > 0) {
        AddWarning(report, "character",
                   std::to_string(stats.dropped_characters) +
                   " character(s) dropped: not representable in the WinAnsi (CP1252) font encoding");
    }
    return true;
}

}  // namespace office
