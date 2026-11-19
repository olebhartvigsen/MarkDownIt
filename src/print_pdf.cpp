// Printing: the print bands written as a PDF, through pdfio and the
// RenderTarget seam. The geometry is one rule: a seam unit is 0.75 of a
// point, the page's top left corner is the PDF MediaBox's bottom left corner
// mirrored, and this file is the only place that conversion is done for a
// saved print. The cut, the placement and the page items are the shared ones
// (src/layout/print_pages.h), and the white sheet and the fonts are laid down
// the way the office exporter lays them (src/office/pdf_export.cpp).

#include "print_pdf.h"

#include "pdfio.h"
#include "pdfio-content.h"

#include "layout/display_player.h"
#include "office/pdf_export.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// theme.h hands the Windows build a <d2d1.h>, and windows.h leaves a
// DrawText macro behind, which would rename this file's seam method to
// DrawTextW and leave the subclass abstract (C3668/C2259). The macro
// brings nothing this file uses, so it goes away before the class below.
#ifdef DrawText
#undef DrawText
#endif

namespace print_pdf {
namespace {

// One seam unit (1/96 inch) is 0.75 of a point (1/72 inch), the same
// conversion the mac driver makes once at its boundary.
constexpr double kSeamToPoints = 72.0 / 96.0;

// The PDF version the office exporter writes; the print writer matches it,
// so a reader cannot tell which feature produced its file.
const char kPdfVersion[] = "1.7";

// The standard 14 fonts, no embedding: Helvetica for prose, Courier for code,
// and the bold and italic flags pick the variant. The same names the office
// exporter uses, in the same order the resource names below number.
constexpr int kFaceCount = 8;
const char* const kFaceNames[kFaceCount] = {
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
    "Courier",    "Courier-Bold",  "Courier-Oblique",   "Courier-BoldOblique",
};
const char* const kFaceResources[kFaceCount] = {"F1", "F2", "F3", "F4",
                                                 "F5", "F6", "F7", "F8"};

int FaceFor(const platform::FontSpec& font) {
    return (font.monospace ? 4 : 0) + (font.bold ? 1 : 0) +
           (font.italic ? 2 : 0);
}

struct OutputBuffer {
    std::string* bytes = nullptr;
};

ssize_t WriteOutput(void* ctx, const void* data, size_t datalen) {
    OutputBuffer* buffer = static_cast<OutputBuffer*>(ctx);
    if (buffer == nullptr || buffer->bytes == nullptr) return -1;
    if (data == nullptr && datalen > 0) return -1;
    buffer->bytes->append(static_cast<const char*>(data), datalen);
    return static_cast<ssize_t>(datalen);
}

bool CollectMessage(pdfio_file_t*, const char* message, void* data) {
    if (data != nullptr && message != nullptr)
        static_cast<std::string*>(data)->append(message).append("\n");
    return true;
}

// The RenderTarget seam over one page's pdfio stream.
//
// A PDF page speaks points, bottom up, while the display list speaks seam
// units, top down. Every operator's coordinates are mapped here, once, by
// one rule:
//
//     x_pt = (place.x + x) * kSeamToPoints
//     y_pt = page_height_pt - (place.y + y) * kSeamToPoints
//
// No content transform is emitted. A flipped CTM in a PDF stream mirrors
// glyphs, so text must never be drawn under one; writing the mapped
// coordinates of every operator itself keeps text upright without a second
// rule for text space.
//
// Alpha has no operator here: the page has already been laid white, so a
// translucent wash is composed onto white in advance, the honest answer for
// a paper medium.
class PdfTarget final : public platform::RenderTarget {
public:
    PdfTarget(pdfio_stream_t* stream, double pageHeightPt,
              const layout::PagePlacement& place, size_t* dropped)
        : stream_(stream), pageHeightPt_(pageHeightPt), place_(place),
          dropped_(dropped) {}

    bool ok() const { return ok_; }

    // RenderTarget ------------------------------------------------------

    void FillRect(const platform::Rect& rect,
                  const platform::Color& color) override {
        if (!(rect.width > 0.0f) || !(rect.height > 0.0f)) return;
        double rgb[3];
        Blend(color, rgb);
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentSetFillColorDeviceRGB(stream_, rgb[0], rgb[1],
                                                rgb[2]) &&
              pdfioContentPathRect(stream_, MapX(rect.x),
                                  MapY(rect.y + rect.height), Len(rect.width),
                                  Len(rect.height)) &&
              pdfioContentFill(stream_, false) &&
              pdfioContentRestore(stream_) && ok_;
    }

    void FillRoundedRect(const platform::Rect& rect, float radius,
                         const platform::Color& color) override {
        FillRoundedSides(rect, radius, color, true, true);
    }

    void FillRoundedRectSides(const platform::Rect& rect, float radius,
                              const platform::Color& color, bool roundLeft,
                              bool roundRight) override {
        FillRoundedSides(rect, radius, color, roundLeft, roundRight);
    }

    void StrokeRect(const platform::Rect& rect, const platform::Color& color,
                    float lineWidth) override {
        if (!(rect.width >= 0.0f) || !(rect.height >= 0.0f)) return;
        double rgb[3];
        Blend(color, rgb);
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentSetStrokeColorDeviceRGB(stream_, rgb[0], rgb[1],
                                                  rgb[2]) &&
              pdfioContentSetLineWidth(stream_, Pen(lineWidth)) &&
              pdfioContentPathRect(stream_, MapX(rect.x),
                                   MapY(rect.y + rect.height),
                                   Len(rect.width), Len(rect.height)) &&
              pdfioContentStroke(stream_) &&
              pdfioContentRestore(stream_) && ok_;
    }

    void StrokeRoundedRect(const platform::Rect& rect, float radius,
                           const platform::Color& color,
                           float lineWidth) override {
        StrokeRoundedSides(rect, radius, color, lineWidth, true, true);
    }

    void StrokeLine(float x0, float y0, float x1, float y1,
                    const platform::Color& color, float lineWidth) override {
        double rgb[3];
        Blend(color, rgb);
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentSetStrokeColorDeviceRGB(stream_, rgb[0], rgb[1],
                                                  rgb[2]) &&
              pdfioContentSetLineWidth(stream_, Pen(lineWidth)) &&
              pdfioContentPathMoveTo(stream_, MapX(x0), MapY(y0)) &&
              pdfioContentPathLineTo(stream_, MapX(x1), MapY(y1)) &&
              pdfioContentStroke(stream_) &&
              pdfioContentRestore(stream_) && ok_;
    }

    void DrawText(const std::string& utf8, const platform::FontSpec& font,
                  float x, float baselineY,
                  const platform::Color& color) override {
        if (utf8.empty()) return;
        size_t dropped = 0;
        const std::string clean = office::CleanWinAnsiText(utf8, dropped);
        if (dropped_ != nullptr && dropped > 0) *dropped_ += dropped;
        if (clean.empty()) return;
        double rgb[3];
        Blend(color, rgb);
        const double size = font.size > 0.0f ? Len(font.size) : 12.0;
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentSetFillColorDeviceRGB(stream_, rgb[0], rgb[1],
                                                rgb[2]) &&
              pdfioContentTextBegin(stream_) &&
              pdfioContentSetTextFont(stream_,
                                      kFaceResources[FaceFor(font)], size) &&
              pdfioContentTextMoveTo(stream_, MapX(x), MapY(baselineY)) &&
              pdfioContentTextShow(stream_, false, clean.c_str()) &&
              pdfioContentTextEnd(stream_) &&
              pdfioContentRestore(stream_) && ok_;
    }

    void DrawBitmap(platform::BitmapHandle, const platform::Rect&) override {
        // The print paths replay without a resolver, so the player hands
        // this target no image; any handle would be a Windows or mac image
        // type this file could not mean anything by anyway.
    }

    void FillEllipse(const platform::Rect& rect,
                     const platform::Color& color) override {
        if (!EllipsePath(rect)) return;
        FillCurrent(color);
    }

    void StrokeEllipse(const platform::Rect& rect, const platform::Color& color,
                       float lineWidth) override {
        if (!EllipsePath(rect)) return;
        StrokeCurrent(color, lineWidth);
    }

    void FillPolygon(const std::vector<platform::Point>& points,
                     const platform::Color& color) override {
        if (!PolygonPath(points, true)) return;
        FillCurrent(color);
    }

    void StrokePolygon(const std::vector<platform::Point>& points,
                       const platform::Color& color, float lineWidth,
                       bool closed) override {
        if (!PolygonPath(points, closed)) return;
        StrokeCurrent(color, lineWidth);
    }

    void FillPieSlice(const platform::Rect& rect, float startAngle,
                      float endAngle, const platform::Color& color) override {
        // No print band carries a pie today (the Mermaid engine emits no
        // display items for paper), but the seam is a promise: the slice is
        // filled as a wedge with a segmented arc, honestly and without a
        // mirrored glyph problem, since everything is polygons.
        const std::vector<platform::Point> wedge = WedgePoints(
            rect, startAngle, endAngle);
        if (wedge.size() < 3) return;
        if (!PolygonPath(wedge, true)) return;
        FillCurrent(color);
    }

    void PushClip(const platform::Rect& rect) override {
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentPathRect(stream_, MapX(rect.x),
                                   MapY(rect.y + rect.height),
                                   Len(rect.width), Len(rect.height)) &&
              pdfioContentClip(stream_, false) && ok_;
    }

    void PopClip() override {
        ok_ = pdfioContentRestore(stream_) && ok_;
    }

private:
    // Coordinates -------------------------------------------------------

    double MapX(float x) const {
        return (place_.x + static_cast<double>(x)) * kSeamToPoints;
    }
    double MapY(float y) const {
        return pageHeightPt_ - (place_.y + static_cast<double>(y)) *
                                   kSeamToPoints;
    }
    double Len(float v) const { return static_cast<double>(v) * kSeamToPoints; }
    double Pen(float lineWidth) const {
        return lineWidth > 0.0f ? Len(lineWidth) : 0.5;
    }

    // A color composed onto the white page: PDF content has no alpha.
    void Blend(const platform::Color& color, double* out) const {
        const double a = color.a < 0.0f ? 0.0
                        : (color.a > 1.0f ? 1.0
                                          : static_cast<double>(color.a));
        const double rgb[3] = {static_cast<double>(color.r),
                               static_cast<double>(color.g),
                               static_cast<double>(color.b)};
        for (int i = 0; i < 3; ++i) {
            const double c = rgb[i] < 0.0 ? 0.0 : (rgb[i] > 1.0 ? 1.0 : rgb[i]);
            out[i] = c * a + (1.0 - a);
        }
    }

    // Paths ---------------------------------------------------------------

    bool RoundedPath(const platform::Rect& rect, float radius, bool roundLeft,
                     bool roundRight) {
        if (!(rect.width > 0.0f) || !(rect.height > 0.0f)) return false;
        const double left = MapX(rect.x);
        const double right = MapX(rect.x + rect.width);
        // In page space the smaller mapped y is the top edge.
        const double top = MapY(rect.y + rect.height);
        const double bottom = MapY(rect.y);
        double r = Len(radius < 0.0f ? 0.0f : radius);
        const double capH = (bottom - top) / 2.0;
        const double capW = (right - left) / 2.0;
        if (r > capH) r = capH;
        if (r > capW) r = capW;
        const double k = r * 0.5522847498;

        // Left side rounds both its corners, right side both of its; a full
        // round asks for all four. The walk is clockwise from the start of
        // the top edge, and every corner is the one quarter-circle Bezier of
        // a rounded rectangle, its control points pulled inside along the
        // edges by k.
        const double rLeft = roundLeft ? r : 0.0;
        const double rRight = roundRight ? r : 0.0;

        if (!pdfioContentPathMoveTo(stream_, left + rLeft, top) ||
            !pdfioContentPathLineTo(stream_, right - rRight, top))
            return false;
        if (roundRight &&
            !pdfioContentPathCurve(stream_, right - rRight + k, top, right,
                                   top + rRight - k, right, top + rRight))
            return false;
        if (!pdfioContentPathLineTo(stream_, right, bottom - rRight))
            return false;
        if (roundRight &&
            !pdfioContentPathCurve(stream_, right, bottom - rRight + k,
                                   right - rRight + k, bottom,
                                   right - rRight, bottom))
            return false;
        if (!pdfioContentPathLineTo(stream_, left + rLeft, bottom))
            return false;
        if (roundLeft &&
            !pdfioContentPathCurve(stream_, left + rLeft - k, bottom, left,
                                   bottom - rLeft + k, left,
                                   bottom - rLeft))
            return false;
        if (!pdfioContentPathLineTo(stream_, left, top + rLeft))
            return false;
        if (roundLeft &&
            !pdfioContentPathCurve(stream_, left, top + rLeft - k,
                                   left + rLeft - k, top, left + rLeft, top))
            return false;
        return pdfioContentPathClose(stream_);
    }

    void FillRoundedSides(const platform::Rect& rect, float radius,
                          const platform::Color& color, bool roundLeft,
                          bool roundRight) {
        if (!RoundedPath(rect, radius, roundLeft, roundRight)) return;
        FillCurrent(color);
    }

    void StrokeRoundedSides(const platform::Rect& rect, float radius,
                            const platform::Color& color, float lineWidth,
                            bool roundLeft, bool roundRight) {
        if (!RoundedPath(rect, radius, roundLeft, roundRight)) return;
        StrokeCurrent(color, lineWidth);
    }

    // An oval inscribed in the rect, as the four Bezier arcs of the office
    // bullet: the mapped center with mapped radii.
    bool EllipsePath(const platform::Rect& rect) {
        if (!(rect.width > 0.0f) || !(rect.height > 0.0f)) return false;
        const double cx = MapX(rect.x + rect.width * 0.5f);
        const double cy = MapY(rect.y + rect.height * 0.5f);
        const double rx = Len(rect.width) / 2.0;
        const double ry = Len(rect.height) / 2.0;
        if (!(rx > 0.0) || !(ry > 0.0)) return false;
        const double kx = rx * 0.5522847498;
        const double ky = ry * 0.5522847498;
        return pdfioContentPathMoveTo(stream_, cx + rx, cy) &&
               pdfioContentPathCurve(stream_, cx + rx, cy + ky, cx + kx,
                                     cy + ry, cx, cy + ry) &&
               pdfioContentPathCurve(stream_, cx - kx, cy + ry, cx - rx,
                                     cy + ky, cx - rx, cy) &&
               pdfioContentPathCurve(stream_, cx - rx, cy - ky, cx - kx,
                                     cy - ry, cx, cy - ry) &&
               pdfioContentPathCurve(stream_, cx + kx, cy - ry, cx + rx,
                                     cy - ky, cx + rx, cy) &&
               pdfioContentPathClose(stream_);
    }

    bool PolygonPath(const std::vector<platform::Point>& points,
                     bool closed) {
        if (points.size() < 2) return false;
        if (!pdfioContentPathMoveTo(stream_, MapX(points[0].x),
                                    MapY(points[0].y)))
            return false;
        for (size_t i = 1; i < points.size(); ++i) {
            if (!pdfioContentPathLineTo(stream_, MapX(points[i].x),
                                        MapY(points[i].y)))
                return false;
        }
        return !closed || pdfioContentPathClose(stream_);
    }

    // Fill and stroke the path that is on the stream right now.
    void FillCurrent(const platform::Color& color) {
        double rgb[3];
        Blend(color, rgb);
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentSetFillColorDeviceRGB(stream_, rgb[0], rgb[1],
                                                rgb[2]) &&
              pdfioContentFill(stream_, false) &&
              pdfioContentRestore(stream_) && ok_;
    }

    void StrokeCurrent(const platform::Color& color, float lineWidth) {
        double rgb[3];
        Blend(color, rgb);
        ok_ = pdfioContentSave(stream_) &&
              pdfioContentSetStrokeColorDeviceRGB(stream_, rgb[0], rgb[1],
                                                  rgb[2]) &&
              pdfioContentSetLineWidth(stream_, Pen(lineWidth)) &&
              pdfioContentStroke(stream_) &&
              pdfioContentRestore(stream_) && ok_;
    }

    // A pie wedge as a polygon: the center, then the arc from startAngle to
    // endAngle in eighteen steps, in the clockwise sense the seam names.
    // The points are laid out in seam space first and mapped by the polygon
    // rule, so both spaces agree without a second convention.
    std::vector<platform::Point> WedgePoints(const platform::Rect& rect,
                                            float startAngle,
                                            float endAngle) const {
        std::vector<platform::Point> points;
        const platform::Point center{rect.x + rect.width * 0.5f,
                                     rect.y + rect.height * 0.5f};
        const float rx = rect.width * 0.5f;
        const float ry = rect.height * 0.5f;
        if (!(rx > 0.0f) || !(ry > 0.0f)) return points;
        float span = endAngle - startAngle;
        while (span < 0.0f) span += 360.0f;
        points.push_back(center);
        const int steps = 18;
        for (int i = 0; i <= steps; ++i) {
            const float a = startAngle + span * i / steps;
            const float rad = a * 3.14159265358979f / 180.0f;
            points.push_back(platform::Point{
                center.x + rx * std::cos(rad),
                center.y + ry * std::sin(rad)});
        }
        return points;
    }

    pdfio_stream_t* stream_ = nullptr;
    double pageHeightPt_ = 0.0;
    layout::PagePlacement place_{};
    size_t* dropped_ = nullptr;
    bool ok_ = true;
};

}  // namespace

std::string WriteBytes(const layout::DocumentLayout& laid,
                       const layout::PrintPage& page,
                       const std::vector<layout::PrintBand>& bands,
                       WriteReport* report) {
    if (report != nullptr) *report = WriteReport();
    if (bands.empty()) return std::string();

    std::string out;
    std::string messages;
    OutputBuffer buffer;
    buffer.bytes = &out;

    pdfio_rect_t media;
    media.x1 = 0.0;
    media.y1 = 0.0;
    media.x2 = static_cast<double>(page.width) * kSeamToPoints;
    media.y2 = static_cast<double>(page.height) * kSeamToPoints;
    if (!(media.x2 > 0.0) || !(media.y2 > 0.0)) return std::string();

    pdfio_file_t* pdf = pdfioFileCreateOutput(
        WriteOutput, &buffer, kPdfVersion, &media, nullptr,
        CollectMessage, &messages);
    if (pdf == nullptr) return std::string();

    pdfio_obj_t* faces[kFaceCount] = {nullptr};
    bool ok = true;
    for (int i = 0; i < kFaceCount && ok; ++i) {
        faces[i] = pdfioFileCreateFontObjFromBase(pdf, kFaceNames[i]);
        if (faces[i] == nullptr) ok = false;
    }

    size_t dropped = 0;
    for (size_t i = 0; i < bands.size() && ok; ++i) {
        pdfio_dict_t* dict = pdfioDictCreate(pdf);
        if (dict == nullptr) {
            ok = false;
            break;
        }
        for (int f = 0; f < kFaceCount && ok; ++f) {
            if (!pdfioPageDictAddFont(dict, kFaceResources[f], faces[f]))
                ok = false;
        }
        if (!ok) break;
        pdfio_stream_t* stream = pdfioFileCreatePage(pdf, dict);
        if (stream == nullptr) {
            ok = false;
            break;
        }

        // The sheet itself, edge to edge: a PDF reader may not lay a page
        // background down at all, and a dark reading mode on screen must
        // not save a dark page here. It is white whatever the page carries.
        ok = pdfioContentSave(stream) &&
             pdfioContentSetFillColorDeviceRGB(stream, 1.0, 1.0, 1.0) &&
             pdfioContentPathRect(stream, 0.0, 0.0, media.x2, media.y2) &&
             pdfioContentFill(stream, false) &&
             pdfioContentRestore(stream);

        // The band's own placement with its slice replayed, the same rule
        // the Windows driver maps with and the mac panel draws through.
        if (ok) {
            PdfTarget target(stream, media.y2,
                             layout::PlacementForBand(page, bands[i]),
                             &dropped);
            display::Play(layout::PageItems(laid, bands[i]), &target);
            ok = target.ok();
        }

        ok = pdfioStreamClose(stream) && ok;
        if (!ok) break;
    }

    const bool closed = pdfioFileClose(pdf);
    if (!ok || !closed) {
        out.clear();
        return std::string();
    }

    if (report != nullptr) {
        report->pages = static_cast<int>(bands.size());
        report->droppedGlyphs = dropped;
    }
    return out;
}

}  // namespace print_pdf
