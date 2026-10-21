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

TEST_CASE("two keys bound to the same hold action reference count") {
    Router router;
    router.set_bindings({
        hold(Action::MoveUp, ModNone, key::ArrowUp),
        hold(Action::MoveUp, ModNone, key::KeyW),
    }, key::Menu);

    const RouteResult first = router.on_key_down(key::ArrowUp, ModNone);
    CHECK(first.active);
    CHECK(first.changed);

    // The second key starts nothing new: the action is already running.
    const RouteResult second = router.on_key_down(key::KeyW, ModNone);
    CHECK(second.active);
    CHECK_FALSE(second.changed);

    // Releasing one still leaves the other holding the action.
    const RouteResult up = router.on_key_up(key::ArrowUp);
    CHECK(up.active);
    CHECK_FALSE(up.changed);
    CHECK(router.any_active());

    const RouteResult last = router.on_key_up(key::KeyW);
    CHECK_FALSE(last.active);
    CHECK(last.changed);
    CHECK_FALSE(router.any_active());
}

TEST_CASE("two keys bound to the same toggle share one state") {
    Router router;
    router.set_bindings({
        toggle(Action::DragLock, ModNone, key::KeyG),
        toggle(Action::DragLock, ModNone, key::KeyH),
    }, key::Menu);

    CHECK(router.on_key_down(key::KeyG, ModNone).active);
    router.on_key_up(key::KeyG);

    // The second chord turns the shared toggle off, and nothing stale is left
    // behind for the first one.
    const RouteResult off = router.on_key_down(key::KeyH, ModNone);
    CHECK_FALSE(off.active);
    router.on_key_up(key::KeyH);
    CHECK_FALSE(router.any_active());
    CHECK(router.active_actions().empty());
}

TEST_CASE("toggle state does not depend on which key set it") {
    Router router;
    router.set_bindings({
        toggle(Action::DragLock, ModNone, key::KeyG),
        toggle(Action::DragLock, ModNone, key::KeyH),
    }, key::Menu);

    // Press G (on), release G, press H (off), release H. The action must not
    // still be reported as active after the second press, which is what the
    // stale active_ entry used to cause.
    router.on_key_down(key::KeyG, ModNone);
    router.on_key_up(key::KeyG);
    const RouteResult second = router.on_key_down(key::KeyH, ModNone);
    CHECK_FALSE(second.active);
    router.on_key_up(key::KeyH);

    const RouteResult third = router.on_key_down(key::KeyG, ModNone);
    CHECK(third.active);
}

TEST_CASE("a hold and a toggle on the same action both count as active") {
    Router router;
    router.set_bindings({
        hold(Action::MoveUp, ModNone, key::ArrowUp),
        toggle(Action::MoveUp, ModNone, key::KeyG),
    }, key::Menu);

    router.on_key_down(key::KeyG, ModNone);
    router.on_key_up(key::KeyG);
    CHECK(router.is_action_active(Action::MoveUp));

    // The hold key is pressed and released while the toggle keeps the action
    // on; releasing it must not report a state change.
    router.on_key_down(key::ArrowUp, ModNone);
    const RouteResult up = router.on_key_up(key::ArrowUp);
    CHECK(up.active);
    CHECK_FALSE(up.changed);
}

TEST_CASE("route results carry the binding mode") {
    Router router = make_router();

    CHECK(router.on_key_down(key::ArrowUp, ModNone).mode == BindMode::Hold);
    router.on_key_up(key::ArrowUp);
    CHECK(router.on_key_down(key::Numpad0, ModCtrl).mode == BindMode::Tap);
    router.on_key_up(key::Numpad0);
    CHECK(router.on_key_down(key::NumpadDecimal, ModFn).mode == BindMode::Toggle);
}

TEST_CASE("release_all keeps held keys so auto-repeat does not re-fire a tap") {
    Router router = make_router();
    router.on_key_down(key::Numpad0, ModCtrl);  // tap; still physically held

    router.release_all();

    // Windows repeats WM_KEYDOWN while the key is down. The repeat must not
    // look like a fresh press and fire the tap a second time.
    const RouteResult repeat = router.on_key_down(key::Numpad0, ModCtrl);
    CHECK_FALSE(repeat.changed);
    CHECK(router.on_key_up(key::Numpad0).action == Action::ClickLeft);

    // After a real release, the next press fires again.
    CHECK(router.on_key_down(key::Numpad0, ModCtrl).changed);
}

TEST_CASE("forget_held_keys drops the physical state after a session change") {
    Router router = make_router();
    router.on_key_down(key::Numpad0, ModCtrl);
    router.forget_held_keys();
    // The key-up after a dropped state is a no-op, and the next press is new.
    CHECK(router.on_key_up(key::Numpad0).action == Action::None);
    CHECK(router.on_key_down(key::Numpad0, ModCtrl).changed);
}

TEST_CASE("toggling off does not stop an action another key still holds") {
    Router router;
    router.set_bindings({
        hold(Action::MoveUp, ModNone, key::ArrowUp),
        toggle(Action::MoveUp, ModNone, key::KeyG),
    }, key::Menu);

    CHECK(router.on_key_down(key::KeyG, ModNone).active);
    router.on_key_up(key::KeyG);

    // The hold key starts the action; the toggle's off-press must not report
    // the action as stopped while ArrowUp is still down.
    router.on_key_down(key::ArrowUp, ModNone);
    const RouteResult off = router.on_key_down(key::KeyG, ModNone);
    CHECK(off.active);
    CHECK_FALSE(off.changed);
    CHECK(router.is_action_active(Action::MoveUp));

    // Releasing the hold key still leaves the toggle... which is now off, so
    // the action ends.
    const RouteResult up = router.on_key_up(key::ArrowUp);
    CHECK_FALSE(up.active);
    CHECK(up.changed);
}
