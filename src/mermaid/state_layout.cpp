// State diagram layout (flat states): dagre port matching mermaid 11.x
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
#include "state_layout.h"
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

// stateEnd path bbox as measured by the shim (path extremes), and the
// start/end circle radius the renderer draws at the dagre center.
constexpr double kEndW = 14.017724288152426;    // rendered bbox (golden)
constexpr double kEndLayoutW = 14.138304985583623;  // what dagre sees (shim path bbox)
constexpr double kEndH = 23.0;
constexpr double kStartEndR = 7.0;

// Edge label proxy: dagre injectEdgeLabelProxies puts a dummy of the
// label size on the middle rank of a labeled edge. In our normalized
// graph (ranks doubled), rank parity is preserved: the middle rank of a
// span-k edge is (ru+rv)/2 in doubled space. Return -1 when the edge has
// no label.
int LabelProxyRank(int ru, int rv) {
    return (ru + rv) / 2;
}

}  // namespace

LaidOutState LayoutStateDiagram(const StateDiagram& diag) {
    LaidOutState out;
    if (diag.nodes.empty() && diag.transitions.empty()) return out;
    if (!diag.error.empty()) { out.error = diag.error; return out; }

    // ---- node index ---------------------------------------------------------
    std::map<std::string, int> id2n;
    std::vector<StateKind> kinds;   // kind per layout-node index

    auto intern = [&](const std::string& id, StateKind kind) -> int {
        auto it = id2n.find(id);
        if (it != id2n.end()) return it->second;
        int idx = static_cast<int>(kinds.size());
        id2n[id] = idx;
        kinds.push_back(kind);
        return idx;
    };

    // Start/end markers used in transitions implicitly declare the nodes.
    for (const auto& t : diag.transitions) {
        if (t.from == "root_start") intern("root_start", StateKind::Start);
        if (t.to == "root_end") intern("root_end", StateKind::End);
    }
    for (const auto& n : diag.nodes) intern(n.id, n.kind);

    // ---- build layout graph ------------------------------------------------
    LayoutGraph g;
    g.nodes.resize(kinds.size());
    for (size_t i = 0; i < kinds.size(); ++i) {
        LayoutNode& ln = g.nodes[i];
        ln.id = static_cast<int>(i);
        ln.is_dummy = false;
        // Layout node sizes mirror what the shim reports to dagre: state and
        // start see the shim text bbox (0 x 12); the end state sees the
        // stateEnd path bbox (14.0177 x 23).
        ln.width = 0.0f;
        ln.height = 12.0f;
        if (kinds[i] == StateKind::End) {
            ln.width = static_cast<float>(kEndLayoutW);
            ln.height = static_cast<float>(kEndH);
        }
        ln.label = "";
    }
    for (const auto& n : diag.nodes) {
        int idx = intern(n.id, n.kind);
        g.nodes[static_cast<size_t>(idx)].label = n.text.empty() ? n.id : n.text;
    }
    // The implicit start/end labels stay as their ids for lookup.
    for (const auto& t : diag.transitions) {
        if (t.from == "root_start")
            g.nodes[static_cast<size_t>(id2n["root_start"])].label = "root_start";
        if (t.to == "root_end")
            g.nodes[static_cast<size_t>(id2n["root_end"])].label = "root_end";
    }

    for (const auto& t : diag.transitions) {
        LayoutEdge e;
        e.from = id2n[t.from];
        e.to = id2n[t.to];
        e.minlen = 1;
        e.weight = 1;
        e.label = t.label;
        if (!t.label.empty()) {
            e.label_width = static_cast<float>(8.0 * static_cast<double>(t.label.size()));
            e.label_height = 20.0f;
        }
        g.edges.push_back(e);
    }

    // Reindex proxies by transition index (transitions == original edges).
    const size_t kTransCount = diag.transitions.size();

// ---- pipeline (dagre order; ranksep halving is internal) ---------------
    LayoutParams p;
    p.rank_sep = CLASS_RANK_SEP;
    p.node_sep = CLASS_NODE_SEP;
    p.edge_sep = CLASS_EDGE_SEP;
    p.margin = 0.0;
    p.left_align_zero = false;  // margin is applied explicitly below

    if (g.nodes.empty()) return out;

    std::vector<int> e_label_proxy_node(kTransCount, -1);

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
                    // dagre: the label dummy carries (w=labelW, h=20) at
                    // normalize time, and coordinateSystem.adjust swaps
                    // w/h for LR/RL before positionY. Since we swapped all
                    // node sizes up front, set the post-adjust sizes here.
                    if (axis_swap) {
                        g.nodes[static_cast<size_t>(dn)].width = oe.label_height;
                        g.nodes[static_cast<size_t>(dn)].height = oe.label_width;
                    } else {
                        g.nodes[static_cast<size_t>(dn)].width = oe.label_width;
                        g.nodes[static_cast<size_t>(dn)].height = oe.label_height;
                    }
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
    std::vector<double> proxy_x(kTransCount, 0.0), proxy_y(kTransCount, 0.0);
    for (size_t ci = 0; ci < kTransCount; ++ci) {
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
        for (size_t ci = 0; ci < kTransCount; ++ci) {
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
        for (size_t ci = 0; ci < kTransCount; ++ci)
            if (e_label_proxy_node[ci] >= 0)
                std::swap(proxy_x[ci], proxy_y[ci]);
        // RL mirrors across the old max_y (now the x extent).
        if (dir == "RL") {
            for (auto& n : g.nodes) n.x = static_cast<float>(max_y - n.x);
            for (auto& e : g.edges)
                for (auto& pt : e.route) pt.x = max_y - pt.x;
            for (size_t ci = 0; ci < kTransCount; ++ci)
                if (e_label_proxy_node[ci] >= 0) proxy_x[ci] = max_y - proxy_x[ci];
        }
    }

    // ---- margin shift (dagre translateGraph: min side -> +margin) ---------
    // Extremes are computed AFTER the rankdir transforms, in final space,
    // and include the label-proxy boxes (they participate in translateGraph
    // in the real dagre run; e.g. the 'go' proxy at x=-48 w=16 drives
    // minx=-56 in stt1, and the 'yes' proxy drives miny in LR diagrams).
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
    for (size_t ci = 0; ci < kTransCount; ++ci) {
        if (e_label_proxy_node[ci] < 0) continue;
        const auto& t = diag.transitions[ci];
        // Net size: the proxy box is swapped by the pre-position adjust and
        // swapped back by undo, so its dimensions in translateGraph space
        // are the originals (8*len x 20) even for LR/RL.
        double pw = 8.0 * static_cast<double>(t.label.size());
        double ph = 20.0;
        minx = std::min(minx, proxy_x[ci] - pw * 0.5);
        miny = std::min(miny, proxy_y[ci] - ph * 0.5);
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
    for (size_t ci = 0; ci < kTransCount; ++ci) {
        if (e_label_proxy_node[ci] >= 0) {
            proxy_x[ci] += shift_x;
            proxy_y[ci] += shift_y;
        }
    }


    // ---- emit nodes --------------------------------------------------------
    for (size_t ni = 0; ni < kinds.size(); ++ni) {
        const LayoutNode& n = g.nodes[ni];
        LaidOutStateNode sn;
        sn.id = n.label;
        if (sn.id.empty()) {
            sn.id = (kinds[ni] == StateKind::Start) ? "root_start"
                                                   : (kinds[ni] == StateKind::End ? "root_end" : "");
        }
        sn.cx = static_cast<double>(n.x);
        sn.cy = static_cast<double>(n.y);
        if (kinds[ni] == StateKind::Start) {
            sn.kind = "start";
            sn.r = kStartEndR;
            sn.w = 2.0 * kStartEndR;
            sn.h = 2.0 * kStartEndR;
        } else if (kinds[ni] == StateKind::End) {
            sn.kind = "end";
            sn.r = kStartEndR;
            sn.w = kEndW;
            sn.h = kEndH;
        } else {
            sn.kind = "state";
            sn.w = 8.0 * static_cast<double>(n.label.size()) + 16.0;
            sn.h = 36.0;
        }
        for (const auto& dn : diag.nodes) {
            if (dn.id == sn.id) sn.text = dn.text;
        }
        out.nodes.push_back(sn);
    }

// ---- emit edges --------------------------------------------------------
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
    for (size_t ci = 0; ci < kTransCount; ++ci) {
        LaidOutStateEdge le;
        const StateTransition& t = diag.transitions[ci];
        le.from = t.from;
        le.to = t.to;

        if (ci < g.edges.size()) {
            const LayoutEdge& ge = g.edges[ci];
            if (!ge.route.empty()) {
                // Re-anchor: dagre's route is clipped at the layout bbox
                // (0x12 fake box). The renderer anchors start/end at the
                // circle border (r=7) toward the neighbor, plain states at
                // the node center. No marker trim is applied (the barb
                // marker geometry does not shift the path).
                auto anchor = [&](StateKind k, const Point& toward,
                                  double nx, double ny) -> Point {
                    if (k == StateKind::Start || k == StateKind::End) {
                        double dx = toward.x - nx, dy = toward.y - ny;
                        double len = std::hypot(dx, dy);
                        if (len <= 1e-9)
                            return Point{nx, ny};
                        double t = kStartEndR / len;
                        return Point{nx + dx * t, ny + dy * t};
                    }
                    return Point{nx, ny};
                };
                std::vector<Point> poly = ge.route;
                // dagre layout.js: reversePointsForReversedEdges - acyclic
                // reversed edges have their point order flipped so the path
                // runs from the original source to the original target.

                if (ge.reversed) std::reverse(poly.begin(), poly.end());

                if (poly.size() >= 2) {
                    // After the reversal poly[0] is the ORIGINAL target side
                    // and poly[last] the original source side, so swap the
                    // anchor roles for reversed edges.
                    int src_idx = ge.reversed ? ge.to : ge.from;
                    int dst_idx = ge.reversed ? ge.from : ge.to;
                    const LayoutNode& fn = g.nodes[static_cast<size_t>(src_idx)];
                    const LayoutNode& tn = g.nodes[static_cast<size_t>(dst_idx)];
                    poly[0] = anchor(kinds[static_cast<size_t>(src_idx)], poly[1],
                                     static_cast<double>(fn.x), static_cast<double>(fn.y));
                    poly[poly.size() - 1] = anchor(kinds[static_cast<size_t>(dst_idx)],
                                                   poly[poly.size() - 2],
                                                   static_cast<double>(tn.x),
                                                   static_cast<double>(tn.y));
                }
                le.d = basis_d(poly);
                le.points = decode(le.d);
            }
        }
        out.edges.push_back(le);

        // ---- edge labels (proxy center, shared with class) ---------------
        if (!t.label.empty()) {
            LaidOutStateLabel tl;
            tl.text = t.label;
            tl.w = 8.0 * static_cast<double>(t.label.size());
            tl.h = 20.0;
            if (e_label_proxy_node[ci] >= 0) {
                tl.x = proxy_x[ci] - tl.w * 0.5;
                tl.y = proxy_y[ci] - tl.h * 0.5;
            } else if (ci < g.edges.size() && !g.edges[ci].route.empty()) {
                const auto& poly = g.edges[ci].route;
                double total = 0;
                for (size_t i = 0; i + 1 < poly.size(); ++i)
                    total += std::hypot(poly[i+1].x - poly[i].x,
                                        poly[i+1].y - poly[i].y);
                double rem = total * 0.5;
                for (size_t i = 0; i + 1 < poly.size(); ++i) {
                    double dx = poly[i+1].x - poly[i].x;
                    double dy = poly[i+1].y - poly[i].y;
                    double L = std::hypot(dx, dy);
                    if (L >= rem) {
                        double f = rem / L;
                        tl.x = poly[i].x + dx * f - tl.w * 0.5;
                        tl.y = poly[i].y + dy * f - tl.h * 0.5;
                        break;
                    }
                    rem -= L;
                }
            }
            out.labels.push_back(tl);
        }
    }

// ---- canvas (shim root-text metric) -------------------------------------
    // The oracle measures the whole SVG root text (edge labels first,
    // then every state's rendered id text), then views w = len*4 + 16,
    // h = 28, with viewBox centered: startx = -w/2, starty = -h/2.
    double total_text = 0;
    for (const auto& t : out.labels) total_text += static_cast<double>(t.text.size());
    for (const auto& n : out.nodes) {
        if (n.kind == "state") total_text += static_cast<double>(n.text.size());
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