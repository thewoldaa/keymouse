// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
};

// Turns a raw event stream into a compact macro. Not thread-safe: the engine
// thread owns it and feeds it drained events.
class MacroRecorder {
public:
    void start(bool record_keyboard, double tolerance_px,
               int32_t screen_width, int32_t screen_height);
    void stop();
    bool active() const { return active_; }

    // Ignores events when inactive or produced by KeyMouse itself.
    void add(const MacroInputEvent& event);

    // Finalizes and clears the recording state. Only meaningful while active.
    Macro finish(std::string id, std::string name);

    size_t step_count() const { return steps_.size(); }
    uint64_t ignored_events() const { return ignored_; }

private:
    void append(MacroStep step, int64_t time_us);

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

    std::vector<MacroStep> steps_;
};

}  // namespace km
