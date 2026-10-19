// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "core/action.h"
#include "core/chord.h"

namespace km {

// How a chord behaves when pressed.
enum class BindMode : uint8_t {
    // Action runs while the chord is held. Movement, scroll, precision, turbo.
    Hold = 0,
    // First press starts the action, second press stops it. Drag lock.
    Toggle = 1,
    // One press performs the action exactly once.
    Tap = 2,
};

// What happens to the original key press.
enum class SwallowMode : uint8_t {
    // Let the key reach the foreground application as well. Needed when the
    // chord is also a game binding the user wants to keep working.
    PassThrough = 0,
    // Consume the key so it does not reach the application. Default.
    Swallow = 1,
};

struct Binding {
    Action action = Action::None;
    Chord chord;
    BindMode mode = BindMode::Hold;
    SwallowMode swallow = SwallowMode::Swallow;

    bool valid() const { return action != Action::None && chord.valid(); }
};

std::string_view bind_mode_name(BindMode mode);
BindMode bind_mode_from_name(std::string_view name);

std::string_view swallow_mode_name(SwallowMode mode);
SwallowMode swallow_mode_from_name(std::string_view name);

}  // namespace km
