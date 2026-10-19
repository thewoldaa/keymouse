// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include "core/scheduler.h"

using namespace km;

namespace {

AutoClickTask make_task(const char* id, double interval_seconds) {
    AutoClickTask task;
    task.id = id;
    task.interval_seconds = interval_seconds;
    return task;
}

}  // namespace

TEST_CASE("scheduler waits for the first interval") {
    AutoClickScheduler scheduler;
    scheduler.configure({make_task("a", 1.0)}, 1000);
    scheduler.set_enabled(true);

    CHECK(scheduler.next_due_ms() == 2000);
    CHECK_FALSE(scheduler.poll(1999, 0).has_value());
    CHECK(scheduler.next_due_ms() == 2000);

    const auto fired = scheduler.poll(2000, 0);
    REQUIRE(fired.has_value());
    CHECK(*fired == AutoClickAction::ClickLeft);
    CHECK(scheduler.runs_for("a") == 1);
    CHECK(scheduler.next_due_ms() == 3000);

    CHECK(scheduler.poll(3000, 0).has_value());
    CHECK(scheduler.runs_for("a") == 2);
}

TEST_CASE("scheduler keeps an absolute phase instead of drifting") {
    AutoClickScheduler scheduler;
    scheduler.configure({make_task("a", 1.0)}, 0);
    scheduler.set_enabled(true);

    CHECK(scheduler.poll(1000, 0).has_value());
    CHECK(scheduler.next_due_ms() == 2000);

    // A late tick still fires on the original grid, not at now + interval.
    CHECK(scheduler.poll(2150, 0).has_value());
    CHECK(scheduler.next_due_ms() == 3000);
}

TEST_CASE("scheduler stops after the repeat count") {
    AutoClickTask task = make_task("a", 1.0);
    task.repeat_count = 2;

    AutoClickScheduler scheduler;
    scheduler.configure({task}, 0);
    scheduler.set_enabled(true);

    CHECK(scheduler.poll(1000, 0).has_value());
    CHECK(scheduler.poll(2000, 0).has_value());
    CHECK(scheduler.runs_for("a") == 2);
    CHECK(scheduler.next_due_ms() == -1);
    CHECK_FALSE(scheduler.poll(3000, 0).has_value());
}

TEST_CASE("scheduler skips a stale deadline unless asked to run it") {
    AutoClickScheduler scheduler;
    scheduler.configure({make_task("a", 1.0)}, 0);
    scheduler.set_enabled(true);

    // Four seconds late is far beyond scheduler latency: the occurrence is
    // skipped and the next slot is used.
    CHECK_FALSE(scheduler.poll(4000, 0).has_value());
    CHECK(scheduler.runs_for("a") == 0);
    CHECK(scheduler.next_due_ms() == 5000);
    CHECK(scheduler.poll(5000, 0).has_value());

    AutoClickTask missed = make_task("b", 1.0);
    missed.run_missed_once = true;
    AutoClickScheduler forgiving;
    forgiving.configure({missed}, 0);
    forgiving.set_enabled(true);

    CHECK(forgiving.poll(4000, 0).has_value());
    CHECK(forgiving.runs_for("b") == 1);
    CHECK(forgiving.next_due_ms() == 5000);
}

TEST_CASE("scheduler honours the foreground application filter") {
    AutoClickTask task = make_task("a", 1.0);
    task.only_if_app = "game.exe";

    AutoClickScheduler scheduler;
    scheduler.configure({task}, 0);
    scheduler.set_enabled(true);

    CHECK_FALSE(scheduler.poll(1000, AutoClickScheduler::hash_executable("other.exe"))
                    .has_value());
    CHECK(scheduler.next_due_ms() == 2000);

    // The comparison is case insensitive, as Windows executable names are.
    CHECK(scheduler.poll(2000, AutoClickScheduler::hash_executable("GAME.EXE"))
              .has_value());
    CHECK(scheduler.runs_for("a") == 1);
}

TEST_CASE("scheduler does not rewind when the master switch is turned back on") {
    AutoClickScheduler scheduler;
    scheduler.configure({make_task("a", 1.0)}, 0);

    CHECK_FALSE(scheduler.poll(5000, 0).has_value());

    scheduler.set_enabled(true);
    CHECK_FALSE(scheduler.poll(5000, 0).has_value());
    CHECK(scheduler.next_due_ms() == 6000);
    CHECK(scheduler.poll(6000, 0).has_value());
}

TEST_CASE("editing a task restarts only that task's countdown") {
    AutoClickTask first = make_task("a", 1.0);
    AutoClickTask second = make_task("b", 5.0);

    AutoClickScheduler scheduler;
    scheduler.configure({first, second}, 0);
    scheduler.set_enabled(true);

    CHECK(scheduler.poll(1000, 0).has_value());
    CHECK(scheduler.runs_for("a") == 1);

    first.interval_seconds = 2.0;
    scheduler.configure({first, second}, 1500);

    // The edited task keeps its identity and run count but restarts the phase.
    CHECK(scheduler.runs_for("a") == 1);
    CHECK(scheduler.next_due_ms() == 3500);
    CHECK_FALSE(scheduler.poll(3499, 0).has_value());
    CHECK(scheduler.poll(3500, 0).has_value());

    // The untouched task still counts down from its original deadline.
    CHECK(scheduler.runs_for("b") == 0);
}

TEST_CASE("a disabled task is never armed") {
    AutoClickTask task = make_task("a", 1.0);
    task.enabled = false;

    AutoClickScheduler scheduler;
    scheduler.configure({task}, 0);
    scheduler.set_enabled(true);

    CHECK(scheduler.next_due_ms() == -1);
    CHECK_FALSE(scheduler.poll(10000, 0).has_value());
}

TEST_CASE("re-enabling a task starts a fresh countdown") {
    AutoClickTask task = make_task("a", 1.0);

    AutoClickScheduler scheduler;
    scheduler.configure({task}, 0);
    scheduler.set_enabled(true);
    CHECK(scheduler.poll(1000, 0).has_value());
    CHECK(scheduler.runs_for("a") == 1);

    task.enabled = false;
    scheduler.configure({task}, 1500);
    CHECK(scheduler.next_due_ms() == -1);
    CHECK_FALSE(scheduler.poll(2500, 0).has_value());

    task.enabled = true;
    scheduler.configure({task}, 3000);
    CHECK(scheduler.next_due_ms() == 4000);
    CHECK_FALSE(scheduler.poll(3999, 0).has_value());
    CHECK(scheduler.poll(4000, 0).has_value());
    CHECK(scheduler.runs_for("a") == 2);
}

TEST_CASE("two tasks that expire together fire on consecutive polls") {
    AutoClickTask first = make_task("a", 1.0);
    first.action = AutoClickAction::ClickLeft;
    AutoClickTask second = make_task("b", 1.0);
    second.action = AutoClickAction::DoubleClick;

    AutoClickScheduler scheduler;
    scheduler.configure({first, second}, 0);
    scheduler.set_enabled(true);

    const auto one = scheduler.poll(1000, 0);
    REQUIRE(one.has_value());
    CHECK(*one == AutoClickAction::ClickLeft);

    const auto two = scheduler.poll(1000, 0);
    REQUIRE(two.has_value());
    CHECK(*two == AutoClickAction::DoubleClick);
}

TEST_CASE("executable hashes ignore case") {
    const uint64_t hash = AutoClickScheduler::hash_executable("game.exe");
    CHECK(AutoClickScheduler::hash_executable("Game.EXE") == hash);
    CHECK(AutoClickScheduler::hash_executable("other.exe") != hash);
    CHECK(AutoClickScheduler::hash_executable("") != hash);
}
