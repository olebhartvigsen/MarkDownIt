// The Mermaid layout profiles: ELK's layered settings and dagre's.

#include "mermaid/elk_profile.h"

#include <cstdlib>
#include <string>

namespace mermaid {

ElkSettings ElkDefaults() { return ElkSettings{}; }

LayoutParams ParamsFor(const ElkSettings& s) {
    LayoutParams p;
    // The engine's three separations are ELK's three: nodes across a
    // layer boundary, nodes inside a layer, and the room an edge keeps.
    p.rank_sep = s.spacing_node_node_between_layers;
    p.node_sep = s.spacing_node_node;
    p.edge_sep = s.spacing_edge_edge;
    p.margin = s.padding;
    return p;
}

LayoutParams DagreParams() { return LayoutParams{}; }

LayoutParams ElkParams() { return ParamsFor(ElkDefaults()); }

namespace {

bool ReadProfileFromEnvironment() {
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "MARKDOWNIT_MERMAID_LAYOUT") != 0 || !buf) {
        return false;
    }
    const std::string value(buf);
    free(buf);
#else
    const char* raw = std::getenv("MARKDOWNIT_MERMAID_LAYOUT");
    if (!raw) return false;
    const std::string value(raw);
#endif
    std::string lowered;
    for (char c : value) {
        if (c == ' ' || c == '\t') continue;
        lowered.push_back(c >= 'A' && c <= 'Z'
                              ? static_cast<char>(c - 'A' + 'a')
                              : c);
    }
    return lowered == "elk";
}

bool ProfileIsElk() {
    static const bool elk = ReadProfileFromEnvironment();
    return elk;
}

}  // namespace

LayoutParams LayoutParamsForProfile() {
    return ProfileIsElk() ? ElkParams() : DagreParams();
}

const char* MermaidLayoutProfileName() {
    return ProfileIsElk() ? "elk" : "dagre";
}

}  // namespace mermaid
