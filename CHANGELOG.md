# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-10-19

Adds the first batch of KalMouse-spec features: a plain-key mouse mode, a
scheduled auto-click system, profile exchange, and finer motion control. The
existing chord system and configuration path are unchanged.

### Added

- **Mouse mode**, a second binding table that uses plain keys with no Ctrl, Alt,
  or Fn layer. Defaults: `I`/`K`/`J`/`L` movement, `U`/`O`/`P` left, right, and
  middle click, `Y`/`H` scroll, `F`/`G` precision and turbo, `Space` drag lock.
- Mouse-mode toggle on `Ctrl+Alt+M`, selectable toggle or hold behaviour, an
  optional beep, an optional "start enabled" preference, dashboard and tray
  status, and automatic disarm when the session locks.
- **Scheduled auto-click** with a master switch on `Ctrl+Alt+T`, multiple tasks,
  left/right/middle/double-click actions, intervals from one second to seven
  days, a first-click delay, a repeat limit, optional foreground-application
  filtering, and a per-task "run missed clicks" choice. Countdown and total
  clicks are shown in the dashboard.
- **Profile import and export** as JSON, with validation, unique naming on
  import, and the existing duplicate-chord and reserved-hotkey warnings extended
  to the mouse-mode table.
- **Absolute movement** (`SetCursorPos`) as an alternative to relative
  `SendInput`, plus an adjustable acceleration maximum and curve.
- New Mouse mode and Auto-click tabs, status counters, and translations in all
  four bundled languages.
- 130 unit tests, including scheduler timing and injection, mouse-mode routing, configuration
  migration, and profile exchange.

### Changed

- Configuration schema bumped to 2. Schema-1 files migrate automatically; the
  new acceleration fields default to 1.0 so the old fixed-speed feel is
  preserved.
- The dashboard starts centred at a size that fits the monitor's work area and
  DPI scale, instead of a fixed 980×700 that could extend past the taskbar.
- Tab content scrolls inside a fixed body while the header and tab bar stay
  visible.

### Fixed

- A key-up that matched a toggle key's base key was swallowed even when the
  key-down was not, which could leave the letter stuck in the foreground
  application.
- Re-enabling an auto-click task now starts a fresh countdown; previously the
  task stayed dormant because its disabled deadline survived the reload.
- The auto-click duration unit selector kept reverting to seconds because its
  read and write used different ImGui state ids.
- Profile import and export dialogs are owned by the dashboard, so they no
  longer open behind it.
- The mouse-mode and auto-click hotkeys accept a fresh press after a lock or
  unlock, even when Windows dropped the matching key-up.
- Initial UI scaling now follows the window's DPI instead of always using 100%.

### Notes

- Mouse mode is off on upgrade by default; turn it on from the dashboard, the
  tray menu, or `Ctrl+Alt+M`.
- Macro recording, playback, editing, and the macro library are planned for
  0.3.0.

## [0.1.0] - 2026-10-19

First release. Windows 10 and later, x64, no administrator rights required.

### Added

- Global low-level keyboard hook with a lock-free hand-off to the input engine;
  the hook thread never allocates or blocks.
- Actions: cursor movement in eight directions, left/right/middle/back/forward
  click, double click, drag lock, scrolling (vertical and horizontal), precision
  and turbo modifiers, cursor-to-centre, enable toggle, dashboard toggle, and
  speed up/down.
- Explicit chord recording: any combination of up to five modifiers
  (Ctrl, Shift, Alt, Win, Fn layer) plus one key, recorded by pressing it.
- Per-binding modes: hold, toggle, tap, with an independent "swallow the key"
  flag so an application can still receive the keystroke.
- Per-application profiles that switch automatically on focus change.
- Three presets: Desktop, Numpad, and Game.
- Full dashboard: status with live counters, bindings, movement tuning,
  profiles, general settings, and diagnostics. Dear ImGui on DirectX 11, no
  browser engine and no runtime dependencies.
- Panic key that disables every mapping at once.
- Tray icon with enable toggle, startup toggle, and quit; closing the window
  keeps the application running.
- Start with Windows via `HKCU\...\Run`, no service and no scheduled task.
- Locales: English, Russian, Japanese, Indonesian.
- Configuration in `%LOCALAPPDATA%\KeyMouse\config.json`, atomically written,
  validated on load, with a broken file preserved as `config.json.broken`.
- Application icon and version resource generated at build time.
- Windows installer (Inno Setup) in English, Russian, Japanese, and Indonesian:
  per user, no elevation prompt, uninstaller asks before removing settings. A
  portable zip is produced from the same `install()` rules.
- Command line: `--minimized`, `--safe-mode`, `--no-autostart`, `--locale
  CODE`, `--config PATH`, `--help`, `--version`.
- The starting language follows the Windows UI language on a first run, and the
  installer's language on a fresh install; both remain changeable in the
  dashboard.
- The version string shown in the dashboard header, the diagnostics tab, and
  `--version` comes from `project(VERSION ...)` in `CMakeLists.txt`, so it cannot
  drift from the executable's resource block.
- 103 unit tests covering chords, routing, motion, configuration, profiles, the
  ring buffer, and command line parsing.

### Notes

- The hardware `Fn` key is invisible to Windows, so the application provides its
  own Fn layer key instead. See the README.
- Modifier matching is exact: while any modifier is held, a binding that does
  not name it will not fire. Bind the combinations you need.
- Cursor speed is affected by the Windows pointer settings, which also apply to
  injected movement. Tune the slider by feel rather than by the number.
- Media keys (play, volume, and the like) cannot be bound: Dear ImGui 1.93 has
  no key identifiers for them. Their normal functions are unaffected.

[Unreleased]: https://github.com/thewoldaa/keymouse/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/thewoldaa/keymouse/releases/tag/v0.2.0
[0.1.0]: https://github.com/thewoldaa/keymouse/releases/tag/v0.1.0
