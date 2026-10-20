// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine.h"

#include <algorithm>
#include <chrono>

#include "input/injection.h"

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
    out->mouse_bindings = config.mouse_mode.bindings;
    out->mouse_toggle_key = config.mouse_mode.toggle_key;
    out->mouse_hold_to_activate = config.mouse_mode.hold_to_activate;
    out->mouse_beep = config.mouse_mode.beep;
    out->auto_click_toggle_key = config.auto_click.toggle_key;
    out->auto_click_tasks = config.auto_click.tasks;
    out->auto_click_enabled = config.auto_click.enabled;
    out->macro_record_keyboard = config.macros.record_keyboard;
    out->macro_move_tolerance_px = config.macros.move_tolerance_px;
    out->macro_record_key = config.macros.record_key;
    out->macro_play_key = config.macros.play_key;
    out->macro_stop_key = config.macros.stop_key;
    for (const MacroEntry& entry : config.macros.entries) {
        if (entry.hotkey.valid()) {
            out->macro_hotkeys.push_back(
                MacroHotkeyTable::Entry{entry.hotkey, entry.id});
        }
    }
    if (!config.profiles.empty()) {
        const size_t index = config.active_profile < config.profiles.size()
            ? config.active_profile
            : 0;
        out->bindings = config.profiles[index].bindings;
    }
    return out;
}

}  // namespace

Engine::Engine(InputBackend& backend, Clock clock) : backend_(backend) {
    if (clock) {
        clock_ = std::move(clock);
    }
    motion_.configure(motion_config_);
}

Engine::~Engine() = default;

void Engine::apply_config(const AppConfig& config) {
    auto snapshot = make_engine_config(config);

    // The hook thread needs the new table immediately: it decides swallowing,
    // and a key pressed during the reload must already be judged by the new
    // bindings.
    table_store_.publish(snapshot->bindings);
    mouse_table_store_.publish(snapshot->mouse_bindings);

    panic_key_.store(snapshot->panic_key.key, std::memory_order_relaxed);
    panic_mods_.store(snapshot->panic_key.mods, std::memory_order_relaxed);

    mouse_toggle_key_.store(snapshot->mouse_toggle_key.key, std::memory_order_relaxed);
    mouse_toggle_mods_.store(snapshot->mouse_toggle_key.mods, std::memory_order_relaxed);
    mouse_hold_.store(snapshot->mouse_hold_to_activate, std::memory_order_relaxed);
    mouse_beep_.store(snapshot->mouse_beep, std::memory_order_relaxed);

    auto_click_toggle_key_.store(snapshot->auto_click_toggle_key.key,
                                 std::memory_order_relaxed);
    auto_click_toggle_mods_.store(snapshot->auto_click_toggle_key.mods,
                                  std::memory_order_relaxed);

    macro_record_key_.store(snapshot->macro_record_key.key, std::memory_order_relaxed);
    macro_record_mods_.store(snapshot->macro_record_key.mods, std::memory_order_relaxed);
    macro_play_key_.store(snapshot->macro_play_key.key, std::memory_order_relaxed);
    macro_play_mods_.store(snapshot->macro_play_key.mods, std::memory_order_relaxed);
    macro_stop_key_.store(snapshot->macro_stop_key.key, std::memory_order_relaxed);
    macro_stop_mods_.store(snapshot->macro_stop_key.mods, std::memory_order_relaxed);
    macro_tolerance_px_.store(snapshot->macro_move_tolerance_px,
                              std::memory_order_relaxed);
    macro_hotkey_store_.publish(snapshot->macro_hotkeys);

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

    if (event.extra_info == kInjectionTag) {
        // An event injected by the macro player. The platform hooks already
        // skip these; the check keeps the engine honest for backends that
        // forward them anyway.
        return false;
    }

    // The panic key works even while the engine is disabled: it is the way back
    // out of a bad configuration, so it must not depend on bindings being
    // usable. Its press also stops a macro recording or playback, which is what
    // "stop everything now" means when the keyboard is misbehaving.
    const KeyId panic_key = static_cast<KeyId>(
        panic_key_.load(std::memory_order_relaxed));
    if (event.key == panic_key) {
        const ModMask panic_mods = panic_mods_.load(std::memory_order_relaxed);
        if (event.type == InputEvent::Type::KeyDown && event.mods == panic_mods) {
            // Auto-repeat of a held panic key must not toggle the engine back
            // on. Only the first press counts.
            if (!panic_down_.exchange(true, std::memory_order_relaxed)) {
                enabled_.store(!enabled_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
                release_requested_.store(true, std::memory_order_relaxed);
                macro_stop_requested_.store(true, std::memory_order_relaxed);
                record_stop_requested_.store(true, std::memory_order_relaxed);
            }
            return true;
        }
        // Swallow the release of a press that was consumed, even when its
        // modifiers were let go first: the application saw the key go down and
        // must see it come up. A release whose press was not consumed keeps
        // its own life, so a plain key next to a chord panic key is unaffected.
        if (event.type == InputEvent::Type::KeyUp &&
            panic_down_.exchange(false, std::memory_order_relaxed)) {
            return true;
        }
    }

    // Break stops macro playback no matter what state the engine is in. It
    // stays usable as the panic key above.
    if (event.key == key::Pause && event.type == InputEvent::Type::KeyDown) {
        macro_stop_requested_.store(true, std::memory_order_relaxed);
    }

    // Mouse mode and the auto-click switch are handled here, like the panic
    // key, so they work while the dashboard is hidden and are never delivered
    // to the foreground application.
    const bool is_down = event.type == InputEvent::Type::KeyDown;
    const bool is_up = event.type == InputEvent::Type::KeyUp;

    if (event.key == mouse_toggle_key_.load(std::memory_order_relaxed)) {
        if (is_down && event.mods == mouse_toggle_mods_.load(std::memory_order_relaxed)) {
            if (!mouse_toggle_down_.exchange(true, std::memory_order_relaxed)) {
                if (mouse_hold_.load(std::memory_order_relaxed)) {
                    mouse_mode_.store(true, std::memory_order_relaxed);
                } else {
                    mouse_mode_.store(!mouse_mode_.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
                }
                release_requested_.store(true, std::memory_order_relaxed);
                if (mouse_beep_.load(std::memory_order_relaxed)) {
                    beep_requested_.store(true, std::memory_order_relaxed);
                }
            }
            return true;
        }
        // Only swallow the release of a press that was actually consumed. A
        // plain "M" must not lose its key-up just because Ctrl+Alt+M is the
        // toggle chord: the foreground application would keep M stuck down.
        if (is_up && mouse_toggle_down_.exchange(false, std::memory_order_relaxed)) {
            if (mouse_hold_.load(std::memory_order_relaxed) &&
                mouse_mode_.exchange(false, std::memory_order_relaxed)) {
                release_requested_.store(true, std::memory_order_relaxed);
                if (mouse_beep_.load(std::memory_order_relaxed)) {
                    beep_requested_.store(true, std::memory_order_relaxed);
                }
            }
            return true;
        }
    }

    if (event.key == auto_click_toggle_key_.load(std::memory_order_relaxed)) {
        if (is_down && event.mods == auto_click_toggle_mods_.load(std::memory_order_relaxed)) {
            if (!auto_click_toggle_down_.exchange(true, std::memory_order_relaxed)) {
                auto_click_enabled_.store(
                    !auto_click_enabled_.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
                auto_click_change_.store(true, std::memory_order_relaxed);
            }
            return true;
        }
        if (is_up && auto_click_toggle_down_.exchange(false, std::memory_order_relaxed)) {
            return true;
        }
    }

    // Macro record/play/stop. One-shot chords with the same swallow pattern as
    // the mode toggles: only the release of a press that was consumed is
    // swallowed, so a plain "R" never loses its key-up.
    if (event.key == macro_record_key_.load(std::memory_order_relaxed)) {
        if (is_down && event.mods == macro_record_mods_.load(std::memory_order_relaxed)) {
            if (!macro_record_down_.exchange(true, std::memory_order_relaxed)) {
                macro_record_toggle_requested_.store(true, std::memory_order_relaxed);
            }
            return true;
        }
        if (is_up && macro_record_down_.exchange(false, std::memory_order_relaxed)) {
            return true;
        }
    }

    if (event.key == macro_play_key_.load(std::memory_order_relaxed)) {
        if (is_down && event.mods == macro_play_mods_.load(std::memory_order_relaxed)) {
            if (!macro_play_down_.exchange(true, std::memory_order_relaxed)) {
                macro_play_pause_requested_.store(true, std::memory_order_relaxed);
            }
            return true;
        }
        if (is_up && macro_play_down_.exchange(false, std::memory_order_relaxed)) {
            return true;
        }
    }

    if (event.key == macro_stop_key_.load(std::memory_order_relaxed)) {
        if (is_down && event.mods == macro_stop_mods_.load(std::memory_order_relaxed)) {
            if (!macro_stop_down_.exchange(true, std::memory_order_relaxed)) {
                macro_stop_requested_.store(true, std::memory_order_relaxed);
            }
            return true;
        }
        if (is_up && macro_stop_down_.exchange(false, std::memory_order_relaxed)) {
            return true;
        }
    }

    // Per-macro hotkeys. The table is immutable and published by apply_config();
    // the hook thread only records which entry fired and the UI thread resolves
    // the id from the retained table.
    if (is_down) {
        const MacroHotkeyTable* table = macro_hotkey_store_.current();
        size_t index = 0;
        if (table != nullptr && table->find(event.mods, event.key, index)) {
            if (macro_hotkey_key_.exchange(event.key, std::memory_order_relaxed) ==
                key::Invalid) {
                macro_hotkey_table_seen_.store(table, std::memory_order_release);
                macro_hotkey_index_.store(index, std::memory_order_relaxed);
                macro_hotkey_requested_.store(true, std::memory_order_relaxed);
            }
            return true;
        }
    } else if (is_up &&
               event.key == macro_hotkey_key_.load(std::memory_order_relaxed)) {
        macro_hotkey_key_.store(key::Invalid, std::memory_order_relaxed);
        return true;
    }

    // Keyboard capture runs alongside normal routing: the keys still reach the
    // foreground application, they are only also written into the recording.
    if ((is_down || is_up) &&
        recording_active_.load(std::memory_order_relaxed) &&
        recording_keyboard_.load(std::memory_order_relaxed)) {
        MacroInputEvent recorded;
        recorded.type = is_down ? MacroInputEvent::Type::KeyDown
                                : MacroInputEvent::Type::KeyUp;
        recorded.time_us = event.time_us;
        recorded.key = event.key;
        recorded.injected = false;
        if (!macro_key_ring_.push(recorded)) {
            bump(counters_.dropped_events);
        }
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
    if (event.type == InputEvent::Type::KeyDown || event.type == InputEvent::Type::KeyUp) {
        if (event.type == InputEvent::Type::KeyDown) {
            const bool active = enabled_.load(std::memory_order_relaxed);
            bool should_swallow = false;
            if (active) {
                // While mouse mode is on, the plain-key table is the only one
                // that acts. The always-on chords stay dormant so a mode key
                // and a chord can never fire together.
                const BindingTable* table = mouse_mode_.load(std::memory_order_relaxed)
                    ? mouse_table_store_.current()
                    : table_store_.current();
                const BindingEntry* entry = table->find(event.mods, event.key);
                should_swallow = entry != nullptr && entry->swallow == SwallowMode::Swallow;
            }
            if (event.key < swallowed_keys_.size()) {
                swallowed_keys_[event.key].store(should_swallow,
                                                 std::memory_order_relaxed);
            }
            if (should_swallow) {
                bump(counters_.swallowed_events);
            }
            swallow = should_swallow;
        } else {
            // Swallow the release of a key whose press was swallowed, even if
            // the modifiers changed or mouse mode was toggled in between.
            if (event.key < swallowed_keys_.size()) {
                swallow = swallowed_keys_[event.key].exchange(
                    false, std::memory_order_relaxed);
            }
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

    const int64_t now_us = clock_();

    if (release_requested_.exchange(false, std::memory_order_relaxed)) {
        release_all();
    }

    // The rings are drained before the requests are applied so a stop request
    // still records the events that arrived just before it; otherwise the tail
    // of every recording would be cut off.
    drain_macro_events(now_us);
    apply_macro_requests(now_us);

    size_t processed = 0;
    QueuedEvent event;

    while (queue_.pop(event)) {
        ++processed;

        if (event.kind == QueuedEvent::Kind::FocusLost ||
            event.kind == QueuedEvent::Kind::SessionLost) {
            bump(counters_.focus_losses);
            if (event.kind == QueuedEvent::Kind::SessionLost) {
                // Auto-off on lock, sleep, or UAC: the keyboard focus is gone,
                // so leaving mouse mode on would arm plain keys behind the
                // lock screen. The toggle key re-enters the mode afterwards.
                mouse_mode_.store(false, std::memory_order_relaxed);
                mouse_toggle_down_.store(false, std::memory_order_relaxed);
                auto_click_toggle_down_.store(false, std::memory_order_relaxed);
                macro_record_down_.store(false, std::memory_order_relaxed);
                macro_play_down_.store(false, std::memory_order_relaxed);
                macro_stop_down_.store(false, std::memory_order_relaxed);
                macro_hotkey_key_.store(key::Invalid, std::memory_order_relaxed);
                panic_down_.store(false, std::memory_order_relaxed);
            }
            release_all();
            continue;
        }

        if (!enabled_.load(std::memory_order_relaxed)) {
            continue;
        }

        Router& active_router = mouse_mode_.load(std::memory_order_relaxed)
            ? mouse_router_
            : router_;
        if (event.kind == QueuedEvent::Kind::KeyDown) {
            const RouteResult result = active_router.on_key_down(event.key, event.mods);
            if (result.action != Action::None && result.changed) {
                handle_action(result.action, result.active, result.mode);
            }
        } else {
            const RouteResult result = active_router.on_key_up(event.key);
            if (result.action != Action::None && result.changed) {
                handle_action(result.action, result.active, result.mode);
            }
        }
    }

    publish_player_status();

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
    mouse_router_.set_bindings(snapshot->mouse_bindings, snapshot->fn_layer_key);

    scheduler_.configure(snapshot->auto_click_tasks, now_ms());
    if (!auto_click_initialized_) {
        auto_click_enabled_.store(snapshot->auto_click_enabled,
                                  std::memory_order_relaxed);
        auto_click_initialized_ = true;
    }
    scheduler_.set_enabled(auto_click_enabled_.load(std::memory_order_relaxed));
    auto_click_next_due_ms_.store(
        scheduler_.enabled() ? scheduler_.next_due_ms() : -1,
        std::memory_order_relaxed);

    release_all();
}

void Engine::handle_action(Action action, bool active, BindMode mode) {
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
            apply_click(MouseButton::Left, active, mode);
            break;

        case Action::ClickRight:
            apply_click(MouseButton::Right, active, mode);
            break;

        case Action::ClickMiddle:
            apply_click(MouseButton::Middle, active, mode);
            break;

        case Action::ClickX1:
            apply_click(MouseButton::X1, active, mode);
            break;

        case Action::ClickX2:
            apply_click(MouseButton::X2, active, mode);
            break;

        case Action::DoubleClick:
            if (one_shot_fires(active, mode)) {
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
            if (one_shot_fires(active, mode)) {
                center_requested_.store(true);
            }
            break;

        case Action::ToggleEnabled:
            if (one_shot_fires(active, mode)) {
                set_enabled(!enabled_.load());
            }
            break;

        case Action::OpenDashboard:
            if (one_shot_fires(active, mode)) {
                dashboard_requested_.store(true);
            }
            break;

        case Action::SpeedUp:
            if (one_shot_fires(active, mode)) {
                motion_config_.speed_px_s = std::min(
                    motion_config_.speed_px_s * 1.25, MotionConfig::kMaxSpeed);
                motion_.configure(motion_config_);
                speed_change_.store(motion_config_.speed_px_s,
                                    std::memory_order_relaxed);
            }
            break;

        case Action::SpeedDown:
            if (one_shot_fires(active, mode)) {
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

void Engine::apply_click(MouseButton button, bool active, BindMode mode) {
    const uint32_t bit = 1u << static_cast<unsigned>(button);

    if (mode == BindMode::Tap) {
        // Tap reports `active == false` on press: a complete click. It does not
        // touch the held-button bits, which belong to Hold and Toggle bindings;
        // a config change that changes the mode releases everything anyway.
        if (!active) {
            backend_.button(button, true);
            backend_.button(button, false);
            bump(counters_.injected_buttons);
        }
        return;
    }

    // Hold and Toggle report the intended button state directly. Tracking the
    // bit lets release_all() let go of a button that is still held when the
    // engine is disabled or the session is lost, so the desktop is never left
    // in a drag.
    if (active) {
        const uint32_t was = held_buttons_.fetch_or(bit, std::memory_order_relaxed);
        if ((was & bit) == 0) {
            backend_.button(button, true);
            bump(counters_.injected_buttons);
        }
    } else {
        const uint32_t was = held_buttons_.fetch_and(~bit, std::memory_order_relaxed);
        if ((was & bit) != 0) {
            backend_.button(button, false);
            bump(counters_.injected_buttons);
        }
    }
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
    const uint32_t buttons = held_buttons_.load(std::memory_order_relaxed);
    set(Action::ClickLeft, (buttons & (1u << static_cast<unsigned>(MouseButton::Left))) != 0);
    set(Action::ClickRight, (buttons & (1u << static_cast<unsigned>(MouseButton::Right))) != 0);
    set(Action::ClickMiddle, (buttons & (1u << static_cast<unsigned>(MouseButton::Middle))) != 0);
    set(Action::ClickX1, (buttons & (1u << static_cast<unsigned>(MouseButton::X1))) != 0);
    set(Action::ClickX2, (buttons & (1u << static_cast<unsigned>(MouseButton::X2))) != 0);
    active_mask_.store(mask, std::memory_order_relaxed);
}

int Engine::tick(double dt_seconds) {
    // Macro playback sits in front of the master switch: turning the engine off
    // with the panic key stops input handling, and the panic path stops the
    // player explicitly, but starting a macro from the dashboard must not
    // silently freeze when the switch happens to be off.
    player_.tick(clock_());
    publish_player_status();

    if (!enabled_.load(std::memory_order_relaxed) || dt_seconds <= 0.0) {
        return 0;
    }

    const auto started = std::chrono::steady_clock::now();

    bump(counters_.ticks);

    int injected = 0;

    // Keep the scheduler's master switch in sync with the atomic that the UI
    // and the hotkey write.
    const bool auto_enabled = auto_click_enabled_.load(std::memory_order_relaxed);
    if (scheduler_.enabled() != auto_enabled) {
        scheduler_.set_enabled(auto_enabled);
    }

    MotionInput input;
    input.up = move_up_.load(std::memory_order_relaxed);
    input.down = move_down_.load(std::memory_order_relaxed);
    input.left = move_left_.load(std::memory_order_relaxed);
    input.right = move_right_.load(std::memory_order_relaxed);
    input.precision = precision_.load(std::memory_order_relaxed);
    input.turbo = turbo_.load(std::memory_order_relaxed);

    const MotionDelta delta = motion_.step(input, dt_seconds);
    if (delta.dx != 0 || delta.dy != 0) {
        bool moved = false;
        if (motion_config_.method == MotionMethod::Absolute) {
            int32_t x = 0;
            int32_t y = 0;
            int32_t left = 0;
            int32_t top = 0;
            int32_t right = 0;
            int32_t bottom = 0;
            if (backend_.cursor_position(x, y) &&
                backend_.screen_bounds(left, top, right, bottom) &&
                right > left && bottom > top) {
                const int32_t nx = std::clamp<int32_t>(x + delta.dx, left, right - 1);
                const int32_t ny = std::clamp<int32_t>(y + delta.dy, top, bottom - 1);
                backend_.move_to(nx, ny);
                moved = true;
            }
        }
        if (!moved) {
            backend_.move(delta.dx, delta.dy);
        }
        bump(counters_.injected_moves);
        ++injected;
    }

    // Scheduled clicks run on the same monotonic clock as the rest of the
    // engine. poll() returns at most one action so two due tasks do not overlap.
    const int64_t now = now_ms();
    if (const auto action = scheduler_.poll(
            now, foreground_hash_.load(std::memory_order_relaxed))) {
        fire_auto_click(*action);
        auto_click_fires_.fetch_add(1, std::memory_order_relaxed);
        ++injected;
    }
    auto_click_next_due_ms_.store(
        scheduler_.enabled() ? scheduler_.next_due_ms() : -1,
        std::memory_order_relaxed);

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

void Engine::fire_auto_click(AutoClickAction action) {
    const auto click = [this](MouseButton button, int count) {
        for (int i = 0; i < count; ++i) {
            backend_.button(button, true);
            backend_.button(button, false);
        }
        counters_.injected_buttons.fetch_add(static_cast<uint64_t>(count),
                                             std::memory_order_relaxed);
    };

    switch (action) {
        case AutoClickAction::ClickLeft: click(MouseButton::Left, 1); break;
        case AutoClickAction::ClickRight: click(MouseButton::Right, 1); break;
        case AutoClickAction::ClickMiddle: click(MouseButton::Middle, 1); break;
        case AutoClickAction::DoubleClick: click(MouseButton::Left, 2); break;
    }
}

void Engine::release_all() {
    router_.release_all();
    mouse_router_.release_all();
    // A halted player must never leave a button or key held down.
    player_.stop();
    // Unconditionally drop a drag lock if one is engaged. The helper is a
    // no-op when the lock is already off.
    apply_drag_lock(false);
    // Same for buttons pressed by a Hold or Toggle click binding.
    const uint32_t buttons = held_buttons_.exchange(0, std::memory_order_relaxed);
    for (unsigned i = 0; i < 5; ++i) {
        if ((buttons & (1u << i)) != 0) {
            backend_.button(static_cast<MouseButton>(i), false);
            bump(counters_.injected_buttons);
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

void Engine::set_capture_mode(bool capturing) {
    const bool was = capture_mode_.exchange(capturing, std::memory_order_relaxed);
    if (!capturing || was) {
        return;
    }

    // The recorder consumes the next key-up, so a toggle that is being held
    // right now will never see its release. Forget the physical state; in hold
    // mode the mode itself must also end, or plain keys would stay armed for
    // the rest of the session.
    forget_toggle_states();
    if (mouse_hold_.load(std::memory_order_relaxed) &&
        mouse_mode_.exchange(false, std::memory_order_relaxed)) {
        release_requested_.store(true, std::memory_order_relaxed);
    }
}

void Engine::set_mouse_mode(bool on) {
    const bool was = mouse_mode_.exchange(on, std::memory_order_relaxed);
    if (was == on) {
        return;
    }
    // The engine thread releases any held key; a toggle during a held "I"
    // must not keep the cursor moving.
    release_requested_.store(true, std::memory_order_relaxed);
    if (mouse_beep_.load(std::memory_order_relaxed)) {
        beep_requested_.store(true, std::memory_order_relaxed);
    }
}

void Engine::forget_toggle_states() {
    mouse_toggle_down_.store(false, std::memory_order_relaxed);
    auto_click_toggle_down_.store(false, std::memory_order_relaxed);
    macro_record_down_.store(false, std::memory_order_relaxed);
    macro_play_down_.store(false, std::memory_order_relaxed);
    macro_stop_down_.store(false, std::memory_order_relaxed);
    macro_hotkey_key_.store(key::Invalid, std::memory_order_relaxed);
    panic_down_.store(false, std::memory_order_relaxed);
}

void Engine::set_auto_click_enabled(bool on) {
    auto_click_enabled_.store(on, std::memory_order_relaxed);
}

int64_t Engine::auto_click_remaining_ms() const {
    if (!auto_click_enabled_.load(std::memory_order_relaxed)) {
        return -1;
    }
    const int64_t due = auto_click_next_due_ms_.load(std::memory_order_relaxed);
    if (due < 0) {
        return -1;
    }
    const int64_t now = now_ms();
    return due > now ? due - now : 0;
}

void Engine::set_foreground_exe(std::string_view exe) {
    foreground_hash_.store(AutoClickScheduler::hash_executable(exe),
                           std::memory_order_relaxed);
}

void Engine::start_macro_recording(bool record_keyboard) {
    recording_keyboard_.store(record_keyboard, std::memory_order_relaxed);
    record_start_requested_.store(true, std::memory_order_relaxed);
}

void Engine::stop_macro_recording() {
    record_stop_requested_.store(true, std::memory_order_relaxed);
}

bool Engine::consume_recorded_macro(Macro& out) {
    if (!recorded_macro_ready_.exchange(false, std::memory_order_relaxed)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(recorded_macro_mutex_);
    if (!recorded_macro_) {
        return false;
    }
    out = std::move(*recorded_macro_);
    recorded_macro_.reset();
    return true;
}

void Engine::play_macro(std::shared_ptr<const Macro> macro,
                        const MacroPlaybackOptions& options) {
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        pending_macro_ = std::move(macro);
        pending_options_ = options;
    }
    playback_pending_.store(true, std::memory_order_release);
}

bool Engine::consume_macro_hotkey(std::string& id) {
    if (!macro_hotkey_requested_.exchange(false, std::memory_order_relaxed)) {
        return false;
    }
    const MacroHotkeyTable* table =
        macro_hotkey_table_seen_.load(std::memory_order_acquire);
    const size_t index = macro_hotkey_index_.load(std::memory_order_relaxed);
    if (table == nullptr || index >= table->size()) {
        return false;
    }
    id = table->entry(index).id;
    return true;
}

void Engine::on_mouse_event(const MacroInputEvent& event) {
    if (!macro_mouse_ring_.push(event)) {
        bump(counters_.dropped_events);
    }
}

void Engine::apply_macro_requests(int64_t now_us) {
    if (record_start_requested_.exchange(false, std::memory_order_relaxed)) {
        if (!recorder_.active()) {
            int32_t left = 0;
            int32_t top = 0;
            int32_t right = 0;
            int32_t bottom = 0;
            int32_t width = 0;
            int32_t height = 0;
            if (backend_.screen_bounds(left, top, right, bottom)) {
                width = right - left;
                height = bottom - top;
            }
            recorder_.start(recording_keyboard_.load(std::memory_order_relaxed),
                            macro_tolerance_px_.load(std::memory_order_relaxed),
                            width, height);
            recording_active_.store(true, std::memory_order_relaxed);
            recording_steps_.store(0, std::memory_order_relaxed);
        }
    }

    if (record_stop_requested_.exchange(false, std::memory_order_relaxed)) {
        if (recorder_.active()) {
            Macro macro = recorder_.finish({}, {});
            {
                std::lock_guard<std::mutex> lock(recorded_macro_mutex_);
                recorded_macro_ = std::move(macro);
            }
            recorded_macro_ready_.store(true, std::memory_order_relaxed);
        }
        recording_active_.store(false, std::memory_order_relaxed);
    }

    if (macro_stop_requested_.exchange(false, std::memory_order_relaxed)) {
        player_.stop();
    }
    if (player_pause_requested_.exchange(false, std::memory_order_relaxed)) {
        player_.pause(now_us);
    }
    if (player_resume_requested_.exchange(false, std::memory_order_relaxed)) {
        player_.resume(now_us);
    }

    if (playback_pending_.exchange(false, std::memory_order_acquire)) {
        std::shared_ptr<const Macro> macro;
        MacroPlaybackOptions options;
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            macro = std::move(pending_macro_);
            options = pending_options_;
        }
        if (macro) {
            player_.start(macro, options, now_us);
        }
    }
}

void Engine::drain_macro_events(int64_t now_us) {
    const bool recording = recording_active_.load(std::memory_order_relaxed);

    MacroInputEvent event;
    while (macro_mouse_ring_.pop(event)) {
        if (recording) {
            recorder_.add(event);
        } else if (!event.injected &&
                   player_.state() == MacroPlayerState::Playing) {
            // Physical mouse activity always wins over a playing macro. The
            // player's own events carry the KeyMouse tag, so this only fires on
            // the real mouse. A button release is not activity: the release of
            // the very click that pressed Play lands after playback has started,
            // and pausing on it would stop every mouse-started macro instantly.
            if (event.type != MacroInputEvent::Type::ButtonUp) {
                player_.pause(now_us);
            }
        }
    }
    while (macro_key_ring_.pop(event)) {
        if (recording) {
            recorder_.add(event);
        }
    }

    if (recording) {
        recording_steps_.store(recorder_.step_count(), std::memory_order_relaxed);
    }
}

void Engine::publish_player_status() {
    player_state_.store(player_.state(), std::memory_order_relaxed);
    player_step_.store(player_.current_step(), std::memory_order_relaxed);
    player_steps_.store(player_.steps_executed(), std::memory_order_relaxed);
    player_loops_.store(player_.loops_completed(), std::memory_order_relaxed);
}

}  // namespace km
