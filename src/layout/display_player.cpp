// The replay body: one switch over the item variant, in list order,
// onto the seam. A dashed line and an underlined run expand to
// several seam calls, but only inside their own case, and the dash
// and decoration rules are resolved here once, so a Direct2D painter
// and a Core Graphics one cannot drift on what a dash or an
// underline means.

#include "layout/display_player.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace display {
namespace {

// One dashed pen. The entries are run lengths in points that
// alternate pen down and pen up; an odd count is doubled so every
// second entry is a gap, and the phase survives from piece to piece,
// the way one pen strokes one path. An empty pattern, or one with no
// positive length at all, draws solid: the pen never lifts.
class DashPen {
public:
    explicit DashPen(const std::vector<float>& dash) {
        entries_ = dash;
        if (!entries_.empty() && entries_.size() % 2 == 1)
            entries_.insert(entries_.end(), entries_.begin(), entries_.end());
        float total = 0.0f;
        for (float v : entries_) total += v;
        solid_ = entries_.empty() || total <= 0.0f;
        if (!solid_) left_ = entries_.front();
    }

    // Strokes one straight piece of the route as solid segments.
    void Piece(platform::RenderTarget* target, float x0, float y0,
               float x1, float y1, const Color& color, float lineWidth) {
        if (solid_) {
            target->StrokeLine(x0, y0, x1, y1, color, lineWidth);
            return;
        }
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0f) return;  // a zero piece moves the pen nowhere
        const float ux = dx / len, uy = dy / len;
        float walked = 0.0f;
        while (walked < len) {
            if (left_ <= 0.0f) {  // entry spent, the next one, toggled
                index_ = (index_ + 1) % entries_.size();
                down_ = !down_;
                left_ = entries_[index_];
                continue;
            }
            const float step = left_ < len - walked ? left_ : len - walked;
            if (down_)
                target->StrokeLine(x0 + ux * walked, y0 + uy * walked,
                                   x0 + ux * (walked + step),
                                   y0 + uy * (walked + step), color,
                                   lineWidth);
            walked += step;
            left_ -= step;
        }
    }

private:
    std::vector<float> entries_;
    size_t index_ = 0;   // the current entry, an even one is pen down
    float left_ = 0.0f;  // what remains of that entry
    bool down_ = true;
    bool solid_ = true;
};

// The width a run and its decorations cover: up to the next run, or
// up to the line width for the last run of the line.
float RunWidth(const TextLine& line, size_t index) {
    const float end =
        index + 1 < line.runs.size() ? line.runs[index + 1].x : line.width;
    return end - line.runs[index].x;
}

// One Text item: every run at its offset on the shared baseline, then
// the underline and the strike of that run while its color is at
// hand. An underline sits two points under the baseline, a strike
// three tenths of the font size above it, and both run the full
// width of the run with a one point pen.
void PlayText(platform::RenderTarget* target, const Text& item) {
    const float baseline = item.top + item.line.ascent;
    for (size_t i = 0; i < item.line.runs.size(); ++i) {
        const platform::GlyphRun& run = item.line.runs[i];
        const Color color{run.r, run.g, run.b, run.a};
        const float x = item.x + run.x, width = RunWidth(item.line, i);
        target->DrawText(run.text, run.font, x, baseline, color);
        if (run.underline)
            target->StrokeLine(x, baseline + 2.0f, x + width,
                               baseline + 2.0f, color, 1.0f);
        if (run.strike)
            target->StrokeLine(x, baseline - 0.30f * run.font.size,
                               x + width, baseline - 0.30f * run.font.size,
                               color, 1.0f);
    }
}

// A route: a solid one enters the seam whole, a dashed one is
// flattened with the pattern continuing across the vertices.
void PlayRoute(platform::RenderTarget* target, const StrokePolygon& item) {
    if (item.dash.empty()) {
        target->StrokePolygon(item.points, item.color, item.lineWidth,
                              item.closed);
        return;
    }
    DashPen pen(item.dash);
    for (size_t i = 1; i < item.points.size(); ++i)
        pen.Piece(target, item.points[i - 1].x, item.points[i - 1].y,
                  item.points[i].x, item.points[i].y, item.color,
                  item.lineWidth);
    if (item.closed && !item.points.empty())
        pen.Piece(target, item.points.back().x, item.points.back().y,
                  item.points.front().x, item.points.front().y, item.color,
                  item.lineWidth);
}

// The single replay switch. Every alternative in list order, each one
// with the seam calls it stands for; the resolver hook is the only
// state the walk keeps besides the target itself.
void PlayItems(const DisplayList& list, platform::RenderTarget* target,
               const ImageResolver* resolver) {
    for (const Item& item : list.items) {
        if (const auto* v = std::get_if<FillRect>(&item))
            target->FillRect(v->rect, v->color);
        else if (const auto* v = std::get_if<FillRoundedRect>(&item))
            target->FillRoundedRect(v->rect, v->radius, v->color);
        else if (const auto* v = std::get_if<FillRoundedRectSides>(&item))
            target->FillRoundedRectSides(v->rect, v->radius, v->color,
                                        v->roundLeft, v->roundRight);
        else if (const auto* v = std::get_if<StrokeRect>(&item))
            target->StrokeRect(v->rect, v->color, v->lineWidth);
        else if (const auto* v = std::get_if<StrokeRoundedRect>(&item))
            target->StrokeRoundedRect(v->rect, v->radius, v->color,
                                     v->lineWidth);
        else if (const auto* v = std::get_if<StrokeLine>(&item))
            DashPen(v->dash).Piece(target, v->x0, v->y0, v->x1, v->y1,
                                  v->color, v->lineWidth);
        else if (const auto* v = std::get_if<StrokePolygon>(&item))
            PlayRoute(target, *v);
        else if (const auto* v = std::get_if<FillEllipse>(&item))
            target->FillEllipse(v->rect, v->color);
        else if (const auto* v = std::get_if<StrokeEllipse>(&item))
            target->StrokeEllipse(v->rect, v->color, v->lineWidth);
        else if (const auto* v = std::get_if<FillPolygon>(&item))
            target->FillPolygon(v->points, v->color);
        else if (const auto* v = std::get_if<PieSlice>(&item))
            target->FillPieSlice(v->bounds, v->startAngle, v->endAngle,
                                 v->color);
        else if (const auto* v = std::get_if<Text>(&item))
            PlayText(target, *v);
        else if (const auto* v = std::get_if<Image>(&item)) {
            if (resolver && *resolver) {
                const platform::BitmapHandle bitmap = (*resolver)(*v);
                if (bitmap) target->DrawBitmap(bitmap, v->rect);
            }
        } else if (const auto* v = std::get_if<PushClip>(&item))
            target->PushClip(v->rect);
        else if (std::holds_alternative<PopClip>(item))
            target->PopClip();
    }
}

// Translation of one alternative. One overload per item, no generic
// fallback, for the same reason ItemBounds has none: a new kind of
// drawing stops compiling until its place on paper is decided here.
void Translate(FillRect& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(FillRoundedRect& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(FillRoundedRectSides& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(StrokeRect& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(StrokeRoundedRect& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(StrokeLine& v, float dx, float dy) {
    v.x0 += dx; v.y0 += dy; v.x1 += dx; v.y1 += dy;
}
void Translate(StrokePolygon& v, float dx, float dy) {
    for (Point& p : v.points) { p.x += dx; p.y += dy; }
}
void Translate(FillEllipse& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(StrokeEllipse& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(FillPolygon& v, float dx, float dy) {
    for (Point& p : v.points) { p.x += dx; p.y += dy; }
}
void Translate(PieSlice& v, float dx, float dy) {
    v.bounds.x += dx; v.bounds.y += dy;
}
void Translate(Text& v, float dx, float dy) {
    v.x += dx; v.top += dy;
}
void Translate(Image& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(PushClip& v, float dx, float dy) {
    v.rect.x += dx; v.rect.y += dy;
}
void Translate(PopClip&, float, float) {}

// Whole-list translation, for the print placement. The unshifted walk is
// untouched above; a shifted play copies once and replays, which costs
// one page's worth of items, the same class of work PageItems already
// spends on its slice.
void PlayAll(const DisplayList& list, platform::RenderTarget* target,
             const ImageResolver* resolver, float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) {
        PlayItems(list, target, resolver);
        return;
    }
    DisplayList moved;
    moved.width = list.width;
    moved.height = list.height;
    moved.items.reserve(list.items.size());
    for (const Item& item : list.items) {
        Item one = item;
        std::visit([&](auto& v) { Translate(v, dx, dy); }, one);
        moved.items.push_back(std::move(one));
    }
    PlayItems(moved, target, resolver);
}

// Bounds ---------------------------------------------------------------
//
// One overload per alternative, and no generic fallback: an item type that
// is added to the variant stops compiling here, which is the guard that keeps
// pagination from dropping a new kind of drawing off the page.
//
// The bounds are the shape's own geometry, not grown by the stroke width.
// The cut decides by where an item is, not by how wide its pen is: a table
// rule drawn exactly on a page's last line belongs to the next page, and
// growing a hairline by half its width on both sides would put a half line at
// the foot of one page and the other half at the head of the next.

Rect BoundsOf(const FillRect& v) { return v.rect; }
Rect BoundsOf(const FillRoundedRect& v) { return v.rect; }
Rect BoundsOf(const FillRoundedRectSides& v) { return v.rect; }
Rect BoundsOf(const StrokeRect& v) { return v.rect; }
Rect BoundsOf(const StrokeRoundedRect& v) { return v.rect; }
Rect BoundsOf(const StrokeLine& v) {
    Rect r;
    r.x = (std::min)(v.x0, v.x1);
    r.y = (std::min)(v.y0, v.y1);
    r.width = std::fabs(v.x1 - v.x0);
    r.height = std::fabs(v.y1 - v.y0);
    return r;
}
Rect BoundsOf(const StrokePolygon& v) {
    if (v.points.empty()) return Rect{};
    Rect r;
    r.x = r.y = std::numeric_limits<float>::max();
    float right = -std::numeric_limits<float>::max();
    float bottom = -std::numeric_limits<float>::max();
    for (const Point& p : v.points) {
        r.x = (std::min)(r.x, p.x);
        r.y = (std::min)(r.y, p.y);
        right = (std::max)(right, p.x);
        bottom = (std::max)(bottom, p.y);
    }
    r.width = right - r.x;
    r.height = bottom - r.y;
    return r;
}
Rect BoundsOf(const FillEllipse& v) { return v.rect; }
Rect BoundsOf(const StrokeEllipse& v) { return v.rect; }
Rect BoundsOf(const FillPolygon& v) {
    StrokePolygon route;
    route.points = v.points;
    return BoundsOf(route);
}
Rect BoundsOf(const PieSlice& v) { return v.bounds; }
Rect BoundsOf(const Text& v) {
    Rect r;
    r.x = v.x;
    r.y = v.top;
    r.width = v.line.width;
    r.height = v.line.height;
    return r;
}
Rect BoundsOf(const Image& v) { return v.rect; }
Rect BoundsOf(const PushClip& v) { return v.rect; }
Rect BoundsOf(const PopClip&) { return Rect{}; }

}  // namespace

Rect ItemBounds(const Item& item) {
    return std::visit([](const auto& v) { return BoundsOf(v); }, item);
}

void Play(const DisplayList& list, platform::RenderTarget* target) {
    PlayAll(list, target, nullptr, 0.0f, 0.0f);
}

void Play(const DisplayList& list, platform::RenderTarget* target,
          const ImageResolver& imageResolver) {
    PlayAll(list, target, &imageResolver, 0.0f, 0.0f);
}

void PlayWithOrigin(const DisplayList& list, platform::RenderTarget* target,
                    float dx, float dy) {
    PlayAll(list, target, nullptr, dx, dy);
}

}  // namespace display
