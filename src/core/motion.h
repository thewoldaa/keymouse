// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace km {

// How the cursor is moved.
enum class MotionMethod : uint8_t {
    // SendInput with MOUSEEVENTF_MOVE. Most compatible with games, but the
    // Windows pointer speed and acceleration settings apply to it.
    Relative = 0,
    // SetCursorPos. Exact positioning that ignores pointer acceleration, at
    // the cost of games that read raw input ignoring the movement.
    Absolute = 1,
};

const char* motion_method_name(MotionMethod method);
MotionMethod motion_method_from_name(std::string_view name);

struct MotionConfig {
    // Cursor speed before the acceleration ramp, in pixels per second.
    double speed_px_s = 600.0;
    // Duration of the acceleration ramp, in milliseconds. Zero jumps straight
    // to the maximum speed.
    double accel_ms = 600.0;
    // Speed multiplier at the end of the ramp. 1 disables acceleration, so the
    // cursor moves at speed_px_s from the first millisecond.
    double accel_max = 3.0;
    // Shape of the ramp. 1 is linear, below 1 ramps up quickly and spends the
    // rest of the time near the maximum, above 1 starts slowly.
    double accel_curve = 1.5;
    // Multiplier applied while the precision action is held.
    double precision_mult = 0.25;
    // Multiplier applied while the turbo action is held.
    double turbo_mult = 3.0;
    // Scale diagonal movement by 1/sqrt(2) so it is not faster than cardinal.
    bool diagonal_normalize = true;
    // Relative injection or absolute positioning.
    MotionMethod method = MotionMethod::Relative;
    // Hard limits for the UI sliders and for config validation.
    static constexpr double kMinSpeed = 10.0;
    static constexpr double kMaxSpeed = 4000.0;
    static constexpr double kMinAccel = 0.0;
    static constexpr double kMaxAccel = 1000.0;
    static constexpr double kMinAccelMax = 1.0;
    static constexpr double kMaxAccelMax = 10.0;
    static constexpr double kMinCurve = 0.25;
    static constexpr double kMaxCurve = 4.0;
};

struct MotionInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool precision = false;
    bool turbo = false;
};

struct MotionState {
    // Fractional pixels carried between ticks. SendInput only accepts
    // integers, so without this accumulator slow speeds stutter.
    double frac_x = 0.0;
    double frac_y = 0.0;
    // Current speed magnitude in pixels per second.
    double current_speed = 0.0;
    // How long a direction has been held, in seconds. Drives the acceleration
    // ramp and is cleared the moment every direction is released.
    double hold_seconds = 0.0;
    // Diagnostics
    uint64_t ticks = 0;
    uint64_t emitted_events = 0;
};

struct MotionDelta {
    int32_t dx = 0;
    int32_t dy = 0;
};

// Deterministic cursor motion integrator.
//
// The engine calls step() at a fixed timestep. Keeping the integrator pure and
// free of platform types means it can be tested without a display, and the
// same code will drive the Linux and macOS backends later.
class Motion {
public:
    void configure(const MotionConfig& config);
    const MotionConfig& config() const { return config_; }

    // Advances the simulation by `dt_seconds` and returns the integer pixel
    // delta to inject. Callers should use a fixed timestep for determinism.
    MotionDelta step(const MotionInput& input, double dt_seconds);

    // Clears fractional accumulators and speed. Call when input stops or the
    // foreground window changes so a held key does not leak across contexts.
    void reset();

    const MotionState& state() const { return state_; }

    // Effective speed for the current input with a fully warmed-up ramp,
    // exposed for the UI preview.
    static double effective_speed(const MotionConfig& config, const MotionInput& input);

    // Effective speed at a given point of the acceleration ramp. `hold_ms` is
    // how long the direction has been held; pass a huge value for the maximum.
    static double speed_at(const MotionConfig& config, const MotionInput& input,
                           double hold_ms);

private:
    MotionConfig config_{};
    MotionState state_{};
};

}  // namespace km
