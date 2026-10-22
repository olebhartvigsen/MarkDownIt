// Drawing the images with Core Graphics.

#import <AppKit/AppKit.h>

#include "image_draw_mac.h"

namespace mac {

void DrawImageIntoContext(CGContextRef context, BitmapHandle image,
                          const Rect& rect) {
    if (!context || !image) {
        return;
    }

    const Image* info = static_cast<const Image*>(image);
    if (info->kind == ImageKind::Raster) {
        CGImageRef cgImage = static_cast<CGImageRef>(info->handle);
        if (!cgImage) {
            return;
        }
        CGContextSaveGState(context);
        // The context is flipped, so the image would come out upside
        // down without this.
        CGContextTranslateCTM(context, 0.0f, rect.y + rect.height);
        CGContextScaleCTM(context, 1.0f, -1.0f);
        CGContextDrawImage(context,
                           CGRectMake(rect.x, 0.0f, rect.width, rect.height),
                           cgImage);
        CGContextRestoreGState(context);
        return;
    }

    // A vector document. NSImage renders SVG from macOS 14 on, and the
    // image is rasterised into the rectangle it is drawn in.
    NSImage* nsImage = (__bridge NSImage*)info->handle;
    if (!nsImage) {
        return;
    }

    CGImageRef cgImage = [nsImage CGImageForProposedRect:nullptr
                                                context:nil
                                                  hints:nil];
    if (cgImage) {
        CGContextSaveGState(context);
        CGContextTranslateCTM(context, 0.0f, rect.y + rect.height);
        CGContextScaleCTM(context, 1.0f, -1.0f);
        CGContextDrawImage(context,
                           CGRectMake(rect.x, 0.0f, rect.width, rect.height),
                           cgImage);
        CGContextRestoreGState(context);
    }
}

}  // namespace mac
