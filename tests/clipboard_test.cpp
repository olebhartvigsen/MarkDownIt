#include "gtest_lite.h"
#include "clipboard.h"
#include <string>

TEST(EscapeMarkdown, EscapesAsterisk) {
    EXPECT_EQ(EscapeMarkdown("hello *world"), "hello \\*world");
}

TEST(EscapeMarkdown, EscapesUnderscore) {
    EXPECT_EQ(EscapeMarkdown("hello_world"), "hello\\_world");
}

TEST(EscapeMarkdown, EscapesBacktick) {
    EXPECT_EQ(EscapeMarkdown("code `here`"), "code \\`here\\`");
}

TEST(EscapeMarkdown, DoesNotEscapeNormalChars) {
    EXPECT_EQ(EscapeMarkdown("hello world"), "hello world");
}

TEST(HtmlToMarkdown, BoldAndItalic) {
    std::string result = HtmlToMarkdown("<b>bold</b> and <i>italic</i>");
    EXPECT_EQ(result, "**bold** and *italic*");
}

TEST(HtmlToMarkdown, CodeAndPre) {
    std::string result = HtmlToMarkdown("`code`");
    EXPECT_EQ(result, "`code`");
}

TEST(HtmlToMarkdown, Heading) {
    std::string result = HtmlToMarkdown("<h2>Title</h2>");
    EXPECT_EQ(result, "## Title\n\n");
}

TEST(HtmlToMarkdown, List) {
    std::string result = HtmlToMarkdown("- item1\n- item2");
    EXPECT_EQ(result, "- item1\n- item2");
}

TEST(HtmlToMarkdown, Strikethrough) {
    std::string result = HtmlToMarkdown("<del>text</del>");
    EXPECT_EQ(result, "~~text~~");
}

TEST(HtmlToMarkdown, Entities) {
    std::string result = HtmlToMarkdown("a &amp; b &lt;tag&gt;");
    EXPECT_EQ(result, "a & b <tag>");
}

TEST(HtmlToMarkdown, Paragraph) {
    std::string result = HtmlToMarkdown("<p>first</p><p>second</p>");
    // Should have paragraph breaks between them.
    EXPECT_TRUE(result.find("first") != std::string::npos);
    EXPECT_TRUE(result.find("second") != std::string::npos);
    EXPECT_TRUE(result.find("\n\n") != std::string::npos);
}

TEST(HtmlToMarkdown, Blockquote) {
    std::string result = HtmlToMarkdown("<blockquote>quoted text</blockquote>");
    EXPECT_TRUE(result.find("> ") != std::string::npos);
    EXPECT_TRUE(result.find("quoted text") != std::string::npos);
}

TEST(HtmlToMarkdown, Hr) {
    std::string result = HtmlToMarkdown("text<br>more");
    EXPECT_EQ(result, "text\nmore");
}
