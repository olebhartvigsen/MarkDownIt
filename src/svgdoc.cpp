#include "svgdoc.h"
#include "svgtext.h"
#include "crash_trace.h"

#include <algorithm>
#include <cmath>

// SHCreateMemStream from shlwapi
#pragma comment(lib, "shlwapi.lib")
#include <shlwapi.h>

#include <cctype>

namespace svg {



// --- Helper: parse a float from a string, stripping common SVG units ---
static float ParseDim(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    // Strip unit suffixes: px, pt, pc, cm, mm, in, em, ex, %
    size_t end = s.size();
    while (end > 0 && (s[end-1] < '0' || s[end-1] > '9') && s[end-1] != '.' && s[end-1] != '-')
        --end;
    try {
        return std::stof(s.substr(0, end));
    } catch (...) {
        return def;
    }
}

// Read width/height from the root <svg> tag, fall back to viewBox.
static void ReadViewBox(const std::string& xml, float& w, float& h) {
    w = 0.0f; h = 0.0f;
    // Find <svg ...> opening tag
    size_t p = xml.find("<svg");
    if (p == std::string::npos) return;
    size_t gt = xml.find('>', p);
    if (gt == std::string::npos) return;
    std::string tag = xml.substr(p, gt - p + 1);

    // Extract attributes
    auto getAttr = [](const std::string& t, const std::string& name) -> std::string {
        // Attribute lookup with a word-boundary guard: a plain find
        // matches suffixes too, e.g. width="1" inside
        // stroke-width="1" (Batik SVGs), which parsed the diagram
        // width as 1 px. Require a non-name char (the whitespace
        // separator, or the tag start) before the attribute name.
        std::string pat;
        size_t a = std::string::npos;
        const char qs[2] = {34, 39};
        for (int qi = 0; qi < 2; ++qi) {
            char q = qs[qi];
            pat = name + "=" + std::string(1, q);
            size_t p2 = 0;
            while ((p2 = t.find(pat, p2)) != std::string::npos) {
                if (p2 == 0 ||
                    !(std::isalnum(static_cast<unsigned char>(t[p2 - 1])) ||
                      t[p2 - 1] == '-' || t[p2 - 1] == '_' ||
                      t[p2 - 1] == ':')) {
                    a = p2;
                    break;
                }
                p2 += pat.size();
            }
            if (a != std::string::npos) break;
        }
        if (a == std::string::npos) return {};
        a += pat.size();
        char q = t[a - 1];
        size_t b = t.find(q, a);
        if (b == std::string::npos) return {};
        return t.substr(a, b - a);
    };

    w = ParseDim(getAttr(tag, "width"));
    h = ParseDim(getAttr(tag, "height"));

    if (w <= 0 || h <= 0) {
        // viewBox is case-sensitive per spec, but real files vary
        // (Batik writes lowercase "viewbox"); try both spellings.
        std::string vb = getAttr(tag, "viewBox");
        if (vb.empty()) vb = getAttr(tag, "viewbox");
        if (!vb.empty()) {
            // viewBox = "minX minY width height"
            // Extract width (3rd) and height (4th) tokens
            size_t sp1 = vb.find(' ');
            if (sp1 != std::string::npos) {
                size_t sp2 = vb.find(' ', sp1 + 1);
                if (sp2 != std::string::npos) {
                    size_t sp3 = vb.find(' ', sp2 + 1);
                    std::string wstr = (sp3 != std::string::npos)
                        ? vb.substr(sp2 + 1, sp3 - sp2 - 1)
                        : vb.substr(sp2 + 1);
                    std::string hstr = (sp3 != std::string::npos)
                        ? vb.substr(sp3 + 1)
                        : std::string();
                    w = ParseDim(wstr);
                    h = ParseDim(hstr);
                }
            }
        }
    }
}

SvgDoc::~SvgDoc() {
    Release();
}

void SvgDoc::Release() {
    if (doc_) { doc_->Release(); doc_ = nullptr; }
    texts_.clear();
    texts_original_.clear();
    width_ = 0.0f;
    height_ = 0.0f;
}


// --- Text pipeline mode (diagnostic; remove after alignment fix) ---
// Marker file %LOCALAPPDATA%\MarkDownIt\svg_text_mode.on:
//   content "native" -> D2D renders <text> itself (no strip, no
//                        custom DrawTexts).
//   anything else     -> strip text, draw runs via DrawTexts.
namespace textmode {
enum Mode { kUnknown, kStrip, kNative };
static Mode g_mode = kUnknown;
static Mode Current() {
    if (g_mode != kUnknown) return g_mode;
    char path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                0, path))) {
        g_mode = kStrip;
        return g_mode;
    }
    std::string f = path;
    f += "\x5cMarkDownIt\x5csvg_text_mode.on";
    HANDLE h = CreateFileA(f.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { g_mode = kStrip; return g_mode; }
    char buf[16] = {};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    std::string v(buf, got);
    // trim
    while (!v.empty() && (v.back() == 13 || v.back() == 10 || v.back() == 32)) v.pop_back();
    g_mode = (v == "native") ? kNative : kStrip;
    return g_mode;
}
}  // namespace textmode

// Diagnostic offset experiments, marker-gated:
//   svg_baseline_off.on  -> skip baseline correction in DrawTexts
//   svg_prev_off.on      -> SetTransform(docT) ignoring page prev
static bool MarkerExists(const char* tail) {
    char path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                0, path))) return false;
    std::string f = path;
    f += std::string(1, 92) + "MarkDownIt" + std::string(1, 92) + tail;
    DWORD at = GetFileAttributesA(f.c_str());
    return at != INVALID_FILE_ATTRIBUTES;
}
static bool g_baseOffChecked = false;
static bool g_baseOff = false;
static bool BaselineOff() {
    if (!g_baseOffChecked) {
        g_baseOff = MarkerExists("svg_baseline_off.on");
        g_baseOffChecked = true;
        diag::TraceFmt("SVGDOC baseline_off=%d", g_baseOff ? 1 : 0);
    }
    return g_baseOff;
}
static bool g_prevOffChecked = false;
static bool g_prevOff = false;
static bool PrevOff() {
    if (!g_prevOffChecked) {
        g_prevOff = MarkerExists("svg_prev_off.on");
        g_prevOffChecked = true;
        diag::TraceFmt("SVGDOC prev_off=%d", g_prevOff ? 1 : 0);
    }
    return g_prevOff;
}
bool SvgDoc::Load(ID2D1DeviceContext5* ctx, const std::string& xml) {
    Release();
    if (!ctx || xml.empty()) return false;

    texts_ = ExtractTextRuns(xml);
    texts_original_ = texts_;
    std::string shapes = StripTextElements(xml);
    if (textmode::Current() == textmode::kNative) {
        // Diagnostic: let D2D render <text> itself and skip our runs.
        texts_.clear();
        shapes = xml;
        diag::TraceFmt("SVGDOC mode=native");
    } else {
        diag::TraceFmt("SVGDOC mode=strip runs=%zu", texts_.size());
    }

    ReadViewBox(xml, width_, height_);
    if (width_ <= 0.0f) width_ = 300.0f;
    if (height_ <= 0.0f) height_ = 150.0f;

    if (shapes.empty()) return true; // text-only SVG, no shapes

    // CreateStream expects an IStream.
    IStream* stream = SHCreateMemStream(
        reinterpret_cast<const BYTE*>(shapes.data()),
        static_cast<UINT>(shapes.size()));
    if (!stream) return false;

    HRESULT hr = ctx->CreateSvgDocument(
        stream, D2D1::SizeF(width_, height_), &doc_);
    stream->Release();

    if (SUCCEEDED(hr) && doc_) {
        // Diagnostic dump: what does D2D think the root looks like?
        // Mismatch between our parsed width_ and D2D's interpretation
        // of root width/height/viewBox shows up as text offset.
        ID2D1SvgElement* root = nullptr;
        doc_->GetRoot(&root);
        if (root) {
            wchar_t tag[64] = {};
            UINT32 len = 0;
            if (SUCCEEDED(root->GetTagName(tag, 64))) {
                tag[63] = 0;
                char tagA[66] = {};
                for (UINT32 ci = 0; ci < len && ci < 63; ++ci) {
                    tagA[ci] = static_cast<char>(tag[ci]);
                }
                diag::TraceFmt("SVGDOC root tag=[%s]", tagA);
            }
            D2D1_SVG_VIEWBOX vb = {};
            HRESULT vhr = root->GetAttributeValue(
                L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                &vb, sizeof(vb));
            diag::TraceFmt(
                "SVGDOC root viewBox hr=0x%08lx x=%g y=%g w=%g h=%g",
                (unsigned long)vhr,
                vb.x, vb.y, vb.width, vb.height);
            root->Release();
        }
    }

    return SUCCEEDED(hr);
}

void SvgDoc::Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
                  float x, float y, float w, float h) {
    if (!ctx) return;
    static float lastW = -1, lastH = -1;
    if (w != lastW || h != lastH) {
        lastW = w; lastH = h;
        diag::TraceFmt("SVGDOC draw x=%.1f y=%.1f w=%.1f h=%.1f docW=%.1f docH=%.1f runs=%zu",
                       x, y, w, h, width_, height_, texts_.size());
        for (size_t i = 0; i < texts_.size() && i < 3; ++i) {
            diag::TraceFmt("SVGDOC run%zu x=%.2f y=%.2f fs=%.2f text=[%.24s]",
                           i, texts_[i].x, texts_[i].y, texts_[i].fontSize,
                           texts_[i].text.c_str());
        }
    }

    // Compute uniform scale to fit, preserving aspect ratio.
    float sx = (width_ > 0.0f) ? w / width_ : 1.0f;
    float sy = (height_ > 0.0f) ? h / height_ : 1.0f;
    float s = (sx < sy) ? sx : sy;

    D2D1_MATRIX_3X2_F prev;
    ctx->GetTransform(&prev);

    // Compose so vector p maps to x + s*p: scale first (row-vector
    // order applies the RIGHT factor first), then translate. With
    // Translation(x,y) * Scale(s) the translation gets scaled too,
    // which draws the whole diagram offset up/left from its card.
    D2D1::Matrix3x2F docT =
        D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y);
    ctx->SetTransform(PrevOff() ? docT : (prev * docT));

    if (doc_) {
        ctx->DrawSvgDocument(doc_);
    }

    DrawTexts(ctx, dw, docT);

    ctx->SetTransform(prev);
}

// RAII helpers for COM objects
template <typename T>
struct RelGuard {
    T* p = nullptr;
    ~RelGuard() { if (p) p->Release(); }
};

static D2D1_COLOR_F ParseColor(const std::string& s, D2D1_COLOR_F fallback) {
    if (s.empty()) return fallback;
    if (s[0] == '#' && s.size() >= 7) {
        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        }
        if (s.size() >= 4 && sscanf(s.c_str() + 1, "%1x%1x%1x", &r, &g, &b) == 3) {
            return D2D1::ColorF(r / 15.0f, g / 15.0f, b / 15.0f, 1.0f);
        }
    }
    // Named colors
    if (s == "black") return D2D1::ColorF(0, 0, 0);
    if (s == "white") return D2D1::ColorF(1, 1, 1);
    if (s == "red") return D2D1::ColorF(1, 0, 0);
    if (s == "green") return D2D1::ColorF(0, 0.5f, 0);
    if (s == "blue") return D2D1::ColorF(0, 0, 1);
    if (s == "none") return D2D1::ColorF(0, 0, 0, 0);
    return fallback;
}

void SvgDoc::DrawTexts(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
                       const D2D1_MATRIX_3X2_F& docTransform) {
    if (texts_.empty() || !dw) return;

    // Use default text color
    D2D1_COLOR_F defaultColor = D2D1::ColorF(0.14f, 0.16f, 0.18f, 1.0f);

    // Create a reusable brush
    RelGuard<ID2D1SolidColorBrush> br;
    ctx->CreateSolidColorBrush(defaultColor, &br.p);
    if (!br.p) return;

    // Restore the outer (page) transform: shapes compose prev*docT in
    // Draw(), so text must do the same or it is drawn offset by the scroll
    // translation whenever the document is scrolled.
    D2D1_MATRIX_3X2_F prev;
    ctx->GetTransform(&prev);
    // Diagnostic 'layout' mode (marker svg_text_layout.on): convert
    // run doc coordinates to page coordinates (docT =
    // Scale(s) * Translation(x,y)) and draw with only the page
    // transform, so any D2D SVG internal document matrix cannot
    // make text behave differently from the shapes. Non-destructive:
    // starts from the pristine copy every paint, then normalizes
    // for the uniform-scale check below.
    static const bool kLayoutMode = MarkerExists("svg_text_layout.on");
    static const bool kDebugCross = MarkerExists("svg_text_debug.on");
    float sc = docTransform._11;
    float txl = docTransform._31;
    float tyl = docTransform._32;
    D2D1_MATRIX_3X2_F textT = PrevOff()
        ? docTransform
        : (kLayoutMode ? prev : (prev * docTransform));
    D2D1::Matrix3x2F pageM = D2D1::Matrix3x2F(prev._11, prev._12,
                                              prev._21, prev._22,
                                              prev._31, prev._32);
    (void)pageM;
    std::vector<TextRun> layoutRuns;
    if (kLayoutMode) {
        layoutRuns = texts_original_;
        for (auto& run : layoutRuns) {
            run.x = txl + sc * run.x;
            run.y = tyl + sc * run.y;
            // The page transform carries no zoom (zoom lives in the
            // layout metrics), so scale the font size explicitly to
            // match how the shapes scale under docT.
            run.fontSize *= sc;
        }
    }
    const std::vector<TextRun>& drawRuns =
        kLayoutMode ? layoutRuns : texts_;
    ctx->SetTransform(textT);

    // Cache text formats by (fontFamily, fontSize, bold)
    struct FmtKey {
        std::string family;
        float size;
        bool bold;
        bool operator==(const FmtKey& o) const {
            return size == o.size && bold == o.bold && family == o.family;
        }
    };
    struct FmtEntry {
        FmtKey key;
        IDWriteTextFormat* fmt;
    };
    std::vector<FmtEntry> fmtCache;

    for (const auto& run : drawRuns) {
        if (run.text.empty()) continue;

        // Find or create text format
        IDWriteTextFormat* fmt = nullptr;
        FmtKey key{ run.fontFamily, run.fontSize, run.bold };
        for (const auto& e : fmtCache) {
            if (e.key == key) { fmt = e.fmt; break; }
        }
        if (!fmt) {
            std::wstring fam = L"Segoe UI";
            if (!run.fontFamily.empty() && run.fontFamily != "inherit") {
                fam.clear();
                for (char c : run.fontFamily) {
                    if (c == ',') break; // first font only
                    if (c != '\'' && c != '"') fam.push_back(static_cast<wchar_t>(c));
                }
                // Trim spaces before matching generic names.
                while (!fam.empty() && fam.back() == 32) fam.pop_back();
                size_t b = 0;
                while (b < fam.size() && fam[b] == 32) ++b;
                if (b) fam = fam.substr(b);
                // Generic SVG families map to concrete Windows fonts;
                // DirectWrite does not resolve "sans-serif"/"Dialog".
                if (fam == L"sans-serif" || fam == L"Dialog" ||
                    fam == L"helvetica" || fam == L"ArialMT") {
                    fam = L"Arial";
                } else if (fam == L"serif" || fam == L"Times" ||
                           fam == L"Times-Roman") {
                    fam = L"Times New Roman";
                } else if (fam == L"monospace" || fam == L"Courier" ||
                           fam == L"Courier-New") {
                    fam = L"Consolas";
                }
            }
            DWRITE_FONT_WEIGHT weight = run.bold
                ? DWRITE_FONT_WEIGHT_BOLD
                : DWRITE_FONT_WEIGHT_REGULAR;
            HRESULT hr = dw->CreateTextFormat(
                fam.c_str(), nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                run.fontSize, L"", &fmt);
            if (FAILED(hr) || !fmt) continue;
            fmtCache.push_back({ key, fmt });
        }

        // Create text layout with generous width, no wrapping.
        // Decode UTF-8 to UTF-16 properly: byte-wise casting renders every
        // non-ASCII label as mojibake (e.g. C3 A9 becomes two C1 controls).
        std::u16string u16;
        {
            for (size_t i = 0; i < run.text.size(); ) {
                unsigned char c = static_cast<unsigned char>(run.text[i]);
                uint32_t cp = 0;
                int n_b = 1;
                if (c < 0x80) { cp = c; n_b = 1; }
                else if ((c & 0xE0) == 0xC0 && i + 1 < run.text.size()) {
                    cp = ((c & 0x1F) << 6) |
                         (static_cast<unsigned char>(run.text[i+1]) & 0x3F);
                    n_b = 2;
                } else if ((c & 0xF0) == 0xE0 && i + 2 < run.text.size()) {
                    cp = ((c & 0x0F) << 12) |
                         ((static_cast<unsigned char>(run.text[i+1]) & 0x3F) << 6) |
                         (static_cast<unsigned char>(run.text[i+2]) & 0x3F);
                    n_b = 3;
                } else if ((c & 0xF8) == 0xF0 && i + 3 < run.text.size()) {
                    cp = ((c & 0x07) << 18) |
                         ((static_cast<unsigned char>(run.text[i+1]) & 0x3F) << 12) |
                         ((static_cast<unsigned char>(run.text[i+2]) & 0x3F) << 6) |
                         (static_cast<unsigned char>(run.text[i+3]) & 0x3F);
                    n_b = 4;
                } else { cp = 0xFFFD; n_b = 1; }
                // Reject surrogates and out-of-range code points.
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                if (cp <= 0xFFFF) {
                    u16.push_back(static_cast<char16_t>(cp));
                } else {
                    cp -= 0x10000;
                    u16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                    u16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                }
                i += n_b;
            }
        }

        IDWriteTextLayout* tl = nullptr;
        HRESULT hr = dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(u16.data()),
            static_cast<UINT32>(u16.size()),
            fmt, 10000.0f, 100.0f, &tl);
        if (FAILED(hr) || !tl) continue;

        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TEXT_METRICS tm = {};
        tl->GetMetrics(&tm);

        // Compute position based on text-anchor
        float tx = run.x;
        if (run.anchor == "middle") tx -= tm.width / 2.0f;
        else if (run.anchor == "end") tx -= tm.width;

        // SVG y is the baseline, not the top.
        // Approximate: move up by ~80% of the font height.
        DWRITE_LINE_METRICS lm = {};
        UINT32 lmCount = 0;
        tl->GetLineMetrics(&lm, 1, &lmCount);
        float baseline = (lmCount > 0) ? lm.baseline : tm.height * 0.8f;
        if (BaselineOff()) baseline = 0.0f;
        // dominant-baseline="central": x/y is the glyph center, not
        // the baseline, so center the layout height on run.y.
        float ty = run.central ? (run.y - tm.height / 2.0f)
                               : (run.y - baseline);

        // Set color
        D2D1_COLOR_F color = ParseColor(run.fill, defaultColor);
        br.p->SetColor(color);

        ctx->DrawTextLayout(
            D2D1::Point2F(tx, ty), tl, br.p,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

        if (kDebugCross) {
            // Magenta cross at the run's anchored point (run.x,
            // run.y): baseline/center anchor per the run flags.
            ID2D1SolidColorBrush* cross = nullptr;
            ctx->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 0.0f, 1.0f, 1.0f), &cross);
            if (cross) {
                float cy = kLayoutMode
                    ? (run.central ? run.y : run.y)
                    : run.y;
                D2D1_POINT_2F c = D2D1::Point2F(run.x, cy);
                D2D1_POINT_2F a = D2D1::Point2F(run.x - 6, cy - 6);
                D2D1_POINT_2F b = D2D1::Point2F(run.x + 6, cy + 6);
                D2D1_POINT_2F d = D2D1::Point2F(run.x - 6, cy + 6);
                D2D1_POINT_2F e = D2D1::Point2F(run.x + 6, cy - 6);
                ctx->DrawLine(a, b, cross, 1.0f);
                ctx->DrawLine(d, e, cross, 1.0f);
                cross->Release();
            }
        }

        tl->Release();
    }

    // Release cached formats and restore the caller's transform.
    for (auto& e : fmtCache) {
        if (e.fmt) e.fmt->Release();
    }
    ctx->SetTransform(prev);
}

}  // namespace svg
