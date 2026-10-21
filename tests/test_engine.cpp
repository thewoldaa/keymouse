// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "core/engine.h"
#include "input/injection.h"

using namespace km;

namespace {

class FakeBackend final : public InputBackend {
public:
    bool start(EventSink) override {
        running_ = true;
        return true;
    }
    void stop() override { running_ = false; }
    bool running() const override { return running_; }

    void move(int32_t dx, int32_t dy) override {
        ++moves;
        last_dx = dx;
        last_dy = dy;
    }
    void button(MouseButton button, bool down) override {
        buttons.emplace_back(button, down);
    }
    void scroll(Axis, int32_t) override { ++scrolls; }

    void key(KeyId target, bool down) override {
        keys.emplace_back(target, down);
        key_while_tagged = tagged_injections;
    }
    void set_injection_tag(bool tagged) override { tagged_injections = tagged; }

    void move_to(int32_t x, int32_t y) override {
        ++absolute_moves;
        last_x = x;
        last_y = y;
    }
    bool cursor_position(int32_t& x, int32_t& y) override {
        x = 500;
        y = 500;
        return true;
    }
    bool screen_bounds(int32_t& left, int32_t& top, int32_t& right,
                       int32_t& bottom) override {
        left = 0;
        top = 0;
        right = 1920;
        bottom = 1080;
        return true;
    }

    uint64_t dropped_events() const override { return 0; }
    const char* name() const override { return "fake"; }

    bool running_ = false;
    int moves = 0;
    int absolute_moves = 0;
    int scrolls = 0;
    int32_t last_dx = 0;
    int32_t last_dy = 0;
    int32_t last_x = 0;
    int32_t last_y = 0;
    std::vector<std::pair<MouseButton, bool>> buttons;
    std::vector<std::pair<KeyId, bool>> keys;
    bool tagged_injections = false;
    bool key_while_tagged = false;
};

InputEvent event(InputEvent::Type type, KeyId key, uint8_t mods = ModNone) {
    InputEvent out;
    out.type = type;
    out.key = key;
    out.mods = mods;
    return out;
}

bool active(const Engine& engine, Action action) {
    return (engine.active_mask() & (1u << static_cast<unsigned>(action))) != 0;
}

Binding plain(Action action, KeyId key, BindMode mode = BindMode::Hold) {
    Binding binding;
    binding.action = action;
    binding.chord = Chord{ModNone, key};
    binding.mode = mode;
    binding.swallow = SwallowMode::Swallow;
    return binding;
}

Binding with_mods(Action action, ModMask mods, KeyId key, BindMode mode = BindMode::Hold) {
    Binding binding = plain(action, key, mode);
    binding.chord.mods = mods;
    return binding;
}

}  // namespace

TEST_CASE("a click bound in hold mode presses the button until release") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.active().bindings = {
        with_mods(Action::ClickLeft, ModNone, key::KeyU, BindMode::Hold),
    };
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyU)));
    engine.pump();

    // Pressed, not tapped: exactly one down event and no up yet.
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].first == MouseButton::Left);
    CHECK(backend.buttons[0].second);
    CHECK(active(engine, Action::ClickLeft));

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyU)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK_FALSE(active(engine, Action::ClickLeft));
}

TEST_CASE("a click bound in toggle mode holds until pressed again") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.active().bindings = {
        with_mods(Action::ClickRight, ModNone, key::KeyO, BindMode::Toggle),
    };
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyO)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].first == MouseButton::Right);
    CHECK(backend.buttons[0].second);

    // Releasing the key leaves the button down; the second press lets go.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyO)));
    engine.pump();
    CHECK(backend.buttons.size() == 1);

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyO)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK_FALSE(active(engine, Action::ClickRight));

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyO)));
}

TEST_CASE("release_all lets go of a button held by a click binding") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.active().bindings = {
        with_mods(Action::ClickMiddle, ModNone, key::KeyP, BindMode::Hold),
    };
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyP)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].second);

    // Disabling the engine must not leave the desktop in a drag.
    engine.set_enabled(false);
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK_FALSE(active(engine, Action::ClickMiddle));
}

TEST_CASE("a tap click still fires once on press") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.active().bindings = {
        with_mods(Action::ClickLeft, ModNone, key::KeyU, BindMode::Tap),
    };
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyU)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK(backend.buttons[0].second);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyU)));
    engine.pump();
    CHECK(backend.buttons.size() == 2);
}

TEST_CASE("mouse mode uses plain keys and keeps the chord table dormant") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.enabled = true;
    config.active().bindings = {plain(Action::MoveDown, key::KeyI)};
    config.mouse_mode.bindings = {
        plain(Action::MoveUp, key::KeyI),
        plain(Action::ClickLeft, key::KeyU, BindMode::Tap),
    };

    engine.apply_config(config);
    engine.pump();
    CHECK_FALSE(engine.mouse_mode());

    // While the mode is off, the plain key belongs to the normal table.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyI)));
    engine.pump();
    CHECK(active(engine, Action::MoveDown));
    CHECK_FALSE(active(engine, Action::MoveUp));
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyI)));
    engine.pump();
    CHECK_FALSE(active(engine, Action::MoveDown));

    engine.set_mouse_mode(true);
    engine.consume_beep_request();
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyI)));
    engine.pump();
    CHECK(active(engine, Action::MoveUp));
    CHECK_FALSE(active(engine, Action::MoveDown));

    const uint64_t moves_before = engine.stats().injected_moves;
    engine.tick(0.1);
    CHECK(engine.stats().injected_moves > moves_before);
    CHECK(backend.moves > 0);

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyI)));
    engine.pump();
    CHECK_FALSE(active(engine, Action::MoveUp));

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyU)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK(backend.buttons[0].first == MouseButton::Left);
    CHECK(backend.buttons[0].second);
    CHECK_FALSE(backend.buttons[1].second);
}

TEST_CASE("the mouse toggle key works without the dashboard") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.mouse_mode.beep = false;
    config.mouse_mode.toggle_key = Chord{ModCtrl | ModAlt, key::KeyM};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyM, ModCtrl | ModAlt)));
    CHECK(engine.mouse_mode());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyM, ModCtrl | ModAlt)));
    CHECK(engine.mouse_mode());

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyM, ModCtrl | ModAlt)));
    CHECK_FALSE(engine.mouse_mode());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyM, ModCtrl | ModAlt)));

    // The plain key is not the toggle chord, so neither half of it may be
    // swallowed. Losing only the key-up leaves "M" stuck in the foreground app.
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyM)));
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyM)));
}

TEST_CASE("hold behaviour only keeps mouse mode while the key is down") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.mouse_mode.beep = false;
    config.mouse_mode.hold_to_activate = true;
    config.mouse_mode.toggle_key = Chord{ModNone, key::KeyM};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyM)));
    CHECK(engine.mouse_mode());
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyM)));
    CHECK_FALSE(engine.mouse_mode());
}

TEST_CASE("the auto-click key toggles the master switch") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.auto_click.enabled = false;
    config.auto_click.toggle_key = Chord{ModCtrl | ModAlt, key::KeyT};
    engine.apply_config(config);
    engine.pump();

    CHECK_FALSE(engine.auto_click_enabled());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyT, ModCtrl | ModAlt)));
    CHECK(engine.auto_click_enabled());
    CHECK(engine.consume_auto_click_change());
    CHECK_FALSE(engine.consume_auto_click_change());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyT, ModCtrl | ModAlt)));

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyT, ModCtrl | ModAlt)));
    CHECK_FALSE(engine.auto_click_enabled());
    CHECK(engine.consume_auto_click_change());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyT, ModCtrl | ModAlt)));

    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyT)));
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyT)));
}

TEST_CASE("forgotten toggle state accepts a fresh press after lock") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.mouse_mode.beep = false;
    config.mouse_mode.toggle_key = Chord{ModCtrl | ModAlt, key::KeyM};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyM, ModCtrl | ModAlt)));
    CHECK(engine.mouse_mode());

    // Windows can drop the key-up when the hook is replaced at lock time.
    // Without forgetting the flag, this next press would look like auto-repeat.
    engine.forget_toggle_states();
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyM, ModCtrl | ModAlt)));
    CHECK_FALSE(engine.mouse_mode());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyM, ModCtrl | ModAlt)));
}

TEST_CASE("losing the session disarms mouse mode and held actions") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.mouse_mode.beep = false;
    config.mouse_mode.bindings = {plain(Action::MoveUp, key::KeyI)};
    engine.apply_config(config);
    engine.pump();

    engine.set_mouse_mode(true);
    engine.consume_beep_request();
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyI)));
    engine.pump();
    CHECK(active(engine, Action::MoveUp));

    CHECK_FALSE(engine.on_input_event(
        event(InputEvent::Type::SessionLost, key::Invalid)));
    engine.pump();

    CHECK_FALSE(engine.mouse_mode());
    CHECK_FALSE(active(engine, Action::MoveUp));
    // The press was swallowed before the session was lost, so the matching
    // release must be swallowed too.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyI)));
}

TEST_CASE("absolute movement uses the screen bounds") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.motion.method = MotionMethod::Absolute;
    config.mouse_mode.bindings = {plain(Action::MoveRight, key::KeyL)};
    engine.apply_config(config);
    engine.pump();

    engine.set_mouse_mode(true);
    engine.consume_beep_request();
    engine.pump();
    engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyL));
    engine.pump();
    engine.tick(0.1);

    CHECK(backend.absolute_moves > 0);
    CHECK(backend.moves == 0);
    CHECK(backend.last_x > 500);
    CHECK(backend.last_y == 500);
}

TEST_CASE("scheduled auto-click fires through the backend") {
    FakeBackend backend;
    int64_t clock_us = 0;
    Engine engine(backend, [&clock_us] { return clock_us; });

    AppConfig config = make_default_config();
    config.enabled = true;
    config.auto_click.enabled = true;

    AutoClickTask task;
    task.id = "scheduled";
    task.name = "Scheduled";
    task.action = AutoClickAction::ClickRight;
    task.interval_seconds = 1.0;
    task.start_delay_seconds = 0.005;
    task.repeat_count = 1;
    config.auto_click.tasks = {task};

    engine.apply_config(config);
    engine.pump();

    // The scheduler runs on the engine's injected clock, so the test advances
    // time instead of sleeping.
    clock_us = 20'000;
    CHECK(engine.tick(0.016) >= 1);
    CHECK(engine.auto_click_fires() == 1);

    bool saw_down = false;
    bool saw_up = false;
    for (const auto& [button, down] : backend.buttons) {
        if (button == MouseButton::Right && down) {
            saw_down = true;
        }
        if (button == MouseButton::Right && !down) {
            saw_up = true;
        }
    }
    CHECK(saw_down);
    CHECK(saw_up);

    // The task had a repeat count of one, so no next deadline remains.
    CHECK(engine.auto_click_remaining_ms() == -1);
}

namespace {

MacroStep macro_step(MacroStepType type, int64_t dt_ms, int32_t data = 0,
                     KeyId key = key::Invalid) {
    MacroStep step;
    step.type = type;
    step.dt_ms = dt_ms;
    step.data = data;
    step.key = key;
    return step;
}

MacroInputEvent mouse_event(MacroInputEvent::Type type, int64_t time_us, int32_t x,
                            int32_t y) {
    MacroInputEvent event;
    event.type = type;
    event.time_us = time_us;
    event.x = x;
    event.y = y;
    return event;
}

}  // namespace

TEST_CASE("recording turns mouse events into a macro") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });

    AppConfig config = make_default_config();
    config.macros.move_tolerance_px = 2.0;
    config.macros.record_keyboard = false;
    engine.apply_config(config);
    engine.pump();

    engine.start_macro_recording(false);
    engine.pump();
    CHECK(engine.macro_recording());
    CHECK(engine.macro_wants_mouse_hook());

    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 1000, 10, 10));
    // One pixel away: inside the tolerance circle, dropped.
    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 2000, 11, 10));
    // Thirty pixels away: kept, and it carries the accumulated delay.
    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 6000, 40, 10));

    MacroInputEvent down;
    down.type = MacroInputEvent::Type::ButtonDown;
    down.time_us = 10000;
    down.data = static_cast<int32_t>(MouseButton::Left);
    engine.on_mouse_event(down);

    // Playback output never lands in a recording.
    MacroInputEvent injected = mouse_event(MacroInputEvent::Type::MouseMove, 12000, 500, 500);
    injected.injected = true;
    engine.on_mouse_event(injected);

    engine.pump();
    CHECK(engine.macro_recording_steps() == 3);

    engine.stop_macro_recording();
    engine.pump();
    CHECK_FALSE(engine.macro_recording());
    CHECK_FALSE(engine.macro_wants_mouse_hook());

    Macro recorded;
    REQUIRE(engine.consume_recorded_macro(recorded));
    // The left button was still down when the recording stopped, so finish()
    // appends its release; the macro cannot end mid-drag.
    REQUIRE(recorded.steps.size() == 4);
    CHECK(recorded.recorded_width == 1920);
    CHECK(recorded.recorded_height == 1080);
    CHECK(recorded.steps[0].type == MacroStepType::Move);
    CHECK(recorded.steps[0].dt_ms == 0);
    CHECK(recorded.steps[0].x == 10);
    CHECK(recorded.steps[1].type == MacroStepType::Move);
    CHECK(recorded.steps[1].dt_ms == 5);
    CHECK(recorded.steps[1].x == 40);
    CHECK(recorded.steps[2].type == MacroStepType::ButtonDown);
    CHECK(recorded.steps[2].dt_ms == 4);
    CHECK(recorded.steps[2].data == static_cast<int32_t>(MouseButton::Left));
    CHECK(recorded.steps[3].type == MacroStepType::ButtonUp);
    CHECK(recorded.steps[3].dt_ms == 0);
    CHECK(recorded.steps[3].data == static_cast<int32_t>(MouseButton::Left));

    // The finished macro is handed out exactly once.
    Macro again;
    CHECK_FALSE(engine.consume_recorded_macro(again));
}

TEST_CASE("recording a camera drag produces relative steps and balanced releases") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });

    AppConfig config = make_default_config();
    config.macros.move_tolerance_px = 2.0;
    config.macros.record_keyboard = true;
    config.macros.record_pov_drag = true;
    engine.apply_config(config);
    engine.pump();

    engine.start_macro_recording(true);
    engine.pump();
    REQUIRE(engine.macro_recording());

    // Right button already held before Record: the hook reports it on the
    // move, the drag is a camera turn from the very first sample.
    MacroInputEvent move = mouse_event(MacroInputEvent::Type::MouseMove, 1000, 400, 300);
    move.buttons_down = 1u << static_cast<unsigned>(MouseButton::Right);
    engine.on_mouse_event(move);

    move = mouse_event(MacroInputEvent::Type::MouseMove, 2000, 430, 290);
    move.buttons_down = 1u << static_cast<unsigned>(MouseButton::Right);
    engine.on_mouse_event(move);

    // A key held while the camera turns, then its OS auto-repeat.
    InputEvent key_down = event(InputEvent::Type::KeyDown, key::KeyW);
    key_down.time_us = 3000;
    CHECK_FALSE(engine.on_input_event(key_down));
    key_down.time_us = 4000;
    CHECK_FALSE(engine.on_input_event(key_down));

    engine.pump();
    CHECK(engine.macro_recording_camera_steps() == 1);
    CHECK(engine.macro_recording_key_steps() == 1);

    // Stop while the button and W are both still down.
    engine.stop_macro_recording();
    engine.pump();

    Macro recorded;
    REQUIRE(engine.consume_recorded_macro(recorded));
    // Three steps: the camera delta, and the W press and release that the
    // recording witnessed. The right button was already down when Record was
    // pressed, so the macro neither presses nor releases it — replaying an
    // unrequested press would be far worse than assuming the user still holds
    // the aim button, which is how the recording was made.
    REQUIRE(recorded.steps.size() == 3);
    CHECK(recorded.steps[0].type == MacroStepType::MouseDelta);
    CHECK(recorded.steps[0].x == 30);
    CHECK(recorded.steps[0].y == -10);
    CHECK(recorded.steps[1].type == MacroStepType::KeyDown);
    CHECK(recorded.steps[1].key == key::KeyW);
    CHECK(recorded.steps[2].type == MacroStepType::KeyUp);
    CHECK(recorded.steps[2].key == key::KeyW);
}

TEST_CASE("keyboard capture only records keys when it is on") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.macros.record_keyboard = true;
    engine.apply_config(config);
    engine.pump();

    engine.start_macro_recording(true);
    engine.pump();

    InputEvent down = event(InputEvent::Type::KeyDown, key::KeyW);
    down.time_us = 5000;
    CHECK_FALSE(engine.on_input_event(down));
    InputEvent up = event(InputEvent::Type::KeyUp, key::KeyW);
    up.time_us = 9000;
    CHECK_FALSE(engine.on_input_event(up));

    engine.stop_macro_recording();
    engine.pump();

    Macro recorded;
    REQUIRE(engine.consume_recorded_macro(recorded));
    REQUIRE(recorded.steps.size() == 2);
    CHECK(recorded.steps[0].type == MacroStepType::KeyDown);
    CHECK(recorded.steps[0].key == key::KeyW);
    CHECK(recorded.steps[1].type == MacroStepType::KeyUp);
    CHECK(recorded.steps[1].dt_ms == 4);
}

TEST_CASE("playback pauses, resumes, and finishes on its own clock") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });
    engine.apply_config(make_default_config());
    engine.pump();

    auto macro = std::make_shared<Macro>();
    MacroStep move = macro_step(MacroStepType::Move, 0);
    move.x = 100;
    move.y = 200;
    macro->steps = {
        move,
        macro_step(MacroStepType::ButtonDown, 10,
                   static_cast<int32_t>(MouseButton::Left)),
        macro_step(MacroStepType::ButtonUp, 10,
                   static_cast<int32_t>(MouseButton::Left)),
    };

    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);
    CHECK(engine.macro_wants_mouse_hook());

    engine.tick(0.01);
    CHECK(backend.absolute_moves == 1);
    CHECK(backend.last_x == 100);
    CHECK(backend.last_y == 200);
    CHECK(engine.macro_player_step() == 1);

    clock = 10000;
    engine.tick(0.01);
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].second);

    // Pause with a button still held: the player lets go so a long pause does
    // not leave the desktop stuck in a drag.
    engine.request_macro_pause();
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Paused);
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);

    // Ticks while paused execute nothing.
    clock = 90000;
    engine.tick(0.01);
    CHECK(backend.buttons.size() == 2);

    engine.request_macro_resume();
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);
    REQUIRE(backend.buttons.size() == 3);
    CHECK(backend.buttons[2].second);

    // The remaining delay of the pending step is kept across the pause.
    clock = 99999;
    engine.tick(0.01);
    CHECK(backend.buttons.size() == 3);
    clock = 100000;
    engine.tick(0.01);
    REQUIRE(backend.buttons.size() == 4);
    CHECK_FALSE(backend.buttons[3].second);
    CHECK(engine.macro_player_state() == MacroPlayerState::Idle);
    CHECK(engine.macro_player_loops() == 1);
    CHECK_FALSE(engine.macro_wants_mouse_hook());
}

TEST_CASE("the player tags its injections and releases held input on stop") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });
    engine.apply_config(make_default_config());
    engine.pump();

    auto macro = std::make_shared<Macro>();
    macro->steps = {
        macro_step(MacroStepType::KeyDown, 0, 0, key::KeyW),
        macro_step(MacroStepType::ButtonDown, 0,
                   static_cast<int32_t>(MouseButton::Left)),
        macro_step(MacroStepType::Wait, 60000),
    };

    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();

    clock = 5000;
    engine.tick(0.01);
    REQUIRE(backend.keys.size() == 1);
    CHECK(backend.keys[0].second);
    CHECK(backend.key_while_tagged);
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].second);

    engine.request_macro_stop();
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Idle);
    // Both are released, and the tag is off again afterwards.
    REQUIRE(backend.keys.size() == 2);
    CHECK_FALSE(backend.keys[1].second);
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK_FALSE(backend.tagged_injections);
}

TEST_CASE("a real mouse event pauses playback but an injected one does not") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });
    engine.apply_config(make_default_config());
    engine.pump();

    auto macro = std::make_shared<Macro>();
    macro->steps = {macro_step(MacroStepType::Wait, 60000)};
    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);

    MacroInputEvent injected = mouse_event(MacroInputEvent::Type::MouseMove, 1000, 10, 10);
    injected.injected = true;
    engine.on_mouse_event(injected);
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);

    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 2000, 500, 400));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Paused);
    CHECK(engine.macro_wants_mouse_hook());

    // A manual resume continues from the pause point.
    engine.request_macro_resume();
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);

    // The release of the click that pressed Play arrives after playback has
    // started; it is not activity and must not pause the macro.
    MacroInputEvent release;
    release.type = MacroInputEvent::Type::ButtonUp;
    release.time_us = 3000;
    release.data = static_cast<int32_t>(MouseButton::Left);
    engine.on_mouse_event(release);
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);

    // The press does.
    MacroInputEvent press;
    press.type = MacroInputEvent::Type::ButtonDown;
    press.time_us = 4000;
    press.data = static_cast<int32_t>(MouseButton::Left);
    engine.on_mouse_event(press);
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Paused);
}

TEST_CASE("Break stops playback") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });
    engine.apply_config(make_default_config());
    engine.pump();

    auto macro = std::make_shared<Macro>();
    macro->steps = {macro_step(MacroStepType::Wait, 60000)};
    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();
    REQUIRE(engine.macro_player_state() == MacroPlayerState::Playing);

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Pause)));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Idle);
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));
}

TEST_CASE("macro control chords surface as one-shot requests") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });

    AppConfig config = make_default_config();
    config.macros.record_key = Chord{ModCtrl | ModAlt, key::KeyR};
    config.macros.play_key = Chord{ModCtrl | ModAlt, key::KeyP};
    config.macros.stop_key = Chord{ModCtrl | ModAlt, key::KeyS};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyR, ModCtrl | ModAlt)));
    CHECK(engine.consume_macro_record_toggle());
    CHECK_FALSE(engine.consume_macro_record_toggle());
    // Auto-repeat must not queue a second toggle.
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyR, ModCtrl | ModAlt)));
    CHECK_FALSE(engine.consume_macro_record_toggle());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyR, ModCtrl | ModAlt)));

    // The plain key keeps its own life.
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyR)));
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyR)));

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyP, ModCtrl | ModAlt)));
    CHECK(engine.consume_macro_play_pause());
    CHECK_FALSE(engine.consume_macro_play_pause());
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyP, ModCtrl | ModAlt)));

    auto macro = std::make_shared<Macro>();
    macro->steps = {macro_step(MacroStepType::Wait, 60000)};
    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();
    REQUIRE(engine.macro_player_state() == MacroPlayerState::Playing);

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyS, ModCtrl | ModAlt)));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Idle);
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyS, ModCtrl | ModAlt)));
}

TEST_CASE("per-macro hotkeys resolve through the published table") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.macros.entries = {
        MacroEntry{"farm", "Farm", Chord{ModAlt, key::KeyF}},
        MacroEntry{"trade", "Trade", Chord{ModCtrl | ModShift, key::KeyT}},
    };
    engine.apply_config(config);
    engine.pump();

    std::string id;
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyF, ModAlt)));
    REQUIRE(engine.consume_macro_hotkey(id));
    CHECK(id == "farm");
    CHECK_FALSE(engine.consume_macro_hotkey(id));

    // Holding the key down does not retrigger it.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyF, ModAlt)));
    CHECK_FALSE(engine.consume_macro_hotkey(id));
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyF, ModAlt)));

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyF, ModAlt)));
    REQUIRE(engine.consume_macro_hotkey(id));
    CHECK(id == "farm");
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyF, ModAlt)));

    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyT, ModCtrl | ModShift)));
    REQUIRE(engine.consume_macro_hotkey(id));
    CHECK(id == "trade");
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyUp, key::KeyT, ModCtrl | ModShift)));

    // Unbound chords pass through untouched.
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyG)));
}

TEST_CASE("events tagged as injected are ignored by the engine") {
    FakeBackend backend;
    Engine engine(backend);
    engine.apply_config(make_default_config());
    engine.pump();

    // Low-level hooks only deliver the low DWORD of dwExtraInfo, so a tag that
    // needs more than 32 bits would never match.
    CHECK(kInjectionTag != 0);
    CHECK(static_cast<uint64_t>(kInjectionTag) <= 0xFFFFFFFFull);

    InputEvent tagged = event(InputEvent::Type::KeyDown, key::KeyW);
    tagged.extra_info = kInjectionTag;
    CHECK_FALSE(engine.on_input_event(tagged));
    CHECK(engine.stats().input_events == 1);
    CHECK(engine.active_mask() == 0u);
}

TEST_CASE("the panic key swallows its release when modifiers go first") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.panic_key = Chord{ModCtrl, key::Pause};
    engine.apply_config(config);
    engine.pump();
    CHECK(engine.enabled());

    // The chord is consumed, and so is the release -- even though Ctrl is let
    // go before Pause, so the release reports no modifiers.
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::Pause, ModCtrl)));
    CHECK_FALSE(engine.enabled());
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));

    // A plain Pause was never consumed, so its release passes through.
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));
}

TEST_CASE("auto-repeat of a held panic key does not toggle back on") {
    FakeBackend backend;
    Engine engine(backend);
    AppConfig config = make_default_config();
    config.panic_key = Chord{ModNone, key::Pause};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Pause)));
    CHECK_FALSE(engine.enabled());
    // Windows repeats WM_KEYDOWN while the key is held; that must not flip the
    // engine back on under the user's finger.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Pause)));
    CHECK_FALSE(engine.enabled());
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));

    // The next deliberate press toggles it back on.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Pause)));
    CHECK(engine.enabled());
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));
}

TEST_CASE("the panic key stops a running macro recording and playback") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });

    AppConfig config = make_default_config();
    config.macros.record_keyboard = false;
    engine.apply_config(config);
    engine.pump();

    engine.start_macro_recording(false);
    engine.pump();
    REQUIRE(engine.macro_recording());

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Pause)));
    engine.pump();
    CHECK_FALSE(engine.macro_recording());
    // The fragment is cancelled, not handed to the application as a macro.
    Macro discarded;
    CHECK_FALSE(engine.consume_recorded_macro(discarded));
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));

    auto macro = std::make_shared<Macro>();
    MacroStep wait;
    wait.type = MacroStepType::Wait;
    wait.dt_ms = 60000;
    macro->steps = {wait};
    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();
    REQUIRE(engine.macro_player_state() == MacroPlayerState::Playing);

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Pause)));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Idle);
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Pause)));
}

TEST_CASE("a chorded control key keeps swallowing its auto-repeat after modifiers go") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.mouse_mode.beep = false;
    config.mouse_mode.toggle_key = Chord{ModCtrl | ModAlt, key::KeyM};
    engine.apply_config(config);
    engine.pump();

    // Consume the chord, then let go of the modifiers while M is still held.
    CHECK(engine.on_input_event(
        event(InputEvent::Type::KeyDown, key::KeyM, ModCtrl | ModAlt)));
    CHECK(engine.mouse_mode());

    // OS auto-repeat now arrives with no modifiers. It must still be swallowed:
    // otherwise the application sees a key-down whose matching key-up is
    // swallowed, leaving M stuck.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyM)));
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyM)));
    CHECK(engine.mouse_mode());

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyM)));
    // After the real release the plain key is free again.
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyM)));
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyM)));
}

TEST_CASE("a chorded macro hotkey keeps swallowing its auto-repeat") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.macros.entries = {MacroEntry{"farm", "Farm", Chord{ModAlt, key::KeyF}}};
    engine.apply_config(config);
    engine.pump();

    std::string id;
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyF, ModAlt)));
    REQUIRE(engine.consume_macro_hotkey(id));

    // Alt released while F is still held.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyF)));
    CHECK_FALSE(engine.consume_macro_hotkey(id));
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyF)));

    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyF)));
    CHECK_FALSE(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyF)));
}

TEST_CASE("a motion-only config change does not release a held movement key") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.motion.method = MotionMethod::Relative;
    config.active().bindings = {plain(Action::MoveRight, key::KeyL)};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyL)));
    engine.pump();
    CHECK(active(engine, Action::MoveRight));

    // Dragging the speed slider or pressing SpeedUp republishes the config
    // without changing any binding.
    config.motion.speed_px_s *= 1.25;
    engine.apply_config(config);
    engine.pump();

    CHECK(active(engine, Action::MoveRight));
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyL)));
    engine.pump();
    CHECK_FALSE(active(engine, Action::MoveRight));
}

TEST_CASE("a binding change still releases held actions") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.motion.method = MotionMethod::Relative;
    config.active().bindings = {plain(Action::MoveRight, key::KeyL)};
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyL)));
    engine.pump();
    CHECK(active(engine, Action::MoveRight));

    config.active().bindings = {plain(Action::MoveLeft, key::KeyH)};
    engine.apply_config(config);
    engine.pump();
    CHECK_FALSE(active(engine, Action::MoveRight));
}

TEST_CASE("the drag lock and a left-click binding share one button state") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.active().bindings = {
        with_mods(Action::DragLock, ModNone, key::Space, BindMode::Toggle),
        with_mods(Action::ClickLeft, ModNone, key::KeyU, BindMode::Toggle),
    };
    engine.apply_config(config);
    engine.pump();

    // Lock the drag, then toggle a left-click hold on: the button is already
    // down, so no second press is injected.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Space)));
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Space)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].second);

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyU)));
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyU)));
    engine.pump();
    CHECK(backend.buttons.size() == 1);

    // Turning the click binding off must not release the drag lock's button.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyU)));
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::KeyU)));
    engine.pump();
    CHECK(backend.buttons.size() == 1);

    // Unlocking the drag ends with exactly one release.
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Space)));
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Space)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
}

TEST_CASE("release_all emits one release even with a drag lock and a held click") {
    FakeBackend backend;
    Engine engine(backend);

    AppConfig config = make_default_config();
    config.active().bindings = {
        with_mods(Action::DragLock, ModNone, key::Space, BindMode::Toggle),
        with_mods(Action::ClickLeft, ModNone, key::KeyU, BindMode::Hold),
    };
    engine.apply_config(config);
    engine.pump();

    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::Space)));
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyUp, key::Space)));
    engine.pump();
    CHECK(engine.on_input_event(event(InputEvent::Type::KeyDown, key::KeyU)));
    engine.pump();
    REQUIRE(backend.buttons.size() == 1);

    engine.set_enabled(false);
    engine.pump();
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK_FALSE(active(engine, Action::ClickLeft));
    CHECK_FALSE(engine.drag_lock());
}

TEST_CASE("micro-jitter does not pause playback but a real move does") {
    FakeBackend backend;
    int64_t clock = 0;
    Engine engine(backend, [&clock] { return clock; });

    AppConfig config = make_default_config();
    config.macros.move_tolerance_px = 3.0;
    engine.apply_config(config);
    engine.pump();

    auto macro = std::make_shared<Macro>();
    macro->steps = {macro_step(MacroStepType::Wait, 60000)};
    engine.play_macro(macro, MacroPlaybackOptions{});
    engine.pump();
    REQUIRE(engine.macro_player_state() == MacroPlayerState::Playing);

    // One pixel of sensor noise, as when the user clicks Play.
    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 1000, 100, 100));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);
    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 2000, 101, 100));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);

    // A real move does pause it.
    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 3000, 400, 300));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Paused);

    // Resume takes a fresh reference point relative to the real cursor, so a
    // jitter right after resuming does not pause it again.
    engine.request_macro_resume();
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);
    engine.on_mouse_event(mouse_event(MacroInputEvent::Type::MouseMove, 4000, 401, 300));
    engine.pump();
    CHECK(engine.macro_player_state() == MacroPlayerState::Playing);
}
