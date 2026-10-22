#pragma once

// The fonts the mac build bundles.
//
// The Windows build renders with Segoe UI and Cascadia Mono. So that a
// document lays out the same way on both systems, the mac build ships
// metric compatible stand ins and registers them at launch: Selawik,
// Microsoft's open replacement for Segoe UI (the advance widths match,
// so line breaks land where they land on Windows), and Cascadia Mono
// itself, the code face the Windows build asks for first.
//
// The files sit in third_party/ and are copied into the app bundle as
// Contents/Resources/Fonts. The text engine maps the theme's family
// names onto them, see text_engine_mac.cpp, and falls back to the
// system fonts when the registration found nothing.

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
// A UI font: Selawik when it is registered, the system font otherwise.
// The chrome asks for this so its labels carry the face the Windows
// chrome uses.
NSFont* BundledUIFont(float size);
#endif

}  // namespace mac
