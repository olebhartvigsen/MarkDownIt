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

bool DrawChevronIntoContext(CGContextRef context, const Rect& rect,
                            bool expanded, const Color& color) {
    if (!context) return false;
    NSString* name = expanded ? @"chevron.down" : @"chevron.right";
    NSImage* symbol = [NSImage imageWithSystemSymbolName:name
                                  accessibilityDescription:nil];
    if (!symbol) return false;
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration
        configurationWithPointSize:rect.height * 0.78
                            weight:NSFontWeightSemibold];
    symbol = [symbol imageWithSymbolConfiguration:config];
    if (!symbol) return false;

    // The symbol draws as a template, so the colour comes from filling
    // its box and knocking that fill down to the shape of the glyph.
    // The two steps are played out in a small picture of their own:
    // the knocking-down step clears everything it covers, and played
    // on the window's own context it left a square hole in whatever
    // lay under the mark, the selection band included.
    const NSSize size = NSMakeSize(rect.width, rect.height);
    NSColor* tint = [NSColor colorWithSRGBRed:color.r green:color.g
                                        blue:color.b alpha:color.a];
    NSImage* tinted = [NSImage imageWithSize:size flipped:YES
                              drawingHandler:^BOOL(NSRect dst) {
        [tint set];
        NSRectFillUsingOperation(dst, NSCompositingOperationCopy);
        [symbol drawInRect:dst fromRect:NSZeroRect
                 operation:NSCompositingOperationDestinationIn
                  fraction:1.0 respectFlipped:YES hints:nil];
        return YES;
    }];
    if (!tinted) return false;

    NSGraphicsContext* gc = [NSGraphicsContext
        graphicsContextWithCGContext:context flipped:YES];
    if (!gc) return false;

    const NSRect box = NSMakeRect(rect.x, rect.y, rect.width, rect.height);
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:gc];
    [tinted drawInRect:box fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0 respectFlipped:YES hints:nil];
    [NSGraphicsContext restoreGraphicsState];
    return true;
}

}  // namespace mac
