#pragma once

// ImageHelper: loads image files via WIC and creates D2D bitmaps.
// Supports local file paths and HTTP/HTTPS URLs.

#include <d2d1.h>
#include <string>

namespace platform {
// The seam's drawing target. Declared here, not included, so this Windows
// header keeps its own include order; the seam's BitmapHandle is the opaque
// void* the methods below pass.
class RenderTarget;
}  // namespace platform

class ImageHelper {
public:
    // Load an image from a URL (local path or HTTP/HTTPS). A local path
    // that is not already absolute is resolved against `baseDir` (the
    // open .md file's own directory, never the process's working
    // directory); pass an empty `baseDir` to use the path exactly as
    // written, the behavior before this parameter existed.
    // Returns nullptr on failure. Caller must Release the bitmap.
    static ID2D1Bitmap* LoadBitmapFromUrl(ID2D1RenderTarget* rt,
        const std::string& url, float maxWidth,
        const std::string& baseDir = std::string());

    // The same load, asked through the seam's target, which is what a
    // painter holds. The result is the seam's opaque BitmapHandle (void*),
    // so a shared-level cache can keep it without naming Direct2D; the
    // concrete bitmap never leaves this module. Answers null when the
    // target is not this platform's Direct2D adapter.
    static void* LoadBitmapForTarget(platform::RenderTarget& target,
        const std::string& url, float maxWidth,
        const std::string& baseDir = std::string());

    // Release a bitmap the seam handed out. A null handle is ignored.
    static void ReleaseBitmap(void* bitmap);

    // The natural size of a bitmap the seam handed out, in points. Answers
    // zero for a null handle.
    static void BitmapSize(void* bitmap, float* w, float* h);

    // Load an SVG file as a UTF-8 text string from a local path or
    // HTTP/HTTPS URL. See LoadBitmapFromUrl for what `baseDir` does.
    // Returns empty string on failure.
    static std::string LoadSvgText(const std::string& url8,
        const std::string& baseDir = std::string());

    // Intrinsic size of a local image file, read from its header without
    // decoding the pixels. Measure needs the size before the draw pass has
    // run, and it has no render target to create a bitmap with. Returns
    // false for a remote URL (not fetched yet) or an unreadable file. See
    // LoadBitmapFromUrl for what `baseDir` does.
    static bool LoadImageSize(const std::string& url8, float* w, float* h,
        const std::string& baseDir = std::string());
};
