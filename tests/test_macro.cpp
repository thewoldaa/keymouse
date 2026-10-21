// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/macro.h"
#include "core/macro_store.h"
#include "core/player.h"
#include "core/recorder.h"

using namespace km;

namespace {

class PlaybackBackend final : public InputBackend {
public:
    bool start(EventSink) override { return true; }
    void stop() override {}
    bool running() const override { return true; }

    void move(int32_t dx, int32_t dy) override {
        ++relative_moves;
        last_dx = dx;
        last_dy = dy;
    }
    void move_to(int32_t x, int32_t y) override {
        ++moves_to;
        last_x = x;
        last_y = y;
        saw_tag = saw_tag || tagged;
    }
    void button(MouseButton button, bool down) override {
        buttons.emplace_back(button, down);
        saw_tag = saw_tag || tagged;
    }
    void scroll(Axis, int32_t) override { ++scrolls; }
    void key(KeyId target, bool down) override {
        keys.emplace_back(target, down);
        saw_tag = saw_tag || tagged;
    }
    void set_injection_tag(bool on) override { tagged = on; }

    bool cursor_position(int32_t&, int32_t&) override { return false; }
    bool screen_bounds(int32_t& left, int32_t& top, int32_t& right,
                       int32_t& bottom) override {
        left = 0;
        top = 0;
        right = 1920;
        bottom = 1080;
        return true;
    }
    uint64_t dropped_events() const override { return 0; }
    const char* name() const override { return "playback-fake"; }

    int relative_moves = 0;
    int moves_to = 0;
    int scrolls = 0;
    int32_t last_dx = 0;
    int32_t last_dy = 0;
    int32_t last_x = 0;
    int32_t last_y = 0;
    std::vector<std::pair<MouseButton, bool>> buttons;
    std::vector<std::pair<KeyId, bool>> keys;
    bool tagged = false;
    bool saw_tag = false;
};

MacroStep step(MacroStepType type, int64_t dt_ms, int32_t data = 0,
               KeyId key = key::Invalid) {
    MacroStep out;
    out.type = type;
    out.dt_ms = dt_ms;
    out.data = data;
    out.key = key;
    return out;
}

MacroStep move_step(int64_t dt_ms, int32_t x, int32_t y) {
    MacroStep out;
    out.type = MacroStepType::Move;
    out.dt_ms = dt_ms;
    out.x = x;
    out.y = y;
    return out;
}

std::filesystem::path temp_macro_dir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("keymouse-macro-test-" + std::to_string(stamp));
}

}  // namespace

TEST_CASE("step type names round trip") {
    const MacroStepType all[] = {
        MacroStepType::Move,       MacroStepType::ButtonDown,
        MacroStepType::ButtonUp,   MacroStepType::Scroll,
        MacroStepType::ScrollH,    MacroStepType::KeyDown,
        MacroStepType::KeyUp,      MacroStepType::Wait,
        MacroStepType::MouseDelta,
    };
    for (const MacroStepType type : all) {
        CHECK(macro_step_type_from_name(macro_step_type_name(type)) == type);
    }
    CHECK(macro_step_type_from_name("nonsense") == MacroStepType::Wait);

    const MouseButton buttons[] = {MouseButton::Left, MouseButton::Right,
                                   MouseButton::Middle, MouseButton::X1,
                                   MouseButton::X2};
    for (const MouseButton button : buttons) {
        const auto parsed = macro_button_from_name(macro_button_name(button));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == button);
    }
    CHECK_FALSE(macro_button_from_name("thumb").has_value());
}

TEST_CASE("macro json round trips every step kind") {
    Macro original;
    original.id = "demo";
    original.name = "Demo \"macro\"";
    original.recorded_width = 1920;
    original.recorded_height = 1080;
    original.steps = {
        move_step(120, -5, 42),
        step(MacroStepType::ButtonDown, 8, static_cast<int32_t>(MouseButton::Right)),
        step(MacroStepType::ButtonUp, 9, static_cast<int32_t>(MouseButton::X1)),
        step(MacroStepType::Scroll, 10, -120),
        step(MacroStepType::ScrollH, 11, 120),
        step(MacroStepType::KeyDown, 12, 0, key::KeyW),
        step(MacroStepType::KeyUp, 13, 0, key::Space),
        step(MacroStepType::Wait, 14),
    };
    {
        MacroStep delta;
        delta.type = MacroStepType::MouseDelta;
        delta.dt_ms = 15;
        delta.x = -17;
        delta.y = 23;
        original.steps.push_back(delta);
    }

    std::string error;
    const auto parsed = macro_from_json(macro_to_json(original), error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    CHECK(parsed->id == "demo");
    CHECK(parsed->name == original.name);
    CHECK(parsed->recorded_width == 1920);
    CHECK(parsed->recorded_height == 1080);
    REQUIRE(parsed->steps.size() == original.steps.size());
    for (size_t i = 0; i < parsed->steps.size(); ++i) {
        CHECK(parsed->steps[i].type == original.steps[i].type);
        CHECK(parsed->steps[i].dt_ms == original.steps[i].dt_ms);
        CHECK(parsed->steps[i].x == original.steps[i].x);
        CHECK(parsed->steps[i].y == original.steps[i].y);
        CHECK(parsed->steps[i].data == original.steps[i].data);
        CHECK(parsed->steps[i].key == original.steps[i].key);
    }
    CHECK(macro_duration_ms(*parsed) == macro_duration_ms(original));
    CHECK(macro_has_action(*parsed));
}

TEST_CASE("macro json rejects malformed documents and clamps values") {
    std::string error;
    CHECK_FALSE(macro_from_json("not json", error).has_value());
    CHECK_FALSE(error.empty());
    error.clear();

    CHECK_FALSE(macro_from_json(R"({"steps": 5})", error).has_value());
    error.clear();
    CHECK_FALSE(macro_from_json(R"({"steps": [{"type": "teleport"}]})", error).has_value());
    error.clear();
    CHECK_FALSE(macro_from_json(R"({"steps": [{"type": "button_down"}]})", error).has_value());
    error.clear();
    CHECK_FALSE(
        macro_from_json(R"({"steps": [{"type": "key_down", "key": "Nope"}]})", error)
            .has_value());
    error.clear();

    const auto clamped = macro_from_json(
        R"({"steps": [{"type": "move", "dt": -5, "x": 999999, "y": 0}]})", error);
    REQUIRE_MESSAGE(clamped.has_value(), error);
    REQUIRE(clamped->steps.size() == 1);
    CHECK(clamped->steps[0].dt_ms == 0);
    CHECK(clamped->steps[0].x == kMaxMacroCoordinate);
}

TEST_CASE("a macro from a newer format is rejected with a clear message") {
    std::string error;
    const auto newer = macro_from_json(
        R"({"format": "keymouse-macro", "version": 99, "steps": []})", error);
    CHECK_FALSE(newer.has_value());
    CHECK(error.find("newer KeyMouse") != std::string::npos);

    // A format-1 file has no version field in some exports and must keep
    // loading, including one that predates camera deltas entirely.
    error.clear();
    const auto old = macro_from_json(
        R"({"format": "keymouse-macro", "version": 1,
            "steps": [{"type": "move", "dt": 10, "x": 3, "y": 4}]})",
        error);
    REQUIRE_MESSAGE(old.has_value(), error);
    REQUIRE(old->steps.size() == 1);
    CHECK(old->steps[0].type == MacroStepType::Move);
}

TEST_CASE("file stems are safe and bounded") {
    CHECK(macro_file_stem("Farming Run 2") == "Farming-Run-2");
    CHECK(macro_file_stem("") == "macro");
    CHECK(macro_file_stem("!!!") == "macro");
    CHECK(macro_file_stem("../../etc/passwd") == "etcpasswd");
    CHECK(macro_file_stem(std::string(200, 'a')).size() == 64);
}

TEST_CASE("the recorder compresses movement and times steps from its first event") {
    MacroRecorder recorder;
    recorder.start(false, 2.0, 1920, 1080);
    REQUIRE(recorder.active());

    MacroInputEvent event;
    event.type = MacroInputEvent::Type::MouseMove;
    event.time_us = 100000;  // The first accepted event is time zero.
    event.x = 10;
    event.y = 10;
    recorder.add(event);

    event.time_us = 150000;
    event.x = 11;  // One pixel away: inside the tolerance circle.
    event.y = 10;
    recorder.add(event);

    event.time_us = 200000;
    event.x = 40;
    event.y = 10;
    recorder.add(event);

    MacroInputEvent click;
    click.type = MacroInputEvent::Type::ButtonDown;
    click.time_us = 250000;
    click.data = static_cast<int32_t>(MouseButton::Left);
    recorder.add(click);

    // Playback output is never recorded back in.
    MacroInputEvent injected;
    injected.type = MacroInputEvent::Type::ButtonUp;
    injected.time_us = 260000;
    injected.injected = true;
    recorder.add(injected);

    // Keyboard capture is off, so the key event is dropped without counting as
    // ignored playback output.
    MacroInputEvent typed;
    typed.type = MacroInputEvent::Type::KeyDown;
    typed.time_us = 270000;
    typed.key = key::KeyW;
    recorder.add(typed);

    CHECK(recorder.ignored_events() == 1);
    CHECK(recorder.step_count() == 3);

    Macro macro = recorder.finish("demo", "Demo");
    CHECK_FALSE(recorder.active());
    CHECK(macro.id == "demo");
    CHECK(macro.name == "Demo");
    CHECK(macro.recorded_width == 1920);
    CHECK(macro.recorded_height == 1080);
    // The left button was pressed but never released when the recording
    // stopped, so finish() closes it: a playback that ended mid-drag must not
    // leave the desktop holding the button.
    REQUIRE(macro.steps.size() == 4);
    CHECK(macro.steps[0].type == MacroStepType::Move);
    CHECK(macro.steps[0].dt_ms == 0);
    CHECK(macro.steps[1].type == MacroStepType::Move);
    CHECK(macro.steps[1].dt_ms == 100);
    CHECK(macro.steps[1].x == 40);
    CHECK(macro.steps[2].type == MacroStepType::ButtonDown);
    CHECK(macro.steps[2].dt_ms == 50);
    CHECK(macro.steps[2].data == static_cast<int32_t>(MouseButton::Left));
    CHECK(macro.steps[3].type == MacroStepType::ButtonUp);
    CHECK(macro.steps[3].data == static_cast<int32_t>(MouseButton::Left));
}

TEST_CASE("keyboard capture records keys when it is enabled") {
    MacroRecorder recorder;
    recorder.start(true, 3.0, 0, 0);

    MacroInputEvent event;
    event.type = MacroInputEvent::Type::KeyDown;
    event.time_us = 5000;
    event.key = key::KeyW;
    recorder.add(event);

    event.type = MacroInputEvent::Type::KeyUp;
    event.time_us = 9000;
    recorder.add(event);

    event.type = MacroInputEvent::Type::KeyDown;
    event.time_us = 10000;
    event.key = key::Invalid;
    recorder.add(event);

    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 2);
    CHECK(macro.steps[0].type == MacroStepType::KeyDown);
    CHECK(macro.steps[0].key == key::KeyW);
    CHECK(macro.steps[1].type == MacroStepType::KeyUp);
    CHECK(macro.steps[1].dt_ms == 4);
}

TEST_CASE("an inactive recorder ignores everything") {
    MacroRecorder recorder;
    MacroInputEvent event;
    event.type = MacroInputEvent::Type::MouseMove;
    event.time_us = 1000;
    recorder.add(event);

    CHECK(recorder.step_count() == 0);
    CHECK_FALSE(recorder.active());
}

TEST_CASE("a button already down when recording starts is still a camera drag") {
    MacroRecorder recorder;
    recorder.start(false, 2.0, 1920, 1080);

    // The press happened before Record was pressed, so this stream has no
    // ButtonDown. Only the physical mask on the move says a drag is running.
    MacroInputEvent move;
    move.type = MacroInputEvent::Type::MouseMove;
    move.time_us = 100000;
    move.x = 500;
    move.y = 500;
    move.buttons_down = 1u << static_cast<unsigned>(MouseButton::Left);
    recorder.add(move);

    move.time_us = 130000;
    move.x = 540;
    move.y = 480;
    recorder.add(move);

    move.time_us = 160000;
    move.x = 600;
    move.y = 470;
    recorder.add(move);

    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 2);
    CHECK(macro.steps[0].type == MacroStepType::MouseDelta);
    CHECK(macro.steps[0].x == 40);
    CHECK(macro.steps[0].y == -20);
    CHECK(macro.steps[1].type == MacroStepType::MouseDelta);
    CHECK(macro.steps[1].x == 60);
    CHECK(macro.steps[1].y == -10);
    CHECK(recorder.camera_steps() == 2);
}

TEST_CASE("a drag press inside the recording is a delta and its release is recorded") {
    MacroRecorder recorder;
    recorder.start(false, 3.0, 1920, 1080);

    MacroInputEvent move;
    move.type = MacroInputEvent::Type::MouseMove;
    move.time_us = 1000;
    move.x = 100;
    move.y = 100;
    recorder.add(move);

    MacroInputEvent down;
    down.type = MacroInputEvent::Type::ButtonDown;
    down.time_us = 2000;
    down.data = static_cast<int32_t>(MouseButton::Right);
    recorder.add(down);

    move.time_us = 3000;
    move.x = 120;
    move.y = 130;
    recorder.add(move);

    MacroInputEvent up;
    up.type = MacroInputEvent::Type::ButtonUp;
    up.time_us = 4000;
    up.data = static_cast<int32_t>(MouseButton::Right);
    recorder.add(up);

    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 4);
    CHECK(macro.steps[0].type == MacroStepType::Move);
    CHECK(macro.steps[1].type == MacroStepType::ButtonDown);
    CHECK(macro.steps[2].type == MacroStepType::MouseDelta);
    CHECK(macro.steps[2].x == 20);
    CHECK(macro.steps[2].y == 30);
    CHECK(macro.steps[3].type == MacroStepType::ButtonUp);
}

TEST_CASE("the release of a button the recording never saw pressed is dropped") {
    MacroRecorder recorder;
    recorder.start(false, 3.0, 1920, 1080);

    // The user was dragging before the recording started and lets go during
    // it. Replaying that release would release the button under the user.
    MacroInputEvent up;
    up.type = MacroInputEvent::Type::ButtonUp;
    up.time_us = 1000;
    up.data = static_cast<int32_t>(MouseButton::Left);
    recorder.add(up);

    const Macro macro = recorder.finish({}, {});
    CHECK(macro.steps.empty());
}

TEST_CASE("a lost button release is repaired by the next physical snapshot") {
    MacroRecorder recorder;
    recorder.start(false, 2.0, 1920, 1080);

    // LMB down, then the button-up is lost (focus change, hook gap).
    MacroInputEvent down;
    down.type = MacroInputEvent::Type::ButtonDown;
    down.time_us = 1000;
    down.data = static_cast<int32_t>(MouseButton::Left);
    recorder.add(down);

    MacroInputEvent move;
    move.type = MacroInputEvent::Type::MouseMove;
    move.time_us = 2000;
    move.x = 10;
    move.y = 10;
    move.buttons_down = 1u << static_cast<unsigned>(MouseButton::Left);
    move.buttons_known = true;
    recorder.add(move);

    // The OS says nothing is held any more, so this is ordinary movement again.
    move.time_us = 3000;
    move.x = 200;
    move.y = 200;
    move.buttons_down = 0;
    move.buttons_known = true;
    recorder.add(move);

    // The release was lost to the stream, but the OS snapshot shows the button
    // up, so the state self-heals. No release is appended for a button the
    // recording no longer owns: emitting one would be a stray click.
    // The lost release heals the stream state: the second move is ordinary
    // absolute movement, not another camera delta. The recording still owns the
    // press it saw, so finish() appends the release it never observed — a
    // replay must not end with the button held.
    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 3);
    CHECK(macro.steps[0].type == MacroStepType::ButtonDown);
    CHECK(macro.steps[1].type == MacroStepType::Move);
    CHECK(macro.steps[1].x == 200);
    CHECK(macro.steps[2].type == MacroStepType::ButtonUp);
    CHECK(macro.steps[2].data == static_cast<int32_t>(MouseButton::Left));
}

TEST_CASE("turning camera drag off records a desktop drag as absolute positions") {
    MacroRecorder recorder;
    recorder.start(false, 2.0, 1920, 1080, false);

    MacroInputEvent move;
    move.type = MacroInputEvent::Type::MouseMove;
    move.time_us = 1000;
    move.x = 10;
    move.y = 10;
    move.buttons_down = 1u << static_cast<unsigned>(MouseButton::Left);
    recorder.add(move);

    move.time_us = 2000;
    move.x = 90;
    move.y = 40;
    recorder.add(move);

    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 2);
    CHECK(macro.steps[1].type == MacroStepType::Move);
    CHECK(macro.steps[1].x == 90);
    CHECK(recorder.camera_steps() == 0);
}

TEST_CASE("key capture drops auto-repeat and unpaired releases") {
    MacroRecorder recorder;
    recorder.start(true, 3.0, 0, 0);

    MacroInputEvent key;
    key.type = MacroInputEvent::Type::KeyDown;
    key.time_us = 1000;
    key.key = key::KeyW;
    recorder.add(key);

    // OS auto-repeat: the key is already down in the recording.
    for (int i = 0; i < 5; ++i) {
        key.time_us += 10000;
        recorder.add(key);
    }

    MacroInputEvent release;
    release.type = MacroInputEvent::Type::KeyUp;
    release.time_us = 70000;
    release.key = key::KeyW;
    recorder.add(release);

    // Ctrl went down before the recording started, so its release is not part
    // of it either.
    release.time_us = 80000;
    release.key = key::ControlLeft;
    recorder.add(release);

    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 2);
    CHECK(macro.steps[0].type == MacroStepType::KeyDown);
    CHECK(macro.steps[0].key == key::KeyW);
    CHECK(macro.steps[1].type == MacroStepType::KeyUp);
    CHECK(macro.steps[1].dt_ms == 69);
    CHECK(recorder.key_steps() == 2);
}

TEST_CASE("finishing a recording releases everything it left held") {
    MacroRecorder recorder;
    recorder.start(true, 3.0, 0, 0);

    MacroInputEvent down;
    down.type = MacroInputEvent::Type::ButtonDown;
    down.time_us = 1000;
    down.data = static_cast<int32_t>(MouseButton::Left);
    recorder.add(down);

    MacroInputEvent key;
    key.type = MacroInputEvent::Type::KeyDown;
    key.time_us = 2000;
    key.key = key::KeyW;
    recorder.add(key);

    // The user stops the recording while still holding both.
    const Macro macro = recorder.finish({}, {});
    REQUIRE(macro.steps.size() == 4);
    CHECK(macro.steps[2].type == MacroStepType::ButtonUp);
    CHECK(macro.steps[2].data == static_cast<int32_t>(MouseButton::Left));
    CHECK(macro.steps[2].dt_ms == 0);
    CHECK(macro.steps[3].type == MacroStepType::KeyUp);
    CHECK(macro.steps[3].key == key::KeyW);
    CHECK(macro.steps[3].dt_ms == 0);
}

TEST_CASE("the player honours loops, speed, and the injection tag") {
    PlaybackBackend backend;
    MacroPlayer player(backend);

    auto macro = std::make_shared<Macro>();
    macro->steps = {
        move_step(0, 100, 200),
        step(MacroStepType::ButtonDown, 100, static_cast<int32_t>(MouseButton::Left)),
        step(MacroStepType::ButtonUp, 100, static_cast<int32_t>(MouseButton::Left)),
    };

    MacroPlaybackOptions options;
    options.loops = 2;
    player.start(macro, options, 0);
    REQUIRE(player.state() == MacroPlayerState::Playing);

    player.tick(0);
    CHECK(backend.moves_to == 1);
    CHECK(backend.last_x == 100);
    CHECK(backend.last_y == 200);

    player.tick(100000);
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].second);

    player.tick(200000);
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);
    CHECK(player.loops_completed() == 1);
    // The second loop is already running: its first step has no delay.
    CHECK(player.state() == MacroPlayerState::Playing);
    CHECK(backend.moves_to == 2);

    player.tick(300000);
    REQUIRE(backend.buttons.size() == 3);
    CHECK(backend.buttons[2].second);
    player.tick(400000);
    REQUIRE(backend.buttons.size() == 4);
    CHECK_FALSE(backend.buttons[3].second);
    CHECK(player.loops_completed() == 2);
    CHECK(player.state() == MacroPlayerState::Idle);
    CHECK(backend.saw_tag);
    CHECK_FALSE(backend.tagged);
}

TEST_CASE("a camera drag step plays as relative movement") {
    PlaybackBackend backend;
    MacroPlayer player(backend);

    auto macro = std::make_shared<Macro>();
    MacroStep delta;
    delta.type = MacroStepType::MouseDelta;
    delta.dt_ms = 10;
    delta.x = 35;
    delta.y = -12;
    macro->steps = {delta};

    player.start(macro, MacroPlaybackOptions{}, 0);
    player.tick(10000);
    CHECK(backend.relative_moves == 1);
    CHECK(backend.last_dx == 35);
    CHECK(backend.last_dy == -12);
    // A delta must never be turned into an absolute placement: in a game the
    // cursor position is not what turns the view.
    CHECK(backend.moves_to == 0);
    CHECK(player.state() == MacroPlayerState::Idle);
}

TEST_CASE("playback speed scales the step delays") {
    PlaybackBackend backend;
    MacroPlayer player(backend);

    auto macro = std::make_shared<Macro>();
    macro->steps = {move_step(100, 5, 5)};

    MacroPlaybackOptions options;
    options.speed = 2.0;
    player.start(macro, options, 0);

    player.tick(49999);
    CHECK(backend.moves_to == 0);
    player.tick(50000);
    CHECK(backend.moves_to == 1);
    CHECK(player.state() == MacroPlayerState::Idle);
}

TEST_CASE("pause keeps the remaining delay and resume re-presses held input") {
    PlaybackBackend backend;
    MacroPlayer player(backend);

    auto macro = std::make_shared<Macro>();
    macro->steps = {
        step(MacroStepType::ButtonDown, 0, static_cast<int32_t>(MouseButton::Left)),
        step(MacroStepType::ButtonUp, 1000, static_cast<int32_t>(MouseButton::Left)),
    };

    player.start(macro, MacroPlaybackOptions{}, 0);
    player.tick(0);
    REQUIRE(backend.buttons.size() == 1);
    CHECK(backend.buttons[0].second);

    player.pause(0);
    CHECK(player.state() == MacroPlayerState::Paused);
    REQUIRE(backend.buttons.size() == 2);
    CHECK_FALSE(backend.buttons[1].second);

    player.tick(500000);
    CHECK(backend.buttons.size() == 2);  // Nothing runs while paused.

    player.resume(500000);
    CHECK(player.state() == MacroPlayerState::Playing);
    REQUIRE(backend.buttons.size() == 3);
    CHECK(backend.buttons[2].second);

    player.tick(1499999);
    CHECK(backend.buttons.size() == 3);  // The remaining delay is kept.
    player.tick(1500000);
    REQUIRE(backend.buttons.size() == 4);
    CHECK_FALSE(backend.buttons[3].second);
    CHECK(player.state() == MacroPlayerState::Idle);
}

TEST_CASE("a tick far behind schedule stops playback instead of replaying a burst") {
    PlaybackBackend backend;
    MacroPlayer player(backend);

    auto macro = std::make_shared<Macro>();
    macro->steps = {move_step(0, 1, 1), move_step(1000, 2, 2)};

    player.start(macro, MacroPlaybackOptions{}, 0);
    player.tick(0);
    CHECK(backend.moves_to == 1);

    // Two seconds late on a one-second step: the process was suspended, and
    // replaying the backlog would fire a burst of clicks.
    player.tick(2000000);
    CHECK(player.state() == MacroPlayerState::Idle);
    CHECK(backend.moves_to == 1);
    CHECK_FALSE(backend.tagged);
}

TEST_CASE("a macro whose only step is a wait still plays") {
    PlaybackBackend backend;
    MacroPlayer player(backend);

    auto macro = std::make_shared<Macro>();
    macro->steps = {step(MacroStepType::Wait, 50)};

    player.start(macro, MacroPlaybackOptions{}, 0);
    player.tick(50000);
    CHECK(player.state() == MacroPlayerState::Idle);
    CHECK(player.steps_executed() == 1);
    CHECK(backend.buttons.empty());
}

TEST_CASE("a recorded camera drag survives the real file path") {
    const std::filesystem::path dir = temp_macro_dir();
    std::filesystem::remove_all(dir);

    // Record a short camera turn the way the engine does: the hook reports the
    // physical button mask on each move, the recording stops mid-drag.
    MacroRecorder recorder;
    recorder.start(true, 2.0, 2560, 1440, true);

    MacroInputEvent move;
    move.type = MacroInputEvent::Type::MouseMove;
    move.time_us = 5000;
    move.x = 1200;
    move.y = 700;
    move.buttons_down = 1u << static_cast<unsigned>(MouseButton::Right);
    move.buttons_known = true;
    recorder.add(move);

    move.time_us = 21000;
    move.x = 1250;
    move.y = 690;
    recorder.add(move);

    MacroInputEvent key;
    key.type = MacroInputEvent::Type::KeyDown;
    key.time_us = 22000;
    key.key = key::KeyA;
    recorder.add(key);

    Macro macro = recorder.finish("Look-left", "Look left");
    REQUIRE(macro.steps.size() == 3);
    CHECK(macro.steps[0].type == MacroStepType::MouseDelta);
    CHECK(macro.steps[1].type == MacroStepType::KeyDown);
    CHECK(macro.steps[2].type == MacroStepType::KeyUp);

    std::string error;
    REQUIRE_MESSAGE(macro_store::save(dir, macro, error), error);
    const auto loaded = macro_store::load(dir, "Look-left", error);
    REQUIRE_MESSAGE(loaded.has_value(), error);
    CHECK(loaded->recorded_width == 2560);
    CHECK(loaded->recorded_height == 1440);
    REQUIRE(loaded->steps.size() == 3);
    // The first move only established the reference point, so the delta is the
    // first appended step and therefore defines time zero.
    CHECK(loaded->steps[0].type == MacroStepType::MouseDelta);
    CHECK(loaded->steps[0].x == 50);
    CHECK(loaded->steps[0].y == -10);
    CHECK(loaded->steps[0].dt_ms == 0);
    CHECK(loaded->steps[1].type == MacroStepType::KeyDown);
    CHECK(loaded->steps[1].dt_ms == 1);
    CHECK(loaded->steps[1].key == key::KeyA);

    std::filesystem::remove_all(dir);
}

TEST_CASE("the macro store round trips files and picks unique ids") {
    const std::filesystem::path dir = temp_macro_dir();
    std::filesystem::remove_all(dir);

    Macro macro;
    macro.id = "Farm-run";
    macro.name = "Farm run";
    macro.steps = {move_step(0, 10, 20), step(MacroStepType::Wait, 30)};

    std::string error;
    REQUIRE_MESSAGE(macro_store::save(dir, macro, error), error);
    CHECK(std::filesystem::exists(macro_store::macro_file(dir, "Farm-run")));

    const auto loaded = macro_store::load(dir, "Farm-run", error);
    REQUIRE_MESSAGE(loaded.has_value(), error);
    CHECK(loaded->id == "Farm-run");
    CHECK(loaded->name == "Farm run");
    REQUIRE(loaded->steps.size() == 2);
    CHECK(loaded->steps[0].type == MacroStepType::Move);
    CHECK(loaded->steps[1].dt_ms == 30);

    // Ids come from the display name and are deduplicated against the files on
    // disk and the ids the caller already knows about.
    CHECK(macro_store::unique_id(dir, "Farm run", {}) == "Farm-run-2");
    CHECK(macro_store::unique_id(dir, "Farm run", {"Farm-run-2"}) == "Farm-run-3");
    CHECK(macro_store::unique_id(dir, "Other", {}) == "Other");

    CHECK(macro_store::remove_file(dir, "Farm-run"));
    CHECK_FALSE(std::filesystem::exists(macro_store::macro_file(dir, "Farm-run")));
    CHECK_FALSE(macro_store::load(dir, "Farm-run", error).has_value());

    std::filesystem::remove_all(dir);
}
