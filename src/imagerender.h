#pragma once

// The image service a document renderer draws with.
//
// The renderer lays a picture out before it has been decoded and draws it
// once the decode answers; the fetch itself is the platform's. The mac
// supplies an asynchronous cache over NSURLSession, the Windows shell its
// blocking loader, and a renderer with no service at all answers nothing
// through NoImageSource, which is what the layout tests use.

#include <string>

#include "platform/seams.h"

namespace platform {

class ImageSource {
public:
    virtual ~ImageSource() = default;

    // The directory holding the open document, so a relative path resolves
    // beside it. Empty for an unsaved document.
    virtual void SetBaseDir(const std::string&) {}

    // Starts the fetch of a remote image, so it begins before the paint.
    virtual void Prefetch(const std::string&) {}

    // The natural size in points, false while the image is not known yet.
    virtual bool GetSize(const std::string&, float*, float*) { return false; }

    // The decoded image, or null while it is missing or still arriving.
    virtual BitmapHandle Get(const std::string&) { return nullptr; }
};

// The source of a renderer that has no images to offer: every ask answers
// nothing, and the layout keeps the placeholder height.
ImageSource& NoImageSource();

}  // namespace platform
