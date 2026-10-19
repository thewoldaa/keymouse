// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include "core/engine.h"

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

}  // namespace

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
    Engine engine(backend);

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
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

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
