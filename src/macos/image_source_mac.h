#pragma once

// The renderer's image service on macOS: the asynchronous ImageCache,
// whose NSURLSession fetch answers the renderer's prefetch and whose
// decoded bitmap or natural size answers the paint and the layout.

#include "imagerender.h"
#include "image_mac.h"

namespace platform {

class MacImageSource final : public ImageSource {
public:
    void SetBaseDir(const std::string& dir) override {
        cache_.SetBaseDir(dir);
    }
    void Prefetch(const std::string& source) override {
        cache_.Prefetch(source);
    }
    bool GetSize(const std::string& source, float* width,
                 float* height) override {
        return cache_.GetSize(source, width, height);
    }
    BitmapHandle Get(const std::string& source) override {
        return cache_.Get(source);
    }

private:
    ImageCache cache_;
};

}  // namespace platform
