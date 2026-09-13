// Compare LayoutClassDiagram node centers against the cls goldens.
#include "../src/mermaid/class_layout.h"
#include "../src/mermaid/class_parse.h"
#include <cstdio>
#include <cstdlib>

static double find_num(const char* key, const char* j, int& from) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", key);
    int i = (int)(strstr(j + from, pat) - (j + from));
    if (i < 0 || i > 400000) return 0;
    const char* p = j + from + i + strlen(pat);
    while (*p==' '||*p=='\n'||*p=='\t'||*p=='\r'||*p=='"') ++p;
    double v = strtod(p, nullptr);
    from += i + (int)strlen(pat);
    return v;
}

static const char* skip_val(const char* j, int& from) {
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: class_probe <fixture.mmd> [golden.json]\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "open %s failed\n", argv[1]); return 2; }
    std::string src; char buf[65536]; int n = (int)fread(buf, 1, sizeof buf, f);
    src.assign(buf, (size_t)n); fclose(f);

    mermaid::ClassDiagram d = mermaid::ParseClassDiagram(src);
    if (!d.error.empty()) { fprintf(stderr, "parse error: %s\n", d.error.c_str()); return 1; }
    mermaid::LaidOutClass out = mermaid::LayoutClassDiagram(d);
    printf("classes=%zu relations=%zu laidout=%zu\n", d.classes.size(), d.relations.size(), out.nodes.size());
    for (const auto& n : out.nodes)
        printf("node %-18s x=%9.2f y=%9.2f w=%6.1f h=%6.1f\n",
               n.id.c_str(), n.x, n.y, n.w, n.h);
    for (const auto& e : out.edges) {
        printf("edge %s -> %s sm='%s' em='%s' pattern=%s\n",
               e.from.c_str(), e.to.c_str(), e.start_marker.c_str(),
               e.end_marker.c_str(), e.pattern.c_str());
        for (const auto& p : e.points)
            printf("  pt %.9f, %.9f\n", p.x, p.y);
        if (!e.d.empty())
            printf("  d: %s\n", e.d.c_str());
        printf("  npts=%zu\n", e.points.size());
        if (!e.label.empty())
            printf("  label '%s' at (%.2f,%.2f) w=%.0f h=%.0f\n",
                   e.label.c_str(), e.label_x, e.label_y, e.label_w, e.label_h);
    }
    for (const auto& t : out.edge_labels)
        printf("tlabel %-8s '%s' at (%.2f,%.2f) w=%.0f h=%.0f ix=%.0f iy=%.0f\n",
               t.kind.c_str(), t.text.c_str(), t.x, t.y, t.w, t.h, t.ix, t.iy);
    printf("canvas w=%.1f h=%.1f startx=%.1f starty=%.1f vbw=%.1f vbh=%.1f\n",
           out.width, out.height, out.startx, out.starty, out.vbwidth, out.vbheight);
    return 0;
}