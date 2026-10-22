#pragma once

// The macOS crash breadcrumbs.
//
// The API matches the shared crash_trace.h, so the code that logs a
// breadcrumb is the same on both platforms. The log lands in
// ~/Library/Logs/MarkDownIt/crash_trace.log.

#include <cstdarg>
#include <string>

namespace mac {

// Appends one breadcrumb line and flushes it immediately, so the last
// line names the stage that ran when the process died.
void TraceMac(const char* fmt, ...);

// The same, for a wide string.
void TraceMacWide(const std::wstring& text);

}  // namespace mac
