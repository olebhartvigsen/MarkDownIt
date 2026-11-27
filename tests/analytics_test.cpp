#include "analytics.h"
#include "gtest_lite.h"
#include "platform/seams.h"

#include <cstdlib>
#include <string>
#include <vector>

// The payload contract, pinned. These tests exist because the thing being
// built here leaves the machine: what it says has to be exactly what
// PRIVACY.md promises, and nothing else.
//
// The seam is faked here, so the launch path can be tested without a socket:
// the fake counts the Post calls and keeps what they carried.

namespace {

class FakeTelemetrySink final : public platform::TelemetrySink {
public:
    struct Call {
        std::string url;
        std::string appKey;
        std::string body;
    };

    // Takes the answer the transport would have given. The launch path is
    // fire and forget, so a false here must change nothing but the trace log.
    explicit FakeTelemetrySink(bool answer = true) : answer_(answer) {}

    bool Post(const std::string& url, const std::string& appKey,
              const std::string& body) override {
        calls.push_back(Call{url, appKey, body});
        return answer_;
    }

    std::vector<Call> calls;

private:
    bool answer_;
};

// The environment is process-wide and the suite runs every test in one
// process, so each case below puts the variables back the way it found them.
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

// The environment is process-wide, so each case restores both variables when
// it ends. CI runs this suite with MARKDOWNIT_NO_ANALYTICS=1 set, and no later
// test may see that changed.
struct EnvValue {
    bool present = false;
    std::string value;
};

EnvValue SaveEnv(const char* name) {
    const char* v = std::getenv(name);
    EnvValue saved;
    saved.present = v != nullptr;
    if (v != nullptr) saved.value = v;
    return saved;
}

class VetoScope {
public:
    VetoScope()
        : analytics_(SaveEnv("MARKDOWNIT_NO_ANALYTICS")),
          doNotTrack_(SaveEnv("DO_NOT_TRACK")) {
        SetEnv("MARKDOWNIT_NO_ANALYTICS", nullptr);
        SetEnv("DO_NOT_TRACK", nullptr);
    }
    ~VetoScope() {
        SetEnv("MARKDOWNIT_NO_ANALYTICS",
               analytics_.present ? analytics_.value.c_str() : nullptr);
        SetEnv("DO_NOT_TRACK",
               doNotTrack_.present ? doNotTrack_.value.c_str() : nullptr);
    }
    VetoScope(const VetoScope&) = delete;
    VetoScope& operator=(const VetoScope&) = delete;

private:
    EnvValue analytics_;
    EnvValue doNotTrack_;
};

}  // namespace


TEST(Analytics, PayloadHasTheContractShape) {
    analytics::SystemProps system;
    system.osName = "Windows";
    system.osVersion = "10.0.22631";
    system.appVersion = "0.2.1";
    system.locale = "da-DK";
    const std::string json = analytics::BuildEventJson(
        analytics::kAppStartEvent, "0123456789abcdef0123456789abcdef",
        "2026-11-26T10:15:30.123Z", system);

    EXPECT_TRUE(json.find("\"eventName\":\"app_start\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"sessionId\":\"0123456789abcdef0123456789abcdef\"") !=
                std::string::npos);
    EXPECT_TRUE(json.find("\"timestamp\":\"2026-11-26T10:15:30.123Z\"") !=
                std::string::npos);
    EXPECT_TRUE(json.find("\"osName\":\"Windows\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"osVersion\":\"10.0.22631\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"appVersion\":\"0.2.1\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"locale\":\"da-DK\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"isDebug\":false") != std::string::npos);
    // Aptabase answers 400 "SystemProps.SdkVersion is required" without
    // this field, so an event missing it is never recorded at all.
    EXPECT_TRUE(json.find("\"sdkVersion\":\"markdownit-cpp@1\"") !=
                std::string::npos);
    EXPECT_TRUE(json.find("\"props\":{}") != std::string::npos);
    // Braces balance, which is the cheapest proof the JSON is not truncated.
    size_t open = 0, close = 0;
    for (char ch : json) {
        if (ch == '{') ++open;
        if (ch == '}') ++close;
    }
    EXPECT_EQ(open, close);
}

TEST(Analytics, PayloadCarriesNothingAboutADocument) {
    analytics::SystemProps system;
    system.osName = "macOS";
    const std::string json = analytics::BuildEventJson(
        analytics::kAppStartEvent, "abc", "2026-11-26T10:15:30.000Z", system);
    // No path, no title, no text. The only string-valued fields are the five
    // system properties plus the identity of the event.
    EXPECT_TRUE(json.find("/") == std::string::npos);      // no paths
    EXPECT_TRUE(json.find(".md") == std::string::npos);    // no file kinds
    EXPECT_TRUE(json.find("file") == std::string::npos);   // no file field at all
}

TEST(Analytics, QuotesAndControlCharactersAreEscaped) {
    analytics::SystemProps system;
    system.osName = "Odd\"Name";
    system.locale = "line\nbreak";
    const std::string json = analytics::BuildEventJson(
        analytics::kAppStartEvent, "abc", "2026-11-26T00:00:00.000Z", system);
    EXPECT_TRUE(json.find("Odd\\\"Name") != std::string::npos);
    EXPECT_TRUE(json.find("line\\nbreak") != std::string::npos);
}

TEST(Analytics, SessionIdsAreRandomAndThirtyTwoHex) {
    const std::string a = analytics::NewSessionId();
    const std::string b = analytics::NewSessionId();
    EXPECT_EQ(a.size(), static_cast<size_t>(32));
    EXPECT_TRUE(a != b);
    for (char ch : a) {
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        EXPECT_TRUE(hex);
    }
}

TEST(Analytics, TimestampIsIsoUtcWithMillis) {
    const std::string now = analytics::UtcTimestampNow();
    EXPECT_EQ(now.size(), static_cast<size_t>(24));
    EXPECT_EQ(now[4], '-');
    EXPECT_EQ(now[10], 'T');
    EXPECT_EQ(now.back(), 'Z');
}

TEST(Analytics, TheKeyIsTheEuRegionOne) {
    EXPECT_EQ(std::string(analytics::kAptabaseKey), std::string("A-EU-9136752434"));
    EXPECT_TRUE(std::string(analytics::kAptabaseUrl).find("eu.aptabase.com") !=
                std::string::npos);
}

// The opt-out, proven at the seam: with the switch off the transport is
// never touched, so no socket is opened and no bytes leave the machine.
TEST(Analytics, DisabledLaunchMakesZeroPostCalls) {
    VetoScope veto;
    FakeTelemetrySink sink;
    analytics::SystemProps system;
    system.osName = "Windows";
    system.osVersion = "10.0.22631";
    system.appVersion = "0.2.1";
    system.locale = "da-DK";

    analytics::TrackAppStart(sink, system, false);

    // The number the report and the privacy promise both rest on.
    EXPECT_EQ(sink.calls.size(), static_cast<size_t>(0));
    EXPECT_EQ(static_cast<int>(sink.calls.size()), 0);
}

// A build, a test run and a capture run must not report, even with the
// setting on. CI and the smoke scripts rely on this veto, and the check lives
// inside TrackAppStart so neither port can skip it.
TEST(Analytics, EnvironmentVetoMakesZeroPostCalls) {
    VetoScope veto;
    SetEnv("MARKDOWNIT_NO_ANALYTICS", "1");

    FakeTelemetrySink sink;
    analytics::SystemProps system;
    system.osName = "macOS";
    system.osVersion = "15.1";
    system.appVersion = "0.2.1";
    system.locale = "da-DK";

    EXPECT_TRUE(analytics::IsSuppressed());
    analytics::TrackAppStart(sink, system, true);

    EXPECT_EQ(static_cast<int>(sink.calls.size()), 0);

    // With the variable gone the veto is over; the guard puts back whatever
    // the environment held when the case started.
    SetEnv("MARKDOWNIT_NO_ANALYTICS", nullptr);
    EXPECT_FALSE(analytics::IsSuppressed());
}

// DO_NOT_TRACK is the cross-tool convention, so a machine that sets it for
// every application silences this one too.
TEST(Analytics, DoNotTrackVetoMakesZeroPostCalls) {
    VetoScope veto;
    SetEnv("DO_NOT_TRACK", "1");

    FakeTelemetrySink sink;
    analytics::SystemProps system;
    system.osName = "Windows";
    system.osVersion = "10.0.22631";
    system.appVersion = "0.2.1";
    system.locale = "en-GB";

    EXPECT_TRUE(analytics::IsSuppressed());
    analytics::TrackAppStart(sink, system, true);

    EXPECT_EQ(static_cast<int>(sink.calls.size()), 0);
}

// "0" is the documented way to say "not set". A variable that carries it must
// not silence a normal launch, or an unset shell variable in a wrapper would
// stop the count everywhere.
TEST(Analytics, AZeroInTheVetoVariableDoesNotSilence) {
    VetoScope veto;
    SetEnv("MARKDOWNIT_NO_ANALYTICS", "0");
    SetEnv("DO_NOT_TRACK", "0");

    FakeTelemetrySink sink;
    analytics::SystemProps system;
    system.osName = "Windows";
    system.osVersion = "10.0.22631";
    system.appVersion = "0.2.1";
    system.locale = "en-GB";

    EXPECT_FALSE(analytics::IsSuppressed());
    analytics::TrackAppStart(sink, system, true);

    EXPECT_EQ(static_cast<int>(sink.calls.size()), 1);
}

// The send, proven at the seam: one call, to the EU endpoint, with the app
// key and the event the privacy note names.
TEST(Analytics, EnabledLaunchMakesExactlyOnePost) {
    VetoScope veto;
    FakeTelemetrySink sink;
    analytics::SystemProps system;
    system.osName = "macOS";
    system.osVersion = "15.1";
    system.appVersion = "0.2.1";
    system.locale = "da-DK";

    analytics::TrackAppStart(sink, system, true);

    EXPECT_EQ(sink.calls.size(), static_cast<size_t>(1));
    if (sink.calls.size() != 1) return;
    const FakeTelemetrySink::Call& call = sink.calls[0];
    EXPECT_EQ(call.url, std::string(analytics::kAptabaseUrl));
    EXPECT_EQ(call.appKey, std::string(analytics::kAptabaseKey));
    EXPECT_TRUE(call.body.find("\"eventName\":\"app_start\"") !=
                std::string::npos);
    EXPECT_TRUE(call.body.find("\"osName\":\"macOS\"") != std::string::npos);
    EXPECT_TRUE(call.body.find("\"osVersion\":\"15.1\"") != std::string::npos);
    // A fresh session id, 32 hex characters, and nothing derived from the
    // machine.
    const size_t idAt = call.body.find("\"sessionId\":\"");
    EXPECT_TRUE(idAt != std::string::npos);
    if (idAt != std::string::npos) {
        const size_t valueAt = idAt + std::string("\"sessionId\":\"").size();
        const size_t endAt = call.body.find('"', valueAt);
        EXPECT_TRUE(endAt != std::string::npos);
        if (endAt != std::string::npos) {
            EXPECT_EQ(endAt - valueAt, static_cast<size_t>(32));
        }
    }
}

// A failed send changes nothing the reader can see: the app already works
// without a network, so the only record is the trace log inside the
// transport and the launch carries on.
TEST(Analytics, AFailedSendIsNotRetried) {
    VetoScope veto;
    FakeTelemetrySink sink(false);
    analytics::SystemProps system;
    system.osName = "Windows";

    analytics::TrackAppStart(sink, system, true);

    EXPECT_EQ(sink.calls.size(), static_cast<size_t>(1));
}

// The payload leaves with no document in it. This is the case that fails if
// a path, a title or the text of a file is ever added to the event.
TEST(Analytics, TheSentBodyCarriesNoDocument) {
    VetoScope veto;
    FakeTelemetrySink sink;
    analytics::SystemProps system;
    system.osName = "Windows";
    system.osVersion = "10.0.22631";
    system.appVersion = "0.2.1";
    system.locale = "da-DK";

    analytics::TrackAppStart(sink, system, true);

    EXPECT_EQ(sink.calls.size(), static_cast<size_t>(1));
    if (sink.calls.empty()) return;
    const std::string& body = sink.calls[0].body;
    // No path separator, no file kind, no document field. The keys the
    // payload may carry are the nine in PRIVACY.md and no others.
    EXPECT_TRUE(body.find('/') == std::string::npos);
    EXPECT_TRUE(body.find('\\') == std::string::npos);
    EXPECT_TRUE(body.find(".md") == std::string::npos);
    EXPECT_TRUE(body.find("file") == std::string::npos);
    EXPECT_TRUE(body.find("path") == std::string::npos);
    EXPECT_TRUE(body.find("title") == std::string::npos);
    EXPECT_TRUE(body.find("text") == std::string::npos);
    EXPECT_TRUE(body.find("document") == std::string::npos);
}