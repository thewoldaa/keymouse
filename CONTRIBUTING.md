# Contributing

Thanks for taking the time. This is a small project; the notes below are what
you need to send a change that fits.

## Getting set up

```powershell
git clone https://github.com/thewoldaa/keymouse
cd keymouse
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\tests\Release\keymouse_tests.exe
```

See [docs/BUILDING.md](docs/BUILDING.md) for prerequisites and build options.

## Before you open a pull request

1. `cmake --build build --config Release` succeeds with no new warnings.
   Warnings are taken seriously here; a clean build is the baseline.
2. The test suite passes: 91 cases, no failures.
3. The application still runs: launch it, move the cursor with the arrow keys,
   and press the panic key. The unit tests do not cover the hook, the tray, or
   rendering.
4. If you changed user-visible text, update **all four** locale files. A missing
   key falls back to English, which is fine for a translation in progress but
   not for a new feature.
5. If you changed the configuration format, update
   [docs/BINDING-FORMAT.md](docs/BINDING-FORMAT.md), the example in the README,
   and the round-trip tests.

## Architecture rules

These are the constraints that keep the project portable and fast. A change that
breaks one of them needs a good reason in the pull request description.

- **`src/core/` must not include `<windows.h>`** or any other platform header.
  The core is compiled and tested without a window station so the Linux and
  macOS backends can be added later. Platform code lives in `src/platform/`,
  `src/input/`, and `src/ui/`.
- **The hook callback does no allocation, no locking, and no I/O.** It performs
  one immutable table lookup and one lock-free push, then returns. Anything more
  risks the hook being removed by Windows for exceeding its time budget.
- **`Router` and `Motion` are engine-thread-only.** The UI thread talks to the
  engine through `Engine::apply_config()`, `set_enabled()`, `set_capture_mode()`
  and `request_release()`. Counters are published as an `EngineStats` snapshot
  by value.
- **Key bindings are stored as canonical physical key names** (`KeyA`,
  `Numpad0`, `ArrowUp`), never virtual-key codes, so a binding survives a
  Russian or Japanese keyboard layout.
- **The engine loop integrates measured time**, not a nominal period, so the
  cursor speed stays correct when the machine is busy.

## Code style

Four space indent, braces on the same line, pointers with the type, namespaces
not indented. `.clang-format` encodes this:

```powershell
clang-format -i src/**/*.cpp src/**/*.h
```

Comments should say *why*, not *what*. Code that looks odd on purpose (a
`reinterpret_cast` in the hook, a `volatile` counter, a sleep for a timer
artefact) should carry a sentence explaining the constraint behind it.

## Commit messages

Short imperative subject, then the reasoning:

```
Fix Pause being reported as NumLock

Pause and NumLock share scan code 0x45 and neither sets the extended flag, so
the scan table alone cannot tell them apart. The virtual key can, and without
the distinction the default panic key never fired.
```

Keep formatting-only changes in their own commit. Do not mix a rename with a
behaviour change.

## Adding a language

1. Copy `locales/en.json` to `locales/<code>.json`.
2. Translate the values only; keys, ordering, and the structure stay as they
   are.
3. Add the language to the table in `src/core/i18n.cpp` (`kLanguages`), giving
   its code and native name.
4. Check it in the dashboard: **General → Interface language**. Missing keys
   show the English string, so an untranslated section is easy to spot.
5. For the installer, add the matching `[Languages]` entry in
   `installer/keymouse.iss`.

Do not machine-translate a whole file and send it untested — partial, accurate
translations are more useful than complete, wrong ones.

## Reporting bugs

Include the version (**Diagnostics → Build**), your Windows version, what you
expected, what happened, and whether any other keyboard software was running
(AutoHotkey, vendor macro software, remappers). The diagnostics tab's counters
are useful: non-zero *dropped events* or *focus losses* explain a lot.

Security issues: please report them privately rather than in a public issue.

## Scope

Ideas that fit: better defaults, accessibility, more direct bindings, clearer
diagnostics, platform backends.

Out of scope: anything that evades anti-cheat detection, macro recording,
keylogging-style history, or shipping a second input path that bypasses the
documented ones.
