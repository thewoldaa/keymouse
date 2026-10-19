# KeyMouse

Control the mouse entirely from the keyboard.

KeyMouse is a small Windows utility that maps keys to cursor movement, clicks,
scrolling, and dragging. Every binding is user-configurable, including chords
with Ctrl, Alt, Shift, Win, and a user-chosen "Fn layer" key.

It is built for two groups:

- **Gamers** who want to move the cursor without leaving the keyboard.
- **People who cannot comfortably use a mouse**, for whom keyboard-only cursor
  control is an accessibility requirement rather than a convenience.

[![CI](https://github.com/thewoldaa/keymouse/actions/workflows/ci.yml/badge.svg)](https://github.com/thewoldaa/keymouse/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/thewoldaa/keymouse?display_name=tag)](https://github.com/thewoldaa/keymouse/releases/latest)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey.svg)](#requirements)

**Download:** [installer or portable zip](https://github.com/thewoldaa/keymouse/releases/latest).

## Status

Version 0.3.0. Windows only. The core is platform independent so Linux and
macOS backends can be added without rewriting the logic; see
[docs/PORTING.md](docs/PORTING.md).

## Features

- **Fully configurable bindings.** Any chord of up to five modifiers plus one
  key, recorded by pressing it.
- **Hold, toggle, and tap modes** per binding. Drag lock is a toggle, movement
  is hold, clicks are tap.
- **Per-application profiles** that switch automatically when a matching
  executable takes focus.
- **Three presets** — Desktop, Numpad, and Game — selectable on first run.
- **Precision and turbo modifiers**, an adjustable acceleration ramp and curve,
  absolute or relative movement, and diagonal normalisation.
- **Mouse mode** turns plain keys into a complete mouse — separate movement,
  left/right/middle click, scroll, drag, precision, and turbo keys — without
  holding Ctrl or an Fn key.
- **Scheduled auto-click** with per-task interval, first delay, repeat limit,
  missed-click handling, and an optional foreground-application filter.
- **Macro recorder and player** — record mouse movement, clicks, and scrolling,
  replay it with loops, a delay between loops, and 0.25× to 4× speed, edit every
  step in a table, and keep a library of `.kmacro` files with global hotkeys.
- **Profile import and export** as readable JSON, with automatic conflict-free
  renaming when an imported profile already exists.
- **Panic key** that disables every mapping instantly.
- **Start with Windows** and a tray icon, both toggleable at any time.
- **Four languages** — English, Russian, Japanese, Indonesian, detected from
  Windows on first run and changeable at any time.
- **Tiny.** One statically linked executable, no runtime dependencies, no network
  access. Ships as a four-language installer or as a portable zip.

## Requirements

- Windows 10 or later, 64-bit.
- A keyboard.

No administrator rights are needed. Input cannot reach elevated windows such as
Task Manager unless KeyMouse itself is run elevated.

## Install

Each release is published in two forms:

- **`KeyMouse-0.3.0-setup.exe`** — per-user install, no administrator prompt and
  no `Program Files`. The wizard speaks English, Russian, Japanese, and
  Indonesian, and copies the documentation next to the application. Uninstalling
  asks whether to keep your settings.
- **`KeyMouse-0.3.0-win64.zip`** — portable. Unpack it anywhere and run
  `KeyMouse.exe`; nothing is written outside `%LOCALAPPDATA%\KeyMouse`.

Both are unsigned, so Windows SmartScreen may warn the first time.

## Building

Prerequisites: Visual Studio 2022 (Desktop C++ workload) and CMake 3.24+.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The binary lands in `build/Release/KeyMouse.exe`. Third-party dependencies
(Dear ImGui, nlohmann/json, doctest) are downloaded and checksum-verified at
configure time; there are no submodules to initialise. Building the installer
and the zip, and the build options, are covered in
[docs/BUILDING.md](docs/BUILDING.md).

## Usage

Run `KeyMouse.exe`.

A first start opens the dashboard, picks up the language Windows is set to, and
adds itself to your login items. Afterwards it starts in the tray with the
dashboard closed: double-click the tray icon, or run the executable again, to
open it. Closing the window keeps it running — use **Quit** in the tray menu.

Command line:

| Option | Effect |
|---|---|
| `--minimized` | Start hidden in the tray |
| `--safe-mode` | Load defaults with mappings off, to escape a bad binding |
| `--no-autostart` | Never create or change the startup entry |
| `--locale CODE` | Start in `en`, `ru`, `ja`, or `id` (first run only) |
| `--config PATH` | Use an alternative configuration file |
| `-h`, `--help`, `-v`, `--version` | Print and exit |

The installer uses the last two of note: it passes `--locale` so the dashboard
opens in the language you installed in, and `--no-autostart` when you clear the
"start with Windows" task, so declining it sticks.

Default bindings (Desktop preset):

| Action | Keys |
|---|---|
| Move | Arrow keys |
| Left click | Ctrl + Numpad 0 |
| Right click | Ctrl + Numpad . |
| Middle click | Ctrl + Numpad 5 |
| Scroll | Ctrl + Numpad + / - |
| Precision | Ctrl + Shift + Numpad Enter |
| Drag lock | Fn layer + Numpad 0 |
| Panic (disable all) | Pause |

Turbo (3×) is left unbound in the Desktop preset; assign it in **Bindings** if
you want it.

## The Fn key problem

Real `Fn` keys are handled by the keyboard controller and never reach the
operating system. No software can detect them on almost every laptop.

KeyMouse therefore provides an **Fn layer**: a physical key of your choice that
acts as an Fn modifier inside the application. Bind `Fn layer + X` and it works
exactly like `Fn + X` would, because the app controls both halves of the chord.

Set it in **General → Fn layer key**. The default is the Menu (Apps) key.

## Mouse mode

Mouse mode is a second, plain-key binding table for the times when you want to
drive the cursor without holding a modifier. It is off by default after an
upgrade and is independent of the normal chord table: while it is on, only the
plain-key table acts, and while it is off the normal bindings are untouched.

| Action | Default key |
|---|---|
| Move up / down / left / right | `I` / `K` / `J` / `L` |
| Left / right / middle click | `U` / `O` / `P` |
| Scroll up / down | `Y` / `H` |
| Precision / turbo | `F` / `G` |
| Drag lock | `Space` |

`Ctrl+Alt+M` toggles the mode. The Mouse mode tab can change the toggle chord,
make it hold-to-activate instead, enable a short beep, start the mode with the
application, and edit every plain-key binding. The dashboard header and the tray
menu show the current state. Locking the workstation turns the mode off so plain
keys are never left armed while you are away.

## Profiles and sharing

The Profiles tab exports the active profile to a JSON file and imports profiles
exported from another machine. Imported profiles are validated, appended, and
renamed if their name is already taken, so an import never overwrites your
existing setup. The Bindings and Mouse mode tabs flag duplicate chords and
reserved hotkeys before they can be added.

## Auto-click

Auto-click runs mouse actions on a schedule, for example a left click every
twelve minutes. It has a master switch (`Ctrl+Alt+T`, or the Auto-click tab) and
any number of tasks. Each task chooses:

- an action: left, right, or middle click, or a left double-click;
- an interval, from one second to seven days;
- a delay before the first click, in seconds, minutes, or hours (zero means
  "after one interval");
- an unlimited repeat count or a limit after which the task disables itself;
- an optional application name, so the task fires only while that executable is
  in the foreground.

The countdown and total clicks are shown in the tab and the Status tab. A
deadline missed by more than two seconds — because the machine slept or KeyMouse
was disabled — is skipped rather than fired late; tick "run missed clicks" on a
task when a late click is preferable to a skipped one. Tasks do not start until
the master switch is on.

## Macros

The Macros tab records a sequence of mouse actions and plays it back.

**Recording.** Press **Record** (`Ctrl+Alt+R`) and the application installs a
mouse hook for the duration; every movement, click, and scroll is captured until
the same chord is pressed again. The movement filter drops points closer
together than the chosen tolerance — three pixels by default — which keeps a
recording at a few hundred steps instead of tens of thousands. Keyboard capture
is a checkbox and off by default; when it is on the keys are recorded *and* still
reach the foreground application, because capture runs alongside normal routing.

**Playback.** **Play** (`Ctrl+Alt+P`) starts the selected macro, and the same
chord pauses and resumes. **Stop** (`Ctrl+Alt+S`) and the panic key (Pause) end
it. Moving or clicking a real mouse pauses playback so you can take over. Each
macro has a loop count (0 = forever), a delay between loops, a speed multiplier
from 0.25× to 4×, and an optional global hotkey, recorded in the library list.

Playing a macro does not disable anything else: mouse mode, auto-click, and the
rest of KeyMouse keep working. Playback is injected with a tag that the recorder
and the hook ignore, so a looping macro is never recorded back into itself.
Mouse-mode movement, on the other hand, is ordinary input and *is* recorded if
the mode is on while recording.

**Editing.** The step table shows the delay, type, and details of every row.
Delays, coordinates, buttons, wheel deltas, and keys are edited in place; rows
can be inserted, duplicated, moved up or down, and deleted; **From cursor** fills
in the current pointer position for a movement step; halving and doubling
delays rescale the whole sequence; and the play button starts from the selected
row.

Macros are readable JSON files with a `.kmacro` extension in
`%LOCALAPPDATA%\KeyMouse\macros`; the library names and hotkeys live in
`config.json`. Import and export copy files in and out of that folder.

Macros replay absolute screen coordinates. They do not follow a window that
moved, and a different resolution or DPI scale invalidates them: re-record after
rearranging the desktop.

## Configuration

Settings live in `%LOCALAPPDATA%\KeyMouse\config.json`. The file is
human-readable and hand-editable; KeyMouse validates and repairs it on load.

```json
{
  "schema": 3,
  "locale": "en",
  "enabled": true,
  "panic_key": { "mods": [], "key": "Pause" },
  "fn_layer_key": "Menu",
  "motion": {
    "speed_px_s": 600.0,
    "accel_ms": 600.0,
    "accel_max": 3.0,
    "accel_curve": 1.5,
    "precision_mult": 0.25,
    "turbo_mult": 3.0,
    "diagonal_normalize": true,
    "method": "relative"
  },
  "mouse_mode": {
    "start_enabled": false,
    "hold_to_activate": false,
    "beep": true,
    "toggle_key": { "mods": ["ctrl", "alt"], "key": "KeyM" }
  },
  "auto_click": {
    "enabled": false,
    "toggle_key": { "mods": ["ctrl", "alt"], "key": "KeyT" },
    "tasks": []
  },
  "macros": {
    "record_keyboard": false,
    "move_tolerance_px": 3.0,
    "record_key": { "mods": ["ctrl", "alt"], "key": "KeyR" },
    "play_key": { "mods": ["ctrl", "alt"], "key": "KeyP" },
    "stop_key": { "mods": ["ctrl", "alt"], "key": "KeyS" },
    "entries": [
      { "id": "farming-run", "name": "Farming run",
        "hotkey": { "mods": ["alt"], "key": "KeyF" } }
    ]
  },
  "profiles": [
    {
      "name": "Default",
      "match_exe": "",
      "bindings": [
        { "action": "move_up", "chord": { "mods": [], "key": "ArrowUp" },
          "mode": "hold", "swallow": "swallow" }
      ]
    }
  ]
}
```

The sample is abridged: the dashboard also writes the full mouse-mode binding
table, auto-click tasks, and the other settings shown in the tabs. Schema-1 files
from 0.1.0 are migrated on load, with the previous fixed-speed feel preserved
(`accel_max` and `accel_curve` set to 1.0); schema-2 files from 0.2.0 gain an
empty macro library. The macro steps themselves live next to the configuration
in `%LOCALAPPDATA%\KeyMouse\macros`, one `.kmacro` file per macro.

A malformed file is renamed to `config.json.broken` and defaults are used, so
the app always starts.

## Known limitations

| Limitation | Reason |
|---|---|
| The hardware `Fn` key cannot be bound | Handled by the keyboard controller, never reaches Windows |
| Modifier matching is exact | While `Ctrl+Shift` is held, the plain `ArrowUp` binding does not fire. Subset matching would hijack `Ctrl+ArrowUp` in the applications you actually use, so the combinations have to be named |
| Cursor speed follows the Windows pointer settings | Pointer acceleration applies to injected relative movement too, so the px/s number is a starting point: tune it by feel |
| Absolute movement is ignored by some games | It uses `SetCursorPos`, which raw-input games may not read; relative movement is the default for that reason |
| Auto-click has a one-second minimum interval | It is a scheduler for periodic actions, not a rapid-fire clicker |
| Auto-click only runs while KeyMouse is enabled | Its master switch and the KeyMouse enable switch are independent; both must be on |
| Mouse mode is off after upgrading to 0.2.0 | It is an additional layer, so existing chord users are not surprised; turn it on with `Ctrl+Alt+M` or enable "start with the application" |
| Macros replay absolute coordinates | Recorded against one screen layout; window moves, resolution changes, and DPI changes invalidate a recording. Re-record after rearranging |
| A real mouse movement or click pauses playback | So the user can take control without waiting for the macro to end; resume from the dashboard or with the play chord |
| Keyboard capture does not swallow keys | Capture runs alongside normal routing, so the foreground application still receives the keys that are being recorded |
| Some games ignore injected input | They read Raw Input rather than the standard message queue |
| Competitive anti-cheat may block or flag injection | Out of scope; KeyMouse does not attempt to evade detection |
| Elevated windows do not receive input | Windows UIPI. Run KeyMouse elevated if you need this |
| UAC prompts and the lock screen are unreachable | Windows secure desktop, by design |
| Media keys (`Play`, `Volume`, …) cannot be recorded | Dear ImGui 1.93 has no key identifiers for them; the keyboard's own controls still work |
| Windows SmartScreen warns on first run | The binaries are unsigned. Build from source if you prefer |

## Roadmap

0.3.0 adds the macro recorder, player, editor, and library on top of the 0.2.0
feature set. The hook, movement, clicks, scroll, drag lock, precision and turbo,
mouse mode, the auto-click scheduler, profiles and exchange, the panic key, four
languages, the installer, the portable zip, and the test suite remain.

Next:

- **0.4** — macro refinements that were left out of 0.3: undo and redo, row
  multi-select, a pick-on-screen coordinate tool, and a tray submenu for the
  macro library
- **Later** — signed binaries, an optional on-screen position indicator, a
  first-run tour, and per-app profile switching by window title
- **Platforms** — Linux (X11, evdev/uinput) and macOS (CGEventTap) backends
  behind the existing core; see [docs/PORTING.md](docs/PORTING.md)

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

Third-party components are MIT licensed and GPL-compatible: Dear ImGui,
nlohmann/json, doctest.
