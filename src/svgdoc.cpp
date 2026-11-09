#include "svgdoc.h"
#include "svgtext.h"
#include "crash_trace.h"

#include <cmath>

// SHCreateMemStream from shlwapi
#pragma comment(lib, "shlwapi.lib")
#include <shlwapi.h>

#include <cctype>

namespace svg {



SvgDoc::~SvgDoc() {
    Release();
}

void SvgDoc::Release() {
    if (doc_) { doc_->Release(); doc_ = nullptr; }
    texts_.clear();
    width_ = 0.0f;
    height_ = 0.0f;
}


bool SvgDoc::Load(ID2D1DeviceContext5* ctx, const std::string& xml) {
    Release();
    if (!ctx || xml.empty()) return false;

    texts_ = ExtractTextRuns(xml);
    // Direct2D draws neither <text> nor <marker>, so the text is lifted out
    // and drawn with DirectWrite, and every marker reference is rewritten
    // into an explicit group it can draw.
    std::string shapes = ExpandMarkers(StripTextElements(xml));
    diag::TraceFmt("SVGDOC mode=strip runs=%zu", texts_.size());

    if (!ReadSize(xml, &width_, &height_)) {
        width_ = 0.0f;
        height_ = 0.0f;
    }
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
            for (const wchar_t* vbName : {L"viewBox", L"viewbox"}) {
                D2D1_SVG_VIEWBOX vb = {};
                HRESULT vhr = root->GetAttributeValue(
                    vbName, D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                    &vb, sizeof(vb));
                diag::TraceFmt(
                    "SVGDOC root viewBox name=%ls hr=0x%08lx x=%g y=%g w=%g h=%g",
                    vbName, (unsigned long)vhr,
                    vb.x, vb.y, vb.width, vb.height);
            }
            for (const wchar_t* szName : {L"width", L"height"}) {
                float sz = 0.0f;
                HRESULT shr = root->GetAttributeValue(
                    szName, D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT,
                    &sz, sizeof(sz));
                diag::TraceFmt(
                    "SVGDOC root %ls float hr=0x%08lx v=%g",
                    szName, (unsigned long)shr, sz);
            }
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

    // Direct2D uses row vectors: apply the local scale and placement,
    // then the caller's page transform (including scrolling).
    D2D1::Matrix3x2F docT =
        D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y);
    ctx->SetTransform(docT * prev);

    if (doc_) {
        ctx->DrawSvgDocument(doc_);
    }

    DrawTexts(ctx, dw);

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

void SvgDoc::DrawTexts(ID2D1DeviceContext5* ctx, IDWriteFactory* dw) {
    if (texts_.empty() || !dw) return;

    // SVG's initial fill is black, and a diagram's own background rect is
    // white, so text without a fill attribute is black in a browser too.
    D2D1_COLOR_F defaultColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);

    // Create a reusable brush
    RelGuard<ID2D1SolidColorBrush> br;
    ctx->CreateSolidColorBrush(defaultColor, &br.p);
    if (!br.p) return;

    // Draw() has already installed the complete SVG-to-page transform.
    // Reuse it for text: composing the document transform here again
    // shrinks and moves labels a second time, away from their shapes.
    D2D1_MATRIX_3X2_F prev;
    ctx->GetTransform(&prev);
    const D2D1_MATRIX_3X2_F textT = prev;

    // Cache text formats by (fontFamily, fontSize, bold)
    struct FmtKey {
        std::string family;
        float size;
        bool bold;
        bool italic;
        bool operator==(const FmtKey& o) const {
            return size == o.size && bold == o.bold && italic == o.italic &&
                   family == o.family;
        }
    };
    struct FmtEntry {
        FmtKey key;
        IDWriteTextFormat* fmt;
    };
    std::vector<FmtEntry> fmtCache;

    // Pen position across runs, for text continuing after a <tspan x="...">.
    float chainAdvance = 0.0f;
    bool  chainValid = false;

    for (const auto& run : texts_) {
        if (run.text.empty()) continue;

        // Find or create text format
        IDWriteTextFormat* fmt = nullptr;
        FmtKey key{ run.fontFamily, run.fontSize, run.bold, run.italic };
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
            DWRITE_FONT_STYLE style = run.italic
                ? DWRITE_FONT_STYLE_ITALIC
                : DWRITE_FONT_STYLE_NORMAL;
            HRESULT hr = dw->CreateTextFormat(
                fam.c_str(), nullptr, weight, style,
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

        // Compute position based on text-anchor. When the exporter
        // gave a data-group box (no explicit anchor), center the
        // glyph box on the box so font-width differences cannot
        // shift the label sideways (Batik pre-computed start-edges
        // with its own font metrics).
        float tx = run.x;
        float centerY = -1.0f;
        if (!run.anchor.empty() && run.anchor != "inherit") {
            if (run.anchor == "middle") tx -= tm.width / 2.0f;
            else if (run.anchor == "end") tx -= tm.width;
        } else if (run.boxValid && run.bw > 0.0f && run.bh > 0.0f) {
            tx = run.bx + run.bw / 2.0f - tm.width / 2.0f;
            centerY = run.by + run.bh / 2.0f;
        }

        // SVG y is the baseline, not the top.
        // Approximate: move up by ~80% of the font height.
        DWRITE_LINE_METRICS lm = {};
        UINT32 lmCount = 0;
        tl->GetLineMetrics(&lm, 1, &lmCount);
        // Real value when DirectWrite reports it; the 80% guess is only a
        // fallback for a layout that reports no line metrics.
        float baseline = (lmCount > 0) ? lm.baseline : tm.height * 0.8f;
        // dominant-baseline="central": x/y is the glyph center, not
        // the baseline, so center the layout height on run.y.
        float ty = (centerY >= 0.0f)
            ? (centerY - tm.height / 2.0f)
            : (run.central ? (run.y - tm.height / 2.0f)
                           : (run.y - baseline));

        // Set color
        D2D1_COLOR_F color = ParseColor(run.fill, defaultColor);
        br.p->SetColor(color);

        // Text that follows a <tspan x="..."> continues from where the
        // previous run ended. The extractor has no font metrics, so it marks
        // the run instead of computing an x, and the advance happens here
        // where tm.width is known.
        if (run.chained && chainValid) {
            tx += chainAdvance;
        }

        // A rotate or skew on an ancestor must turn the glyphs too. D2D draws
        // the shapes rotated, so unrotated text sits visibly off its own shape.
        // Compose translate(anchor) * linear * translate(-anchor) around the
        // run's position so it spins in place.
        if (run.transformed) {
            const float ax = tx, ay = ty;
            // translate(anchor) * linear * translate(-anchor): the linear part
            // is (ma, mb, mc, md) and the translation part re-anchors it.
            D2D1_MATRIX_3X2_F rot;
            rot._11 = run.ma; rot._12 = run.mb;
            rot._21 = run.mc; rot._22 = run.md;
            rot._31 = ax - (run.ma * ax + run.mc * ay);
            rot._32 = ay - (run.mb * ax + run.md * ay);
            ctx->SetTransform(rot * textT);
            ctx->DrawTextLayout(
                D2D1::Point2F(tx, ty), tl, br.p,
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            ctx->SetTransform(textT);
        } else {
            ctx->DrawTextLayout(
                D2D1::Point2F(tx, ty), tl, br.p,
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }

        // Remember where this run ended for the next chained run.
        chainAdvance = tm.width;
        chainValid = true;

        tl->Release();
    }

    // Release cached formats and restore the caller's transform.
    for (auto& e : fmtCache) {
        if (e.fmt) e.fmt->Release();
    }
    ctx->SetTransform(prev);
}

}  // namespace svg
