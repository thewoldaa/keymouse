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
└── engine thread    ~500 Hz tick, motion integration, injection
```

Three threads, no shared mutable state beyond atomics and one SPSC queue.

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
                       ├─► panic chord? ─────────────────┼─► toggle enabled, return 1
                       ├─► BindingTable::find()          │   (lock-free read)
                       │        │                       │
                       │        └─► swallow? ────────────┼─► return 1
                       │                                │
                       └─► SpscRing<QueuedEvent> ──────►│ pump()
                                                        ├─► Router state machine
                                                        ├─► Motion::step(dt)
                                                        └─► SendInput()
```

The hook callback may not allocate, lock, or perform I/O. Everything expensive
happens on the engine thread. The only synchronization is:

- `BindingTable*` — immutable, published via `BindingTableStore`, never freed
- `SpscRing<QueuedEvent, 512>` — lock-free, single producer, single consumer
- `std::atomic` flags for enable state, action bits, and counters
- `std::shared_ptr<const EngineConfig>` behind a short-lived mutex — the
  configuration hand-off, which is off the hook path and may allocate

Nothing travels the other way: the injected clicks, scrolls, and movements are
produced on the engine thread and never re-enter the hook, which sees only
keyboards.

## Module map

```
src/
├── core/           platform independent, no <windows.h>
│   ├── action      Action enum and stable serialized names
│   ├── binding     Binding model: chord + mode + swallow
│   ├── binding_table  Flat immutable lookup table for the hook thread
│   ├── chord       Chord type, parse, format, modifier bits
│   ├── config      JSON schema, presets, sanitize, validate
│   ├── engine      Ties backend, router, and motion together
│   ├── i18n        Locale loading with English fallback
│   ├── keycodes    Canonical layout-independent key ids
│   ├── motion      Deterministic cursor integrator
│   ├── profiles    Foreground matching, preset application
│   ├── router      Hold/toggle/tap state machine
│   ├── spsc_ring   Lock-free queue
│   └── version     Version string, filled in by CMake
├── input/          platform input backend
│   ├── input_backend.h   the seam
│   ├── backend_win32     WH_KEYBOARD_LL + SendInput
│   └── keycodes_win32    scancode ↔ canonical id
├── platform/       OS services behind narrow interfaces
│   ├── dpi, locale, paths, screen, session, startup, timer
├── ui/             Dear ImGui dashboard
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
5. The panic chord toggles the enable flag, requests a release, and swallows
   both the press and the release. It is checked before the binding table and
   before the enable flag, because it is the way out of a bad configuration.
6. Otherwise the swallow decision is a single `BindingTable::find()` and a
   comparison; the matching key-up is swallowed too, so applications never see a
   phantom release.
7. The event is pushed to the ring, or counted as dropped when the ring is full.

No allocation. No locks. No logging. Dropping a key event is better than missing
the hook timeout.

### Engine thread

`Engine::pump` first applies any configuration published since the last call,
then drains the ring and runs `Router`, which does allocate (it uses
`std::unordered_map`). `Engine::tick` integrates motion and scroll by the
measured `dt` and injects the result. A focused-away or locked session arrives
as an ordinary queued event and releases everything held.

`EngineConfig` reaches this thread as a whole `shared_ptr` snapshot. A
half-updated configuration therefore cannot be applied, and the fields the hook
thread needs — the panic chord, the binding table — are published separately and
immediately.

### UI thread

The dashboard may only call `apply_config`, `set_enabled`, `set_capture_mode`,
`request_release`, and the read-only accessors. Everything else — router state,
motion state, injection — belongs to the engine thread. `EngineStats` is a
snapshot by value: the counters are atomics written from both other threads, and
reading them one by one could show a mixture of two moments.

### Capture mode

While a chord recorder is open in the dashboard the hook neither swallows nor
acts on anything. Events are still counted, but they reach the foreground
application untouched, which is what makes it possible to re-record a chord that
is already bound — otherwise the binding would eat the keys the user is pressing
to describe it.

## Injection and feedback

KeyMouse injects mouse input and hooks the keyboard, and the hook deliberately
does *not* filter events carrying `LLKHF_INJECTED`. Two reasons:

- The sets do not overlap, so there is no loop to guard against: no injected
  event can be a keystroke, and the hook never sees the mouse.
- Filtering key events would break every remapper, macro pad, and on-screen
  keyboard, and it would make the application untestable with synthetic input.
  It reads to the user as "KeyMouse randomly stops working".

The one thing that does need care is that injected *movement* passes through the
Windows pointer settings, which apply pointer acceleration to relative motion.
See the note on speed in the README.

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
  ├─ sanitize()       clamps ranges, drops invalid bindings
  └─ validate()       reports problems, does not block startup
        │
        ▼
apply_config_to_engine()
  ├─ BindingTableStore::publish()   new immutable table
  └─ release_all()                  clear stuck keys
```

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
