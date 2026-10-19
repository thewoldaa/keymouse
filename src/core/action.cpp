// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/action.h"

#include <array>

namespace km {
namespace {

struct ActionName {
    Action action;
    std::string_view name;
    bool continuous;
};

// Append only: these strings are persisted in user config files.
constexpr std::array<ActionName, static_cast<size_t>(Action::Count)> kActionNames{{
    {Action::None, "none", false},
    {Action::MoveUp, "move_up", true},
    {Action::MoveDown, "move_down", true},
    {Action::MoveLeft, "move_left", true},
    {Action::MoveRight, "move_right", true},
    {Action::ClickLeft, "click_left", false},
    {Action::ClickRight, "click_right", false},
    {Action::ClickMiddle, "click_middle", false},
    {Action::ClickX1, "click_x1", false},
    {Action::ClickX2, "click_x2", false},
    {Action::DoubleClick, "double_click", false},
    {Action::DragLock, "drag_lock", false},
    {Action::ScrollUp, "scroll_up", true},
    {Action::ScrollDown, "scroll_down", true},
    {Action::ScrollLeft, "scroll_left", true},
    {Action::ScrollRight, "scroll_right", true},
    {Action::Precision, "precision", true},
    {Action::Turbo, "turbo", true},
    {Action::CursorCenter, "cursor_center", false},
    {Action::ToggleEnabled, "toggle_enabled", false},
    {Action::OpenDashboard, "open_dashboard", false},
    {Action::SpeedUp, "speed_up", false},
    {Action::SpeedDown, "speed_down", false},
}};

}  // namespace

std::string_view action_name(Action action) {
    const auto index = static_cast<size_t>(action);
    if (index < kActionNames.size()) {
        return kActionNames[index].name;
    }
    return "none";
}

Action action_from_name(std::string_view name) {
    for (const auto& entry : kActionNames) {
        if (entry.name == name) {
            return entry.action;
        }
    }
    return Action::None;
}

bool action_is_continuous(Action action) {
    const auto index = static_cast<size_t>(action);
    if (index < kActionNames.size()) {
        return kActionNames[index].continuous;
    }
    return false;
}

bool action_is_bindable(Action action) {
    return action != Action::None && action != Action::Count;
}

}  // namespace km
