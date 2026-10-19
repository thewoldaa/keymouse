# Porting to Linux and macOS

Windows is the first target. The design keeps the decision logic portable so a
port is a new backend, not a rewrite.

## What is already portable

Everything in `src/core/`:

- chord parsing and formatting
- canonical key ids
- binding table
- router state machine
- motion integrator
- config schema, presets, validation
- i18n loader
- SPSC ring
- engine

None of it includes `<windows.h>`. The tests for these run on any platform with
a C++20 compiler.

## What must be written per platform

### The input seam

`src/input/input_backend.h` defines the contract:

```cpp
class InputBackend {
public:
    virtual bool start(EventSink sink) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual void move(int32_t dx, int32_t dy) = 0;
    virtual void button(MouseButton button, bool down) = 0;
    virtual void scroll(Axis axis, int32_t delta) = 0;
    virtual uint64_t dropped_events() const = 0;
    virtual const char* name() const = 0;
};
```

Implement it once per platform and link the right one.

### The platform seam

`src/platform/` provides narrow interfaces for:

- `paths` — config and locale locations
- `startup` — autostart mechanism
- `screen` — cursor position, monitor geometry, elevation
- `session` — lock and unlock notifications
- `dpi` — display scaling

## Linux

| Backend | Status | Requirements |
|---|---|---|
| X11 + XTest | Works fully | `libX11`, `libXtst` |
| evdev + uinput | Works | read access to `/dev/input/event*`, write to `/dev/uinput` |
| Wayland | Largely blocked | see below |

### X11

Capture with `XRecordQueryVersion` / `XRecordCreateContext`, inject with
`XTestFakeRelativeMotionEvent` and `XTestFakeButtonEvent`. This is the
straightforward path and covers Xorg sessions.

### evdev + uinput

Read raw events from `/dev/input/event*` and create a virtual mouse through
`/dev/uinput`. Works on both X11 and — for the injection half — some Wayland
compositors. Requires a udev rule so the user does not need root:

```
# /etc/udev/rules.d/60-keymouse.rules
KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"
```

### Wayland

**This is a real limitation, not a bug to fix.** Wayland deliberately prevents
applications from injecting input into other applications. The supported path is
the `libei` protocol via the RemoteDesktop portal, which requires a user consent
dialog per session and whose support varies by compositor.

Plan: ship X11 and evdev/uinput first. Treat Wayland as best-effort with a clear
message in the UI when the portal is unavailable. Do not promise full support.

### Autostart

`~/.config/autostart/keymouse.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=KeyMouse
Exec=keymouse --minimized
X-GNOME-Autostart-enabled=true
```

## macOS

| Need | API |
|---|---|
| Capture | `CGEventTapCreate` with `kCGHIDEventTap` |
| Injection | `CGEventPost(kCGHIDEventTap, event)` |
| Permission | Accessibility (`AXIsProcessTrustedWithOptions`) |
| Distribution | Code signing and notarization, or Gatekeeper blocks it |

### Capture

```objc
CGEventTapCreate(kCGSessionEventTap,
                 kCGHeadInsertEventTap,
                 kCGEventTapOptionDefault,
                 CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp),
                 callback, userInfo);
```

The tap runs on a dedicated run loop thread. Return `NULL` from the callback to
swallow the event, which maps directly to the existing swallow policy.

### Injection

```objc
CGEventRef move = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved,
                                          CGPointMake(x, y), kCGMouseButtonLeft);
CGEventPost(kCGHIDEventTap, move);
CFRelease(move);
```

### Permissions

macOS requires the user to grant Accessibility permission in System Settings.
The app must check `AXIsProcessTrustedWithOptions` and show clear instructions
when it is missing. There is no way to prompt-and-grant programmatically.

### Signing

An unsigned, un-notarized app is blocked by Gatekeeper on modern macOS.
Distribution requires an Apple Developer ID and `notarytool`. Document this; do
not ship a broken unsigned build.

## Checklist for a new backend

1. Implement `InputBackend` in `src/input/backend_<platform>.cpp`.
2. Implement the `src/platform/` interfaces.
3. Add a `keycodes_<platform>.cpp` mapping native keycodes to canonical ids.
4. Add the platform to `CMakeLists.txt` with the right sources and libraries.
5. Write a test that runs the core test suite on that platform (it should pass
   unchanged — if it does not, a platform dependency leaked into `core/`).
6. Add the platform's autostart and permission handling.
7. Document the platform's hard limitations in the README.

## What not to do

- Do not put `#ifdef _WIN32` inside `core/`. If you need it, the code belongs
  in `input/` or `platform/`.
- Do not fork the engine. The same engine, router, and motion model should drive
  every backend.
- Do not promise Wayland parity. Be honest in the UI about what works.
