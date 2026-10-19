// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/player.h"

#include <algorithm>
#include <cmath>

namespace km {
namespace {

// A tick this late is not scheduling jitter; the machine was asleep or the
// process was stopped. Replaying the backlog would fire a burst of clicks, so
// playback stops instead.
constexpr int64_t kMaxLateUs = 500000;

// Upper bound on steps executed in one tick, so a short dt cannot starve the
// rest of the engine loop.
constexpr int kMaxStepsPerTick = 512;

int64_t scale_us(int64_t dt_ms, double speed) {
    return static_cast<int64_t>(std::llround(
        static_cast<double>(dt_ms) * 1000.0 / speed));
}

// Marks every injection made inside the scope as coming from the macro player.
// The platform hook uses the tag to ignore playback events, and the recorder
// uses it to keep a recording from swallowing its own replay.
class TagGuard {
public:
    explicit TagGuard(InputBackend& backend) : backend_(backend) {
        backend_.set_injection_tag(true);
    }
    ~TagGuard() { backend_.set_injection_tag(false); }

    TagGuard(const TagGuard&) = delete;
    TagGuard& operator=(const TagGuard&) = delete;

private:
    InputBackend& backend_;
};

}  // namespace

MacroPlayer::MacroPlayer(InputBackend& backend) : backend_(backend) {}

void MacroPlayer::start(std::shared_ptr<const Macro> macro,
                        const MacroPlaybackOptions& options, int64_t now_us) {
    TagGuard tag(backend_);
    release_held();

    macro_ = std::move(macro);
    options_ = options;
    options_.speed = std::clamp(options_.speed, 0.25, 4.0);
    options_.loop_delay_ms = std::clamp<int64_t>(options_.loop_delay_ms, 0,
                                                 24LL * 60 * 60 * 1000);
    steps_executed_ = 0;
    loops_completed_ = 0;

    have_bounds_ = backend_.screen_bounds(left_, top_, right_, bottom_) &&
                   right_ > left_ && bottom_ > top_;

    if (!macro_ || macro_->steps.empty() || options_.start_step >= macro_->steps.size()) {
        macro_.reset();
        state_ = MacroPlayerState::Idle;
        return;
    }

    step_index_ = options_.start_step;
    state_ = MacroPlayerState::Playing;
    next_due_us_ = now_us + scale_us(macro_->steps[step_index_].dt_ms, options_.speed);
}

void MacroPlayer::tick(int64_t now_us) {
    if (state_ != MacroPlayerState::Playing || !macro_) {
        return;
    }
    if (now_us < next_due_us_) {
        return;
    }
    if (now_us - next_due_us_ > kMaxLateUs) {
        stop();
        return;
    }

    TagGuard tag(backend_);

    int budget = kMaxStepsPerTick;
    while (state_ == MacroPlayerState::Playing && budget-- > 0 &&
           now_us >= next_due_us_) {
        const MacroStep& step = macro_->steps[step_index_];
        execute(step);
        ++steps_executed_;
        ++step_index_;

        if (step_index_ >= macro_->steps.size()) {
            ++loops_completed_;
            const bool more_loops = options_.loops == 0 ||
                                    loops_completed_ < static_cast<uint64_t>(options_.loops);
            if (!more_loops) {
                // The macro is over. Release anything the final step left
                // pressed so a recording that ends mid-drag cannot stick.
                release_held();
                macro_.reset();
                state_ = MacroPlayerState::Idle;
                return;
            }
            step_index_ = options_.start_step;
            next_due_us_ += scale_us(options_.loop_delay_ms, options_.speed) +
                            scale_us(macro_->steps[step_index_].dt_ms, options_.speed);
        } else {
            next_due_us_ += scale_us(macro_->steps[step_index_].dt_ms, options_.speed);
        }
    }

    // A burst of due steps was cut off; continue from now instead of trying to
    // catch up on the next tick.
    if (state_ == MacroPlayerState::Playing && now_us >= next_due_us_) {
        next_due_us_ = now_us;
    }
}

void MacroPlayer::pause(int64_t now_us) {
    if (state_ != MacroPlayerState::Playing) {
        return;
    }
    TagGuard tag(backend_);
    state_ = MacroPlayerState::Paused;
    pause_remaining_us_ = std::max<int64_t>(next_due_us_ - now_us, 0);
    // Remember what was held so resume() can press it again, then let it go so
    // a long pause does not leave the desktop stuck in a drag.
    held_snapshot_ = held_;
    release_held();
}

void MacroPlayer::resume(int64_t now_us) {
    if (state_ != MacroPlayerState::Paused) {
        return;
    }
    TagGuard tag(backend_);
    state_ = MacroPlayerState::Playing;
    // Re-press whatever was held when the pause began, in the original order,
    // then continue with the same step timing.
    for (const Held& held : held_snapshot_) {
        if (held.kind == HeldKind::Button) {
            backend_.button(static_cast<MouseButton>(held.value), true);
        } else {
            backend_.key(static_cast<KeyId>(held.value), true);
        }
    }
    held_ = held_snapshot_;
    held_snapshot_.clear();
    next_due_us_ = now_us + pause_remaining_us_;
}

void MacroPlayer::stop() {
    if (state_ == MacroPlayerState::Idle) {
        return;
    }
    TagGuard tag(backend_);
    release_held();
    held_snapshot_.clear();
    macro_.reset();
    state_ = MacroPlayerState::Idle;
}

void MacroPlayer::release_held() {
    for (auto it = held_.rbegin(); it != held_.rend(); ++it) {
        if (it->kind == HeldKind::Button) {
            backend_.button(static_cast<MouseButton>(it->value), false);
        } else {
            backend_.key(static_cast<KeyId>(it->value), false);
        }
    }
    held_.clear();
}

void MacroPlayer::execute(const MacroStep& step) {
    switch (step.type) {
        case MacroStepType::Move: {
            int32_t x = step.x;
            int32_t y = step.y;
            if (have_bounds_) {
                x = std::clamp(x, left_, right_ - 1);
                y = std::clamp(y, top_, bottom_ - 1);
            }
            backend_.move_to(x, y);
            break;
        }
        case MacroStepType::ButtonDown: {
            if (step.data >= 0 && step.data <= static_cast<int32_t>(MouseButton::X2)) {
                const auto button = static_cast<MouseButton>(step.data);
                backend_.button(button, true);
                hold(HeldKind::Button, step.data);
            }
            break;
        }
        case MacroStepType::ButtonUp: {
            if (step.data >= 0 && step.data <= static_cast<int32_t>(MouseButton::X2)) {
                const auto button = static_cast<MouseButton>(step.data);
                backend_.button(button, false);
                unhold(HeldKind::Button, step.data);
            }
            break;
        }
        case MacroStepType::Scroll:
            backend_.scroll(Axis::Vertical, step.data);
            break;
        case MacroStepType::ScrollH:
            backend_.scroll(Axis::Horizontal, step.data);
            break;
        case MacroStepType::KeyDown:
            if (step.key != key::Invalid) {
                backend_.key(step.key, true);
                hold(HeldKind::Key, step.key);
            }
            break;
        case MacroStepType::KeyUp:
            if (step.key != key::Invalid) {
                backend_.key(step.key, false);
                unhold(HeldKind::Key, step.key);
            }
            break;
        case MacroStepType::Wait:
            break;
    }
}

void MacroPlayer::hold(HeldKind kind, int32_t value) {
    const bool already = std::any_of(held_.begin(), held_.end(),
        [kind, value](const Held& held) {
            return held.kind == kind && held.value == value;
        });
    if (!already) {
        held_.push_back(Held{kind, value});
    }
}

void MacroPlayer::unhold(HeldKind kind, int32_t value) {
    held_.erase(std::remove_if(held_.begin(), held_.end(),
        [kind, value](const Held& held) {
            return held.kind == kind && held.value == value;
        }),
        held_.end());
}

}  // namespace km
