// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace km {

// Every effect KeyMouse can perform. The enum is the serialized identifier,
// so values must stay stable across releases; append new actions at the end.
enum class Action : uint16_t {
    None = 0,

    // Movement (hold while the chord is down)
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,

    // Buttons
    ClickLeft,
    ClickRight,
    ClickMiddle,
    ClickX1,
    ClickX2,
    DoubleClick,

    // Dragging: holds the left button down until released
    DragLock,

    // Wheel
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,

    // Movement modifiers
    Precision,
    Turbo,

    // Cursor placement
    CursorCenter,

    // Application control
    ToggleEnabled,
    OpenDashboard,
    SpeedUp,
    SpeedDown,

    Count
};

// Stable string identifiers used in the config file.
std::string_view action_name(Action action);

// Parses a name produced by action_name(). Unknown names return Action::None.
Action action_from_name(std::string_view name);

// True for actions that stay active while the chord is held (movement,
// precision, turbo). False for one-shot actions.
bool action_is_continuous(Action action);

// True for actions that are meaningful in the bindings UI.
bool action_is_bindable(Action action);

}  // namespace km
