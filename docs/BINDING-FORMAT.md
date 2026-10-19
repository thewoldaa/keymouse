# Binding format

A binding is a **chord** plus a **mode** plus a **swallow policy**.

## Chord

A chord is zero or more modifiers plus exactly one base key.

```json
{ "mods": ["ctrl", "alt"], "key": "Numpad0" }
```

### Modifiers

| Config value | Meaning |
|---|---|
| `ctrl` | Either Ctrl key |
| `alt` | Either Alt key |
| `shift` | Either Shift key |
| `win` | Either Windows key |
| `fn` | The user-selected Fn layer key |

Left and right variants collapse to one bit. A user who binds `Ctrl + Numpad0`
does not want the binding to fail because they used the right-hand Ctrl. Per-side
bindings are not a feature worth the extra chord space.

The `fn` modifier is synthetic: it refers to whatever physical key is configured
as `fn_layer_key`, because real Fn keys never reach the OS.

### Matching is exact

A chord fires only while the set of held modifiers is **exactly** the set the
chord names. With Ctrl and Shift both down, a `Ctrl + ArrowUp` binding does not
fire, and a plain `ArrowUp` binding does not either — they are different chords,
and neither is the one being pressed.

Subset matching was considered and rejected: it would make `Ctrl + ArrowUp`
impossible to bind, because the plain binding would swallow the combination
first. If a control should work with and without a modifier, bind it twice.

### Base key

The base key is a **canonical physical key name**, never a virtual-key code.

```
KeyA .. KeyZ          letters, by physical position
Digit1 .. Digit0      number row
F1 .. F24             function keys
Numpad0 .. Numpad9    numeric keypad
ArrowUp/Down/Left/Right
Enter Escape Backspace Tab Space CapsLock
Minus Equal BracketLeft BracketRight Backslash Semicolon Quote
Backquote Comma Period Slash
PrintScreen ScrollLock Pause Insert Home PageUp Delete End PageDown
NumLock NumpadDivide NumpadMultiply NumpadSubtract NumpadAdd NumpadEnter NumpadDecimal
ControlLeft ShiftLeft AltLeft MetaLeft ControlRight ShiftRight AltRight MetaRight
Menu IntlBackslash IntlRo IntlYen KanaMode Convert NonConvert Lang1 Lang2
MediaPlayPause MediaStop MediaTrackNext MediaTrackPrev VolumeMute VolumeDown VolumeUp
```

**Why physical names and not VK codes:** a Russian or Japanese user who switches
keyboard layout keeps the same physical keys. Virtual-key codes change underneath
them; canonical names do not. Bindings survive layout changes.

Names are matched case-insensitively when parsing, and written canonically.

The media and volume keys can be bound, but only by editing the file: the
dashboard's recorder cannot capture them, because Dear ImGui 1.93 has no key
identifiers for them.

A base key may not itself be a modifier. `Ctrl + Ctrl` is not a binding.

### Limits

At most five modifiers (all of them). At most one base key.

## Mode

| Mode | Behaviour | Typical use |
|---|---|---|
| `hold` | Active while the chord is held | Movement, scroll, precision |
| `toggle` | First press starts, second stops | Drag lock |
| `tap` | Fires once on press | Clicks, centre cursor |

`tap` and `toggle` ignore key-up. `hold` stops on key-up.

Auto-repeat is suppressed: holding a `tap` binding does not fire it repeatedly.
Windows sends repeated `WM_KEYDOWN` for held keys, and without suppression a
click binding would machine-gun.

## Swallow policy

| Value | Behaviour |
|---|---|
| `swallow` | The key is consumed and does not reach the application. Default. |
| `pass` | The key reaches the application as well. |

Use `pass` when the chord is also something you want the game or app to see.
Use `swallow` (the default) for dedicated control keys.

## Full example

```json
{
  "action": "drag_lock",
  "chord": { "mods": ["fn"], "key": "Numpad0" },
  "mode": "toggle",
  "swallow": "swallow"
}
```

## Actions

| Action | Continuous | Description |
|---|---|---|
| `move_up` `move_down` `move_left` `move_right` | yes | Cursor movement |
| `click_left` `click_right` `click_middle` | no | Mouse buttons |
| `click_x1` `click_x2` | no | Back and forward buttons |
| `double_click` | no | Two left clicks |
| `drag_lock` | toggle | Holds the left button down |
| `scroll_up` `scroll_down` `scroll_left` `scroll_right` | yes | Wheel |
| `precision` | yes | Slows movement by `precision_mult` |
| `turbo` | yes | Speeds movement by `turbo_mult` |
| `cursor_center` | no | Moves the cursor to the monitor centre |
| `toggle_enabled` | no | Turns KeyMouse on or off |
| `open_dashboard` | no | Shows the dashboard |
| `speed_up` `speed_down` | no | Adjusts speed by 25% |

## Mouse mode table

`mouse_mode.bindings` uses exactly the same binding objects, but is a separate
table that is only consulted while mouse mode is on. Its defaults are unmodified
keys, because the point of the mode is that no Ctrl, Alt, or Fn layer is needed:

| Action | Key |
|---|---|
| `move_up` / `move_down` / `move_left` / `move_right` | `KeyI` / `KeyK` / `KeyJ` / `KeyL` |
| `click_left` / `click_right` / `click_middle` | `KeyU` / `KeyO` / `KeyP` |
| `scroll_up` / `scroll_down` | `KeyY` / `KeyH` |
| `precision` / `turbo` | `KeyF` / `KeyG` |
| `drag_lock` | `Space` |

The table can technically contain chords, but the dashboard and documentation
assume plain keys; a modifier defeats the purpose of the mode. While mouse mode
is on, the active profile's chord table is dormant. The toggle chord itself
(`mouse_mode.toggle_key`, default `Ctrl+Alt+M`) is reserved and cannot be used
inside either table.

## Validation rules

KeyMouse reports these problems in the Diagnostics tab and repairs what it can:

- A chord used twice in the same profile or the mouse-mode table.
- A binding on the panic key or on either toggle hotkey.
- A profile with no movement bindings.
- Duplicate profile names.
- A binding with an invalid chord (dropped on load).

## OS shortcuts

Some chords are claimed by Windows before any hook sees them, or produce
unwanted system behaviour. Avoid binding:

| Chord | Effect |
|---|---|
| `Ctrl + Alt + Del` | Secure Attention Sequence, unreachable |
| `Win + L` | Locks the workstation |
| `Alt + F4` | Closes the foreground window |
| `Ctrl + Shift + Esc` | Opens Task Manager |
| `Win + Tab`, `Alt + Tab` | Task switching |

KeyMouse does not block these. Binding them produces confusing behaviour rather
than a working control.
