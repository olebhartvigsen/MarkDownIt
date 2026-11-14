#pragma once

// Watching the open file for changes made outside the app.
//
// The mechanism is the platform's: the Windows window owns a poller thread
// that posts a window message (filewatch.h), the mac window owns a timer on
// the main queue (src/macos/filewatch_mac.h). What both ask here is the same
// question, so the policy is written once: what counts as a change, when the
// change is this app's own write coming back, and whether the page may be
// replaced without asking.

#include <cstdint>

namespace watch {

// What the disk says about a file. Both fields matter: a rewrite that keeps
// the length is still a rewrite, and a save inside the filesystem's clock
// tick keeps the time. Units are the platform's own (bytes, and the
// modification time in its finest ticks); the values are only ever compared
// with values this process read, never with a clock the app keeps.
struct Stamp {
    uint64_t size = 0;
    int64_t modified = 0;
    // False when the file could not be read at all: it was deleted, the
    // share went away, or the writer held it exclusively. An unreadable
    // file is not a changed file, and the page stays as it is.
    bool valid = false;
};

inline bool operator==(const Stamp& a, const Stamp& b) {
    if (a.valid != b.valid) return false;
    if (!a.valid) return true;
    return a.size == b.size && a.modified == b.modified;
}
inline bool operator!=(const Stamp& a, const Stamp& b) { return !(a == b); }

// What the shell should do about the file it is showing.
enum class Change {
    // Nothing new: no file, no change, or the change is the one this app
    // just made itself.
    None,
    // The buffer has no unsaved edits, so the file can be read again and
    // the reader put back where they were.
    Reload,
    // The buffer has unsaved edits. Reading the disk again throws them
    // away, so the shell asks first, and the answer decides.
    Ask,
};

// The stamp the app last read or wrote. A save is not external news, and a
// file restored from a backup or a checkout can move backwards in time, so
// the test is "differs", not "newer".
class Tracker {
public:
    // The file was read or written by us: this is the state to compare
    // against from now on.
    void Remember(const Stamp& stamp) { seen_ = stamp; }
    // No file is open any more.
    void Forget() { seen_ = Stamp{}; }

    const Stamp& seen() const { return seen_; }
    bool Watching() const { return seen_.valid; }

    // The disk reports `now`, and the buffer does or does not hold unsaved
    // edits.
    Change Observe(const Stamp& now, bool hasUnsavedEdits) const {
        if (!now.valid || !seen_.valid) return Change::None;
        if (now == seen_) return Change::None;
        return hasUnsavedEdits ? Change::Ask : Change::Reload;
    }

    // A change that has been answered stops being news: after the reload,
    // or after choosing to keep the page as it is, the same change must not
    // ask again.
    void Accept(const Stamp& now) { seen_ = now; }

private:
    Stamp seen_;
};

}  // namespace watch
