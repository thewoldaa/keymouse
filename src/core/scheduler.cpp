// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace km {
namespace {

// A deadline later than this is not ordinary scheduler latency, it means the
// process was not running (sleep, suspend, master switch off). Firing then
// would surprise the user, so the occurrence is skipped unless the task asked
// for the missed click.
constexpr int64_t kMissedThresholdMs = 2000;

int64_t interval_ms(const AutoClickTask& task) {
    const double seconds = std::max(task.interval_seconds, kMinAutoClickIntervalSeconds);
    return static_cast<int64_t>(std::llround(seconds * 1000.0));
}

int64_t first_due_ms(int64_t now_ms, const AutoClickTask& task) {
    // "Every 12 minutes" means the first click after twelve minutes, not at
    // startup. A explicit start delay overrides that.
    const double delay = task.start_delay_seconds > 0.0
        ? task.start_delay_seconds
        : task.interval_seconds;
    return now_ms + static_cast<int64_t>(std::llround(
        std::max(delay, 0.0) * 1000.0));
}

// First deadline strictly after now, preserving the original phase where
// possible so a delayed tick does not shift every future click.
int64_t next_slot_ms(int64_t due_ms, int64_t step_ms, int64_t now_ms) {
    if (step_ms <= 0) {
        return -1;
    }
    if (due_ms > now_ms) {
        return due_ms;
    }
    const int64_t behind = now_ms - due_ms;
    const int64_t steps = behind / step_ms + 1;
    return due_ms + steps * step_ms;
}

}  // namespace

uint64_t AutoClickScheduler::hash_executable(std::string_view exe) {
    // FNV-1a over the lowercased name. Case folding is ASCII only, which is
    // what Windows executable names use in practice.
    uint64_t hash = 1469598103934665603ull;
    for (const char raw : exe) {
        char c = raw;
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

AutoClickScheduler::Runtime* AutoClickScheduler::find_runtime(std::string_view id) {
    for (auto& runtime : runtime_) {
        if (runtime.id == id) {
            return &runtime;
        }
    }
    return nullptr;
}

void AutoClickScheduler::configure(const std::vector<AutoClickTask>& tasks,
                                   int64_t now_ms) {
    std::vector<Runtime> next;
    next.reserve(tasks.size());

    for (const auto& task : tasks) {
        Runtime runtime;
        runtime.id = task.id;

        const Runtime* previous = find_runtime(task.id);
        if (previous) {
            runtime = *previous;
        } else {
            runtime.first_interval = task.interval_seconds;
            runtime.first_delay = task.start_delay_seconds;
            runtime.repeat_count = task.repeat_count;
            runtime.next_due_ms = first_due_ms(now_ms, task);
            runtime.was_enabled = task.enabled;
        }

        // Changing the timing restarts that task's countdown, which is what the
        // user expects after dragging the interval slider.
        const bool timing_changed =
            runtime.first_interval != task.interval_seconds ||
            runtime.first_delay != task.start_delay_seconds;
        if (timing_changed) {
            runtime.first_interval = task.interval_seconds;
            runtime.first_delay = task.start_delay_seconds;
            runtime.next_due_ms = first_due_ms(now_ms, task);
            runtime.finished = false;
        }
        if (runtime.repeat_count != task.repeat_count) {
            runtime.repeat_count = task.repeat_count;
            runtime.finished = false;
            if (runtime.next_due_ms < 0) {
                runtime.next_due_ms = first_due_ms(now_ms, task);
            }
        }

        // A task switched back on starts a fresh countdown. Without this the
        // -1 deadline written while it was disabled would survive forever.
        if (task.enabled && !runtime.was_enabled) {
            runtime.finished = false;
            runtime.next_due_ms = first_due_ms(now_ms, task);
        }
        runtime.was_enabled = task.enabled;

        if (!task.enabled) {
            runtime.next_due_ms = -1;
        } else if (task.repeat_count > 0 && runtime.runs >= task.repeat_count) {
            runtime.finished = true;
            runtime.next_due_ms = -1;
        }

        next.push_back(std::move(runtime));
    }

    tasks_ = tasks;
    runtime_ = std::move(next);
}

std::optional<AutoClickAction> AutoClickScheduler::poll(int64_t now_ms,
                                                        uint64_t foreground_hash) {
    if (!enabled_) {
        return std::nullopt;
    }

    size_t best = tasks_.size();
    int64_t best_due = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (!tasks_[i].enabled) {
            continue;
        }
        const auto& runtime = runtime_[i];
        if (runtime.finished || runtime.next_due_ms < 0) {
            continue;
        }
        if (runtime.next_due_ms < best_due) {
            best_due = runtime.next_due_ms;
            best = i;
        }
    }

    if (best == tasks_.size() || best_due > now_ms) {
        return std::nullopt;
    }

    const AutoClickTask& task = tasks_[best];
    Runtime& runtime = runtime_[best];

    // Application filter: an occurrence that happens while another window is
    // in front is skipped, not deferred, so the cadence stays predictable.
    if (!task.only_if_app.empty() &&
        hash_executable(task.only_if_app) != foreground_hash) {
        runtime.next_due_ms = next_slot_ms(runtime.next_due_ms, interval_ms(task), now_ms);
        return std::nullopt;
    }

    const int64_t late = now_ms - runtime.next_due_ms;
    if (late > kMissedThresholdMs && !task.run_missed_once) {
        runtime.next_due_ms = next_slot_ms(runtime.next_due_ms, interval_ms(task), now_ms);
        return std::nullopt;
    }

    ++runtime.runs;
    if (task.repeat_count > 0 && runtime.runs >= task.repeat_count) {
        runtime.finished = true;
        runtime.next_due_ms = -1;
    } else {
        runtime.next_due_ms = next_slot_ms(runtime.next_due_ms, interval_ms(task), now_ms);
    }
    return task.action;
}

int64_t AutoClickScheduler::next_due_ms() const {
    int64_t best = -1;
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (!tasks_[i].enabled) {
            continue;
        }
        const auto& runtime = runtime_[i];
        if (runtime.finished || runtime.next_due_ms < 0) {
            continue;
        }
        if (best < 0 || runtime.next_due_ms < best) {
            best = runtime.next_due_ms;
        }
    }
    return best;
}

int AutoClickScheduler::runs_for(std::string_view id) const {
    for (const auto& runtime : runtime_) {
        if (runtime.id == id) {
            return runtime.runs;
        }
    }
    return 0;
}

}  // namespace km
