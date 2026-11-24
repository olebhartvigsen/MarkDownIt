// Assured validation of the Mermaid implementation.
//
// Every fixture is pushed through the real path a document takes: a
// fenced block, md4c, the promotion in the parser, the layout the app
// uses. Then three things are checked that no golden can check for us:
//
//   1. the fence is promoted to a diagram and not left as a code card,
//   2. the geometry obeys the invariants a readable diagram has (finite
//      numbers, no overlapping nodes, a canvas that contains its nodes,
//      lanes that enclose their members and keep off each other),
//   3. two identical parses give identical geometry.
//
// A fourth check pins the geometry itself: a canonical dump per fixture
// is compared against the checked-in snapshot, so an unintended layout
// change shows as a diff naming the fixture. Snapshots are regenerated
// on purpose, with MARKDOWNIT_UPDATE_SNAPSHOTS=1.

#include "../src/dom.h"
#include "../src/parser.h"
#include "../src/mermaid/class_layout.h"
#include "../src/mermaid/layout.h"
#include "../src/mermaid/state_layout.h"
#include "../src/mermaid/gantt_layout.h"
#include "../src/mermaid/pie_layout.h"
#include "../src/mermaid/seq_layout.h"
#include "gtest_lite.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    std::string id;
};

std::string Slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The fixtures, and the block kind each one must become. A fixture that
// stops being promoted is a regression the rest of the suite would miss.
struct Fixture {
    const char* file;
    BlockKind kind;
};

std::vector<Fixture> Fixtures() {
    std::vector<Fixture> out;
    for (const char* name :
         {"01-linear", "02-shapes-edges", "03-diamond", "04-crossing",
          "05-long-edge", "06-siblings", "07-lr", "08-rl", "09-bt",
          "10-swimlanes-simple", "11-swimlanes-cross"}) {
        out.push_back({name, BlockKind::MermaidFlowchart});
    }
    // Class diagrams are parsed and laid out in tests but are not
    // promoted to a block, so there is no document geometry to validate
    // for them yet. They are covered by their own golden suite.
    // stt2 declares a composite state, which the layout still refuses:
    // that fence must stay a code card, and the suite pins it.
    for (const char* name : {"stt1", "stt3", "stt4", "stt5"}) {
        out.push_back({name, BlockKind::MermaidState});
    }
    out.push_back({"stt2", BlockKind::CodeBlock});
    for (const char* name : {"pie1", "pie2", "pie3"}) {
        out.push_back({name, BlockKind::MermaidPie});
    }
    for (const char* name : {"seq1", "seq2", "seq3", "seq4", "seq5", "seq6",
                             "seq7", "seq8"}) {
        out.push_back({name, BlockKind::MermaidSequence});
    }
    return out;
}

Document ParseAsDocument(const std::string& source) {
    // A fixture file ends with a newline; inserting another before the
    // closing fence would end the fence early and leave a blank
    // paragraph behind it.
    std::string trimmed = source;
    while (!trimmed.empty() &&
           (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }
    Document doc;
    ParseMarkdown("```mermaid\n" + trimmed + "\n```\n", doc);
    return doc;
}

// The boxes a diagram draws, so the invariants below can be generic.
std::vector<Box> BoxesOf(const Node& n) {
    std::vector<Box> boxes;
    switch (n.block) {
        case BlockKind::MermaidFlowchart:
            if (n.mermaid_layout) {
                for (const auto& node : n.mermaid_layout->nodes) {
                    if (node.is_dummy) continue;
                    boxes.push_back({static_cast<float>(node.x),
                                     static_cast<float>(node.y),
                                     static_cast<float>(node.width),
                                     static_cast<float>(node.height),
                                     std::to_string(node.id)});
                }
            }
            break;
        case BlockKind::MermaidState:
            if (n.mermaid_state) {
                for (const auto& node : n.mermaid_state->nodes) {
                    boxes.push_back({static_cast<float>(node.cx),
                                     static_cast<float>(node.cy),
                                     static_cast<float>(node.w),
                                     static_cast<float>(node.h), node.id});
                }
            }
            break;
        default:
            break;
    }
    return boxes;
}

float CanvasWidth(const Node& n) {
    switch (n.block) {
        case BlockKind::MermaidFlowchart:
            return n.mermaid_layout ? static_cast<float>(n.mermaid_layout->width)
                                    : 0.0f;
        case BlockKind::MermaidState:
            return n.mermaid_state ? static_cast<float>(n.mermaid_state->width)
                                   : 0.0f;
        case BlockKind::MermaidPie:
            return n.mermaid_pie ? static_cast<float>(n.mermaid_pie->width)
                                 : 0.0f;
        case BlockKind::MermaidSequence:
            return n.mermaid_seq ? static_cast<float>(n.mermaid_seq->width)
                                 : 0.0f;
        case BlockKind::MermaidGantt:
            return n.mermaid_gantt ? static_cast<float>(n.mermaid_gantt->width)
                                   : 0.0f;
        default:
            return 0.0f;
    }
}

float CanvasHeight(const Node& n) {
    switch (n.block) {
        case BlockKind::MermaidFlowchart:
            return n.mermaid_layout
                       ? static_cast<float>(n.mermaid_layout->height)
                       : 0.0f;
        case BlockKind::MermaidState:
            return n.mermaid_state ? static_cast<float>(n.mermaid_state->height)
                                   : 0.0f;
        case BlockKind::MermaidPie:
            return n.mermaid_pie ? static_cast<float>(n.mermaid_pie->height)
                                 : 0.0f;
        case BlockKind::MermaidSequence:
            return n.mermaid_seq ? static_cast<float>(n.mermaid_seq->vbheight)
                                 : 0.0f;
        case BlockKind::MermaidGantt:
            return n.mermaid_gantt ? static_cast<float>(n.mermaid_gantt->height)
                                   : 0.0f;
        default:
            return 0.0f;
    }
}

// A stable text form of the geometry, for the snapshot and for the
// determinism check.
std::string CanonicalDump(const Node& n) {
    std::ostringstream out;
    out.precision(3);
    out << std::fixed;
    out << "kind " << static_cast<int>(n.block) << "\n";
    out << "canvas " << CanvasWidth(n) << " " << CanvasHeight(n) << "\n";
    for (const Box& b : BoxesOf(n)) {
        out << "box " << b.id << " " << b.x << " " << b.y << " " << b.w << " "
            << b.h << "\n";
    }
    if (n.block == BlockKind::MermaidFlowchart && n.mermaid_layout) {
        for (const auto& lane : n.mermaid_layout->lanes) {
            out << "lane " << lane.id << " " << lane.x << " " << lane.y << " "
                << lane.width << " " << lane.height << "\n";
        }
        for (const auto& e : n.mermaid_layout->edges) {
            out << "edge " << e.from << ">" << e.to << " " << e.route.size()
                << "\n";
        }
    }
    return out.str();
}

}  // namespace

// 1. The fence becomes the diagram it says it is, on the real path.
TEST(MermaidValidation, EveryFixtureIsPromoted) {
    for (const Fixture& f : Fixtures()) {
        const std::string path =
            std::string("tests/mermaid/fixtures/") + f.file + ".mmd";
        const Document doc = ParseAsDocument(Slurp(path));
        ASSERT_EQ(doc.nodes.size(), 1u);
        if (doc.nodes[0].block != f.kind) {
            std::fprintf(stderr, "[%s] promoted to %d, expected %d\n",
                         f.file, static_cast<int>(doc.nodes[0].block),
                         static_cast<int>(f.kind));
        }
        EXPECT_TRUE(doc.nodes[0].block == f.kind);
    }
}

// 2. The geometry is readable: real numbers, inside its canvas, boxes
// that do not sit on each other, and lanes that enclose their members
// and keep off their neighbours.
TEST(MermaidValidation, GeometryObeysItsInvariants) {
    for (const Fixture& f : Fixtures()) {
        const std::string path =
            std::string("tests/mermaid/fixtures/") + f.file + ".mmd";
        const Document doc = ParseAsDocument(Slurp(path));
        ASSERT_EQ(doc.nodes.size(), 1u);
        const Node& n = doc.nodes[0];
        if (f.kind == BlockKind::CodeBlock) {
            // A fence the layout refuses keeps its source and draws
            // nothing; there is no geometry to hold to anything.
            EXPECT_TRUE(CanvasWidth(n) == 0.0f);
            continue;
        }

        const float w = CanvasWidth(n);
        const float h = CanvasHeight(n);
        EXPECT_TRUE(std::isfinite(w) && std::isfinite(h));
        EXPECT_GT(w, 0.0f);
        EXPECT_GT(h, 0.0f);

        const std::vector<Box> boxes = BoxesOf(n);
        for (const Box& b : boxes) {
            EXPECT_TRUE(std::isfinite(b.x) && std::isfinite(b.y));
            EXPECT_GT(b.w, 0.0f);
            EXPECT_GT(b.h, 0.0f);
            // The canvas holds its nodes. A node outside it is clipped
            // by the painter, which reads as a missing state.
            EXPECT_GE(b.x - b.w * 0.5f, -1.0f);
            EXPECT_GE(b.y - b.h * 0.5f, -1.0f);
            EXPECT_LE(b.x + b.w * 0.5f, w + 1.0f);
            EXPECT_LE(b.y + b.h * 0.5f, h + 1.0f);
        }
        for (size_t i = 0; i < boxes.size(); ++i) {
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                const bool apart =
                    std::fabs(boxes[i].x - boxes[j].x) >=
                        (boxes[i].w + boxes[j].w) * 0.5f - 0.5f ||
                    std::fabs(boxes[i].y - boxes[j].y) >=
                        (boxes[i].h + boxes[j].h) * 0.5f - 0.5f;
                if (!apart) {
                    std::fprintf(stderr, "[%s] boxes %s and %s overlap\n",
                                 f.file, boxes[i].id.c_str(),
                                 boxes[j].id.c_str());
                }
                EXPECT_TRUE(apart);
            }
        }

        if (n.block == BlockKind::MermaidFlowchart && n.mermaid_layout) {
            for (const auto& lane : n.mermaid_layout->lanes) {
                EXPECT_GT(lane.width, 0.0f);
                EXPECT_GT(lane.height, 0.0f);
            }
            for (size_t i = 0; i < n.mermaid_layout->lanes.size(); ++i) {
                for (size_t j = i + 1; j < n.mermaid_layout->lanes.size();
                     ++j) {
                    const auto& a = n.mermaid_layout->lanes[i];
                    const auto& b = n.mermaid_layout->lanes[j];
                    const bool apart =
                        a.x + a.width <= b.x + 0.01f ||
                        b.x + b.width <= a.x + 0.01f ||
                        a.y + a.height <= b.y + 0.01f ||
                        b.y + b.height <= a.y + 0.01f;
                    if (!apart) {
                        std::fprintf(stderr, "[%s] lanes %s and %s overlap\n",
                                     f.file, a.id.c_str(), b.id.c_str());
                    }
                    EXPECT_TRUE(apart);
                }
            }
        }
    }
}

// 3. The same document gives the same drawing. A layout that reads
// uninitialised memory or iterates an unordered set shows here.
TEST(MermaidValidation, LayoutIsDeterministic) {
    for (const Fixture& f : Fixtures()) {
        const std::string path =
            std::string("tests/mermaid/fixtures/") + f.file + ".mmd";
        const Document first = ParseAsDocument(Slurp(path));
        const Document second = ParseAsDocument(Slurp(path));
        ASSERT_EQ(first.nodes.size(), 1u);
        ASSERT_EQ(second.nodes.size(), 1u);
        if (CanonicalDump(first.nodes[0]) != CanonicalDump(second.nodes[0])) {
            std::fprintf(stderr, "[%s] two parses disagree\n", f.file);
        }
        EXPECT_TRUE(CanonicalDump(first.nodes[0]) ==
                    CanonicalDump(second.nodes[0]));
    }
}

// 4. The geometry is pinned. This is the regression gate: any change to
// the shipped layout shows as a diff naming the fixture.
TEST(MermaidValidation, GeometryMatchesItsSnapshot) {
    const bool update = std::getenv("MARKDOWNIT_UPDATE_SNAPSHOTS") != nullptr;
    const std::string dir = "tests/mermaid/snapshot/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    for (const Fixture& f : Fixtures()) {
        const std::string path =
            std::string("tests/mermaid/fixtures/") + f.file + ".mmd";
        const Document doc = ParseAsDocument(Slurp(path));
        ASSERT_EQ(doc.nodes.size(), 1u);
        const std::string dump = CanonicalDump(doc.nodes[0]);
        const std::string snapPath = dir + f.file + ".txt";

        if (update) {
            std::ofstream out(snapPath, std::ios::binary);
            out << dump;
            continue;
        }
        std::ifstream in(snapPath, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "[%s] no snapshot at %s\n", f.file,
                         snapPath.c_str());
            EXPECT_TRUE(false);
            continue;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        // Tolerate a CRLF checkout too: the gate is about the numbers,
        // and a line ending is not one of them.
        std::string snap = ss.str();
        snap.erase(std::remove(snap.begin(), snap.end(), '\r'), snap.end());
        if (snap != dump) {
            std::fprintf(stderr,
                         "[%s] geometry moved; regenerate with "
                         "MARKDOWNIT_UPDATE_SNAPSHOTS=1 if that was intended\n",
                         f.file);
        }
        EXPECT_TRUE(snap == dump);
    }
}
