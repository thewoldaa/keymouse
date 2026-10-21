// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/router.h"

#include <algorithm>

namespace km {

void Router::set_bindings(const std::vector<Binding>& bindings, KeyId fn_layer_key) {
    (void)fn_layer_key;
    bindings_.clear();
    bindings_.reserve(bindings.size());
    for (const auto& binding : bindings) {
        if (!binding.valid()) {
            continue;
        }
        Entry entry;
        entry.action = binding.action;
        entry.mode = binding.mode;
        entry.swallow = binding.swallow;
        bindings_[make_key(binding.chord.mods, binding.chord.key)] = entry;
    }
}

RouteResult Router::on_key_down(KeyId key, ModMask mods) {
    RouteResult result;

    // Re-pressing a key that is already down (auto-repeat) must not retrigger
    // a tap or toggle action. Windows sends repeated WM_KEYDOWN for held keys.
    if (held_keys_.contains(key)) {
        const auto it = bindings_.find(make_key(held_keys_[key], key));
        if (it != bindings_.end()) {
            result.swallow = it->second.swallow == SwallowMode::Swallow;
            result.action = it->second.action;
            result.mode = it->second.mode;
            result.active = true;
        }
        return result;
    }
    held_keys_[key] = mods;

    const auto it = bindings_.find(make_key(mods, key));
    if (it == bindings_.end()) {
        return result;
    }

    const Entry& entry = it->second;
    result.action = entry.action;
    result.swallow = entry.swallow == SwallowMode::Swallow;
    result.mode = entry.mode;

    switch (entry.mode) {
        case BindMode::Hold: {
            // Reference count by action, not by key: a second chord bound to
            // the same action is a no-op state-wise while the first still
            // holds it.
            const bool was_active = is_action_active(entry.action);
            active_[make_key(mods, key)] = entry.action;
            result.active = true;
            result.changed = !was_active;
            break;
        }

        case BindMode::Tap:
            // The action fires once on press. `active` stays false so the
            // engine treats it as an instantaneous effect.
            result.active = false;
            result.changed = true;
            break;

        case BindMode::Toggle: {
            // A toggle and a hold can share an action. Turning the toggle off
            // must not stop the action while another key still holds it, so
            // the result reports the state after the flip rather than the
            // toggle's own value.
            const bool was_active = is_action_active(entry.action);
            bool& state = toggled_[static_cast<uint16_t>(entry.action)];
            state = !state;
            const bool now_active = is_action_active(entry.action);
            result.active = now_active;
            result.changed = now_active != was_active;
            break;
        }
    }
    return result;
}

RouteResult Router::on_key_up(KeyId key) {
    RouteResult result;

    const auto held = held_keys_.find(key);
    if (held == held_keys_.end()) {
        return result;
    }
    const ModMask mods = held->second;
    held_keys_.erase(held);

    const auto it = bindings_.find(make_key(mods, key));
    if (it == bindings_.end()) {
        return result;
    }

    const Entry& entry = it->second;
    result.action = entry.action;
    result.swallow = entry.swallow == SwallowMode::Swallow;
    result.mode = entry.mode;

    if (entry.mode == BindMode::Hold) {
        active_.erase(make_key(mods, key));
        // Another chord may still hold the same action; if it does, nothing
        // changed and the engine must keep the action running.
        const bool still_active = is_action_active(entry.action);
        result.active = still_active;
        result.changed = !still_active;
    }
    // Tap and Toggle ignore key-up: tap already fired, toggle keeps its state.
    return result;
}

std::vector<Action> Router::release_all() {
    std::vector<Action> released;
    released.reserve(active_.size() + toggled_.size());

    for (const auto& [key, action] : active_) {
        released.push_back(action);
    }
    for (const auto& [action_value, state] : toggled_) {
        if (state) {
            released.push_back(static_cast<Action>(action_value));
        }
    }

    std::sort(released.begin(), released.end());
    released.erase(std::unique(released.begin(), released.end()), released.end());

    active_.clear();
    toggled_.clear();
    // held_keys_ is deliberately kept. It records which physical keys are down,
    // not which actions are running: dropping it would make the next OS
    // auto-repeat of a still-held Tap or Toggle key look like a fresh press and
    // re-fire the action. The key's own on_key_up() removes it, and reset()
    // clears it on a session change.
    return released;
}

void Router::forget_held_keys() {
    held_keys_.clear();
}

bool Router::any_active() const {    if (!active_.empty()) {
        return true;
    }
    for (const auto& [action_value, state] : toggled_) {
        if (state) {
            return true;
        }
    }
    return false;
}

bool Router::is_action_active(Action action) const {
    for (const auto& [key, held_action] : active_) {
        if (held_action == action) {
            return true;
        }
    }
    const auto it = toggled_.find(static_cast<uint16_t>(action));
    return it != toggled_.end() && it->second;
}

std::vector<Action> Router::active_actions() const {
    std::vector<Action> actions;
    actions.reserve(active_.size());
    for (const auto& [key, action] : active_) {
        actions.push_back(action);
    }
    for (const auto& [action_value, state] : toggled_) {
        if (state) {
            actions.push_back(static_cast<Action>(action_value));
        }
    }
    std::sort(actions.begin(), actions.end());
    actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
    return actions;
}

void Router::reset() {
    bindings_.clear();
    held_keys_.clear();
    active_.clear();
    toggled_.clear();
}

}  // namespace km
