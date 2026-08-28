# Mermaid-diagrammer i MarkDownIt: implementeringsplan

> **For Hermes:** Brug subagent-driven-development til at implementere denne plan opgave for opgave.

**Mål:** Vise indlejrede ```mermaid-kodeblokke som tegnede diagrammer i stedet for rå tekst.

**Arkitektur:** En native Direct2D-diagramrenderer. Parseren beholder sprogtaggen på fence'en, en ny mermaid-parser oversætter kildeteksten til en abstrakt graf, en layout-motor placerer noder, og renderer.cpp tegner grafen med de eksisterende Direct2D-primitiver. Ingen JavaScript-motor, ingen netværk, ingen nye afhængigheder.

**Tech stack:** C++17, Direct2D 1.1, DirectWrite, md4c. Samme værktøjskæde som resten af projektet.

---

## Nuværende kontekst

Verificeret i koden ved planlægningen:

| Forhold | Status |
|---|---|
| `Node` i `src/dom.h:85` | Har `raw` (UTF-32 kodetekst), men **ingen sprogtag** |
| `parser.cpp:237` `MD_BLOCK_CODE` | Ignorerer `MD_BLOCK_CODE_DETAIL::lang` |
| `renderer.cpp:849` | `DrawCodeBlock` for alle kodeblokke |
| `renderer.cpp:679` | Måling af kodeblokhøjde i `Measure` |
| `imagehelper.cpp` | WIC + WinHTTP, men laver ingen caching |
| `renderer.cpp:54` | `BlockSpacing` skelner allerede på `BlockKind` |

To arkitektoniske valg blev fravalgt:

1. **WebView2 + mermaid.js.** Ville give fuld Mermaid-understøttelse, men bryder projektets kerneløfte om et enkelt exe uden runtime-afhængigheder. WebView2-runtime findes ikke garanteret på en låst virksomheds-PC.
2. **Fjernrendering via kroki.io eller mermaid.ink.** Simpelt at bygge, men sender dokumentindhold til en tredjepart. Uacceptabelt på et låst virksomhedsnetværk, og diagrammer virker ikke offline.

Planen dækker derfor et **realistisk delmængde** af Mermaid: `flowchart` og `graph` (retning TD, TB, LR, RL) samt `sequenceDiagram`. Det dækker langt de fleste diagrammer i teknisk dokumentation. Ikke-understøttede diagramtyper falder tilbage til den nuværende kodeblokvisning, så intet går tabt.

## Antagelser

- Brugeren redigerer markdown i samme vindue, så diagrammer skal gentegnes ved reparse.
- Diagrammer skal fungere i view mode og edit mode. I source mode vises rå tekst som i dag.
- Et diagram er et enkelt blok-element uden tekstmarkering indeni i første version.

---

## Foreslået fremgangsmåde

Fem faser. Hver fase er selvstændigt værdifuld og kan bygges og testes for sig.

- **Fase 1:** Sprogtaggen bevares gennem parseren. Ingen synlig ændring.
- **Fase 2:** Mermaid-kildetekst til graf-datamodel. Ren logik, testbar uden Windows.
- **Fase 3:** Layoutmotor placerer noder og kanter. Ren logik, testbar uden Windows.
- **Fase 4:** Direct2D-tegning af den beregnede layout.
- **Fase 5:** Cache, fejlhåndtering og integration med edit mode.

Fase 2 og 3 er portabel C++ og kan derfor syntakstjekkes og enhedstestes lokalt i WSL. Kun fase 4 kræver CI som compile gate.

---

## Fase 1: Sprogtag gennem parseren

### Opgave 1: Tilføj `lang`-felt til Node

**Formål:** Give DOM'en et sted at gemme fence-sprogtaggen.

**Filer:**
- Rediger: `src/dom.h:95`

**Trin 1: Tilføj feltet**

I `struct Node`, lige efter `raw`:

```cpp
    std::u32string             raw;          // code block raw text (UTF-32)
    std::string                lang;         // fence info string, e.g. "mermaid"
```

**Trin 2: Verificer**

Kør: `g++ -std=c++17 -fsyntax-only -Isrc src/parser.cpp`
Forventet: ingen fejl.

**Trin 3: Commit**

```bash
git add src/dom.h
git commit -m "feat: add lang field to Node for fence info strings"
```

---

### Opgave 2: Udfyld `lang` fra md4c

**Formål:** Læse sprogtaggen fra `MD_BLOCK_CODE_DETAIL` ved blokstart.

**Filer:**
- Rediger: `src/parser.cpp:237-242`

**Trin 1: Skriv den fejlende test**

I `tests/parser_smoke.cpp`:

```cpp
TEST(ParserFenceLang) {
    Document doc = ParseMarkdown("```mermaid\ngraph TD\n```\n");
    ASSERT_EQ(doc.nodes.size(), 1u);
    ASSERT_EQ(doc.nodes[0].block, BlockKind::CodeBlock);
    ASSERT_EQ(doc.nodes[0].lang, std::string("mermaid"));
}
```

**Trin 2: Kør testen og se den fejle**

Forventet: `lang` er tom, testen fejler.

**Trin 3: Implementer**

Erstat `case MD_BLOCK_CODE:` i `parser.cpp:237`:

```cpp
        case MD_BLOCK_CODE: {
            int idx = push_node(*ctx, Node{});
            ctx->doc->nodes[idx].block = BlockKind::CodeBlock;
            auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            if (d && d->lang.text && d->lang.size > 0) {
                ctx->doc->nodes[idx].lang.assign(d->lang.text, d->lang.size);
                // Info string may carry extra words: "mermaid theme=dark".
                size_t sp = ctx->doc->nodes[idx].lang.find(' ');
                if (sp != std::string::npos) {
                    ctx->doc->nodes[idx].lang.resize(sp);
                }
                for (auto& c : ctx->doc->nodes[idx].lang) {
                    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                }
            }
            ctx->block_stack.push_back({type, idx, false, true});
            break;
        }
```

**Trin 4: Kør testen igen**

Forventet: PASS.

**Trin 5: Commit**

```bash
git add src/parser.cpp tests/parser_smoke.cpp
git commit -m "feat: capture fence info string into Node::lang"
```

---

## Fase 2: Mermaid-kildetekst til graf

### Opgave 3: Definer diagram-datamodellen

**Formål:** En backend-uafhængig beskrivelse af et diagram.

**Filer:**
- Opret: `src/mermaid.h`

```cpp
#pragma once

// Mermaid subset parser. Produces a backend-independent graph model.
// No Windows dependencies: this header is portable C++17 and unit tested.

#include <string>
#include <vector>

namespace mermaid {

enum class DiagramType { Unknown, Flowchart, Sequence };
enum class Direction   { TD, LR, RL, BT };
enum class NodeShape   { Rect, RoundRect, Stadium, Diamond, Circle };
enum class EdgeStyle   { Solid, Dotted, Thick };
enum class ArrowHead   { None, Arrow, Cross, Circle };

struct GraphNode {
    std::string id;
    std::string label;
    NodeShape   shape = NodeShape::Rect;
};

struct GraphEdge {
    int       from = -1;      // index into nodes
    int       to   = -1;
    std::string label;
    EdgeStyle style = EdgeStyle::Solid;
    ArrowHead head  = ArrowHead::Arrow;
};

// One message in a sequenceDiagram.
struct SeqMessage {
    int       from = -1;      // index into nodes (participants)
    int       to   = -1;
    std::string label;
    EdgeStyle style = EdgeStyle::Solid;
};

struct Diagram {
    DiagramType             type = DiagramType::Unknown;
    Direction               dir  = Direction::TD;
    std::vector<GraphNode>  nodes;
    std::vector<GraphEdge>  edges;
    std::vector<SeqMessage> messages;
    std::string             error;   // non-empty means parse failed
};

// Parse mermaid source. On failure, result.type is Unknown and
// result.error explains why, so the caller can fall back to code rendering.
Diagram Parse(const std::string& src);

}  // namespace mermaid
```

**Commit:**

```bash
git add src/mermaid.h
git commit -m "feat: add mermaid diagram model header"
```

---

### Opgave 4: Genkend diagramtype og retning

**Formål:** Læse den første meningsfulde linje.

**Filer:**
- Opret: `src/mermaid.cpp`
- Opret: `tests/mermaid_test.cpp`

**Trin 1: Skriv de fejlende tests**

```cpp
TEST(MermaidDetectsFlowchart) {
    auto d = mermaid::Parse("flowchart LR\n  A --> B\n");
    ASSERT_EQ(d.type, mermaid::DiagramType::Flowchart);
    ASSERT_EQ(d.dir, mermaid::Direction::LR);
}

TEST(MermaidDetectsGraphAlias) {
    auto d = mermaid::Parse("graph TD\n  A --> B\n");
    ASSERT_EQ(d.type, mermaid::DiagramType::Flowchart);
    ASSERT_EQ(d.dir, mermaid::Direction::TD);
}

TEST(MermaidRejectsUnknownType) {
    auto d = mermaid::Parse("gantt\n  title X\n");
    ASSERT_EQ(d.type, mermaid::DiagramType::Unknown);
    ASSERT_TRUE(!d.error.empty());
}
```

**Trin 2: Kør og se dem fejle.** Forventet: link-fejl, `Parse` findes ikke.

**Trin 3: Implementer** linjesplit, trimning, kommentarfjernelse (`%%`) og typegenkendelse i `mermaid.cpp`. `TB` behandles som `TD`.

**Trin 4: Kør testene.** Forventet: 3 passed.

**Trin 5: Tilføj til CMake.** Læg `src/mermaid.cpp` i `MarkDownIt`-target (`CMakeLists.txt:87`) og `tests/mermaid_test.cpp` plus `src/mermaid.cpp` i `MarkDownIt.tests`-target (`CMakeLists.txt:189`).

**Commit:** `feat: detect mermaid diagram type and direction`

---

### Opgave 5: Parse flowchart-noder og former

**Formål:** Genkende `A[Tekst]`, `B(Rund)`, `C{Beslutning}`, `D((Cirkel))`, `E([Stadium])`.

**Tests:**

```cpp
TEST(MermaidNodeShapes) {
    auto d = mermaid::Parse(
        "graph TD\n"
        "  A[Firkant] --> B(Rund)\n"
        "  B --> C{Valg}\n");
    ASSERT_EQ(d.nodes.size(), 3u);
    ASSERT_EQ(d.nodes[0].label, std::string("Firkant"));
    ASSERT_EQ(d.nodes[0].shape, mermaid::NodeShape::Rect);
    ASSERT_EQ(d.nodes[1].shape, mermaid::NodeShape::RoundRect);
    ASSERT_EQ(d.nodes[2].shape, mermaid::NodeShape::Diamond);
}

TEST(MermaidNodeWithoutLabelUsesId) {
    auto d = mermaid::Parse("graph TD\n  A --> B\n");
    ASSERT_EQ(d.nodes[0].label, std::string("A"));
}
```

Vigtigt: en node kan nævnes flere gange. Første gang med etiket vinder, senere nævnelser må ikke overskrive etiketten med id'et. Brug en id-til-indeks-map.

Understøt også citerede etiketter: `A["Tekst med ]"]`.

**Commit:** `feat: parse mermaid flowchart nodes and shapes`

---

### Opgave 6: Parse flowchart-kanter

**Formål:** Genkende `-->`, `---`, `-.->`, `==>`, og kantetiketter `-->|tekst|` samt `-- tekst -->`.

**Tests:**

```cpp
TEST(MermaidEdgeLabel) {
    auto d = mermaid::Parse("graph LR\n  A -->|ja| B\n");
    ASSERT_EQ(d.edges.size(), 1u);
    ASSERT_EQ(d.edges[0].label, std::string("ja"));
    ASSERT_EQ(d.edges[0].head, mermaid::ArrowHead::Arrow);
}

TEST(MermaidDottedEdge) {
    auto d = mermaid::Parse("graph LR\n  A -.-> B\n");
    ASSERT_EQ(d.edges[0].style, mermaid::EdgeStyle::Dotted);
}

TEST(MermaidChainedEdges) {
    auto d = mermaid::Parse("graph LR\n  A --> B --> C\n");
    ASSERT_EQ(d.nodes.size(), 3u);
    ASSERT_EQ(d.edges.size(), 2u);
}
```

**Commit:** `feat: parse mermaid flowchart edges with labels and styles`

---

### Opgave 7: Parse sequenceDiagram

**Formål:** Genkende `participant`, `actor` og beskeder `A->>B: tekst`.

**Tests:**

```cpp
TEST(MermaidSequence) {
    auto d = mermaid::Parse(
        "sequenceDiagram\n"
        "  participant Bruger\n"
        "  participant API\n"
        "  Bruger->>API: Hent data\n"
        "  API-->>Bruger: Svar\n");
    ASSERT_EQ(d.type, mermaid::DiagramType::Sequence);
    ASSERT_EQ(d.nodes.size(), 2u);
    ASSERT_EQ(d.messages.size(), 2u);
    ASSERT_EQ(d.messages[1].style, mermaid::EdgeStyle::Dotted);
}

TEST(MermaidSequenceImplicitParticipant) {
    auto d = mermaid::Parse("sequenceDiagram\n  A->>B: hej\n");
    ASSERT_EQ(d.nodes.size(), 2u);
}
```

Deltagere skal beholde den rækkefølge de først optræder i, da det bestemmer kolonneplaceringen.

**Commit:** `feat: parse mermaid sequence diagrams`

---

## Fase 3: Layoutmotor

### Opgave 8: Definer layout-resultatet

**Formål:** Geometri i logiske enheder, uafhængigt af Direct2D.

**Filer:**
- Opret: `src/mermaidlayout.h`

```cpp
#pragma once

// Turns a parsed mermaid::Diagram into positioned geometry.
// Portable C++17: no Windows types, so it is unit testable in WSL.

#include "mermaid.h"
#include <string>
#include <vector>

namespace mermaid {

struct LaidOutNode {
    float x = 0, y = 0, w = 0, h = 0;   // top-left plus size, in DIPs
    std::string label;
    NodeShape shape = NodeShape::Rect;
};

struct LaidOutEdge {
    std::vector<std::pair<float, float>> points;  // polyline, >= 2 points
    std::string label;
    float labelX = 0, labelY = 0;
    EdgeStyle style = EdgeStyle::Solid;
    ArrowHead head  = ArrowHead::Arrow;
};

struct Layout {
    std::vector<LaidOutNode> nodes;
    std::vector<LaidOutEdge> edges;
    float width = 0, height = 0;
};

// measureText returns the width in DIPs of a label at the diagram font size.
// The renderer passes a DirectWrite-backed callback; tests pass a stub.
using MeasureFn = float (*)(const std::string& text, void* ctx);

Layout ComputeLayout(const Diagram& d, float maxWidth,
                     MeasureFn measure, void* measureCtx);

}  // namespace mermaid
```

**Commit:** `feat: add mermaid layout header`

---

### Opgave 9: Lagdelt layout for flowcharts

**Formål:** Placere noder i lag efter afstand fra rodnoder.

Algoritmen er en forenklet Sugiyama:

1. Tildel lag med længste-sti fra noder uden indgående kanter. Cyklusser brydes ved at ignorere kanter tilbage til et allerede besøgt lag.
2. Sortér inden for hvert lag efter gennemsnitspositionen af forgængerne, så kantkrydsninger reduceres.
3. Beregn nodestørrelser fra `measure` plus indre margen.
4. Placer lagene langs hovedaksen bestemt af `Direction`, og centrer hvert lag på tværsaksen.
5. Kanter bliver polylinjer fra kant til kant af noderne, med et knæk midt imellem lagene.

**Tests:**

```cpp
static float StubMeasure(const std::string& s, void*) {
    return static_cast<float>(s.size()) * 8.0f;
}

TEST(LayoutAssignsLayers) {
    auto d = mermaid::Parse("graph TD\n  A --> B\n  B --> C\n");
    auto l = mermaid::ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 3u);
    ASSERT_TRUE(l.nodes[0].y < l.nodes[1].y);
    ASSERT_TRUE(l.nodes[1].y < l.nodes[2].y);
}

TEST(LayoutHorizontalDirection) {
    auto d = mermaid::Parse("graph LR\n  A --> B\n");
    auto l = mermaid::ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_TRUE(l.nodes[0].x < l.nodes[1].x);
}

TEST(LayoutHandlesCycle) {
    auto d = mermaid::Parse("graph TD\n  A --> B\n  B --> A\n");
    auto l = mermaid::ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 2u);   // must terminate, not hang
}
```

Cyklustesten er vigtig: en naiv længste-sti-beregning løber uendeligt på en cyklus.

**Commit:** `feat: layered layout for mermaid flowcharts`

---

### Opgave 10: Layout for sequenceDiagram

**Formål:** Deltagere som kolonner, beskeder som vandrette pile ned ad tidsaksen.

Deltagerkasser øverst, livline som lodret linje nedad, og en besked pr. række med fast rækkehøjde. Diagrammets bredde er summen af kolonnebredderne, højden er header plus antal beskeder gange rækkehøjde.

**Test:**

```cpp
TEST(LayoutSequenceColumns) {
    auto d = mermaid::Parse(
        "sequenceDiagram\n  A->>B: en\n  B->>A: to\n");
    auto l = mermaid::ComputeLayout(d, 800.0f, StubMeasure, nullptr);
    ASSERT_EQ(l.nodes.size(), 2u);
    ASSERT_TRUE(l.nodes[0].x < l.nodes[1].x);
    ASSERT_EQ(l.edges.size(), 2u);
    ASSERT_TRUE(l.edges[0].points[0].second < l.edges[1].points[0].second);
}
```

**Commit:** `feat: layout for mermaid sequence diagrams`

---

## Fase 4: Tegning med Direct2D

### Opgave 11: Farver til diagrammer i temaet

**Formål:** Diagrammer skal følge lyst og mørkt tema.

**Filer:**
- Rediger: `src/theme.h`

Tilføj til `Palette`: `diagramNodeBg`, `diagramNodeBorder`, `diagramEdge`, `diagramText`. Genbrug de eksisterende kodeblokfarver som udgangspunkt, så diagrammer ser ud som en del af dokumentet.

**Commit:** `feat: add diagram colors to palette`

---

### Opgave 12: DrawDiagram i rendereren

**Formål:** Tegne en `Layout` med Direct2D.

**Filer:**
- Rediger: `src/renderer.h`, `src/renderer.cpp`

Ny metode:

```cpp
    void DrawDiagram(ID2D1RenderTarget* rt, IDWriteFactory* dw,
                     const mermaid::Layout& layout,
                     float x, float y, float width, float& outH);
```

Tegneækkefølge: baggrundskort, derefter kanter under noder, derefter noder, derefter etiketter øverst.

Former:
- `Rect`: `FillRectangle` plus `DrawRectangle`.
- `RoundRect` og `Stadium`: `ID2D1RoundedRectangleGeometry`, hvor stadium bruger radius lig halv højde.
- `Diamond`: `ID2D1PathGeometry` med fire punkter.
- `Circle`: `FillEllipse` plus `DrawEllipse`.

Pile: en lille udfyldt trekant ved slutpunktet, roteret efter det sidste linjesegments retning. Stiplede kanter bruger `ID2D1StrokeStyle` med `D2D1_DASH_STYLE_DASH`, tykke bruger større `strokeWidth`.

**Kritisk:** alle COM-objekter skal frigives på alle udgangsstier. Audit-runden fandt allerede en `selBrush`-lækage i `DrawTable`. Brug en lille RAII-wrapper eller en samlet `cleanup`-blok, ikke spredte `Release`-kald.

**Verifikation:** CI skal være grøn, derefter manuel test med et testdokument.

**Commit:** `feat: draw mermaid diagrams with Direct2D`

---

### Opgave 13: Kobl diagrammer ind i render- og målestien

**Formål:** Få `mermaid`-blokke til at gå til `DrawDiagram` i stedet for `DrawCodeBlock`.

**Filer:**
- Rediger: `src/renderer.cpp:679` (måling) og `src/renderer.cpp:849` (tegning)

Begge steder får samme forgrening:

```cpp
        if (n.block == BlockKind::CodeBlock && n.lang == "mermaid") {
            const mermaid::Layout* lay = diagram_cache_.Get(n.srcOffset, n.raw, drawW);
            if (lay) {
                // measure: blockH = lay->height + 2.0f * m.codePad;
                // draw:    DrawDiagram(rt, dw, *lay, drawX, curY, drawW, blockH);
            } else {
                // Parse or layout failed: fall through to DrawCodeBlock.
            }
        }
```

**Måling og tegning skal bruge nøjagtig samme højde.** Auditen fandt allerede scrollbar-drift, fordi `Measure` og render var uenige om billedhøjder. Gentag ikke fejlen: begge stier skal kalde samme cache og samme højdeformel.

**Commit:** `feat: route mermaid code blocks to the diagram renderer`

---

## Fase 5: Cache, fejl og integration

### Opgave 14: Diagram-cache

**Formål:** Undgå at parse og layoute på hver eneste paint-frame.

**Filer:**
- Opret: `src/diagramcache.h` og `src/diagramcache.cpp`

Nøglen er hash af kildeteksten plus den afrundede bredde. Værdien er den færdige `Layout`. Cachen ryddes ved reparse, ved temaskift og ved zoom.

Dette er ikke valgfrit. `Measure` og `Render` kalder begge pr. frame, så uden cache køres layoutalgoritmen to gange for hvert diagram ved hver eneste optegning.

**Test:**

```cpp
TEST(DiagramCacheHits) {
    DiagramCache c;
    auto* a = c.Get(0, U"graph TD\n A-->B\n", 800.0f);
    auto* b = c.Get(0, U"graph TD\n A-->B\n", 800.0f);
    ASSERT_TRUE(a == b);       // same pointer means it was cached
}
```

**Commit:** `feat: cache parsed and laid out diagrams`

---

### Opgave 15: Nedgradering ved fejl

**Formål:** Et ugyldigt diagram må aldrig skjule indholdet eller crashe.

Regler:

- `Parse` fejler, eller diagramtypen er ikke understøttet: tegn blokken som en almindelig kodeblok, præcis som i dag.
- Layout giver nul noder: samme nedgradering.
- Ingen exceptions ud af `Parse` eller `ComputeLayout`. Ugyldigt input skal give `error`, ikke et kast.

**Test:**

```cpp
TEST(MermaidGarbageDoesNotThrow) {
    auto d = mermaid::Parse("graph TD\n  ]]][[--> \n  \x01\x02\n");
    ASSERT_EQ(d.nodes.size(), d.nodes.size());  // reaching here means no throw
}
```

**Commit:** `feat: fall back to code rendering for invalid diagrams`

---

### Opgave 16: Edit mode og reparse

**Formål:** Diagrammet opdateres, mens brugeren skriver i fence'en.

Reparse-debouncen findes allerede (`OnReparseTimer`, timer ID 2, netop rettet i audit-runden). Når reparse sker, skal diagram-cachen ryddes for de berørte blokke.

I edit mode er der to mulige adfærdsmønstre:

1. Vis altid diagrammet, også når markøren står i blokken.
2. Vis rå tekst, når markøren er inde i blokken, og diagram ellers.

Mulighed 2 er langt mere brugbar, for man kan ikke redigere en tegning. Det svarer til hvordan Obsidian og Typora opfører sig, og det genbruger den eksisterende viden om markørens blok.

**Commit:** `feat: show mermaid source while the caret is inside the block`

---

### Opgave 17: Testdokument og README

**Filer:**
- Opret: `tests/fixtures/mermaid.md` med flowchart TD, flowchart LR, sequenceDiagram, et diagram med etiketter og former, og et ugyldigt diagram.
- Rediger: `README.md` med et afsnit om understøttede diagramtyper og de kendte begrænsninger.

Vær ærlig i README om hvad der ikke er understøttet: gantt, klassediagrammer, ER-diagrammer, state, pie, subgraphs og styling-direktiver.

**Commit:** `docs: document mermaid diagram support and limits`

---

## Filer der ændres

| Fil | Ændring |
|---|---|
| `src/dom.h` | Nyt `lang`-felt på `Node` |
| `src/parser.cpp` | Udfylder `lang` fra `MD_BLOCK_CODE_DETAIL` |
| `src/mermaid.h` / `.cpp` | Ny: mermaid-parser |
| `src/mermaidlayout.h` / `.cpp` | Ny: layoutmotor |
| `src/diagramcache.h` / `.cpp` | Ny: cache |
| `src/renderer.h` / `.cpp` | Ny `DrawDiagram`, forgrening i `Measure` og `Render` |
| `src/theme.h` | Diagramfarver i `Palette` |
| `CMakeLists.txt` | Nye kildefiler i begge targets |
| `tests/mermaid_test.cpp` | Ny testfil |
| `tests/parser_smoke.cpp` | Test af `lang`-feltet |

## Validering

**Lokalt i WSL** (fase 1 til 3, portabel C++):

```bash
g++ -std=c++17 -fsyntax-only -Isrc src/mermaid.cpp src/mermaidlayout.cpp
```

**I CI** (compile gate for hele projektet):

```bash
git push origin <branch>
gh run watch <id> --exit-status
gh run download <id> -n MarkDownIt-x64 -D /tmp/md-mermaid
```

Testjobbet i CI bygger med `-DBUILD_TESTS=ON` og kører `MarkDownIt.tests.exe`, så mermaid-testene kører automatisk.

**Manuelt:** åbn `tests/fixtures/mermaid.md`, kontroller at alle fem diagrammer ser rigtige ud i lyst og mørkt tema, ved forskellige zoomniveauer og ved vinduesbredder der tvinger ombrydning.

## Risici og afvejninger

| Risiko | Håndtering |
|---|---|
| **Mermaid-syntaksen er stor.** Vi dækker aldrig det hele. | Afgræns til flowchart og sequence, og nedgrader resten synligt og sikkert. Skriv begrænsningerne i README. |
| **Layoutkvalitet.** Rigtig Mermaid bruger dagre med kantkrydsningsminimering. Vores bliver enklere. | Acceptabelt for typiske diagrammer på under 20 noder. Store grafer bliver rodede, men læselige. |
| **Ydelse.** Layout er dyrere end at tegne tekst. | Cachen i opgave 14 gør det til en engangsomkostning pr. redigering. |
| **Kodemængde.** Realistisk 1500 til 2000 nye linjer på en kodebase på 10.700. | Fasedelt, så hver fase kan stoppes eller udskydes uden at efterlade halvfærdig kode. |
| **Måling og tegning kan blive uenige** om højden, hvilket gav scrollbar-fejl før. | Begge stier bruger samme cache og samme formel. Eksplicit nævnt i opgave 13. |
| **COM-lækager** i den nye tegnekode. | RAII-wrapper i stedet for manuelle `Release`-kald, jf. `DrawTable`-lækagen fra auditen. |

## Åbne spørgsmål

1. **Skal diagrammer kunne markeres og kopieres som tekst?** Forslag: nej i første version. Fence-teksten er stadig tilgængelig i source mode.
2. **Skal der være en knap til at slå diagramvisning fra?** Forslag: ja, en indstilling i `settings.cpp`, men først efter fase 4 virker.
3. **Skal `subgraph` understøttes?** Det er almindeligt i arkitekturdiagrammer, men kræver indlejret layout. Forslag: udskyd til efter fase 5.
4. **Eksport af diagram til PNG?** Ligger uden for denne plan.

## Estimat

| Fase | Realistisk estimat | Pessimistisk estimat |
|---|---|---|
| Fase 1: sprogtag | 0,5 dag | 1 dag |
| Fase 2: mermaid-parser | 2 dage | 4 dage |
| Fase 3: layoutmotor | 3 dage | 6 dage |
| Fase 4: Direct2D-tegning | 3 dage | 6 dage |
| Fase 5: cache og integration | 2 dage | 4 dage |
| **I alt** | **10,5 dage** | **21 dage** |

Det pessimistiske estimat afspejler, at layout og tegning er de to steder, hvor detaljer som pilerotation, formgeometri og lagsortering typisk kræver flere runder.
