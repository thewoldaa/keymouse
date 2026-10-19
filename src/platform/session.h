// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace km {

// Session change notifications.
//
// Locking the workstation or answering a UAC prompt switches to another desktop.
// Windows stops delivering to low-level hooks while that lasts and, depending
// on the driver and the security software in between, may not resume cleanly on
// return. KeyMouse re-arms its hook on every unlock, and drops every held
// action on lock so a key held across the switch does not keep moving the
// cursor on the way back.
namespace session {

enum class SessionEvent {
    Locked,
    Unlocked,
    Other,
};

// Called on the UI thread, from the message loop.
using SessionCallback = void (*)(SessionEvent);

// Registers for session notifications on the given window.
bool subscribe(void* window_handle, SessionCallback callback);

void unsubscribe(void* window_handle);

// Dispatches a WM_WTSSESSION_CHANGE payload. Called by the window procedure
// with the message's wParam.
void handle_session_change(unsigned long event);

}  // namespace session
}  // namespace km
