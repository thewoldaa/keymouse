// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

#include "core/keycodes.h"

namespace km {

enum class MouseButton : uint8_t { Left, Right, Middle, X1, X2 };
enum class Axis : uint8_t { Horizontal, Vertical };

// A key event as delivered by the platform backend.
struct InputEvent {
    enum class Type : uint8_t {
        KeyDown,
        KeyUp,
        // The foreground window changed or was destroyed. Held actions must be
        // released so a key held over an alt-tab does not keep moving.
        FocusLost,
        // The session was locked or a UAC prompt appeared. The hook is dead
        // until it is re-armed.
        SessionLost,
    };

    Type type = Type::KeyDown;
    KeyId key = key::Invalid;
    // Modifier mask active at the time of the event, computed by the backend
    // from the OS key state so the router does not have to track modifiers.
    uint8_t mods = 0;
};

// Called on the hook thread. Must return quickly: no allocation, no locks, no
// I/O. Returning true consumes the event.
using EventSink = std::function<bool(const InputEvent&)>;

// Platform input backend. Windows implements this with a low-level keyboard
// hook and SendInput; Linux and macOS will provide their own.
class InputBackend {
public:
    virtual ~InputBackend() = default;

    // Installs the hook and starts delivering events to `sink`. Returns false
    // if the hook could not be installed.
    virtual bool start(EventSink sink) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;

    // Injection. Safe to call from any thread.
    virtual void move(int32_t dx, int32_t dy) = 0;
    virtual void button(MouseButton button, bool down) = 0;
    virtual void scroll(Axis axis, int32_t delta) = 0;

    // Absolute positioning, used by MotionMethod::Absolute. The default
    // implementation reports "unsupported" so backends can ignore it; the
    // engine falls back to relative movement.
    virtual void move_to(int32_t x, int32_t y) { (void)x; (void)y; }
    virtual bool cursor_position(int32_t& x, int32_t& y) {
        (void)x;
        (void)y;
        return false;
    }
    // Bounding box of the virtual desktop, in the same coordinates as
    // cursor_position().
    virtual bool screen_bounds(int32_t& left, int32_t& top, int32_t& right, int32_t& bottom) {
        (void)left;
        (void)top;
        (void)right;
        (void)bottom;
        return false;
    }

    // Tells the backend which physical key the user selected as the "Fn layer"
    // modifier, so it can include ModFn in the modifier mask it reports. Called
    // from the UI thread whenever the configuration changes. Backends that
    // cannot do this may ignore it.
    virtual void set_fn_layer_key(KeyId key) { (void)key; }

    // Diagnostics
    virtual uint64_t dropped_events() const = 0;
    virtual const char* name() const = 0;
};

// Creates the backend for the platform this binary was built for.
InputBackend* make_native_backend();

}  // namespace km
