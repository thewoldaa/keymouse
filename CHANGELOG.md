# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.2] - 2026-10-21

A second correctness release, driven by the post-0.3.1 audit in issue #2. No
features were added and no configuration schema changed; existing `config.json`
and `.kmacro` files load as they are.

### Fixed

- **The reserved-key warning on macro control keys works again.** The 0.3.1 fix
  passed the key as both the chord and the "ignore" argument, which made the
  comparison impossible and disabled the warning entirely. The check now names
  the control key it is skipping, so each picker is compared against the other
  five and never against itself.
- **A chorded control key keeps swallowing its auto-repeat.** After `Ctrl+Alt+M`
  was consumed, releasing the modifiers while `M` stayed down let the OS
  auto-repeat leak to the foreground application, which then saw a key-down
  whose key-up was swallowed and kept `M` stuck. The same fix covers the panic
  key, the auto-click key, the macro record/play/stop chords, and per-macro
  hotkeys.
- **`Break` only stops macros when it is the panic key.** A hardcoded check made
  `Pause` stop playback even when the panic key had been rebound, without
  swallowing it.
- **Config reloads no longer drop held keys.** Applying a configuration
  released every held action, so dragging the speed slider, pressing
  `SpeedUp`/`SpeedDown`, or any unrelated setting stopped a held movement key.
  The routers are rebuilt and held keys released only when the bindings
  actually changed.
- **A tap on a held button no longer releases it.** A `Tap` click on a button
  that a drag lock or a `Hold` binding had already pressed sent an up event and
  let go of it. All button injection now derives from a single held-state mask,
  so the drag lock and a left-click binding cannot release each other, and
  `release_all()` emits exactly one release per button.
- **Toggling an action off no longer stops it while another key holds it.** A
  `Toggle` chord sharing an action with a `Hold` chord reported the action as
  stopped on the off-press; it now reports the combined state.
- **Auto-repeat no longer re-fires a tap or toggle after a release.** The router
  kept its "which keys are down" state across `release_all()`, so a key still
  held when the engine was disabled could fire again on the next repeat.
- **Media, CJK, and `Pause` keys play back correctly.** The scan code table
  mapped media and CJK keys to the ACPI power/sleep/wake scan codes and `Pause`
  to `Ctrl+Break`, which made the backend's virtual-key fallback unreachable
  dead code. Those entries are gone, so those keys take the virtual-key path;
  `Pause` is injected as `VK_PAUSE`.
- **Automatic profile switching no longer overrides the profile being edited.**
  The foreground rules moved `active_profile`, so selecting a profile in the
  Profiles tab and then focusing KeyMouse's own window yanked the selection to
  the fallback. The engine now follows a separate runtime profile, the selection
  is left alone, and `keymouse.exe` is ignored.
- **Foreground polling starts immediately when it becomes necessary.** The poll
  timestamp was advanced before the "nothing needs this" early return, so
  opening the dashboard or enabling a filtered auto-click task waited up to one
  poll period.
- **A macro recording interrupted by the panic key or the lock screen is
  discarded.** The partial fragment used to be saved as a macro, adding an empty
  or truncated entry to the library. Empty recordings are also no longer stored.
- **Unsaved macro editor changes survive New, Duplicate, Import, and a finished
  recording.** Each of those replaces the editor content and silently discarded
  pending edits.
- **One pixel of mouse jitter no longer pauses playback.** Movement is compared
  against the configured movement filter, so clicking Play or Resume no longer
  stops the macro before it starts.
- **The Fn layer picker only offers keys the chord recorder can see.** Media and
  CJK keys have no recorder mapping, so choosing one made `Fn + X` impossible to
  record. The chord recorder also shows held modifiers live instead of leaving
  a stale preview when they are released.
- **The panic-key conflict message stays on screen** instead of flashing for one
  frame, and the panic-key picker now records Fn chords and checks the other
  control keys.
- **Per-macro hotkeys warn about conflicts**, and `chord_in_use()` now counts
  macro hotkeys, so a hotkey that shadows a binding is reported.
- **The keyboard hook thread wakes on its own.** It used a blocking
  `GetMessageW`, so a `WM_QUIT` posted before the message queue existed could
  make shutdown hang. It now uses the same timed wait as the mouse hook.

## [0.3.1] - 2026-10-20

A correctness release for 0.3.0, driven by the code audit in issue #2. No
features were added and no configuration schema changed; existing `config.json`
and `.kmacro` files load as they are.

### Fixed

- **Fn layer chords can be recorded again.** The chord recorder treated the
  configured Fn layer key as an ordinary base key, so pressing `Fn` and then
  another key recorded the Fn key alone. It now counts the Fn layer key as a
  modifier and waits for the real base key. Bindings, mouse mode, auto-click,
  macro hotkeys, and macro control keys all pass the configured key.
- **Macro `KeyDown`/`KeyUp` steps show and accept modifier keys.** The step
  editor reused the chord recorder, which rejects modifier keys, so a recorded
  `Shift` step displayed as `(unset)` and could not be edited. It now uses a
  dedicated single-key recorder that accepts any mapped key.
- **Macro hotkeys can be cleared.** An optional hotkey previously could not be
  unset once assigned; a Clear button next to it now resets it.
- **Click actions honour hold and toggle modes.** A mouse click bound to `Hold`
  or `Toggle` fired a tap on release instead of pressing the button on press.
  Clicks now track the injected button state; held buttons are released on
  disable, focus loss, session lock, and shutdown.
- **Releasing one of two keys bound to the same action no longer stops it.**
  Held actions are reference counted by action, and a toggle turned off from a
  second chord no longer leaves a stale active entry behind.
- **The panic key release is always swallowed.** Releasing the modifiers before
  the panic key used to let the key-up reach the foreground application. The
  press state is now tracked like the other mode toggles.
- **The panic key stops macro recording and playback**, and auto-repeat of a
  held panic key no longer toggles the engine back on.
- **The reserved-key warning on macro control keys is gone.** The check compared
  each control key against itself and always reported a collision.
- **The Bindings tab flags reserved chords**, and the mouse-mode and auto-click
  toggles now check all control keys, not just two of them.
- **Leaving a matched application returns to the fallback profile.** A profile
  with no application match now takes over when no other profile claims the
  foreground window, instead of the last matched profile staying active.
- **Automatic profile switches no longer rewrite the configuration.** The switch
  is applied once and is not persisted, so an Alt-Tab no longer applies the
  config twice and writes `config.json`.
- **`Win+Tab`, `Win+L`, and `Win+D` are passed through correctly.** The hook
  checked the asynchronous key state of the wrong keys and without the `0x8000`
  mask, so the chords were not reliably left to Windows.
- **The mouse hook thread is joined instead of detached.** A quick stop and
  restart could race the previous thread's cleanup against the new thread's
  initialization.
- **`PrintScreen` and `F13`..`F24` play back in macros.** The scan code table
  mapped `F13` to 0x59 and shifted the rest of the range by one, so recorded
  function keys replayed as the wrong key; `PrintScreen`, `F23`, and `F24` had
  no mapping at all. Keys without a scan code now fall back to virtual key
  injection.
- **Exported macros get a `.kmacro` extension.** The save dialog hardcoded
  `.json`, so a typed file name without an extension produced a file the
  importer would reject.

### Changed

- The injection tag flag is atomic, as the backend header already documented.
- The engine's auto-click scheduler uses the engine's injected clock rather
  than a separate `steady_clock`, so tests can drive it deterministically and
  all timing shares one timeline.
- The unused `make_native_backend()` declaration and `is_protected_key()`
  helper are gone.

### Performance

- Publishing an unchanged binding list no longer allocates and retains another
  32 KB table, and macro hotkey tables are deduplicated the same way. Dragging
  a slider in the Movement or Macros tab used to allocate tens of kilobytes per
  frame.
- The foreground executable is no longer queried when nothing consumes it: the
  window is hidden, automatic profile switching is off or has fewer than two
  profiles, and no enabled auto-click task is filtered by application.

### Tests

- 179 unit tests, up from 155: click hold and toggle, button release on
  disable, hold reference counting, shared toggle state, panic release and
  auto-repeat, panic stopping a recording, profile fallback, duplicate table
  publication, and the `PrintScreen` and `F13`..`F24` scan code round trips.

## [0.3.0] - 2026-10-19

Adds the macro subsystem: record mouse input, replay it with loops and speed
control, edit the steps row by row, and keep a library of `.kmacro` files. The
hook path, mouse mode, auto-click, and profiles are unchanged.

### Added

- **Macro recorder** on an on-demand `WH_MOUSE_LL` hook: cursor movement,
  left/right/middle/back/forward clicks, and vertical and horizontal scrolling.
  A movement filter (0.5 to 16 px, 3 px by default) drops points closer
  together than the tolerance. Keyboard capture is optional and off by default.
- **Macro player** with a loop count (0 = forever), a delay between loops, a
  0.25× to 4× speed multiplier, and play, pause, resume, and stop. Playback runs
  on the engine thread against absolute deadlines, can start from any step, and
  releases every held button and key on pause, stop, or failure.
- **Step editor**: delays, step types, coordinates, buttons, wheel deltas, and
  keys are editable in place; rows can be inserted, duplicated, moved, and
  deleted; coordinates can be taken from the current cursor position; delays can
  be halved or doubled; the sequence plays from the selected row.
- **Macro library** of readable JSON `.kmacro` files in
  `%LOCALAPPDATA%\KeyMouse\macros`, with a display name and a global hotkey per
  macro, create, duplicate, rename, delete, import, and export.
- **Control chords** handled on the hook thread: record `Ctrl+Alt+R`,
  play/pause `Ctrl+Alt+P`, stop `Ctrl+Alt+S`, all configurable. The panic key
  (Pause) always stops playback.
- **Physical-mouse auto-pause**: moving or clicking a real mouse during playback
  pauses the macro so the user can take over. Locking the workstation stops
  recording and playback.
- New Macros tab, playback status, and translations in all four bundled
  languages.
- 155 unit tests, adding the macro file format, recorder compression and timing,
  player scheduling, pause and resume, tag scoping, store round trips, and
  engine integration.

### Changed

- Configuration schema bumped to 3. Older files load with an empty macro library
  and default macro settings; macro ids are normalized to safe file stems.
- Macro playback is injected with an `dwExtraInfo` tag; the engine drops tagged
  events and the recorder ignores them, so a replay is never recorded back into
  a macro. Mouse-mode injections stay untagged, so keyboard-to-mouse movement is
  still recordable.
- Pause and stop release held buttons and keys before returning, so a stopped
  macro cannot leave the desktop in a drag.

### Fixed

- Stopping a recording now flushes the events that arrived just before the stop
  request; previously the tail of every recording could be cut off.
- Starting playback while the engine is disabled no longer freezes on the first
  step: the player ticks before the master-switch check.

### Notes

- Macros replay absolute screen coordinates. They do not follow window moves,
  resolution changes, or DPI changes; re-record after rearranging the desktop.
- Keyboard capture does not swallow keys. Captured keys still reach the
  foreground application while they are recorded, because capture runs
  alongside normal routing.

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

[Unreleased]: https://github.com/thewoldaa/keymouse/compare/v0.3.1...HEAD
[0.3.1]: https://github.com/thewoldaa/keymouse/releases/tag/v0.3.1
[0.3.0]: https://github.com/thewoldaa/keymouse/releases/tag/v0.3.0
[0.2.0]: https://github.com/thewoldaa/keymouse/releases/tag/v0.2.0
[0.1.0]: https://github.com/thewoldaa/keymouse/releases/tag/v0.1.0
