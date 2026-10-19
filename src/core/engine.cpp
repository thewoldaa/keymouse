// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine.h"

#include <algorithm>
#include <chrono>

namespace km {
namespace {

// One wheel notch is 120 units; Windows treats 120 units as three lines, so a
// single line is 40 units.
constexpr int32_t kScrollUnitsPerLine = 40;

std::shared_ptr<const EngineConfig> make_engine_config(const AppConfig& config) {
    auto out = std::make_shared<EngineConfig>();
    out->motion = config.motion;
    out->scroll_lines_per_second = config.scroll_lines_per_second;
    out->fn_layer_key = config.fn_layer_key;
    out->panic_key = config.panic_key;
    if (!config.profiles.empty()) {
        const size_t index = config.active_profile < config.profiles.size()
            ? config.active_profile
            : 0;
        out->bindings = config.profiles[index].bindings;
    }
    return out;
}

}  // namespace

Engine::Engine(InputBackend& backend) : backend_(backend) {
    motion_.configure(motion_config_);
}

Engine::~Engine() = default;

void Engine::apply_config(const AppConfig& config) {
    auto snapshot = make_engine_config(config);

    // The hook thread needs the new table immediately: it decides swallowing,
    // and a key pressed during the reload must already be judged by the new
    // bindings.
    table_store_.publish(snapshot->bindings);

    panic_key_.store(snapshot->panic_key.key, std::memory_order_relaxed);
    panic_mods_.store(snapshot->panic_key.mods, std::memory_order_relaxed);

    enabled_.store(config.enabled, std::memory_order_relaxed);

    // Hand the rest to the engine thread. It owns Router and Motion, so it is
    // the only place those may be touched. The shared_ptr keeps the snapshot
    // alive if the user edits the configuration again before the engine thread
    // picks this one up; the newer edit simply replaces it.
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        pending_config_ = std::move(snapshot);
    }
    config_pending_.store(true, std::memory_order_release);

    // Held keys refer to the old binding set; the engine thread releases them
    // when it applies the new one.
    release_requested_.store(true, std::memory_order_relaxed);
}

void Engine::set_enabled(bool enabled) {
    const bool was = enabled_.exchange(enabled, std::memory_order_relaxed);
    if (was && !enabled) {
        // Released on the engine thread, which owns the router.
        release_requested_.store(true, std::memory_order_relaxed);
    }
}

bool Engine::on_input_event(const InputEvent& event) {
    bump(counters_.input_events);

    if (capture_mode_.load(std::memory_order_relaxed)) {
        // The dashboard is recording a chord. Everything must reach it
        // untouched, including chords that are already bound, otherwise a
        // binding could never be recorded twice.
        return false;
    }

    // The panic key works even while the engine is disabled: it is the way back
    // out of a bad configuration, so it must not depend on bindings being
    // usable.
    if (event.key == panic_key_.load(std::memory_order_relaxed) &&
        event.mods == panic_mods_.load(std::memory_order_relaxed)) {
        if (event.type == InputEvent::Type::KeyDown) {
            enabled_.store(!enabled_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
            release_requested_.store(true, std::memory_order_relaxed);
        }
        // Swallow the release as well, so the foreground application does not
        // see a key-up for a key it never saw go down.
        return true;
    }

    QueuedEvent queued;

    switch (event.type) {
        case InputEvent::Type::KeyDown:
            queued.kind = QueuedEvent::Kind::KeyDown;
            break;
        case InputEvent::Type::KeyUp:
            queued.kind = QueuedEvent::Kind::KeyUp;
            break;
        case InputEvent::Type::FocusLost:
            queued.kind = QueuedEvent::Kind::FocusLost;
            break;
        case InputEvent::Type::SessionLost:
            queued.kind = QueuedEvent::Kind::SessionLost;
            break;
    }
    queued.key = event.key;
    queued.mods = event.mods;

    // The swallow decision has to be made here, on the hook thread, because
    // the return value is what stops the key. It is a pure table lookup.
    bool swallow = false;
    if (enabled_.load(std::memory_order_relaxed) &&
        (event.type == InputEvent::Type::KeyDown ||
         event.type == InputEvent::Type::KeyUp)) {
        // The matching key-up is swallowed too, otherwise applications see a
        // phantom release for a key they never saw pressed.
        const BindingEntry* entry = table_store_.current()->find(event.mods, event.key);
        swallow = entry != nullptr && entry->swallow == SwallowMode::Swallow;
        if (swallow && event.type == InputEvent::Type::KeyDown) {
            bump(counters_.swallowed_events);
        }
    }

    if (!queue_.push(queued)) {
        bump(counters_.dropped_events);
    } else {
        bump(counters_.queued_events);
    }

    return swallow;
}

size_t Engine::pump() {
    apply_pending_config();

    if (release_requested_.exchange(false, std::memory_order_relaxed)) {
        release_all();
    }

    size_t processed = 0;
    QueuedEvent event;

    while (queue_.pop(event)) {
        ++processed;

        if (event.kind == QueuedEvent::Kind::FocusLost ||
            event.kind == QueuedEvent::Kind::SessionLost) {
            bump(counters_.focus_losses);
            release_all();
            continue;
        }

        if (!enabled_.load(std::memory_order_relaxed)) {
            continue;
        }

        if (event.kind == QueuedEvent::Kind::KeyDown) {
            const RouteResult result = router_.on_key_down(event.key, event.mods);
            if (result.action != Action::None && result.changed) {
                handle_action(result.action, result.active);
            }
        } else {
            const RouteResult result = router_.on_key_up(event.key);
            if (result.action != Action::None && result.changed) {
                handle_action(result.action, result.active);
            }
        }
    }

    return processed;
}

void Engine::apply_pending_config() {
    // Cheap check first: the configuration changes at human speed, so the
    // common case must not take a lock.
    if (!config_pending_.load(std::memory_order_acquire)) {
        return;
    }

    std::shared_ptr<const EngineConfig> snapshot;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        snapshot = std::move(pending_config_);
    }
    config_pending_.store(false, std::memory_order_release);
    if (!snapshot) {
        return;
    }

    motion_config_ = snapshot->motion;
    motion_.configure(motion_config_);
    scroll_lines_per_second_ = snapshot->scroll_lines_per_second;
    router_.set_bindings(snapshot->bindings, snapshot->fn_layer_key);
    release_all();
}

void Engine::handle_action(Action action, bool active) {
    switch (action) {
        case Action::MoveUp: move_up_.store(active); break;
        case Action::MoveDown: move_down_.store(active); break;
        case Action::MoveLeft: move_left_.store(active); break;
        case Action::MoveRight: move_right_.store(active); break;
        case Action::ScrollUp: scroll_up_.store(active); break;
        case Action::ScrollDown: scroll_down_.store(active); break;
        case Action::ScrollLeft: scroll_left_.store(active); break;
        case Action::ScrollRight: scroll_right_.store(active); break;
        case Action::Precision: precision_.store(active); break;
        case Action::Turbo: turbo_.store(active); break;

        case Action::ClickLeft:
            if (!active) {
                backend_.button(MouseButton::Left, true);
                backend_.button(MouseButton::Left, false);
                bump(counters_.injected_buttons);
            }
            break;

        case Action::ClickRight:
            if (!active) {
                backend_.button(MouseButton::Right, true);
                backend_.button(MouseButton::Right, false);
                bump(counters_.injected_buttons);
            }
            break;

        case Action::ClickMiddle:
            if (!active) {
                backend_.button(MouseButton::Middle, true);
                backend_.button(MouseButton::Middle, false);
                bump(counters_.injected_buttons);
            }
            break;

        case Action::ClickX1:
            if (!active) {
                backend_.button(MouseButton::X1, true);
                backend_.button(MouseButton::X1, false);
                bump(counters_.injected_buttons);
            }
            break;

        case Action::ClickX2:
            if (!active) {
                backend_.button(MouseButton::X2, true);
                backend_.button(MouseButton::X2, false);
                bump(counters_.injected_buttons);
            }
            break;

        case Action::DoubleClick:
            if (!active) {
                for (int i = 0; i < 2; ++i) {
                    backend_.button(MouseButton::Left, true);
                    backend_.button(MouseButton::Left, false);
                }
                counters_.injected_buttons.fetch_add(2, std::memory_order_relaxed);
            }
            break;

        case Action::DragLock:
            apply_drag_lock(active);
            break;

        case Action::CursorCenter:
            if (!active) {
                center_requested_.store(true);
            }
            break;

        case Action::ToggleEnabled:
            if (!active) {
                set_enabled(!enabled_.load());
            }
            break;

        case Action::OpenDashboard:
            if (!active) {
                dashboard_requested_.store(true);
            }
            break;

        case Action::SpeedUp:
            if (!active) {
                motion_config_.speed_px_s = std::min(
                    motion_config_.speed_px_s * 1.25, MotionConfig::kMaxSpeed);
                motion_.configure(motion_config_);
                speed_change_.store(motion_config_.speed_px_s,
                                    std::memory_order_relaxed);
            }
            break;

        case Action::SpeedDown:
            if (!active) {
                motion_config_.speed_px_s = std::max(
                    motion_config_.speed_px_s / 1.25, MotionConfig::kMinSpeed);
                motion_.configure(motion_config_);
                speed_change_.store(motion_config_.speed_px_s,
                                    std::memory_order_relaxed);
            }
            break;

        case Action::None:
        case Action::Count:
            break;
    }

    refresh_active_mask();
}

void Engine::apply_drag_lock(bool engaged) {
    if (drag_lock_.exchange(engaged) == engaged) {
        return;
    }
    backend_.button(MouseButton::Left, engaged);
    bump(counters_.injected_buttons);
}

void Engine::refresh_active_mask() {
    uint32_t mask = 0;
    const auto set = [&mask](Action action, bool on) {
        if (on) {
            mask |= 1u << static_cast<unsigned>(action);
        }
    };
    set(Action::MoveUp, move_up_.load(std::memory_order_relaxed));
    set(Action::MoveDown, move_down_.load(std::memory_order_relaxed));
    set(Action::MoveLeft, move_left_.load(std::memory_order_relaxed));
    set(Action::MoveRight, move_right_.load(std::memory_order_relaxed));
    set(Action::ScrollUp, scroll_up_.load(std::memory_order_relaxed));
    set(Action::ScrollDown, scroll_down_.load(std::memory_order_relaxed));
    set(Action::ScrollLeft, scroll_left_.load(std::memory_order_relaxed));
    set(Action::ScrollRight, scroll_right_.load(std::memory_order_relaxed));
    set(Action::Precision, precision_.load(std::memory_order_relaxed));
    set(Action::Turbo, turbo_.load(std::memory_order_relaxed));
    set(Action::DragLock, drag_lock_.load(std::memory_order_relaxed));
    active_mask_.store(mask, std::memory_order_relaxed);
}

int Engine::tick(double dt_seconds) {
    if (!enabled_.load(std::memory_order_relaxed) || dt_seconds <= 0.0) {
        return 0;
    }

    const auto started = std::chrono::steady_clock::now();

    bump(counters_.ticks);

    int injected = 0;

    MotionInput input;
    input.up = move_up_.load(std::memory_order_relaxed);
    input.down = move_down_.load(std::memory_order_relaxed);
    input.left = move_left_.load(std::memory_order_relaxed);
    input.right = move_right_.load(std::memory_order_relaxed);
    input.precision = precision_.load(std::memory_order_relaxed);
    input.turbo = turbo_.load(std::memory_order_relaxed);

    const MotionDelta delta = motion_.step(input, dt_seconds);
    if (delta.dx != 0 || delta.dy != 0) {
        backend_.move(delta.dx, delta.dy);
        bump(counters_.injected_moves);
        ++injected;
    }

    // Continuous scrolling, using the same accumulator pattern as motion so
    // slow scroll speeds do not stutter.
    const int scroll_x = (scroll_right_.load() ? 1 : 0) - (scroll_left_.load() ? 1 : 0);
    const int scroll_y = (scroll_up_.load() ? 1 : 0) - (scroll_down_.load() ? 1 : 0);
    if (scroll_x != 0 || scroll_y != 0) {
        scroll_remainder_ += scroll_lines_per_second_ * dt_seconds;
        const int lines = static_cast<int>(scroll_remainder_);
        scroll_remainder_ -= static_cast<double>(lines);
        if (lines > 0) {
            if (scroll_y != 0) {
                backend_.scroll(Axis::Vertical, scroll_y * lines * kScrollUnitsPerLine);
                bump(counters_.injected_scrolls);
                ++injected;
            }
            if (scroll_x != 0) {
                backend_.scroll(Axis::Horizontal, scroll_x * lines * kScrollUnitsPerLine);
                bump(counters_.injected_scrolls);
                ++injected;
            }
        }
    } else {
        scroll_remainder_ = 0.0;
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    counters_.last_tick_us.store(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
        std::memory_order_relaxed);

    return injected;
}

void Engine::release_all() {
    const auto released = router_.release_all();
    for (Action action : released) {
        if (action == Action::DragLock) {
            apply_drag_lock(false);
        }
    }

    move_up_.store(false);
    move_down_.store(false);
    move_left_.store(false);
    move_right_.store(false);
    scroll_up_.store(false);
    scroll_down_.store(false);
    scroll_left_.store(false);
    scroll_right_.store(false);
    precision_.store(false);
    turbo_.store(false);
    motion_.reset();
    scroll_remainder_ = 0.0;
    active_mask_.store(0, std::memory_order_relaxed);
}

EngineStats Engine::stats() const {
    EngineStats out;
    out.ticks = counters_.ticks.load(std::memory_order_relaxed);
    out.input_events = counters_.input_events.load(std::memory_order_relaxed);
    out.queued_events = counters_.queued_events.load(std::memory_order_relaxed);
    out.dropped_events = counters_.dropped_events.load(std::memory_order_relaxed);
    out.swallowed_events = counters_.swallowed_events.load(std::memory_order_relaxed);
    out.injected_moves = counters_.injected_moves.load(std::memory_order_relaxed);
    out.injected_buttons = counters_.injected_buttons.load(std::memory_order_relaxed);
    out.injected_scrolls = counters_.injected_scrolls.load(std::memory_order_relaxed);
    out.focus_losses = counters_.focus_losses.load(std::memory_order_relaxed);
    out.last_tick_ms = static_cast<double>(
        counters_.last_tick_us.load(std::memory_order_relaxed)) / 1000.0;
    return out;
}

std::optional<double> Engine::consume_speed_change() {
    const double speed = speed_change_.exchange(0.0, std::memory_order_relaxed);
    if (speed <= 0.0) {
        return std::nullopt;
    }
    return speed;
}

}  // namespace km
