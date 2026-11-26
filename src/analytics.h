#pragma once

#include <string>

// Anonymous usage analytics, the shared half.
//
// The app sends one event per launch: that it started, on which OS and app
// version. No document, path, title or text ever leaves the machine, and the
// event carries no identifier that survives a restart: the session id is
// random per process.
//
// The transport is a seam (platform::TelemetrySink, src/platform/seams.h), so
// the payload can be built and tested without a socket. The switch is a
// setting, on by default, and PRIVACY.md is the promise the payload has to
// keep.
//
// The key is an Aptabase app key. It is a public identifier that ships inside
// every client, not a secret, so it lives here rather than in a build secret.
// The EU region is baked into the key, and the endpoint follows from it.

// The transport seam. Declared here and not with the seam header on purpose:
// the payload code needs only the name, and this keeps every Windows shell
// translation unit that includes analytics.h clear of platform/seams.h and
// the DrawText macro trap that comes with it (AGENTS.md).
namespace platform {
class TelemetrySink;
}  // namespace platform

namespace analytics {

// The Aptabase app key, region EU.
inline constexpr const char kAptabaseKey[] = "A-EU-9136752434";

// Where an EU-region key posts. Aptabase answers 200 to an accepted event and
// 4xx to a malformed one; the sender is fire and forget, so the answer only
// goes to the trace log.
inline constexpr const char kAptabaseUrl[] = "https://eu.aptabase.com/api/v0/event";

// The one event the app sends today.
inline constexpr const char kAppStartEvent[] = "app_start";

// Aptabase requires this field and rejects the whole event with 400
// ("SystemProps.SdkVersion is required") when it is missing, so a payload
// without it is silently never recorded. It names the client library, not the
// app version, so it does not duplicate the version in src/app.rc.
inline constexpr const char kSdkVersion[] = "markdownit-cpp@1";

// What the event says about the machine. Every field is coarse on purpose:
// the OS family and version, the app version, and whether this is a debug
// build. Nothing here identifies a person or a document.
struct SystemProps {
    std::string osName;      // "Windows" or "macOS"
    std::string osVersion;   // "10.0.22631" or "15.1"
    std::string appVersion;  // "0.2.1"
    std::string locale;      // "da-DK", as the OS reports it
    bool debug = false;
};

// A random 128-bit session id as 32 hex characters. Randomness is the point:
// it is not derived from the machine, so two launches cannot be tied to one
// another.
std::string NewSessionId();

// The Aptabase payload for one event, as JSON. `timestamp` is the ISO 8601
// UTC time; the caller passes it so the format is testable.
//
// Shape, from Aptabase's ingest contract:
//   {"timestamp":"...","sessionId":"...","eventName":"...",
//    "systemProps":{"locale":"...","osName":"...","osVersion":"...",
//                   "appVersion":"...","isDebug":false,
//                   "sdkVersion":"..."},"props":{}}
std::string BuildEventJson(const std::string& eventName,
                           const std::string& sessionId,
                           const std::string& timestamp,
                           const SystemProps& system);

// True in a build without NDEBUG. The one definition of "is this a
// development build", so both shells and the payload agree, and the flag is
// not decided again at a second call site.
inline constexpr bool IsDebugBuild() {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

// Sends the one app_start event of this launch.
//
// `enabled` is the setting, and it is the only gate here: when it is false
// this does nothing at all, no payload is built and no transport is touched
// (tests/analytics_test.cpp proves the sink sees zero calls). When it is
// true the payload is built with a fresh session id and the current time and
// handed to `sink`, which owns the wire and writes any failure to the crash
// trace. The call blocks for up to the transport's two seconds, so the shell
// runs it on a detached thread: startup never waits on the network, and the
// sink never touches the UI.
void TrackAppStart(platform::TelemetrySink& sink, const SystemProps& system,
                   bool enabled);

// The current UTC time in the format the payload needs, without a locale or
// timezone dependency: "2026-11-26T10:15:30.123Z".
std::string UtcTimestampNow();

}  // namespace analytics