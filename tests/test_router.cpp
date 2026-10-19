// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_common.h"

#include "core/router.h"

using namespace km;

namespace {

Binding hold(Action action, ModMask mods, KeyId key,
             SwallowMode swallow = SwallowMode::Swallow) {
    Binding binding;
    binding.action = action;
    binding.chord = Chord{mods, key};
    binding.mode = BindMode::Hold;
    binding.swallow = swallow;
    return binding;
}

Binding tap(Action action, ModMask mods, KeyId key) {
    Binding binding = hold(action, mods, key);
    binding.mode = BindMode::Tap;
    return binding;
}

Binding toggle(Action action, ModMask mods, KeyId key) {
    Binding binding = hold(action, mods, key);
    binding.mode = BindMode::Toggle;
    return binding;
}

Router make_router() {
    Router router;
    router.set_bindings({
        hold(Action::MoveUp, ModNone, key::ArrowUp),
        hold(Action::MoveRight, ModCtrl, key::Numpad6),
        tap(Action::ClickLeft, ModCtrl, key::Numpad0),
        toggle(Action::DragLock, ModFn, key::NumpadDecimal),
    }, key::Menu);
    return router;
}

}  // namespace

TEST_CASE("unbound key is ignored and not swallowed") {
    Router router = make_router();
    const RouteResult result = router.on_key_down(key::KeyA, ModNone);
    CHECK(result.action == Action::None);
    CHECK_FALSE(result.swallow);
}

TEST_CASE("hold action activates on down and deactivates on up") {
    Router router = make_router();

    const RouteResult down = router.on_key_down(key::ArrowUp, ModNone);
    CHECK(down.action == Action::MoveUp);
    CHECK(down.active);
    CHECK(down.changed);
    CHECK(down.swallow);

    const RouteResult up = router.on_key_up(key::ArrowUp);
    CHECK(up.action == Action::MoveUp);
    CHECK_FALSE(up.active);
    CHECK(up.changed);
}

TEST_CASE("modifier is part of the chord identity") {
    Router router = make_router();

    CHECK(router.on_key_down(key::Numpad6, ModNone).action == Action::None);
    router.on_key_up(key::Numpad6);
    CHECK(router.on_key_down(key::Numpad6, ModCtrl).action == Action::MoveRight);
}

TEST_CASE("tap fires once and ignores key up") {
    Router router = make_router();

    const RouteResult down = router.on_key_down(key::Numpad0, ModCtrl);
    CHECK(down.action == Action::ClickLeft);
    CHECK(down.changed);
    CHECK_FALSE(down.active);

    const RouteResult up = router.on_key_up(key::Numpad0);
    CHECK(up.action == Action::ClickLeft);
    CHECK_FALSE(up.changed);
}

TEST_CASE("auto-repeat does not retrigger a tap") {
    Router router = make_router();

    const RouteResult first = router.on_key_down(key::Numpad0, ModCtrl);
    CHECK(first.changed);

    const RouteResult repeat = router.on_key_down(key::Numpad0, ModCtrl);
    CHECK_FALSE(repeat.changed);
}

TEST_CASE("toggle flips state on each press") {
    Router router = make_router();

    const RouteResult first = router.on_key_down(key::NumpadDecimal, ModFn);
    CHECK(first.action == Action::DragLock);
    CHECK(first.active);

    router.on_key_up(key::NumpadDecimal);
    const RouteResult second = router.on_key_down(key::NumpadDecimal, ModFn);
    CHECK_FALSE(second.active);
}

TEST_CASE("release_all returns active actions") {
    Router router = make_router();
    router.on_key_down(key::ArrowUp, ModNone);
    router.on_key_down(key::Numpad6, ModCtrl);

    const auto released = router.release_all();
    CHECK(released.size() == 2);
    CHECK(released[0] == Action::MoveUp);
    CHECK(released[1] == Action::MoveRight);
    CHECK_FALSE(router.any_active());
}

TEST_CASE("release_all includes toggled actions") {
    Router router = make_router();
    router.on_key_down(key::NumpadDecimal, ModFn);
    router.on_key_up(key::NumpadDecimal);

    const auto released = router.release_all();
    REQUIRE(released.size() == 1);
    CHECK(released[0] == Action::DragLock);
}

TEST_CASE("key up without a key down is ignored") {
    Router router = make_router();
    const RouteResult result = router.on_key_up(key::ArrowUp);
    CHECK(result.action == Action::None);
    CHECK_FALSE(result.changed);
}

TEST_CASE("modifier release before base key still resolves") {
    Router router = make_router();

    // Ctrl down (unbound), Numpad6 down with Ctrl, then Ctrl up, then Numpad6 up.
    router.on_key_down(key::ControlLeft, ModNone);
    const RouteResult down = router.on_key_down(key::Numpad6, ModCtrl);
    CHECK(down.action == Action::MoveRight);

    const RouteResult up = router.on_key_up(key::Numpad6);
    CHECK(up.action == Action::MoveRight);
    CHECK_FALSE(up.active);
}

TEST_CASE("active_actions reflects state") {
    Router router = make_router();
    router.on_key_down(key::ArrowUp, ModNone);
    router.on_key_down(key::NumpadDecimal, ModFn);
    router.on_key_up(key::NumpadDecimal);

    const auto actions = router.active_actions();
    CHECK(actions.size() == 2);
    CHECK(actions[0] == Action::MoveUp);
    CHECK(actions[1] == Action::DragLock);
}

TEST_CASE("pass through bindings are not swallowed") {
    Router router;
    router.set_bindings({
        hold(Action::MoveUp, ModNone, key::ArrowUp, SwallowMode::PassThrough),
    }, key::Menu);

    const RouteResult result = router.on_key_down(key::ArrowUp, ModNone);
    CHECK(result.action == Action::MoveUp);
    CHECK_FALSE(result.swallow);
}

TEST_CASE("invalid bindings are skipped") {
    Router router;
    Binding invalid;
    invalid.action = Action::MoveUp;  // no chord
    router.set_bindings({invalid}, key::Menu);
    CHECK(router.on_key_down(key::Invalid, ModNone).action == Action::None);
}

TEST_CASE("reset clears everything") {
    Router router = make_router();
    router.on_key_down(key::ArrowUp, ModNone);
    router.reset();
    CHECK_FALSE(router.any_active());
    CHECK(router.on_key_down(key::ArrowUp, ModNone).action == Action::None);
}
