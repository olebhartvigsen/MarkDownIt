#include "gtest_lite.h"
#include "diagramcache.h"
#include "mermaid/layout.h"
#include "mermaid/seq_parse.h"

#include <string>
#include <type_traits>

// Both engines are linked on Windows. Their types must have distinct names
// so the linker cannot use one engine's vector operations for the other.
static_assert(!std::is_same_v<mermaid::legacy::SeqMessage, mermaid::SeqMessage>);
static_assert(!std::is_same_v<mermaid::legacy::NodeShape, mermaid::NodeShape>);
static_assert(!std::is_same_v<mermaid::legacy::MeasureFn, mermaid::MeasureFn>);

TEST(MermaidLegacy, SequenceModelsCoexist) {
    const std::string source =
        "sequenceDiagram\n"
        "participant Alice\n"
        "participant Bob\n"
        "Alice->>Bob: Hello\n"
        "Bob-->>Alice: Reply\n";

    for (int i = 0; i < 8; ++i) {
        auto legacy = mermaid::legacy::Parse(source);
        auto current = mermaid::ParseSequence(source);
        ASSERT_TRUE(legacy.error.empty());
        ASSERT_TRUE(current.error.empty());
        ASSERT_EQ(legacy.messages.size(), 2u);
        ASSERT_EQ(current.messages.size(), 2u);
        ASSERT_EQ(current.participants.size(), 2u);
        EXPECT_EQ(legacy.messages[0].label, "Hello");
        EXPECT_EQ(current.messages[0].text, "Hello");
        EXPECT_TRUE(mermaid::MsgIsDotted(current.messages[1].type));
        EXPECT_EQ(current.participants[0].display, "Alice");
    }
}
