// The layout dump of the port alignment plan, Batch 5 / "Layout dump
// parity". It lays out every fixture file in tests/fixtures/parity/
// with the fixed-width fake text engine (tests/fake_text_engine.h) and
// writes a canonical JSON dump: block order, rectangles, line breaks,
// table column widths, list marker positions and the display list from
// Batch 3.
//
// The same fake engine measures identically on both platforms, so the
// dump is byte for byte the same whether this binary was built by
// MSVC or by Xcode. Two things read the output:
//   - tools/check_layout_parity.py compares a platform's own dump
//     against the committed golden in tests/layout-golden/, so a
//     failure names the platform that moved.
//   - The same script compares the Windows dump against the macOS
//     dump directly, the backstop if a golden was updated wrongly.
//
// Goldens are written only by tools/update_layout_goldens.py, never
// by hand (references/platform-parity.md, AGENTS.md: the same rule
// the Office goldens follow).

#include "fake_text_engine.h"
#include "blockspacing.h"
#include "layout/document_layout.h"
#include "parser.h"
#include "theme.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

std::string Json(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 32) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(c)
                        << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

// Six decimal digits: enough to tell a real layout drift from binary
// float noise, never enough to make rounding itself the diff.
std::string Num(float v) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << v;
    return out.str();
}

void WriteFont(std::ostream& out, const platform::FontSpec& f) {
    out << "{\"family\":" << Json(f.family) << ",\"size\":" << Num(f.size)
        << ",\"bold\":" << (f.bold ? "true" : "false")
        << ",\"italic\":" << (f.italic ? "true" : "false")
        << ",\"monospace\":" << (f.monospace ? "true" : "false") << '}';
}

void WriteRun(std::ostream& out, const platform::GlyphRun& r) {
    out << "{\"text\":" << Json(r.text) << ",\"font\":";
    WriteFont(out, r.font);
    out << ",\"r\":" << Num(r.r) << ",\"g\":" << Num(r.g)
        << ",\"b\":" << Num(r.b) << ",\"a\":" << Num(r.a)
        << ",\"x\":" << Num(r.x)
        << ",\"underline\":" << (r.underline ? "true" : "false")
        << ",\"strike\":" << (r.strike ? "true" : "false") << '}';
}

void WriteLine(std::ostream& out, const platform::TextLine& line) {
    out << "{\"width\":" << Num(line.width) << ",\"height\":"
        << Num(line.height) << ",\"ascent\":" << Num(line.ascent)
        << ",\"runs\":[";
    for (std::size_t i = 0; i < line.runs.size(); ++i) {
        if (i) out << ',';
        WriteRun(out, line.runs[i]);
    }
    out << "]}";
}

void WriteColor(std::ostream& out, const display::Color& c) {
    out << "{\"r\":" << Num(c.r) << ",\"g\":" << Num(c.g) << ",\"b\":"
        << Num(c.b) << ",\"a\":" << Num(c.a) << '}';
}

void WriteRect(std::ostream& out, const display::Rect& r) {
    out << "{\"x\":" << Num(r.x) << ",\"y\":" << Num(r.y) << ",\"width\":"
        << Num(r.width) << ",\"height\":" << Num(r.height) << '}';
}

void WritePoints(std::ostream& out, const std::vector<display::Point>& pts) {
    out << '[';
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (i) out << ',';
        out << "[" << Num(pts[i].x) << ',' << Num(pts[i].y) << ']';
    }
    out << ']';
}

// One display list item, named by kind so a diff reads like a sentence
// instead of a positional guess.
struct ItemWriter {
    std::ostream& out;

    void operator()(const display::FillRect& v) const {
        out << "{\"kind\":\"FillRect\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"color\":";
        WriteColor(out, v.color);
        out << '}';
    }
    void operator()(const display::FillRoundedRect& v) const {
        out << "{\"kind\":\"FillRoundedRect\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"radius\":" << Num(v.radius) << ",\"color\":";
        WriteColor(out, v.color);
        out << '}';
    }
    void operator()(const display::FillRoundedRectSides& v) const {
        out << "{\"kind\":\"FillRoundedRectSides\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"radius\":" << Num(v.radius) << ",\"color\":";
        WriteColor(out, v.color);
        out << ",\"roundLeft\":" << (v.roundLeft ? "true" : "false")
            << ",\"roundRight\":" << (v.roundRight ? "true" : "false")
            << '}';
    }
    void operator()(const display::StrokeRect& v) const {
        out << "{\"kind\":\"StrokeRect\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"color\":";
        WriteColor(out, v.color);
        out << ",\"lineWidth\":" << Num(v.lineWidth) << '}';
    }
    void operator()(const display::StrokeRoundedRect& v) const {
        out << "{\"kind\":\"StrokeRoundedRect\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"radius\":" << Num(v.radius) << ",\"color\":";
        WriteColor(out, v.color);
        out << ",\"lineWidth\":" << Num(v.lineWidth) << '}';
    }
    void operator()(const display::StrokeLine& v) const {
        out << "{\"kind\":\"StrokeLine\",\"x0\":" << Num(v.x0) << ",\"y0\":"
            << Num(v.y0) << ",\"x1\":" << Num(v.x1) << ",\"y1\":"
            << Num(v.y1) << ",\"color\":";
        WriteColor(out, v.color);
        out << ",\"lineWidth\":" << Num(v.lineWidth) << ",\"dash\":[";
        for (std::size_t i = 0; i < v.dash.size(); ++i) {
            if (i) out << ',';
            out << Num(v.dash[i]);
        }
        out << "]}";
    }
    void operator()(const display::StrokePolygon& v) const {
        out << "{\"kind\":\"StrokePolygon\",\"points\":";
        WritePoints(out, v.points);
        out << ",\"color\":";
        WriteColor(out, v.color);
        out << ",\"lineWidth\":" << Num(v.lineWidth) << ",\"closed\":"
            << (v.closed ? "true" : "false") << ",\"dash\":[";
        for (std::size_t i = 0; i < v.dash.size(); ++i) {
            if (i) out << ',';
            out << Num(v.dash[i]);
        }
        out << "]}";
    }
    void operator()(const display::FillEllipse& v) const {
        out << "{\"kind\":\"FillEllipse\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"color\":";
        WriteColor(out, v.color);
        out << '}';
    }
    void operator()(const display::StrokeEllipse& v) const {
        out << "{\"kind\":\"StrokeEllipse\",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"color\":";
        WriteColor(out, v.color);
        out << ",\"lineWidth\":" << Num(v.lineWidth) << '}';
    }
    void operator()(const display::FillPolygon& v) const {
        out << "{\"kind\":\"FillPolygon\",\"points\":";
        WritePoints(out, v.points);
        out << ",\"color\":";
        WriteColor(out, v.color);
        out << '}';
    }
    void operator()(const display::PieSlice& v) const {
        out << "{\"kind\":\"PieSlice\",\"bounds\":";
        WriteRect(out, v.bounds);
        out << ",\"startAngle\":" << Num(v.startAngle) << ",\"endAngle\":"
            << Num(v.endAngle) << ",\"color\":";
        WriteColor(out, v.color);
        out << '}';
    }
    void operator()(const display::Text& v) const {
        out << "{\"kind\":\"Text\",\"x\":" << Num(v.x) << ",\"top\":"
            << Num(v.top) << ",\"line\":";
        WriteLine(out, v.line);
        out << ",\"srcOffset\":" << v.srcOffset << ",\"srcLength\":"
            << v.srcLength << '}';
    }
    void operator()(const display::Image& v) const {
        out << "{\"kind\":\"Image\",\"url\":" << Json(v.url)
            << ",\"imageKind\":"
            << (v.kind == platform::ImageKind::Vector ? "\"Vector\""
                                                       : "\"Raster\"")
            << ",\"rect\":";
        WriteRect(out, v.rect);
        out << ",\"naturalWidth\":" << Num(v.naturalWidth)
            << ",\"naturalHeight\":" << Num(v.naturalHeight) << '}';
    }
    void operator()(const display::PushClip& v) const {
        out << "{\"kind\":\"PushClip\",\"rect\":";
        WriteRect(out, v.rect);
        out << '}';
    }
    void operator()(const display::PopClip&) const {
        out << "{\"kind\":\"PopClip\"}";
    }
};

void WriteTable(std::ostream& out, const layout::LaidTable& t) {
    out << "{\"colW\":" << Num(t.colW) << ",\"columnCount\":"
        << t.columnCount << ",\"colWs\":[";
    for (std::size_t i = 0; i < t.colWs.size(); ++i) {
        if (i) out << ',';
        out << Num(t.colWs[i]);
    }
    out << "],\"colXs\":[";
    for (std::size_t i = 0; i < t.colXs.size(); ++i) {
        if (i) out << ',';
        out << Num(t.colXs[i]);
    }
    out << "],\"rowHeights\":[";
    for (std::size_t i = 0; i < t.rowHeights.size(); ++i) {
        if (i) out << ',';
        out << Num(t.rowHeights[i]);
    }
    out << "]}";
}

void WriteBlock(std::ostream& out, const layout::LaidBlock& b) {
    out << "{\"kind\":" << static_cast<int>(b.kind) << ",\"level\":"
        << b.level << ",\"x\":" << Num(b.x) << ",\"y\":" << Num(b.y)
        << ",\"width\":" << Num(b.width) << ",\"height\":" << Num(b.height)
        << ",\"nodeIndex\":" << b.nodeIndex << ",\"srcOffset\":"
        << b.srcOffset << ",\"srcLength\":" << b.srcLength
        << ",\"markerText\":" << Json(b.markerText) << ",\"markerWidth\":"
        << Num(b.markerWidth) << ",\"task\":" << (b.task ? "true" : "false")
        << ",\"imagesHeight\":" << Num(b.imagesHeight) << ",\"imageUrls\":[";
    for (std::size_t i = 0; i < b.imageUrls.size(); ++i) {
        if (i) out << ',';
        out << Json(b.imageUrls[i]);
    }
    out << "],\"table\":";
    if (b.table) {
        WriteTable(out, *b.table);
    } else {
        out << "null";
    }
    out << '}';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: layout-parity OUTPUT.json INPUT.md "
                     "[INPUT.md ...]\n";
        return 2;
    }
    try {
        std::ofstream out(argv[1], std::ios::binary);
        if (!out) throw std::runtime_error("Cannot create layout dump");
        out.imbue(std::locale::classic());

        fake::FakeTextEngine engine;
        const LayoutMetrics metrics = BaseMetrics(MarkdownStyle::Apple);
        const Palette palette = BasePalette(MarkdownStyle::Apple);

        out << "{\"schema\":1,\"engine\":\"fake-8px\",\"style\":\"apple\","
               "\"contentWidth\":600,\"cases\":[";
        bool firstCase = true;
        std::size_t cases = 0, blocks = 0, items = 0;
        for (int i = 2; i < argc; ++i) {
            std::ifstream input(argv[i], std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open fixture");
            const std::string md((std::istreambuf_iterator<char>(input)),
                                  {});
            Document doc;
            if (!ParseMarkdown(md, doc)) {
                throw std::runtime_error("Cannot parse fixture");
            }

            layout::LayoutInput in;
            in.doc = &doc;
            in.metrics = &metrics;
            in.palette = &palette;
            in.contentWidth = 600.0f;
            in.zoom = 1.0f;
            in.editing = false;
            in.engine = &engine;
            layout::DocumentLayout laid = layout::LayoutDocument(in);

            if (!firstCase) out << ',';
            firstCase = false;
            out << "{\"file\":"
                << Json(std::filesystem::path(argv[i]).filename().string())
                << ",\"listWidth\":" << Num(laid.list.width)
                << ",\"listHeight\":" << Num(laid.list.height)
                << ",\"blocks\":[";
            for (std::size_t b = 0; b < laid.blocks.size(); ++b) {
                if (b) out << ',';
                WriteBlock(out, laid.blocks[b]);
            }
            out << "],\"items\":[";
            for (std::size_t it = 0; it < laid.list.items.size(); ++it) {
                if (it) out << ',';
                std::visit(ItemWriter{out}, laid.list.items[it]);
            }
            out << "]}";
            ++cases;
            blocks += laid.blocks.size();
            items += laid.list.items.size();
        }
        out << "]}\n";
        out.close();
        if (!out) throw std::runtime_error("Cannot finish layout dump");
        std::cout << cases << " fixtures, " << blocks << " blocks, "
                  << items << " display items\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
