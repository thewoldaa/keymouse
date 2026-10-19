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

Version 0.1.0. Windows only. The core is platform independent so Linux and
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
- **Precision and turbo modifiers**, acceleration, and diagonal normalisation.
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

- **`KeyMouse-0.1.0-setup.exe`** — per-user install, no administrator prompt and
  no `Program Files`. The wizard speaks English, Russian, Japanese, and
  Indonesian, and copies the documentation next to the application. Uninstalling
  asks whether to keep your settings.
- **`KeyMouse-0.1.0-win64.zip`** — portable. Unpack it anywhere and run
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

## Configuration

Settings live in `%LOCALAPPDATA%\KeyMouse\config.json`. The file is
human-readable and hand-editable; KeyMouse validates and repairs it on load.

```json
{
  "schema": 1,
  "locale": "en",
  "enabled": true,
  "panic_key": { "mods": [], "key": "Pause" },
  "fn_layer_key": "Menu",
  "motion": {
    "speed_px_s": 600.0,
    "accel_ms": 120.0,
    "precision_mult": 0.25,
    "turbo_mult": 3.0,
    "diagonal_normalize": true
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

A malformed file is renamed to `config.json.broken` and defaults are used, so
the app always starts.

## Known limitations

| Limitation | Reason |
|---|---|
| The hardware `Fn` key cannot be bound | Handled by the keyboard controller, never reaches Windows |
| Modifier matching is exact | While `Ctrl+Shift` is held, the plain `ArrowUp` binding does not fire. Subset matching would hijack `Ctrl+ArrowUp` in the applications you actually use, so the combinations have to be named |
| Cursor speed follows the Windows pointer settings | Pointer acceleration applies to injected movement too, so the px/s number is a starting point: tune it by feel |
| Some games ignore injected input | They read Raw Input rather than the standard message queue |
| Competitive anti-cheat may block or flag injection | Out of scope; KeyMouse does not attempt to evade detection |
| Elevated windows do not receive input | Windows UIPI. Run KeyMouse elevated if you need this |
| UAC prompts and the lock screen are unreachable | Windows secure desktop, by design |
| Media keys (`Play`, `Volume`, …) cannot be recorded | Dear ImGui 1.93 has no key identifiers for them; the keyboard's own controls still work |
| Windows SmartScreen warns on first run | The binaries are unsigned. Build from source if you prefer |

## Roadmap

0.1.0 ships the whole feature list above: the hook, movement, clicks, scroll,
drag lock, precision and turbo, the dashboard, per-application profiles, the
panic key, four languages, the installer, the portable zip, and a test suite.

Next:

- **0.2** — signed binaries, an optional on-screen position indicator, profile
  import and export, a first-run tour, and per-app profile switching by window
  title
- **Later** — Linux (X11, evdev/uinput) and macOS (CGEventTap) backends behind
  the existing core; see [docs/PORTING.md](docs/PORTING.md)

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

Third-party components are MIT licensed and GPL-compatible: Dear ImGui,
nlohmann/json, doctest.
