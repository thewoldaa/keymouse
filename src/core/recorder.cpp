// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/recorder.h"

#include <algorithm>

namespace km {

void MacroRecorder::start(bool record_keyboard, double tolerance_px,
                          int32_t screen_width, int32_t screen_height) {
    active_ = true;
    record_keyboard_ = record_keyboard;
    const double tolerance = std::clamp(tolerance_px, 0.0, 64.0);
    tolerance_squared_ = tolerance * tolerance;
    screen_width_ = screen_width;
    screen_height_ = screen_height;

    have_start_ = false;
    last_step_us_ = 0;
    ignored_ = 0;
    have_point_ = false;
    steps_.clear();
}

void MacroRecorder::stop() {
    active_ = false;
}

void MacroRecorder::append(MacroStep step, int64_t time_us) {
    if (steps_.size() >= kMaxMacroSteps) {
        ++ignored_;
        return;
    }
    if (!have_start_) {
        // The first accepted event defines the zero point, so the recorder does
        // not depend on any outside clock.
        have_start_ = true;
        last_step_us_ = time_us;
    }
    const int64_t delta_us = std::max<int64_t>(time_us - last_step_us_, 0);
    step.dt_ms = std::min<int64_t>(delta_us / 1000, kMaxMacroStepDelayMs);
    last_step_us_ = time_us;
    steps_.push_back(step);
}

void MacroRecorder::add(const MacroInputEvent& event) {
    if (!active_) {
        return;
    }
    if (event.injected) {
        // The macro player and the mouse-mode injector mark their events, so a
        // recording never contains its own output.
        ++ignored_;
        return;
    }

    switch (event.type) {
        case MacroInputEvent::Type::MouseMove: {
            // Movement is the noisy part. Keep the first point and every point
            // that left the tolerance circle; the accumulated delay rides on
            // the next accepted point, so timing stays correct.
            if (have_point_) {
                const double dx = static_cast<double>(event.x - last_x_);
                const double dy = static_cast<double>(event.y - last_y_);
                if (dx * dx + dy * dy < tolerance_squared_) {
                    return;
                }
            }
            MacroStep step;
            step.type = MacroStepType::Move;
            step.x = std::clamp(event.x, -kMaxMacroCoordinate, kMaxMacroCoordinate);
            step.y = std::clamp(event.y, -kMaxMacroCoordinate, kMaxMacroCoordinate);
            append(step, event.time_us);
            have_point_ = true;
            last_x_ = step.x;
            last_y_ = step.y;
            break;
        }

        case MacroInputEvent::Type::ButtonDown:
        case MacroInputEvent::Type::ButtonUp: {
            MacroStep step;
            step.type = event.type == MacroInputEvent::Type::ButtonDown
                ? MacroStepType::ButtonDown
                : MacroStepType::ButtonUp;
            step.data = event.data;
            append(step, event.time_us);
            break;
        }

        case MacroInputEvent::Type::WheelVertical:
        case MacroInputEvent::Type::WheelHorizontal: {
            MacroStep step;
            step.type = event.type == MacroInputEvent::Type::WheelVertical
                ? MacroStepType::Scroll
                : MacroStepType::ScrollH;
            step.data = event.data;
            append(step, event.time_us);
            break;
        }

        case MacroInputEvent::Type::KeyDown:
        case MacroInputEvent::Type::KeyUp: {
            if (!record_keyboard_) {
                return;
            }
            if (event.key == key::Invalid) {
                return;
            }
            MacroStep step;
            step.type = event.type == MacroInputEvent::Type::KeyDown
                ? MacroStepType::KeyDown
                : MacroStepType::KeyUp;
            step.key = event.key;
            append(step, event.time_us);
            break;
        }
    }
}

Macro MacroRecorder::finish(std::string id, std::string name) {
    Macro macro;
    macro.id = std::move(id);
    macro.name = std::move(name);
    macro.recorded_width = screen_width_;
    macro.recorded_height = screen_height_;
    macro.steps = std::move(steps_);
    steps_.clear();
    active_ = false;
    return macro;
}

}  // namespace km
