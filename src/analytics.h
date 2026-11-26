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

namespace analytics {

// The Aptabase app key, region EU.
inline constexpr const char kAptabaseKey[] = "A-EU-9136752434";

// Where an EU-region key posts. Aptabase answers 200 to an accepted event and
// 4xx to a malformed one; the sender is fire and forget, so the answer only
// goes to the trace log.
inline constexpr const char kAptabaseUrl[] = "https://eu.aptabase.com/api/v0/event";

// The one event the app sends today.
inline constexpr const char kAppStartEvent[] = "app_start";

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
//                   "appVersion":"...","isDebug":false},"props":{}}
std::string BuildEventJson(const std::string& eventName,
                           const std::string& sessionId,
                           const std::string& timestamp,
                           const SystemProps& system);

// The current UTC time in the format the payload needs, without a locale or
// timezone dependency: "2026-11-26T10:15:30.123Z".
std::string UtcTimestampNow();

}  // namespace analytics
