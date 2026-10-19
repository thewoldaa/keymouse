// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/autoclick.h"

namespace km {

std::string_view auto_click_action_name(AutoClickAction action) {
    switch (action) {
        case AutoClickAction::ClickLeft: return "click_left";
        case AutoClickAction::ClickRight: return "click_right";
        case AutoClickAction::ClickMiddle: return "click_middle";
        case AutoClickAction::DoubleClick: return "double_click";
    }
    return "click_left";
}

AutoClickAction auto_click_action_from_name(std::string_view name) {
    if (name == "click_right") return AutoClickAction::ClickRight;
    if (name == "click_middle") return AutoClickAction::ClickMiddle;
    if (name == "double_click") return AutoClickAction::DoubleClick;
    return AutoClickAction::ClickLeft;
}

}  // namespace km
