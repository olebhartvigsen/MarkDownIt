// The command table is the one place a command is named. The Windows
// ribbon, the mac menu bar and the mac format bar all read these rows,
// so a duplicate name, a selector no item can send or a heading level
// the mac side cannot reach is a real defect and not a matter of taste.

#include "commands.h"

#include <cstddef>
#include <cstring>
#include <set>
#include <string>

#include "gtest_lite.h"

namespace {

bool HasText(const char* text) { return text && text[0] != '\0'; }

}  // namespace

TEST(CommandTable, EveryRowIsUnique) {
    std::set<int> ids;
    std::set<std::string> names;
    std::set<std::string> selectors;
    const std::size_t count = sizeof(commands::kCommands) /
                              sizeof(commands::kCommands[0]);
    for (std::size_t i = 0; i < count; ++i) {
        const commands::CommandSpec& row = commands::kCommands[i];
        EXPECT_TRUE(ids.insert(static_cast<int>(row.id)).second);
        EXPECT_TRUE(names.insert(row.name).second);
        if (HasText(row.selector)) {
            // Two rows with one selector would make the responder chain
            // answer for whichever the table reached first.
            EXPECT_TRUE(selectors.insert(row.selector).second);
        }
    }
    EXPECT_EQ(count, ids.size());
}

TEST(CommandTable, EveryMacSelectorIsAnAction) {
    const std::size_t count = sizeof(commands::kCommands) /
                              sizeof(commands::kCommands[0]);
    for (std::size_t i = 0; i < count; ++i) {
        const commands::CommandSpec& row = commands::kCommands[i];
        if (!HasText(row.selector)) continue;
        const std::string selector = row.selector;
        EXPECT_EQ(selector.back(), ':');
        EXPECT_TRUE(commands::FindBySelector(row.selector) != nullptr);
    }
}

TEST(CommandTable, BodyTextLeavesAHeading) {
    // The ribbon has no button that removes a heading; the mac style
    // popup names body text directly, so the row is the document's one
    // way back to a plain paragraph.
    const commands::CommandSpec* body =
        commands::Find(commands::CommandId::BodyText);
    EXPECT_TRUE(body != nullptr);
    if (!body) return;
    EXPECT_TRUE(body->ribbon == nullptr);
    EXPECT_EQ(std::strcmp(body->selector, "setBodyText:"), 0);
    EXPECT_TRUE(body->mutating);
}

TEST(CommandTable, EveryHeadingLevelHasAMacCommand) {
    const commands::CommandId levels[] = {
        commands::CommandId::Heading1, commands::CommandId::Heading2,
        commands::CommandId::Heading3, commands::CommandId::Heading4,
        commands::CommandId::Heading5, commands::CommandId::Heading6,
    };
    for (commands::CommandId id : levels) {
        const commands::CommandSpec* row = commands::Find(id);
        EXPECT_TRUE(row != nullptr);
        if (!row) continue;
        EXPECT_TRUE(HasText(row->selector));
        EXPECT_TRUE(row->mutating);
        // The Windows side reaches every level, three on the ribbon and
        // three through the accelerators.
        EXPECT_TRUE(row->win.vk != 0);
    }
}

TEST(CommandTable, TheBlockCommandsCarryBothKeySets) {
    const commands::CommandId blocks[] = {
        commands::CommandId::Bullets, commands::CommandId::Numbering,
        commands::CommandId::Quote,
    };
    for (commands::CommandId id : blocks) {
        const commands::CommandSpec* row = commands::Find(id);
        EXPECT_TRUE(row != nullptr);
        if (!row) continue;
        // A list is bound on both windows, so a reader who knows one
        // window knows the other.
        EXPECT_TRUE(row->win.vk != 0);
        EXPECT_TRUE(HasText(row->mac.key));
        EXPECT_EQ(std::strlen(row->mac.key), static_cast<std::size_t>(1));
        EXPECT_TRUE(row->mac.shift);
    }
}
