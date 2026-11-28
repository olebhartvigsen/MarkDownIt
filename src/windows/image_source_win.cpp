// The Windows image service behind platform::ImageSource, over
// ImageHelper: the same size read, decode and bitmap creation the
// Windows renderer made for itself.

#include <windows.h>

#include <d2d1.h>

// The platform headers put the DrawText macro in place; the seam keeps its
// own spelling, so it goes before any header that reads the seam.
#ifdef DrawText
#undef DrawText
#endif

#include "windows/image_source_win.h"

#include "imagehelper.h"

namespace platform {
namespace {

// The Windows renderer's own cap: a reader who opens a large document
// should not keep every picture it ever showed.
constexpr size_t kMaxEntries = 128;

}  // namespace

WinImageSource::~WinImageSource() { Clear(); }

void WinImageSource::Clear() {
    for (Entry& entry : entries_) {
        if (entry.bitmap) ImageHelper::ReleaseBitmap(entry.bitmap);
        entry.bitmap = nullptr;
    }
    entries_.clear();
}

WinImageSource::Entry* WinImageSource::Find(const std::string& url) {
    for (Entry& entry : entries_) {
        if (entry.url == url) return &entry;
    }
    return nullptr;
}

WinImageSource::Entry& WinImageSource::Insert(const std::string& url) {
    if (entries_.size() >= kMaxEntries) {
        if (entries_.front().bitmap) {
            ImageHelper::ReleaseBitmap(entries_.front().bitmap);
        }
        entries_.erase(entries_.begin());
    }
    Entry entry;
    entry.url = url;
    entries_.push_back(std::move(entry));
    return entries_.back();
}

void WinImageSource::Prefetch(const std::string& source) {
    // The decode is synchronous on this port. Doing it here, before the
    // paint, keeps the cost in the same place the Windows renderer had
    // it: the pass that lays the picture out.
    if (!target_ || Find(source)) return;
    void* bitmap =
        ImageHelper::LoadBitmapForTarget(*target_, source, 0.0f, baseDir_);
    if (!bitmap) return;
    Entry& entry = Insert(source);
    entry.bitmap = bitmap;
    ImageHelper::BitmapSize(bitmap, &entry.width, &entry.height);
    entry.sized = entry.width > 0.0f && entry.height > 0.0f;
}

bool WinImageSource::GetSize(const std::string& source, float* width,
                             float* height) {
    Entry* entry = Find(source);
    if (!entry) {
        // The header read needs no target, so a size is known even
        // before the frame has one.
        float w = 0.0f;
        float h = 0.0f;
        if (!ImageHelper::LoadImageSize(source, &w, &h, baseDir_)) return false;
        if (w <= 0.0f || h <= 0.0f) return false;
        entry = &Insert(source);
        entry->width = w;
        entry->height = h;
        entry->sized = true;
    }
    if (!entry->sized) return false;
    if (width) *width = entry->width;
    if (height) *height = entry->height;
    return true;
}

BitmapHandle WinImageSource::Get(const std::string& source) {
    Entry* entry = Find(source);
    if (entry && entry->bitmap) return entry->bitmap;
    if (!target_) return nullptr;
    void* bitmap =
        ImageHelper::LoadBitmapForTarget(*target_, source, 0.0f, baseDir_);
    if (!bitmap) return nullptr;
    if (!entry) entry = &Insert(source);
    entry->bitmap = bitmap;
    if (!entry->sized) {
        ImageHelper::BitmapSize(bitmap, &entry->width, &entry->height);
        entry->sized = entry->width > 0.0f && entry->height > 0.0f;
    }
    return entry->bitmap;
}

}  // namespace platform
