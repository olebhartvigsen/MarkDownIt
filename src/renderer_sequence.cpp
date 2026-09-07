#include "renderer.h"

#include "theme.h"
#include "mermaid/seq_layout.h"
#include "mermaid/layout_cache.h"

#include <cmath>
#include <string>
#include <d2d1.h>
#include <dwrite.h>

// Sequence diagram rendering. The layout (LaidOutSequence) is golden-gated
// against mermaid.js at 0.5 DIP; this file only paints that geometry. Colors
// follow mermaid's sequence default theme: actor box #ECECFF/#9370DB, note
// #ffffde/#9370DB, signal text #333333, lifeline #666666.
namespace {

constexpr uint32_t kSeqActorFill = 0xECECFF;
constexpr uint32_t kSeqActorStroke = 0x9370DB;
constexpr uint32_t kSeqNoteFill = 0xFFFFDE;
constexpr uint32_t kSeqInk = 0x333333;
constexpr uint32_t kSeqLifeline = 0x666666;
constexpr uint32_t kSeqLoopFill = 0xEDEDED;

constexpr float kActorBoxHeight = 65.0f;   // mermaid actor box height
constexpr float kActorFooterHeight = 20.0f;  // mirrorActors footer box height
constexpr float kActorFontSize = 14.0f;
constexpr float kMsgFontSize = 14.0f;
constexpr float kLabelFontSize = 12.0f;

// UTF-8 -> UTF-16 (same helper as the pie renderer).
void SeqUtf8To16(const std::string& s, std::u16string& out) {
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int n_b = 1;
        if (c < 0x80) { cp = c; n_b = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); n_b = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) |
                 (s[i+2] & 0x3F); n_b = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) |
                 ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); n_b = 4;
        } else { cp = '?'; n_b = 1; }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += n_b;
    }
}

// Draw one text run centered on (cx, cy) if asked; mirrors DrawPieText.
void DrawSeqText(IDWriteFactory* dw, ID2D1RenderTarget* rt,
                 IDWriteTextFormat* fmt, const std::string& utf8,
                 float cx, float cy, ID2D1SolidColorBrush* brush,
                 bool center_h, bool center_v) {
    if (!fmt || !brush) return;
    std::u16string t16;
    SeqUtf8To16(utf8, t16);
    if (t16.empty()) return;
    IDWriteTextLayout* tl = nullptr;
    if (FAILED(dw->CreateTextLayout(
            reinterpret_cast<const WCHAR*>(t16.data()),
            static_cast<UINT32>(t16.size()), fmt,
            1e9f, 1e9f, &tl)) || !tl) return;
    DWRITE_TEXT_METRICS tm{};
    tl->GetMetrics(&tm);
    float w = tm.widthIncludingTrailingWhitespace;
    float h = tm.height;
    float x = cx;
    float y = cy;
    if (center_h) x -= w * 0.5f;
    if (center_v) y -= h * 0.5f;
    rt->DrawTextLayout(D2D1::Point2F(x, y), tl, brush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
    tl->Release();
}

// Filled triangle arrowhead at (x, y) pointing along dir (+1 right, -1 left).
void DrawFilledHead(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                    float x, float y, float dir, float scale) {
    D2D1_POINT_2F tip = D2D1::Point2F(x, y);
    D2D1_POINT_2F up = D2D1::Point2F(x - 11.0f * dir * scale, y - 5.0f * scale);
    D2D1_POINT_2F dn = D2D1::Point2F(x - 11.0f * dir * scale, y + 5.0f * scale);
    ID2D1Factory* fac = nullptr;
    rt->GetFactory(&fac);
    if (!fac) return;
    ID2D1PathGeometry* geom = nullptr;
    if (FAILED(fac->CreatePathGeometry(&geom)) || !geom) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geom->Open(&sink)) && sink) {
        sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(up);
        sink->AddLine(dn);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        rt->FillGeometry(geom, brush);
    }
    geom->Release();
}

// Open (V-shaped) arrowhead for dotted messages: stroke only.
void DrawOpenHead(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                  float x, float y, float dir, float scale) {
    D2D1_POINT_2F up = D2D1::Point2F(x - 10.0f * dir * scale, y - 5.0f * scale);
    D2D1_POINT_2F dn = D2D1::Point2F(x - 10.0f * dir * scale, y + 5.0f * scale);
    rt->DrawLine(D2D1::Point2F(x, y), up, brush, 1.5f * scale);
    rt->DrawLine(D2D1::Point2F(x, y), dn, brush, 1.5f * scale);
}

}  // namespace

void Renderer::DrawMermaidSequenceBlock(ID2D1RenderTarget* rt,
                                        IDWriteFactory* dw,
                                        const Node& n, float x, float y) {
    if (!rt || !dw) return;
    if (!n.mermaid_seq) return;
    const auto& ls = *n.mermaid_seq;
    const float pad = mermaid::kMermaidBlockPad;
    const float scale = zoom_;
    // Golden coords live in mermaid's viewBox space (startx may be negative).
    // Translate into block-local space: origin at viewBox min + block pos.
    const float ox = x - static_cast<float>(ls.startx) * scale;
    const float oy = y + pad - static_cast<float>(ls.starty) * scale;

    auto P = [&](double v) { return ox + static_cast<float>(v) * scale; };
    auto Q = [&](double v) { return oy + static_cast<float>(v) * scale; };

    ID2D1SolidColorBrush* ink = nullptr;
    ID2D1SolidColorBrush* actorFill = nullptr;
    ID2D1SolidColorBrush* actorStroke = nullptr;
    ID2D1SolidColorBrush* noteFill = nullptr;
    ID2D1SolidColorBrush* lifeline = nullptr;
    ID2D1SolidColorBrush* loopFill = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(kSeqInk), &ink);
    rt->CreateSolidColorBrush(D2D1::ColorF(kSeqActorFill), &actorFill);
    rt->CreateSolidColorBrush(D2D1::ColorF(kSeqActorStroke), &actorStroke);
    rt->CreateSolidColorBrush(D2D1::ColorF(kSeqNoteFill), &noteFill);
    rt->CreateSolidColorBrush(D2D1::ColorF(kSeqLifeline), &lifeline);
    rt->CreateSolidColorBrush(D2D1::ColorF(kSeqLoopFill), &loopFill);
    if (!ink || !actorFill || !actorStroke || !noteFill || !lifeline || !loopFill) {
        if (ink) ink->Release();
        if (actorFill) actorFill->Release();
        if (actorStroke) actorStroke->Release();
        if (noteFill) noteFill->Release();
        if (lifeline) lifeline->Release();
        if (loopFill) loopFill->Release();
        return;
    }

    // Background rects (rect blocks) first.
    for (const auto& b : ls.backgrounds) {
        D2D1_RECT_F rc = D2D1::RectF(P(b.x), Q(b.y),
                                     P(b.x + b.w), Q(b.y + b.h));
        // Golden fill is a hex string from the oracle; default soft gray.
        rt->FillRectangle(rc, loopFill);
    }

    // Loop / alt / par / opt / critical / break frames.
    for (const auto& l : ls.loops) {
        D2D1_RECT_F rc = D2D1::RectF(P(l.startx), Q(l.starty),
                                     P(l.stopx), Q(l.stopy));
        rt->DrawRectangle(rc, ink, 1.0f * scale);
        // Label box: filled tag with the keyword at the frame's top-left.
        if (!l.label.empty()) {
            float lw = 60.0f * scale, lh = 20.0f * scale;
            D2D1_RECT_F lb = D2D1::RectF(P(l.startx), Q(l.starty),
                                         P(l.startx) + lw, Q(l.starty) + lh);
            rt->FillRectangle(lb, actorFill);
            rt->DrawRectangle(lb, ink, 1.0f * scale);
            DrawSeqText(dw, rt, body_fmt_, l.label,
                        P(l.startx) + lw * 0.5f, Q(l.starty) + lh * 0.5f,
                        ink, true, true);
        }
        if (!l.title.empty()) {
            DrawSeqText(dw, rt, body_fmt_, l.title,
                        P(l.startx) + 70.0f * scale, Q(l.starty) + 10.0f * scale,
                        ink, false, true);
        }
        // Section dividers: dashed line + branch title.
        for (size_t i = 0; i < l.section_y.size(); ++i) {
            float sy = Q(l.section_y[i]);
            rt->DrawLine(D2D1::Point2F(P(l.startx), sy),
                         D2D1::Point2F(P(l.stopx), sy),
                         ink, 1.0f * scale);
            if (i < l.section_titles.size() && !l.section_titles[i].empty()) {
                DrawSeqText(dw, rt, body_fmt_, l.section_titles[i],
                            P(l.startx) + 70.0f * scale, sy + 10.0f * scale,
                            ink, false, true);
            }
        }
    }

    // Lifelines (between top box bottom and footer/top of bottom box).
    for (const auto& a : ls.actors) {
        (void)a;  // lifelines are derived from actor boxes below
    }
    for (size_t i = 0; i < ls.actors.size(); ++i) {
        const auto& a = ls.actors[i];
        float cx = P(a.lifeline_x);
        float top_bottom = Q(0.0) + kActorBoxHeight * scale;
        float footer_top = Q(a.stopy);
        if (footer_top < top_bottom) footer_top = top_bottom;
        rt->DrawLine(D2D1::Point2F(cx, top_bottom),
                     D2D1::Point2F(cx, footer_top),
                     lifeline, 1.0f * scale);
        (void)i;
    }

    // Activations.
    for (const auto& a : ls.activations) {
        D2D1_RECT_F rc = D2D1::RectF(P(a.x), Q(a.y),
                                     P(a.x + a.w), Q(a.y + a.h));
        rt->FillRectangle(rc, actorFill);
        rt->DrawRectangle(rc, actorStroke, 1.0f * scale);
    }

    // Messages (lines and self paths) with heads and text.
    for (const auto& m : ls.messages) {
        bool dotted = (m.type == mermaid::MsgType::Dotted);
        if (m.self) {
            // Self path: M x,y1 C x+60,y1-10 x+60,y1+30 x,y1+20 (relative
            // to mermaid's own coordinate scheme; endpoints in geo).
            float sx = P(m.x1), sy = Q(m.y1);
            float ex = P(m.x2), ey = Q(m.y2);
            ID2D1Factory* fac = nullptr;
            rt->GetFactory(&fac);
            ID2D1PathGeometry* geom = nullptr;
            if (fac && SUCCEEDED(fac->CreatePathGeometry(&geom)) && geom) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(geom->Open(&sink)) && sink) {
                    sink->BeginFigure(D2D1::Point2F(sx, sy),
                                      D2D1_FIGURE_BEGIN_HOLLOW);
                    D2D1_BEZIER_SEGMENT bz = {};
                    bz.point1 = D2D1::Point2F(sx + 60.0f * scale,
                                              sy - 10.0f * scale);
                    bz.point2 = D2D1::Point2F(sx + 60.0f * scale,
                                              sy + 30.0f * scale);
                    bz.point3 = D2D1::Point2F(ex, ey);
                    sink->AddBezier(bz);
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    sink->Release();
                    rt->DrawGeometry(geom, ink, 1.5f * scale);
                }
                geom->Release();
            }
            // Head points down-ish at the return; draw small filled head.
            D2D1_POINT_2F tip = D2D1::Point2F(ex, ey);
            D2D1_POINT_2F u = D2D1::Point2F(ex + 10.0f * scale, ey - 4.0f * scale);
            D2D1_POINT_2F d = D2D1::Point2F(ex + 10.0f * scale, ey + 4.0f * scale);
            ID2D1Factory* fac2 = nullptr;
            rt->GetFactory(&fac2);
            ID2D1PathGeometry* hg = nullptr;
            if (fac2 && SUCCEEDED(fac2->CreatePathGeometry(&hg)) && hg) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(hg->Open(&sink)) && sink) {
                    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(u);
                    sink->AddLine(d);
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                    rt->FillGeometry(hg, ink);
                }
                hg->Release();
            }
        } else {
            float x1 = P(m.x1), y1 = Q(m.y1), x2 = P(m.x2), y2 = Q(m.y2);
            float dir = (x2 >= x1) ? 1.0f : -1.0f;
            if (dotted) {
                // Dashed by drawing short segments (D2D1 stroke style would
                // need a factory-held style; segments keep it local).
                float dx = x2 - x1, dy = y2 - y1;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.5f) {
                    float ux = dx / len, uy = dy / len;
                    float seg = 5.0f * scale, gap = 4.0f * scale;
                    float t = 0.0f;
                    while (t < len) {
                        float t2 = (t + seg < len) ? t + seg : len;
                        rt->DrawLine(D2D1::Point2F(x1 + ux * t, y1 + uy * t),
                                     D2D1::Point2F(x1 + ux * t2, y1 + uy * t2),
                                     ink, 1.5f * scale);
                        t += seg + gap;
                    }
                }
                DrawOpenHead(rt, ink, x2, y2, dir, scale);
            } else {
                rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                             ink, 1.5f * scale);
                DrawFilledHead(rt, ink, x2, y2, dir, scale);
            }
        }
        if (!m.text.empty()) {
            DrawSeqText(dw, rt, body_fmt_, m.text,
                        P(m.tx), Q(m.ty), ink, true, true);
        }
    }

    // Autonumber circles: filled circle at (startx, lineStartY + 4).
    for (const auto& num : ls.numbers) {
        float cx = P(num.x), cy = Q(num.y);
        D2D1_ELLIPSE c = D2D1::Ellipse(D2D1::Point2F(cx, cy),
                                       9.0f * scale, 9.0f * scale);
        rt->FillEllipse(c, ink);
        DrawSeqText(dw, rt, body_fmt_, std::to_string(num.n),
                    cx, cy, actorFill, true, true);
    }

    // Notes.
    for (const auto& nt : ls.notes) {
        D2D1_RECT_F rc = D2D1::RectF(P(nt.x), Q(nt.y),
                                     P(nt.x + nt.w), Q(nt.y + nt.h));
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(rc, 4.0f * scale, 4.0f * scale), noteFill);
        rt->DrawRoundedRectangle(
            D2D1::RoundedRect(rc, 4.0f * scale, 4.0f * scale), actorStroke,
            1.0f * scale);
        DrawSeqText(dw, rt, body_fmt_, nt.text, P(nt.tx), Q(nt.ty),
                    ink, true, true);
    }

    // Actor boxes: top first pass stored, bottom uses stopy. Top box y=0.
    for (const auto& a : ls.actors) {
        float bx = P(a.x);
        float bw = static_cast<float>(a.w) * scale;
        D2D1_RECT_F top = D2D1::RectF(bx, Q(0.0),
                                      bx + bw, Q(0.0) + kActorBoxHeight * scale);
        rt->FillRectangle(top, actorFill);
        rt->DrawRectangle(top, actorStroke, 1.0f * scale);
        DrawSeqText(dw, rt, body_fmt_, a.name,
                    bx + bw * 0.5f, Q(0.0) + kActorBoxHeight * scale * 0.5f,
                    ink, true, true);
        // Footer (mirrorActors): small box at stopy.
        float fy = Q(a.stopy);
        D2D1_RECT_F foot = D2D1::RectF(bx, fy, bx + bw,
                                       fy + kActorFooterHeight * scale);
        rt->FillRectangle(foot, actorFill);
        rt->DrawRectangle(foot, actorStroke, 1.0f * scale);
        DrawSeqText(dw, rt, body_fmt_, a.name,
                    bx + bw * 0.5f, fy + kActorFooterHeight * scale * 0.5f,
                    ink, true, true);
    }

    // Title: mermaid draws it at (title_x, -25) in viewBox space; the
    // viewBox already shifts by -40 so Q(-25) lands inside the block.
    if (!ls.title.empty()) {
        DrawSeqText(dw, rt, body_fmt_, ls.title,
                    P(ls.title_x), Q(-25.0), ink, true, true);
    }

    ink->Release();
    actorFill->Release();
    actorStroke->Release();
    noteFill->Release();
    lifeline->Release();
    loopFill->Release();
}
