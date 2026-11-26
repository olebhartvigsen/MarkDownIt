#pragma once

// The macOS half of the usage telemetry transport.
//
// The payload is shared (src/analytics.h); this file posts it with
// NSURLSession, which is part of Foundation and needs nothing added to the
// bundle. The seam it implements, platform::TelemetrySink, is declared in
// src/platform/seams.h.

#include <memory>
#include <string>

#include "analytics.h"

namespace platform {

class TelemetrySink;

// The transport for this platform. Never returns null and never throws.
std::unique_ptr<TelemetrySink> CreateTelemetrySink();

// The OS version as "major.minor", trimmed from
// NSProcessInfo.operatingSystemVersionString ("Version 15.1.1 ..." -> "15.1").
std::string MacOsVersion();

// The user's locale as a tag, for example "da-DK". The OS reports "da_DK";
// the tag form is the one the payload and the Windows side use.
std::string MacLocale();

// The app version from the bundle's CFBundleShortVersionString, which CMake
// fills from the version in src/app.rc. No second copy of the number.
std::string MacAppVersion();

// Everything the payload says about this machine, filled in one place.
analytics::SystemProps CurrentSystemProps();

}  // namespace platform
