// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/autoclick.h"

namespace km {

// Deadline driven auto-click scheduler.
//
// The engine calls poll() once per tick with a monotonic clock in
// milliseconds. The scheduler keeps one deadline per task and returns at most
// one action per call, so two tasks that expire together are executed one after
// the other on consecutive ticks rather than overlapping.
//
// Deadlines are absolute, not accumulated delays, so the interval does not
// drift. A deadline that is far in the past (the machine slept, or the master
// switch was off for a long time) is skipped by default; run_missed_once fires
// it once instead.
class AutoClickScheduler {
public:
    // Replaces the task list. Runtime state (next deadline, run count) is
    // carried across reloads by task id, so editing one task does not reset
    // the countdown of the others.
    void configure(const std::vector<AutoClickTask>& tasks, int64_t now_ms);

    // Master switch. Turning it on does not rewind deadlines: tasks whose
    // deadline passed while it was off are treated as missed.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    // Returns the action to perform now, or nullopt. `foreground_hash` is the
    // hash produced by hash_executable() for the foreground process.
    std::optional<AutoClickAction> poll(int64_t now_ms, uint64_t foreground_hash);

    // Earliest deadline among enabled tasks, or -1 when none is armed.
    int64_t next_due_ms() const;

    // How many times a task has fired since it was created.
    int runs_for(std::string_view id) const;

    // Stable hash used for the only_if_app filter. Case insensitive.
    static uint64_t hash_executable(std::string_view exe);

private:
    struct Runtime {
        std::string id;
        int64_t next_due_ms = -1;
        int runs = 0;
        bool finished = false;
        // Last timing seen in the configuration, used to restart the countdown
        // when the user edits it.
        double first_interval = 0.0;
        double first_delay = 0.0;
        int repeat_count = 0;
        bool was_enabled = true;
    };

    Runtime* find_runtime(std::string_view id);

    std::vector<AutoClickTask> tasks_;
    std::vector<Runtime> runtime_;
    bool enabled_ = false;
};

}  // namespace km
