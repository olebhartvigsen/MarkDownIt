// Pie diagram: parse `pie` definitions.
//
// Grammar (subset mermaid-compatible):
//   pie [showData] [title <text>]
//   <label> : <value>
//   also floats. Section labels may be quoted ("label"). Comments %%... are
//   skipped. Values are JSON numbers (double).
#ifndef MERMAID_PIE_PARSE_H
#define MERMAID_PIE_PARSE_H

#include <string>
#include <vector>

namespace mermaid {

struct PieSlice {
    std::string label;
    double value = 0;
};

struct PieDiagram {
    std::string title;
    bool show_data = false;
    std::vector<PieSlice> slices;   // source order (unsorted)
    std::string error;
};

// Parses `pie ...` source (without code fences). On error, error is set and
// the returned diagram is empty.
PieDiagram ParsePie(std::string_view src);

// "A, B and C" style label parse for the statement: `pie showData` etc.
// Kept internal; exposed for tests.
bool PieParseHeaderLine(const std::string& line, PieDiagram& pie);

}  // namespace mermaid

#endif  // MERMAID_PIE_PARSE_H
