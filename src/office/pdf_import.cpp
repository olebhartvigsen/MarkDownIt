// PDF import: text-based PDFs become editable document models (office
// spec, PDF import). pdfio reads the file; the text comes out of the
// content streams, with the font resources and the ToUnicode CMaps of
// the page deciding how the string bytes turn into characters.
//
// The structure is a best effort on purpose: headings come from the
// relative text size, lists from their markers, paragraphs from the
// line flow, and hyperlinks from the link annotations. Everything the
// model cannot hold (fonts, tables, images, scanned pages without OCR)
// is reported in the compatibility report instead of being dropped
// silently.
#include "pdf_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "pdfio.h"

namespace office {
namespace {

// ------------------------------------------------------------- utilities

bool CollectMessage(pdfio_file_t* pdf, const char* message, void* data) {
    (void)pdf;
    if (data && message) static_cast<std::string*>(data)->append(message).append("\n");
    return true;
}

const char* PasswordFromData(void* data, const char* filename) {
    (void)filename;
    if (data == nullptr) return "";
    return static_cast<const std::string*>(data)->c_str();
}

void AppendWarning(CompatReport& report, const char* feature, const std::string& detail) {
    CompatWarning warning;
    warning.feature = feature;
    warning.detail = detail;
    report.warnings.push_back(warning);
}

std::string TrimMessages(const std::string& messages) {
    std::string trimmed = messages;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'))
        trimmed.erase(trimmed.size() - 1);
    return trimmed;
}

std::string LowerAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool ContainsAscii(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string CountText(size_t count, const std::string& singular, const std::string& plural) {
    std::string text = std::to_string(count) + " ";
    text += count == 1 ? singular : plural;
    return text;
}

void AppendUtf8(std::string& out, unsigned code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
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

size_t CountChars(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++count;
    return count;
}

// The export writes the WinAnsi (CP1252) encoding, so a byte without a
// ToUnicode entry maps through this table.
const unsigned kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039,
    0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

void AppendCp1252(std::string& out, unsigned byte) {
    if (byte >= 0x80 && byte <= 0x9F) {
        AppendUtf8(out, kCp1252High[byte - 0x80]);
    } else {
        AppendUtf8(out, byte);
    }
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ------------------------------------------------------------- ToUnicode

// The subset of CMap the importer needs: the bfchar and bfrange entries.
struct CMap {
    std::map<unsigned, std::string> map;

    bool empty() const { return map.empty(); }

    std::string Lookup(unsigned code) const {
        const auto found = map.find(code);
        return found == map.end() ? std::string() : found->second;
    }
};

// The next <hex> run in a CMap, as raw bytes.
bool NextHexBytes(const std::string& text, size_t& pos, std::vector<unsigned>& bytes) {
    while (pos < text.size() && text[pos] != '<' && text.compare(pos, 8, "endbfche") != 0 &&
           text.compare(pos, 9, "endbfrang") != 0 && text.compare(pos, 9, "endbfchar") != 0 &&
           text.compare(pos, 10, "endbfrange") != 0) {
        ++pos;
    }
    if (pos >= text.size() || text[pos] != '<') return false;
    const size_t end = text.find('>', pos);
    if (end == std::string::npos) {
        pos = text.size();
        return false;
    }
    bytes.clear();
    for (size_t i = pos + 1; i + 1 < end; i += 2) {
        const int high = HexDigit(text[i]);
        const int low = HexDigit(text[i + 1]);
        if (high < 0 || low < 0) continue;
        bytes.push_back(static_cast<unsigned>(high * 16 + low));
    }
    pos = end + 1;
    return !bytes.empty();
}

unsigned CodeOfBytes(const std::vector<unsigned>& bytes) {
    unsigned code = 0;
    for (unsigned b : bytes) code = (code << 8) | b;
    return code;
}

// UTF-16BE bytes to UTF-8, surrogate pairs included.
std::string Utf8OfBytes(const std::vector<unsigned>& bytes) {
    std::string out;
    for (size_t i = 0; i < bytes.size();) {
        if (i + 1 >= bytes.size()) {
            AppendUtf8(out, bytes[i]);
            break;
        }
        unsigned unit = (bytes[i] << 8) | bytes[i + 1];
        i += 2;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < bytes.size()) {
            const unsigned low = (bytes[i] << 8) | bytes[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        AppendUtf8(out, unit);
    }
    return out;
}

void ParseCmap(const std::string& text, CMap& cmap) {
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t begin = text.find("beginbf", pos);
        if (begin == std::string::npos) break;
        const bool is_range = text.compare(begin, 11, "beginbfrange") == 0;
        pos = begin + (is_range ? 11 : 10);

        std::vector<unsigned> source;
        while (NextHexBytes(text, pos, source)) {
            if (!is_range) {
                std::vector<unsigned> target;
                if (!NextHexBytes(text, pos, target)) break;
                cmap.map[CodeOfBytes(source)] = Utf8OfBytes(target);
                continue;
            }

            std::vector<unsigned> last;
            if (!NextHexBytes(text, pos, last)) break;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;

            const unsigned first_code = CodeOfBytes(source);
            const unsigned last_code = CodeOfBytes(last);
            if (pos < text.size() && text[pos] == '[') {
                ++pos;
                for (unsigned code = first_code; code <= last_code; ++code) {
                    std::vector<unsigned> target;
                    if (!NextHexBytes(text, pos, target)) break;
                    cmap.map[code] = Utf8OfBytes(target);
                }
                while (pos < text.size() && text[pos] != ']') ++pos;
                if (pos < text.size()) ++pos;
            } else {
                std::vector<unsigned> start;
                if (!NextHexBytes(text, pos, start)) break;
                // A range walks the last unit of its start value, which
                // is what the common one-name-per-code CMaps do.
                unsigned base = 0;
                if (start.size() >= 2)
                    base = (start[start.size() - 2] << 8) | start[start.size() - 1];
                else if (!start.empty())
                    base = start.back();
                for (unsigned code = first_code; code <= last_code; ++code) {
                    const unsigned value = base + (code - first_code);
                    if (value > 0xFFFF) break;
                    std::vector<unsigned> unit = {(value >> 8) & 0xFF, value & 0xFF};
                    cmap.map[code] = Utf8OfBytes(unit);
                }
            }
        }
    }
}

// -------------------------------------------------------------- font info

struct FontInfo {
    bool bold = false;
    bool italic = false;
    bool mono = false;
    bool two_byte = false;   // Type0 fonts (Identity-H and friends)
    bool has_cmap = false;
    CMap cmap;
};

std::string ReadStreamText(pdfio_obj_t* obj) {
    if (obj == nullptr) return std::string();
    pdfio_stream_t* stream = pdfioObjOpenStream(obj, true);
    if (stream == nullptr) return std::string();
    std::string data;
    char buffer[4096];
    auto count = pdfioStreamRead(stream, buffer, sizeof(buffer));
    while (count > 0) {
        data.append(buffer, static_cast<size_t>(count));
        count = pdfioStreamRead(stream, buffer, sizeof(buffer));
    }
    pdfioStreamClose(stream);
    return data;
}

pdfio_dict_t* FontDict(pdfio_dict_t* font_dict, const char* key) {
    pdfio_dict_t* dict = pdfioDictGetDict(font_dict, key);
    pdfio_obj_t* obj = pdfioDictGetObj(font_dict, key);
    if (dict == nullptr && obj != nullptr) dict = pdfioObjGetDict(obj);
    return dict;
}

void LoadFonts(pdfio_dict_t* page_dict, std::map<std::string, FontInfo>& fonts) {
    pdfio_dict_t* resources = pdfioDictGetDict(page_dict, "Resources");
    if (resources == nullptr) return;
    pdfio_dict_t* font_dict = pdfioDictGetDict(resources, "Font");
    if (font_dict == nullptr) return;

    for (size_t i = 0; i < pdfioDictGetNumPairs(font_dict); ++i) {
        const char* key = pdfioDictGetKey(font_dict, i);
        if (key == nullptr) continue;
        pdfio_dict_t* dict = FontDict(font_dict, key);

        FontInfo info;
        const char* base = dict != nullptr ? pdfioDictGetName(dict, "BaseFont") : nullptr;
        const char* subtype = dict != nullptr ? pdfioDictGetName(dict, "Subtype") : nullptr;
        const std::string name = LowerAscii(base != nullptr ? base : "");
        info.bold = ContainsAscii(name, "bold") || ContainsAscii(name, "black") ||
                    ContainsAscii(name, "semibold");
        info.italic = ContainsAscii(name, "italic") || ContainsAscii(name, "oblique");
        info.mono = ContainsAscii(name, "courier") || ContainsAscii(name, "mono") ||
                    ContainsAscii(name, "consol") || ContainsAscii(name, "typewriter");
        if (subtype != nullptr && ContainsAscii(LowerAscii(subtype), "type0")) info.two_byte = true;

        if (dict != nullptr) {
            pdfio_obj_t* to_unicode = pdfioDictGetObj(dict, "ToUnicode");
            if (to_unicode != nullptr) {
                const std::string cmap_text = ReadStreamText(to_unicode);
                if (!cmap_text.empty()) {
                    ParseCmap(cmap_text, info.cmap);
                    info.has_cmap = !info.cmap.empty();
                }
            }
        }
        fonts[key] = info;
    }
}

// The width of the drawn text decides where a word gap belongs; the
// exact widths live in the font, so half an em per character is the
// estimate the line joining uses.
double EstimatedWidth(const std::string& text, double size) {
    return static_cast<double>(CountChars(text)) * size * 0.5;
}

std::string MapCodes(const FontInfo& font, const std::string& bytes) {
    std::string out;
    if (font.two_byte) {
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
            const unsigned code =
                (static_cast<unsigned char>(bytes[i]) << 8) | static_cast<unsigned char>(bytes[i + 1]);
            if (font.has_cmap) out += font.cmap.Lookup(code);
        }
        return out;
    }
    for (char ch : bytes) {
        const unsigned code = static_cast<unsigned char>(ch);
        if (font.has_cmap) {
            const std::string mapped = font.cmap.Lookup(code);
            if (!mapped.empty()) {
                out += mapped;
                continue;
            }
        }
        AppendCp1252(out, code);
    }
    return out;
}

// ----------------------------------------------------------------- items

struct TextItem {
    double x = 0.0;
    double y = 0.0;
    double size = 12.0;
    std::string text;
    bool bold = false;
    bool italic = false;
    bool mono = false;
    std::string link;
};

struct LinkAnnot {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    std::string uri;
};

struct PageText {
    std::vector<TextItem> items;
    std::vector<LinkAnnot> links;
    bool has_rule_ops = false;
    size_t images = 0;
};

// A content stream operand, kept in the small stack the operators read.
struct Operand {
    enum Kind { Number, String, Name } kind = Number;
    double number = 0.0;
    std::string text;
};

// Decode a string token: pdfio hands back spans like (text or <hex>.
std::string StringTokenBytes(const std::string& token) {
    if (token.empty() || token[0] != '(') {
        if (token.size() >= 2 && token[0] == '<' && token[1] != '<') {
            std::string digits;
            for (size_t i = 1; i < token.size() && token[i] != '>'; ++i)
                if (std::isxdigit(static_cast<unsigned char>(token[i])))
                    digits.push_back(token[i]);
            std::string bytes;
            for (size_t i = 0; i + 1 < digits.size(); i += 2) {
                const int high = HexDigit(digits[i]);
                const int low = HexDigit(digits[i + 1]);
                if (high < 0 || low < 0) continue;
                bytes.push_back(static_cast<char>(high * 16 + low));
            }
            return bytes;
        }
        return token;
    }
    const size_t end = token.size() > 1 && token.back() == ')' ? token.size() - 1 : token.size();
    std::string bytes;
    for (size_t i = 1; i < end; ++i) {
        const char c = token[i];
        if (c != '\\' || i + 1 >= end) {
            bytes.push_back(c);
            continue;
        }
        const char next = token[++i];
        switch (next) {
            case 'n': bytes.push_back('\n'); break;
            case 'r': bytes.push_back('\r'); break;
            case 't': bytes.push_back('\t'); break;
            case 'b': bytes.push_back('\b'); break;
            case 'f': bytes.push_back('\f'); break;
            default:
                if (next >= '0' && next <= '7') {
                    unsigned value = static_cast<unsigned>(next - '0');
                    int taken = 1;
                    while (taken < 3 && i + 1 < end && token[i + 1] >= '0' && token[i + 1] <= '7') {
                        value = value * 8 + static_cast<unsigned>(token[++i] - '0');
                        ++taken;
                    }
                    bytes.push_back(static_cast<char>(value & 0xFF));
                } else {
                    bytes.push_back(next);
                }
                break;
        }
    }
    return bytes;
}

bool IsOperator(const std::string& token) {
    static const char* kOperators[] = {
        "BT", "ET", "Tf", "Td", "TD", "Tm", "T*",  "Tj", "TJ", "'",  "\"", "re", "Do", "BI",
        "EI", "q",  "Q",  "cm", "gs", "W",  "W*",  "n",  "f",  "f*", "B",  "B*", "b",  "b*",
        "S",  "s",  "d",  "g",  "G",  "rg", "RG",  "k",  "K",  "w",  "J",  "j",  "M",  "i",
        "cs", "CS", "sc", "SC", "scn", "SCN", "sh"};
    for (const char* name : kOperators)
        if (token == name) return true;
    return false;
}

void ParsePageContent(pdfio_obj_t* page, const std::map<std::string, FontInfo>& fonts,
                      PageText& out) {
    const size_t streams = pdfioPageGetNumStreams(page);
    for (size_t s = 0; s < streams; ++s) {
        pdfio_stream_t* stream = pdfioPageOpenStream(page, s, true);
        if (stream == nullptr) continue;

        std::vector<Operand> operands;
        FontInfo font;
        double font_size = 12.0;
        double line_x = 0.0;
        double line_y = 0.0;
        double leading = 12.0;

        auto show = [&](const std::string& bytes) {
            if (bytes.empty()) return;
            TextItem item;
            item.x = line_x;
            item.y = line_y;
            item.size = font_size;
            item.text = MapCodes(font, bytes);
            item.bold = font.bold;
            item.italic = font.italic;
            item.mono = font.mono;
            if (!item.text.empty()) out.items.push_back(item);
        };

        auto number_at = [&](size_t index, double fallback) {
            if (index < operands.size() && operands[index].kind == Operand::Number)
                return operands[index].number;
            return fallback;
        };

        char token[4096];
        while (pdfioStreamGetToken(stream, token, sizeof(token))) {
            const std::string value(token);
            if (!IsOperator(value)) {
                Operand operand;
                if (!value.empty() && value[0] == '/') {
                    operand.kind = Operand::Name;
                    operand.text = value.substr(1);
                } else if (!value.empty() && (value[0] == '(' || value[0] == '<')) {
                    operand.kind = Operand::String;
                    operand.text = value;
                } else {
                    char* end = nullptr;
                    const double number = std::strtod(value.c_str(), &end);
                    if (end != nullptr && *end == '\0' && !value.empty()) {
                        operand.kind = Operand::Number;
                        operand.number = number;
                    } else {
                        operands.clear();
                        continue;
                    }
                }
                if (operands.size() < 32) operands.push_back(operand);
                continue;
            }

            if (value == "BT") {
                line_x = 0.0;
                line_y = 0.0;
            } else if (value == "Tf") {
                for (const Operand& operand : operands) {
                    if (operand.kind == Operand::Name) {
                        const auto found = fonts.find(operand.text);
                        font = found == fonts.end() ? FontInfo() : found->second;
                    } else if (operand.kind == Operand::Number && operand.number > 0.0) {
                        font_size = operand.number;
                    }
                }
            } else if (value == "Td" || value == "TD") {
                line_x += number_at(operands.size() >= 2 ? operands.size() - 2 : 0, 0.0);
                const double step =
                    number_at(operands.size() >= 1 ? operands.size() - 1 : 0, 0.0);
                line_y += step;
                if (value == "TD") leading = -step;
            } else if (value == "Tm") {
                if (operands.size() >= 6) {
                    line_x = number_at(4, line_x);
                    line_y = number_at(5, line_y);
                }
            } else if (value == "T*") {
                line_y -= leading;
            } else if (value == "Tj") {
                for (const Operand& operand : operands)
                    if (operand.kind == Operand::String) show(StringTokenBytes(operand.text));
            } else if (value == "TJ") {
                std::string joined;
                for (const Operand& operand : operands) {
                    if (operand.kind == Operand::String) {
                        joined += StringTokenBytes(operand.text);
                    } else if (operand.kind == Operand::Number && operand.number <= -120.0) {
                        if (!joined.empty()) joined += ' ';
                    }
                }
                show(joined);
            } else if (value == "'" || value == "\"") {
                line_y -= leading;
                for (const Operand& operand : operands)
                    if (operand.kind == Operand::String) show(StringTokenBytes(operand.text));
            } else if (value == "re") {
                out.has_rule_ops = true;
            }
            operands.clear();
        }
        pdfioStreamClose(stream);
    }
}

void LoadLinks(pdfio_dict_t* page_dict, std::vector<LinkAnnot>& links) {
    pdfio_array_t* annots = pdfioDictGetArray(page_dict, "Annots");
    if (annots == nullptr) return;
    for (size_t i = 0; i < pdfioArrayGetSize(annots); ++i) {
        pdfio_dict_t* annot = pdfioArrayGetDict(annots, i);
        if (annot == nullptr) {
            pdfio_obj_t* obj = pdfioArrayGetObj(annots, i);
            if (obj != nullptr) annot = pdfioObjGetDict(obj);
        }
        if (annot == nullptr) continue;
        const char* subtype = pdfioDictGetName(annot, "Subtype");
        if (subtype == nullptr || std::string(subtype) != "Link") continue;

        const char* uri = pdfioDictGetString(annot, "URI");
        pdfio_dict_t* action = pdfioDictGetDict(annot, "A");
        if (uri == nullptr && action != nullptr) uri = pdfioDictGetString(action, "URI");
        if (uri == nullptr || *uri == '\0') continue;

        pdfio_rect_t rect;
        if (pdfioDictGetRect(annot, "Rect", &rect) == nullptr) continue;

        LinkAnnot link;
        link.x1 = rect.x1;
        link.y1 = rect.y1;
        link.x2 = rect.x2;
        link.y2 = rect.y2;
        link.uri = uri;
        links.push_back(link);
    }
}

void CountImages(pdfio_dict_t* page_dict, PageText& out) {
    pdfio_dict_t* resources = pdfioDictGetDict(page_dict, "Resources");
    if (resources == nullptr) return;
    pdfio_dict_t* xobjects = pdfioDictGetDict(resources, "XObject");
    if (xobjects == nullptr) return;
    for (size_t i = 0; i < pdfioDictGetNumPairs(xobjects); ++i) {
        const char* key = pdfioDictGetKey(xobjects, i);
        if (key == nullptr) continue;
        pdfio_dict_t* dict = FontDict(xobjects, key);
        if (dict == nullptr) continue;
        const char* subtype = pdfioDictGetName(dict, "Subtype");
        if (subtype != nullptr && std::string(subtype) == "Image") ++out.images;
    }
}

// ------------------------------------------------------------- structure

struct Line {
    double y = 0.0;
    double x = 0.0;
    double size = 12.0;
    double last_x = 0.0;
    std::string last_text;
    std::string text;
    std::vector<Run> runs;
};

// The body size is the size most of the text is set in; the heading
// steps are measured against it.
double BodySize(const std::vector<TextItem>& items) {
    std::map<int, size_t> weight;
    for (const TextItem& item : items) {
        if (item.size <= 0.0) continue;
        const int rounded = static_cast<int>(std::lround(item.size));
        weight[rounded] += CountChars(item.text);
    }
    size_t best = 0;
    int best_size = 12;
    for (const auto& entry : weight) {
        if (entry.second > best) {
            best = entry.second;
            best_size = entry.first;
        }
    }
    return static_cast<double>(best_size);
}

std::vector<Line> BuildLines(const std::vector<TextItem>& items) {
    std::vector<const TextItem*> sorted;
    sorted.reserve(items.size());
    for (const TextItem& item : items) sorted.push_back(&item);
    std::sort(sorted.begin(), sorted.end(), [](const TextItem* a, const TextItem* b) {
        if (std::fabs(a->y - b->y) > 2.0) return a->y > b->y;
        return a->x < b->x;
    });

    std::vector<Line> lines;
    for (const TextItem* item : sorted) {
        size_t index = lines.size();
        for (size_t i = 0; i < lines.size(); ++i) {
            if (std::fabs(lines[i].y - item->y) <= 2.0) {
                index = i;
                break;
            }
        }
        if (index == lines.size()) {
            Line fresh;
            fresh.y = item->y;
            fresh.x = item->x;
            fresh.size = item->size;
            fresh.last_x = item->x;
            fresh.last_text = item->text;
            fresh.text = item->text;
            fresh.runs.push_back(
                Run{item->text, item->bold, item->italic, item->mono, false, false, item->link});
            lines.push_back(fresh);
            continue;
        }

        Line& line = lines[index];
        // A word gap shows up as a jump past the previous word's width.
        if (!line.text.empty() &&
            item->x > line.last_x + EstimatedWidth(line.last_text, item->size) + item->size * 0.25) {
            line.runs.push_back(Run{" "});
            line.text += ' ';
        }
        line.runs.push_back(
            Run{item->text, item->bold, item->italic, item->mono, false, false, item->link});
        line.text += item->text;
        line.last_x = item->x;
        line.last_text = item->text;
        if (item->size > line.size) line.size = item->size;
    }
    return lines;
}

// A list line starts with a bullet or a number marker, and the marker
// itself is not part of the text the model keeps.
bool ListMarker(const std::string& text, bool& ordered) {
    ordered = false;
    if (text.compare(0, 4, "\xE2\x80\xA2 ") == 0) return true;   // "bullet" + space
    if (text.compare(0, 4, "\xE2\x80\x93 ") == 0) return true;   // en dash + space
    if (text.size() >= 2 && (text[0] == '-' || text[0] == '*') && text[1] == ' ') return true;
    size_t digits = 0;
    while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits]))) ++digits;
    if (digits > 0 && digits + 1 < text.size() && (text[digits] == '.' || text[digits] == ')') &&
        text[digits + 1] == ' ') {
        ordered = true;
        return true;
    }
    return false;
}

// The marker characters come off the front of the runs, so the model
// holds the item text without its bullet or number.
void TrimRuns(std::vector<Run>& runs, size_t chars) {
    while (chars > 0 && !runs.empty()) {
        Run& first = runs.front();
        const size_t length = CountChars(first.text);
        if (length <= chars) {
            chars -= length;
            runs.erase(runs.begin());
            continue;
        }
        size_t bytes = 0;
        size_t seen = 0;
        while (bytes < first.text.size() && seen < chars) {
            ++bytes;
            while (bytes < first.text.size() &&
                   (static_cast<unsigned char>(first.text[bytes]) & 0xC0) == 0x80)
                ++bytes;
            ++seen;
        }
        first.text.erase(0, bytes);
        chars = 0;
    }
    while (!runs.empty() && runs.front().text.empty()) runs.erase(runs.begin());
}

void AppendBlocks(const std::vector<Line>& lines, double body_size, std::vector<Block>& blocks) {
    double previous_y = 0.0;
    double previous_x = 0.0;
    double previous_size = 0.0;
    bool have_paragraph = false;

    for (const Line& line : lines) {
        if (line.runs.empty()) continue;
        bool ordered = false;
        const bool is_list = ListMarker(line.text, ordered);

        BlockKind kind = Paragraph;
        int level = 0;
        if (!is_list && line.size >= body_size * 1.8) {
            kind = Title;
        } else if (!is_list && line.size >= body_size * 1.35) {
            kind = Heading;
            level = 1;
        } else if (!is_list && line.size >= body_size * 1.15 && CountChars(line.text) <= 80) {
            kind = Heading;
            level = 2;
        } else if (is_list) {
            kind = ListItem;
        }

        if (kind == Paragraph && have_paragraph &&
            std::fabs(line.x - previous_x) <= 6.0 &&
            previous_y - line.y <= std::max(line.size, previous_size) * 2.0) {
            Block& paragraph = blocks.back();
            paragraph.runs.push_back(Run{" "});
            paragraph.runs.insert(paragraph.runs.end(), line.runs.begin(), line.runs.end());
        } else {
            Block block;
            block.kind = kind;
            block.level = level;
            block.ordered = ordered;
            block.runs = line.runs;
            if (kind == ListItem) {
                size_t marker = 0;
                bool marker_ordered = false;
                ListMarker(line.text, marker_ordered);
                if (marker_ordered) {
                    marker = 0;
                    while (marker < line.text.size() &&
                           (std::isdigit(static_cast<unsigned char>(line.text[marker])) ||
                            line.text[marker] == '.' || line.text[marker] == ')'))
                        ++marker;
                    ++marker;   // the space after the marker
                } else if (line.text.compare(0, 4, "\xE2\x80\xA2 ") == 0 ||
                           line.text.compare(0, 4, "\xE2\x80\x93 ") == 0) {
                    marker = 2;
                } else {
                    marker = 2;
                }
                TrimRuns(block.runs, marker);
            }
            blocks.push_back(block);
        }

        previous_y = line.y;
        previous_x = line.x;
        previous_size = line.size;
        have_paragraph = kind == Paragraph;
    }
}

// The OCR text arrives as plain lines; each one becomes a paragraph.
void AppendRecognizedText(const std::string& recognized, std::vector<Block>& blocks) {
    std::string current;
    auto flush = [&]() {
        if (current.empty()) return;
        Block block;
        block.kind = Paragraph;
        block.runs.push_back(Run{current});
        blocks.push_back(block);
        current.clear();
    };
    for (char c : recognized) {
        if (c == '\n' || c == '\r')
            flush();
        else
            current.push_back(c);
    }
    flush();
}

void AttachLinks(std::vector<TextItem>& items, const std::vector<LinkAnnot>& links) {
    for (TextItem& item : items) {
        for (const LinkAnnot& link : links) {
            const double width = EstimatedWidth(item.text, item.size);
            const double x1 = item.x;
            const double x2 = item.x + width;
            const double y1 = item.y - item.size * 0.25;
            const double y2 = item.y + item.size * 0.9;
            if (x2 > link.x1 && x1 < link.x2 && y2 > link.y1 && y1 < link.y2) {
                item.link = link.uri;
                break;
            }
        }
    }
}

}  // namespace

bool PdfImport(const std::string& path, const PdfImportOptions& opt, DocModel& out,
               CompatReport& report, std::string& error) {
    out = DocModel();
    error.clear();

    std::string password = opt.password;
    std::string messages;
    pdfio_file_t* pdf =
        pdfioFileOpen(path.c_str(), PasswordFromData, &password, CollectMessage, &messages);
    if (pdf == nullptr) {
        const std::string lower = LowerAscii(messages);
        // pdfio says "Unable to unlock PDF file" when the password it
        // was handed does not open the file.
        if (ContainsAscii(lower, "encrypt") || ContainsAscii(lower, "password") ||
            ContainsAscii(lower, "decrypt") || ContainsAscii(lower, "unlock")) {
            error = "the PDF is password-protected; a password is required";
        } else {
            error = messages.empty() ? std::string("unable to open the PDF")
                                     : TrimMessages(messages);
        }
        return false;
    }

    const size_t pages = pdfioFileGetNumPages(pdf);
    if (pages == 0) {
        error = "the PDF has no pages";
        pdfioFileClose(pdf);
        return false;
    }

    size_t scanned_pages = 0;
    size_t ocr_pages = 0;
    size_t images = 0;
    size_t mixed_size_pages = 0;
    bool has_rules = false;
    bool text_seen = false;
    bool page_setup_set = false;
    double first_width = 0.0;
    double first_height = 0.0;

    for (size_t p = 0; p < pages; ++p) {
        pdfio_obj_t* page = pdfioFileGetPage(pdf, p);
        if (page == nullptr) continue;
        pdfio_dict_t* page_dict = pdfioObjGetDict(page);
        if (page_dict == nullptr) continue;

        // The page setup comes from the first page that carries one.
        pdfio_rect_t box;
        if (pdfioDictGetRect(page_dict, "MediaBox", &box) != nullptr) {
            const double width = box.x2 - box.x1;
            const double height = box.y2 - box.y1;
            if (width > 0.0 && height > 0.0) {
                if (!page_setup_set) {
                    out.page.width_pt = width;
                    out.page.height_pt = height;
                    out.page.landscape = width > height;
                    page_setup_set = true;
                    first_width = width;
                    first_height = height;
                } else if (std::fabs(width - first_width) > 1.0 ||
                           std::fabs(height - first_height) > 1.0) {
                    ++mixed_size_pages;
                }
            }
        }

        std::map<std::string, FontInfo> fonts;
        LoadFonts(page_dict, fonts);
        PageText page_text;
        LoadLinks(page_dict, page_text.links);
        ParsePageContent(page, fonts, page_text);
        CountImages(page_dict, page_text);
        images += page_text.images;
        if (page_text.has_rule_ops) has_rules = true;

        if (page_text.items.empty()) {
            ++scanned_pages;
            std::string recognized;
            if (opt.ocr != nullptr) recognized = opt.ocr(opt.ocr_data, path, p);
            if (!recognized.empty()) {
                ++ocr_pages;
                text_seen = true;
                AppendRecognizedText(recognized, out.blocks);
            } else {
                AppendWarning(report, "scanned",
                              "page " + std::to_string(p + 1) +
                                  " looks scanned or image-based; it needs OCR to yield text");
            }
        } else {
            text_seen = true;
            AttachLinks(page_text.items, page_text.links);
            const std::vector<Line> lines = BuildLines(page_text.items);
            AppendBlocks(lines, BodySize(page_text.items), out.blocks);
        }

        if (p + 1 < pages) {
            Block page_break;
            page_break.kind = PageBreak;
            out.blocks.push_back(page_break);
        }
    }
    pdfioFileClose(pdf);

    if (ocr_pages > 0) {
        AppendWarning(report, "ocr",
                      CountText(ocr_pages, "scanned page", "scanned pages") +
                          " came in through OCR; check the recognized text against the page");
    }
    if (mixed_size_pages > 0) {
        AppendWarning(report, "page",
                      CountText(mixed_size_pages, "page", "pages") +
                          " with another size than page 1 keep the page 1 setup");
    }
    if (has_rules) {
        AppendWarning(report, "table",
                      "table and figure rules are not rebuilt; their text is imported as "
                      "regular lines");
    }
    if (images > 0) {
        AppendWarning(report, "image",
                      CountText(images, "drawing or picture", "drawings or pictures") +
                          " on the pages; images are not carried by the model");
    }
    if (text_seen) {
        AppendWarning(report, "font",
                      "the typefaces and exact formatting are mapped to the model (bold, "
                      "italic, monospaced); the original fonts are not reproduced");
    }

    if (!text_seen) {
        error = "no text could be extracted; the pages look scanned or image-based";
        return false;
    }
    return true;
}

}  // namespace office
