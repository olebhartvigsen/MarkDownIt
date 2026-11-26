#pragma once

// The Windows half of the usage telemetry transport.
//
// The payload is shared (src/analytics.h); this file posts it with WinHTTP,
// which is the one HTTP stack every supported Windows has without shipping
// anything. The seam it implements, platform::TelemetrySink, is declared in
// src/platform/seams.h, and the factory below is the only way the shell gets
// one.
//
// The header stays out of <windows.h> on purpose: it is included from
// src/app.cpp, next to the rest of the shell, and the seam stays the one
// platform header that file needs.

#include <memory>
#include <string>

#include "analytics.h"

namespace platform {

class TelemetrySink;

// The transport for this platform. Never returns null and never throws.
std::unique_ptr<TelemetrySink> CreateTelemetrySink();

// The OS version as "major.minor.build", from RtlGetVersion. GetVersionEx
// answers 6.2 on Windows 10 without a manifest, so it cannot be used.
std::string WindowsOsVersion();

// The user's locale as a tag, for example "da-DK", as the OS reports it.
std::string WindowsLocale();

// The app version, "major.minor.build", read from the version resource
// that src/app.rc compiles into the exe. No second copy of the number.
std::string WindowsAppVersion();

// Everything the payload says about this machine, filled in one place.
analytics::SystemProps CurrentSystemProps();

}  // namespace platform
