// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/action.h"
#include "core/binding.h"
#include "core/chord.h"

namespace km {

// What the engine should do in response to a key event.
struct RouteResult {
    // True when the key event must be consumed and not passed to the
    // foreground application.
    bool swallow = false;
    // Action that became active (Hold) or was fired (Tap/Toggle).
    Action action = Action::None;
    // True when the action should now be considered active; false when it
    // just stopped.
    bool active = false;
    // True when this is a state change worth notifying listeners about.
    bool changed = false;
    // The binding mode that produced this result. The engine needs it to tell
    // an instantaneous Tap from a Hold/Toggle state transition: a click bound
    // in Hold mode must press the button and keep it down, not fire a tap.
    BindMode mode = BindMode::Hold;
};

// Resolves physical key events into actions.
//
// The router holds the "what is currently pressed" state. It is not thread
// safe: it lives on the engine thread and receives events copied out of the
// hook's ring buffer.
class Router {
public:
    // Rebuilds the lookup table. Cheap enough to call on config reload
    // (a few hundred entries at most).
    void set_bindings(const std::vector<Binding>& bindings, KeyId fn_layer_key);

    // A key went down. Returns the action to start, if any.
    RouteResult on_key_down(KeyId key, ModMask mods);

    // A key went up.
    RouteResult on_key_up(KeyId key);

    // Releases every held action. Called on focus loss, disable, or shutdown.
    // Returns the actions that were released so the engine can undo their
    // effects (for example, releasing a drag lock).
    std::vector<Action> release_all();

    // True when at least one action is currently held or toggled on.
    bool any_active() const;

    // True when `action` is held by any key or toggled on. Used for reference
    // counting: two chords may be bound to the same action, and releasing one
    // must not stop it while the other is still down.
    bool is_action_active(Action action) const;

    // Snapshot of the active action set, for the dashboard.
    std::vector<Action> active_actions() const;

    // Clears all state without emitting actions. Used when reloading config.
    void reset();

private:
    struct Entry {
        Action action = Action::None;
        BindMode mode = BindMode::Hold;
        SwallowMode swallow = SwallowMode::Swallow;
    };

    static uint32_t make_key(ModMask mods, KeyId key) {
        return (static_cast<uint32_t>(mods) << 16) | key;
    }

    // Bindings indexed by (mods << 16 | key).
    std::unordered_map<uint32_t, Entry> bindings_;
    // Physical keys currently held, with the modifier mask that was active
    // when they went down. Needed because the modifier may be released before
    // the base key. A ModMask rather than the packed form, so the value can go
    // straight back into make_key without narrowing.
    std::unordered_map<KeyId, ModMask> held_keys_;
    // Held keys mapped to their action. Strictly Hold bindings: a toggled
    // action lives in toggled_ only, so toggling it off from a second chord
    // cannot leave a stale entry behind here.
    std::unordered_map<uint32_t, Action> active_;
    // Toggle state, keyed by action. Shared by every chord bound to it.
    std::unordered_map<uint16_t, bool> toggled_;
};

}  // namespace km
