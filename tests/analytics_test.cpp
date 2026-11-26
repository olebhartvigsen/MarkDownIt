#include "analytics.h"
#include "gtest_lite.h"

#include <string>

// The payload contract, pinned. These tests exist because the thing being
// built here leaves the machine: what it says has to be exactly what
// PRIVACY.md promises, and nothing else.

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
