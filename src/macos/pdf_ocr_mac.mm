// PDF page OCR through Vision (office spec, PDF import: scanned pages
// are offered OCR, Danish included). The page is drawn into a bitmap
// with Core Graphics and handed to VNRecognizeTextRequest; no external
// dependency is involved, Vision ships with macOS.
#include "pdf_ocr_mac.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

namespace mac {

std::string RecognizePdfPage(const std::string& path, std::size_t page_index) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        if (url == nil) return std::string();
        CGPDFDocumentRef document = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);
        if (document == nullptr) return std::string();

        std::string text;
        const std::size_t pages =
            static_cast<std::size_t>(CGPDFDocumentGetNumberOfPages(document));
        if (page_index < pages) {
            CGPDFPageRef page = CGPDFDocumentGetPage(document, page_index + 1);
            if (page != nullptr) {
                // Two pixels per point is what the recognizer wants.
                const CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
                const double scale = 2.0;
                const std::size_t width =
                    static_cast<std::size_t>(box.size.width * scale + 0.5);
                const std::size_t height =
                    static_cast<std::size_t>(box.size.height * scale + 0.5);

                CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
                CGContextRef context = CGBitmapContextCreate(
                    nullptr, width, height, 8, 0, space,
                    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
                if (context != nullptr) {
                    CGContextSetRGBFillColor(context, 1.0, 1.0, 1.0, 1.0);
                    CGContextFillRect(
                        context, CGRectMake(0, 0, static_cast<CGFloat>(width),
                                            static_cast<CGFloat>(height)));
                    // The drawing transform carries the page rotation and
                    // the box offset, so a rotated page still lands square.
                    CGContextSaveGState(context);
                    const CGAffineTransform transform = CGPDFPageGetDrawingTransform(
                        page, kCGPDFMediaBox,
                        CGRectMake(0, 0, static_cast<CGFloat>(width),
                                   static_cast<CGFloat>(height)),
                        0, true);
                    CGContextConcatCTM(context, transform);
                    CGContextDrawPDFPage(context, page);
                    CGContextRestoreGState(context);

                    CGImageRef image = CGBitmapContextCreateImage(context);
                    if (image != nullptr) {
                        if (@available(macOS 10.15, *)) {
                            VNImageRequestHandler* handler =
                                [[VNImageRequestHandler alloc] initWithCGImage:image
                                                                      options:@{}];
                            VNRecognizeTextRequest* request =
                                [[VNRecognizeTextRequest alloc] init];
                            request.recognitionLevel =
                                VNRequestTextRecognitionLevelAccurate;
                            request.usesLanguageCorrection = YES;

                            // Danish first, then English, and only the
                            // languages this Mac actually knows.
                            NSMutableArray<NSString*>* languages =
                                [NSMutableArray array];
                            NSArray<NSString*>* supported =
                                [VNRecognizeTextRequest
                                    supportedRecognitionLanguagesForTextRecognitionLevel:
                                        VNRequestTextRecognitionLevelAccurate
                                                                       revision:
                                        VNRecognizeTextRequestRevision1
                                                                          error:nil];
                            for (NSString* wanted in @[ @"da-DK", @"en-US" ]) {
                                if (supported == nil ||
                                    [supported containsObject:wanted]) {
                                    [languages addObject:wanted];
                                }
                            }
                            if ([languages count] > 0) {
                                request.recognitionLanguages = languages;
                            }

                            NSError* recognition_error = nil;
                            if ([handler performRequests:@[ request ]
                                                   error:&recognition_error]) {
                                for (VNRecognizedTextObservation* observation in
                                     request.results) {
                                    VNRecognizedText* candidate =
                                        [[observation topCandidates:1] firstObject];
                                    if (candidate != nil && candidate.string != nil) {
                                        text += candidate.string.UTF8String;
                                        text += "\n";
                                    }
                                }
                            }
                        }
                        CGImageRelease(image);
                    }
                    CGContextRelease(context);
                }
                CGColorSpaceRelease(space);
            }
        }
        CGPDFDocumentRelease(document);
        return text;
    }
}

}  // namespace mac
