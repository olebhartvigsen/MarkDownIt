#include "parse.h"

#include <string>
#include <vector>

namespace mermaid {

namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string Trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpace(s[b])) ++b;
    while (e > b && IsSpace(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// Split `s` on whitespace runs.
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && IsSpace(s[i])) ++i;
        size_t j = i;
        while (j < n && !IsSpace(s[j])) ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

}  // namespace

Flowchart ParseFlowchart(std::string_view src) {
    Flowchart fc;

    // Find the first non-empty, non-comment line.
    std::string header;
    size_t i = 0, n = src.size();
    while (i < n) {
        size_t j = i;
        while (j < n && src[j] != '\n') ++j;
        std::string line = Trim(src.substr(i, j - i));
        i = (j < n) ? j + 1 : j;
        if (line.empty()) continue;
        if (line.size() >= 2 && line[0] == '%' && line[1] == '%') continue;
        header = line;
        break;
    }

    if (header.empty()) {
        fc.error = "not a flowchart diagram";
        return fc;
    }

    auto tokens = Split(header);
    if (tokens.empty() || (tokens[0] != "flowchart" && tokens[0] != "graph")) {
        fc.error = "not a flowchart diagram";
        return fc;
    }

    if (tokens.size() == 1) {
        fc.dir = Dir::TB;  // default
        return fc;
    }
    if (tokens.size() != 2) {
        fc.error = "malformed flowchart header";
        return fc;
    }

    const std::string& d = tokens[1];
    if (d == "TB" || d == "TD") fc.dir = Dir::TB;
    else if (d == "BT")         fc.dir = Dir::BT;
    else if (d == "LR")         fc.dir = Dir::LR;
    else if (d == "RL")         fc.dir = Dir::RL;
    else { fc.error = "unknown flowchart direction"; return fc; }

    return fc;
}

}  // namespace mermaid
