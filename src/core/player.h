// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/macro.h"
#include "input/input_backend.h"

namespace km {

enum class MacroPlayerState : uint8_t {
    Idle = 0,
    Playing = 1,
    Paused = 2,
};

struct MacroPlaybackOptions {
    // 0 means loop forever.
    int loops = 1;
    // Playback speed multiplier, 0.25x to 4x.
    double speed = 1.0;
    // Pause between loops.
    int64_t loop_delay_ms = 0;
    // First step to play; playback still ends at the last step.
    size_t start_step = 0;
};

// Plays a macro against an InputBackend. Confined to one thread (the engine
// thread); the application requests playback through Engine, which owns the
// cross-thread handoff.
class MacroPlayer {
public:
    explicit MacroPlayer(InputBackend& backend);

    void start(std::shared_ptr<const Macro> macro, const MacroPlaybackOptions& options,
               int64_t now_us);
    void tick(int64_t now_us);
    void pause(int64_t now_us);
    void resume(int64_t now_us);
    void stop();

    MacroPlayerState state() const { return state_; }
    bool active() const { return state_ != MacroPlayerState::Idle; }
    size_t current_step() const { return step_index_; }
    uint64_t steps_executed() const { return steps_executed_; }
    uint64_t loops_completed() const { return loops_completed_; }

    // Releases any button or key the player is holding. Called on stop, pause,
    // and when the session ends.
    void release_held();

private:
    enum class HeldKind : uint8_t { Button, Key };
    struct Held {
        HeldKind kind;
        int32_t value;
    };

    void execute(const MacroStep& step);
    void hold(HeldKind kind, int32_t value);
    void unhold(HeldKind kind, int32_t value);

    InputBackend& backend_;
    std::shared_ptr<const Macro> macro_;
    MacroPlaybackOptions options_;

    MacroPlayerState state_ = MacroPlayerState::Idle;
    size_t step_index_ = 0;
    uint64_t steps_executed_ = 0;
    uint64_t loops_completed_ = 0;

    int64_t next_due_us_ = 0;
    int64_t pause_remaining_us_ = 0;

    bool have_bounds_ = false;
    int32_t left_ = 0;
    int32_t top_ = 0;
    int32_t right_ = 0;
    int32_t bottom_ = 0;

    std::vector<Held> held_;
    // Buttons and keys that were held when the player paused. They are pressed
    // again on resume.
    std::vector<Held> held_snapshot_;
};

}  // namespace km
