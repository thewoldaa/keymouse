// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <algorithm>
#include <string>

#include "core/config.h"

using namespace km;

TEST_CASE("default config is valid") {
    const AppConfig config = make_default_config();
    CHECK(validate(config).empty());
    CHECK(config.profiles.size() == 1);
    CHECK(config.enabled);
}

TEST_CASE("every preset produces a valid config") {
    for (Preset preset : {Preset::Desktop, Preset::Numpad, Preset::Game}) {
        const AppConfig config = make_preset(preset);
        CHECK_MESSAGE(validate(config).empty(), "preset failed validation");
        CHECK_FALSE(config.active().bindings.empty());
    }
}

TEST_CASE("presets have distinct bindings") {
    const AppConfig desktop = make_preset(Preset::Desktop);
    const AppConfig game = make_preset(Preset::Game);

    CHECK(desktop.active().bindings[0].chord.key == key::ArrowUp);
    CHECK(game.active().bindings[0].chord.mods == (ModCtrl | ModAlt));
}

TEST_CASE("json roundtrip preserves the config") {
    AppConfig original = make_preset(Preset::Game);
    original.locale = "ru";
    original.enabled = false;
    original.fn_layer_key = key::CapsLock;
    original.motion.speed_px_s = 1234.5;
    original.active_profile = 0;

    std::string error;
    const auto parsed = from_json(to_json(original), error);
    REQUIRE_MESSAGE(parsed.has_value(), error);

    CHECK(parsed->locale == "ru");
    CHECK(parsed->enabled == false);
    CHECK(parsed->fn_layer_key == key::CapsLock);
    CHECK(parsed->motion.speed_px_s == doctest::Approx(1234.5));
    CHECK(parsed->profiles.size() == original.profiles.size());
    CHECK(parsed->active().bindings.size() == original.active().bindings.size());
}

TEST_CASE("json roundtrip preserves every binding field") {
    AppConfig original = make_default_config();
    original.active().bindings.clear();

    Binding binding;
    binding.action = Action::DragLock;
    binding.chord = Chord{ModCtrl | ModFn, key::NumpadDecimal};
    binding.mode = BindMode::Toggle;
    binding.swallow = SwallowMode::PassThrough;
    original.active().bindings.push_back(binding);

    std::string error;
    const auto parsed = from_json(to_json(original), error);
    REQUIRE_MESSAGE(parsed.has_value(), error);

    const auto& restored = parsed->active().bindings.at(0);
    CHECK(restored.action == Action::DragLock);
    CHECK(restored.chord.mods == (ModCtrl | ModFn));
    CHECK(restored.chord.key == key::NumpadDecimal);
    CHECK(restored.mode == BindMode::Toggle);
    CHECK(restored.swallow == SwallowMode::PassThrough);
}

TEST_CASE("malformed json is rejected with an error") {
    std::string error;
    CHECK_FALSE(from_json("{ not json", error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(from_json("[]", error).has_value());
}

TEST_CASE("newer schema is rejected") {
    std::string error;
    const auto parsed = from_json(R"({"schema": 999})", error);
    CHECK_FALSE(parsed.has_value());
    CHECK(error.find("newer") != std::string::npos);
}

TEST_CASE("unknown keys are ignored") {
    std::string error;
    const auto parsed = from_json(R"({
        "schema": 1,
        "locale": "ja",
        "some_future_key": {"nested": true}
    })", error);
    REQUIRE_MESSAGE(parsed.has_value(), error);
    CHECK(parsed->locale == "ja");
}

TEST_CASE("sanitize clamps out of range values") {
    AppConfig config = make_default_config();
    config.motion.speed_px_s = 999999.0;
    config.motion.precision_mult = -1.0;
    config.motion.turbo_mult = 500.0;
    config.scroll_lines_per_second = 1000.0;
    config.locale = "klingon";
    config.active_profile = 42;

    sanitize(config);

    CHECK(config.motion.speed_px_s <= MotionConfig::kMaxSpeed);
    CHECK(config.motion.precision_mult >= 0.01);
    CHECK(config.motion.turbo_mult <= 20.0);
    CHECK(config.scroll_lines_per_second <= 60.0);
    CHECK(config.locale == "en");
    CHECK(config.active_profile == 0);
}

TEST_CASE("sanitize replaces an invalid fn layer key") {
    AppConfig config = make_default_config();
    config.fn_layer_key = key::ControlLeft;
    sanitize(config);
    CHECK(config.fn_layer_key == key::Menu);
}

TEST_CASE("sanitize keeps a valid fn layer key") {
    AppConfig config = make_default_config();
    config.fn_layer_key = key::CapsLock;
    sanitize(config);
    CHECK(config.fn_layer_key == key::CapsLock);
}

TEST_CASE("sanitize drops invalid bindings") {
    AppConfig config = make_default_config();
    Binding broken;
    broken.action = Action::MoveUp;  // chord is invalid
    config.active().bindings.push_back(broken);

    sanitize(config);

    for (const auto& binding : config.active().bindings) {
        CHECK(binding.chord.valid());
    }
}

TEST_CASE("validate detects duplicate chords") {
    AppConfig config = make_default_config();
    const Binding duplicate = config.active().bindings[0];
    config.active().bindings.push_back(duplicate);

    const auto problems = validate(config);
    CHECK_FALSE(problems.empty());
}

TEST_CASE("validate detects a binding on the panic key") {
    AppConfig config = make_default_config();
    config.panic_key = Chord{ModNone, key::F9};
    config.active().bindings.push_back(
        Binding{Action::ClickLeft, Chord{ModNone, key::F9}, BindMode::Tap, SwallowMode::Swallow});

    const auto problems = validate(config);
    CHECK(std::any_of(problems.begin(), problems.end(),
        [](const std::string& p) { return p.find("panic") != std::string::npos; }));
}

TEST_CASE("validate detects a profile without movement") {
    AppConfig config = make_default_config();
    config.active().bindings.clear();
    config.active().bindings.push_back(
        Binding{Action::ClickLeft, Chord{ModNone, key::F1}, BindMode::Tap, SwallowMode::Swallow});

    const auto problems = validate(config);
    CHECK(std::any_of(problems.begin(), problems.end(),
        [](const std::string& p) { return p.find("movement") != std::string::npos; }));
}

TEST_CASE("chord_in_use finds bindings across profiles") {
    AppConfig config = make_default_config();
    CHECK(config.chord_in_use(Chord{ModNone, key::ArrowUp}));
    CHECK_FALSE(config.chord_in_use(Chord{ModNone, key::F24}));
}

TEST_CASE("mouse mode and auto-click survive a json roundtrip") {
    AppConfig original = make_default_config();
    original.mouse_mode.start_enabled = true;
    original.mouse_mode.hold_to_activate = true;
    original.mouse_mode.beep = false;
    original.mouse_mode.toggle_key = Chord{ModCtrl | ModShift, key::KeyM};
    original.mouse_mode.bindings.push_back(
        Binding{Action::ClickRight, Chord{ModNone, key::KeyO}, BindMode::Tap,
                SwallowMode::Swallow});

    original.auto_click.enabled = true;
    original.auto_click.toggle_key = Chord{ModNone, key::KeyT};
    AutoClickTask task;
    task.id = "raid";
    task.name = "Raid clicker";
    task.action = AutoClickAction::DoubleClick;
    task.interval_seconds = 12.5;
    task.start_delay_seconds = 3.0;
    task.repeat_count = 40;
    task.run_missed_once = true;
    task.only_if_app = "game.exe";
    original.auto_click.tasks = {task};

    std::string error;
    const auto parsed = from_json(to_json(original), error);
    REQUIRE_MESSAGE(parsed.has_value(), error);

    CHECK(parsed->mouse_mode.start_enabled);
    CHECK(parsed->mouse_mode.hold_to_activate);
    CHECK_FALSE(parsed->mouse_mode.beep);
    CHECK(parsed->mouse_mode.toggle_key.mods == (ModCtrl | ModShift));
    CHECK(parsed->mouse_mode.bindings.size() == original.mouse_mode.bindings.size());
    CHECK(parsed->mouse_mode.bindings.back().action == Action::ClickRight);

    CHECK(parsed->auto_click.enabled);
    CHECK(parsed->auto_click.toggle_key.key == key::KeyT);
    REQUIRE(parsed->auto_click.tasks.size() == 1);
    const AutoClickTask& restored = parsed->auto_click.tasks[0];
    CHECK(restored.id == "raid");
    CHECK(restored.name == "Raid clicker");
    CHECK(restored.action == AutoClickAction::DoubleClick);
    CHECK(restored.interval_seconds == doctest::Approx(12.5));
    CHECK(restored.start_delay_seconds == doctest::Approx(3.0));
    CHECK(restored.repeat_count == 40);
    CHECK(restored.run_missed_once);
    CHECK(restored.only_if_app == "game.exe");
}

TEST_CASE("schema 1 configs migrate without changing the speed feel") {
    std::string error;
    const auto parsed = from_json(R"({
        "schema": 1,
        "locale": "en",
        "motion": {
            "speed_px_s": 800.0,
            "accel_ms": 120.0
        }
    })", error);
    REQUIRE_MESSAGE(parsed.has_value(), error);

    CHECK(parsed->schema == kConfigSchemaVersion);
    CHECK(parsed->motion.accel_max == doctest::Approx(1.0));
    CHECK(parsed->motion.accel_curve == doctest::Approx(1.0));
    // The mode did not exist in schema 1, so it arrives switched off but with
    // a usable default key table.
    CHECK_FALSE(parsed->mouse_mode.start_enabled);
    CHECK_FALSE(parsed->mouse_mode.bindings.empty());
    CHECK_FALSE(parsed->auto_click.enabled);
}

TEST_CASE("sanitize repairs mouse mode and auto-click values") {
    AppConfig config = make_default_config();
    Binding broken;
    broken.action = Action::MoveUp;  // invalid chord
    config.mouse_mode.bindings.push_back(broken);
    config.mouse_mode.toggle_key = Chord{};
    config.auto_click.toggle_key = Chord{};

    AutoClickTask task;
    task.interval_seconds = 0.1;
    task.start_delay_seconds = -30.0;
    task.repeat_count = -5;
    config.auto_click.tasks = {task};

    sanitize(config);

    for (const auto& binding : config.mouse_mode.bindings) {
        CHECK(binding.valid());
    }
    CHECK(config.mouse_mode.toggle_key == Chord{ModCtrl | ModAlt, key::KeyM});
    CHECK(config.auto_click.toggle_key == Chord{ModCtrl | ModAlt, key::KeyT});
    REQUIRE(config.auto_click.tasks.size() == 1);
    CHECK(config.auto_click.tasks[0].id == "task-1");
    CHECK(config.auto_click.tasks[0].name == "Task 1");
    CHECK(config.auto_click.tasks[0].interval_seconds ==
          doctest::Approx(kMinAutoClickIntervalSeconds));
    CHECK(config.auto_click.tasks[0].start_delay_seconds == doctest::Approx(0.0));
    CHECK(config.auto_click.tasks[0].repeat_count == 0);
}

TEST_CASE("validate catches control keys used inside mouse mode") {
    AppConfig config = make_default_config();
    config.mouse_mode.bindings.push_back(
        Binding{Action::ClickLeft, config.mouse_mode.toggle_key, BindMode::Tap,
                SwallowMode::Swallow});

    const auto problems = validate(config);
    CHECK(std::any_of(problems.begin(), problems.end(), [](const std::string& p) {
        return p.find("toggle key") != std::string::npos;
    }));
}

TEST_CASE("validate catches duplicate auto-click task ids") {
    AppConfig config = make_default_config();
    AutoClickTask task;
    task.id = "same";
    task.name = "One";
    task.interval_seconds = 10.0;
    config.auto_click.tasks = {task, task};

    const auto problems = validate(config);
    CHECK(std::any_of(problems.begin(), problems.end(), [](const std::string& p) {
        return p.find("id 'same'") != std::string::npos;
    }));
}
