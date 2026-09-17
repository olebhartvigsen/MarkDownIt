#include "svgdoc.h"
#include "svgtext.h"

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
    width_ = 0.0f;
    height_ = 0.0f;
}

bool SvgDoc::Load(ID2D1DeviceContext5* ctx, const std::string& xml) {
    Release();
    if (!ctx || xml.empty()) return false;

    texts_ = ExtractTextRuns(xml);
    std::string shapes = StripTextElements(xml);

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

    return SUCCEEDED(hr);
}

void SvgDoc::Draw(ID2D1DeviceContext5* ctx, IDWriteFactory* dw,
                  float x, float y, float w, float h) {
    if (!ctx) return;

    // Compute uniform scale to fit, preserving aspect ratio.
    float sx = (width_ > 0.0f) ? w / width_ : 1.0f;
    float sy = (height_ > 0.0f) ? h / height_ : 1.0f;
    float s = (sx < sy) ? sx : sy;

    D2D1_MATRIX_3X2_F prev;
    ctx->GetTransform(&prev);

    // Compose: scale first, then translate. This keeps coordinates in
    // SVG user space and maps to the document position.
    D2D1::Matrix3x2F docT =
        D2D1::Matrix3x2F::Translation(x, y) * D2D1::Matrix3x2F::Scale(s, s);
    ctx->SetTransform(prev * docT);

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
    ctx->SetTransform(prev * docTransform);

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

    for (const auto& run : texts_) {
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
        float ty = run.y - baseline;

        // Set color
        D2D1_COLOR_F color = ParseColor(run.fill, defaultColor);
        br.p->SetColor(color);

        ctx->DrawTextLayout(
            D2D1::Point2F(tx, ty), tl, br.p,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

        tl->Release();
    }

    // Release cached formats and restore the caller's transform.
    for (auto& e : fmtCache) {
        if (e.fmt) e.fmt->Release();
    }
    ctx->SetTransform(prev);
}

}  // namespace svg
