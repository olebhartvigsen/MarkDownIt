// Layout phase 5: coordinate assignment via Brandes-Koepf.
//
// Y coordinates: pure function of rank. Ranks are pre-doubled by AssignRanks
// so real nodes sit on even internal ranks and mid-rank dummies on odd ones.
// Row height = max node height on that row. Internal rank step = rank_sep/2.
//
// X coordinates: full BK per bk.js in dagre-d3-es. Four alignments
// (up/down x left/right), each does vertical-alignment blocks + horizontal
// compaction with dagre's asymmetric sep. Median of the 4 candidate x's per
// node yields the final coordinate.
//
// This is a straight port of tools/mermaid-oracle/node_modules/dagre-d3-es/
// src/dagre/position/bk.js. Conflict detection is included for correctness
// on graphs with dummy chains crossing each other; for graphs with no
// dummies it degenerates to an empty conflict set.

#include "layout_internal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mermaid {

namespace {

using Layering = std::vector<std::vector<int>>;

int MaxRank(const LayoutGraph& g) {
    int r = -1;
    for (const auto& n : g.nodes) if (n.rank > r) r = n.rank;
    return r;
}

Layering BuildLayering(const LayoutGraph& g) {
    int rmax = MaxRank(g);
    Layering L(rmax < 0 ? 0 : rmax + 1);
    for (const auto& n : g.nodes) {
        if (n.rank < 0) continue;
        L[static_cast<size_t>(n.rank)].push_back(n.id);
    }
    for (auto& row : L) {
        std::sort(row.begin(), row.end(), [&](int a, int b) {
            int oa = g.nodes[a].order, ob = g.nodes[b].order;
            if (oa != ob) return oa < ob;
            return a < b;
        });
    }
    return L;
}

struct Adj {
    std::vector<std::vector<int>> preds;
    std::vector<std::vector<int>> succs;
};

Adj BuildAdj(const LayoutGraph& g) {
    Adj a;
    a.preds.assign(g.nodes.size(), {});
    a.succs.assign(g.nodes.size(), {});
    for (const auto& e : g.edges) {
        if (e.from < 0 || e.to < 0) continue;
        a.succs[static_cast<size_t>(e.from)].push_back(e.to);
        a.preds[static_cast<size_t>(e.to)  ].push_back(e.from);
    }
    return a;
}

// Conflict store: pair of node ids (v<w) marked.
struct Conflicts {
    std::unordered_set<long long> set;
    static long long key(int v, int w) {
        if (v > w) std::swap(v, w);
        return (static_cast<long long>(v) << 32) ^ static_cast<unsigned int>(w);
    }
    void add(int v, int w) { set.insert(key(v, w)); }
    bool has(int v, int w) const { return set.count(key(v, w)) > 0; }
};

int FindOtherInnerSegmentNode(const LayoutGraph& g, const Adj& adj, int v) {
    if (!g.nodes[v].is_dummy) return -1;
    for (int u : adj.preds[v]) {
        if (g.nodes[u].is_dummy) return u;
    }
    return -1;
}

Conflicts FindType1Conflicts(const LayoutGraph& g, const Adj& adj, const Layering& L) {
    Conflicts c;
    for (size_t li = 1; li < L.size(); ++li) {
        const auto& prev = L[li - 1];
        const auto& layer = L[li];
        int k0 = 0, scanPos = 0;
        int prevLen = static_cast<int>(prev.size());
        int lastNode = layer.empty() ? -1 : layer.back();
        for (size_t i = 0; i < layer.size(); ++i) {
            int v = layer[i];
            int w = FindOtherInnerSegmentNode(g, adj, v);
            int k1 = (w >= 0) ? g.nodes[w].order : prevLen;
            if (w >= 0 || v == lastNode) {
                for (size_t j = static_cast<size_t>(scanPos); j <= i; ++j) {
                    int scanNode = layer[j];
                    for (int u : adj.preds[scanNode]) {
                        int uPos = g.nodes[u].order;
                        bool bothDummy = g.nodes[u].is_dummy && g.nodes[scanNode].is_dummy;
                        if ((uPos < k0 || uPos > k1) && !bothDummy) {
                            c.add(u, scanNode);
                        }
                    }
                }
                scanPos = static_cast<int>(i + 1);
                k0 = k1;
            }
        }
    }
    return c;
}

// Returns root, align maps (size n).
struct AlignRes {
    std::vector<int> root;
    std::vector<int> align;
};

AlignRes VerticalAlignment(const LayoutGraph& g,
                           const Layering& layering,
                           const Conflicts& conflicts,
                           bool use_preds,
                           const Adj& adj) {
    size_t n = g.nodes.size();
    AlignRes r;
    r.root.resize(n);
    r.align.resize(n);
    std::vector<int> pos(n, 0);
    for (const auto& layer : layering) {
        for (size_t k = 0; k < layer.size(); ++k) {
            int v = layer[k];
            r.root[v] = v;
            r.align[v] = v;
            pos[v] = static_cast<int>(k);
        }
    }
    for (const auto& layer : layering) {
        int prevIdx = -1;
        for (int v : layer) {
            const std::vector<int>& raw = use_preds ? adj.preds[v] : adj.succs[v];
            if (raw.empty()) continue;
            std::vector<int> ws = raw;
            std::sort(ws.begin(), ws.end(), [&](int a, int b){ return pos[a] < pos[b]; });
            double mp = (static_cast<double>(ws.size()) - 1.0) * 0.5;
            int lo = static_cast<int>(std::floor(mp));
            int hi = static_cast<int>(std::ceil(mp));
            for (int i = lo; i <= hi; ++i) {
                int w = ws[static_cast<size_t>(i)];
                if (r.align[v] == v && prevIdx < pos[w] && !conflicts.has(v, w)) {
                    r.align[w] = v;
                    r.align[v] = r.root[w];
                    r.root[v] = r.root[w];
                    prevIdx = pos[w];
                }
            }
        }
    }
    return r;
}

// Separation for two horizontally adjacent nodes v (right) and u (left,
// previously placed in the layer). dagre stores it symmetrically. labelpos
// on real nodes is unused in our port.
double Sep(const LayoutGraph& g, int v, int u, double node_sep, double edge_sep,
           bool /*reverseSep*/) {
    const LayoutNode& vL = g.nodes[v];
    const LayoutNode& uL = g.nodes[u];
    double sum = 0.0;
    sum += vL.width * 0.5;
    sum += (vL.is_dummy ? edge_sep : node_sep) * 0.5;
    sum += (uL.is_dummy ? edge_sep : node_sep) * 0.5;
    sum += uL.width * 0.5;
    return sum;
}

// horizontalCompaction: build block graph, then two passes.
std::vector<double> HorizontalCompaction(const LayoutGraph& g,
                                         const Layering& layering,
                                         const std::vector<int>& root,
                                         const std::vector<int>& align,
                                         double node_sep, double edge_sep,
                                         bool reverseSep) {
    size_t n = g.nodes.size();
    // Block graph: nodes are block-roots, edges carry sep.
    std::unordered_map<int, std::unordered_map<int, double>> blockOut;
    std::unordered_map<int, std::unordered_map<int, double>> blockIn;
    std::unordered_set<int> blockNodes;
    for (const auto& layer : layering) {
        int u = -1;
        for (int v : layer) {
            int vRoot = root[v];
            blockNodes.insert(vRoot);
            if (u >= 0) {
                int uRoot = root[u];
                double s = Sep(g, v, u, node_sep, edge_sep, reverseSep);
                auto it = blockOut[uRoot].find(vRoot);
                double prev = (it == blockOut[uRoot].end()) ? -1e300 : it->second;
                double w = std::max(prev, s);
                blockOut[uRoot][vRoot] = w;
                blockIn [vRoot][uRoot] = w;
            }
            u = v;
        }
    }

    std::vector<double> xs(n, 0.0);
    std::unordered_map<int, double> bx;
    for (int r : blockNodes) bx[r] = 0.0;

    // Pass1: predecessors first, xs[e] = max over inEdges of xs[u]+w
    auto iter = [&](bool pass1) {
        std::vector<int> stack(blockNodes.begin(), blockNodes.end());
        std::unordered_set<int> visited;
        while (!stack.empty()) {
            int elem = stack.back(); stack.pop_back();
            if (visited.count(elem)) {
                if (pass1) {
                    double best = 0.0;
                    auto it = blockIn.find(elem);
                    if (it != blockIn.end()) {
                        for (auto& kv : it->second) {
                            best = std::max(best, bx[kv.first] + kv.second);
                        }
                    }
                    bx[elem] = best;
                } else {
                    double best = std::numeric_limits<double>::infinity();
                    auto it = blockOut.find(elem);
                    if (it != blockOut.end()) {
                        for (auto& kv : it->second) {
                            best = std::min(best, bx[kv.first] - kv.second);
                        }
                    }
                    if (best != std::numeric_limits<double>::infinity()) {
                        bx[elem] = std::max(bx[elem], best);
                    }
                }
            } else {
                visited.insert(elem);
                stack.push_back(elem);
                auto& src = pass1 ? blockIn[elem] : blockOut[elem];
                for (auto& kv : src) stack.push_back(kv.first);
            }
        }
    };
    iter(true);
    iter(false);

    for (size_t v = 0; v < n; ++v) xs[v] = bx[root[v]];
    return xs;
}

double AlignmentWidth(const LayoutGraph& g, const std::vector<double>& xs) {
    double mx = -1e300, mn = 1e300;
    for (size_t v = 0; v < g.nodes.size(); ++v) {
        double hw = g.nodes[v].width * 0.5;
        mx = std::max(mx, xs[v] + hw);
        mn = std::min(mn, xs[v] - hw);
    }
    return mx - mn;
}

}  // namespace

void AssignCoordinates(LayoutGraph& g, const LayoutParams& p) {
    if (g.nodes.empty()) return;
    Layering layering = BuildLayering(g);
    Adj adj = BuildAdj(g);

    // ---- Y from rank; ranksep is halved to match dagre's makeSpaceForEdgeLabels.
    std::vector<double> row_h(layering.size(), 0.0);
    for (size_t r = 0; r < layering.size(); ++r) {
        double h = 0.0;
        for (int u : layering[r]) h = std::max(h, static_cast<double>(g.nodes[u].height));
        row_h[r] = h;
    }
    double half_rank_sep = p.rank_sep * 0.5;
    std::vector<double> row_center(layering.size(), 0.0);
    if (!layering.empty()) row_center[0] = row_h[0] * 0.5;
    for (size_t r = 1; r < layering.size(); ++r) {
        row_center[r] = row_center[r-1] + row_h[r-1] * 0.5 + row_h[r] * 0.5 + half_rank_sep;
    }
    for (size_t r = 0; r < layering.size(); ++r) {
        for (int u : layering[r]) g.nodes[u].y = static_cast<float>(row_center[r]);
    }

    // ---- X via BK 4-alignment ----
    Conflicts conflicts = FindType1Conflicts(g, adj, layering);
    if (std::getenv("BK_DEBUG")) {
        std::fprintf(stderr, "LAYER=");
        for (const auto& row : layering) {
            std::fprintf(stderr, "[");
            for (int u : row) {
                bool dummy = g.nodes[u].is_dummy;
                int chain_e = -1;
                for (const auto& dc : g.dummy_chains)
                    for (int dn : dc.dummy_nodes)
                        if (dn == u) chain_e = dc.original_edge_index;
                if (dummy && chain_e >= 0)
                    std::fprintf(stderr, "e%d(%s>%s)", chain_e,
                                 g.nodes[g.original_edges[chain_e].from].label.c_str(),
                                 g.nodes[g.original_edges[chain_e].to].label.c_str());
                else
                    std::fprintf(stderr, "%s#", g.nodes[u].label.c_str());
            }
            std::fprintf(stderr, "]");
        }
        std::fprintf(stderr, "\n");
    }
    // (Type-2 conflicts skipped: no border segments in our port.)

    // 4 candidate x-vectors: ul, ur, dl, dr.
    std::vector<std::vector<double>> xss(4, std::vector<double>(g.nodes.size(), 0.0));
    const char verts[2] = {'u','d'};
    const char horizs[2] = {'l','r'};
    for (int vi = 0; vi < 2; ++vi) {
        Layering base = layering;
        if (verts[vi] == 'd') std::reverse(base.begin(), base.end());
        for (int hi = 0; hi < 2; ++hi) {
            Layering L2 = base;
            if (horizs[hi] == 'r') {
                for (auto& row : L2) std::reverse(row.begin(), row.end());
            }
            bool use_preds = (verts[vi] == 'u');
            AlignRes al = VerticalAlignment(g, L2, conflicts, use_preds, adj);
            bool reverseSep = (horizs[hi] == 'r');
            auto xs = HorizontalCompaction(g, L2, al.root, al.align,
                                           p.node_sep, p.edge_sep, reverseSep);
            if (horizs[hi] == 'r') {
                for (auto& x : xs) x = -x;
            }
            xss[vi * 2 + hi] = std::move(xs);
            if (std::getenv("BK_DEBUG")) {
                std::fprintf(stderr, "XSS%c%c=", verts[vi], horizs[hi]);
                for (size_t v = 0; v < xss[vi*2+hi].size(); ++v)
                    std::fprintf(stderr, "%s:%.0f ", g.nodes[v].label.c_str(), xss[vi*2+hi][v]);
                std::fprintf(stderr, "\n");
            }
        }
    }

    // Align coordinates: find smallest-width alignment; shift others so
    // left-biased mins line up on the left, right-biased maxes on the right.
    int smallest = 0;
    double bestW = AlignmentWidth(g, xss[0]);
    for (int k = 1; k < 4; ++k) {
        double w = AlignmentWidth(g, xss[k]);
        if (w < bestW) { bestW = w; smallest = k; }
    }
    auto vals_min = [&](const std::vector<double>& xs){
        double m = 1e300; for (double v : xs) m = std::min(m, v); return m;
    };
    auto vals_max = [&](const std::vector<double>& xs){
        double m = -1e300; for (double v : xs) m = std::max(m, v); return m;
    };
    double targetMin = vals_min(xss[smallest]);
    double targetMax = vals_max(xss[smallest]);
    for (int k = 0; k < 4; ++k) {
        if (k == smallest) continue;
        bool leftBiased = (k % 2 == 0);  // ul=0, dl=2 are left; ur=1, dr=3 right
        double delta;
        if (leftBiased) delta = targetMin - vals_min(xss[k]);
        else            delta = targetMax - vals_max(xss[k]);
        for (auto& v : xss[k]) v += delta;
    }

    // Balance: per-node median of middle two values.
    std::vector<double> finalX(g.nodes.size(), 0.0);
    for (size_t v = 0; v < g.nodes.size(); ++v) {
        double vs[4] = {xss[0][v], xss[1][v], xss[2][v], xss[3][v]};
        std::sort(vs, vs + 4);
        finalX[v] = (vs[1] + vs[2]) * 0.5;
    }

    for (size_t i = 0; i < g.nodes.size(); ++i) {
        g.nodes[i].x = static_cast<float>(finalX[i]);
    }

    // Left-align real nodes' left edges to 0.
    double min_left = std::numeric_limits<double>::infinity();
    for (const auto& n : g.nodes) {
        if (n.is_dummy) continue;
        double left = static_cast<double>(n.x) - static_cast<double>(n.width) * 0.5;
        if (left < min_left) min_left = left;
    }
    if (min_left != std::numeric_limits<double>::infinity() && min_left != 0.0) {
        for (auto& n : g.nodes) n.x = static_cast<float>(n.x - min_left);
    }
}

}  // namespace mermaid
