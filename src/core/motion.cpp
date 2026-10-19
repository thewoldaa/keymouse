// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/motion.h"

#include <algorithm>
#include <cmath>

namespace km {
namespace {

constexpr double kSqrtHalf = 0.70710678118654752440;

// Fraction of the configured maximum reached after `hold_ms`, in [0, 1].
double ramp_fraction(const MotionConfig& config, double hold_ms) {
    if (config.accel_ms <= 0.0 || config.accel_max <= 1.0) {
        return 1.0;
    }
    const double linear = std::clamp(hold_ms / config.accel_ms, 0.0, 1.0);
    return std::pow(linear, std::max(config.accel_curve, 0.01));
}

}  // namespace

const char* motion_method_name(MotionMethod method) {
    switch (method) {
        case MotionMethod::Relative: return "relative";
        case MotionMethod::Absolute: return "absolute";
    }
    return "relative";
}

MotionMethod motion_method_from_name(std::string_view name) {
    return name == "absolute" ? MotionMethod::Absolute : MotionMethod::Relative;
}

void Motion::configure(const MotionConfig& config) {
    config_ = config;
    config_.speed_px_s = std::clamp(config_.speed_px_s, MotionConfig::kMinSpeed,
                                    MotionConfig::kMaxSpeed);
    config_.accel_ms = std::clamp(config_.accel_ms, MotionConfig::kMinAccel,
                                  MotionConfig::kMaxAccel);
    config_.accel_max = std::clamp(config_.accel_max, MotionConfig::kMinAccelMax,
                                   MotionConfig::kMaxAccelMax);
    config_.accel_curve = std::clamp(config_.accel_curve, MotionConfig::kMinCurve,
                                     MotionConfig::kMaxCurve);
    config_.precision_mult = std::clamp(config_.precision_mult, 0.01, 1.0);
    config_.turbo_mult = std::clamp(config_.turbo_mult, 1.0, 20.0);
}

double Motion::speed_at(const MotionConfig& config, const MotionInput& input,
                        double hold_ms) {
    const double ramp = ramp_fraction(config, hold_ms);
    // The base speed is the floor: the mult is only reached at the end of the
    // ramp. Keep the arithmetic linear in accel_max so a value of 1 is exactly
    // a constant speed with no floating point drift.
    double speed = config.speed_px_s * (1.0 + (config.accel_max - 1.0) * ramp);
    if (input.precision) {
        speed *= config.precision_mult;
    }
    if (input.turbo) {
        speed *= config.turbo_mult;
    }
    return speed;
}

double Motion::effective_speed(const MotionConfig& config, const MotionInput& input) {
    // "Effective" means fully accelerated, which is what the dashboard shows
    // as the top speed of each mode.
    return speed_at(config, input, config.accel_ms > 0.0 ? config.accel_ms : 0.0);
}

MotionDelta Motion::step(const MotionInput& input, double dt_seconds) {
    MotionDelta delta;
    if (dt_seconds <= 0.0) {
        return delta;
    }

    // Opposite directions cancel; holding left+right is not a speed boost.
    const int axis_x = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    const int axis_y = (input.down ? 1 : 0) - (input.up ? 1 : 0);

    ++state_.ticks;

    if (axis_x == 0 && axis_y == 0) {
        state_.current_speed = 0.0;
        state_.hold_seconds = 0.0;
        // Do not clear the fractional accumulators here: keeping them means a
        // tap-tap-tap rhythm accumulates smoothly instead of losing the
        // remainder on every release.
        return delta;
    }

    // The ramp is measured in wall time spent holding a direction, not in
    // ticks, so the feel is identical at every engine rate.
    state_.hold_seconds += dt_seconds;
    const double target = speed_at(config_, input, state_.hold_seconds * 1000.0);
    state_.current_speed = target;

    double vx = static_cast<double>(axis_x) * target;
    double vy = static_cast<double>(axis_y) * target;

    if (axis_x != 0 && axis_y != 0 && config_.diagonal_normalize) {
        vx *= kSqrtHalf;
        vy *= kSqrtHalf;
    }

    state_.frac_x += vx * dt_seconds;
    state_.frac_y += vy * dt_seconds;

    // Truncate towards zero, not floor: negative movement must not round the
    // accumulator in the wrong direction.
    const auto mx = static_cast<int32_t>(state_.frac_x);
    const auto my = static_cast<int32_t>(state_.frac_y);
    state_.frac_x -= static_cast<double>(mx);
    state_.frac_y -= static_cast<double>(my);

    delta.dx = mx;
    delta.dy = my;
    if (mx != 0 || my != 0) {
        ++state_.emitted_events;
    }
    return delta;
}

void Motion::reset() {
    state_.frac_x = 0.0;
    state_.frac_y = 0.0;
    state_.current_speed = 0.0;
    state_.hold_seconds = 0.0;
}

}  // namespace km
