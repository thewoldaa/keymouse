# Building KeyMouse

KeyMouse builds on Windows with Visual Studio and CMake. There is nothing to
install beyond the toolchain: the three third-party libraries are downloaded and
checksum-verified during the configure step.

## Prerequisites

| Requirement | Notes |
|---|---|
| Windows 10 or later, x64 | Development is on Windows 11 |
| Visual Studio 2022 | The "Desktop development with C++" workload |
| CMake 3.24 or newer | Any recent installer; it does not need to be on `PATH` if you use the VS-bundled copy |
| PowerShell 5.1 | Only for regenerating the icon |

Python, NASM, vcpkg and a network proxy are *not* required.

## Configure and build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable is `build/Release/KeyMouse.exe`. The build copies `locales/`
next to it so a plain build tree behaves like an installed one.

To build in the background while you work, or just to check that the library
targets compile:

```powershell
cmake --build build --config Release --target keymouse_core
```

## Tests

```powershell
cmake --build build --config Release --target keymouse_tests
.\build\tests\Release\keymouse_tests.exe
```

or through CTest:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The suite covers the platform-independent core only (`keymouse_core`): chords,
the binding table, the router, motion integration, configuration migration and
round-trips, profiles and profile exchange, the auto-click scheduler, engine
routing, and the lock-free queue. Anything that needs a window station, a hook,
or a display is verified by running the application.

`--success` and `--test-case=<name>` are doctest options and work as expected.

## Build options

| Option | Default | Effect |
|---|---|---|
| `KEYMOUSE_BUILD_TESTS` | `ON` | Build the unit test suite |
| `KEYMOUSE_STATIC_RUNTIME` | `ON` | Link the MSVC runtime statically (`/MT`) |
| `KEYMOUSE_WARNINGS_AS_ERRORS` | `OFF` | Turn warnings into errors |

Static linking is the default because it makes the portable zip a single
executable with no VC++ redistributable to install.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DKEYMOUSE_WARNINGS_AS_ERRORS=ON
```

## Dependencies

`cmake/Dependencies.cmake` fetches Dear ImGui, nlohmann/json, and doctest with
`FetchContent` using release **tarballs** pinned by SHA256, not git clones.
Downloads are cached under `build/_deps`, so reconfiguring is offline-friendly
after the first run.

To use a local copy of a dependency (for patching ImGui, say), point the
matching `FETCHCONTENT_SOURCE_DIR_*` variable at it:

```powershell
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_IMGUI=C:\src\imgui
```

The variable names follow the FetchContent convention:
`FETCHCONTENT_SOURCE_DIR_IMGUI`, `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON`,
`FETCHCONTENT_SOURCE_DIR_DOCTEST`.

## The icon and version resource

`resources/keymouse.ico` is generated, not hand-drawn:

```powershell
powershell -ExecutionPolicy Bypass -File tools/make_icon.ps1
```

It emits 16, 24, 32, 48, 64, 128 and 256 pixel frames; the two largest are PNG
compressed, the rest are 32-bit DIBs. `resources/keymouse.rc.in` is filled in by
CMake at configure time so the version block cannot drift from
`project(VERSION ...)` — bump the version in `CMakeLists.txt` and nowhere else.

## Packaging

Portable zip (no installer):

```powershell
cmake --build build --config Release --target package
```

It lands in `build/KeyMouse-<version>-win64.zip` and unpacks into a folder of
the same name: the executable, `locales/`, `docs/`, and the top-level markdown
files. CPack pulls the same `install()` rules the layout is documented with, so
there is no second file list to keep in sync.

Installer, which needs [Inno Setup 6](https://jrsoftware.org/isdl.php) — `iscc`
on `PATH`, or the default per-user location used below:

```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer\keymouse.iss
```

The result is `installer/Output/KeyMouse-<version>-setup.exe`, a per-user
install that never asks for elevation. The script takes the executable and the
translations from `build/Release/`, so this route expects a Release build too.
`installer/languages/Indonesian.isl` is the official *unofficial* translation
committed to the repository, because Inno Setup ships Russian and Japanese but
not Indonesian.

## Troubleshooting

**`git clone` fails with "unsafe repository" or an ownership error.** The
dependency step does not use git; if you hit this while cloning KeyMouse itself,
add the path to `safe.directory` or take ownership of the folder.

**The build cannot find the resource compiler.** Install the "Desktop
development with C++" workload; `rc.exe` ships with the Windows SDK component.

**`defaultlib 'MSVCRT' conflicts` when linking.** Something is being built with a
different runtime than the rest. Check that `KEYMOUSE_STATIC_RUNTIME` is set
consistently and do a clean configure (`cmake -S . -B build` after deleting
`build/CMakeCache.txt`) — the setting is baked into the project files.

**The dashboard shows translation keys like `tab.status` instead of words.**
`locales/*.json` is not next to the executable. Either rebuild (the copy step
runs on every build) or copy the directory by hand.

**The tray icon is the default Windows application icon.** The build did not
embed `resources/keymouse.ico`; regenerate it with `tools/make_icon.ps1` and
rebuild.
