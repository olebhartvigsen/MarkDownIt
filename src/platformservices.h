// The per-platform service contract of the shared code (plan 4.1).
//
// The document controller and the command bodies that follow it are
// portable: they know the buffer, the selection and the undo stack,
// and nothing about a window. The few things they need from the
// platform are named here, and each port implements them inline in
// its app-side translation unit, so no new build files are needed
// and the mac and Windows linkers keep their own lists.
//
// This wave needs one service: the undo clock. UndoStack coalesces
// typing within 800 ms, and the stamps of the undo entries decide
// what merges (undo stack guide). Today the two ports stamp
// differently: Windows stamps GetTickCount64 and the mac port
// stamps zero, which turned the 800 ms rule off (platform-parity,
// divergence 3). The controller stamps every entry it builds through
// this service, so both ports read the same monotonic clock.

#pragma once

#include <cstdint>

namespace platform {

// Milliseconds on a clock that never moves backwards. Windows
// implements it with GetTickCount64; the mac port implements it with
// the steady clock.
uint64_t GetMonotonicMs();

}  // namespace platform
