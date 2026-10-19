// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include <set>
#include <string>

#include "core/keycodes.h"

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
