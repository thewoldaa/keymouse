// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace km {
namespace platform {

// Short, non-blocking system sound used as feedback when mouse mode changes.
// Safe to call from the UI thread.
void beep();

}  // namespace platform
}  // namespace km
