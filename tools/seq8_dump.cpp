// Print engine activations + notes + loops for seq8 so the gate diff can
// be compared against the golden.
#include "mermaid/seq_parse.h"
#include "mermaid/seq_layout.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main() {
    auto seq = mermaid::ParseSequence(ReadFile(
        "/workspace/MarkDownIt/tests/mermaid/fixtures/seq8.mmd"));
    auto lo = mermaid::LayoutSequence(seq);
    std::fprintf(stderr, "error=%s\n", lo.error.c_str());
    std::fprintf(stderr, "=== activations (%zu) ===\n", lo.activations.size());
    for (const auto& a : lo.activations) {
        std::fprintf(stderr, "x=%.1f y=%.1f w=%.1f h=%.1f\n",
                     a.x, a.y, a.w, a.h);
    }
    std::fprintf(stderr, "=== messages (%zu) ===\n", lo.messages.size());
    for (const auto& m : lo.messages) {
        std::fprintf(stderr, "%s: t=(%.1f,%.1f) line=(%.1f,%.1f)->(%.1f,%.1f)\n",
                     m.text.c_str(), m.tx, m.ty, m.x1, m.y1,
                     m.x2, m.y2);
    }
    std::fprintf(stderr, "=== notes (%zu) ===\n", lo.notes.size());
    for (const auto& n : lo.notes) {
        std::fprintf(stderr, "%s: (%.1f,%.1f %.1fx%.1f) t=(%.1f,%.1f)\n",
                     n.text.c_str(), n.x, n.y, n.w, n.h, n.tx, n.ty);
    }
    std::fprintf(stderr, "=== loops (%zu) ===\n", lo.loops.size());
    for (const auto& l : lo.loops) {
        std::fprintf(stderr, "%s title='%s' (%.1f,%.1f)->(%.1f,%.1f)\n",
                     l.kind.c_str(), l.title.c_str(), l.startx, l.starty, l.stopx, l.stopy);
    }
    return 0;
}