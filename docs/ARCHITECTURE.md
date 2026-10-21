# Architecture

## Goals

1. **Idle cost near zero.** A background utility that burns CPU is a bug.
2. **Deterministic motion.** The same settings produce the same movement on
   every machine.
3. **Portable core.** Windows is the first target, not the only one. The
   decision logic must not know what an `HWND` is.

## Process layout

```
KeyMouse.exe
├── UI thread        message pump, ImGui, DirectX 11, tray icon
├── hook thread      WH_KEYBOARD_LL callback, one message loop
│                    WH_MOUSE_LL installed on demand while a macro
│                    is recording or playing
└── engine thread    ~500 Hz tick, motion integration, injection
```

Three threads, no shared mutable state beyond atomics and SPSC queues.

### Why the hook has its own thread

`SetWindowsHookEx(WH_KEYBOARD_LL, ...)` delivers callbacks on the thread that
installed the hook, and that thread must pump messages. If the hook shared the
UI thread, a slow frame would delay key delivery and Windows would silently
remove the hook after the system-wide timeout. A dedicated thread that does
nothing but pump messages keeps callback latency in the microseconds.

### Why the engine is separate again

Motion needs a steady tick, and tying it to the UI refresh rate would make
cursor speed depend on frame rate. The engine thread nominally wakes every 2 ms
and never blocks on the UI.

### Why the mouse hook is temporary

Recording needs mouse events, but a permanently installed `WH_MOUSE_LL` would
run a system-wide callback for every pointer movement from startup to shutdown.
The hook is installed when a recording or a playback starts and removed when
both are done, so an idle KeyMouse costs nothing and a machine that never uses
macros never has a second hook. It lives on the same hook thread as the keyboard
hook, which already has a message loop; the hook itself never swallows and only
converts `MSLLHOOKSTRUCT` into a `MacroInputEvent`.

It does not assume it woke on time: the tick measures the interval and
integrates that, clamped to `kMaxTickSeconds` (50 ms). A machine that stalls —
the laptop suspends, a debugger pauses the thread — then resumes with one long
frame instead of a cursor teleport, and a scheduling hiccup produces a
proportionally larger step rather than a permanently wrong speed.

Sleeping is `platform::sleep_for`, which uses a high-resolution waitable timer
(`CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) and
falls back to raising the timer resolution with `timeBeginPeriod(1)` where that
is unavailable. A plain `Sleep(1)` is rounded up to the scheduler quantum on
Windows 10 before 1803, which is what used to starve this loop.

## Data flow

```
                  hook thread                      engine thread
                       │                                │
    KBDLLHOOKSTRUCT ──►│ scan code → KeyId              │
                       ├─► Fn layer key state            │
                       ├─► capture mode? ────────────────┼─► return 0, never swallow
                       ├─► tagged playback? ────────────┼─► return 0, never swallow
                       ├─► panic chord? ─────────────────┼─► toggle enabled, return 1
                       ├─► mouse-mode toggle? ───────────┼─► flip mode, release, return 1
                       ├─► auto-click toggle? ───────────┼─► flip master switch, return 1
                       ├─► macro control chord? ─────────┼─► request record/play/stop, return 1
                       ├─► per-macro hotkey? ────────────┼─► report the macro id, return 1
                       ├─► recording? ───────────────────┼─► SpscRing<MacroInputEvent>
                       ├─► active table:                  │   (lock-free read)
                       │   mouse mode ? plain keys       │
                       │              : profile chords   │
                       │        └─► swallow? ────────────┼─► return 1
                       │                                │
                       └─► SpscRing<QueuedEvent> ──────►│ pump()
                                                        ├─► Router state machine
                                                        ├─► macro rings → recorder
                                                        ├─► player.tick(clock)
                                                        ├─► Motion::step(dt)
                                                        ├─► AutoClickScheduler::poll(now)
                                                        └─► SendInput() / SetCursorPos()

    MSLLHOOKSTRUCT ───►│ MacroInputEvent ──────────────►│ macro ring
                        │  (tagged when injected)        │  ├─► recorder (untagged only)
                        │                                │  └─► pause a running playback
```

The hook callback may not allocate, lock, or perform I/O. Everything expensive
happens on the engine thread. The only synchronization is:

- `BindingTable*` — immutable, published via `BindingTableStore`, never freed
- `MacroHotkeyTable*` — immutable, published via `MacroHotkeyStore`, never freed
- `SpscRing<QueuedEvent, 512>` — lock-free, single producer, single consumer
- `SpscRing<MacroInputEvent, 8192>` for mouse input and one of 2048 for keyboard
  input — same contract, only active while recording
- `std::atomic` flags for enable state, action bits, and counters
- `std::shared_ptr<const EngineConfig>` behind a short-lived mutex — the
  configuration hand-off, which is off the hook path and may allocate

Nothing travels the other way: the injected clicks, scrolls, and movements are
produced on the engine thread and never re-enter the keyboard hook, which sees
only keyboards. The mouse hook does see them, which is why playback stamps its
injections with an `dwExtraInfo` tag: the hook marks those events as injected,
the engine drops them before they reach a binding, and the recorder ignores them,
so a looping macro cannot record itself.

## Module map

```
src/
├── core/           platform independent, no <windows.h>
│   ├── action      Action enum and stable serialized names
│   ├── autoclick   Auto-click task model, actions, limits
│   ├── binding     Binding model: chord + mode + swallow
│   ├── binding_table  Flat immutable lookup table for the hook thread
│   ├── chord       Chord type, parse, format, modifier bits
│   ├── config      JSON schema, presets, sanitize, validate
│   ├── engine      Ties backend, router, motion, scheduler, and macros together
│   ├── i18n        Locale loading with English fallback
│   ├── keycodes    Canonical layout-independent key ids
│   ├── macro       Macro step model and the .kmacro JSON format
│   ├── macro_hotkeys  Immutable per-macro hotkey table for the hook thread
│   ├── macro_store .kmacro files: save, load, remove, unique ids
│   ├── motion      Deterministic cursor integrator, relative/absolute
│   ├── player      Scheduled macro playback with loops, speed, pause
│   ├── profiles    Foreground matching, preset application, JSON exchange
│   ├── recorder    Compresses a raw event stream into macro steps
│   ├── router      Hold/toggle/tap state machine
│   ├── scheduler   Monotonic-deadline auto-click scheduler
│   ├── spsc_ring   Lock-free queue
│   └── version     Version string, filled in by CMake
├── input/          platform input backend
│   ├── input_backend.h   the seam
│   ├── injection.h       the dwExtraInfo tag for injected playback
│   ├── backend_win32     WH_KEYBOARD_LL + SendInput/SetCursorPos
│   └── keycodes_win32    scancode ↔ canonical id
├── platform/       OS services behind narrow interfaces
│   ├── beep, dpi, file_dialog, locale, paths, screen, session, startup,
│   │   timer
│   └── mouse_hook    on-demand WH_MOUSE_LL install and removal
├── ui/             Dear ImGui dashboard and tabs
└── app/            Application lifetime, CLI, single instance
```

**The rule that keeps this portable:** nothing in `core/` may include
`<windows.h>`. If a core file needs a platform type, the logic belongs in
`input/` or `platform/`. This is enforced by review, and it is why the router
and motion model are unit-tested without a display.

## Threading contract details

### Hook thread

The callback in `backend_win32.cpp` translates and classifies, then calls
`Engine::on_input_event`, which is the only entry point into the engine from
this thread. In order:

1. `Win+Tab`, `Win+L`, and `Win+D` are handed straight back to Windows.
2. Scan code, extended flag, and virtual key become a canonical `KeyId`. Keys
   with no canonical id are passed through.
3. The Fn layer key updates its own state, and every other key gets `ModFn` OR'd
   into its modifier mask while it is held. The mask itself comes from
   `GetAsyncKeyState`, not from tracking key events, so a modifier that was
   already down when KeyMouse started is still reported correctly.
4. Capture mode returns "do not swallow" and stops there. See below.
5. Events carrying the playback tag are dropped and passed through. They were
   produced by the macro player, not by the user.
6. The panic chord toggles the enable flag, requests a release, cancels a macro
   recording, requests a macro stop, and swallows both the press and the
   release. The panic check runs before the binding table and before the enable
   flag, because it is the way out of a bad configuration.
7. The mouse-mode and auto-click toggle chords flip their runtime switches and
   are swallowed. Their releases are swallowed only when the matching press was
   swallowed, so a plain `M` still delivers its key-up while `Ctrl+Alt+M` is the
   mouse-mode chord.
8. The macro control chords (record, play/pause, stop) queue a one-shot request
   for the UI thread and are swallowed. Auto-repeat does not queue a second
   request: the key must come up first.
   Steps 6 to 8 identify an auto-repeat by the base key alone, not by the
   modifier mask. Windows repeats the key-down with whatever modifiers are down
   at that moment, so a consumed chord whose modifiers were released first would
   otherwise leak the repeat to the application while its key-up stayed
   swallowed.
9. A per-macro hotkey is looked up in the published `MacroHotkeyTable` and
   reported as an index into it; the UI thread resolves the id. The chord and its
   release are swallowed, and holding the key does not retrigger it. The same
   base-key rule covers its auto-repeat.
10. While a recording is active, keyboard events are also pushed to the key ring
    (only when keyboard capture is on), and then continue to the binding table.
    The recorder observes keys; it never swallows them.
11. The lookup uses the plain-key table while mouse mode is on and the active
    profile's chord table otherwise. Otherwise the swallow decision is a single
    `BindingTable::find()` and a comparison; the matching key-up is swallowed
    too, so applications never see a phantom release.
12. The event is pushed to the ring, or counted as dropped when the ring is full.

No allocation. No locks. No logging. Dropping a key event is better than missing
the hook timeout.

### Engine thread

`Engine::pump` applies any configuration published since the last call, drains
the macro rings into the recorder (before the requests, so a stop request still
captures the events that arrived just before it), and applies the one-shot macro
requests: start or stop recording, play, pause, resume, stop, and per-macro
hotkeys. It then drains the keyboard ring and runs `Router`, which does allocate
(it uses `std::unordered_map`). `Engine::tick` ticks the macro player against the
injected clock, then integrates motion and scroll by the measured `dt`, polls the
auto-click scheduler, and injects the result. A focused-away or locked session
arrives as an ordinary queued event and releases everything held; a lock also
disarms mouse mode and stops recording and playback.

The player ticks in front of the master-switch check: the switch controls key
handling, and a macro started from the dashboard must still run when it is off.
The panic path stops the player explicitly, so pressing it always ends playback.

`EngineConfig` reaches this thread as a whole `shared_ptr` snapshot. A
half-updated configuration therefore cannot be applied, and the fields the hook
thread needs — the panic chord, the toggle chords, and both binding tables — are
published separately and immediately.

### UI thread

The dashboard may only call `apply_config`, `set_enabled`, `set_capture_mode`,
`set_mouse_mode`, `set_auto_click_enabled`, `set_foreground_exe`,
`request_release`, the macro entry points (`start_macro_recording`,
`stop_macro_recording`, `play_macro`, `request_macro_pause`,
`request_macro_resume`, `request_macro_stop`), the one-shot request consumers,
and the read-only accessors. Everything else — router state, motion state,
scheduler state, macro player state, injection — belongs to the engine thread.
`EngineStats` is a snapshot by value: the counters are atomics written from both
other threads, and reading them one by one could show a mixture of two moments.

### Capture mode

While a chord recorder is open in the dashboard the hook neither swallows nor
acts on anything. Events are still counted, but they reach the foreground
application untouched, which is what makes it possible to re-record a chord that
is already bound — otherwise the binding would eat the keys the user is pressing
to describe it. Entering capture mode clears the tracked toggle-key state; a
held hold-to-activate mouse-mode key cannot see its release while the recorder is
open, so the mode is disarmed rather than left on.

## Injection and feedback

KeyMouse injects mouse input and hooks the keyboard, and the keyboard hook
deliberately does *not* filter events carrying `LLKHF_INJECTED`. Two reasons:

- The sets do not overlap, so there is no loop to guard against: no injected
  event can be a keystroke, and the keyboard hook never sees the mouse. The
  mouse hook exists only while a macro is recording or playing.
- Filtering key events would break every remapper, macro pad, and on-screen
  keyboard, and it would make the application untestable with synthetic input.
  It reads to the user as "KeyMouse randomly stops working".

Mouse playback does need a loop guard, because a looping macro would otherwise
be recorded by the next recording and would pause itself on every movement. The
player scopes the guard with a `TagGuard`: every injection it makes carries
`kInjectionTag` in `dwExtraInfo`, and only the player sets it. The mouse hook
converts the tag to `MacroInputEvent::injected`; the recorder drops those events
and counts them, and the engine drops tagged keyboard events before any binding.
Mouse-mode and auto-click injections stay untagged on purpose: they are ordinary
input, so keyboard-driven cursor movement is recordable.

The one thing that does need care is that injected *movement* passes through the
Windows pointer settings when sent as relative `SendInput`, which applies pointer
acceleration. Absolute movement uses `SetCursorPos` instead: it ignores those
settings and is immune to acceleration, but games that read raw input may not see
it. See the note on speed in the README.

Auto-click runs on the engine thread and uses the same monotonic clock as motion.
Each task stores an absolute deadline, so a slow tick does not accumulate drift.
A deadline more than two seconds late is treated as a missed occurrence and
skipped unless the task asks to run it once; this keeps a suspended machine from
unloading a burst of clicks on resume. The foreground executable is hashed on the
UI thread and published as an atomic for the only-if-app filter.

Macro playback uses the same clock and the same absolute-deadline idea, with a
copied `shared_ptr<const Macro>` so editing a macro while it plays cannot change
the executing step list. A tick more than 500 ms behind schedule stops playback
instead of replaying the backlog, for the same reason the auto-click scheduler
skips missed occurrences. Pausing releases every held button and key and stores
the remaining delay; resuming re-presses them and shifts the next deadline by
that delay, so a pause does not change the pacing of the rest of the macro.
Stopping, failing, or losing the session releases held input through the same
path as `release_all()`.

## Session and focus

Low-level hooks die when the session locks or a UAC prompt appears. KeyMouse
subscribes to `WM_WTSSESSION_CHANGE`, stops the hook while the session is
unavailable, and re-installs it on unlock — `Win32InputBackend::start` and
`stop` are re-entrant for exactly this reason.

Foreground changes release all held actions. Without that, holding a movement
key while alt-tabbing leaves the cursor drifting in the new window.

## Configuration lifecycle

```
load_config()
  ├─ read JSON
  ├─ from_json()      rejects newer schema, keeps .broken copy
  ├─ migrate         schema 1 → 2: accel_max/curve = 1.0
  │                  schema 2 → 3: empty macro library, default macro keys
  ├─ sanitize()       clamps ranges, drops invalid bindings, seeds mouse table,
  │                   normalizes and deduplicates macro ids and hotkeys
  └─ validate()       reports problems, does not block startup
        │
        ▼
apply_config_to_engine()
  ├─ BindingTableStore::publish()   normal and mouse-mode tables
  ├─ MacroHotkeyStore::publish()    per-macro hotkey table
  ├─ AutoClickScheduler::configure() keep or restart each task countdown
  └─ release_all()                  only when the binding lists changed
```

Automatic profile switching runs on a separate runtime profile. The engine is
given a copy of the configuration whose `active_profile` is that runtime index,
so focus changes never move the selection the Profiles tab edits and never mark
the configuration dirty. Selecting a profile by hand adopts it as the runtime
profile on the next apply; `keymouse.exe` in the foreground is ignored.

The configuration holds macro metadata (id, display name, hotkey). The steps
live in one `.kmacro` file per macro under `%LOCALAPPDATA%\KeyMouse\macros`,
loaded when the macro is played or opened in the editor; renaming a macro changes
the display name only, never the file id.

Writes are atomic: write `config.json.tmp`, then rename. A crash mid-write
cannot truncate the real config.

### First run, and who owns "start with Windows"

The registry is the source of truth, not the config file. On a first run the
application writes the entry its default configuration asks for and opens the
dashboard; the config file only mirrors the registry afterwards. A user who
deletes the entry by hand has made a decision, and the next launch must not
silently undo it.

The installer hooks into this in two places. It passes `--locale` so a fresh
install opens in the language the wizard was run in, and `--no-autostart` when
the "start with Windows" task is cleared, so the first launch does not create
the entry the user just declined. Both flags only apply while no config file
exists; neither can override a choice made in the dashboard afterwards.

Language detection is the same kind of default: `platform::system_locale()`
maps the Windows UI language to one of the four shipped translations, and
anything else falls back to English.
