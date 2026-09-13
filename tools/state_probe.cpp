// Compare LayoutStateDiagram against the stt goldens (nodes + edges + labels).
#include "../src/mermaid/state_layout.h"
#include "../src/mermaid/state_parse.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static double find_num(const char* key, const char* j, int& from) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    const char* hit = strstr(j + from, pat);
    if (!hit) return 0;
    int i = (int)(hit - (j + from));
    const char* p = hit + strlen(pat);
    while (*p==' '||*p=='\n'||*p=='\t'||*p=='\r'||*p=='"') ++p;
    double v = strtod(p, nullptr);
    from += i + (int)strlen(pat);
    return v;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: state_probe <fixture.mmd> <golden.json>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "open %s failed\n", argv[1]); return 2; }
    std::string src; char buf[65536]; int n = (int)fread(buf, 1, sizeof buf, f);
    src.assign(buf, (size_t)n); fclose(f);

    mermaid::StateDiagram d = mermaid::ParseStateDiagram(src);
    if (!d.error.empty()) { fprintf(stderr, "parse error: %s\n", d.error.c_str()); return 1; }
    mermaid::LaidOutState out = mermaid::LayoutStateDiagram(d);
    if (!out.error.empty()) { fprintf(stderr, "layout error: %s\n", out.error.c_str()); return 1; }

    FILE* g = fopen(argv[2], "rb");
    if (!g) { fprintf(stderr, "open %s failed\n", argv[2]); return 2; }
    std::string js; char gbuf[1048576]; int gn = (int)fread(gbuf, 1, sizeof gbuf, g);
    js.assign(gbuf, (size_t)gn); fclose(g);

    printf("nodes=%zu edges=%zu labels=%zu golden_nodes=", out.nodes.size(), out.edges.size(), out.labels.size());
    // count golden nodes
    int from = 0; int gcount = 0;
    for (;;) { char pat[32]; snprintf(pat, sizeof pat, "\"kind\":"); const char* hit = strstr(js.c_str() + from, pat); if (!hit) break; from = (int)(hit - js.c_str()) + 7; ++gcount; }
    printf("%d\n", gcount);

    // nodes: id kind cx cy w h r (search by kind blocks is fragile; dump ours,
    // then the checker compares numerically in python)
    for (const auto& n : out.nodes)
        printf("N %s %s %10.6f %10.6f %8.4f %8.4f r=%.3f\n",
               n.id.c_str(), n.kind.c_str(), n.cx, n.cy, n.w, n.h, n.r);
    for (const auto& e : out.edges)
        printf("E %s -> %s pts=%zu d=%s\n", e.from.c_str(), e.to.c_str(), e.points.size(), e.d.c_str());
    for (const auto& l : out.labels)
        printf("L %s %10.6f %10.6f %8.4f %8.4f\n", l.text.c_str(), l.x, l.y, l.w, l.h);
    printf("CANVAS vbw=%.6f vbh=%.6f startx=%.6f starty=%.6f\n",
           out.vbwidth, out.vbheight, out.startx, out.starty);
    return 0;
}