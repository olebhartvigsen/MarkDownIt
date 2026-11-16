// The JSON reader and pretty-printer
// (plan: .hermes/plans/2026-11-16_140000-json-viewer-support.md).

#include "json.h"
#include "gtest_lite.h"

#include <string>

TEST(Json, ParsesAllSixKinds) {
    auto r = json::Parse(
        "{\"a\":null,\"b\":true,\"c\":false,\"d\":1,\"e\":\"s\",\"f\":[1,2]}");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.root.kind, json::Kind::Object);
    EXPECT_EQ(r.root.object.size(), 6u);
    EXPECT_EQ(r.root.object[0].second.kind, json::Kind::Null);
    EXPECT_TRUE(r.root.object[1].second.boolValue);
    EXPECT_FALSE(r.root.object[2].second.boolValue);
    EXPECT_EQ(r.root.object[3].second.text, std::string("1"));
    EXPECT_EQ(r.root.object[4].second.text, std::string("s"));
    EXPECT_EQ(r.root.object[5].second.kind, json::Kind::Array);
    EXPECT_EQ(r.root.object[5].second.array.size(), 2u);
}

TEST(Json, NestedStructures) {
    auto r = json::Parse("{\"a\":{\"b\":[1,{\"c\":2}]}}");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.root.object[0].second.object[0].second.array[1]
                  .object[0].second.text, std::string("2"));
}

TEST(Json, StringEscapes) {
    auto r = json::Parse("\"a\\\"b\\\\c\\nd\\u0041\"");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.root.kind, json::Kind::String);
    EXPECT_EQ(r.root.text, std::string("a\"b\\c\nd" "A"));
}

TEST(Json, SurrogatePairEscape) {
    // U+1F600 GRINNING FACE, as a surrogate pair.
    auto r = json::Parse("\"\\ud83d\\ude00\"");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.root.text, std::string("\xF0\x9F\x98\x80"));
}

TEST(Json, NumberForms) {
    auto r1 = json::Parse("123");
    EXPECT_TRUE(r1.ok);
    EXPECT_EQ(r1.root.text, std::string("123"));
    auto r2 = json::Parse("-12.5");
    EXPECT_TRUE(r2.ok);
    EXPECT_EQ(r2.root.text, std::string("-12.5"));
    auto r3 = json::Parse("1.5e10");
    EXPECT_TRUE(r3.ok);
    EXPECT_EQ(r3.root.text, std::string("1.5e10"));
    auto r4 = json::Parse("0");
    EXPECT_TRUE(r4.ok);
}

TEST(Json, MaxDepthExceededIsReportedNotCrashed) {
    std::string deep;
    for (int i = 0; i < 100; i++) deep += "[";
    for (int i = 0; i < 100; i++) deep += "]";
    auto r = json::Parse(deep);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.truncated);
}

TEST(Json, MaxBytesExceededIsReportedNotCrashed) {
    std::string huge(json::kMaxBytes + 10, ' ');
    huge = "[" + huge + "]";
    auto r = json::Parse(huge);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.truncated);
}

TEST(Json, MalformedMissingColonReportsLineAndColumn) {
    auto r = json::Parse("{\"a\"}");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(Json, TrailingCommaIsRejected) {
    EXPECT_FALSE(json::Parse("[1,2,]").ok);
    EXPECT_FALSE(json::Parse("{\"a\":1,}").ok);
}

TEST(Json, UnterminatedStringIsRejected) {
    EXPECT_FALSE(json::Parse("\"abc").ok);
}

TEST(Json, TrailingDataAfterValueIsRejected) {
    EXPECT_FALSE(json::Parse("1 2").ok);
}

TEST(Json, PrettyPrintRoundTrips) {
    auto r1 = json::Parse("{\"b\":1,\"a\":[1,2,{\"x\":true}],\"c\":null}");
    EXPECT_TRUE(r1.ok);
    std::string pretty = json::PrettyPrint(r1.root);
    auto r2 = json::Parse(pretty);
    EXPECT_TRUE(r2.ok);
    // Key order is kept, not resorted.
    EXPECT_EQ(r2.root.object[0].first, std::string("b"));
    EXPECT_EQ(r2.root.object[1].first, std::string("a"));
    EXPECT_EQ(r2.root.object[2].first, std::string("c"));
}

TEST(Json, PrettyPrintEmptyContainers) {
    auto r = json::Parse("{\"a\":[],\"b\":{}}");
    EXPECT_TRUE(r.ok);
    std::string pretty = json::PrettyPrint(r.root);
    EXPECT_TRUE(pretty.find("[]") != std::string::npos);
    EXPECT_TRUE(pretty.find("{}") != std::string::npos);
}

TEST(Json, PrettyPrintIsTwoSpaceIndented) {
    auto r = json::Parse("{\"a\":1}");
    EXPECT_TRUE(r.ok);
    std::string pretty = json::PrettyPrint(r.root);
    EXPECT_EQ(pretty, std::string("{\n  \"a\": 1\n}\n"));
}
