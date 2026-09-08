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
constexpr double ACTOR_W = 150.0;   // conf.width (initial actor width)
constexpr double ACTOR_H = 65.0;    // conf.height
constexpr double FOOT_H = 12.0;     // actor rect getBBox height (shim)
constexpr double BOX_MARGIN = 10.0;
constexpr double BOX_TEXT_MARGIN = 5.0;
constexpr double NOTE_MARGIN = 10.0;
constexpr double ACTIVATION_W = 10.0;
constexpr double LABEL_BOX_W = 50.0;
constexpr double LABEL_BOX_H = 20.0;
constexpr double TEXT_H = 12.0;     // shim text height (one line)
constexpr double TEXT_UNIT = 4.0;   // shim width per UTF-16 unit
constexpr double WRAP_PAD = 10.0;
constexpr double BOTTOM_MARGIN_ADJ = 1.0;

// JS Math.round: half away from zero (floor(x + 0.5) for our positives).
double JRound(double v) { return std::floor(v + 0.5); }
double Max(double a, double b) { return a > b ? a : b; }
double Min(double a, double b) { return a < b ? a : b; }

}  // namespace

double SeqTextWidth(const std::string& utf8) {
    double n = 0;
    for (unsigned char c : utf8) {
        if ((c & 0xC0) != 0x80) ++n;  // non-continuation = UTF-16 unit (BMP)
    }
    return n * TEXT_UNIT;
}
double SeqTextHeight(const std::string&) { return TEXT_H; }

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
            // msg.from (A) gets /2 — but the fixture single note over A
            // (seq3) shows charging does not change the 150/50 defaults,
            // A is actor[0] with NO prevActor: only msg.from gets width/2.
            double tw = SeqTextWidth(m.text) + 2 * WRAP_PAD;
            if (f == t) {
                // OVER: prevActor (f-1) width/2; msg.from f width/2.
                if (f > 0) charge(f - 1, tw / 2);
                charge(f, tw / 2);
            } else if (t == f + 1) {
                charge(f, tw / 2);   // msg.from (B in "over B,C")
                charge(t - 1 == f ? f : f, tw / 2);  // actor.prevActor == f
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
            // maxMessageWidthPerActor[msg.to]) — since actor = msg.to, this
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
        mw[i] = need;  // store potential margin (default handled later)
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
        double starty = 0;      // captured start y (post pre-bump)
        double startx = 1e18, stopx = -1e18;
        double lo_y = 1e18, hi_y = -1e18;  // inflated starty/stopy
        std::vector<double> section_y;
        std::vector<std::string> section_titles;
        std::vector<size_t> section_item;  // loop_index of else/and/option
    };
    std::vector<OpenLoop> open;
    bool have_data = false;

    // mermaid updateBounds: data min/max + inflation of every open item by
    // n*boxMargin (n = distance from innermost, innermost=1). Activations
    // are NOT inflated in y (type check) but data still gets y inflation —
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
            OpenLoop& ol = open[open.size() - 1 - k];  // innermost first
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
    double vpos = ACTOR_H;  // bumpVerticalPos(maxHeight)

    // activations db: {actor, startx, starty, stopx} (bounds.activations)
    struct Act { int actor; double startx, starty, stopx; };
    std::vector<Act> acts;

    auto act_bounds = [&](int a, double& left, double& right) {
        left = ax[a] + aw[a] / 2 - 1;
        right = ax[a] + aw[a] / 2 + 1;
        for (const auto& ad : acts) {
            if (ad.actor != a) continue;
            left = Min(left, ad.startx);
            right = Max(right, ad.stopx);
        }
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
                double h = TEXT_H + 2 * NOTE_MARGIN;  // bump(12 + 20 = 32)
                SeqNoteGeo geo;
                geo.x = startx; geo.y = starty;
                geo.w = w; geo.h = h;
                // text: x = round(startx + w/2) [anchor middle, margin 10];
                // y first line = round(starty + (0+0+10)/2) = starty + 5.
                geo.tx = JRound(startx + w / 2);
                geo.ty = JRound(starty + NOTE_MARGIN / 2);
                geo.text = m.text;
                out.notes.push_back(geo);
                vpos += h;  // bump(textHeight + 2*noteMargin)
                insert(startx, starty, startx + w, starty + h);
                break;
            }
            case MsgType::ActiveStart: {
                // bounds.newActivation: stacked index → x = center + (n-1)*5;
                int a = it.actor_index;
                if (a < 0) break;
                size_t stacked = 0;
                for (const auto& ad : acts)
                    if (ad.actor == a) ++stacked;
                Act na;
                na.actor = a;
                na.startx = ax[a] + aw[a] / 2
                          + (static_cast<double>(stacked) - 1.0) * ACTIVATION_W / 2;
                na.starty = vpos;          // GOLDEN: y = current vP
                na.stopx = na.startx + ACTIVATION_W;
                acts.push_back(na);
                break;
            }
            case MsgType::ActiveEnd: {
                // activeEnd: splice last activation of the actor; min-height
                // rule 18 (starty=vp-6, vP+=12); draw + insert.
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
                    SeqActivationGeo gg;
                    gg.x = ad.startx;
                    gg.y = starty;
                    gg.w = ad.stopx - ad.startx;
                    gg.h = vp - starty;
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
                vpos += BOX_MARGIN;              // preMargin
                ol.starty = vpos;
                if (titled) {
                    vpos += (BOX_MARGIN + BOX_TEXT_MARGIN) + Max(TEXT_H, LABEL_BOX_H);
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
                vpos += BOX_MARGIN + BOX_TEXT_MARGIN;  // preMargin 15
                ol.section_y.push_back(vpos);
                const SeqLoop& lp = seq.loops[it.loop_id];
                ol.section_titles.push_back(lp.label);
                ol.section_item.push_back(it.loop_id);
                vpos += BOX_MARGIN + Max(TEXT_H, LABEL_BOX_H);  // +30
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
                // Actual seq1 golden: loopLineCount 0 (no lines drawn!) —
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
                vpos += BOX_MARGIN;   // bump(10)
                vpos += TEXT_H;       // bump(lineHeight=12) → starty+22
                bool self = (m.from == m.to);
                double startx, stopx;
                double tx, ty;
                if (self) {
                    startx = stopx = ax[fi] + aw[fi] / 2;
                    // totalOffset = (12-10)=2; += boxMargin → 12;
                    // lineStartY = vP + 12 = starty+34; +30 → 42.
                    tx = startx;                                  // width 0
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
                    // activation head lands on an activation band (±1)
                    if (m.activation_delta != 0 && std::fabs(tl - tr) > 2) {
                        // msg.activate is carried by the syntax `->>+`; the
                        // isArrowToActivation test is |toLeft-toRight| > 2 —
                        // NOTE dist: isArrowToActivation = abs(toLeft -
                        // toRight) > 2; adjust stopx toward the band edge:
                        stopx += adj * (ACTIVATION_W / 2 - 1);
                    } else if (m.activation_delta != 0) {
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
                double line_y = starty + 34.0;
                if (self) {
                    // self: lineStartY = vP_after_pre + totalOffset(2+10)
                    //      = starty + 34 (same); the path draws from there.
                    line_y = starty + 34.0;
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
                    double dx = Max(SeqTextWidth(m.text) / 2, ACTOR_W / 2);
                    insert(startx - dx, vpos - 10.0 + 42.0, stopx + dx,
                           vpos + 30.0 + 42.0);
                    vpos += 42.0;   // bump(totalOffset=42) → starty+64
                } else {
                    insert(startx, line_y - 10.0, stopx, line_y);
                    vpos += (TEXT_H - 10.0) + BOX_MARGIN;  // +12 → = line_y
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
                    nn.x = startx;         // marker/num at START side x
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
    vpos += FOOT_H + BOX_MARGIN;
    insert_dataonly(d_minx, vpos, d_maxx, vpos);  // stopy only: data.stopy tracks bumps
    // bumpVerticalPos already lifts data.stopy implicitly — emulate: maxy.
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
