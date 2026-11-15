// How long a table document takes to lay out, and how big it gets, for the
// row cap in src/csv.h. Build with the fake engine so the number is the
// layout's own cost and not a platform text stack's.
//
//   g++ -std=c++17 -O2 -I tests -I src -I third_party/md4c
//       -I third_party/pugixml -o probe_csv_layout tools/probe_csv_layout.cpp
//       src/csv.cpp src/csvdocument.cpp src/parser.cpp src/documentfile.cpp
//       src/layout/*.cpp src/textlayout_generic.cpp src/layoutcache.cpp
//       src/editcontroller.cpp src/textbuffer.cpp src/formatting.cpp
//       src/autoformat.cpp src/undostack.cpp src/inputfilter.cpp
//       src/navigation.cpp src/mermaid/*.cpp third_party/pugixml/pugixml.cpp
//       third_party/md4c/md4c.c third_party/miniz/miniz.c
//
// The numbers it printed are in references/csv-view.md and in the CSV
// plan's caps section.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "csv.h"
#include "csvdocument.h"
#include "fake_text_engine.h"
#include "layout/document_layout.h"
#include "theme.h"

namespace {

std::string MakeCsv(int rows, int columns) {
    std::string out;
    for (int c = 0; c < columns; ++c) {
        if (c) out += ',';
        out += "column_" + std::to_string(c);
    }
    out += '\n';
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            if (c) out += ',';
            if (c % 3 == 0) {
                out += std::to_string(r * 7 + c);
            } else if (c % 3 == 1) {
                out += "value " + std::to_string(r) + "-" + std::to_string(c);
            } else {
                out += "2026-11-15T10:";
                out += (r % 60 < 10 ? "0" : "");
                out += std::to_string(r % 60);
                out += ":00Z";
            }
        }
        out += '\n';
    }
    return out;
}

void Run(int rows, int columns) {
    const std::string text = MakeCsv(rows, columns);
    const auto t0 = std::chrono::steady_clock::now();
    const Document doc = csvdoc::BuildDocument(text, "probe.csv");
    const auto t1 = std::chrono::steady_clock::now();
    fake::FakeTextEngine engine;
    const LayoutMetrics m = BaseMetrics();
    const Palette pal = BasePalette();
    layout::LayoutInput in;
    in.doc = &doc;
    in.metrics = &m;
    in.palette = &pal;
    in.contentWidth = 900.0f;
    in.zoom = 1.0f;
    in.engine = &engine;
    const auto t2 = std::chrono::steady_clock::now();
    layout::DocumentLayout laid = layout::LayoutDocument(in);
    const auto t3 = std::chrono::steady_clock::now();
    const double parseMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double layoutMs =
        std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("%5d rows x %2d cols: %8.1f KB  parse %6.1f ms  layout %8.1f ms"
                "  blocks %zu  items %zu\n",
                rows, columns, text.size() / 1024.0, parseMs, layoutMs,
                laid.blocks.size(), laid.list.items.size());
}

}  // namespace

int main() {
    for (int rows : {100, 1000, 5000, 10000, 20000}) Run(rows, 12);
    Run(2000, 40);
    Run(5000, 40);
    return 0;
}
