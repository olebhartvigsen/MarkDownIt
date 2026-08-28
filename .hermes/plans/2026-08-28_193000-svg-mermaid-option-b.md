# SVG-motor og mermaid via SVG (Option B) — implementeringsplan

> **For Hermes:** Brug subagent-driven-development til at implementere denne plan opgave for opgave.

**Mål:** Give MarkDownIt en ægte SVG-motor, og lade mermaid-diagrammer blive
tegnet som skarp vektorgrafik i stedet for den nuværende native renderer.

**Arkitektur:** mermaid.js kører i en skjult WebView2 og returnerer en
SVG-streng. En ny hybrid SVG-renderer tegner den: former gennem Direct2D's
`ID2D1SvgDocument`, og tekst gennem DirectWrite, fordi Direct2D ikke kan tegne
SVG-tekst. Samme renderer betjener også almindelige SVG-billeder og
` ```svg `-blokke.

**Tech stack:** C++17, Direct2D 1.1 (`ID2D1DeviceContext5`), DirectWrite,
WebView2 SDK (kun til mermaid), md4c.

---

## Kritisk forudsætning (verificeret)

Microsoft dokumenterer hvilke SVG-elementer Direct2D kan tegne
(<https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support>).
Listen er: `circle`, `clipPath`, `defs`, `desc`, `ellipse`, `g`, `image`,
`line`, `linearGradient`, `path`, `polygon`, `polyline`, `radialGradient`,
`rect`, `stop`, `svg`, `title`, `use`.

`text` og `tspan` står ikke på listen. `font-family` og `font-size` står heller
ikke blandt de understøttede præsentationsattributter. Direct2D ignorerer
ukendte elementer i stilhed.

**Konsekvens:** Et rent `DrawSvgDocument`-kald tegner et mermaid-diagram helt
uden etiketter. Det er den samme fejl som i dag, blot med en anden årsag. Derfor
skal tekst hentes ud af SVG-træet og tegnes særskilt med DirectWrite.

Anden vigtig detalje: mermaid pakker som standard etiketter i `<foreignObject>`
med HTML indeni. Det skal slås fra med `htmlLabels: false`, ellers findes der
slet ingen `<text>`-elementer at tegne.

---

## Nuværende tilstand

Branch `feat/mermaid-phase1` ligger 10 commits foran `main` og indeholder den
native renderer, som ikke virker godt nok:

| Commit | Indhold |
|--------|---------|
| `6a9c1d3` | `Node::lang` fra fence info string |
| `68a4890` | mermaid-parser |
| `8c29fdd` | layoutmotor |
| `12e8b4f` | Direct2D-tegning |
| `e813223` | cache, edit mode, fallback |
| `766f884` | scroll-fix |
| `9ff2b9f`, `3991cb9`, `0d669c7`, `1ad2b8d` | tekstmåling og clipping |

Commit `6a9c1d3` beholdes. Resten rulles tilbage.

`main` indeholder allerede Insert-fanen med tabel-grid (`c6213a2`, `40d0ed4`).

Render-målet er i dag `ID2D1HwndRenderTarget` oprettet fra `ID2D1Factory`
(`src/app.cpp:1451` og `src/app.cpp:1488`). SVG kræver `ID2D1DeviceContext5`,
så det skal undersøges om `QueryInterface` rækker.

---

## Faser

| Fase | Indhold | Opgaver |
|------|---------|---------|
| 0 | Tilbagerulning | 1 |
| 1 | Direct2D-kontekst opgraderes | 2-3 |
| 2 | SVG-motor med hybrid tekst | 4-9 |
| 3 | SVG i markdown | 10-12 |
| 4 | WebView2 og mermaid | 13-18 |
| 5 | Cache, fallback, dokumentation | 19-22 |

---

## Fase 0: Tilbagerulning

### Opgave 1: Ny branch fra sprogtag-commit

**Formål:** Fjerne den native renderer, men beholde `Node::lang`.

**Trin 1: Opret branch**

```bash
cd /home/au19277/projekter/MarkDownIt
git checkout -b feat/svg-engine 6a9c1d3
```

**Trin 2: Bekræft at kun sprogtaget er med**

```bash
git log --oneline main..HEAD
```

Forventet: én commit, `6a9c1d3`.

**Trin 3: Bekræft at de native filer er væk**

```bash
ls src/mermaid* src/diagramcache* 2>&1
```

Forventet: `No such file or directory`.

**Trin 4: Bekræft at sprogtaget stadig er der**

```bash
grep -n "lang" src/dom.h
```

Forventet: feltet `std::string lang;` findes.

**Trin 5: Commit ikke nødvendig** (branch peger allerede rigtigt).

---

## Fase 1: Direct2D-kontekst

### Opgave 2: Undersøg om render-målet kan give ID2D1DeviceContext5

**Formål:** Finde ud af om `QueryInterface` rækker, eller om render-målet skal
bygges om.

**Filer:**
- Ændr: `src/app.cpp:1451` (factory), `src/app.cpp:1488` (render target)
- Ændr: `src/app.h:129-130`

**Trin 1: Skift factory-typen til `ID2D1Factory1`**

I `src/app.h`:

```cpp
ID2D1Factory1*         d2d_factory_ = nullptr;
ID2D1HwndRenderTarget* rt_ = nullptr;
ID2D1DeviceContext5*   d2d_ctx5_ = nullptr;   // valgfri, kan være null
```

**Trin 2: Opret factory som `ID2D1Factory1`**

I `src/app.cpp` omkring linje 1451:

```cpp
D2D1_FACTORY_OPTIONS opts = {};
HRESULT hr = D2D1CreateFactory(
    D2D1_FACTORY_TYPE_SINGLE_THREADED,
    __uuidof(ID2D1Factory1),
    &opts,
    reinterpret_cast<void**>(&d2d_factory_));
```

**Trin 3: Hent `ID2D1DeviceContext5` efter render-målet er skabt**

Lige efter `CreateHwndRenderTarget` lykkes:

```cpp
// SVG kraever DeviceContext5. Findes fra Windows 10 Creators Update.
// Hvis QI fejler, koerer appen videre uden SVG.
if (rt_) {
    HRESULT qi = rt_->QueryInterface(
        __uuidof(ID2D1DeviceContext5),
        reinterpret_cast<void**>(&d2d_ctx5_));
    if (FAILED(qi)) {
        d2d_ctx5_ = nullptr;
    }
}
```

**Trin 4: Frigiv i oprydningen**

Find hvor `rt_` frigives og tilføj:

```cpp
if (d2d_ctx5_) { d2d_ctx5_->Release(); d2d_ctx5_ = nullptr; }
```

Samme sted hvor render-målet genskabes (`src/app.cpp:1843`), skal
`d2d_ctx5_` frigives og hentes igen.

**Trin 5: Byg i CI**

```bash
git add src/app.h src/app.cpp
git commit -m "feat: hent ID2D1DeviceContext5 til SVG-tegning"
git push origin feat/svg-engine
gh workflow run build.yml --ref feat/svg-engine
```

Forventet: CI grøn.

**Trin 6: Bekræft ved kørsel**

Deploy exe'en og åbn et dokument. Hvis appen tegner som før, er
opgraderingen harmløs. Læg en midlertidig `OutputDebugStringW` ind, der
skriver om `d2d_ctx5_` er `nullptr`, og fjern den igen bagefter.

**Risiko:** Hvis `QueryInterface` fejler, skal render-målet bygges om til
D3D11 plus DXGI swap chain. Det er en større opgave, og den beskrives i
Opgave 3.

---

### Opgave 3: Reserveplan hvis QueryInterface fejler

**Formål:** Have en vej videre hvis `ID2D1HwndRenderTarget` ikke kan give en
device context.

**Udfør kun denne opgave hvis Opgave 2 viste `d2d_ctx5_ == nullptr`.**

Render-målet skal så laves om til:

1. `D3D11CreateDevice` med `D3D11_CREATE_DEVICE_BGRA_SUPPORT`
2. `IDXGIDevice` fra D3D11-enheden
3. `ID2D1Factory1::CreateDevice`
4. `ID2D1Device::CreateDeviceContext` giver `ID2D1DeviceContext`, som kan
   QI'es til `ID2D1DeviceContext5`
5. `IDXGIFactory2::CreateSwapChainForHwnd`
6. Bitmap fra swap chain'ets bagbuffer sættes som mål

Det berører `Present`, `Resize` og DPI-håndtering. Sæt en dag af.

---

## Fase 2: SVG-motor

### Opgave 4: Opret svgdoc.h med grænsefladen

**Formål:** Definere en klasse, der kan holde et SVG-dokument og tegne det.

**Filer:**
- Opret: `src/svgdoc.h`

```cpp
#pragma once

// SvgDoc: holder et parset SVG-dokument og tegner det.
//
// Direct2D kan ikke tegne SVG-tekst. Se
// https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support
// Derfor tegnes former af Direct2D, mens tekst hentes ud af traeet og
// tegnes med DirectWrite.

#include <d2d1_3.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace svg {

// Et enkelt tekstelement hentet ud af SVG-traeet.
struct TextRun {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float fontSize = 16.0f;
    std::string fontFamily;
    std::string anchor;      // start | middle | end
    std::string fill;        // farve som "#rrggbb" eller navn
    bool bold = false;
};

class SvgDoc {
public:
    ~SvgDoc();

    // Parser en SVG-streng. Returnerer false hvis den ikke kan laeses.
    bool Load(ID2D1DeviceContext5* ctx, const std::string& xml);

    // Naturlig stoerrelse fra width/height eller viewBox.
    float Width() const { return width_; }
    float Height() const { return height_; }

    // Tegner dokumentet skaleret ind i (x, y, w, h).
    void Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
              float x, float y, float w, float h);

    void Release();

private:
    ID2D1SvgDocument* doc_ = nullptr;
    std::vector<TextRun> texts_;
    float width_ = 0.0f;
    float height_ = 0.0f;
};

}  // namespace svg
```

**Verifikation:** Filen kompilerer alene.

```bash
g++ -std=c++17 -fsyntax-only -Isrc -x c++ src/svgdoc.h
```

Forventet: fejl om manglende Windows-headere er i orden. Ingen syntaksfejl.

---

### Opgave 5: Skriv en lille XML-parser til tekstudtræk

**Formål:** Finde alle `<text>`-elementer med position, størrelse og indhold.
Det skal være portabelt, så det kan testes i WSL.

**Filer:**
- Opret: `src/svgtext.h`, `src/svgtext.cpp`
- Test: `tests/svgtext_test.cpp`

**Trin 1: Skriv den fejlende test**

I `tests/svgtext_test.cpp`:

```cpp
#include "gtest_lite.h"
#include "svgtext.h"

TEST(SvgText, FindsSingleText) {
    std::string xml =
        "<svg><text x=\"10\" y=\"20\" font-size=\"14\">Hej</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "Hej");
    EXPECT_TRUE(runs[0].x > 9.9f && runs[0].x < 10.1f);
    EXPECT_TRUE(runs[0].y > 19.9f && runs[0].y < 20.1f);
}

TEST(SvgText, ReadsTextAnchor) {
    std::string xml =
        "<svg><text x=\"5\" y=\"5\" text-anchor=\"middle\">Midt</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].anchor, "middle");
}

TEST(SvgText, ReadsTspanChildren) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\"><tspan>A</tspan><tspan>B</tspan></text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].text, "A");
    EXPECT_EQ(runs[1].text, "B");
}

TEST(SvgText, DecodesEntities) {
    std::string xml =
        "<svg><text x=\"0\" y=\"0\">A &amp; B &lt;C&gt;</text></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, "A & B <C>");
}

TEST(SvgText, IgnoresTextInsideDefs) {
    std::string xml =
        "<svg><defs><text x=\"0\" y=\"0\">skjult</text></defs></svg>";
    auto runs = svg::ExtractTextRuns(xml);
    EXPECT_EQ(runs.size(), 0u);
}

TEST(SvgText, GarbageDoesNotThrow) {
    auto runs = svg::ExtractTextRuns("<svg><text x=");
    EXPECT_TRUE(runs.size() < 2u);
}
```

**Trin 2: Kør testen og se den fejle**

```bash
cd /home/au19277/projekter/MarkDownIt
g++ -std=c++17 -Isrc -Itests tests/svgtext_test.cpp tests/test_main.cpp \
    src/svgtext.cpp -o /tmp/svgtext_test
```

Forventet: oversætterfejl, `svgtext.h` findes ikke.

**Trin 3: Skriv `src/svgtext.h`**

```cpp
#pragma once

#include <string>
#include <vector>

namespace svg {

struct TextRun;  // defineret i svgdoc.h

// Henter alle synlige tekstelementer ud af en SVG-streng.
// Portabel C++17, ingen Windows-afhaengigheder, saa den kan testes i WSL.
std::vector<TextRun> ExtractTextRuns(const std::string& xml);

}  // namespace svg
```

Bemærk: `TextRun` flyttes til en fælles header, så både `svgdoc.h` og
`svgtext.h` kan bruge den uden at trække Windows med ind. Læg den i
`src/svgtypes.h`.

**Trin 4: Skriv `src/svgtext.cpp`**

Krav til implementeringen:

- Scan efter `<text`. Hop over alt inde i `<defs>`.
- Læs attributterne `x`, `y`, `font-size`, `font-family`, `text-anchor`,
  `fill`, `font-weight`, samt `style` med de samme navne indeni.
- Hvis der er `<tspan>`-børn, giv hver sin `TextRun`. Arv forældrens
  attributter, og lad barnets egne overskrive.
- Afkod `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&#39;` samt numeriske
  reference som `&#65;`.
- Kast aldrig. Ufuldstændigt input giver bare færre resultater.

**Trin 5: Kør testen igen**

```bash
g++ -std=c++17 -Isrc -Itests tests/svgtext_test.cpp tests/test_main.cpp \
    src/svgtext.cpp -o /tmp/svgtext_test && /tmp/svgtext_test
```

Forventet: `6 passed`, `0 failures`.

**Trin 6: Commit**

```bash
git add src/svgtypes.h src/svgtext.h src/svgtext.cpp tests/svgtext_test.cpp
git commit -m "feat: hent tekstelementer ud af SVG"
```

---

### Opgave 6: Fjern tekst fra SVG før Direct2D parser den

**Formål:** Undgå at Direct2D bruger tid på elementer, den alligevel ignorerer,
og undgå at `<foreignObject>` forvirrer parseren.

**Filer:**
- Ændr: `src/svgtext.h`, `src/svgtext.cpp`
- Test: `tests/svgtext_test.cpp`

**Trin 1: Skriv den fejlende test**

```cpp
TEST(SvgText, StripsTextElements) {
    std::string xml =
        "<svg><rect x=\"0\"/><text x=\"1\" y=\"2\">Hej</text></svg>";
    std::string out = svg::StripTextElements(xml);
    EXPECT_TRUE(out.find("rect") != std::string::npos);
    EXPECT_TRUE(out.find("Hej") == std::string::npos);
}

TEST(SvgText, StripsForeignObject) {
    std::string xml =
        "<svg><foreignObject><div>Hej</div></foreignObject><rect/></svg>";
    std::string out = svg::StripTextElements(xml);
    EXPECT_TRUE(out.find("foreignObject") == std::string::npos);
    EXPECT_TRUE(out.find("rect") != std::string::npos);
}
```

**Trin 2: Kør og se den fejle.** Forventet: `StripTextElements` findes ikke.

**Trin 3: Tilføj funktionen**

```cpp
// Fjerner text-, tspan- og foreignObject-elementer, saa Direct2D kun
// faar de former, den faktisk kan tegne.
std::string StripTextElements(const std::string& xml);
```

**Trin 4: Kør testen igen.** Forventet: alle består.

**Trin 5: Commit**

```bash
git add src/svgtext.h src/svgtext.cpp tests/svgtext_test.cpp
git commit -m "feat: fjern tekst og foreignObject foer Direct2D-parsing"
```

---

### Opgave 7: Implementer SvgDoc::Load

**Formål:** Bygge et `ID2D1SvgDocument` af den rensede SVG og gemme
tekstelementerne ved siden af.

**Filer:**
- Opret: `src/svgdoc.cpp`

**Trin 1: Skriv `Load`**

```cpp
bool SvgDoc::Load(ID2D1DeviceContext5* ctx, const std::string& xml) {
    Release();
    if (!ctx || xml.empty()) return false;

    texts_ = ExtractTextRuns(xml);
    std::string shapes = StripTextElements(xml);

    // Direct2D vil have SVG som en stream.
    IStream* stream = SHCreateMemStream(
        reinterpret_cast<const BYTE*>(shapes.data()),
        static_cast<UINT>(shapes.size()));
    if (!stream) return false;

    ReadViewBox(xml, width_, height_);
    if (width_ <= 0.0f) width_ = 300.0f;
    if (height_ <= 0.0f) height_ = 150.0f;

    HRESULT hr = ctx->CreateSvgDocument(
        stream, D2D1::SizeF(width_, height_), &doc_);
    stream->Release();

    return SUCCEEDED(hr) && doc_ != nullptr;
}
```

`ReadViewBox` læser `width`/`height` fra rod-elementet, og falder tilbage til
`viewBox`, hvis de mangler. mermaid sætter ofte kun `viewBox` plus en
`style="max-width:..."`.

**Trin 2: Skriv `Release` og destruktoren.** Frigiv `doc_`, ryd `texts_`.

**Trin 3: CI-byg.** Forventet: grøn.

---

### Opgave 8: Implementer SvgDoc::Draw med hybrid tekst

**Formål:** Tegne former og tekst i det rigtige koordinatsystem.

**Trin 1: Skriv `Draw`**

```cpp
void SvgDoc::Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
                  float x, float y, float w, float h) {
    if (!ctx || !doc_) return;

    float sx = (width_  > 0.0f) ? w / width_  : 1.0f;
    float sy = (height_ > 0.0f) ? h / height_ : 1.0f;
    float s = (sx < sy) ? sx : sy;   // bevar sideforhold

    D2D1_MATRIX_3X2_F prev;
    ctx->GetTransform(&prev);

    D2D1::Matrix3x2F local =
        D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y);
    ctx->SetTransform(local * prev);

    ctx->DrawSvgDocument(doc_);
    DrawTexts(ctx, dw);

    ctx->SetTransform(prev);
}
```

Rækkefølgen `local * prev` er vigtig. Render-løkken har allerede en
scroll-transform, og den må ikke overskrives. Det var netop fejlen i den
tidligere native renderer.

**Trin 2: Skriv `DrawTexts`**

For hver `TextRun`:

1. Lav et `IDWriteTextFormat` med `fontFamily` og `fontSize`. Fald tilbage
   til `Segoe UI`, hvis familien er ukendt.
2. Lav et `IDWriteTextLayout` med rigelig bredde og `NO_WRAP`.
3. Aflæs `DWRITE_TEXT_METRICS`.
4. Regn x ud efter `anchor`:
   - `start` giver `tx = run.x`
   - `middle` giver `tx = run.x - tm.width / 2`
   - `end` giver `tx = run.x - tm.width`
5. SVG's `y` er skriftlinjen, ikke toppen. Træk `baseline` fra:
   `ty = run.y - tm.height * 0.8f`. Brug helst
   `DWRITE_LINE_METRICS::baseline` for at ramme præcist.
6. Lav en pensel af `fill`. Fald tilbage til temaets tekstfarve.
7. `DrawTextLayout`.

Alle COM-objekter frigives på hver udgang. Brug en RAII-hjælper.

**Trin 3: CI-byg.** Forventet: grøn.

**Trin 4: Commit**

```bash
git add src/svgdoc.h src/svgdoc.cpp src/svgtypes.h CMakeLists.txt
git commit -m "feat: SVG-renderer med Direct2D-former og DirectWrite-tekst"
```

---

### Opgave 9: Tilføj filerne til CMakeLists

**Filer:**
- Ændr: `CMakeLists.txt`

Læg `src/svgdoc.cpp`, `src/svgtext.cpp` i både `MarkDownIt` og
`MarkDownIt.tests`. Læg `tests/svgtext_test.cpp` i testmålet.

**Verifikation:** CI kører testene og melder det nye antal.

---

## Fase 3: SVG i markdown

### Opgave 10: Vis .svg-billeder

**Formål:** `![tekst](diagram.svg)` skal virke.

**Filer:**
- Ændr: `src/imagehelper.h`, `src/imagehelper.cpp`
- Ændr: `src/renderer.cpp` (billedgrenen)

**Trin 1:** Lad `ImageHelper` genkende endelsen `.svg` og `data:image/svg+xml`.

**Trin 2:** Læs filen som tekst og send den til `SvgDoc::Load` i stedet for WIC.

**Trin 3:** Renderer'en får et `SvgDoc` i stedet for et `ID2D1Bitmap`, når
kilden er SVG.

**Trin 4:** Test med en håndskrevet SVG i `tests/fixtures/`.

---

### Opgave 11: Vis ```svg-kodeblokke

**Formål:** En kodeblok med sproget `svg` skal tegnes.

**Filer:**
- Ændr: `src/renderer.cpp`

Grenen ligner den, mermaid får senere. Betingelsen er `n.lang == "svg"`.
Indholdet af `n.raw` sendes direkte til `SvgDoc::Load`.

**Vigtigt:** Måling og tegning skal bruge nøjagtig samme højdeudregning.
Ellers driver rullepanelet, hvilket revisionen tidligere fandt.

---

### Opgave 12: Vis rå kode når markøren står i blokken

**Formål:** Samme adfærd som Obsidian. Man skal kunne rette sin SVG.

Genbrug mønsteret fra den gamle mermaid-kode: hvis `sel->active.offset` ligger
mellem `n.srcOffset` og `n.srcOffset + n.srcLength`, tegnes kodeblokken i
stedet.

---

## Fase 4: WebView2 og mermaid

### Opgave 13: Læg WebView2 SDK ind

**Formål:** Kunne oversætte mod WebView2.

**Filer:**
- Ændr: `CMakeLists.txt`
- Ændr: `.github/workflows/build.yml`

**Trin 1:** Hent NuGet-pakken `Microsoft.Web.WebView2` i CI-trinnet.

**Trin 2:** Peg include-stien på `build/native/include`.

**Trin 3:** `WebView2Loader.dll` indlæses dynamisk med `LoadLibraryW`, så
exe'en stadig starter på en maskine uden WebView2.

**Verifikation:** CI grøn, og exe'en starter stadig.

---

### Opgave 14: Indlejr mermaid.min.js som ressource

**Formål:** Ingen netværkskald, og stadig én fil.

**Filer:**
- Opret: `third_party/mermaid/mermaid.min.js`
- Ændr: `src/resource.rc`

Læg filen ind som `RCDATA`. Den fylder omkring 1 MB.

**Verifikation:** `FindResourceW` finder den ved kørsel.

---

### Opgave 15: Skriv MermaidRenderer med skjult WebView2

**Formål:** Omsætte mermaid-kode til en SVG-streng.

**Filer:**
- Opret: `src/mermaidsvg.h`, `src/mermaidsvg.cpp`

Grænsefladen skal være asynkron, fordi WebView2 er det:

```cpp
class MermaidRenderer {
public:
    // Kaldes naar en SVG er klar. Tom streng betyder fejl.
    using Callback = void(*)(uint32_t srcOffset,
                             const std::string& svg,
                             void* ctx);

    bool Init(HWND parent);       // false hvis WebView2 mangler
    bool Available() const;

    // Beder om en SVG. Svaret kommer senere via callback.
    void Request(uint32_t srcOffset, const std::string& code,
                 Callback cb, void* ctx);

    void Shutdown();
};
```

Vigtigt ved opsætningen:

- Vinduet er nul pixels stort og skjult.
- mermaid startes med `htmlLabels: false`, ellers kommer etiketterne som
  `<foreignObject>` og kan ikke tegnes.
- `securityLevel: 'strict'` slår scripts i diagrammer fra.
- `startOnLoad: false`, og der kaldes `mermaid.render` direkte.
- Resultatet sendes tilbage med `postMessage`.

Opsætningen i JavaScript:

```javascript
mermaid.initialize({
  startOnLoad: false,
  htmlLabels: false,
  flowchart: { htmlLabels: false },
  securityLevel: 'strict',
  theme: 'default'
});
```

---

### Opgave 16: Kobl mermaid-blokke til renderer'en

**Formål:** `n.lang == "mermaid"` skal give et diagram.

Flowet er:

1. Renderer'en møder en mermaid-blok og spørger cachen.
2. Cachen har den, og diagrammet tegnes.
3. Cachen har den ikke. Blokken tegnes som kodeblok, og der bestilles en
   SVG.
4. Når svaret kommer, lægges det i cachen, og der kaldes `InvalidateRect`.
5. Næste tegning viser diagrammet.

Den forsinkelse er acceptabel. Første visning tager et øjeblik, derefter
kommer det fra cachen.

---

### Opgave 17: Højden skal kendes før SVG'en er klar

**Formål:** Undgå at teksten hopper, når diagrammet dukker op.

Så længe SVG'en mangler, måles blokken som en kodeblok. Når den kommer, ændrer
højden sig, og dokumentet skal måles igen. Sørg for at `totalH_` og
rullepanelet opdateres i samme ombæring.

---

### Opgave 18: Håndter at WebView2 mangler

**Formål:** Appen må ikke gå ned på en maskine uden WebView2.

Hvis `Init` fejler, sættes et flag. Alle mermaid-blokke tegnes så som
kodeblokke, præcis som i dag. Ingen fejlbesked ved opstart.

---

## Fase 5: Cache, oprydning, dokumentation

### Opgave 19: SVG-cache

**Filer:**
- Opret: `src/svgcache.h`, `src/svgcache.cpp`
- Test: `tests/svgcache_test.cpp`

Nøglen er en FNV-1a-hash af kildeteksten. Værdien er den færdige SVG-streng,
ikke det parsede dokument, for `ID2D1SvgDocument` hører til en device context
og skal laves om, når den forsvinder.

Cachen ryddes ved genindlæsning af filen, og ved skift af tema. Den må ikke
ryddes i `WM_PAINT`, hvilket var en fejl sidst.

---

### Opgave 20: Testfil med diagrammer

**Filer:**
- Opret: `tests/fixtures/svg.md`

Indhold: en flowchart, et sekvensdiagram, et klassediagram, en gantt, en rå
` ```svg `-blok, et `.svg`-billede og et ugyldigt diagram.

Gantt og klassediagram er værd at have med, fordi mermaid kan tegne dem, mens
den gamle native renderer ikke kunne.

---

### Opgave 21: Opdater README

Beskriv at diagrammer nu tegnes af mermaid selv, at alle mermaid-typer virker,
og at WebView2 skal være til stede. Nævn at SVG-billeder og ` ```svg `-blokke
også virker.

Kør humanizer-færdigheden på teksten. Ingen tankestreger.

---

### Opgave 22: Flet til main

```bash
gh pr create --base main --head feat/svg-engine \
    --title "feat: SVG-motor og mermaid via SVG" \
    --body "..."
```

Flet først når CI er grøn, og du selv har set diagrammerne tegnet rigtigt.

---

## Filer der ændres

| Fil | Handling |
|-----|----------|
| `src/svgtypes.h` | Opret. Fælles `TextRun`. |
| `src/svgtext.h` / `.cpp` | Opret. Tekstudtræk, portabel. |
| `src/svgdoc.h` / `.cpp` | Opret. Hybrid renderer. |
| `src/svgcache.h` / `.cpp` | Opret. Cache af SVG-strenge. |
| `src/mermaidsvg.h` / `.cpp` | Opret. WebView2-bro. |
| `src/app.h` / `.cpp` | `ID2D1Factory1`, `ID2D1DeviceContext5`, cache, WebView2. |
| `src/renderer.h` / `.cpp` | Grene for `svg` og `mermaid`, plus SVG-billeder. |
| `src/imagehelper.h` / `.cpp` | Genkend `.svg`. |
| `src/resource.rc` | mermaid.min.js som ressource. |
| `CMakeLists.txt` | Nye filer og WebView2. |
| `.github/workflows/build.yml` | Hent WebView2 SDK. |
| `README.md` | Beskriv SVG og mermaid. |
| `tests/svgtext_test.cpp` | Opret. |
| `tests/svgcache_test.cpp` | Opret. |
| `tests/fixtures/svg.md` | Opret. |

Slettes ved tilbagerulningen: `src/mermaid.h`, `src/mermaid.cpp`,
`src/mermaidlayout.h`, `src/mermaidlayout.cpp`, `src/diagramcache.h`,
`src/diagramcache.cpp` og deres tests.

---

## Validering

**Enhedstest i WSL:**

```bash
g++ -std=c++17 -Isrc -Itests tests/svgtext_test.cpp tests/svgcache_test.cpp \
    tests/test_main.cpp src/svgtext.cpp src/svgcache.cpp -o /tmp/svg_test \
    && /tmp/svg_test
```

**Oversættelse:** CI på `windows-2022`.

**Manuel afprøvning med `tests/fixtures/svg.md`:**

1. Alle etiketter står inde i deres figur.
2. Sekvensdiagrammet har livslinjer, aktiveringsfelter og pile.
3. Diagrammer følger med, når man ruller.
4. Zoom giver skarpe kanter, ikke slørede.
5. Markøren i en blok viser den rå kode.
6. En ugyldig blok vises som kode.
7. Rullepanelet passer med indholdet.

---

## Risici

| Risiko | Sandsynlighed | Følge | Modtræk |
|--------|---------------|-------|---------|
| `QueryInterface` til `ID2D1DeviceContext5` fejler | Middel | Stor | Opgave 3, byg render-målet om over D3D11 |
| Skriftlinjen rammer skævt | Høj | Middel | Brug `DWRITE_LINE_METRICS::baseline`, ikke et gæt |
| mermaid sender stadig `foreignObject` | Middel | Stor | `htmlLabels: false` begge steder, og fjern dem i Opgave 6 |
| Direct2D ignorerer flere elementer end ventet | Middel | Middel | Sammenlign med et browserbillede, tilføj efter behov |
| WebView2 mangler på maskinen | Lav | Middel | Opgave 18, fald tilbage til kodeblok |
| Exe'en vokser med 1 MB | Sikker | Lille | Acceptabelt |
| Første visning er forsinket | Sikker | Lille | Cache, og kun første gang |

---

## Estimat

| Fase | Realistisk | Pessimistisk |
|------|-----------|--------------|
| 0. Tilbagerulning | 0,5 time | 1 time |
| 1. Kontekst | 0,5 dag | 2 dage |
| 2. SVG-motor | 2 dage | 4 dage |
| 3. SVG i markdown | 1 dag | 2 dage |
| 4. WebView2 og mermaid | 2 dage | 4 dage |
| 5. Cache og dokumentation | 1 dag | 2 dage |
| **I alt** | **6,5 dage** | **14 dage** |

---

## Åbne spørgsmål

1. **Tema.** Skal mermaid følge appens tema, når der senere kommer en mørk
   udgave? Forslag: send `theme: 'dark'` til mermaid ud fra paletten.
2. **Eksport.** Skal man kunne gemme et diagram som PNG med højreklik? Det er
   let, når SVG'en først findes, men det er ikke nødvendigt nu.
3. **Fast WebView2-udgave.** Skal en bestemt udgave lægges ved, så
   diagrammerne ser ens ud alle steder? Det koster omkring 120 MB, så forslaget
   er nej.
4. **Egen SVG-parser.** Hvis Direct2D viser sig at ignorere for meget, kan
   figurerne tegnes direkte fra `path`-data. Det er en stor opgave, og den bør
   kun overvejes, hvis Fase 2 skuffer.
