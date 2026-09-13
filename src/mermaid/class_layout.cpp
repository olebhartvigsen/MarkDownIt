// Class diagram layout: dagre port matching mermaid classRenderer-v3.
// Verified against tools/mermaid-oracle class_oracle.mjs goldens:
//   - Node sizes: w = 4*concat_len + 24 (shim foreignObject metric);
//     h = 36 (members+methods), 60 (members only), 72 (empty box).
//   - dagre call: nodesep=50, ranksep=50 (halved to 25 by the internal
//     makeSpaceForEdgeLabels emulation in AssignRanks/Normalize),
//     edgesep=20, marginx=marginy=8. Edge labels become label-proxy
//     dummies (w = 8*len, h = 20) on the middle rank of the edge.
//   - After position, translateGraph shifts everything so the min side
//     sits at +margin (observed as a plain +8 on every coordinate).
//   - Edge polylines route through the dummy chain; the label anchor is
//     (calcLabelPosition(points).x, proxy.y); edge labels sit at the
//     label-proxy dummy's (x, y).
#include "class_layout.h"
#include "layout_internal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mermaid {

namespace {

constexpr double CLASS_MARGIN = 8.0;   // marginx / marginy
constexpr double CLASS_EDGE_SEP = 20.0;
constexpr double CLASS_NODE_SEP = 50.0;
constexpr double CLASS_RANK_SEP = 50.0;  // halved internally by the port

// mermaid classBox: every text (annotations with guillemets, title,
// members, methods) contributes 4 px/char; the rect adds 2*12 padding.
// Guillemets « » are 1 JS code unit each, so an annotation contributes
// inner.size() + 2 units (not + 4).
double ClassBoxWidth(const ClassBox& c) {
    double total = 0;
    for (const auto& a : c.annotations) total += static_cast<double>(a.size()) + 2.0;
    total += static_cast<double>(c.id.size());
    for (const auto& m : c.members) total += static_cast<double>(m.size());
    for (const auto& m : c.methods) total += static_cast<double>(m.size());
    return total * 4.0 + 24.0;
}

double ClassBoxHeight(const ClassBox& c) {
    if (c.members.empty() && c.methods.empty()) return 72.0;  // renderExtraBox
    if (!c.members.empty() && c.methods.empty()) return 60.0;  // h += GAP*2
    return 36.0;
}

double TextWidth8(const std::string& s) { return 8.0 * static_cast<double>(s.size()); }

// Edge label proxy: dagre injectEdgeLabelProxies puts a dummy of the
// label size on the middle rank of a labeled edge. In our normalized
// graph (ranks doubled), rank parity is preserved: the middle rank of a
// span-k edge is (ru+rv)/2 in doubled space. Return -1 when the edge has
// no label.
int LabelProxyRank(int ru, int rv) {
    return (ru + rv) / 2;
}

}  // namespace

LaidOutClass LayoutClassDiagram(const ClassDiagram& diag) {
    LaidOutClass out;
    if (diag.classes.empty() && diag.relations.empty()) return out;
    if (!diag.error.empty()) { out.error = diag.error; return out; }

    // ---- node index -----------------------------------------------------
    std::map<std::string, int> id2n;
    for (size_t i = 0; i < diag.classes.size(); ++i)
        id2n[diag.classes[i].id] = static_cast<int>(i);

    // ---- build layout graph ----------------------------------------------
    LayoutGraph g;
    g.nodes.reserve(diag.classes.size());
    for (const auto& c : diag.classes) {
        LayoutNode n;
        n.id = static_cast<int>(id2n[c.id]);
        n.label = c.id;
        n.width = static_cast<float>(ClassBoxWidth(c));
        n.height = static_cast<float>(ClassBoxHeight(c));
        g.nodes.push_back(n);
    }
    std::vector<ClassRelation> rels = diag.relations;
    // Auto-declare classes referenced only by relations (mermaid does).
    for (const auto& r : rels) {
        if (id2n.find(r.from) == id2n.end()) {
            ClassBox c; c.id = r.from;
            id2n[r.from] = static_cast<int>(g.nodes.size());
            LayoutNode n; n.id = id2n[r.from]; n.label = r.from;
            n.width = static_cast<float>(4.0 * r.from.size() + 24.0);
            n.height = 72.0f;
            g.nodes.push_back(n);
        }
        if (id2n.find(r.to) == id2n.end()) {
            ClassBox c; c.id = r.to;
            id2n[r.to] = static_cast<int>(g.nodes.size());
            LayoutNode n; n.id = id2n[r.to]; n.label = r.to;
            n.width = static_cast<float>(4.0 * r.to.size() + 24.0);
            n.height = 72.0f;
            g.nodes.push_back(n);
        }
    }

    for (const auto& r : rels) {
        LayoutEdge e;
        e.from = id2n[r.from];
        e.to = id2n[r.to];
        e.minlen = 1;
        e.weight = 1;
        e.label = r.label;
        if (!r.label.empty()) {
            e.label_width = static_cast<float>(TextWidth8(r.label));
            e.label_height = 20.0f;
        }
        e.style = r.dashed ? LineStyle::Dotted : LineStyle::Solid;
        g.edges.push_back(e);
    }

    // ---- pipeline (dagre order; ranksep halving is internal) ---------------
    LayoutParams p;
    p.rank_sep = CLASS_RANK_SEP;
    p.node_sep = CLASS_NODE_SEP;
    p.edge_sep = CLASS_EDGE_SEP;
    p.margin = 0.0;  // margin is applied explicitly below

    if (g.nodes.empty()) return out;

    std::vector<int> e_label_proxy_node(rels.size(), -1);

    // LR/RL: dagre's adjustCoordinateSystem swaps node width<->height before
    // the TB pipeline so column geometry comes out of row geometry.
    const std::string dir = diag.direction;
    const bool axis_swap = (dir == "LR" || dir == "RL");
    if (axis_swap) {
        for (auto& n : g.nodes) std::swap(n.width, n.height);
    }

    MakeAcyclic(g);
    AssignRanks(g);
    Normalize(g);
    // Label proxies: after Normalize the graph has mid-rank dummies for
    // every edge chain; give the middle dummy of each labeled edge the
    // label size and remember it.
    for (size_t ci = 0; ci < g.original_edges.size(); ++ci) {
        const LayoutEdge& oe = g.original_edges[ci];
        if (oe.label_width <= 0.0f) continue;
        int ru = g.nodes[static_cast<size_t>(oe.from)].rank;
        int rv = g.nodes[static_cast<size_t>(oe.to)].rank;
        int want = LabelProxyRank(ru, rv);
        // Find the chain dummy for original edge ci sitting on `want`.
        for (const auto& chain : g.dummy_chains) {
            if (chain.original_edge_index != static_cast<int>(ci)) continue;
            for (int dn : chain.dummy_nodes) {
                if (g.nodes[static_cast<size_t>(dn)].rank == want) {
                    g.nodes[static_cast<size_t>(dn)].width = oe.label_width;
                    g.nodes[static_cast<size_t>(dn)].height = oe.label_height;
                    e_label_proxy_node[ci] = dn;
                    break;
                }
            }
        }
    }
    Order(g);
    AssignCoordinates(g, p);
    RouteEdges(g, p);

    // Capture label-proxy positions before Denormalize removes dummies.
    // (x resolves later via calcLabelPosition; y is the proxy's rank y.)
    std::vector<double> proxy_x(rels.size(), 0.0), proxy_y(rels.size(), 0.0);
    for (size_t ci = 0; ci < rels.size(); ++ci) {
        int dn = e_label_proxy_node[ci];
        if (dn >= 0) {
            proxy_x[ci] = g.nodes[static_cast<size_t>(dn)].x;
            proxy_y[ci] = g.nodes[static_cast<size_t>(dn)].y;
        }
    }

    Denormalize(g);
    // ---- rankdir (dagre undoCoordinateSystem) ------------------------------
    if (dir == "BT" || dir == "RL") {
        double max_y = 0.0;
        for (const auto& n : g.nodes)
            max_y = std::max(max_y, static_cast<double>(n.y) + n.height * 0.5);
        for (auto& n : g.nodes) n.y = static_cast<float>(max_y - n.y);
        for (auto& e : g.edges)
            for (auto& pt : e.route) pt.y = max_y - pt.y;
        for (size_t ci = 0; ci < rels.size(); ++ci) {
            if (e_label_proxy_node[ci] >= 0) proxy_y[ci] = max_y - proxy_y[ci];
        }
    }
    if (axis_swap) {
        double max_y = 0.0;
        for (const auto& n : g.nodes)
            max_y = std::max(max_y, static_cast<double>(n.y) + n.height * 0.5);
        for (auto& n : g.nodes) {
            std::swap(n.x, n.y);
            std::swap(n.width, n.height);  // undoCoordinateSystem also swaps dims back
        }
        for (auto& e : g.edges)
            for (auto& pt : e.route) std::swap(pt.x, pt.y);
        for (size_t ci = 0; ci < rels.size(); ++ci)
            if (e_label_proxy_node[ci] >= 0)
                std::swap(proxy_x[ci], proxy_y[ci]);
        // RL mirrors across the old max_y (now the x extent).
        if (dir == "RL") {
            for (auto& n : g.nodes) n.x = static_cast<float>(max_y - n.x);
            for (auto& e : g.edges)
                for (auto& pt : e.route) pt.x = max_y - pt.x;
            for (size_t ci = 0; ci < rels.size(); ++ci)
                if (e_label_proxy_node[ci] >= 0) proxy_x[ci] = max_y - proxy_x[ci];
        }
    }

    // ---- margin shift (dagre translateGraph: min side -> +margin) ---------
    double minx = 1e300, miny = 1e300;
    for (const auto& n : g.nodes) {
        minx = std::min(minx, static_cast<double>(n.x) - n.width * 0.5);
        miny = std::min(miny, static_cast<double>(n.y) - n.height * 0.5);
    }
    for (const auto& e : g.edges) {
        for (const auto& pt : e.route) {
            minx = std::min(minx, pt.x);
            miny = std::min(miny, pt.y);
        }
    }
    if (minx > 1e299) { minx = 0; miny = 0; }
    double shift_x = CLASS_MARGIN - minx;
    double shift_y = CLASS_MARGIN - miny;
    for (auto& n : g.nodes) {
        n.x = static_cast<float>(static_cast<double>(n.x) + shift_x);
        n.y = static_cast<float>(static_cast<double>(n.y) + shift_y);
    }
    for (auto& e : g.edges) {
        for (auto& pt : e.route) { pt.x += shift_x; pt.y += shift_y; }
    }
    for (size_t ci = 0; ci < rels.size(); ++ci) {
        if (e_label_proxy_node[ci] >= 0) {
            proxy_x[ci] += shift_x;
            proxy_y[ci] += shift_y;
        }
    }

    // ---- emit nodes --------------------------------------------------------
    for (const auto& n : g.nodes) {
        LaidOutClassNode ln;
        ln.id = n.label;
        ln.title = n.label;
        for (const auto& c : diag.classes) {
            if (c.id == n.label) {
                ln.annotations = c.annotations;
                ln.members = c.members;
                ln.methods = c.methods;
                break;
            }
        }
        ln.x = n.x;
        ln.y = n.y;
        ln.w = n.width;
        ln.h = n.height;
        // Box-internal geometry (empirically matched to the golden oracle;
        // the shim's fixed text metrics make these independent of the real
        // font): label_y = annH + 14 - h/2, label_x = -2*len(title),
        // member/method text x = -w/2 + 12, rows start under a divider and
        // step 20 px; dividers at 48 - h/2 and 84 - h/2 (78 for empty boxes).
        const double annH = ln.annotations.empty() ? 0.0 : 12.0;
        double d1 = 48.0 - ln.h * 0.5;
        double d2 = (ln.members.empty() && ln.methods.empty() ? 78.0 : 84.0) - ln.h * 0.5;
        ln.divider_y = {d1, d2};
        ln.label_x = -2.0 * static_cast<double>(ln.title.size());
        ln.label_y = annH + 14.0 - ln.h * 0.5;
        ln.label_w = 8.0 * static_cast<double>(ln.title.size());
        double tx = -ln.w * 0.5 + 12.0;
        for (size_t i = 0; i < ln.members.size(); ++i) {
            ln.member_x.push_back(tx);
            ln.member_y.push_back(d1 + 2.0 + annH + 20.0 * static_cast<double>(i));
        }
        for (size_t i = 0; i < ln.methods.size(); ++i) {
            ln.method_x.push_back(tx);
            ln.method_y.push_back(d2 + 2.0 + annH + 20.0 * static_cast<double>(i));
        }
        if (!ln.annotations.empty()) {
            ln.annotation_y = 14.0 - ln.h * 0.5;
            ln.annotation_w = 8.0 * static_cast<double>(ln.annotations[0].size() + 2);
        }
        out.nodes.push_back(ln);
    }

    // ---- emit edges --------------------------------------------------------
    // Marker names (mermaid class markers): extension <|, composition *,
    // aggregation o, dependency >. The marker sits on the side the arrow
    // points to (marker_end), or on the start for reverse arrows.
    auto marker_name = [&](RelType t) -> std::string {
        switch (t) {
            case RelType::Extension:   return "extension";
            case RelType::Composition: return "composition";
            case RelType::Aggregation: return "aggregation";
            case RelType::Dependency:  return "dependency";
            default:                   return "";
        }
    };
    // Walk `dist` along the polyline from an end and return the cut point.
    auto clip_polyline = [&](std::vector<Point> pts, double dist, bool from_start) {
        std::vector<Point> p = pts;
        struct Seg { Point a, b; double len; };
        std::vector<Seg> segs;
        for (size_t i = 0; i + 1 < p.size(); ++i) {
            double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
            segs.push_back(Seg{p[i], p[i+1], std::hypot(dx, dy)});
        }
        double target = dist;
        double acc = 0;
        int cut = -1;
        for (int i = 0; i < static_cast<int>(segs.size()); ++i) {
            int si = from_start ? i : static_cast<int>(segs.size()) - 1 - i;
            if (acc + segs[static_cast<size_t>(si)].len >= target) {
                cut = si; break;
            }
            acc += segs[static_cast<size_t>(si)].len;
        }
        if (cut < 0) return p;
        const Seg& s = segs[static_cast<size_t>(cut)];
        double frac = from_start ? (target - acc) / s.len
                                 : 1.0 - (target - acc) / s.len;
        Point c;
        c.x = s.a.x + (s.b.x - s.a.x) * frac;
        c.y = s.a.y + (s.b.y - s.a.y) * frac;
        if (from_start) {
            std::vector<Point> out;
            out.push_back(c);
            for (int i = cut + 1; i < static_cast<int>(p.size()); ++i) out.push_back(p[static_cast<size_t>(i)]);
            return out;
        }
        p.resize(static_cast<size_t>(cut) + 1);
        p.push_back(c);
        return p;
    };
    // d3-shape Basis closure (line().curve(curveBasis)) rendered as an SVG
    // path string with 3-decimal rounding (matches the golden oracle).
    auto basis_d = [&](const std::vector<Point>& poly) {
        auto rnd = [&](double v) -> double {
            return std::floor(v * 1000.0 + 0.5) / 1000.0;
        };
        std::string d;
        char buf[64];
        if (poly.empty()) return std::string("");
        bool line_started = false;
        // d3 Basis state: _point 0..3, window (_x0,_y0)=prev2 (_x1,_y1)=prev1
        int point_state = 0;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        auto emit = [&](const std::string& cmd, double a, double b) {
            if (!d.empty()) d += "";
            snprintf(buf, sizeof buf, "%s%.3f,%.3f", cmd.c_str(), rnd(a), rnd(b));
            d += buf;
        };
        auto bezier = [&](double x, double y) {
            // point(that, x, y) helper
            double cx1 = (2.0 * x0 + x1) / 3.0, cy1 = (2.0 * y0 + y1) / 3.0;
            double cx2 = (x0 + 2.0 * x1) / 3.0, cy2 = (y0 + 2.0 * y1) / 3.0;
            double ex = (x0 + 4.0 * x1 + x) / 6.0, ey = (y0 + 4.0 * y1 + y) / 6.0;
            snprintf(buf, sizeof buf, "C%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                     rnd(cx1), rnd(cy1), rnd(cx2), rnd(cy2), rnd(ex), rnd(ey));
            d += buf;
        };
        for (size_t i = 0; i < poly.size(); ++i) {
            double x = poly[i].x, y = poly[i].y;
            switch (point_state) {
                case 0:
                    point_state = 1;
                    snprintf(buf, sizeof buf, "M%.3f,%.3f", rnd(x), rnd(y));
                    d += buf;
                    break;
                case 1:
                    point_state = 2;
                    break;
                case 2: {
                    point_state = 3;
                    snprintf(buf, sizeof buf, "L%.3f,%.3f",
                             rnd((5.0 * x0 + x1) / 6.0), rnd((5.0 * y0 + y1) / 6.0));
                    d += buf;
                    bezier(x, y);
                    break;
                }
                default:
                    bezier(x, y);
                    break;
            }
            x0 = x1; y0 = y1;
            x1 = x; y1 = y;
        }
        if (point_state == 3) {
            bezier(x1, y1);
            snprintf(buf, sizeof buf, "L%.3f,%.3f", rnd(x1), rnd(y1));
            d += buf;
        } else if (point_state == 2) {
            snprintf(buf, sizeof buf, "L%.3f,%.3f", rnd(x1), rnd(y1));
            d += buf;
        }
        return d;
    };
    // Decode the d string back into polyline endpoints (M/L/C final points).
    auto decode = [&](const std::string& d) {
        std::vector<Point> pts;
        std::vector<std::string> nums;
        std::string cur;
        char kind = 0;
        for (char ch : d) {
            if (ch == 'M' || ch == 'L' || ch == 'C') {
                if (!cur.empty()) { nums.push_back(cur); cur.clear(); }
                if (kind && !nums.empty()) {
                    if (kind == 'C') {
                        if (nums.size() >= 6)
                            pts.push_back(Point{std::strtod(nums[4].c_str(), nullptr),
                                                std::strtod(nums[5].c_str(), nullptr)});
                    } else {
                        for (size_t i = 0; i + 1 < nums.size(); i += 2)
                            pts.push_back(Point{std::strtod(nums[i].c_str(), nullptr),
                                                std::strtod(nums[i+1].c_str(), nullptr)});
                    }
                }
                kind = ch; nums.clear(); cur.clear();
            } else if (ch == ' ' || ch == ',') {
                if (!cur.empty()) { nums.push_back(cur); cur.clear(); }
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) nums.push_back(cur);
        if (kind && !nums.empty()) {
            if (kind == 'C') {
                if (nums.size() >= 6)
                    pts.push_back(Point{std::strtod(nums[4].c_str(), nullptr),
                                        std::strtod(nums[5].c_str(), nullptr)});
            } else {
                for (size_t i = 0; i + 1 < nums.size(); i += 2)
                    pts.push_back(Point{std::strtod(nums[i].c_str(), nullptr),
                                        std::strtod(nums[i+1].c_str(), nullptr)});
            }
        }
        return pts;
    };
    // polys: assume straight line: use clip_polyline result
    for (size_t ci = 0; ci < rels.size(); ++ci) {
        LaidOutClassEdge le;
        const ClassRelation& r = rels[ci];
        le.from = r.from;
        le.to = r.to;
        std::string sm = (r.marker_end == RelEnd::Start) ? marker_name(r.type) : "";
        std::string em = (r.marker_end == RelEnd::End)   ? marker_name(r.type) : "";
        le.start_marker = sm;
        le.end_marker = em;
        le.pattern = r.dashed ? "dashed" : "solid";
        if (ci < g.edges.size()) {
            const LayoutEdge& ge = g.edges[ci];
            le.label = ge.label;
            if (!ge.route.empty()) {
                std::vector<Point> poly = ge.route;
                if (!sm.empty() && poly.size() > 1) poly = clip_polyline(poly, 18.0, true);
                double end_trim = (em == "extension") ? 18.0 : (em == "dependency") ? 6.0 : 0.0;
                if (end_trim > 0.0 && poly.size() > 1) poly = clip_polyline(poly, end_trim, false);
                le.d = basis_d(poly);
                le.points = decode(le.d);
                // Edge label anchor: calcLabelPosition = polyline midpoint
                // (traverseEdge), or the label-proxy center for proxied edges.
                double lx = 0, ly = 0;
                if (e_label_proxy_node[ci] >= 0) {
                    lx = proxy_x[ci]; ly = proxy_y[ci];
                } else if (!r.label.empty()) {
                    // traverseEdge: halfway along the polyline
                    double total = 0;
                    for (size_t i = 0; i + 1 < poly.size(); ++i)
                        total += std::hypot(poly[i+1].x - poly[i].x, poly[i+1].y - poly[i].y);
                    double rem = total * 0.5;
                    if (poly.size() >= 2) {
                        lx = poly[0].x; ly = poly[0].y;
                        for (size_t i = 0; i + 1 < poly.size(); ++i) {
                            double dx = poly[i+1].x - poly[i].x;
                            double dy = poly[i+1].y - poly[i].y;
                            double L = std::hypot(dx, dy);
                            if (L >= rem) {
                                double f = rem / L;
                                lx = poly[i].x + dx * f;
                                ly = poly[i].y + dy * f;
                                break;
                            }
                            rem -= L;
                        }
                    }
                }
                le.label_x = lx;
                le.label_y = ly;
                le.label_w = 8.0 * static_cast<double>(r.label.size());
                le.label_h = 20.0;
            }
        }
        out.edges.push_back(le);

        // ---- edge labels & terminals (positionEdgeLabel) ---------------------
        const LayoutEdge* ge = (ci < g.edges.size()) ? &g.edges[ci] : nullptr;
        if (!r.label.empty() && ge != nullptr) {
            LaidOutClassTerminal tl;
            tl.kind = "label";
            tl.text = r.label;
            tl.x = le.label_x;
            tl.y = le.label_y;
            tl.w = le.label_w;
            tl.h = 20.0;
            out.edge_labels.push_back(tl);
        }
        // Cardinality terminals (start/end quoted labels). Observed calls from
        // mermaid: start cardinality -> calcTerminalLabelPosition(10,
        // "start_right", path), end -> (10, "end_left", path). The terminal
        // marker size arg is always 10 here: class relations set
        // arrowTypeStart/End to a truthy string ("none" when no marker).
        auto add_terminal = [&](const std::string& text, const std::string& position) {
            if (text.empty() || ge == nullptr || ge->route.empty()) return;
            // calcTerminalLabelPosition(terminalMarkerSize, position, points)
            std::vector<Point> pts = ge->route;
            bool is_start_side = (position == "start_left" || position == "start_right");
            double marker_size = 10.0;
            // points.reverse() unless start_left / start_right
            std::vector<Point> points = pts;
            if (!is_start_side) std::reverse(points.begin(), points.end());
            double dist = 25.0 + marker_size;
            Point center = points.front();
            if (points.size() >= 2) {
                double rem = dist;
                for (size_t i = 0; i + 1 < points.size(); ++i) {
                    double dx = points[i+1].x - points[i].x;
                    double dy = points[i+1].y - points[i].y;
                    double L = std::hypot(dx, dy);
                    if (L >= rem) { double f = rem / L; center.x = points[i].x + dx * f; center.y = points[i].y + dy * f; break; }
                    rem -= L;
                }
            }
            double d = 10.0 + marker_size * 0.5;
            const double PI = 3.141592653589793;
            double ang = std::atan2(points[0].y - center.y, points[0].x - center.x);
            double sx, sy;
            if (position == "start_left") {
                sx = std::sin(ang + PI) * d + (points[0].x + center.x) / 2.0;
                sy = -std::cos(ang + PI) * d + (points[0].y + center.y) / 2.0;
            } else if (position == "end_right") {
                sx = std::sin(ang - PI) * d + (points[0].x + center.x) / 2.0 - 5.0;
                sy = -std::cos(ang - PI) * d + (points[0].y + center.y) / 2.0 - 5.0;
            } else if (position == "end_left") {
                sx = std::sin(ang) * d + (points[0].x + center.x) / 2.0 - 5.0;
                sy = -std::cos(ang) * d + (points[0].y + center.y) / 2.0 - 5.0;
            } else {  // start_right
                sx = std::sin(ang) * d + (points[0].x + center.x) / 2.0;
                sy = -std::cos(ang) * d + (points[0].y + center.y) / 2.0;
            }
            LaidOutClassTerminal t2;
            t2.kind = "terminal";
            t2.text = text;
            t2.x = sx;
            t2.y = sy;
            t2.w = 4.0 * static_cast<double>(text.size());
            t2.h = 12.0;
            t2.ix = -2.0 * static_cast<double>(text.size());
            t2.iy = -6.0;
            out.edge_labels.push_back(t2);
        };
        add_terminal(r.start_label, "start_right");
        add_terminal(r.end_label, "end_left");
    }

    // ---- canvas (shim root-text metric) -------------------------------------
    // The oracle measures the whole SVG root text (edge labels and
    // terminals first, then every class: annotations with guillemets,
    // title, members, methods), then views w = len*4 + 16, h = 28, with
    // viewBox centered: startx = -w/2, starty = -h/2.
    double total_text = 0;
    for (const auto& t : out.edge_labels) total_text += t.text.size();
    for (const auto& n : out.nodes) {
        for (const auto& a : n.annotations) total_text += a.size() + 2.0;
        total_text += n.title.size();
        for (const auto& m : n.members) total_text += m.size();
        for (const auto& m : n.methods) total_text += m.size();
    }
    out.vbwidth = total_text * 4.0 + 16.0;
    out.vbheight = 28.0;
    out.width = out.vbwidth;
    out.height = out.vbheight;
    out.startx = -out.vbwidth * 0.5;
    out.starty = -14.0;

    return out;
}

}  // namespace mermaid