// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace km {

struct MotionConfig {
    // Cursor speed at full tilt, in pixels per second.
    double speed_px_s = 600.0;
    // Time to reach 95% of target speed when a direction key is pressed.
    // 0 disables acceleration entirely.
    double accel_ms = 120.0;
    // Multiplier applied while the precision action is held.
    double precision_mult = 0.25;
    // Multiplier applied while the turbo action is held.
    double turbo_mult = 3.0;
    // Scale diagonal movement by 1/sqrt(2) so it is not faster than cardinal.
    bool diagonal_normalize = true;
    // Hard limits for the UI sliders and for config validation.
    static constexpr double kMinSpeed = 10.0;
    static constexpr double kMaxSpeed = 4000.0;
    static constexpr double kMinAccel = 0.0;
    static constexpr double kMaxAccel = 1000.0;
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
    // Current speed magnitude, smoothed towards the target.
    double current_speed = 0.0;
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

    // Effective speed for the current input, exposed for the UI preview.
    static double effective_speed(const MotionConfig& config, const MotionInput& input);

private:
    MotionConfig config_{};
    MotionState state_{};
};

}  // namespace km
