#pragma once

// The appearance the app renders in: the mode the user chose, and the
// appearance that choice resolves to.
//
// The mode is what the settings store keeps and what a command toggles.
// The appearance is what the renderers and the chrome actually read.
// Resolving the two is one shared rule, so the window, the document and
// the tests all answer the same way, and no port has its own copy of it.
//
// System means the operating system decides. The shell asks the OS and
// hands the answer in; this header never calls a platform API.
//
// MARKDOWNIT_THEME overrides the stored mode for a run, in the same way
// MARKDOWNIT_STYLE overrides the markdown style. It exists so the CI
// captures can render a fixed appearance on a runner whose own setting
// is not ours to choose.

#include <cstdlib>
#include <string>

namespace theme {

enum class ThemeMode {
    System = 0,
    Light = 1,
    Dark = 2,
};

enum class Appearance {
    Light = 0,
    Dark = 1,
};

// System, the reference guide's default: follow the OS.
inline constexpr ThemeMode kDefaultThemeMode = ThemeMode::System;

// Parse a mode name, ignoring case and surrounding spaces. Returns false
// and leaves *out alone when the text names no mode.
inline bool ParseThemeMode(const std::string& text, ThemeMode* out) {
    size_t b = 0, e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t')) ++b;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
    std::string t;
    for (size_t i = b; i < e; ++i) {
        const char c = text[i];
        t += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (t == "system" || t == "auto") { *out = ThemeMode::System; return true; }
    if (t == "light") { *out = ThemeMode::Light; return true; }
    if (t == "dark") { *out = ThemeMode::Dark; return true; }
    return false;
}

// The name the settings file or the registry carries.
inline const char* ThemeModeName(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light: return "light";
        case ThemeMode::Dark: return "dark";
        default: return "system";
    }
}

// Clamp an integer read from a store to a mode. Anything unknown is
// System, the safe default.
inline ThemeMode ThemeModeFromInt(int value) {
    switch (value) {
        case static_cast<int>(ThemeMode::Light): return ThemeMode::Light;
        case static_cast<int>(ThemeMode::Dark): return ThemeMode::Dark;
        default: return ThemeMode::System;
    }
}

// The one resolution rule. An explicit choice wins; System follows the
// OS answer the caller supplies.
inline Appearance ResolveAppearance(ThemeMode mode, bool systemIsDark) {
    switch (mode) {
        case ThemeMode::Light: return Appearance::Light;
        case ThemeMode::Dark: return Appearance::Dark;
        default: return systemIsDark ? Appearance::Dark : Appearance::Light;
    }
}

inline bool IsDark(Appearance appearance) {
    return appearance == Appearance::Dark;
}

// The environment override, if one is set. Returns false when the
// variable is unset or names no mode.
inline bool ThemeModeFromEnvironment(ThemeMode* out) {
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "MARKDOWNIT_THEME") != 0 || !buf) return false;
    const bool parsed = ParseThemeMode(buf, out);
    free(buf);
    return parsed;
#else
    const char* value = std::getenv("MARKDOWNIT_THEME");
    if (!value) return false;
    return ParseThemeMode(value, out);
#endif
}

// The mode this run starts in: the override when set, the stored value
// otherwise.
inline ThemeMode StartupThemeMode(ThemeMode stored) {
    ThemeMode override_mode = ThemeMode::System;
    if (ThemeModeFromEnvironment(&override_mode)) return override_mode;
    return stored;
}

}  // namespace theme
