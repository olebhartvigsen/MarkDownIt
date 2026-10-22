// The font registration.
//
// See fonts_mac.h for why the mac build bundles fonts. Registration is
// a process wide Core Text call, so the faces are usable from the
// moment didFinishLaunching runs and nothing is installed on the
// system. A failed registration is a breadcrumb, not a fatal error:
// the text engine falls back to the system faces.

#include "fonts_mac.h"

#include "crash_trace_mac.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

namespace mac {

int RegisterFontsFromDirectory(const std::string& dir) {
    NSString* path = [NSString stringWithUTF8String:dir.c_str()];
    if (!path) return 0;
    NSArray<NSString*>* entries =
        [[NSFileManager defaultManager] contentsOfDirectoryAtPath:path
                                                            error:nil];
    if (!entries) return 0;

    int registered = 0;
    for (NSString* name in entries) {
        if (![[name pathExtension] isEqualToString:@"ttf"]) continue;
        NSURL* url = [NSURL fileURLWithPath:
            [path stringByAppendingPathComponent:name]];
        CFErrorRef error = nullptr;
        const bool ok = CTFontManagerRegisterFontsForURL(
            (__bridge CFURLRef)url, kCTFontManagerScopeProcess, &error);
        if (ok) {
            ++registered;
            continue;
        }
        // A second registration of the same file is fine, anything
        // else is worth a breadcrumb.
        const CFIndex code = error ? CFErrorGetCode(error) : 0;
        if (code == kCTFontManagerErrorAlreadyRegistered) {
            ++registered;
        } else if (error) {
            CFStringRef desc = CFErrorCopyDescription(error);
            char text[512] = {0};
            if (desc && CFStringGetCString(desc, text, sizeof(text),
                                           kCFStringEncodingUTF8)) {
                TraceMac("a bundled font did not register: %s", text);
            }
            if (desc) CFRelease(desc);
        }
        if (error) CFRelease(error);
    }
    return registered;
}

int RegisterBundledFonts() {
    NSString* dir = [[NSBundle mainBundle] pathForResource:@"Fonts" ofType:nil];
    if (!dir) {
        TraceMac("the bundle carries no font directory");
        return 0;
    }
    const int count = RegisterFontsFromDirectory([dir UTF8String]);
    TraceMac("registered %d bundled fonts", count);
    return count;
}

#ifdef __OBJC__
NSFont* BundledUIFont(float size) {
    NSFont* font = [NSFont fontWithName:@"Selawik" size:size];
    return font ? font : [NSFont systemFontOfSize:size];
}
#endif

}  // namespace mac
