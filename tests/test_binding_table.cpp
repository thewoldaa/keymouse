// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include "core/binding_table.h"

using namespace km;

namespace {

Binding make(Action action, ModMask mods, KeyId key,
             SwallowMode swallow = SwallowMode::Swallow,
             BindMode mode = BindMode::Hold) {
    Binding binding;
    binding.action = action;
    binding.chord = Chord{mods, key};
    binding.mode = mode;
    binding.swallow = swallow;
    return binding;
}

}  // namespace

TEST_CASE("lookup finds a bound chord") {
    const auto table = BindingTable::build({make(Action::MoveUp, ModNone, key::ArrowUp)});
    const BindingEntry* entry = table->find(ModNone, key::ArrowUp);
    REQUIRE(entry != nullptr);
    CHECK(entry->action == Action::MoveUp);
    CHECK(entry->mode == BindMode::Hold);
}

TEST_CASE("lookup misses unbound chords") {
    const auto table = BindingTable::build({make(Action::MoveUp, ModNone, key::ArrowUp)});
    CHECK(table->find(ModNone, key::ArrowDown) == nullptr);
    CHECK(table->find(ModCtrl, key::ArrowUp) == nullptr);
}

TEST_CASE("modifiers are part of the key") {
    const auto table = BindingTable::build({
        make(Action::MoveUp, ModNone, key::Numpad8),
        make(Action::MoveDown, ModCtrl, key::Numpad8),
    });

    CHECK(table->find(ModNone, key::Numpad8)->action == Action::MoveUp);
    CHECK(table->find(ModCtrl, key::Numpad8)->action == Action::MoveDown);
}

TEST_CASE("swallow mode is preserved") {
    const auto table = BindingTable::build({
        make(Action::MoveUp, ModNone, key::ArrowUp, SwallowMode::PassThrough),
    });
    const BindingEntry* entry = table->find(ModNone, key::ArrowUp);
    REQUIRE(entry != nullptr);
    CHECK(entry->swallow == SwallowMode::PassThrough);
}

TEST_CASE("invalid bindings are skipped") {
    Binding broken;
    broken.action = Action::MoveUp;
    const auto table = BindingTable::build({broken});
    CHECK(table->empty());
    CHECK(table->count() == 0);
}

TEST_CASE("out of range keys do not crash") {
    const auto table = BindingTable::build({});
    CHECK(table->find(ModNone, 250) == nullptr);
    CHECK(table->find(ModNone, 60000) == nullptr);
}

TEST_CASE("store returns a valid table before publishing") {
    BindingTableStore store;
    REQUIRE(store.current() != nullptr);
    CHECK(store.current()->empty());
}

TEST_CASE("store publishes new tables atomically") {
    BindingTableStore store;
    store.publish({make(Action::MoveUp, ModNone, key::ArrowUp)});
    CHECK(store.current()->find(ModNone, key::ArrowUp) != nullptr);

    store.publish({make(Action::MoveDown, ModNone, key::ArrowDown)});
    CHECK(store.current()->find(ModNone, key::ArrowUp) == nullptr);
    CHECK(store.current()->find(ModNone, key::ArrowDown) != nullptr);
}

TEST_CASE("old tables stay alive after republishing") {
    BindingTableStore store;
    const auto first = store.publish({make(Action::MoveUp, ModNone, key::ArrowUp)});
    store.publish({make(Action::MoveDown, ModNone, key::ArrowDown)});

    // A hook thread holding the old pointer must still be able to read it.
    CHECK(first->find(ModNone, key::ArrowUp) != nullptr);
    CHECK(first->find(ModNone, key::ArrowUp)->action == Action::MoveUp);
}

TEST_CASE("count reflects distinct slots") {
    const auto table = BindingTable::build({
        make(Action::MoveUp, ModNone, key::ArrowUp),
        make(Action::MoveDown, ModNone, key::ArrowDown),
        make(Action::ClickLeft, ModCtrl, key::Numpad0),
    });
    CHECK(table->count() == 3);
}
