#pragma once
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
// Forward-declare for non-Windows test compilation.
typedef void* HWND;
#endif

// Custom clipboard format name for lossless markdown copy/paste.
extern const char* kMarkdownFormatName;

// Copy the selection to the clipboard in two formats:
// 1. Custom "MarkDownIt Markdown" format (raw UTF-8 markdown)
// 2. CF_UNICODETEXT (same markdown, as wide string)
bool ClipboardCopy(HWND hwnd, const std::string& utf8);

// Cut: copy then return the selected range so the caller can delete it.
// Returns the selected text, or empty if nothing selected.
bool ClipboardCut(HWND hwnd, const std::string& utf8);

// Paste: read CF_UNICODETEXT only, normalize line endings and escape Markdown.
// Ignore custom formats, HTML, RTF and objects, even if no text is available.
// Returns text for insertion, or empty for a no-op.
std::string ClipboardPaste(HWND hwnd);

// Convert a simple HTML fragment to Markdown.
// Handles: headings, bold, italic, code, links, lists, blockquotes.
// Anything else degrades to plain text.
std::string HtmlToMarkdown(const std::string& html);

// Escape markdown metacharacters in plain text so a pasted
// asterisk does not become emphasis.
std::string EscapeMarkdown(const std::string& text);
