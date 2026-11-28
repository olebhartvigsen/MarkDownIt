// The inline code pill pass: one body for both ports.

#include "layout/chips.h"

#include <algorithm>
#include <vector>

#include "layout/text_runs.h"

namespace layout {
namespace {

struct Fragment {
    platform::Rect rect;
    display::SourceRange span;
};

bool SameSpan(const display::SourceRange& a, const display::SourceRange& b) {
    return a.offset == b.offset && a.length == b.length &&
           a.inlineCode == b.inlineCode;
}

// The visible part of a rect under the current clip, if any.
bool ClipRect(const std::vector<platform::Rect>& clips, const platform::Rect& in,
              platform::Rect* out) {
    *out = in;
    for (const platform::Rect& clip : clips) {
        const float left = (std::max)(out->x, clip.x);
        const float top = (std::max)(out->y, clip.y);
        const float right = (std::min)(out->x + out->width, clip.x + clip.width);
        const float bottom =
            (std::min)(out->y + out->height, clip.y + clip.height);
        if (!(right > left) || !(bottom > top)) return false;
        out->x = left;
        out->y = top;
        out->width = right - left;
        out->height = bottom - top;
    }
    return true;
}

void Flush(platform::RenderTarget& target,
           const std::vector<platform::Rect>& clips,
           const std::vector<Fragment>& group, float padX, float radius,
           const platform::Color& color) {
    for (size_t i = 0; i < group.size(); ++i) {
        const bool first = i == 0;
        const bool last = i + 1 == group.size();
        platform::Rect drawn = group[i].rect;
        drawn.x -= first ? padX : 0.0f;
        drawn.width += (first ? padX : 0.0f) + (last ? padX : 0.0f);
        platform::Rect visible;
        if (!ClipRect(clips, drawn, &visible)) continue;
        target.FillRoundedRectSides(visible, radius, color, first, last);
    }
}

}  // namespace

void PaintInlineCodeChips(platform::RenderTarget& target,
                          const display::DisplayList& list, float originX,
                          float originY, const LayoutMetrics& metrics,
                          const platform::Color& color, float zoom,
                          float bandTop, float bandBottom) {
    const float codeEm = metrics.codeFontSize * kPtToDip * zoom;
    const float padX = metrics.inlineCodePadX * codeEm;
    const float radius = kInlineCodeRadius * zoom;
    std::vector<platform::Rect> clips;
    std::vector<Fragment> group;

    const auto flush = [&]() {
        if (!group.empty()) Flush(target, clips, group, padX, radius, color);
        group.clear();
    };

    for (const display::Item& item : list.items) {
        // A clip boundary is a boundary for the group too. A cell's pill is
        // collected under the cell's clip and must be flushed there: the
        // next item's PushClip narrows the clip to the next cell, and a
        // flush under that clip tested the pill against a rectangle it was
        // never inside, so it was dropped and the cell drew no pill at all.
        if (const auto* clip = std::get_if<display::PushClip>(&item)) {
            flush();
            platform::Rect moved = clip->rect;
            moved.x += originX;
            moved.y += originY;
            clips.push_back(moved);
            continue;
        }
        if (std::holds_alternative<display::PopClip>(item)) {
            flush();
            if (!clips.empty()) clips.pop_back();
            continue;
        }
        const auto* text = std::get_if<display::Text>(&item);
        if (!text) continue;
        if (text->top + text->line.height < bandTop ||
            text->top > bandBottom) {
            flush();
            continue;
        }
        if (text->runSpans.size() != text->line.runs.size()) {
            flush();
            continue;
        }

        const float boxH = (std::min)(text->line.height, 1.6f * codeEm);
        const float boxTop = originY + text->top + (text->line.height - boxH) * 0.5f;
        for (size_t i = 0; i < text->line.runs.size(); ++i) {
            const platform::GlyphRun& run = text->line.runs[i];
            const display::SourceRange& span = text->runSpans[i];
            if (!span.inlineCode || span.length == 0 || run.text.empty()) {
                flush();
                continue;
            }
            const float right =
                i + 1 < text->line.runs.size() ? text->line.runs[i + 1].x
                                               : text->line.width;
            const float width = right - run.x;
            if (!(width > 0.0f)) continue;
            Fragment fragment;
            fragment.rect =
                platform::Rect{originX + text->x + run.x, boxTop, width, boxH};
            fragment.span = span;
            if (!group.empty() && !SameSpan(group.back().span, span)) flush();
            group.push_back(fragment);
        }
    }
    flush();
}

}  // namespace layout
