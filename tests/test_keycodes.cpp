// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <set>
#include <string>

#include "core/keycodes.h"

#if defined(_WIN32)
#include "input/keycodes_win32.h"
#endif

using namespace km;

TEST_CASE("every key id is unique") {
    std::set<KeyId> seen;
    for (const auto& info : all_keys()) {
        CHECK_MESSAGE(seen.insert(info.id).second, "duplicate id for ", info.name);
    }
}

TEST_CASE("every canonical name is unique") {
    std::set<std::string> seen;
    for (const auto& info : all_keys()) {
        CHECK_MESSAGE(seen.insert(std::string(info.name)).second,
                      "duplicate name: ", info.name);
    }
}

TEST_CASE("names roundtrip through lookup") {
    for (const auto& info : all_keys()) {
        if (info.id == key::Invalid) {
            continue;
        }
        const auto parsed = key_from_name(info.name);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == info.id);
    }
}

TEST_CASE("all_keys is sorted by id") {
    const auto& keys = all_keys();
    for (size_t i = 1; i < keys.size(); ++i) {
        CHECK(keys[i - 1].id < keys[i].id);
    }
}

TEST_CASE("key_info finds known keys") {
    const KeyInfo* info = key_info(key::Numpad0);
    REQUIRE(info != nullptr);
    CHECK(info->name == "Numpad0");
    CHECK_FALSE(info->is_modifier);
}

TEST_CASE("key_info rejects unknown ids") {
    CHECK(key_info(60000) == nullptr);
}

TEST_CASE("modifiers are flagged") {
    CHECK(key_is_modifier(key::ControlLeft));
    CHECK(key_is_modifier(key::ShiftRight));
    CHECK(key_is_modifier(key::FnLayer));
    CHECK_FALSE(key_is_modifier(key::Numpad0));
    CHECK_FALSE(key_is_modifier(key::KeyA));
}

TEST_CASE("contiguous ranges are correct") {
    CHECK(key::KeyA + 25 == key::KeyZ);
    CHECK(key::Key1 + 8 == 38);
    CHECK(key::Numpad1 + 8 == key::Numpad9);
    CHECK(key::F1 + 11 == key::F12);
}

TEST_CASE("unknown name returns nullopt") {
    CHECK_FALSE(key_from_name("NotAKey").has_value());
    CHECK_FALSE(key_from_name("").has_value());
    CHECK_FALSE(key_from_name("numpad0").has_value());  // case sensitive by design
}

TEST_CASE("labels are never empty") {
    for (const auto& info : all_keys()) {
        CHECK_FALSE(info.label.empty());
    }
}

#if defined(_WIN32)
TEST_CASE("PrintScreen maps to its extended scan code") {
    const auto translation = win32::to_scan_code(key::PrintScreen);
    CHECK(translation.key == key::PrintScreen);
    CHECK(translation.extended);
    CHECK(translation.scan == 0x37);
}

TEST_CASE("every F13..F24 key has a scan code") {
    std::set<uint32_t> scans;
    for (KeyId id = key::F13; id <= key::F24; ++id) {
        const auto translation = win32::to_scan_code(id);
        CHECK_MESSAGE(translation.key == id, "missing scan code for ", key_name(id));
        CHECK_FALSE(translation.extended);
        // Distinct keys must not share a scan code; a duplicate would mean one
        // of the mappings is off by one.
        CHECK_MESSAGE(scans.insert(translation.scan).second,
                      "duplicate scan code for ", key_name(id));
    }
}

TEST_CASE("F13 is scan 0x64 and F24 is scan 0x76") {
    CHECK(win32::to_scan_code(key::F13).scan == 0x64);
    CHECK(win32::to_scan_code(key::F24).scan == 0x76);
    // Scan 0x59 is not an F-key; mapping it to one shifted the whole range.
    CHECK(win32::from_scan_code(0x59, false, 0).key != key::F13);
}

TEST_CASE("F13..F24 scan codes round trip through the hook table") {
    for (KeyId id = key::F13; id <= key::F24; ++id) {
        const auto translation = win32::to_scan_code(id);
        REQUIRE(translation.key == id);
        const auto back = win32::from_scan_code(translation.scan,
                                                translation.extended, 0);
        CHECK(back.key == id);
    }
}

TEST_CASE("PrintScreen round trips through the hook table") {
    const auto translation = win32::to_scan_code(key::PrintScreen);
    REQUIRE(translation.key == key::PrintScreen);
    const auto back = win32::from_scan_code(translation.scan,
                                            translation.extended, 0);
    CHECK(back.key == key::PrintScreen);
}

TEST_CASE("media keys have a virtual-key injection fallback") {
    // The extended scan code exists, but a virtual-key path must exist too so a
    // key whose scan code a backend cannot reproduce still has a way out.
    const auto vk = win32::to_virtual_key(key::MediaPlayPause);
    REQUIRE(vk.has_value());
    // VK_MEDIA_PLAY_PAUSE is 0xB3 (winuser.h).
    CHECK(*vk == 0xB3u);
    CHECK(win32::from_virtual_key(*vk) == key::MediaPlayPause);
}

TEST_CASE("media and CJK keys have no scan code, so the fallback is reachable") {
    // 0xE0 0x5E..0x6A are the ACPI power/sleep/wake keys, not media keys. A
    // bogus mapping here would make the backend's virtual-key fallback dead
    // code and send the wrong key.
    for (KeyId id : {key::VolumeMute, key::VolumeDown, key::VolumeUp,
                     key::MediaStop, key::MediaPlayPause, key::MediaTrackPrev,
                     key::MediaTrackNext, key::Lang2}) {
        CHECK_MESSAGE(win32::to_scan_code(id).key == key::Invalid,
                      "unexpected scan code for ", key_name(id));
        CHECK_MESSAGE(win32::to_virtual_key(id).has_value(),
                      "no virtual key for ", key_name(id));
    }
    // key::Lang1 has no virtual key of its own: VK_HANGUL and VK_KANA are the
    // same value, so the Korean key arrives as KanaMode and is sent back that
    // way. It is deliberately absent from the table above.

    // Pause is injected as VK_PAUSE: 0xE0 0x46 is Ctrl+Break, a different key.
    CHECK(win32::to_scan_code(key::Pause).key == key::Invalid);
    REQUIRE(win32::to_virtual_key(key::Pause).has_value());
    CHECK(*win32::to_virtual_key(key::Pause) == 0x13u);  // VK_PAUSE
}

TEST_CASE("virtual-key fallback covers every letter and digit") {
    for (KeyId id = key::KeyA; id <= key::KeyZ; ++id) {
        const auto vk = win32::to_virtual_key(id);
        REQUIRE(vk.has_value());
        CHECK(win32::from_virtual_key(*vk) == id);
    }
}
#endif
