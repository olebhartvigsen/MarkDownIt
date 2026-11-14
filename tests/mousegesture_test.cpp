#include "gtest_lite.h"
#include "mousegesture.h"

// The rule the two shells share: a press the window answers itself owns
// the whole mouse sequence.

TEST(MouseGesture, APressThatOpensADocumentOwnsTheRestOfItsSequence) {
    MouseGesture gesture;
    gesture.OnPress();
    // A plain press starts a selection gesture: the document may read
    // the movement that follows.
    EXPECT_FALSE(gesture.Consumed());

    // The press opened a document from the welcome screen instead.
    gesture.Consume();
    EXPECT_TRUE(gesture.Consumed());

    // The release ends it, and the next press starts clean.
    gesture.OnRelease();
    EXPECT_FALSE(gesture.Consumed());
}

TEST(MouseGesture, ANewPressClearsAConsumptionWhoseReleaseNeverCame) {
    MouseGesture gesture;
    gesture.OnPress();
    gesture.Consume();
    // A dialog took the release, so the flag is still set; the next
    // press must not inherit it.
    gesture.OnPress();
    EXPECT_FALSE(gesture.Consumed());
}
