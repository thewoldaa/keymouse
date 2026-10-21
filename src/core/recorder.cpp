// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/recorder.h"

#include <algorithm>

namespace km {

void MacroRecorder::start(bool record_keyboard, double tolerance_px,
                          int32_t screen_width, int32_t screen_height,
                          bool record_pov_drag) {
    active_ = true;
    record_keyboard_ = record_keyboard;
    record_pov_drag_ = record_pov_drag;
    const double tolerance = std::clamp(tolerance_px, 0.0, 64.0);
    tolerance_squared_ = tolerance * tolerance;
    screen_width_ = screen_width;
    screen_height_ = screen_height;

    have_start_ = false;
    last_step_us_ = 0;
    ignored_ = 0;
    have_point_ = false;
    buttons_held_ = 0;
    buttons_recorded_ = 0;
    keys_recorded_.fill(false);
    pending_dx_ = 0;
    pending_dy_ = 0;
    last_event_us_ = 0;
    camera_steps_ = 0;
    key_steps_ = 0;
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

void MacroRecorder::flush_pending_delta(int64_t time_us) {
    if (pending_dx_ == 0 && pending_dy_ == 0) {
        return;
    }
    MacroStep step;
    step.type = MacroStepType::MouseDelta;
    step.x = std::clamp(pending_dx_, -kMaxMacroCoordinate, kMaxMacroCoordinate);
    step.y = std::clamp(pending_dy_, -kMaxMacroCoordinate, kMaxMacroCoordinate);
    pending_dx_ = 0;
    pending_dy_ = 0;
    // The step is stamped with the button event's own time. It takes the delay
    // that elapsed since the previous step; the button event that follows then
    // lands on the same instant with a zero delay, so nothing shifts.
    append(step, time_us);
}

bool MacroRecorder::camera_drag_active() const {
    // buttons_held_ is fed by the button events and, when the backend reports
    // it, resynchronized from the OS mask on every move. That makes it correct
    // both for a drag whose press predates the recording and for a release the
    // stream missed.
    return record_pov_drag_ && buttons_held_ != 0;
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

    // Every accepted event advances the recorder clock, whether or not it
    // becomes a step, so a dropped sample's delay is carried by the next one.
    last_event_us_ = event.time_us;

    switch (event.type) {
        case MacroInputEvent::Type::MouseMove: {
            // A held button turns ordinary pointer movement into a drag: in a
            // game that is the camera, and the camera is driven by raw relative
            // deltas. Recording absolute positions here would place the cursor
            // correctly on playback but the view would never turn, so a drag is
            // stored as MouseDelta instead. A snapshot from the OS is ground
            // truth: it covers a drag whose press predates the recording and it
            // repairs a release the stream never saw. A backend that does not
            // report the field leaves the state built from its own events alone.
            if (event.buttons_known) {
                buttons_held_ = event.buttons_down;
            } else if (event.buttons_down != 0) {
                buttons_held_ |= event.buttons_down;
            }
            // A button that was already down when the recording started has no
            // press in the stream and gets no synthetic one: replaying an
            // unrequested press (LMB is a shot in most games) is worse than a
            // macro that assumes the user still holds that button, which is
            // exactly the state the recording was made in.
            if (camera_drag_active()) {
                if (!have_point_) {
                    // No reference position yet: this event only establishes
                    // one, exactly as the first absolute point would.
                    have_point_ = true;
                    last_x_ = event.x;
                    last_y_ = event.y;
                    return;
                }
                const int32_t dx = event.x - last_x_;
                const int32_t dy = event.y - last_y_;
                last_x_ = event.x;
                last_y_ = event.y;

                pending_dx_ += dx;
                pending_dy_ += dy;
                const double px = static_cast<double>(pending_dx_);
                const double py = static_cast<double>(pending_dy_);
                if (px * px + py * py < tolerance_squared_) {
                    // Too small to keep on its own, but remembered so a slow
                    // drag still adds up to the distance the user moved.
                    return;
                }
                MacroStep step;
                step.type = MacroStepType::MouseDelta;
                step.x = std::clamp(pending_dx_, -kMaxMacroCoordinate,
                                    kMaxMacroCoordinate);
                step.y = std::clamp(pending_dy_, -kMaxMacroCoordinate,
                                    kMaxMacroCoordinate);
                pending_dx_ = 0;
                pending_dy_ = 0;
                ++camera_steps_;
                append(step, event.time_us);
                return;
            }

            // No button held: ordinary pointer movement, recorded as an
            // absolute position. Movement is the noisy part. Keep the first
            // point and every point that left the tolerance circle; the
            // accumulated delay rides on the next accepted point, so timing
            // stays correct.
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
            // The button's own event is the latest word on its state, so a
            // physical snapshot sampled before it cannot resurrect a released
            // button on the next move.
            if (event.data >= 0 && event.data <= static_cast<int32_t>(MouseButton::X2)) {
                const uint32_t bit = 1u << static_cast<unsigned>(event.data);
                if (event.type == MacroInputEvent::Type::ButtonDown) {
                    buttons_held_ |= bit;
                    buttons_recorded_ |= bit;
                } else {
                    buttons_held_ &= ~bit;
                    // A release is recorded only for a press the recording
                    // owns: the up of a button that was already down when the
                    // recording started would release it under the user. The
                    // drag distance still pending is real movement though, so
                    // it is flushed before the step is skipped.
                    if ((buttons_recorded_ & bit) == 0) {
                        flush_pending_delta(event.time_us);
                        return;
                    }
                    buttons_recorded_ &= ~bit;
                }
            }
            // A pending sub-tolerance drag must not be lost when the button
            // comes up; fold it into the button step's own timestamp by
            // recording it first.
            flush_pending_delta(event.time_us);
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
            if (event.key == key::Invalid || event.key >= keys_recorded_.size()) {
                return;
            }
            bool& down = keys_recorded_[event.key];
            if (event.type == MacroInputEvent::Type::KeyDown) {
                if (down) {
                    // OS auto-repeat. The key is already held in the recording
                    // and a second KeyDown would only add a step that does
                    // nothing on playback.
                    return;
                }
                down = true;
            } else {
                if (!down) {
                    // The press happened before the recording started, most
                    // likely a modifier of the record chord itself. Dropping the
                    // release keeps the macro's key state balanced.
                    return;
                }
                down = false;
            }
            MacroStep step;
            step.type = event.type == MacroInputEvent::Type::KeyDown
                ? MacroStepType::KeyDown
                : MacroStepType::KeyUp;
            step.key = event.key;
            ++key_steps_;
            append(step, event.time_us);
            break;
        }
    }
}

Macro MacroRecorder::finish(std::string id, std::string name) {
    // Close whatever the user was still holding when the recording stopped.
    // A macro that starts a drag or holds W and never releases it would leave
    // the desktop or the game in that state on playback; the player releases on
    // stop, but a looping macro would loop with the input stuck down. The
    // synthetic releases are stamped with the last event time, so the extra
    // delay is exactly how long the input was still held after the last
    // recorded step.
    const int64_t now = have_start_ ? last_event_us_ : 0;
    flush_pending_delta(now);
    for (unsigned i = 0; i < 5; ++i) {
        const uint32_t bit = 1u << i;
        if ((buttons_recorded_ & bit) == 0) {
            continue;
        }
        MacroStep step;
        step.type = MacroStepType::ButtonUp;
        step.data = static_cast<int32_t>(i);
        append(step, now);
    }
    for (size_t key = 0; key < keys_recorded_.size(); ++key) {
        if (!keys_recorded_[key]) {
            continue;
        }
        MacroStep step;
        step.type = MacroStepType::KeyUp;
        step.key = static_cast<KeyId>(key);
        append(step, now);
    }
    buttons_held_ = 0;
    buttons_recorded_ = 0;
    keys_recorded_.fill(false);
    pending_dx_ = 0;
    pending_dy_ = 0;

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
