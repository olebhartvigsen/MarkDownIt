#include "analytics.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>

#include "platform/seams.h"

namespace analytics {
namespace {

// JSON string escaping for the five fields we send. They are short and come
// from the OS, but a locale name is not guaranteed to be ASCII everywhere, so
// control characters and the two quote characters are escaped rather than
// assumed away.
std::string Quote(const std::string& in) {
    std::string out = "\"";
    for (unsigned char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

}  // namespace

std::string NewSessionId() {
    std::random_device device;
    // Two 64-bit draws, so a collision needs 128 bits of bad luck rather than
    // 32. The generator is seeded from the OS entropy source.
    std::mt19937_64 rng((static_cast<uint64_t>(device()) << 32) ^ device());
    const uint64_t hi = rng();
    const uint64_t lo = rng();
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(hi),
                  static_cast<unsigned long long>(lo));
    return std::string(buf, 32);
}

std::string UtcTimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                  utc.tm_min, utc.tm_sec, static_cast<int>(millis.count()));
    return std::string(buf);
}

std::string BuildEventJson(const std::string& eventName,
                           const std::string& sessionId,
                           const std::string& timestamp,
                           const SystemProps& system) {
    std::string json;
    json.reserve(320);
    json += "{\"timestamp\":";
    json += Quote(timestamp);
    json += ",\"sessionId\":";
    json += Quote(sessionId);
    json += ",\"eventName\":";
    json += Quote(eventName);
    json += ",\"systemProps\":{\"locale\":";
    json += Quote(system.locale);
    json += ",\"osName\":";
    json += Quote(system.osName);
    json += ",\"osVersion\":";
    json += Quote(system.osVersion);
    json += ",\"appVersion\":";
    json += Quote(system.appVersion);
    json += ",\"isDebug\":";
    json += system.debug ? "true" : "false";
    json += "},\"props\":{}}";
    return json;
}

void TrackAppStart(platform::TelemetrySink& sink, const SystemProps& system,
                   bool enabled) {
    // The opt-out: no payload is built and the sink is never called, so a
    // reader who turned the switch off makes no request at all.
    if (!enabled) return;

    // A fresh session id and the moment of this launch. Neither is kept: the
    // id is drawn for this event and dropped with it.
    const std::string body =
        BuildEventJson(kAppStartEvent, NewSessionId(), UtcTimestampNow(),
                       system);

    // Fire and forget. The answer goes to the trace log inside the transport;
    // the reader is never shown anything, because the app works without a
    // network and this is not a feature they asked for.
    sink.Post(kAptabaseUrl, kAptabaseKey, body);
}

}  // namespace analytics
