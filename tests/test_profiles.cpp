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

TEST_CASE("profiles without match_exe are never auto-selected") {
    AppConfig config = make_default_config();
    config.profiles[0].match_exe = "";

    const auto match = match_profile(config, "anything.exe");
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

TEST_CASE("no match returns false") {
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
