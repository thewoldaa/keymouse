// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/keycodes.h"
#include "core/macro.h"
#include "input/input_backend.h"

namespace km {

// A raw hook event on its way to the recorder. Deliberately a small POD: the
// platform hooks fill it in and never allocate or lock.
struct MacroInputEvent {
    enum class Type : uint8_t {
        MouseMove,
        ButtonDown,
        ButtonUp,
        WheelVertical,
        WheelHorizontal,
        KeyDown,
        KeyUp,
    };

    Type type = Type::MouseMove;
    // True when KeyMouse itself produced the event. Playback loops must not be
    // recorded back in.
    bool injected = false;
    // Monotonic timestamp in microseconds.
    int64_t time_us = 0;
    int32_t x = 0;
    int32_t y = 0;
    // MouseButton for button events, wheel delta for wheel events.
    int32_t data = 0;
    KeyId key = key::Invalid;
    // Bit per MouseButton that was physically down when the event was produced,
    // as read by the platform hook from the OS. A move uses it to recognize a
    // camera drag even when the button went down before the recording started,
    // so the first movement after pressing Record is still a relative delta.
    uint32_t buttons_down = 0;
    // True when the backend filled buttons_down from the OS. Without it a zero
    // mask is ambiguous — "nothing is held" and "this backend does not report
    // button state" look the same — and could not be used to correct the event
    // stream after a missed release.
    bool buttons_known = false;
};

// Turns a raw event stream into a compact macro. Not thread-safe: the engine
// thread owns it and feeds it drained events.
class MacroRecorder {
public:
    void start(bool record_keyboard, double tolerance_px,
               int32_t screen_width, int32_t screen_height,
               bool record_pov_drag = true);
    void stop();
    bool active() const { return active_; }

    // Ignores events when inactive or produced by KeyMouse itself.
    void add(const MacroInputEvent& event);

    // Finalizes and clears the recording state. Only meaningful while active.
    Macro finish(std::string id, std::string name);

    // Short human-readable summary of what the recorder assumed, for the
    // dashboard: how many camera-delta steps and key steps the last recording
    // produced. Reset by start() and accumulated until finish().
    uint64_t camera_steps() const { return camera_steps_; }
    uint64_t key_steps() const { return key_steps_; }

    size_t step_count() const { return steps_.size(); }
    uint64_t ignored_events() const { return ignored_; }

private:
    void append(MacroStep step, int64_t time_us);
    // Emits the drag displacement that was still below the tolerance circle, if
    // any, so releasing the button does not silently shorten a slow drag.
    void flush_pending_delta(int64_t time_us);
    // True while a movement should be recorded as a camera delta: the button
    // stream says one is down, the physical mask says one is down right now, or
    // a POV drag marker was seen and the matching release has not arrived.
    bool camera_drag_active() const;

    bool active_ = false;
    bool record_keyboard_ = false;
    double tolerance_squared_ = 9.0;
    int32_t screen_width_ = 0;
    int32_t screen_height_ = 0;

    // Both are set from the first event, not from a wall clock: the platform
    // hook timestamps arrive from QueryPerformanceCounter, and the engine has
    // no way to sample that same clock without mixing epochs.
    bool have_start_ = false;
    int64_t last_step_us_ = 0;
    uint64_t ignored_ = 0;

    bool have_point_ = false;
    int32_t last_x_ = 0;
    int32_t last_y_ = 0;

    // Bit per MouseButton currently held, resynchronized from the OS mask on
    // every move when the backend reports one. While any button is down,
    // movement is recorded as a relative delta (MouseDelta) rather than an
    // absolute position: a game turns the camera by reading raw mouse deltas,
    // so an absolute Move step would teleport the pointer without turning the
    // view. The mask covers a drag that started before the recording did and
    // repairs a release the event stream missed.
    uint32_t buttons_held_ = 0;
    // Bit per MouseButton whose press was actually written into the recording.
    // Only those get a release step: let go of a button that was already down
    // when the recording started and there is nothing to release in the macro.
    uint32_t buttons_recorded_ = 0;
    // Keys the recording currently has down. A repeated key-down is the OS
    // auto-repeat and must not become a second step; a key-up whose press
    // predates the recording (the modifiers of the record chord) is dropped.
    std::array<bool, 256> keys_recorded_{};
    // False when the user wants classic desktop drags: movement is then always
    // recorded as an absolute position, button or not.
    bool record_pov_drag_ = true;
    // Displacement accumulated below the tolerance circle. Dropping it outright
    // would make a long slow drag shorter than it really was.
    int32_t pending_dx_ = 0;
    int32_t pending_dy_ = 0;

    // Last event the recorder looked at, including ones that became no step.
    // finish() stamps the synthetic releases with this time rather than an
    // outside clock, so the recording ends exactly where its event stream did.
    int64_t last_event_us_ = 0;

    // Counters for the dashboard, so a recording that silently captured no
    // camera movement is visible while it is still running.
    uint64_t camera_steps_ = 0;
    uint64_t key_steps_ = 0;

    std::vector<MacroStep> steps_;
};

}  // namespace km
