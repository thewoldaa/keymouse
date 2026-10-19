// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace km {

// What a scheduled task does when its timer expires.
enum class AutoClickAction : uint8_t {
    ClickLeft = 0,
    ClickRight = 1,
    ClickMiddle = 2,
    DoubleClick = 3,
};

std::string_view auto_click_action_name(AutoClickAction action);
AutoClickAction auto_click_action_from_name(std::string_view name);

// One row in the Auto-Click tab.
//
// The click always happens at the cursor position at the moment the timer
// expires. The cursor is never moved: the point of the feature is clicking
// where the user left the pointer.
struct AutoClickTask {
    // Stable identity used to keep the countdown across config reloads and to
    // match the task in the UI. Generated when a task is created.
    std::string id;
    std::string name;

    bool enabled = true;

    AutoClickAction action = AutoClickAction::ClickLeft;

    // Interval between clicks, in seconds. The UI edits this in the unit the
    // user picked, but the config stores seconds so hand-editing is simple.
    double interval_seconds = 720.0;
    // Delay before the first click. Zero means "after one interval", which is
    // what "click every 12 minutes" implies.
    double start_delay_seconds = 0.0;
    // 0 means unlimited. Otherwise the task disables itself after this many
    // clicks.
    int repeat_count = 0;
    // When the app was asleep or the machine was suspended and the deadline
    // passed, run the missed occurrence once instead of skipping it.
    bool run_missed_once = false;
    // Only fire while this executable is in the foreground. Empty means any
    // application.
    std::string only_if_app;
};

// Limits shared by the UI and the validator.
inline constexpr double kMinAutoClickIntervalSeconds = 1.0;
inline constexpr double kMaxAutoClickIntervalSeconds = 7.0 * 24.0 * 60.0 * 60.0;
inline constexpr double kMaxAutoClickDelaySeconds = 7.0 * 24.0 * 60.0 * 60.0;
inline constexpr int kMaxAutoClickRepeats = 1000000;

}  // namespace km
