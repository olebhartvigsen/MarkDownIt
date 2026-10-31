#pragma once

// The font registration, and the chrome's face.
//
// The mac build draws with the platform's own faces: the system UI
// font for text and chrome, Menlo for code, as the port plan's font
// note asks. The bundle still ships the metric compatible stand ins
// the Windows build uses (Selawik for Segoe UI, Cascadia Mono) and
// registers them at launch, so a parity comparison or a future
// setting can pick them without a rebuild.
//
// The files sit in third_party/ and are copied into the app bundle as
// Contents/Resources/Fonts.

#include <string>

#ifdef __OBJC__
@class NSFont;
#endif

namespace mac {

// Registers every .ttf in the directory for this process. A file that
// is already registered counts as registered, because the call is made
// from more than one place (the app and the tests). Returns how many of
// the fonts are usable afterwards, 0 when the directory does not exist.
int RegisterFontsFromDirectory(const std::string& dir);

// Registers the fonts of the running app bundle (Resources/Fonts).
int RegisterBundledFonts();

#ifdef __OBJC__
// The chrome's face: the platform's own UI font, so the strip, the
// find bar and the status rows look native.
NSFont* SystemUIFont(float size);
#endif

}  // namespace mac
