// Word .docx (WordprocessingML) writer for the shared office document model.
//
// Portable C++17 with no Windows headers, so the writer runs both under MSVC in
// CI and headless on Linux. The package is assembled with miniz and the XML is
// written by hand so every byte stays under the writer's control.
//
// The produced package carries the parts Word needs:
//
//   [Content_Types].xml           every part listed, or Word offers a repair
//   _rels/.rels                   package root -> main document
//   word/document.xml             the body plus a single section
//   word/_rels/document.xml.rels  styles, numbering, hyperlink targets
//   word/styles.xml               Normal, Title, Heading1..9, ListParagraph
//   word/numbering.xml            the bullet and the decimal list definition
//   word/footer1.xml              only when the page setup asks for page numbers
//
// Some of the layout is a contract with the independent reader in
// tools/office-oracle/dump_docx.py, and with Word itself:
//
//   * exactly one w:r per model Run, in model order, never merged, so the
//     reader reports the same run split the model carries
//   * a mono run names Consolas, one of the monospace fonts the reader knows
//   * list items are numbered through numId 10 (bullets) and 11 (decimals)
//   * one body-level w:sectPr, so the reader reports a single section
//   * every w:t carries xml:space="preserve", so spaces survive

#include "docx_export.h"

#include "miniz.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace office {
namespace {

const char kXmlDeclaration[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
const char kWmlNamespace[] =
    "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
const char kRelationshipsNamespace[] =
    "http://schemas.openxmlformats.org/package/2006/relationships";
const char kOfficeDocumentRelType[] =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
// The monospace font the reader recognises. Any name on its list would do;
// Consolas is the one that ships with Windows.
const char kMonoFont[] = "Consolas";
// U+2022 BULLET, written as hex escapes so this source stays ASCII. Same
// convention as tests/navigation_test.cpp.
const char kBulletGlyph[] = "\xe2" "\x80" "\xa2";
// Word caps page geometry at 31680 twips (55.88 cm); see ECMA-376.
const double kMaxTwips = 31680.0;
// Numbering ids: the two list kinds get their own definition, so the reader
// only has to look at the numFmt to tell them apart.
const char kBulletNumberingId[] = "10";
const char kDecimalNumberingId[] = "11";
const int kMaxHeadingLevel = 9;   // Heading 1 .. Heading 9
const int kMaxListLevel = 8;      // nine list levels, Word counts them from 0
// The timestamp every package entry carries: 1980-01-02T00:00:00Z, far enough
// from 1980-01-01 that no timezone shift can push an entry before the earliest
// date the zip format can express. Fixing it makes repeated exports of one
// document byte identical on a machine (the DOS time field still shows the
// local timezone); Word ignores the field.
const MZ_TIME_T kEntryTimestamp = static_cast<MZ_TIME_T>(315619200);

std::string Number(long long value) { return std::to_string(value); }

// ---------------------------------------------------------------------------
// Text and XML

bool IsContinuationByte(unsigned char byte) { return (byte & 0xC0) == 0x80; }

// Length in bytes of the valid UTF-8 sequence starting at value[index], or 0
// when the byte there cannot start one. Overlong forms and surrogates are
// rejected because an XML parser rejects them too.
std::size_t Utf8SequenceLength(const std::string& value, std::size_t index) {
    const unsigned char first = static_cast<unsigned char>(value[index]);
    const std::size_t remaining = value.size() - index;
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0) {
        if (first < 0xC2 || remaining < 2) return 0;
        return IsContinuationByte(static_cast<unsigned char>(value[index + 1])) ? 2 : 0;
    }
    if ((first & 0xF0) == 0xE0) {
        if (remaining < 3) return 0;
        const unsigned char second = static_cast<unsigned char>(value[index + 1]);
        if (!IsContinuationByte(second) ||
            !IsContinuationByte(static_cast<unsigned char>(value[index + 2])))
            return 0;
        if (first == 0xE0 && second < 0xA0) return 0;   // overlong
        if (first == 0xED && second >= 0xA0) return 0;  // UTF-16 surrogate
        return 3;
    }
    if ((first & 0xF8) == 0xF0) {
        if (first > 0xF4 || remaining < 4) return 0;
        const unsigned char second = static_cast<unsigned char>(value[index + 1]);
        if (!IsContinuationByte(second) ||
            !IsContinuationByte(static_cast<unsigned char>(value[index + 2])) ||
            !IsContinuationByte(static_cast<unsigned char>(value[index + 3])))
            return 0;
        if (first == 0xF0 && second < 0x90) return 0;   // overlong
        if (first == 0xF4 && second > 0x8F) return 0;   // beyond U+10FFFF
        return 4;
    }
    return 0;
}

// Append value escaped for XML text or for a double quoted attribute value.
// Bytes XML cannot carry are replaced (a control character becomes a space) or
// dropped (a malformed sequence), and changed records that, so the caller can
// report it instead of losing text silently.
void AppendEscaped(std::string& out, const std::string& value, bool& changed) {
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        if (byte < 0x80) {
            switch (byte) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case 0x22: out += "&quot;"; break;
                default:
                    if (byte < 0x20 && byte != 0x09 && byte != 0x0A && byte != 0x0D) {
                        out += ' ';
                        changed = true;
                    } else {
                        out += static_cast<char>(byte);
                    }
                    break;
            }
            index += 1;
            continue;
        }
        const std::size_t length = Utf8SequenceLength(value, index);
        if (length == 0) {
            changed = true;
            index += 1;
            continue;
        }
        out.append(value, index, length);
        index += length;
    }
}

void FlushText(std::string& out, std::string& pending, bool& changed) {
    if (pending.empty()) return;
    out += "<w:t xml:space=\"preserve\">";
    AppendEscaped(out, pending, changed);
    out += "</w:t>";
    pending.clear();
}

// The inline content of one run. A line break or a tab inside the text becomes
// w:br / w:tab inside the same Word run, so the model's run split survives and
// the reader still reports the same text.
void AppendRunContent(std::string& out, const std::string& text, bool& changed) {
    std::string pending;
    std::size_t index = 0;
    while (index < text.size()) {
        const char character = text[index];
        if (character == '\n' || character == '\r') {
            FlushText(out, pending, changed);
            out += "<w:br/>";
            if (character == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
                index += 1;
            index += 1;
            continue;
        }
        if (character == '\t') {
            FlushText(out, pending, changed);
            out += "<w:tab/>";
            index += 1;
            continue;
        }
        pending += character;
        index += 1;
    }
    FlushText(out, pending, changed);
}

// ---------------------------------------------------------------------------
// Compatibility report

// Warnings are collected per feature so a large document still yields a short
// report, and emitted in feature order so the report is stable across runs.
class Warnings {
public:
    void Add(const std::string& feature, const std::string& detail) {
        Entry& entry = entries_[feature];
        if (entry.count == 0) entry.detail = detail;
        entry.count += 1;
    }

    void AppendTo(CompatReport& report) const {
        for (std::map<std::string, Entry>::const_iterator item = entries_.begin();
             item != entries_.end(); ++item) {
            CompatWarning warning;
            warning.feature = item->first;
            warning.detail = item->second.detail;
            if (item->second.count > 1)
                warning.detail += " (" + Number(item->second.count) + " occurrences)";
            report.warnings.push_back(warning);
        }
    }

private:
    struct Entry {
        std::string detail;
        int count = 0;
    };

    std::map<std::string, Entry> entries_;
};

// ---------------------------------------------------------------------------
// Page geometry

struct Geometry {
    long width = 11906;      // A4 portrait, in twips
    long height = 16838;
    long margin = 1134;      // 2 cm
    bool landscape = false;
    int text_width = 9638;   // page width minus both margins, for table cells
};

// Word counts twentieths of a point. Clamped so a wild model cannot produce a
// value the schema refuses.
long Twips(double points) {
    if (!std::isfinite(points)) return 0;
    double value = std::floor(points * 20.0 + 0.5);
    if (value < 0.0) return 0;
    if (value > kMaxTwips) return static_cast<long>(kMaxTwips);
    return static_cast<long>(value);
}

// The model carries points; landscape swaps the two dimensions, the way Word
// writes a rotated page.
Geometry EffectiveGeometry(const PageSetup& page, Warnings& warnings) {
    Geometry geometry;
    geometry.landscape = page.landscape;

    double width = page.width_pt;
    double height = page.height_pt;
    double margin = page.margin_pt;

    if (!std::isfinite(width) || width <= 0.0) {
        warnings.Add("PageSetup", "non-positive page width replaced by A4");
        width = 595.28;
    }
    if (!std::isfinite(height) || height <= 0.0) {
        warnings.Add("PageSetup", "non-positive page height replaced by A4");
        height = 841.89;
    }
    if (!std::isfinite(margin) || margin < 0.0) {
        warnings.Add("PageSetup", "negative page margin replaced by 2 cm");
        margin = 56.7;
    }

    geometry.width = Twips(width);
    geometry.height = Twips(height);
    geometry.margin = Twips(margin);
    if (geometry.landscape) std::swap(geometry.width, geometry.height);

    long text = geometry.width - 2 * geometry.margin;
    if (text < 1) {
        warnings.Add("PageSetup", "margins leave no text width; table cells use a narrow grid");
        text = 1;
    }
    geometry.text_width = static_cast<int>(text);
    return geometry;
}

// ---------------------------------------------------------------------------
// Body writer

class DocxWriter {
public:
    // The warnings collector is shared with the caller: the page setup, the
    // dropped features and the sanitized text all report through it.
    DocxWriter(int table_width, Warnings& warnings)
        : table_width_(table_width), warnings_(warnings) {}

    // One stable relationship id per distinct hyperlink target, handed out in
    // the order the targets first appear in the document.
    void CollectLinks(const std::vector<Run>& runs, std::size_t& next_relationship_id) {
        for (const Run& run : runs) {
            if (run.link.empty()) continue;
            if (links_.find(run.link) != links_.end()) continue;
            links_[run.link] = "rId" + Number(static_cast<long long>(next_relationship_id));
            next_relationship_id += 1;
        }
    }

    const std::map<std::string, std::string>& links() const { return links_; }

    bool text_changed() const { return text_changed_; }

    std::string SectionProperties(const Geometry& geometry, const std::string& footer_id) const {
        std::string out = "<w:sectPr>";
        if (!footer_id.empty()) {
            out += "<w:footerReference w:type=\"default\" r:id=\"";
            out += footer_id;
            out += "\"/>";
        }
        out += "<w:pgSz w:w=\"";
        out += Number(geometry.width);
        out += "\" w:h=\"";
        out += Number(geometry.height);
        out += "\"";
        if (geometry.landscape) out += " w:orient=\"landscape\"";
        out += "/><w:pgMar w:top=\"";
        out += Number(geometry.margin);
        out += "\" w:right=\"";
        out += Number(geometry.margin);
        out += "\" w:bottom=\"";
        out += Number(geometry.margin);
        out += "\" w:left=\"";
        out += Number(geometry.margin);
        out += "\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/>"
               "<w:cols w:space=\"720\"/><w:docGrid w:linePitch=\"360\"/></w:sectPr>";
        return out;
    }

    void AppendBlock(std::string& out, const Block& block) {
        switch (block.kind) {
            case BlockKind::Title:
                AppendParagraph(out, "<w:pStyle w:val=\"Title\"/>", block.runs);
                break;
            case BlockKind::Heading: {
                int level = block.level;
                if (level < 1 || level > kMaxHeadingLevel) {
                    warnings_.Add("HeadingLevel",
                                 "heading level outside 1..9 clamped to the nearest level");
                    level = std::max(1, std::min(kMaxHeadingLevel, level));
                }
                AppendParagraph(out, "<w:pStyle w:val=\"Heading" + Number(level) + "\"/>",
                                block.runs);
                break;
            }
            case BlockKind::ListItem: {
                int level = block.level;
                if (level < 0 || level > kMaxListLevel) {
                    warnings_.Add("ListLevel",
                                 "list level outside 0..8 clamped to the nearest level");
                    level = std::max(0, std::min(kMaxListLevel, level));
                }
                std::string properties =
                    "<w:pStyle w:val=\"ListParagraph\"/><w:numPr><w:ilvl w:val=\"";
                properties += Number(level);
                properties += "\"/><w:numId w:val=\"";
                properties += block.ordered ? kDecimalNumberingId : kBulletNumberingId;
                properties += "\"/></w:numPr>";
                AppendParagraph(out, properties, block.runs);
                break;
            }
            case BlockKind::PageBreak:
                out += "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>";
                break;
            case BlockKind::Table:
                AppendTable(out, block);
                break;
            case BlockKind::CodeBlock:
                // The text and its run formatting survive; the code block's own
                // look does not, and the model has no field to carry it.
                warnings_.Add("CodeBlock",
                             "code block written as a plain paragraph: shading, border and "
                             "code style are not carried");
                AppendParagraph(out, std::string(), block.runs);
                break;
            case BlockKind::Image:
                // A picture cannot be carried: the model holds no image data.
                warnings_.Add("Image",
                             "the picture is not embedded; only the block text is written");
                AppendParagraph(out, std::string(), block.runs);
                break;
            case BlockKind::ThematicBreak:
                // A rule under an empty line. Any runs the block carries are kept.
                AppendParagraph(out,
                                "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" "
                                "w:color=\"auto\"/></w:pBdr>",
                                block.runs);
                break;
            case BlockKind::Paragraph:
            default:
                AppendParagraph(out, std::string(), block.runs);
                break;
        }
    }

private:
    void AppendParagraph(std::string& out, const std::string& properties,
                         const std::vector<Run>& runs) {
        out += "<w:p>";
        if (!properties.empty()) {
            out += "<w:pPr>";
            out += properties;
            out += "</w:pPr>";
        }
        for (const Run& run : runs) AppendRun(out, run);
        out += "</w:p>";
    }

    // One Word run per model run, never merged with its neighbour. The property
    // order is the one the schema prescribes: fonts, bold, italic, strike,
    // color, underline.
    void AppendRun(std::string& out, const Run& run) {
        std::string properties;
        if (run.mono) {
            properties += "<w:rFonts w:ascii=\"";
            properties += kMonoFont;
            properties += "\" w:hAnsi=\"";
            properties += kMonoFont;
            properties += "\" w:cs=\"";
            properties += kMonoFont;
            properties += "\"/>";
        }
        if (run.bold) properties += "<w:b/>";
        if (run.italic) properties += "<w:i/>";
        if (run.strike) properties += "<w:strike/>";
        if (!run.link.empty()) properties += "<w:color w:val=\"0563C1\"/>";
        if (run.underline) properties += "<w:u w:val=\"single\"/>";

        std::string content = "<w:r>";
        if (!properties.empty()) {
            content += "<w:rPr>";
            content += properties;
            content += "</w:rPr>";
        }
        AppendRunContent(content, run.text, text_changed_);
        content += "</w:r>";

        const std::string relationship = run.link.empty() ? std::string() : IdFor(run.link);
        if (relationship.empty()) {
            out += content;
            return;
        }
        out += "<w:hyperlink r:id=\"";
        out += relationship;
        out += "\">";
        out += content;
        out += "</w:hyperlink>";
    }

    std::string IdFor(const std::string& target) const {
        const std::map<std::string, std::string>::const_iterator found = links_.find(target);
        return found == links_.end() ? std::string() : found->second;
    }

    void AppendTable(std::string& out, const Block& block) {
        std::size_t columns = 0;
        for (const std::vector<Cell>& row : block.rows) columns = std::max(columns, row.size());
        if (columns == 0) columns = 1;   // a grid is required even with no rows
        const long column_width =
            std::max(1L, static_cast<long>(table_width_) / static_cast<long>(columns));

        out += "<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblW w:type=\"auto\" "
               "w:w=\"0\"/><w:tblLook w:val=\"04A0\" w:firstRow=\"1\" w:lastRow=\"0\" "
               "w:firstColumn=\"1\" w:lastColumn=\"0\" w:noHBand=\"0\" w:noVBand=\"1\"/>"
               "</w:tblPr><w:tblGrid>";
        for (std::size_t column = 0; column < columns; ++column) {
            out += "<w:gridCol w:w=\"";
            out += Number(column_width);
            out += "\"/>";
        }
        out += "</w:tblGrid>";

        for (const std::vector<Cell>& row : block.rows) {
            out += "<w:tr>";
            for (const Cell& cell : row) {
                out += "<w:tc><w:tcPr><w:tcW w:type=\"dxa\" w:w=\"";
                out += Number(column_width);
                out += "\"/></w:tcPr>";
                // Every cell needs a paragraph, also when it carries no runs.
                AppendParagraph(out, std::string(), cell.runs);
                out += "</w:tc>";
            }
            out += "</w:tr>";
        }
        out += "</w:tbl>";
    }

    std::map<std::string, std::string> links_;
    int table_width_ = 9638;
    bool text_changed_ = false;
    Warnings& warnings_;
};

// ---------------------------------------------------------------------------
// Parts

std::string ContentTypesPart(bool with_footer) {
    std::string out = kXmlDeclaration;
    out += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
           "<Default Extension=\"rels\" "
           "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
           "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
           "<Override PartName=\"/word/document.xml\" "
           "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml."
           "document.main+xml\"/>"
           "<Override PartName=\"/word/styles.xml\" "
           "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles"
           "+xml\"/>"
           "<Override PartName=\"/word/numbering.xml\" "
           "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml."
           "numbering+xml\"/>";
    if (with_footer) {
        out += "<Override PartName=\"/word/footer1.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml."
               "footer+xml\"/>";
    }
    out += "</Types>";
    return out;
}

std::string RootRelationshipsPart() {
    std::string out = kXmlDeclaration;
    out += "<Relationships xmlns=\"";
    out += kRelationshipsNamespace;
    out += "\"><Relationship Id=\"rId1\" Type=\"";
    out += kOfficeDocumentRelType;
    out += "/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
    return out;
}

std::string DocumentRelationshipsPart(const std::map<std::string, std::string>& links,
                                      const std::string& footer_id, bool& changed) {
    std::string out = kXmlDeclaration;
    out += "<Relationships xmlns=\"";
    out += kRelationshipsNamespace;
    out += "\"><Relationship Id=\"rId1\" Type=\"";
    out += kOfficeDocumentRelType;
    out += "/styles\" Target=\"styles.xml\"/><Relationship Id=\"rId2\" Type=\"";
    out += kOfficeDocumentRelType;
    out += "/numbering\" Target=\"numbering.xml\"/>";
    if (!footer_id.empty()) {
        out += "<Relationship Id=\"";
        out += footer_id;
        out += "\" Type=\"";
        out += kOfficeDocumentRelType;
        out += "/footer\" Target=\"footer1.xml\"/>";
    }
    for (std::map<std::string, std::string>::const_iterator item = links.begin();
         item != links.end(); ++item) {
        out += "<Relationship Id=\"";
        out += item->second;
        out += "\" Type=\"";
        out += kOfficeDocumentRelType;
        out += "/hyperlink\" Target=\"";
        AppendEscaped(out, item->first, changed);
        out += "\" TargetMode=\"External\"/>";
    }
    out += "</Relationships>";
    return out;
}

std::string StylesPart() {
    std::string out = kXmlDeclaration;
    out += "<w:styles xmlns:w=\"";
    out += kWmlNamespace;
    out += "\"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" "
           "w:hAnsi=\"Calibri\" w:cs=\"Calibri\"/><w:sz w:val=\"22\"/><w:szCs "
           "w:val=\"22\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing "
           "w:after=\"160\" w:line=\"259\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
           "</w:docDefaults>"
           "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
           "<w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/>"
           "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
           "<w:pPr><w:spacing w:after=\"300\"/></w:pPr><w:rPr><w:sz w:val=\"56\"/>"
           "<w:szCs w:val=\"56\"/></w:rPr></w:style>";

    // Word's own heading sizes in half points, for level 1..9.
    static const int kHeadingHalfPoints[] = {32, 26, 24, 22, 22, 22, 22, 22, 22};
    for (int level = 1; level <= kMaxHeadingLevel; ++level) {
        // The style name is the internal one ("heading 1"); python-docx maps it
        // back to "Heading 1", which is what the reader matches on.
        out += "<w:style w:type=\"paragraph\" w:styleId=\"Heading";
        out += Number(level);
        out += "\"><w:name w:val=\"heading ";
        out += Number(level);
        out += "\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
               "<w:pPr><w:keepNext/><w:keepLines/><w:outlineLvl w:val=\"";
        out += Number(level - 1);
        out += "\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"";
        out += Number(kHeadingHalfPoints[level - 1]);
        out += "\"/><w:szCs w:val=\"";
        out += Number(kHeadingHalfPoints[level - 1]);
        out += "\"/></w:rPr></w:style>";
    }

    out += "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List "
           "Paragraph\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/></w:style>"
           "<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\">"
           "<w:name w:val=\"Normal Table\"/><w:tblPr><w:tblInd w:w=\"0\" "
           "w:type=\"dxa\"/></w:tblPr></w:style>"
           "<w:style w:type=\"table\" w:styleId=\"TableGrid\"><w:name w:val=\"Table "
           "Grid\"/><w:basedOn w:val=\"TableNormal\"/><w:tblPr><w:tblBorders>"
           "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>"
           "</w:tblBorders></w:tblPr></w:style></w:styles>";
    return out;
}

void AppendNumberingLevel(std::string& out, int level, const char* format, const char* text) {
    out += "<w:lvl w:ilvl=\"";
    out += Number(level);
    out += "\"><w:start w:val=\"1\"/><w:numFmt w:val=\"";
    out += format;
    out += "\"/><w:lvlText w:val=\"";
    out += text;
    out += "\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"";
    out += Number(720L * (level + 1));
    out += "\" w:hanging=\"360\"/></w:pPr></w:lvl>";
}

std::string NumberingPart() {
    std::string out = kXmlDeclaration;
    out += "<w:numbering xmlns:w=\"";
    out += kWmlNamespace;
    out += "\"><w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType "
           "w:val=\"hybridMultilevel\"/>";
    // All levels the writer can emit (kMaxListLevel = 8) are defined, so
    // a deep list never falls back to Word's default numbering for the
    // missing levels. Bullet glyphs and decimal texts follow Word's own
    // level ladder; indentation scales per level.
    static const char* kBullets[] = {kBulletGlyph, "o",
        "\xe2\x96\xa0", "\xe2\x97\x8f", "\xe2\x96\xa0",
        "\xe2\x97\x8f", "\xe2\x96\xa0", "\xe2\x97\x8f", "\xe2\x96\xa0"};
    static const char* kDecimal[] = {"%1.", "%2.", "%3.", "%4.", "%5.",
                                     "%6.", "%7.", "%8.", "%9."};
    for (int level = 0; level <= 8; ++level)
        AppendNumberingLevel(out, level, "bullet", kBullets[level]);
    out += "</w:abstractNum><w:abstractNum w:abstractNumId=\"1\">"
           "<w:multiLevelType w:val=\"hybridMultilevel\"/>";
    for (int level = 0; level <= 8; ++level)
        AppendNumberingLevel(out, level, "decimal", kDecimal[level]);
    out += "</w:abstractNum><w:num w:numId=\"";
    out += kBulletNumberingId;
    out += "\"><w:abstractNumId w:val=\"0\"/></w:num><w:num w:numId=\"";
    out += kDecimalNumberingId;
    out += "\"><w:abstractNumId w:val=\"1\"/></w:num></w:numbering>";
    return out;
}

// "Side N" in the footer of every page: fixed text, then a PAGE field.
std::string FooterPart() {
    std::string out = kXmlDeclaration;
    out += "<w:ftr xmlns:w=\"";
    out += kWmlNamespace;
    out += "\"><w:p><w:pPr><w:jc w:val=\"center\"/></w:pPr>"
           "<w:r><w:t xml:space=\"preserve\">Side </w:t></w:r>"
           "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r>"
           "<w:r><w:instrText xml:space=\"preserve\"> PAGE </w:instrText></w:r>"
           "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
           "<w:r><w:t xml:space=\"preserve\">1</w:t></w:r>"
           "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p></w:ftr>";
    return out;
}

// ---------------------------------------------------------------------------
// Package

struct Part {
    std::string name;
    std::string bytes;
};

bool WritePackage(const std::vector<Part>& parts, std::string& out_bytes, std::string& error) {
    mz_zip_archive archive;
    std::memset(&archive, 0, sizeof(archive));
    if (mz_zip_writer_init_heap(&archive, 0, 0) != MZ_TRUE) {
        error = "cannot start the zip writer";
        return false;
    }
    MZ_TIME_T timestamp = kEntryTimestamp;
    for (const Part& part : parts) {
        if (mz_zip_writer_add_mem_ex_v2(&archive, part.name.c_str(), part.bytes.data(),
                                        part.bytes.size(), nullptr, 0, MZ_DEFAULT_LEVEL, 0, 0,
                                        &timestamp, nullptr, 0, nullptr, 0) != MZ_TRUE) {
            mz_zip_writer_end(&archive);
            error = "cannot add " + part.name + " to the package";
            return false;
        }
    }

    void* buffer = nullptr;
    std::size_t size = 0;
    if (mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size) != MZ_TRUE) {
        mz_zip_writer_end(&archive);
        error = "cannot finish the package";
        return false;
    }
    out_bytes.assign(static_cast<const char*>(buffer), size);
    mz_free(buffer);
    mz_zip_writer_end(&archive);
    if (out_bytes.empty()) {
        error = "the package came out empty";
        return false;
    }
    return true;
}

// Rough size of the body XML, so a large document does not reallocate its way
// through the write. Being wrong here only costs a little memory.
std::size_t BodySizeHint(const DocModel& doc) {
    std::size_t hint = 512;
    for (const Block& block : doc.blocks) {
        hint += 64 + block.runs.size() * 48 + block.rows.size() * 128;
        for (const Run& run : block.runs) hint += run.text.size() + run.link.size() + 32;
        for (const std::vector<Cell>& row : block.rows) {
            for (const Cell& cell : row) {
                hint += 96;
                for (const Run& run : cell.runs) hint += run.text.size() + 32;
            }
        }
    }
    return hint;
}

}  // namespace

bool DocxExport(const DocModel& doc, std::string& out_bytes, CompatReport& report,
                std::string& error) {
    out_bytes.clear();
    error.clear();

    Warnings warnings;
    const Geometry geometry = EffectiveGeometry(doc.page, warnings);

    // Relationship ids are fixed for the named parts and handed out in document
    // order to the hyperlink targets.
    std::string footer_id;
    std::size_t next_relationship_id = 3;
    if (doc.page.footer_page_numbers) {
        footer_id = "rId" + Number(static_cast<long long>(next_relationship_id));
        next_relationship_id += 1;
    }

    DocxWriter writer(geometry.text_width, warnings);
    for (const Block& block : doc.blocks) {
        writer.CollectLinks(block.runs, next_relationship_id);
        for (const std::vector<Cell>& row : block.rows) {
            for (const Cell& cell : row) writer.CollectLinks(cell.runs, next_relationship_id);
        }
    }

    if (!doc.title.empty()) {
        bool carried = false;
        for (const Block& block : doc.blocks) {
            if (block.kind == BlockKind::Title) {
                carried = true;
                break;
            }
        }
        if (!carried)
            warnings.Add("DocumentTitle",
                         "the model title has no Title block to carry it and is not written");
    }

    std::string body;
    body.reserve(BodySizeHint(doc));
    for (const Block& block : doc.blocks) writer.AppendBlock(body, block);
    body += writer.SectionProperties(geometry, footer_id);

    std::string document = kXmlDeclaration;
    document += "<w:document xmlns:w=\"";
    document += kWmlNamespace;
    document += "\" xmlns:r=\"";
    document += kOfficeDocumentRelType;
    document += "\"><w:body>";
    document += body;
    document += "</w:body></w:document>";

    bool link_targets_changed = false;
    const std::string document_relationships =
        DocumentRelationshipsPart(writer.links(), footer_id, link_targets_changed);
    if (writer.text_changed() || link_targets_changed)
        warnings.Add("InvalidText",
                     "text or a link target had to be cleaned up: control characters became "
                     "spaces, malformed bytes were dropped");

    std::vector<Part> parts;
    parts.push_back(Part{"[Content_Types].xml", ContentTypesPart(!footer_id.empty())});
    parts.push_back(Part{"_rels/.rels", RootRelationshipsPart()});
    parts.push_back(Part{"word/document.xml", document});
    parts.push_back(Part{"word/_rels/document.xml.rels", document_relationships});
    parts.push_back(Part{"word/styles.xml", StylesPart()});
    parts.push_back(Part{"word/numbering.xml", NumberingPart()});
    if (!footer_id.empty()) parts.push_back(Part{"word/footer1.xml", FooterPart()});

    if (!WritePackage(parts, out_bytes, error)) {
        out_bytes.clear();
        return false;
    }

    warnings.AppendTo(report);
    return true;
}

}  // namespace office
