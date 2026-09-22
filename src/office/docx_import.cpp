// Word .docx import for MarkDownIt.
//
// Reads an OPC package (a ZIP container) from raw bytes and fills the shared
// office::DocModel, plus a compatibility report for the features the model
// cannot carry. The mapping is deliberately literal, because
// tools/office-oracle/dump_docx.py and tests/office/golden/ define exactly
// what the model has to contain:
//
//   blocks  the "Title" style -> Title, "Heading N" -> Heading with level N,
//           a paragraph with numbering -> ListItem (ordered unless the number
//           format is "bullet", level = w:ilvl), every other paragraph ->
//           Paragraph, w:tbl -> Table, w:br w:type="page" -> PageBreak
//   runs    read in document order and never merged; w:hyperlink runs carry
//           the relationship target in link
//   page    the body w:sectPr; w:pgSz/w:pgMar are twips, divided by 20 for
//           points; margin_pt is the left margin; landscape when the page is
//           wider than it is tall
//
// Portable C++17: miniz for the container, pugixml for the XML parts, no
// Windows headers, so the same code runs in CI and headless on Linux.

#include "docx_import.h"

#include "miniz.h"
#include "pugixml.hpp"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace office {
namespace {

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

bool IsDigitAscii(char ch) { return ch >= '0' && ch <= '9'; }

bool IsAlnumAscii(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z');
}

std::string LowerAscii(const std::string& text) {
    std::string lowered = text;
    for (char& ch : lowered) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return lowered;
}

std::string TrimAscii(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && static_cast<unsigned char>(text[begin]) <= ' ') ++begin;
    while (end > begin && static_cast<unsigned char>(text[end - 1]) <= ' ') --end;
    return text.substr(begin, end - begin);
}

bool ParseInt(const std::string& text, int& value) {
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(trimmed.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDouble(const std::string& text, double& value) {
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end);
    if (end == nullptr || *end != '\0') return false;
    value = parsed;
    return true;
}

// ---------------------------------------------------------------------------
// XML helpers. Element and attribute names are matched by their local name,
// so a part is read the same way whether it uses the usual w:/r:/m: prefixes
// or different ones.
// ---------------------------------------------------------------------------

const char* LocalName(const char* qualified) {
    const char* colon = std::strchr(qualified, ':');
    return colon ? colon + 1 : qualified;
}

bool IsElement(const pugi::xml_node& node, const char* local) {
    return node.type() == pugi::node_element &&
           std::strcmp(LocalName(node.name()), local) == 0;
}

// The root element of a parsed part, e.g. <w:styles> in word/styles.xml.
pugi::xml_node RootElement(const pugi::xml_document& document) {
    return document.first_child();
}

pugi::xml_node ChildElement(const pugi::xml_node& parent, const char* local) {
    for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling()) {
        if (IsElement(child, local)) return child;
    }
    return pugi::xml_node();
}

std::string AttributeValue(const pugi::xml_node& node, const char* local) {
    for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute()) {
        if (std::strcmp(LocalName(attr.name()), local) == 0) return std::string(attr.value());
    }
    return std::string();
}

// The text of an element: every character-data child, in order. A w:t with
// xml:space="preserve" keeps its leading and trailing spaces, which is why
// the parts are parsed with parse_ws_pcdata.
std::string ElementText(const pugi::xml_node& node) {
    std::string text;
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
        }
    }
    return text;
}

// ---------------------------------------------------------------------------
// Run text. The reference reader (python-docx) translates a run's inner
// content elements: w:t keeps its characters, w:tab and w:ptab are a tab,
// w:cr and a text-wrapping w:br are a newline, w:noBreakHyphen is a dash, and
// a w:br of type "page" or "column" carries no text.
// ---------------------------------------------------------------------------

struct RunPiece {
    std::string text;
    bool page_break = false;
};

template <typename Visitor>
void VisitRunText(const pugi::xml_node& run_element, Visitor visit) {
    for (pugi::xml_node child = run_element.first_child(); child; child = child.next_sibling()) {
        if (IsElement(child, "t")) {
            RunPiece piece;
            piece.text = ElementText(child);
            visit(piece);
        } else if (IsElement(child, "tab") || IsElement(child, "ptab")) {
            RunPiece piece;
            piece.text = "\t";
            visit(piece);
        } else if (IsElement(child, "cr")) {
            RunPiece piece;
            piece.text = "\n";
            visit(piece);
        } else if (IsElement(child, "noBreakHyphen")) {
            RunPiece piece;
            piece.text = "-";
            visit(piece);
        } else if (IsElement(child, "br")) {
            const std::string type = LowerAscii(AttributeValue(child, "type"));
            RunPiece piece;
            if (type.empty() || type == "textwrapping") {
                piece.text = "\n";
            } else {
                piece.page_break = type == "page";
            }
            visit(piece);
        }
    }
}

std::string RunTextOf(const pugi::xml_node& run_element) {
    std::string text;
    VisitRunText(run_element, [&text](const RunPiece& piece) { text += piece.text; });
    return text;
}

// ---------------------------------------------------------------------------
// Run formatting. Only direct formatting counts, exactly like the reference
// reader: style-inherited formatting is not resolved.
// ---------------------------------------------------------------------------

bool ReadOnOff(const pugi::xml_node& element) {
    if (!element) return false;
    const std::string value = LowerAscii(AttributeValue(element, "val"));
    if (value.empty()) return true;   // <w:b/> and friends mean "on"
    if (value == "0" || value == "false" || value == "off") return false;
    return true;                      // "1", "true", "on" and unknown values
}

bool ReadUnderline(const pugi::xml_node& element) {
    if (!element) return false;
    return LowerAscii(AttributeValue(element, "val")) != "none";
}

bool IsMonoFontName(const std::string& name) {
    const std::string lowered = LowerAscii(TrimAscii(name));
    if (lowered.empty()) return false;
    static const char* const kMonoFonts[] = {
        "consolas",        "courier",          "courier new",    "lucida console",
        "menlo",           "monaco",           "cascadia mono",  "dejavu sans mono",
        "liberation mono", "monospace",
    };
    for (const char* font : kMonoFonts) {
        if (lowered == font) return true;
    }
    static const char* const kMonoFragments[] = {"mono", "courier", "consolas", "cascadia"};
    for (const char* fragment : kMonoFragments) {
        if (lowered.find(fragment) != std::string::npos) return true;
    }
    return false;
}

bool ReadMono(const pugi::xml_node& r_fonts) {
    if (!r_fonts) return false;
    static const char* const kFontAttributes[] = {"ascii", "hAnsi", "cs"};
    for (const char* attribute : kFontAttributes) {
        if (IsMonoFontName(AttributeValue(r_fonts, attribute))) return true;
    }
    return false;
}

Run ReadRun(const pugi::xml_node& run_element, const std::string& link) {
    Run run;
    run.link = link;
    const pugi::xml_node r_pr = ChildElement(run_element, "rPr");
    if (r_pr) {
        run.bold = ReadOnOff(ChildElement(r_pr, "b"));
        run.italic = ReadOnOff(ChildElement(r_pr, "i"));
        run.strike = ReadOnOff(ChildElement(r_pr, "strike"));
        run.underline = ReadUnderline(ChildElement(r_pr, "u"));
        run.mono = ReadMono(ChildElement(r_pr, "rFonts"));
    }
    return run;
}

bool RunHasFormatting(const Run& run) {
    return run.bold || run.italic || run.mono || run.strike || run.underline ||
           !run.link.empty();
}

// ---------------------------------------------------------------------------
// Package access
// ---------------------------------------------------------------------------

// A part bigger than this is treated as a broken package instead of being
// allocated; a 200-page document.xml is around 1 MB.
const mz_uint64 kMaxPartBytes = 512ull * 1024ull * 1024ull;

struct ZipReader {
    mz_zip_archive archive;
    bool open;

    ZipReader() : open(false) { std::memset(&archive, 0, sizeof(archive)); }
    ~ZipReader() {
        if (open) mz_zip_reader_end(&archive);
    }

    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
};

// Part names are case-preserving but compared case-insensitively, so the
// exact name is tried first and a case-insensitive scan second.
int LocatePart(mz_zip_archive& archive, const std::string& name) {
    mz_uint32 index = 0;
    if (mz_zip_reader_locate_file_v2(&archive, name.c_str(), nullptr, 0, &index)) {
        return static_cast<int>(index);
    }
    const std::string wanted = LowerAscii(name);
    const mz_uint total = mz_zip_reader_get_num_files(&archive);
    for (mz_uint file = 0; file < total; ++file) {
        const mz_uint needed = mz_zip_reader_get_filename(&archive, file, nullptr, 0);
        if (needed == 0 || needed > 4096) continue;
        std::vector<char> buffer(needed, '\0');
        mz_zip_reader_get_filename(&archive, file, buffer.data(), needed);
        if (LowerAscii(buffer.data()) == wanted) return static_cast<int>(file);
    }
    return -1;
}

// Reads one part. Returns false only when the part exists but cannot be read
// (corrupt, truncated, encrypted, or absurdly large); a missing part is
// reported through found.
bool ReadPart(mz_zip_archive& archive, const std::string& name, std::string& out,
              bool& found, std::string& error) {
    found = false;
    const int index = LocatePart(archive, name);
    if (index < 0) return true;
    found = true;

    mz_zip_archive_file_stat stat;
    std::memset(&stat, 0, sizeof(stat));
    if (!mz_zip_reader_file_stat(&archive, static_cast<mz_uint>(index), &stat)) {
        error = name + " could not be read from the package: the ZIP entry is corrupt";
        return false;
    }
    if (stat.m_uncomp_size > kMaxPartBytes) {
        error = name + " is too large to import (over 512 MB)";
        return false;
    }
    out.assign(static_cast<size_t>(stat.m_uncomp_size), '\0');
    if (!out.empty() &&
        !mz_zip_reader_extract_to_mem(&archive, static_cast<mz_uint>(index), &out[0],
                                      out.size(), 0)) {
        error = name +
                " could not be extracted from the package: the ZIP member is corrupt, "
                "truncated or encrypted";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// word/styles.xml, word/numbering.xml, word/_rels/document.xml.rels
// ---------------------------------------------------------------------------

// A w:numPr: present, with the level and the numId it refers to.
struct NumberingRef {
    bool present = false;
    int level = 0;
    std::string num_id;
};

struct StyleInfo {
    std::string name;       // w:name/@w:val, e.g. "heading 1"
    std::string based_on;   // w:basedOn/@w:val
    NumberingRef numbering;
};

struct ImportContext {
    std::map<std::string, StyleInfo> styles;                     // styleId -> style
    std::string default_paragraph_style;                         // w:default="1"
    std::map<std::string, std::string> targets;                  // rId -> target
    std::map<std::string, std::map<int, std::string>> numbering; // numId -> ilvl -> numFmt
};

void LoadStyles(const pugi::xml_node& root, ImportContext& context) {
    for (pugi::xml_node style = root.first_child(); style; style = style.next_sibling()) {
        if (!IsElement(style, "style")) continue;
        if (LowerAscii(AttributeValue(style, "type")) != "paragraph") continue;
        const std::string style_id = AttributeValue(style, "styleId");
        if (style_id.empty()) continue;

        StyleInfo info;
        const pugi::xml_node name = ChildElement(style, "name");
        if (name) info.name = AttributeValue(name, "val");
        const pugi::xml_node based_on = ChildElement(style, "basedOn");
        if (based_on) info.based_on = AttributeValue(based_on, "val");
        const pugi::xml_node num_pr = ChildElement(ChildElement(style, "pPr"), "numPr");
        if (num_pr) {
            info.numbering.present = true;
            const pugi::xml_node ilvl = ChildElement(num_pr, "ilvl");
            if (ilvl) ParseInt(AttributeValue(ilvl, "val"), info.numbering.level);
            const pugi::xml_node num_id = ChildElement(num_pr, "numId");
            if (num_id) info.numbering.num_id = AttributeValue(num_id, "val");
        }
        context.styles[style_id] = info;
        if (context.default_paragraph_style.empty() &&
            AttributeValue(style, "default") == "1") {
            context.default_paragraph_style = style_id;
        }
    }
}

void LoadNumbering(const pugi::xml_node& root, ImportContext& context) {
    std::map<std::string, std::map<int, std::string>> abstract_levels;
    for (pugi::xml_node abstract_num = root.first_child(); abstract_num;
         abstract_num = abstract_num.next_sibling()) {
        if (!IsElement(abstract_num, "abstractNum")) continue;
        const std::string abstract_id = AttributeValue(abstract_num, "abstractNumId");
        if (abstract_id.empty()) continue;
        std::map<int, std::string> levels;
        for (pugi::xml_node level = abstract_num.first_child(); level;
             level = level.next_sibling()) {
            if (!IsElement(level, "lvl")) continue;
            int ilvl = 0;
            ParseInt(AttributeValue(level, "ilvl"), ilvl);
            const pugi::xml_node format = ChildElement(level, "numFmt");
            // A level without w:numFmt is kept with an empty format: the
            // reference reader then treats it as ordered instead of falling
            // back to level 0.
            levels[ilvl] = format ? AttributeValue(format, "val") : std::string();
        }
        abstract_levels[abstract_id] = levels;
    }
    for (pugi::xml_node num = root.first_child(); num; num = num.next_sibling()) {
        if (!IsElement(num, "num")) continue;
        const std::string num_id = AttributeValue(num, "numId");
        if (num_id.empty()) continue;
        const pugi::xml_node reference = ChildElement(num, "abstractNumId");
        if (!reference) continue;
        const std::string abstract_id = AttributeValue(reference, "val");
        auto found = abstract_levels.find(abstract_id);
        context.numbering[num_id] =
            found == abstract_levels.end() ? std::map<int, std::string>() : found->second;
    }
}

void LoadTargets(const pugi::xml_node& root, ImportContext& context) {
    for (pugi::xml_node relationship = root.first_child(); relationship;
         relationship = relationship.next_sibling()) {
        if (!IsElement(relationship, "Relationship")) continue;
        const std::string id = AttributeValue(relationship, "Id");
        if (id.empty()) continue;
        context.targets[id] = AttributeValue(relationship, "Target");
    }
}

// ---------------------------------------------------------------------------
// Paragraph classification
// ---------------------------------------------------------------------------

// "heading 1" is the name Word writes, "Heading1" the usual styleId.
int HeadingLevelFromName(const std::string& name) {
    static const char kPrefix[] = "heading";
    const std::string lowered = LowerAscii(TrimAscii(name));
    if (lowered.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return 0;
    const std::string rest = TrimAscii(lowered.substr(sizeof(kPrefix) - 1));
    if (rest.empty()) return 0;
    for (char ch : rest) {
        if (!IsDigitAscii(ch)) return 0;
    }
    int level = 0;
    if (!ParseInt(rest, level) || level < 1) return 0;
    return level > 9 ? 9 : level;
}

struct StyleClass {
    enum Kind { Paragraph, Heading, Title };
    Kind kind = Paragraph;
    int level = 0;
};

// The style name decides the block kind, like the reference reader: a style
// named "Title" is a title, "heading N" (or the styleId "HeadingN") is a
// heading, everything else is body text. Inherited style names do not count.
StyleClass ClassifyStyle(const ImportContext& context, const std::string& style_id) {
    StyleClass result;
    const std::string resolved =
        style_id.empty() ? context.default_paragraph_style : style_id;
    auto found = context.styles.find(resolved);
    const std::string name = found == context.styles.end() ? std::string() : found->second.name;

    if (LowerAscii(TrimAscii(name)) == "title") {
        result.kind = StyleClass::Title;
        return result;
    }
    int level = HeadingLevelFromName(name);
    if (level == 0) level = HeadingLevelFromName(resolved);
    if (level > 0) {
        result.kind = StyleClass::Heading;
        result.level = level;
    }
    return result;
}

std::string ParagraphStyleId(const pugi::xml_node& paragraph) {
    const pugi::xml_node p_pr = ChildElement(paragraph, "pPr");
    const pugi::xml_node p_style = ChildElement(p_pr, "pStyle");
    return p_style ? AttributeValue(p_style, "val") : std::string();
}

NumberingRef DirectNumbering(const pugi::xml_node& p_pr) {
    NumberingRef reference;
    const pugi::xml_node num_pr = ChildElement(p_pr, "numPr");
    if (!num_pr) return reference;
    reference.present = true;
    const pugi::xml_node ilvl = ChildElement(num_pr, "ilvl");
    if (ilvl) ParseInt(AttributeValue(ilvl, "val"), reference.level);
    const pugi::xml_node num_id = ChildElement(num_pr, "numId");
    if (num_id) reference.num_id = AttributeValue(num_id, "val");
    return reference;
}

// Numbering of a paragraph: its own w:numPr, otherwise the w:numPr of its
// style chain (up to eight steps, like the reference reader). A paragraph
// with w:numId 0 has no numbering.
bool ResolveNumbering(const ImportContext& context, const pugi::xml_node& paragraph,
                      const std::string& style_id, int& level, std::string& num_fmt) {
    NumberingRef reference = DirectNumbering(ChildElement(paragraph, "pPr"));
    if (!reference.present) {
        std::string current = style_id.empty() ? context.default_paragraph_style : style_id;
        for (int depth = 0; depth < 8 && !current.empty(); ++depth) {
            auto found = context.styles.find(current);
            if (found == context.styles.end()) break;
            if (found->second.numbering.present) {
                reference = found->second.numbering;
                break;
            }
            current = found->second.based_on;
        }
    }
    if (!reference.present) return false;
    if (reference.num_id.empty() || reference.num_id == "0") return false;

    level = reference.level < 0 ? 0 : reference.level;
    num_fmt.clear();
    auto numbering = context.numbering.find(reference.num_id);
    if (numbering != context.numbering.end()) {
        auto exact = numbering->second.find(level);
        if (exact != numbering->second.end()) {
            num_fmt = exact->second;
        } else {
            auto first = numbering->second.find(0);
            if (first != numbering->second.end()) num_fmt = first->second;
        }
    }
    return true;
}

struct ParagraphShape {
    BlockKind kind = Paragraph;
    int level = 0;         // heading level, or list level for a list item
    bool ordered = false;  // list items only
};

ParagraphShape ClassifyParagraph(const ImportContext& context,
                                 const pugi::xml_node& paragraph) {
    const std::string style_id = ParagraphStyleId(paragraph);
    const StyleClass style = ClassifyStyle(context, style_id);

    ParagraphShape shape;
    int list_level = 0;
    std::string num_fmt;
    if (ResolveNumbering(context, paragraph, style_id, list_level, num_fmt)) {
        shape.kind = ListItem;
        shape.level = list_level;
        shape.ordered = LowerAscii(num_fmt) != "bullet";
        return shape;
    }
    if (style.kind == StyleClass::Heading) {
        shape.kind = Heading;
        shape.level = style.level;
    } else if (style.kind == StyleClass::Title) {
        shape.kind = Title;
    }
    return shape;
}

// ---------------------------------------------------------------------------
// Page setup
// ---------------------------------------------------------------------------

void ReadPageSetup(const pugi::xml_node& sect_pr, PageSetup& page) {
    const pugi::xml_node page_size = ChildElement(sect_pr, "pgSz");
    if (page_size) {
        double width_twips = 0.0;
        double height_twips = 0.0;
        if (ParseDouble(AttributeValue(page_size, "w"), width_twips) && width_twips > 0.0) {
            page.width_pt = width_twips / 20.0;
        }
        if (ParseDouble(AttributeValue(page_size, "h"), height_twips) && height_twips > 0.0) {
            page.height_pt = height_twips / 20.0;
        }
        page.landscape = page.width_pt > page.height_pt;
    }
    const pugi::xml_node margins = ChildElement(sect_pr, "pgMar");
    if (margins) {
        double left_twips = 0.0;
        if (ParseDouble(AttributeValue(margins, "left"), left_twips) && left_twips >= 0.0) {
            page.margin_pt = left_twips / 20.0;
        }
    }
}

// ---------------------------------------------------------------------------
// Compatibility report: one warning per unsupported feature that is actually
// present in the file, and no warning at all for the features that are not.
// ---------------------------------------------------------------------------

struct FeatureScan {
    int images = 0;        // w:drawing, w:pict, v:imagedata
    int text_boxes = 0;    // w:txbxContent
    int footnotes = 0;     // w:footnoteReference
    int endnotes = 0;      // w:endnoteReference
    int comments = 0;      // w:commentReference
    int tracked = 0;       // w:ins, w:del
    int header_refs = 0;   // w:headerReference
    int footer_refs = 0;   // w:footerReference
    int toc_fields = 0;    // a TOC field instruction
    int equations = 0;     // m:oMath, m:oMathPara
};

bool ContainsTocInstruction(const std::string& instruction) {
    const std::string lowered = LowerAscii(instruction);
    const std::string needle = "toc";
    size_t position = lowered.find(needle);
    while (position != std::string::npos) {
        const size_t after = position + needle.size();
        const bool left_ok = position == 0 || !IsAlnumAscii(lowered[position - 1]);
        const bool right_ok = after >= lowered.size() || !IsAlnumAscii(lowered[after]);
        if (left_ok && right_ok) return true;
        position = lowered.find(needle, position + 1);
    }
    return false;
}

// Walks the whole part once, with an explicit stack: an imported document can
// be large and deep, and this must never overflow the C++ stack.
void ScanFeatures(const pugi::xml_node& root, FeatureScan& scan) {
    std::vector<pugi::xml_node> pending;
    for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
        pending.push_back(child);
    }
    while (!pending.empty()) {
        const pugi::xml_node node = pending.back();
        pending.pop_back();
        if (node.type() != pugi::node_element) continue;

        const char* local = LocalName(node.name());
        if (std::strcmp(local, "drawing") == 0 || std::strcmp(local, "pict") == 0 ||
            std::strcmp(local, "imagedata") == 0) {
            ++scan.images;
        } else if (std::strcmp(local, "txbxContent") == 0 || std::strcmp(local, "txbx") == 0) {
            ++scan.text_boxes;
        } else if (std::strcmp(local, "footnoteReference") == 0) {
            ++scan.footnotes;
        } else if (std::strcmp(local, "endnoteReference") == 0) {
            ++scan.endnotes;
        } else if (std::strcmp(local, "commentReference") == 0) {
            ++scan.comments;
        } else if (std::strcmp(local, "ins") == 0 || std::strcmp(local, "del") == 0) {
            ++scan.tracked;
        } else if (std::strcmp(local, "headerReference") == 0) {
            ++scan.header_refs;
        } else if (std::strcmp(local, "footerReference") == 0) {
            ++scan.footer_refs;
        } else if (std::strcmp(local, "oMath") == 0 || std::strcmp(local, "oMathPara") == 0) {
            ++scan.equations;
        } else if (std::strcmp(local, "fldSimple") == 0) {
            if (ContainsTocInstruction(AttributeValue(node, "instr"))) ++scan.toc_fields;
        } else if (std::strcmp(local, "instrText") == 0) {
            if (ContainsTocInstruction(ElementText(node))) ++scan.toc_fields;
        }
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
            pending.push_back(child);
        }
    }
}

std::string CountText(int count, const char* singular, const char* plural) {
    std::string text = std::to_string(count);
    text += " ";
    text += count == 1 ? singular : plural;
    return text;
}

void AppendWarning(CompatReport& report, const char* feature, const std::string& detail) {
    CompatWarning warning;
    warning.feature = feature;
    warning.detail = detail;
    report.warnings.push_back(warning);
}

void AppendFeatureWarnings(const FeatureScan& scan, CompatReport& report) {
    if (scan.images > 0) {
        AppendWarning(report, "image",
                      CountText(scan.images, "drawing or picture", "drawings or pictures") +
                          " in the document body; images are not carried by the model");
    }
    if (scan.text_boxes > 0) {
        AppendWarning(report, "text box",
                      CountText(scan.text_boxes, "text box", "text boxes") +
                          " in the document body; text boxes are not carried by the model");
    }
    if (scan.footnotes > 0) {
        AppendWarning(report, "footnote",
                      CountText(scan.footnotes, "footnote reference", "footnote references") +
                          " in the document body; footnotes are not carried by the model");
    }
    if (scan.endnotes > 0) {
        AppendWarning(report, "endnote",
                      CountText(scan.endnotes, "endnote reference", "endnote references") +
                          " in the document body; endnotes are not carried by the model");
    }
    if (scan.comments > 0) {
        AppendWarning(report, "comment",
                      CountText(scan.comments, "comment reference", "comment references") +
                          " in the document body; comments are not carried by the model");
    }
    if (scan.tracked > 0) {
        AppendWarning(report, "tracked changes",
                      CountText(scan.tracked, "tracked insertion or deletion",
                                "tracked insertions or deletions") +
                          " in the document body; revision markup is not carried by the model");
    }
    if (scan.header_refs > 0 || scan.footer_refs > 0) {
        std::string detail;
        if (scan.header_refs > 0) {
            detail = CountText(scan.header_refs, "header reference", "header references");
        }
        if (scan.footer_refs > 0) {
            if (!detail.empty()) detail += " and ";
            detail += CountText(scan.footer_refs, "footer reference", "footer references");
        }
        detail += " in the section; headers and footers are not carried by the model";
        AppendWarning(report, "header/footer", detail);
    }
    if (scan.toc_fields > 0) {
        AppendWarning(report, "table of contents",
                      CountText(scan.toc_fields, "table of contents field",
                                "table of contents fields") +
                          " in the document body; the field is not carried by the model");
    }
    if (scan.equations > 0) {
        AppendWarning(report, "equation",
                      CountText(scan.equations, "equation", "equations") +
                          " in the document body; equations are not carried by the model");
    }
}

// ---------------------------------------------------------------------------
// Body reading
// ---------------------------------------------------------------------------

class BodyReader;

// Turns one w:p into one or more blocks. A paragraph is split at every
// explicit page break: the runs before the break form a block, the break
// becomes a PageBreak block, and the runs after it form another block.
class ParagraphWriter {
public:
    ParagraphWriter(BodyReader& reader, const ParagraphShape& shape);

    // One w:r. link is empty for a plain run and the relationship target for
    // a run inside w:hyperlink.
    void AppendRun(const pugi::xml_node& run_element, const std::string& link);

    void Finish();

private:
    void StartRun(const Run& run);
    void FlushRun();
    void EmitPageBreak();
    void EmitBlock();

    BodyReader& reader_;
    ParagraphShape shape_;
    std::vector<Run> runs_;
    Run current_;
    bool pending_ = false;
    bool emitted_any_block_ = false;
};

class BodyReader {
public:
    BodyReader(const ImportContext& context, DocModel& model)
        : context_(context), model_(model) {}

    void ReadBody(const pugi::xml_node& body) {
        for (pugi::xml_node child = body.first_child(); child; child = child.next_sibling()) {
            if (IsElement(child, "p")) {
                ReadParagraph(child);
            } else if (IsElement(child, "tbl")) {
                ReadTable(child);
            }
        }
    }

    void AddBlock(const Block& block) {
        model_.blocks.push_back(block);
        if (block.kind == Title && model_.title.empty()) {
            std::string text;
            for (const Run& run : block.runs) text += run.text;
            model_.title = text;
        }
    }

private:
    void ReadParagraph(const pugi::xml_node& paragraph) {
        const ParagraphShape shape = ClassifyParagraph(context_, paragraph);
        ParagraphWriter writer(*this, shape);
        // Only the direct w:r and w:hyperlink children carry runs, exactly
        // like the reference reader. Runs wrapped in w:ins (tracked
        // insertions), w:smartTag or w:sdt are not imported; the
        // compatibility report calls tracked changes out.
        for (pugi::xml_node child = paragraph.first_child(); child;
             child = child.next_sibling()) {
            if (IsElement(child, "r")) {
                writer.AppendRun(child, std::string());
            } else if (IsElement(child, "hyperlink")) {
                // The link target comes from the document relationships; the
                // run keeps its own direct formatting, so an underlined link
                // run stays underlined.
                const std::string target = LookupTarget(AttributeValue(child, "id"));
                for (pugi::xml_node provider = child.first_child(); provider;
                     provider = provider.next_sibling()) {
                    if (IsElement(provider, "r")) writer.AppendRun(provider, target);
                }
            }
        }
        writer.Finish();
    }

    // A table cell reports text only, like the reference reader: the model
    // carries no cell formatting.
    void ReadTable(const pugi::xml_node& table) {
        Block block;
        block.kind = Table;
        for (pugi::xml_node row = table.first_child(); row; row = row.next_sibling()) {
            if (!IsElement(row, "tr")) continue;
            std::vector<Cell> cells;
            for (pugi::xml_node cell = row.first_child(); cell; cell = cell.next_sibling()) {
                if (!IsElement(cell, "tc")) continue;
                Cell out_cell;
                Run run;
                run.text = CellText(cell);
                out_cell.runs.push_back(run);
                cells.push_back(out_cell);
            }
            block.rows.push_back(cells);
        }
        AddBlock(block);
    }

    std::string LookupTarget(const std::string& id) const {
        auto found = context_.targets.find(id);
        return found == context_.targets.end() ? std::string() : found->second;
    }

    static std::string CellText(const pugi::xml_node& cell) {
        std::string text;
        bool first = true;
        for (pugi::xml_node paragraph = cell.first_child(); paragraph;
             paragraph = paragraph.next_sibling()) {
            if (!IsElement(paragraph, "p")) continue;
            if (!first) text += "\n";
            first = false;
            text += ParagraphText(paragraph);
        }
        return text;
    }

    static std::string ParagraphText(const pugi::xml_node& paragraph) {
        std::string text;
        for (pugi::xml_node child = paragraph.first_child(); child;
             child = child.next_sibling()) {
            if (IsElement(child, "r")) {
                text += RunTextOf(child);
            } else if (IsElement(child, "hyperlink")) {
                for (pugi::xml_node provider = child.first_child(); provider;
                     provider = provider.next_sibling()) {
                    if (IsElement(provider, "r")) text += RunTextOf(provider);
                }
            }
        }
        return text;
    }

    const ImportContext& context_;
    DocModel& model_;
};

ParagraphWriter::ParagraphWriter(BodyReader& reader, const ParagraphShape& shape)
    : reader_(reader), shape_(shape) {}

void ParagraphWriter::AppendRun(const pugi::xml_node& run_element, const std::string& link) {
    const Run run = ReadRun(run_element, link);
    StartRun(run);
    VisitRunText(run_element, [this, &run](const RunPiece& piece) {
        if (piece.page_break) {
            // The run can hold text on both sides of the break, so it is
            // started again for the block after it, with the same formatting.
            EmitPageBreak();
            StartRun(run);
        } else {
            current_.text += piece.text;
        }
    });
}

void ParagraphWriter::Finish() {
    FlushRun();
    // Every w:p is a block, even an empty one; a paragraph that only held a
    // page break leaves the PageBreak block behind and nothing else.
    if (!runs_.empty() || !emitted_any_block_) EmitBlock();
}

void ParagraphWriter::StartRun(const Run& run) {
    FlushRun();
    current_ = run;
    pending_ = true;
}

void ParagraphWriter::FlushRun() {
    if (!pending_) return;
    pending_ = false;
    // An empty run without formatting carries nothing; it is dropped instead
    // of becoming an empty run in the model.
    if (!current_.text.empty() || RunHasFormatting(current_)) runs_.push_back(current_);
    current_ = Run();
}

void ParagraphWriter::EmitPageBreak() {
    FlushRun();
    if (!runs_.empty()) EmitBlock();
    Block page_break;
    page_break.kind = PageBreak;
    reader_.AddBlock(page_break);
    emitted_any_block_ = true;
}

void ParagraphWriter::EmitBlock() {
    Block block;
    block.kind = shape_.kind;
    if (shape_.kind == Heading || shape_.kind == ListItem) block.level = shape_.level;
    if (shape_.kind == ListItem) block.ordered = shape_.ordered;
    block.runs = runs_;
    runs_.clear();
    reader_.AddBlock(block);
    emitted_any_block_ = true;
}

}  // namespace

bool DocxImport(const std::string& bytes, DocModel& out, CompatReport& report,
                std::string& error) {
    out = DocModel();
    report = CompatReport();
    error.clear();

    if (bytes.empty()) {
        error = "empty input: not a Word .docx package";
        return false;
    }

    // A compound file (CFB) is either a legacy binary .doc or an encrypted
    // OOXML package, which is a CFB container holding an EncryptedPackage
    // stream. Neither can be read here, so say which one it is.
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(bytes.data());
    if (bytes.size() >= 8 && raw[0] == 0xD0 && raw[1] == 0xCF && raw[2] == 0x11 &&
        raw[3] == 0xE0) {
        error =
            "not a Word .docx package: the file is an OLE/CFB container, so it is a legacy "
            "Word .doc (binary) document or a password-protected (encrypted) OOXML file; "
            "MarkDownIt reads unencrypted .docx packages only";
        return false;
    }

    ZipReader package;
    if (!mz_zip_reader_init_mem(&package.archive, bytes.data(), bytes.size(), 0)) {
        error =
            "not a Word .docx package: the ZIP directory could not be read (the file is not "
            "a ZIP container, or it is corrupt or truncated)";
        return false;
    }
    package.open = true;

    std::string document_xml;
    bool found = false;
    if (!ReadPart(package.archive, "word/document.xml", document_xml, found, error)) {
        return false;
    }
    if (!found) {
        error = "not a Word .docx package: the archive has no word/document.xml part";
        return false;
    }

    pugi::xml_document document;
    // parse_ws_pcdata keeps whitespace-only text nodes, so a run whose text is
    // a single space survives the import.
    const pugi::xml_parse_result parsed = document.load_buffer(
        document_xml.data(), document_xml.size(),
        pugi::parse_default | pugi::parse_ws_pcdata, pugi::encoding_auto);
    if (!parsed) {
        error = "word/document.xml is not parseable XML: ";
        error += parsed.description();
        error += " at byte offset ";
        error += std::to_string(static_cast<long long>(parsed.offset));
        return false;
    }

    const pugi::xml_node body = ChildElement(ChildElement(document, "document"), "body");
    if (!body) {
        error = "word/document.xml has no w:body element";
        return false;
    }

    // The supporting parts are optional: a package without styles, numbering
    // or relationships still imports, it just carries less information. A
    // part that cannot be read or parsed is skipped for the same reason.
    ImportContext context;
    {
        std::string styles_xml;
        bool styles_found = false;
        if (ReadPart(package.archive, "word/styles.xml", styles_xml, styles_found, error)) {
            if (styles_found) {
                pugi::xml_document styles;
                if (styles.load_buffer(styles_xml.data(), styles_xml.size(),
                                       pugi::parse_default | pugi::parse_ws_pcdata,
                                       pugi::encoding_auto)) {
                    LoadStyles(RootElement(styles), context);
                }
            }
        } else {
            error.clear();
        }
    }
    {
        std::string numbering_xml;
        bool numbering_found = false;
        if (ReadPart(package.archive, "word/numbering.xml", numbering_xml, numbering_found,
                     error)) {
            if (numbering_found) {
                pugi::xml_document numbering;
                if (numbering.load_buffer(numbering_xml.data(), numbering_xml.size(),
                                          pugi::parse_default | pugi::parse_ws_pcdata,
                                          pugi::encoding_auto)) {
                    LoadNumbering(RootElement(numbering), context);
                }
            }
        } else {
            error.clear();
        }
    }
    {
        std::string rels_xml;
        bool rels_found = false;
        if (ReadPart(package.archive, "word/_rels/document.xml.rels", rels_xml, rels_found,
                     error)) {
            if (rels_found) {
                pugi::xml_document rels;
                if (rels.load_buffer(rels_xml.data(), rels_xml.size(),
                                     pugi::parse_default | pugi::parse_ws_pcdata,
                                     pugi::encoding_auto)) {
                    LoadTargets(RootElement(rels), context);
                }
            }
        } else {
            error.clear();
        }
    }

    BodyReader reader(context, out);
    reader.ReadBody(body);

    const pugi::xml_node sect_pr = ChildElement(body, "sectPr");
    if (sect_pr) ReadPageSetup(sect_pr, out.page);

    FeatureScan scan;
    ScanFeatures(document, scan);
    AppendFeatureWarnings(scan, report);
    return true;
}

}  // namespace office
