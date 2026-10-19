// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/motion.h"

#include <algorithm>
#include <cmath>

namespace km {
namespace {

constexpr double kSqrtHalf = 0.70710678118654752440;

// Exponential approach. With a time constant chosen so the speed reaches
// roughly 95% of target after accel_ms, movement starts soft and settles.
double smoothing_alpha(double accel_ms, double dt_seconds) {
    if (accel_ms <= 0.0) {
        return 1.0;
    }
    const double tau = accel_ms / 3000.0;
    return 1.0 - std::exp(-dt_seconds / tau);
}

}  // namespace

void Motion::configure(const MotionConfig& config) {
    config_ = config;
    config_.speed_px_s = std::clamp(config_.speed_px_s, MotionConfig::kMinSpeed, MotionConfig::kMaxSpeed);
    config_.accel_ms = std::clamp(config_.accel_ms, MotionConfig::kMinAccel, MotionConfig::kMaxAccel);
    config_.precision_mult = std::clamp(config_.precision_mult, 0.01, 1.0);
    config_.turbo_mult = std::clamp(config_.turbo_mult, 1.0, 20.0);
}

double Motion::effective_speed(const MotionConfig& config, const MotionInput& input) {
    double speed = config.speed_px_s;
    if (input.precision) {
        speed *= config.precision_mult;
    }
    if (input.turbo) {
        speed *= config.turbo_mult;
    }
    return speed;
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
        // Do not clear the fractional accumulators here: keeping them means a
        // tap-tap-tap rhythm accumulates smoothly instead of losing the
        // remainder on every release.
        return delta;
    }

    const double target = effective_speed(config_, input);
    const double alpha = smoothing_alpha(config_.accel_ms, dt_seconds);
    state_.current_speed += (target - state_.current_speed) * alpha;

    double vx = static_cast<double>(axis_x) * state_.current_speed;
    double vy = static_cast<double>(axis_y) * state_.current_speed;

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
}

}  // namespace km
