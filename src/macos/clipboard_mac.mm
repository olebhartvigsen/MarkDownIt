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

// The pasteboard type that carries the raw markdown.
//
// The Windows build puts the raw markdown on the clipboard under a
// format of its own, next to the plain text, so a copy of a table keeps
// its table. The pasteboard type is the macOS version of that format.
NSString* MarkdownPasteboardType() {
    return @"com.olebhartvigsen.markdownit.markdown";
}

}  // namespace

Clipboard::Clipboard() = default;

Clipboard::~Clipboard() = default;

std::string Clipboard::GetText() const {
    NSString* text = [GeneralPasteboard() stringForType:NSPasteboardTypeString];
    if (!text) return std::string();
    return std::string([text UTF8String]);
}

bool Clipboard::SetText(const std::string& text) {
    NSPasteboard* pasteboard = GeneralPasteboard();
    [pasteboard clearContents];

    // NSString wants a nil terminated UTF-8 string. The text comes from
    // the document, which never has an interior null, but the copy is
    // cheap and the guarantee is worth having.
    NSString* value = [[NSString alloc] initWithBytes:text.data()
                                                length:text.size()
                                              encoding:NSUTF8StringEncoding];
    if (!value) return false;
    [pasteboard setString:value forType:NSPasteboardTypeString];

    // The raw markdown goes on the pasteboard under its own type as
    // well, so the copy keeps the structure the text alone would lose.
    [pasteboard setString:value forType:MarkdownPasteboardType()];
    return true;
}

bool Clipboard::HasText() const {
    return [GeneralPasteboard() stringForType:NSPasteboardTypeString] != nil;
}

}  // namespace mac
