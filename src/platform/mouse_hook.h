// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/recorder.h"

namespace km {
namespace platform {

// Called on the hook thread for every mouse event while the hook is running.
// It must not allocate, lock, or block; Windows removes hooks whose callback
// takes too long.
using MouseEventSink = void (*)(const MacroInputEvent& event);

// Installs WH_MOUSE_LL on a dedicated message-pumping thread. Only one hook can
// exist per process, and it is installed on demand (recording or playback) so
// the application carries no mouse hook while idle. Returns false when the
// hook could not be installed.
bool start_mouse_hook(MouseEventSink sink);
void stop_mouse_hook();
bool mouse_hook_running();

}  // namespace platform
}  // namespace km
