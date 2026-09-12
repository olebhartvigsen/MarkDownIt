#include "seq_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mermaid {

namespace {

// Mermaid DEFAULT sequence config (seq_config.mjs dump), mirrored verbatim:
// useMaxWidth true, hideUnusedParticipants false, activationWidth 10,
// diagramMarginX 50, diagramMarginY 10, actorMargin 50, width 150,
// height 65, boxMargin 10, boxTextMargin 5, noteMargin 10, messageMargin 35,
// mirrorActors true, bottomMarginAdj 1, labelBoxWidth 50, labelBoxHeight 20,
// wrapPadding 10.
constexpr double DIAGRAM_MARGIN_X = 50.0;
constexpr double DIAGRAM_MARGIN_Y = 10.0;
constexpr double ACTOR_MARGIN = 50.0;
constexpr double ACTOR_W = 150.0;  // conf.width (initial actor width)
constexpr double ACTOR_H = 65.0;   // conf.height
constexpr double FOOT_H = 12.0;    // actor rect getBBox height (shim)
constexpr double BOX_MARGIN = 10.0;
constexpr double BOX_TEXT_MARGIN = 5.0;
constexpr double NOTE_MARGIN = 10.0;
constexpr double ACTIVATION_W = 10.0;
constexpr double LABEL_BOX_W = 50.0;
constexpr double LABEL_BOX_H = 20.0;
// Intentional design deviation from mermaid: vertical air after alt/opt/par/
// critical branch-label rows (both frame start and each else/and-option).
constexpr double kAltElseExtraSpace = 12.0;
constexpr double WRAP_PAD = 10.0;
constexpr double BOTTOM_MARGIN_ADJ = 1.0;

// JS Math.round: half away from zero (floor(x + 0.5) for our positives).
double JRound(double v) { return std::floor(v + 0.5); }
double Max(double a, double b) { return a > b ? a : b; }
double Min(double a, double b) { return a < b ? a : b; }

}  // namespace

// UTF-8 -> codepoint iterator (sequence labels are UTF-8; BMP only here).
// Advance table generated from segoeui.ttf (tools/mermaid-oracle/
// gen_seq_metrics.mjs): real Segoe UI glyph advances. The table lives in
// table scope: close mermaid, include, reopen.
}  // namespace mermaid
#include "seq_metrics_table.h"
namespace mermaid {

namespace {

// measure_dwrite.h exposes the platform measure seam; the portable core
// uses the generated table, the platform renderer swaps in DirectWrite
// behind the same function signature when MeasureSeqText is set.
SeqMeasureFn g_seq_measure = nullptr; // tests may inject

double DefaultSeqTextWidth(const std::string& utf8, double fontSize) {
    double total = 0;
    for (size_t i = 0; i < utf8.size(); ) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        uint32_t cp = 0;
        int n_b = 1;
        if (c < 0x80) { cp = c; n_b = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1F) << 6) | (utf8[i+1] & 0x3F); n_b = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0F) << 12) | ((utf8[i+1] & 0x3F) << 6) |
                 (utf8[i+2] & 0x3F); n_b = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07) << 18) | ((utf8[i+1] & 0x3F) << 12) |
                 ((utf8[i+2] & 0x3F) << 6) | (utf8[i+3] & 0x3F); n_b = 4;
        } else { cp = 0x20; n_b = 1; }
        uint16_t adv = SeqAdvanceFor(cp);
        if (adv == 0) adv = SeqAdvanceFor(0x20); // unmapped: space width
        total += adv;
        i += n_b;
    }
    // mermaid calculateTextDimensions rounds the measured width (Math.round
    // in the dims loop): mirror it so engine and oracle agree at half-px
    // boundaries.
    double px = total / kSeqFontUpm * fontSize;
    return std::floor(px + 0.5);
}

double DefaultSeqTextHeight(double fontSize) {
    return kSeqLineHeightPx16 / 16.0 * fontSize;
}

// Chrome getBBox of a drawn one-line SVG text: ink height round(size*1.06)
// (16px -> 17). Mermaid's drawNote sizes the rect from getBBox; only the
// NOTE box uses this; message geometry uses the line-box height (calcDims).
double DefaultSeqTextInkHeight(double fontSize) {
    return std::floor(fontSize * 1.06 + 0.5);
}

}  // namespace

double SeqTextWidth(const std::string& utf8, double fontSize) {
    if (g_seq_measure) return g_seq_measure(utf8, fontSize);
    return DefaultSeqTextWidth(utf8, fontSize);
}
double SeqTextHeight(const std::string&, double fontSize) {
    return DefaultSeqTextHeight(fontSize);
}
void SetSeqMeasureFn(SeqMeasureFn fn) { g_seq_measure = fn; }

LaidOutSequence LayoutSequence(const SequenceDiagram& seq) {
    LaidOutSequence out;
    if (!seq.error.empty()) {
        out.error = seq.error;
        return out;
    }
    out.autonumber = seq.autonumber;
    out.title = seq.title;

    const size_t N = seq.participants.size();
    std::vector<std::string> ids;
    std::vector<double> ax(N, 0.0), aw(N, ACTOR_W);
    for (const auto& p : seq.participants) ids.push_back(p.id);
    auto aidx = [&](const std::string& id) -> int {
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == id) return static_cast<int>(i);
        return -1;
    };

    // ---- getMaxMessageWidthPerActor (charging) ------------------------------
    // The DIST charges the ADJACENT pair only: actor = msg.to; if the SOURCE
    // is actor.prevActor (typical `A->>B`) B is charged; if the source is
    // actor.nextActor (`B-->>A`) msg.to (A, on the LEFT) is charged. Self
    // charges both ends width/2. Note OVER: prevActor gets width/2 and
    // msg.from gets width/2 (width = text + 2*wrapPadding). Non-adjacent
    // messages charge nobody.
    std::vector<double> mw(N, 0.0);
    auto charge = [&](int who, double w) {
        if (who >= 0) mw[who] = Max(mw[who], w);
    };
    for (const auto& it : seq.items) {
        if (it.msg_index < 0) continue;
        const SeqMessage& m = seq.messages[it.msg_index];
        int f = aidx(m.from), t = aidx(m.to);
        if (f < 0 || t < 0) continue;
        if (m.type == MsgType::Note) {
            // OVER-branch charging only applies when both neighbors exist;
            // single-actor note over A: actor = A: prevActor A-1 gets /2,
            // msg.from (A) gets /2; but the fixture single note over A
            // (seq3) shows charging does not change the 150/50 defaults,
            // A is actor[0] with NO prevActor: only msg.from gets width/2.
            double tw = SeqTextWidth(m.text) + 2 * WRAP_PAD;
            if (f == t) {
                // OVER: prevActor (f-1) width/2; msg.from f width/2.
                if (f > 0) charge(f - 1, tw / 2);
                charge(f, tw / 2);
            } else if (t == f + 1) {
                charge(f, tw / 2);  // msg.from (B in "over B,C")
                charge(t - 1 == f ? f : f, tw / 2); // actor.prevActor == f
            } else if (m.placement == NotePlacement::Over && t > 0) {
                charge(t - 1, tw / 2);
                charge(f, tw / 2);
            }
            continue;
        }
        double w = SeqTextWidth(m.text) + 2 * WRAP_PAD;
        if (f == t) {
            charge(f, w / 2);
        } else if (f == t + 1) {
            // msg.from is the TO-actor's nextActor → the DIST charges *,
            // i.e. the SENDER ((msg.from === actor.nextActor) →
            // maxMessageWidthPerActor[msg.to]); since actor = msg.to, this
            // lands on msg.to here.
            charge(t, w);
        } else if (t == f + 1) {
            // msg.from is the TO-actor's prevActor → the DIST charges
            // msg.from (the SENDER).
            charge(f, w);
        }
        // non-adjacent partners charge nobody (mermaid behavior)
    }

    // ---- calculateActorMargins ---------------------------------------------
    for (size_t i = 0; i < N; ++i) {
        // actor.width = max(conf.width, textWidth + 2*wrapPadding)
        aw[i] = Max(ACTOR_W, SeqTextWidth(seq.participants[i].display) + 2 * WRAP_PAD);
    }
    // actor.margin = max(messageWidth + actorMargin - w/2 - next.w/2, 50)
    for (size_t i = 0; i < N; ++i) {
        if (mw[i] <= 0.0) continue;
        double need = mw[i] + ACTOR_MARGIN - aw[i] / 2;
        if (i + 1 < N) need -= aw[i + 1] / 2;
        // final margin never below default (the walk applies Max with 50)
        mw[i] = need; // store potential margin (default handled later)
    }

    // ---- addActorRenderingData positions (no box groups) --------------------
    // actor.margin = max(need, conf.actorMargin); x = prevWidth (prevMargin
    // was already folded into prevWidth after each actor).
    double prev_w = 0.0;
    for (size_t i = 0; i < N; ++i) {
        double margin = Max(mw[i], ACTOR_MARGIN);
        SeqActorBox box;
        box.name = seq.participants[i].display;
        box.id = seq.participants[i].id;
        box.shape = seq.participants[i].shape;
        box.x = prev_w;
        box.w = aw[i];
        box.lifeline_x = box.x + aw[i] / 2;
        ax[i] = box.x;
        out.actors.push_back(box);
        prev_w += aw[i] + margin;
    }
    // VERIFIED seq2: A=0, B=0+150+50=200, C=200+150+50=400 (golden).

    // ---- bounds model -------------------------------------------------------
    double d_minx = 0, d_maxx = 0, d_miny = 0, d_maxy = 0;
    struct OpenLoop {
        size_t loop_index = 0;
        double starty = 0;     // captured start y (post pre-bump)
        double startx = 1e18, stopx = -1e18;
        double lo_y = 1e18, hi_y = -1e18; // inflated starty/stopy
        std::vector<double> section_y;
        std::vector<std::string> section_titles;
        std::vector<size_t> section_item; // loop_index of else/and/option
    };
    std::vector<OpenLoop> open;
    bool have_data = false;

    // activations db: {actor, startx, starty, stopx, stopy} (bounds.activations)
    // stopy tracks the band's growing bottom while open (-inf = open);
    // starty is stored at vP+2 (newActivation) and pulled by later inserts
    // with RAW values (mermaid updateBounds: activations inflate with
    // n = -index ≤ 0, i.e. never inflated, startx/stopx never touched).
    struct Act { int actor; double startx, starty, stopx, stopy; };
    std::vector<Act> acts;

    // mermaid updateBounds: data min/max + inflation of every open item by
    // n*boxMargin (n = distance from innermost, innermost=1). Activations
    // are NOT inflated in y (type check) but data still gets y inflation; 
    // activations are separate and not inflated at all; data starty/stopy
    // inflate via "if (!(type === activation))" only for ITEM bounds; data
    // startx/stopx always inflate; data starty/stopy inflate ONLY from
    // non-activation items. Net effect for our fixtures: identical because
    // activations are only inserted at deactivation (handled outside).
    auto insert = [&](double x0, double y0, double x1, double y1) {
        double nx0 = Min(x0, x1), nx1 = Max(x0, x1);
        double ny0 = Min(y0, y1), ny1 = Max(y0, y1);
        if (!have_data) {
            d_minx = nx0; d_maxx = nx1; d_miny = ny0; d_maxy = ny1;
            have_data = true;
        } else {
            d_minx = Min(d_minx, nx0); d_maxx = Max(d_maxx, nx1);
            d_miny = Min(d_miny, ny0); d_maxy = Max(d_maxy, ny1);
        }
        for (size_t k = 0; k < open.size(); ++k) {
            double n = static_cast<double>(k + 1);
            OpenLoop& ol = open[open.size() - 1 - k]; // innermost first
            ol.startx = Min(ol.startx, nx0 - n * BOX_MARGIN);
            ol.stopx = Max(ol.stopx, nx1 + n * BOX_MARGIN);
            ol.lo_y = Min(ol.lo_y, ny0 - n * BOX_MARGIN);
            ol.hi_y = Max(ol.hi_y, ny1 + n * BOX_MARGIN);
            // data also inflates by n*boxMargin (updateBounds parity)
            d_minx = Min(d_minx, nx0 - n * BOX_MARGIN);
            d_maxx = Max(d_maxx, nx1 + n * BOX_MARGIN);
            d_miny = Min(d_miny, ny0 - n * BOX_MARGIN);
            d_maxy = Max(d_maxy, ny1 + n * BOX_MARGIN);
        }
        // Open activation bands: updateBounds iterates activations AFTER
        // the sequenceItems, so the k-th open band (0-based, oldest first
        // in the push order) gets n = -k: starty is pulled toward
        // ny0 + k*10 (inner stacked bands are protected by k*10), stopy
        // toward ny1 - k*10. x never moves. A band's drawn rect is
        // [starty, activeEnd's vP]; stopy only feeds this pull model.
        for (size_t k = 0; k < acts.size(); ++k) {
            double n = -static_cast<double>(k);
            acts[k].starty = Min(acts[k].starty, ny0 - n * BOX_MARGIN);
            acts[k].stopy = Max(acts[k].stopy, ny1 + n * BOX_MARGIN);
        }
    };
    auto insert_dataonly = [&](double x0, double y0, double x1, double y1) {
        double nx0 = Min(x0, x1), nx1 = Max(x0, x1);
        double ny0 = Min(y0, y1), ny1 = Max(y0, y1);
        if (!have_data) {
            d_minx = nx0; d_maxx = nx1; d_miny = ny0; d_maxy = ny1;
            have_data = true;
            return;
        }
        d_minx = Min(d_minx, nx0); d_maxx = Max(d_maxx, nx1);
        d_miny = Min(d_miny, ny0); d_maxy = Max(d_maxy, ny1);
    };

    // ---- actor inserts + bump(verticalPos=maxHeight=65) --------------------
    // addActorRenderingData runs BEFORE any other item: vP = 0.
    for (size_t i = 0; i < N; ++i) {
        insert_dataonly(ax[i], 0.0, ax[i] + aw[i], ACTOR_H);
    }
    double vpos = ACTOR_H; // bumpVerticalPos(maxHeight)

    // |toLeft-toRight| > 2 (mermaid isArrowToActivation): the target already
    // carries an activation band.
    auto act_bounds = [&](int a, double& left, double& right) {
        left = ax[a] + aw[a] / 2 - 1;
        right = ax[a] + aw[a] / 2 + 1;
        for (const auto& ad : acts) {
            if (ad.actor != a) continue;
            left = Min(left, ad.startx);
            right = Max(right, ad.stopx);
        }
    };
    auto has_band = [&](int a) {
        for (const auto& ad : acts)
            if (ad.actor == a) return true;
        return false;
    };

    int auto_n = 1;

    for (const auto& it : seq.items) {
        switch (it.type) {
            case MsgType::Note: {
                const SeqMessage& m = seq.messages[it.msg_index];
                // drawNote: bump(boxMargin); height=10; starty = vP;
                vpos += BOX_MARGIN;
                double starty = vpos;
                double f = aidx(m.from);
                if (f < 0) break;
                std::size_t fi = static_cast<std::size_t>(f);
                double w, startx;
                if (m.placement == NotePlacement::RightOf) {
                    // dist: REQUIRES nextActor else skipped in parser spillover;
                    // fixture-free: width = max(w/2+to.w/2, text+2*noteMargin)
                    int t2 = aidx(m.to);
                    double tw = SeqTextWidth(m.text) + 2 * NOTE_MARGIN;
                    double half_sum = aw[fi] / 2 +
                        (t2 >= 0 ? aw[static_cast<std::size_t>(t2)] : aw[fi]) / 2;
                    w = Max(half_sum, tw);
                    startx = ax[fi] + (aw[fi] + ACTOR_MARGIN) / 2;
                } else if (m.placement == NotePlacement::LeftOf) {
                    int t2 = aidx(m.to);
                    double tw = SeqTextWidth(m.text) + 2 * NOTE_MARGIN;
                    double half_sum = aw[fi] / 2 +
                        (t2 >= 0 ? aw[static_cast<std::size_t>(t2)] : aw[fi]) / 2;
                    w = Max(half_sum, tw);
                    startx = ax[fi] - w + (aw[fi] - ACTOR_MARGIN) / 2;
                } else if (m.from == m.to) {
                    // width = max(actorW, conf.width, textW + 2*noteMargin)
                    double tw = SeqTextWidth(m.text) + 2 * NOTE_MARGIN;
                    w = Max(Max(aw[fi], ACTOR_W), tw);
                    startx = ax[fi] + (aw[fi] - w) / 2;
                } else {
                    // two-actor OVER: |centers| + actorMargin
                    int t2 = aidx(m.to);
                    if (t2 < 0) break;
                    std::size_t ti = static_cast<std::size_t>(t2);
                    w = std::fabs((ax[fi] + aw[fi] / 2) - (ax[ti] + aw[ti] / 2)) + ACTOR_MARGIN;
                    startx = ax[fi] < ax[ti]
                        ? ax[fi] + aw[fi] / 2 - ACTOR_MARGIN / 2
                        : ax[ti] + aw[ti] / 2 - ACTOR_MARGIN / 2;
                }
                double h = DefaultSeqTextInkHeight(16.0) + 2 * NOTE_MARGIN;
                SeqNoteGeo geo;
                geo.x = startx; geo.y = starty;
                geo.w = w; geo.h = h;
                // text: x = round(startx + w/2) [anchor middle, margin 10];
                // y first line = round(starty + (0+0+10)/2) = starty + 5.
                geo.tx = JRound(startx + w / 2);
                geo.ty = JRound(starty + NOTE_MARGIN / 2);
                geo.text = m.text;
                out.notes.push_back(geo);
                vpos += h; // bump(textHeight + 2*noteMargin)
                insert(startx, starty, startx + w, starty + h);
                break;
            }
            case MsgType::ActiveStart: {
                // newActivation: stacked index → x = center + (n-1)*5;
                // starty = vP + 2 (mermaid literal +2), stopy open (-inf).
                int a = it.actor_index;
                if (a < 0) break;
                size_t stacked = 0;
                for (const auto& ad : acts)
                    if (ad.actor == a) ++stacked;
                Act na;
                na.actor = a;
                na.startx = ax[a] + aw[a] / 2
                          + (static_cast<double>(stacked) - 1.0) * ACTIVATION_W / 2;
                na.starty = vpos + 2.0;
                na.stopx = na.startx + ACTIVATION_W;
                na.stopy = -1e17;
                acts.push_back(na);
                break;
            }
            case MsgType::ActiveEnd: {
                // activeEnd: splice last activation of the actor; raw inserts
                // since the band opened pull starty up / stopy down; min-height
                // clamp starty+18 > vP → (vP-6, vP+12); draw [starty, vP],
                // insert [vP-10, vP].
                for (std::vector<Act>::reverse_iterator ri = acts.rbegin();
                     ri != acts.rend(); ++ri) {
                    if (ri->actor != it.actor_index) continue;
                    Act ad = *ri;
                    acts.erase(std::next(ri).base());
                    double vp = vpos;
                    double starty = ad.starty;
                    if (starty + 18 > vp) {
                        starty = vp - 6;
                        vp += 12;
                        vpos = vp;
                    }
                    // The stored top already includes mermaid's +2 (newActivation stores
                    // y = vP + 2); the drawn rect spans [starty, vP] as-is.
                    SeqActivationGeo gg;
                    gg.x = ad.startx;
                    gg.y = starty;
                    gg.w = ad.stopx - ad.startx;
                    gg.h = vp - starty;
                    if (gg.h < 0) gg.h = 0;
                    out.activations.push_back(gg);
                    insert(ad.startx, vp - 10, ad.stopx, vp);
                    break;
                }
                break;
            }
            case MsgType::LoopStart:
            case MsgType::AltStart:
            case MsgType::OptStart:
            case MsgType::ParStart:
            case MsgType::CriticalStart:
            case MsgType::BreakStart:
            case MsgType::RectStart: {
                // adjustLoopHeightForWrap(preMargin=boxMargin): bump(10);
                // starty capture at newLoop (= vP after pre-bump);
                // titled kinds: bump(postMargin + max(textH, labelBoxH))
                //   postMargin = boxMargin + boxTextMargin = 15 → +35
                // rect: postMargin = boxMargin = 10 → +20.
                bool titled = it.type != MsgType::RectStart;
                OpenLoop ol;
                ol.loop_index = static_cast<size_t>(it.loop_index);
                vpos += BOX_MARGIN;             // preMargin
                ol.starty = vpos;
                if (titled) {
                    vpos += (BOX_MARGIN + BOX_TEXT_MARGIN) + Max(SeqTextHeight("x", 16.0), LABEL_BOX_H)
                          + kAltElseExtraSpace; // intentional extra air after label
                } else {
                    vpos += BOX_MARGIN;
                }
                open.push_back(ol);
                break;
            }
            case MsgType::AltElse:
            case MsgType::ParAnd:
            case MsgType::CriticalOption: {
                // adjustLoopHeightForWrap(pre=boxMargin+boxTextMargin=15,
                // post=boxMargin=10): bump(15); addSection records y = vP;
                // bump(10 + max(12,20) = 30).
                if (open.empty()) break;
                OpenLoop& ol = open.back();
                vpos += BOX_MARGIN + BOX_TEXT_MARGIN; // preMargin 15
                ol.section_y.push_back(vpos);
                const SeqLoop& lp = seq.loops[it.loop_id];
                ol.section_titles.push_back(lp.label);
                ol.section_item.push_back(it.loop_id);
                vpos += BOX_MARGIN + Max(SeqTextHeight("x", 16.0), LABEL_BOX_H)
                      + kAltElseExtraSpace; // intentional extra air after label
                break;
            }
            case MsgType::LoopEnd:
            case MsgType::AltEnd:
            case MsgType::OptEnd:
            case MsgType::ParEnd:
            case MsgType::CriticalEnd:
            case MsgType::BreakEnd:
            case MsgType::RectEnd: {
                if (open.empty()) break;
                OpenLoop ol = open.back();
                open.pop_back();
                SeqLoopGeo geo;
                geo.kind = seq.loops[ol.loop_index].kind;
                geo.startx = ol.startx;
                geo.stopx = ol.stopx;
                geo.starty = ol.lo_y;
                geo.stopy = ol.hi_y;
                geo.label = geo.kind;
                geo.title = seq.loops[ol.loop_index].label;
                geo.section_y = ol.section_y;
                geo.section_titles = ol.section_titles;
                // rect fill + background list
                if (geo.kind == "rect") {
                    SeqBackgroundGeo bg;
                    bg.x = geo.startx; bg.y = geo.starty;
                    bg.w = geo.stopx - geo.startx;
                    bg.h = geo.stopy - geo.starty;
                    bg.fill = seq.loops[ol.loop_index].fill;
                    out.backgrounds.push_back(bg);
                }
                out.loops.push_back(geo);
                // end-of-loop bump: stopy - vP (may be negative → NO bump
                // guard: mermaid bump() adds blindly; negative SHRINKS vP;
                // observed seq3: stopy 632 < vP? no: 632-622 = +10 (bump up).
                // For seq1 empty loops: stopy == starty... loop with NO
                // contents: no inserts → startx= +/-inf UNSET: mermaid
                // creates loopModel with startx=void → drawLoop lines NaN?
                // Actual seq1 golden: loopLineCount 0 (no lines drawn!); 
                // jsdom drops NaN attrs. Our loader accepts count 0.
                double delta = ol.hi_y - vpos;
                if (ol.hi_y > -1e17) vpos += delta;
                break;
            }
            default: {
                // ---- message (boundMessage) ------------------------------
                const SeqMessage& m = seq.messages[it.msg_index];
                int f = aidx(m.from), t2 = aidx(m.to);
                if (f < 0 || t2 < 0) break;
                std::size_t fi = static_cast<std::size_t>(f);
                std::size_t ti = static_cast<std::size_t>(t2);
                double starty = vpos;
                vpos += BOX_MARGIN;  // bump(10)
                vpos += SeqTextHeight(m.text, 16.0); // bump(text height)
                bool self = (m.from == m.to);
                double startx, stopx;
                double tx, ty;
                if (self) {
                    startx = stopx = ax[fi] + aw[fi] / 2;
                    // totalOffset = (12-10)=2; += boxMargin → 12;
                    // lineStartY = vP + 12 = starty+34; +30 → 42.
                    tx = startx;                                 // width 0
                    ty = JRound(starty + 10.0 + NOTE_MARGIN / 2); // +15
                } else {
                    double fl, fr, tl, tr;
                    act_bounds(f, fl, fr);
                    act_bounds(t2, tl, tr);
                    bool to_right = fl <= tl;
                    // buildMessageModel: startx = fromRight (right-going),
                    // stopx = toLeft; adjustments on stopx BEFORE text:
                    startx = to_right ? fr : fl;
                    stopx = to_right ? tl : tr;
                    double adj = to_right ? -1.0 : 1.0;
                    // msg.activate (mermaid buildMessageModel): only a TRAILING
                    // plus (arrow opens the TARGET) adjusts the endpoint toward
                    // the future band edge, and only when the target does not
                    // already carry a band (isArrowToActivation). A trailing
                    // minus (`-->>-`, closes the SOURCE) never adjusts the
                    // endpoint; golden e-probe: -->>- lands 1px inside the
                    // ±1 lifeline slack.
                    if (m.activation_delta > 0 && !has_band(t2)) {
                        stopx += adj * (ACTIVATION_W / 2 - 1);
                    }
                    if (m.type != MsgType::SolidOpen &&
                        m.type != MsgType::DottedOpen) {
                        stopx += adj * 3.0;
                    }
                    // text: x = round(startx + (stopx-startx)/2); y first
                    // line = round((starty + 10) + (0+0+10)/2) = starty + 15.
                    tx = JRound(startx + (stopx - startx) / 2);
                    ty = JRound(starty + 10.0 + NOTE_MARGIN / 2);
                }
                // boundMessage: lineStartY = vP_after_bumps + totalOffset,
                // where vP_after_bumps = starty + 10 + h and totalOffset =
                // (h - 10) + boxMargin(10) = h  →  starty + 10 + 2h.
                // (h = 12 shim-era → starty+34, the old constant.)
                double line_y = starty + 10.0 + 2.0 * SeqTextHeight(m.text, 16.0);
                if (self) {
                    // self: totalOffset starts (h-10), +boxMargin twice?
                    // no rightAngles: totalOffset = (h-10) + boxMargin(10) + 30
                    //   → vP + h + 30 = starty + 10 + h + h + 30?? mermaid:
                    //   lineStartY = vP + totalOffset with the FIRST +
                    //   (h-10)+10 = h: starty+10+h + h → same as non-self,
                    //   then totalOffset += 30 for the bump only (line stays
                    //   at the non-self position).
                    line_y = starty + 10.0 + 2.0 * SeqTextHeight(m.text, 16.0);
                }
                SeqMessageGeo g;
                g.self = self;
                g.x1 = startx; g.y1 = line_y;
                g.x2 = stopx; g.y2 = line_y;
                g.tx = tx; g.ty = ty;
                g.starty = starty;
                g.type = m.type;
                g.text = m.text;
                out.messages.push_back(g);
                // boundMessage inserts: line band + msg-model band.
                if (self) {
                    // self: totalOffset = (h-10) + boxMargin(10) + 30 = h+30;
                    // insert band spans vP-10+total → vP+30+total; dx per
                    // drawMessage self branch: max(textW/2, conf.width/2).
                    double h21 = SeqTextHeight(m.text, 16.0);
                    double toff = h21 + 30.0;
                    double dxs = Max(SeqTextWidth(m.text) / 2, ACTOR_W / 2);
                    insert(startx - dxs, vpos - 10.0 + toff, stopx + dxs,
                           vpos + 30.0 + toff);
                    vpos += toff;
                } else {
                    insert(startx, line_y - 10.0, stopx, line_y);
                    vpos += (SeqTextHeight(m.text, 16.0) - 10.0) + BOX_MARGIN;
                    // msg-model insert (buildMessageModel bounds):
                    double f_left, f_right, t_left, t_right;
                    act_bounds(f, f_left, f_right);
                    act_bounds(t2, t_left, t_right);
                    double fromb = Min(Min(f_left, f_right), Min(t_left, t_right));
                    double tob = Max(Max(f_left, f_right), Max(t_left, t_right));
                    insert(fromb, starty, tob, starty + 24.0);
                }
                if (out.autonumber) {
                    SeqNumberGeo nn;
                    nn.n = auto_n;
                    nn.x = startx;        // marker/num at START side x
                    nn.y = line_y + 4;
                    out.numbers.push_back(nn);
                }
                ++auto_n;
                break;
            }
        }
    }

    // ---- footer (mirrorActors=true, default) --------------------------------
    // drawActors(true): bump(boxMargin*2) → bottom-actor stopy; each actor
    // gets stopy = vP; participant footer height = 12 (shim); bump(12+10).
    vpos += BOX_MARGIN * 2;
    double foot_stopy = vpos;
    for (size_t i = 0; i < out.actors.size(); ++i) {
        out.actors[i].stopy = foot_stopy;
    }
    // Footer bump: real browsers draw the footer rects 65 DIP high; the
    // oracle shim (jsdom) reports 0 for them, but svg height in a real
    // render reserves the strip (reference SVG: rect bottom 2056 + margin
    // 11 = viewBox 2077). Adopt real semantics so the painter never draws
    // past the measured canvas on non-default column widths.
    vpos += 65.0 + BOX_MARGIN;
    insert_dataonly(d_minx, vpos, d_maxx, vpos); // stopy only: data.stopy tracks bumps
    // bumpVerticalPos already lifts data.stopy implicitly; emulate: maxy.
    d_maxy = Max(d_maxy, vpos);
    if (!have_data) {
        d_minx = 0; d_maxx = 0; d_miny = 0; d_maxy = vpos;
    }

    // ---- canvas -------------------------------------------------------------
    double box_w = d_maxx - d_minx;
    double box_h = d_maxy - d_miny;
    double height = box_h + 2 * DIAGRAM_MARGIN_Y - BOX_MARGIN + BOTTOM_MARGIN_ADJ;
    double width = box_w + 2 * DIAGRAM_MARGIN_X;
    out.width = width;
    out.height = height;
    out.vbheight = height;
    out.startx = d_minx - DIAGRAM_MARGIN_X;
    out.starty = d_miny - DIAGRAM_MARGIN_Y;
    if (!out.title.empty()) {
        out.title_x = (d_maxx - d_minx) / 2 - 2 * DIAGRAM_MARGIN_X;
        // viewBox grows by 40 vertically when a title exists:
        // svg height stays `height`; the viewBox y-min shifts up by 40 and
        // extends the reported height by 40 (extraVertForTitle).
        out.starty -= 40.0;
        out.vbheight += 40.0;
    }
    return out;
}

}  // namespace mermaid
