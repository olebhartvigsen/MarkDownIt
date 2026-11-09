// The per-platform service contract of the shared code (plan 4.1).
//
// The document controller and the command bodies that follow it are
// portable: they know the buffer, the selection and the undo stack,
// and nothing about a window. The few things they need from the
// platform are named here.
//
// This contract carries one service: the undo clock. UndoStack
// coalesces typing within 800 ms, and the stamps of the undo entries
// decide what merges (undo stack guide). The implementation lives
// with the controller in documentcontroller.cpp, so it links wherever
// the controller links: the app targets, the test targets and the
// parity runners. Both ports read the same std::chrono steady clock,
// which is monotonic on every platform; GetTickCount64 no longer has
// a part in it, and the 800 ms rule measures the same time everywhere.

#pragma once

#include <cstdint>

namespace platform {

// Milliseconds on a clock that never moves backwards. Both ports
// answer with std::chrono::steady_clock (documentcontroller.cpp).
uint64_t GetMonotonicMs();

}  // namespace platform
