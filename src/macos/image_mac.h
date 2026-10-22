#pragma once

// Image loading for the macOS build.
//
// The Windows build loads images with WIC and fetches remote ones with
// WinHTTP. macOS has the same two jobs: ImageIO reads a local file and
// NSURLSession fetches a remote one. The byte cap is the same 32 MB the
// Windows loader uses, so a document cannot make the app pull an
// unbounded amount of data down.
//
// A remote image is fetched in the background and drawn once it has
// arrived, so a slow network never blocks the window. The first ask for
// a remote image returns nothing and the layout reserves a placeholder
// until the image shows up.

#include <cstddef>
#include <string>
#include <vector>

#include "seams.h"

namespace mac {

// The largest image the loader accepts, in bytes.
constexpr size_t kMaxImageBytes = 32u * 1024u * 1024u;

// The number of images the cache keeps. The Windows renderer keeps the
// same number, so the two behave the same on a document with many
// images.
constexpr size_t kMaxCachedImages = 128;

// True when the source names a remote resource.
bool IsRemoteUrl(const std::string& source);

// True when the source names a vector document.
//
// The Windows renderer decides the same way: by the file extension, or
// by the data URI prefix.
bool IsVectorSource(const std::string& source);

// The image cache.
//
// A local image is read the first time it is asked for. A remote one is
// fetched in the background, so the first ask returns nothing and a
// later one returns the image.
class ImageCache {
public:
    ImageCache();
    ~ImageCache();

    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // The image for the source, or null when it is missing, too large,
    // or a remote image that has not arrived yet.
    BitmapHandle Get(const std::string& source);

    // The size of the image in points, so the layout can reserve the
    // right height before the image has arrived. Returns false when the
    // size is not known yet.
    bool GetSize(const std::string& source, float* outWidth, float* outHeight);

    // Starts the fetch of a remote image. The layout calls this as soon
    // as it sees the image, so the fetch begins before the first paint.
    void Prefetch(const std::string& source);

private:
    // The entry the cache keeps for one source.
    struct Entry;

    // The entry for the source, or null when there is none.
    Entry* Find(const std::string& source);

    // The cached entries.
    std::vector<Entry*> entries_;
};

}  // namespace mac
