#pragma once

// ImageHelper: loads image files via WIC and creates D2D bitmaps.
// Supports local file paths and HTTP/HTTPS URLs.

#include <d2d1.h>
#include <string>

class ImageHelper {
public:
    // Load an image from a URL (local path or HTTP/HTTPS).
    // Returns nullptr on failure. Caller must Release the bitmap.
    static ID2D1Bitmap* LoadBitmapFromUrl(ID2D1RenderTarget* rt,
        const std::string& url, float maxWidth);
};
