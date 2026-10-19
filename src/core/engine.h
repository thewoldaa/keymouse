// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "core/action.h"
#include "core/binding.h"
#include "core/binding_table.h"
#include "core/chord.h"
#include "core/config.h"
#include "core/motion.h"
#include "core/router.h"
#include "core/scheduler.h"
#include "core/spsc_ring.h"
#include "input/input_backend.h"

namespace km {

// Point-in-time copy of the engine counters.
//
// The live counters are incremented from the hook thread and the engine thread
// while the dashboard reads them, so they are atomics and are handed out as a
// snapshot rather than by reference.
struct EngineStats {
    uint64_t ticks = 0;
    uint64_t input_events = 0;
    uint64_t queued_events = 0;
    uint64_t dropped_events = 0;
    uint64_t swallowed_events = 0;
    uint64_t injected_moves = 0;
    uint64_t injected_buttons = 0;
    uint64_t injected_scrolls = 0;
    uint64_t focus_losses = 0;
    double last_tick_ms = 0.0;
};

// One resolved key event, produced on the hook thread and consumed on the
// engine thread.
struct QueuedEvent {
    enum class Kind : uint8_t { KeyDown, KeyUp, FocusLost, SessionLost };
    Kind kind = Kind::KeyDown;
    KeyId key = key::Invalid;
    ModMask mods = ModNone;
};

// The part of the configuration the engine owns.
//
// It is copied by apply_config() on the UI thread and handed to the engine
// thread, which is the only thread allowed to touch Router and Motion. A whole
// snapshot is copied rather than individual fields so a half-updated
// configuration can never be applied.
struct EngineConfig {
    MotionConfig motion;
    double scroll_lines_per_second = 8.0;
    std::vector<Binding> bindings;
    // The plain-key table used only while mouse mode is on.
    std::vector<Binding> mouse_bindings;
    KeyId fn_layer_key = key::Menu;
    Chord panic_key{ModNone, key::Pause};
    Chord mouse_toggle_key{ModCtrl | ModAlt, key::KeyM};
    bool mouse_hold_to_activate = false;
    bool mouse_beep = true;
    Chord auto_click_toggle_key{ModCtrl | ModAlt, key::KeyT};
    std::vector<AutoClickTask> auto_click_tasks;
    bool auto_click_enabled = false;
};

// Converts key events into cursor motion and button presses.
//
// Threading contract:
//   * on_input_event() runs on the hook thread. It may only read atomics and
//     the immutable BindingTable, and push to a lock-free ring. No allocation,
//     no locks, no I/O.
//   * pump() and tick() run on the engine thread. Router and Motion are
//     private to that thread.
//   * apply_config(), set_enabled() and set_capture_mode() may be called from
//     any thread. They only touch atomics plus the immutable binding table, and
//     defer everything else to the engine thread.
//   * stats() builds a snapshot from atomics and is safe from any thread.
class Engine {
public:
    explicit Engine(InputBackend& backend);
    ~Engine();

    // Publishes a new configuration.
    //
    // The binding table is published immediately, because the hook thread needs
    // it to decide what to swallow. The router and motion settings are applied
    // by the engine thread at the start of the next pump(), which is also where
    // held keys are released.
    void apply_config(const AppConfig& config);

    // Enables or disables the engine. Disabling releases every held action;
    // the release itself happens on the engine thread.
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

    // While capture mode is on, events are neither swallowed nor acted on, so
    // the dashboard can record a chord that is already bound. The hook still
    // runs; it just stops interpreting.
    void set_capture_mode(bool capturing);
    bool capture_mode() const { return capture_mode_.load(std::memory_order_relaxed); }

    // Mouse mode: the plain-key table that only acts while this is on.
    // Safe from any thread; the engine thread releases held keys.
    void set_mouse_mode(bool on);
    bool mouse_mode() const { return mouse_mode_.load(std::memory_order_relaxed); }

    // Drop the "the toggle key is physically down" state. Windows can swallow
    // the matching key-up when the session locks or the hook is replaced, and
    // without this the next press of a toggle key would be treated as
    // auto-repeat and ignored.
    void forget_toggle_states();

    // Scheduled clicking. The enabled flag mirrors the master switch in the
    // config; the scheduler itself is configured on the engine thread.
    void set_auto_click_enabled(bool on);
    bool auto_click_enabled() const {
        return auto_click_enabled_.load(std::memory_order_relaxed);
    }

    // Hotkey/UI feedback consumed by the application layer.
    bool consume_beep_request() {
        return beep_requested_.exchange(false, std::memory_order_relaxed);
    }
    // True when the auto-click hotkey changed the switch, so the caller can
    // persist the new state. The state itself is read with auto_click_enabled().
    bool consume_auto_click_change() {
        return auto_click_change_.exchange(false, std::memory_order_relaxed);
    }
    // Milliseconds until the next scheduled click, or -1 when none is armed.
    int64_t auto_click_remaining_ms() const;
    uint64_t auto_click_fires() const {
        return auto_click_fires_.load(std::memory_order_relaxed);
    }

    // Lowercased foreground executable name, reduced to a hash for the
    // only_if_app filter. Safe from any thread.
    void set_foreground_exe(std::string_view exe);

    // Hook thread entry point. Returns true when the event must be consumed.
    bool on_input_event(const InputEvent& event);

    // Engine thread: drains queued events. Returns the number processed.
    size_t pump();

    // Engine thread: advances motion and continuous scroll by dt_seconds.
    // Returns the number of injected events.
    int tick(double dt_seconds);

    // Releases every held action and resets motion. Engine thread, or any
    // thread once the engine thread has been stopped.
    void release_all();

    // Asks the engine thread to release everything on its next pump. Safe from
    // any thread; used when a key may be held while the desktop changes.
    void request_release() {
        release_requested_.store(true, std::memory_order_relaxed);
    }

    EngineStats stats() const;

    const BindingTable* bindings() const { return table_store_.current(); }

    // Bit i is set while Action(i) is held or toggled on. Updated on the
    // engine thread, read by the dashboard. Useful because a binding that does
    // not fire is otherwise invisible.
    uint32_t active_mask() const { return active_mask_.load(std::memory_order_relaxed); }
    bool drag_lock() const { return drag_lock_.load(std::memory_order_relaxed); }

    // Requests consumed by the application layer on the UI thread.
    bool consume_center_request() {
        return center_requested_.exchange(false, std::memory_order_relaxed);
    }
    bool consume_dashboard_request() {
        return dashboard_requested_.exchange(false, std::memory_order_relaxed);
    }
    // Returns the speed a SpeedUp/SpeedDown binding selected, so the dashboard
    // can persist it. nullopt when nothing changed.
    std::optional<double> consume_speed_change();

private:
    // Engine thread only.
    void apply_pending_config();
    void handle_action(Action action, bool active);
    void apply_drag_lock(bool engaged);
    void refresh_active_mask();
    void fire_auto_click(AutoClickAction action);

    static void bump(std::atomic<uint64_t>& counter) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    InputBackend& backend_;
    BindingTableStore table_store_;
    BindingTableStore mouse_table_store_;
    Router router_;
    Router mouse_router_;
    Motion motion_;
    AutoClickScheduler scheduler_;

    // Hook thread -> engine thread.
    SpscRing<QueuedEvent, 512> queue_{};

    // Which key-downs were swallowed on the hook thread. The matching key-up
    // must be swallowed even if the modifier state changed or the mode was
    // switched in between, otherwise the application sees a release it never
    // saw pressed.
    std::array<std::atomic<bool>, 256> swallowed_keys_{};

    // UI thread -> engine thread configuration handoff.
    mutable std::mutex config_mutex_;
    std::shared_ptr<const EngineConfig> pending_config_;
    std::atomic<bool> config_pending_{false};

    std::atomic<bool> enabled_{true};
    std::atomic<bool> capture_mode_{false};
    std::atomic<bool> release_requested_{false};
    std::atomic<bool> drag_lock_{false};
    std::atomic<bool> center_requested_{false};
    std::atomic<bool> dashboard_requested_{false};
    std::atomic<uint32_t> active_mask_{0};

    // Published by apply_config() for the hook thread. Read with relaxed
    // ordering: the values are independent of each other and a torn read is
    // impossible for either.
    std::atomic<uint16_t> panic_key_{key::Pause};
    std::atomic<uint8_t> panic_mods_{ModNone};

    // Mouse mode. The toggle is handled on the hook thread, like the panic key,
    // so it works even while the engine is disabled and the dashboard is hidden.
    std::atomic<bool> mouse_mode_{false};
    std::atomic<bool> mouse_hold_{false};
    std::atomic<bool> mouse_beep_{true};
    std::atomic<uint16_t> mouse_toggle_key_{key::KeyM};
    std::atomic<uint8_t> mouse_toggle_mods_{ModCtrl | ModAlt};
    std::atomic<bool> mouse_toggle_down_{false};
    std::atomic<bool> beep_requested_{false};

    // Auto-click master toggle, also handled on the hook thread.
    std::atomic<uint16_t> auto_click_toggle_key_{key::KeyT};
    std::atomic<uint8_t> auto_click_toggle_mods_{ModCtrl | ModAlt};
    std::atomic<bool> auto_click_toggle_down_{false};
    std::atomic<bool> auto_click_enabled_{false};
    std::atomic<bool> auto_click_change_{false};
    std::atomic<int64_t> auto_click_next_due_ms_{-1};
    std::atomic<uint64_t> auto_click_fires_{0};
    std::atomic<uint64_t> foreground_hash_{0};

    // Speed selected by a SpeedUp/SpeedDown binding, in px/s, or 0 when no
    // change is pending.
    std::atomic<double> speed_change_{0.0};

    // Action state written by the engine thread, read by tick().
    std::atomic<bool> move_up_{false};
    std::atomic<bool> move_down_{false};
    std::atomic<bool> move_left_{false};
    std::atomic<bool> move_right_{false};
    std::atomic<bool> scroll_up_{false};
    std::atomic<bool> scroll_down_{false};
    std::atomic<bool> scroll_left_{false};
    std::atomic<bool> scroll_right_{false};
    std::atomic<bool> precision_{false};
    std::atomic<bool> turbo_{false};

    // Counters, written from the hook thread and the engine thread.
    struct Counters {
        std::atomic<uint64_t> ticks{0};
        std::atomic<uint64_t> input_events{0};
        std::atomic<uint64_t> queued_events{0};
        std::atomic<uint64_t> dropped_events{0};
        std::atomic<uint64_t> swallowed_events{0};
        std::atomic<uint64_t> injected_moves{0};
        std::atomic<uint64_t> injected_buttons{0};
        std::atomic<uint64_t> injected_scrolls{0};
        std::atomic<uint64_t> focus_losses{0};
        std::atomic<int64_t> last_tick_us{0};
    };
    Counters counters_{};

    // Engine thread only.
    MotionConfig motion_config_{};
    double scroll_lines_per_second_ = 8.0;
    double scroll_remainder_ = 0.0;
    bool auto_click_initialized_ = false;
};

}  // namespace km
