#pragma once

// The three narrow interfaces the macOS port is written against.
//
// The Windows renderer talks to Direct2D and DirectWrite directly. The
// macOS renderer talks to these seams instead, which are deliberately
// small: they cover exactly what the markdown renderer needs and nothing
// else. That keeps the mac drawing code honest, because there is no way
// to reach for a platform API that is not in the seam.
//
// The seams are for the mac layer only. The Windows renderer keeps talking
// to Direct2D; retrofitting it onto the seams is an optional later
// refactor, not part of this port.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mac {

// ---------------------------------------------------------------------------
// TextEngine: measure and place text
// ---------------------------------------------------------------------------

// A font request, in the terms the shared theme uses.
struct FontSpec {
    // Font family name, for example "Helvetica Neue" or "Menlo".
    std::string family;
    // Size in points.
    float size = 14.0f;
    // Bold.
    bool bold = false;
    // Italic.
    bool italic = false;
    // Monospace, which the code theme needs.
    bool monospace = false;
};

// A run of text with one style, which is what Core Text draws.
struct GlyphRun {
    // The text of the run, UTF-8.
    std::string text;
    // The style of the run.
    FontSpec font;
    // The color of the run, as 0..1 floats in RGBA order.
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    // The horizontal offset of the run inside its line, in points.
    float x = 0.0f;
    // The run is a link, drawn with an underline under the glyphs.
    bool underline = false;
    // The run is struck through.
    bool strike = false;
};

// One laid out line of text.
struct TextLine {
    // The width of the line, in points.
    float width = 0.0f;
    // The height of the line, in points.
    float height = 0.0f;
    // The distance from the top of the line to the baseline, in points.
    float ascent = 0.0f;
    // The runs of the line, in order.
    std::vector<GlyphRun> runs;
};

// A rectangle in points.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Measures and places text.
//
// MeasureWrapped lays the runs out in a column of the given width and
// returns the lines. The runs keep their order, so a caller can map a
// character offset back to the run that holds it.
//
// HitTestOffset answers which character offset a horizontal position on a
// line belongs to, which is what the caret and the selection need.
//
// OffsetToRect answers where a character offset sits on a line, which is
// what the caret and the selection rectangles need.
//
// The offsets are UTF-8 byte offsets into the concatenated run text of the
// line, which is the same unit the document uses.
class TextEngine {
public:
    virtual ~TextEngine() = default;

    // Called before a full layout pass, so the engine can drop the
    // measurements of the pass before it. The default keeps everything;
    // an engine that keeps its lines alive for hit testing overrides
    // it.
    virtual void BeginLayout() {}

    // Lays the runs out. The runs are measured and broken into lines that
    // fit maxWidth. A maxWidth of zero means one unbounded line.
    virtual std::vector<TextLine> MeasureWrapped(
        const std::vector<GlyphRun>& runs,
        float maxWidth,
        float lineSpacing) = 0;

    // The character offset on the line at the given x. The x is relative
    // to the start of the line. Returns the offset of the closest
    // character boundary, and sets outTrailing to true when the position
    // is in the trailing half of a character.
    virtual uint32_t HitTestOffset(const TextLine& line, float x,
                                   bool* outTrailing) const = 0;

    // The rectangle a character offset occupies on the line.
    virtual Rect OffsetToRect(const TextLine& line, uint32_t offset) const = 0;

    // The height of a line of the given font, which the editor needs for
    // the caret and for line spacing.
    virtual float LineHeight(const FontSpec& font) const = 0;
};

// ---------------------------------------------------------------------------
// RenderTarget: draw
// ---------------------------------------------------------------------------

// A color as 0..1 floats in RGBA order.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

// A point in points.
struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

// An image the target can draw.
//
// The handle is opaque to the seam: a decoded bitmap on one platform, a
// vector document on another. The target owns the drawing, so the
// renderer never has to know what is behind the handle.
using BitmapHandle = void*;

// The kinds of image a target can be handed.
enum class ImageKind {
    Raster,  // a decoded bitmap, drawn as it is
    Vector,  // a vector document, scaled into the rectangle
};

// What a BitmapHandle points at.
//
// The seam passes the handle around as an opaque pointer, so the mac
// layer keeps the concrete type here and both sides agree on it.
struct Image {
    ImageKind kind = ImageKind::Raster;
    // The platform image: a CGImageRef for a raster image, an NSImage
    // for a vector one.
    void* handle = nullptr;
    // The natural size of the image in points.
    float width = 0.0f;
    float height = 0.0f;
};

// Draws the shapes and text the markdown renderer needs.
//
// Every method works in points. The caller scales the context once, so
// the renderer never deals with device pixels.
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    // Fills a rectangle.
    virtual void FillRect(const Rect& rect, const Color& color) = 0;

    // Fills a rectangle with rounded corners. The radius is in points.
    virtual void FillRoundedRect(const Rect& rect, float radius,
                                 const Color& color) = 0;

    // Strokes a rectangle with a one point line.
    virtual void StrokeRect(const Rect& rect, const Color& color,
                            float lineWidth) = 0;

    // Strokes a rounded rectangle, the outline of the geometry
    // FillRoundedRect fills. The radius is in points.
    virtual void StrokeRoundedRect(const Rect& rect, float radius,
                                   const Color& color, float lineWidth) = 0;

    // Strokes a straight line.
    virtual void StrokeLine(float x0, float y0, float x1, float y1,
                            const Color& color, float lineWidth) = 0;

    // Draws a line of text at the given position. The position is the
    // left edge of the baseline.
    virtual void DrawText(const std::string& utf8, const FontSpec& font,
                          float x, float baselineY, const Color& color) = 0;

    // Draws an image into the rectangle, scaled to fit it. The image is
    // scaled to fit, so the aspect ratio is kept and the image is
    // centred in the rectangle.
    virtual void DrawBitmap(BitmapHandle image, const Rect& rect) = 0;

    // Fills an ellipse that fits the rectangle.
    virtual void FillEllipse(const Rect& rect, const Color& color) = 0;

    // Strokes an ellipse that fits the rectangle.
    virtual void StrokeEllipse(const Rect& rect, const Color& color,
                               float lineWidth) = 0;

    // Fills a closed polygon. The points are in the order they are
    // given, and the last point is joined back to the first.
    virtual void FillPolygon(const std::vector<Point>& points,
                             const Color& color) = 0;

    // Strokes a polygon. The points are in the order they are given,
    // and the last point is joined back to the first when the shape is
    // closed.
    virtual void StrokePolygon(const std::vector<Point>& points,
                               const Color& color, float lineWidth,
                               bool closed) = 0;

    // Fills a pie slice of the ellipse that fits the rectangle.
    //
    // The angles are in degrees, measured clockwise from the three
    // o'clock position, the same way the pie chart data names them.
    virtual void FillPieSlice(const Rect& rect, float startAngle,
                              float endAngle, const Color& color) = 0;

    // Pushes a clip rectangle. Every PushClip must be matched by a
    // PopClip.
    virtual void PushClip(const Rect& rect) = 0;
    virtual void PopClip() = 0;
};

// ---------------------------------------------------------------------------
// AppShell: the window and the platform
// ---------------------------------------------------------------------------

// The window and the services the app needs from the system.
//
// The mac implementation wraps NSWindow, NSApplication, NSPasteboard and
// the rest. The interface exists so the app logic can ask for a service
// without knowing it is on macOS, and so the behaviour can be tested.
class AppShell {
public:
    virtual ~AppShell() = default;

    // The size of the window's content area, in points.
    virtual void GetContentSize(float* outWidth, float* outHeight) const = 0;

    // The scale factor of the backing store, which is 1.0 on a non Retina
    // display and 2.0 on a Retina one.
    virtual float GetScaleFactor() const = 0;

    // Shows the window and makes it key.
    virtual void ShowWindow() = 0;

    // Requests a redraw of the content area.
    virtual void InvalidateContent() = 0;

    // Starts a repeating timer. The callback runs on the main thread.
    // Returns a handle that StopTimer takes.
    virtual uint32_t StartTimer(double intervalSeconds,
                                std::function<void()> callback) = 0;
    virtual void StopTimer(uint32_t handle) = 0;

    // Shows the system context menu at a point in the content area.
    virtual void ShowContextMenu(const std::vector<std::string>& items,
                                 float x, float y) = 0;

    // The text on the clipboard, and the ability to set it.
    virtual std::string GetClipboardText() = 0;
    virtual void SetClipboardText(const std::string& text) = 0;

    // Opens a URL in the user's browser.
    virtual void OpenInBrowser(const std::string& url) = 0;

    // The user's home directory, where the settings and the logs live.
    virtual std::string GetHomeDirectory() const = 0;

    // The path of the file the user opened, empty when nothing is open.
    virtual std::string GetOpenFilePath() const = 0;
};

}  // namespace mac
