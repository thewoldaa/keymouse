// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include "core/chord.h"

using namespace km;

TEST_CASE("format orders modifiers consistently") {
    Chord chord;
    chord.mods = ModShift | ModCtrl | ModAlt;
    chord.key = key::Numpad0;

    CHECK(format_chord(chord) == "Ctrl + Alt + Shift + Numpad0");
}

TEST_CASE("format handles a bare key") {
    Chord chord{ModNone, key::ArrowUp};
    CHECK(format_chord(chord) == "ArrowUp");
}

TEST_CASE("invalid chords format as empty") {
    CHECK(format_chord(Chord{}).empty());
    CHECK(format_chord(Chord{ModNone, key::ControlLeft}).empty());
}

TEST_CASE("parse accepts the canonical form") {
    const auto chord = parse_chord("Ctrl + Alt + Numpad0");
    REQUIRE(chord.has_value());
    CHECK(chord->mods == (ModCtrl | ModAlt));
    CHECK(chord->key == key::Numpad0);
}

TEST_CASE("parse accepts all five modifiers") {
    CHECK(parse_chord("Ctrl+Alt+Shift+Win+Fn+KeyA").has_value());
}

TEST_CASE("parse ignores whitespace and case") {
    const auto chord = parse_chord("  ctrl+ALT +numpad0 ");
    REQUIRE(chord.has_value());
    CHECK(chord->mods == (ModCtrl | ModAlt));
    CHECK(chord->key == key::Numpad0);
}

TEST_CASE("parse handles every modifier") {
    const auto chord = parse_chord("Ctrl+Alt+Shift+Win+Fn+F1");
    REQUIRE(chord.has_value());
    CHECK(chord->mods == kAllModifiers);
    CHECK(chord->key == key::F1);
}

TEST_CASE("parse rejects malformed input") {
    CHECK_FALSE(parse_chord("").has_value());
    CHECK_FALSE(parse_chord("Ctrl+").has_value());
    CHECK_FALSE(parse_chord("+Ctrl").has_value());
    CHECK_FALSE(parse_chord("Ctrl+Alt").has_value());        // no base key
    CHECK_FALSE(parse_chord("Ctrl+Ctrl+A").has_value());     // duplicate modifier
    CHECK_FALSE(parse_chord("Ctrl+A+B").has_value());        // two base keys
    CHECK_FALSE(parse_chord("Ctrl+NotAKey").has_value());
    CHECK_FALSE(parse_chord("Ctrl+ShiftLeft").has_value());  // modifier as base
}

TEST_CASE("parse rejects impossible modifier counts") {
    CHECK_FALSE(parse_chord("Ctrl+Ctrl+A").has_value());
    CHECK(parse_chord("Ctrl+Alt+Shift+Win+Fn+KeyA").has_value());
}

TEST_CASE("roundtrip for many chords") {
    const ModMask mods_list[] = {
        ModNone, ModCtrl, ModAlt, ModShift, ModWin, ModFn,
        static_cast<ModMask>(ModCtrl | ModAlt),
        static_cast<ModMask>(ModCtrl | ModShift | ModFn),
    };
    const KeyId keys[] = {key::KeyA, key::Numpad0, key::F12, key::ArrowLeft, key::Menu};

    for (ModMask mods : mods_list) {
        for (KeyId id : keys) {
            const Chord original{mods, id};
            const auto parsed = parse_chord(format_chord(original));
            REQUIRE(parsed.has_value());
            CHECK(*parsed == original);
        }
    }
}

TEST_CASE("modifier bits map correctly") {
    CHECK(modifier_bit(key::ControlLeft) == ModCtrl);
    CHECK(modifier_bit(key::ControlRight) == ModCtrl);
    CHECK(modifier_bit(key::AltRight) == ModAlt);
    CHECK(modifier_bit(key::ShiftLeft) == ModShift);
    CHECK(modifier_bit(key::MetaRight) == ModWin);
    CHECK(modifier_bit(key::KeyA) == ModNone);
}

TEST_CASE("fn layer key resolves to the fn modifier") {
    CHECK(modifier_bit_for(key::Menu, key::Menu) == ModFn);
    CHECK(modifier_bit_for(key::Menu, key::CapsLock) == ModNone);
    CHECK(modifier_bit_for(key::ControlLeft, key::Menu) == ModCtrl);
}
