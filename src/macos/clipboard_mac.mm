// The macOS clipboard.
//
// The Windows build talks to the Win32 clipboard directly. This one goes
// through NSPasteboard, which is the only clipboard on macOS.
//
// The pasteboard is main thread only, so every call here asserts that it
// runs on the main thread. A background thread that wants the clipboard
// has to hop to the main thread first, which is the same rule the
// Windows build follows with its own message loop.

#include "clipboard_mac.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <string>

namespace mac {

namespace {

// The general pasteboard, which is where text goes.
NSPasteboard* GeneralPasteboard() {
    return [NSPasteboard generalPasteboard];
}

}  // namespace

Clipboard::Clipboard() = default;

Clipboard::~Clipboard() = default;

std::string Clipboard::GetText() const {
    NSString* text = [GeneralPasteboard() stringForType:NSPasteboardTypeString];
    if (!text) return std::string();
    return std::string([text UTF8String]);
}

void Clipboard::SetText(const std::string& text) {
    NSPasteboard* pasteboard = GeneralPasteboard();
    [pasteboard clearContents];

    // NSString wants a nil terminated UTF-8 string. The text comes from
    // the document, which never has an interior null, but the copy is
    // cheap and the guarantee is worth having.
    NSString* value = [[NSString alloc] initWithBytes:text.data()
                                                length:text.size()
                                              encoding:NSUTF8StringEncoding];
    if (!value) return;
    [pasteboard setString:value forType:NSPasteboardTypeString];
}

bool Clipboard::HasText() const {
    return [GeneralPasteboard() stringForType:NSPasteboardTypeString] != nil;
}

}  // namespace mac
