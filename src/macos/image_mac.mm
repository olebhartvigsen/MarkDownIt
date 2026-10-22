// Image loading for the macOS build.
//
// A local image is read with ImageIO. A remote one is fetched with
// NSURLSession in the background, so a slow network never blocks the
// window. The byte cap is the same 32 MB the Windows loader uses.
//
// A vector document (an SVG) is handed to NSImage, which renders SVG
// from macOS 14 on. On an older system the image is skipped and the
// layout keeps the placeholder.

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>

#include "image_mac.h"

#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace mac {

// The entry the cache keeps for one source.
struct ImageCache::Entry {
    // The source, as it was asked for.
    std::string source;
    // The decoded image, or null when it is missing or still on its
    // way.
    Image image;
    // True when the source was tried and could not be read. Remembering
    // the failure is what stops a dead URL from being fetched again on
    // the next repaint.
    bool failed = false;
    // True when a fetch is running for this source.
    bool fetching = false;
};

namespace {

// The lock that guards the cache. The session's completion handler runs
// on its own queue, so the cache is touched from more than one thread.
std::mutex& CacheMutex() {
    static std::mutex mutex;
    return mutex;
}

// The session that fetches the remote images.
NSURLSession* SharedSession() {
    static NSURLSession* session = [NSURLSession sharedSession];
    return session;
}

// The data of a local file, or an empty string when the file cannot be
// read or is over the cap.
std::string ReadLocalFile(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return std::string();
    }

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    if (size < 0 || static_cast<size_t>(size) > kMaxImageBytes) {
        std::fclose(file);
        return std::string();
    }
    std::fseek(file, 0, SEEK_SET);

    std::string data;
    data.resize(static_cast<size_t>(size));
    const size_t read = size > 0 ? std::fread(&data[0], 1, data.size(), file) : 0;
    std::fclose(file);
    data.resize(read);
    return data;
}

// Decodes the data into a raster image. Returns false when the data is
// not an image the system can read.
bool DecodeRaster(const std::string& data, Image* out) {
    if (data.empty()) {
        return false;
    }
    CFDataRef cfData = CFDataCreate(nullptr,
                                    reinterpret_cast<const UInt8*>(data.data()),
                                    static_cast<CFIndex>(data.size()));
    if (!cfData) {
        return false;
    }
    CGImageSourceRef source = CGImageSourceCreateWithData(cfData, nullptr);
    CFRelease(cfData);
    if (!source) {
        return false;
    }

    CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!cgImage) {
        return false;
    }

    out->kind = ImageKind::Raster;
    out->handle = cgImage;
    out->width = static_cast<float>(CGImageGetWidth(cgImage));
    out->height = static_cast<float>(CGImageGetHeight(cgImage));
    return true;
}

// Decodes the data into a vector image. NSImage renders SVG from
// macOS 14 on, so on an older system there is no image.
bool DecodeVector(const std::string& data, Image* out) {
    if (data.empty()) {
        return false;
    }
    if (@available(macOS 14.0, *)) {
        NSData* nsData = [NSData dataWithBytes:data.data()
                                        length:data.size()];
        NSImage* image = [[NSImage alloc] initWithData:nsData];
        if (!image || ![image isValid]) {
            return false;
        }
        const NSSize size = image.size;
        out->kind = ImageKind::Vector;
        // The handle is a plain pointer, so the object has to be kept
        // alive by hand. The bridge takes a reference, and the cache
        // gives it back when the image goes away.
        out->handle = (void*)CFBridgingRetain(image);
        out->width = static_cast<float>(size.width);
        out->height = static_cast<float>(size.height);
        return true;
    }
    return false;
}

// Decodes the data, choosing the decoder by the source.
bool Decode(const std::string& source, const std::string& data, Image* out) {
    if (IsVectorSource(source)) {
        return DecodeVector(data, out);
    }
    return DecodeRaster(data, out);
}

}  // namespace

bool IsRemoteUrl(const std::string& source) {
    return source.compare(0, 7, "http://") == 0 ||
           source.compare(0, 8, "https://") == 0;
}

bool IsVectorSource(const std::string& source) {
    std::string lower = source;
    for (char& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lower.compare(0, 19, "data:image/svg+xml") == 0) {
        return true;
    }
    return lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".svg") == 0;
}

ImageCache::ImageCache() = default;

ImageCache::~ImageCache() {
    for (Entry* entry : entries_) {
        if (entry->image.handle) {
            if (entry->image.kind == ImageKind::Raster) {
                CGImageRelease(static_cast<CGImageRef>(entry->image.handle));
            } else {
                // The handle holds a reference the bridge took, so it is
                // given back here. The bridge hands the object over, and
                // ARC takes it from there.
                (void)CFBridgingRelease(entry->image.handle);
            }
        }
        delete entry;
    }
}

ImageCache::Entry* ImageCache::Find(const std::string& source) {
    for (Entry* entry : entries_) {
        if (entry->source == source) {
            return entry;
        }
    }
    return nullptr;
}

BitmapHandle ImageCache::Get(const std::string& source) {
    if (source.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(CacheMutex());

    Entry* entry = Find(source);
    if (entry) {
        if (entry->image.handle) {
            return &entry->image;
        }
        if (entry->failed || entry->fetching) {
            return nullptr;
        }
    }

    if (IsRemoteUrl(source)) {
        // The fetch is started by Prefetch, which runs outside the lock.
        // Nothing to do here.
        return nullptr;
    }

    // A local file is read the first time it is asked for.
    const std::string data = ReadLocalFile(source);
    if (!entry) {
        entry = new Entry();
        entry->source = source;
        entries_.push_back(entry);
        if (entries_.size() > kMaxCachedImages) {
            delete entries_.front();
            entries_.erase(entries_.begin());
        }
    }

    if (data.empty() || !Decode(source, data, &entry->image)) {
        entry->failed = true;
        return nullptr;
    }
    return &entry->image;
}

bool ImageCache::GetSize(const std::string& source, float* outWidth,
                         float* outHeight) {
    BitmapHandle handle = Get(source);
    if (!handle) {
        return false;
    }
    const Image* image = static_cast<const Image*>(handle);
    if (outWidth) {
        *outWidth = image->width;
    }
    if (outHeight) {
        *outHeight = image->height;
    }
    return true;
}

void ImageCache::Prefetch(const std::string& source) {
    if (source.empty() || !IsRemoteUrl(source)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(CacheMutex());
        Entry* entry = Find(source);
        if (entry && (entry->image.handle || entry->failed || entry->fetching)) {
            return;
        }
        if (!entry) {
            entry = new Entry();
            entry->source = source;
            entries_.push_back(entry);
            if (entries_.size() > kMaxCachedImages) {
                delete entries_.front();
                entries_.erase(entries_.begin());
            }
        }
        entry->fetching = true;
    }

    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:source.c_str()]];
    if (!url) {
        std::lock_guard<std::mutex> lock(CacheMutex());
        Entry* entry = Find(source);
        if (entry) {
            entry->failed = true;
            entry->fetching = false;
        }
        return;
    }

    NSString* sourceString = [NSString stringWithUTF8String:source.c_str()];
    __block ImageCache* cache = this;

    NSURLSessionDataTask* task = [SharedSession()
        dataTaskWithURL:url
      completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        (void)response;
        std::lock_guard<std::mutex> lock(CacheMutex());

        Entry* entry = cache->Find(sourceString.UTF8String);
        if (!entry) {
            return;
        }
        entry->fetching = false;

        if (error || !data || data.length == 0 ||
            data.length > kMaxImageBytes) {
            entry->failed = true;
            return;
        }

        const std::string bytes(static_cast<const char*>(data.bytes),
                                data.length);
        if (!Decode(sourceString.UTF8String, bytes, &entry->image)) {
            entry->failed = true;
        }
    }];
    [task resume];
}

}  // namespace mac
