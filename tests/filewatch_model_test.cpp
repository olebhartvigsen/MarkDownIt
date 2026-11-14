#include "gtest_lite.h"
#include "../src/filewatch_model.h"

namespace {

watch::Stamp File(uint64_t size, int64_t modified) {
    watch::Stamp stamp;
    stamp.size = size;
    stamp.modified = modified;
    stamp.valid = true;
    return stamp;
}

}  // namespace

TEST(FileWatch, StampsCompareOnBothFields) {
    EXPECT_TRUE(File(10, 100) == File(10, 100));
    // A rewrite that keeps the length is still a rewrite.
    EXPECT_TRUE(File(10, 100) != File(10, 101));
    // A save inside the filesystem's clock tick keeps the time.
    EXPECT_TRUE(File(10, 100) != File(11, 100));
    // An unreadable file is not a changed file.
    EXPECT_TRUE(watch::Stamp{} == watch::Stamp{});
    EXPECT_TRUE(watch::Stamp{} != File(0, 0));
}

TEST(FileWatch, NoFileIsNotWatched) {
    watch::Tracker tracker;
    EXPECT_FALSE(tracker.Watching());
    // Nothing has been read, so nothing can have changed.
    EXPECT_TRUE(tracker.Observe(File(10, 100), false) == watch::Change::None);
    EXPECT_TRUE(tracker.Observe(watch::Stamp{}, false) == watch::Change::None);
}

TEST(FileWatch, TheSameFileIsNotAChange) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    EXPECT_TRUE(tracker.Watching());
    EXPECT_TRUE(tracker.Observe(File(10, 100), false) == watch::Change::None);
    // The dirty flag says nothing about a file that did not change.
    EXPECT_TRUE(tracker.Observe(File(10, 100), true) == watch::Change::None);
}

TEST(FileWatch, AChangedFileReloadsWhenTheBufferIsClean) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    EXPECT_TRUE(tracker.Observe(File(20, 200), false) == watch::Change::Reload);
    // After the reload the new stamp is the one to compare against.
    tracker.Accept(File(20, 200));
    EXPECT_TRUE(tracker.Observe(File(20, 200), false) == watch::Change::None);
}

TEST(FileWatch, AChangedFileAsksWhenTheBufferHasEdits) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    EXPECT_TRUE(tracker.Observe(File(20, 200), true) == watch::Change::Ask);
}

TEST(FileWatch, KeepingThePageStopsTheQuestion) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    const watch::Stamp now = File(20, 200);
    EXPECT_TRUE(tracker.Observe(now, true) == watch::Change::Ask);
    // The reader said no: the same change must not ask again on the next
    // poll, but a newer change must.
    tracker.Accept(now);
    EXPECT_TRUE(tracker.Observe(now, true) == watch::Change::None);
    EXPECT_TRUE(tracker.Observe(File(20, 300), true) == watch::Change::Ask);
}

TEST(FileWatch, OwnWritesAreNotNews) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    // Save wrote the file: the app remembers the stamp it left behind, so
    // the poller reporting it back is not a change.
    tracker.Remember(File(30, 300));
    EXPECT_TRUE(tracker.Observe(File(30, 300), false) == watch::Change::None);
}

TEST(FileWatch, AFileThatMovesBackInTimeStillReloads) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 500));
    // A checkout or a restore from a backup: older, and still news.
    EXPECT_TRUE(tracker.Observe(File(10, 400), false) == watch::Change::Reload);
}

TEST(FileWatch, ADeletedFileChangesNothing) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    // The stat failed, so the disk says nothing. The page keeps its text
    // and the next save writes the file again.
    EXPECT_TRUE(tracker.Observe(watch::Stamp{}, false) == watch::Change::None);
    EXPECT_TRUE(tracker.Observe(watch::Stamp{}, true) == watch::Change::None);
    // The stale stamp is still the one to compare against once it returns.
    EXPECT_TRUE(tracker.Observe(File(10, 100), false) == watch::Change::None);
    EXPECT_TRUE(tracker.Observe(File(11, 100), false) == watch::Change::Reload);
}

TEST(FileWatch, ForgetEndsTheWatch) {
    watch::Tracker tracker;
    tracker.Remember(File(10, 100));
    tracker.Forget();
    EXPECT_FALSE(tracker.Watching());
    // An untitled document has no file, so nothing on disk is its news.
    EXPECT_TRUE(tracker.Observe(File(20, 200), true) == watch::Change::None);
}
