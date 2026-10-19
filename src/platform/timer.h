// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

namespace km {
namespace platform {

// Sleeps for `duration` with roughly millisecond precision.
//
// std::this_thread::sleep_for() is quantised to the system timer tick, which is
// about 15.6 ms unless some other process has raised the resolution. A loop
// that asks for 2 ms and is given 15 ms runs several times slower than it
// believes it does, so anything it integrates against a fixed timestep comes
// out wrong. This uses a high-resolution waitable timer where the OS has one
// (Windows 10 1803 and later) and falls back to sleep_for() with a raised
// timer resolution elsewhere.
//
// The timer is per thread, created on first use and closed at thread exit.
void sleep_for(std::chrono::nanoseconds duration);

}  // namespace platform
}  // namespace km
