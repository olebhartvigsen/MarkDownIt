#pragma once

// The macOS clipboard.
//
// A thin wrapper over NSPasteboard, so the rest of the app never touches
// AppKit for clipboard work and the behaviour can be tested with a fake.

#include <string>

namespace mac {

// Reads and writes plain text on the general pasteboard.
class Clipboard {
public:
    Clipboard();
    ~Clipboard();

    // The text on the clipboard, empty when there is none.
    std::string GetText() const;

    // Puts text on the clipboard, replacing what was there.
    // Puts the text on the pasteboard and reports whether the write
    // went through (text spec C3).
    bool SetText(const std::string& text);

    // Whether the clipboard holds text.
    bool HasText() const;
};

}  // namespace mac
