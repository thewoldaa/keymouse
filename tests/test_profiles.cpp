// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include "core/profiles.h"

using namespace km;

TEST_CASE("exe_name_from_path handles both separators") {
    CHECK(exe_name_from_path("C:\\Games\\Foo.EXE") == "foo.exe");
    CHECK(exe_name_from_path("/usr/bin/foo") == "foo");
    CHECK(exe_name_from_path("foo.exe") == "foo.exe");
    CHECK(exe_name_from_path("") == "");
}

TEST_CASE("matching is case insensitive") {
    AppConfig config = make_default_config();
    config.profiles[0].match_exe = "Game.EXE";

    const auto match = match_profile(config, "game.exe");
    CHECK(match.matched);
    CHECK(match.index == 0);
}

TEST_CASE("matching works with full paths") {
    AppConfig config = make_default_config();
    config.profiles[0].match_exe = "game.exe";

    const auto match = match_profile(config, "D:\\Steam\\game.exe");
    CHECK(match.matched);
}

TEST_CASE("profiles without match_exe act as the fallback") {
    AppConfig config = make_default_config();
    config.profiles[0].match_exe = "";

    // No profile claims the executable, so the only profile -- which has no
    // application match -- is the fallback.
    const auto match = match_profile(config, "anything.exe");
    CHECK(match.matched);
    CHECK(match.index == 0);
}

TEST_CASE("leaving a matched application returns to the fallback profile") {
    AppConfig config = make_default_config();
    config.profiles.clear();

    Profile fallback;
    fallback.name = "Default";
    fallback.match_exe = "";
    Profile game;
    game.name = "Game";
    game.match_exe = "game.exe";

    config.profiles.push_back(fallback);
    config.profiles.push_back(game);

    const auto in_game = match_profile(config, "game.exe");
    CHECK(in_game.matched);
    CHECK(in_game.index == 1);

    // Focus moves to an application no profile claims: the fallback takes over
    // instead of leaving the game profile active.
    const auto elsewhere = match_profile(config, "explorer.exe");
    CHECK(elsewhere.matched);
    CHECK(elsewhere.index == 0);
}

TEST_CASE("with no fallback profile an unmatched application keeps the current one") {
    AppConfig config = make_default_config();
    config.profiles.clear();

    Profile game;
    game.name = "Game";
    game.match_exe = "game.exe";
    Profile editor;
    editor.name = "Editor";
    editor.match_exe = "editor.exe";

    config.profiles.push_back(game);
    config.profiles.push_back(editor);

    const auto match = match_profile(config, "explorer.exe");
    CHECK_FALSE(match.matched);
}

TEST_CASE("first matching profile wins") {
    AppConfig config = make_default_config();
    config.profiles.clear();

    Profile first;
    first.name = "First";
    first.match_exe = "shared.exe";
    Profile second;
    second.name = "Second";
    second.match_exe = "shared.exe";

    config.profiles.push_back(first);
    config.profiles.push_back(second);

    const auto match = match_profile(config, "shared.exe");
    CHECK(match.matched);
    CHECK(match.index == 0);
}

TEST_CASE("no match returns false when no fallback exists") {
    AppConfig config = make_default_config();
    config.profiles[0].match_exe = "game.exe";
    CHECK_FALSE(match_profile(config, "other.exe").matched);
    CHECK_FALSE(match_profile(config, "").matched);
}

TEST_CASE("apply_preset keeps general settings") {
    AppConfig config = make_preset(Preset::Desktop);
    config.locale = "ja";
    config.start_with_windows = false;
    config.panic_key = Chord{ModNone, key::F9};
    config.fn_layer_key = key::CapsLock;

    apply_preset(config, Preset::Game);

    CHECK(config.locale == "ja");
    CHECK(config.start_with_windows == false);
    CHECK(config.panic_key.key == key::F9);
    CHECK(config.fn_layer_key == key::CapsLock);
    CHECK(config.active().bindings[0].chord.mods == (ModCtrl | ModAlt));
}

TEST_CASE("apply_preset preserves the profile name and exe match") {
    AppConfig config = make_default_config();
    config.profiles[0].name = "MyProfile";
    config.profiles[0].match_exe = "game.exe";

    apply_preset(config, Preset::Numpad);

    CHECK(config.profiles[0].name == "MyProfile");
    CHECK(config.profiles[0].match_exe == "game.exe");
}

TEST_CASE("profile exchange roundtrips names, matches, and bindings") {
    AppConfig config = make_default_config();
    config.profiles[0].name = "Desktop";
    config.profiles[0].match_exe = "explorer.exe";

    Profile game;
    game.name = "Game";
    game.match_exe = "game.exe";
    game.bindings.push_back(
        Binding{Action::MoveUp, Chord{ModCtrl | ModAlt, key::ArrowUp}, BindMode::Hold,
                SwallowMode::PassThrough});
    config.profiles.push_back(game);

    std::string error;
    const auto imported = profiles_from_json(profiles_to_json(config.profiles), error);
    REQUIRE_MESSAGE(imported.has_value(), error);
    REQUIRE(imported->size() == 2);

    CHECK((*imported)[0].name == "Desktop");
    CHECK((*imported)[0].match_exe == "explorer.exe");
    CHECK((*imported)[1].name == "Game");
    CHECK((*imported)[1].match_exe == "game.exe");
    REQUIRE((*imported)[1].bindings.size() == 1);
    CHECK((*imported)[1].bindings[0].action == Action::MoveUp);
    CHECK((*imported)[1].bindings[0].chord.mods == (ModCtrl | ModAlt));
    CHECK((*imported)[1].bindings[0].swallow == SwallowMode::PassThrough);
}

TEST_CASE("profile import rejects unrelated and malformed files") {
    std::string error;
    CHECK_FALSE(profiles_from_json(R"({"profiles": []})", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(profiles_from_json(R"({"format": "other", "profiles": []})", error)
                    .has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(profiles_from_json("{ not json", error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("profile import rejects an empty profile list") {
    std::string error;
    const auto imported = profiles_from_json(
        R"({"format": "keymouse-profiles", "version": 1, "profiles": []})", error);
    CHECK_FALSE(imported.has_value());
    CHECK_FALSE(error.empty());
}
