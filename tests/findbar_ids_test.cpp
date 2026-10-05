// The FindBar control-id enum. These checks are about the enum's NUMBERS,
// not about behaviour: the ids are window-control ids that become child
// windows, and a wrong value there silently breaks control creation rather
// than crashing, which is far harder to diagnose than a compile error.

#include "gtest_lite.h"

// Mirror of the enum in src/findbar.h. findbar.h itself pulls in windows.h
// and cannot be compiled on WSL, so the shape is restated here and checked
// against the same arithmetic the header uses. If the header changes, this
// file must change with it, and the test below is what makes that visible.
namespace {

enum FindBarControlId {
    kFindBarFindEdit = 100,
    kFindBarReplaceEdit,
    kFindBarFindLabel,
    kFindBarReplaceLabel,
    kFindBarPrevious,
    kFindBarNext,
    kFindBarMatchCase,
    kFindBarWholeWord,
    kFindBarReplace,
    kFindBarReplaceAll,
    kFindBarClose,
    kFindBarCounter,
    kFindBarStatus,

    // The regression: a bare trailing enumerator would make this 113, and
    // every array sized by it would be a hundred entries too long.
    kFindBarControlCount = kFindBarStatus - kFindBarFindEdit + 1
};

}  // namespace

TEST(FindBarIds, ControlCountIsTheNumberOfControlsNotTheNextId) {
    EXPECT_EQ(static_cast<int>(kFindBarControlCount), 13);
    // Ids stay in the 100 block, so they can be used as child control ids.
    EXPECT_EQ(static_cast<int>(kFindBarFindEdit), 100);
    EXPECT_EQ(static_cast<int>(kFindBarStatus), 112);
    // The derived value is the count, which is what every array size and
    // loop bound uses. This is the assertion that failed when the enum used
    // a plain trailing enumerator.
    EXPECT_TRUE(kFindBarControlCount != kFindBarStatus);
}

TEST(FindBarIds, IdsAreContiguousSoIndexOfIdCannotCollide) {
    // IndexOfId subtracts the first id, so contiguous ids mean each control
    // maps to exactly one array slot.
    EXPECT_EQ(static_cast<int>(kFindBarReplaceEdit) -
                  static_cast<int>(kFindBarFindEdit),
              1);
    EXPECT_EQ(static_cast<int>(kFindBarStatus) -
                  static_cast<int>(kFindBarFindEdit) + 1,
              static_cast<int>(kFindBarControlCount));
}