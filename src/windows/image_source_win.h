#pragma once

// The Windows image service behind platform::ImageSource.
//
// ImageHelper decodes a file or a URL with WIC and makes the Direct2D
// bitmap, and this class caches the size and the bitmap per URL, with the
// 128 entry cap the Windows renderer has always had. The decode is
// synchronous, so Prefetch is a no-op beyond what a size ask already did.

#include <string>
#include <vector>

// windows.h turns DrawText into DrawTextW through a macro, which rips the
// seam's virtual method out of the class. The seam is included with the
// macro gone, the way render_target_win.h does it.
#ifdef DrawText
#undef DrawText
#endif
#include "imagerender.h"

namespace platform {

class RenderTarget;

class WinImageSource final : public ImageSource {
public:
    ~WinImageSource() override;

    // The target the next bitmap is made on. A Direct2D bitmap belongs to
    // the device that made it, so the shell sets this before each frame.
    void SetTarget(RenderTarget* target) { target_ = target; }

    void SetBaseDir(const std::string& dir) override { baseDir_ = dir; }

    // Synchronous on this port: asking for the size decodes the header,
    // and Get makes the bitmap.
    void Prefetch(const std::string& source) override;

    bool GetSize(const std::string& source, float* width,
                 float* height) override;
    BitmapHandle Get(const std::string& source) override;

    // Releases every cached bitmap. The shell calls it when the device is
    // lost, because a bitmap made on the old device cannot be drawn on
    // the new one.
    void Clear();

private:
    struct Entry {
        std::string url;
        BitmapHandle bitmap = nullptr;
        float width = 0.0f;
        float height = 0.0f;
        bool sized = false;
    };

    Entry* Find(const std::string& url);
    Entry& Insert(const std::string& url);

    std::vector<Entry> entries_;
    std::string baseDir_;
    RenderTarget* target_ = nullptr;
};

}  // namespace platform
