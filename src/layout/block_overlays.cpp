#include "layout/block_overlays.h"

namespace layout {

void PaintRegionWashes(platform::RenderTarget& target,
                       platform::TextRangeSource& source,
                       const OverlayRegion& region, const PaintBand& band,
                       const std::vector<TextMatch>& markers,
                       const std::vector<TextMatch>& matches, int current,
                       const platform::Color& markerFill,
                       const platform::Color& markerLine,
                       const platform::Color& matchFill,
                       const platform::Color& currentMatch) {
    if (!region.indexToSrc || region.indexToSrc->empty()) return;
    if (region.indexLen == 0) return;

    BlockTextMap map;
    map.u16ToSrc = region.indexToSrc;
    map.u16Len = region.indexLen;
    map.blockStart = region.blockStart;
    map.blockEnd = region.blockEnd;

    // Markers first: the mark is under the search fill where a match sits on
    // a mark, the order the frame has always painted.
    if (!markers.empty()) {
        PaintMarkerHighlights(target, source, map, band, markers, markerFill,
                              markerLine);
    }
    if (!matches.empty()) {
        PaintMatchHighlights(target, source, map, band, matches, current,
                             matchFill, currentMatch);
    }
}

}  // namespace layout
