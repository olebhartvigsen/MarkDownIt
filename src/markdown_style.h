#pragma once

// The visual style the markdown renders in. A style is a profile of
// theme tokens (theme.h) plus a few structural switches that the shared
// layout reads from LayoutMetrics, so both ports follow it without a
// platform branch.
//
// The style is chosen at run time by the environment variable
// MARKDOWNIT_STYLE:
//
//   apple      A clean documentation look built on Apple's DocC and
//              Human Interface Guidelines, without copying Apple's
//              design. See references/apple-wiki-style-guide.md.
//   github     GitHub's markdown CSS, the reference style.
//   wikipedia  The Wikipedia article look (Vector skin), built on the
//              Manual of Style. See references/wikipedia-markdown-style.md.
//
// The match ignores case and surrounding spaces. An unset or unknown
// value selects the default, Apple. The variable is read once, on
// the first call, so a running process never changes style midway.

#include <cstdlib>
#include <string>

enum class MarkdownStyle {
    GitHub,
    Wikipedia,
    Apple,
};

// The default when MARKDOWNIT_STYLE is unset or not recognised.
inline constexpr MarkdownStyle kDefaultMarkdownStyle = MarkdownStyle::Apple;

// Parse a style name. Returns false and leaves *out alone when the
// text names no style.
inline bool ParseMarkdownStyle(const std::string& text, MarkdownStyle* out) {
    size_t b = 0, e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t')) ++b;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
    std::string t;
    for (size_t i = b; i < e; ++i) {
        const char c = text[i];
        t += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (t == "github") { *out = MarkdownStyle::GitHub; return true; }
    if (t == "wikipedia") { *out = MarkdownStyle::Wikipedia; return true; }
    if (t == "apple") { *out = MarkdownStyle::Apple; return true; }
    return false;
}

// The style this process renders in.
inline MarkdownStyle ActiveMarkdownStyle() {
#if defined(MDI_PIN_STYLE_GITHUB)
    // The test targets define this so the layout goldens, which pin
    // GitHub's numbers, measure one fixed style whatever the
    // environment says. The style profile test names both styles
    // explicitly and does not depend on it.
    return MarkdownStyle::GitHub;
#else
    static const MarkdownStyle style = [] {
        MarkdownStyle s = kDefaultMarkdownStyle;
        std::string value;
#ifdef _WIN32
        char* buf = nullptr;
        size_t len = 0;
        if (_dupenv_s(&buf, &len, "MARKDOWNIT_STYLE") == 0 && buf) {
            value = buf;
            free(buf);
        }
#else
        if (const char* v = std::getenv("MARKDOWNIT_STYLE")) value = v;
#endif
        ParseMarkdownStyle(value, &s);
        return s;
    }();
    return style;
#endif
}
