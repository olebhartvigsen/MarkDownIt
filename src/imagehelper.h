#pragma once

// ImageHelper: loads image files via WIC and creates D2D bitmaps.
// Supports local file paths and embedded base64 data URIs.

#include <d2d1.h>
#include <string>

class ImageHelper {
public:
    // Load an image from a file path and create a D2D bitmap.
    // Returns nullptr on failure. Caller must Release the bitmap.
    static ID2D1Bitmap* LoadBitmapFromFile(ID2D1RenderTarget* rt,
        const std::u32string& url32, float maxWidth);
};
