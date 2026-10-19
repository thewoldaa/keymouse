// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <cmath>

#include "core/motion.h"

using namespace km;

namespace {

constexpr double kTick = 1.0 / 500.0;

MotionConfig make_config(double speed = 600.0, double accel = 0.0) {
    MotionConfig config;
    config.speed_px_s = speed;
    config.accel_ms = accel;
    // Most tests measure the steady-state speed, so turn the ramp off unless a
    // test opts into it.
    config.accel_max = 1.0;
    config.accel_curve = 1.0;
    return config;
}

MotionInput only_right() {
    MotionInput input;
    input.right = true;
    return input;
}

}  // namespace

TEST_CASE("no input produces no movement") {
    Motion motion;
    motion.configure(make_config());
    const MotionDelta delta = motion.step(MotionInput{}, kTick);
    CHECK(delta.dx == 0);
    CHECK(delta.dy == 0);
}

TEST_CASE("movement over one second is close to the configured speed") {
    Motion motion;
    motion.configure(make_config(600.0));

    int64_t total = 0;
    for (int i = 0; i < 500; ++i) {
        total += motion.step(only_right(), kTick).dx;
    }
    // Truncation loses less than one pixel per tick.
    CHECK(total >= 590);
    CHECK(total <= 600);
}

TEST_CASE("subpixel accumulation preserves slow movement") {
    Motion motion;
    motion.configure(make_config(20.0));

    int64_t total = 0;
    for (int i = 0; i < 500; ++i) {
        total += motion.step(only_right(), kTick).dx;
    }
    // Without an accumulator this would be zero at 20 px/s and 500 Hz.
    CHECK(total >= 18);
    CHECK(total <= 20);
}

TEST_CASE("diagonals are not faster than cardinals") {
    MotionConfig config = make_config(600.0);
    config.diagonal_normalize = true;

    Motion diagonal;
    diagonal.configure(config);
    MotionInput input;
    input.right = true;
    input.down = true;

    double magnitude = 0.0;
    for (int i = 0; i < 500; ++i) {
        const MotionDelta delta = diagonal.step(input, kTick);
        magnitude += std::sqrt(static_cast<double>(delta.dx * delta.dx + delta.dy * delta.dy));
    }
    CHECK(magnitude <= 600.0 * 1.02);
    CHECK(magnitude >= 590.0);
}

TEST_CASE("diagonal normalization can be disabled") {
    MotionConfig config = make_config(600.0);
    config.diagonal_normalize = false;

    Motion diagonal;
    diagonal.configure(config);
    MotionInput input;
    input.right = true;
    input.down = true;

    double magnitude = 0.0;
    for (int i = 0; i < 500; ++i) {
        const MotionDelta delta = diagonal.step(input, kTick);
        magnitude += std::sqrt(static_cast<double>(delta.dx * delta.dx + delta.dy * delta.dy));
    }
    CHECK(magnitude > 600.0 * 1.3);
}

TEST_CASE("opposite directions cancel") {
    Motion motion;
    motion.configure(make_config());
    MotionInput input;
    input.left = true;
    input.right = true;

    for (int i = 0; i < 100; ++i) {
        const MotionDelta delta = motion.step(input, kTick);
        CHECK(delta.dx == 0);
    }
}

TEST_CASE("precision scales speed down") {
    Motion motion;
    motion.configure(make_config(1000.0));
    MotionInput input = only_right();
    input.precision = true;

    int64_t total = 0;
    for (int i = 0; i < 500; ++i) {
        total += motion.step(input, kTick).dx;
    }
    CHECK(total >= 240);
    CHECK(total <= 250);
}

TEST_CASE("turbo scales speed up") {
    Motion motion;
    motion.configure(make_config(1000.0));
    MotionInput input = only_right();
    input.turbo = true;

    int64_t total = 0;
    for (int i = 0; i < 500; ++i) {
        total += motion.step(input, kTick).dx;
    }
    CHECK(total >= 2990);
    CHECK(total <= 3000);
}

TEST_CASE("precision and turbo combine") {
    MotionConfig config = make_config(1000.0);
    config.precision_mult = 0.5;
    config.turbo_mult = 2.0;

    Motion motion;
    motion.configure(config);
    MotionInput input = only_right();
    input.precision = true;
    input.turbo = true;

    int64_t total = 0;
    for (int i = 0; i < 500; ++i) {
        total += motion.step(input, kTick).dx;
    }
    CHECK(total >= 990);
    CHECK(total <= 1000);
}

TEST_CASE("acceleration ramps up over time") {
    MotionConfig config = make_config(1000.0, 200.0);
    config.accel_max = 3.0;
    Motion motion;
    motion.configure(config);

    int64_t early = 0;
    int64_t late = 0;
    for (int i = 0; i < 500; ++i) {
        const int32_t dx = motion.step(only_right(), kTick).dx;
        if (i < 25) {
            early += dx;
        } else if (i >= 475) {
            late += dx;
        }
    }
    CHECK(late > early * 2);
}

TEST_CASE("reset clears accumulated state") {
    Motion motion;
    motion.configure(make_config(600.0, 100.0));
    for (int i = 0; i < 100; ++i) {
        motion.step(only_right(), kTick);
    }

    motion.reset();
    CHECK(motion.state().frac_x == doctest::Approx(0.0));
    CHECK(motion.state().current_speed == doctest::Approx(0.0));
}

TEST_CASE("configure clamps out of range values") {
    Motion motion;
    MotionConfig config;
    config.speed_px_s = 999999.0;
    config.precision_mult = 0.0;
    config.turbo_mult = 1000.0;
    motion.configure(config);

    CHECK(motion.config().speed_px_s == MotionConfig::kMaxSpeed);
    CHECK(motion.config().precision_mult >= 0.01);
    CHECK(motion.config().turbo_mult <= 20.0);
}

TEST_CASE("negative movement rounds towards zero") {
    Motion motion;
    motion.configure(make_config(600.0));

    int64_t total = 0;
    for (int i = 0; i < 500; ++i) {
        MotionInput input;
        input.left = true;
        total += motion.step(input, kTick).dx;
    }
    CHECK(total <= -590);
    CHECK(total >= -600);
}

TEST_CASE("zero timestep is a no-op") {
    Motion motion;
    motion.configure(make_config());
    const MotionDelta delta = motion.step(only_right(), 0.0);
    CHECK(delta.dx == 0);
    CHECK(delta.dy == 0);
}
