# Game compatibility

KeyMouse injects input through `SendInput`, which is the standard Windows input
path. Some games read it, some do not.

## What works

| Input model | KeyMouse works? | Notes |
|---|---|---|
| Standard Win32 message queue | Yes | Most desktop games and every non-game app |
| DirectInput (legacy) | Yes | DirectInput sits on the same path |
| Raw Input, non-exclusive | Usually | The game sees the movement; some ignore injected flags |
| Raw Input, exclusive | Sometimes | Depends on whether the game filters `LLMHF_INJECTED` |
| Kernel-level anti-cheat | No | See below |

## What does not work, and why

### Anti-cheat

Competitive titles with kernel-level anti-cheat (Vanguard, Easy Anti-Cheat,
BattlEye, FACEIT) can detect injected input and may flag the account. KeyMouse
does not attempt to evade detection, spoof hardware, or disguise injected events.
If a game blocks injected input, that is the game's decision and it stands.

This is a hard boundary, not a limitation to be worked around.

### Exclusive Raw Input

Some games call `RegisterRawInputDevices` with `RIDEV_NOLEGACY` and then read
`RAWINPUT` directly. Whether injected motion reaches them depends on the game.
There is no general fix; the game decides.

### Absolute movement and raw input

Mouse mode uses the same motion path as the normal bindings. If the relative
`SendInput` path is not seen by a game, switch **Motion method** to `absolute`:
that uses `SetCursorPos`, which bypasses pointer acceleration and may work where
relative events do not. The trade-off is the reverse — some raw-input games only
read relative deltas and ignore absolute cursor placement. Try both and keep the
one the game responds to.

### Elevated games

A non-elevated KeyMouse cannot inject into an elevated process because of UIPI.
Run KeyMouse as administrator if you need this. Note that running elevated also
means the dashboard runs elevated, which is a larger trust surface.

## Practical advice

- **Try it first.** Most games work. Test with a harmless binding before relying
  on it in a competitive match.
- **Use `pass` swallow mode** if the game also needs the key. Movement keys
  bound with `swallow` will not reach the game.
- **Record macros at the resolution and window position you intend to replay
  them in.** Absolute steps store screen coordinates, so a moved window or a
  different display mode makes the recording land somewhere else. Camera-drag
  steps are relative and survive a resolution change; the absolute steps around
  them do not.
- **Hold the aim button while the camera is recorded.** While any mouse button
  is down, movement is stored as a relative delta and replayed through relative
  injection, which is the path a first-person game reads to turn the view. A
  button that was already held when Record was pressed is not pressed again on
  playback — the macro assumes the recording's button state — so replay it the
  same way you recorded it. Smooth mouse-look with no button held is ordinary
  pointer movement and is stored as absolute positions.
- **Keep the real mouse still during playback.** Moving it pauses the macro so
  you can take over; that is deliberate, but it also means a knock against the
  desk interrupts a long recording.
- **Macro playback is the same injection as live control.** A game that ignores
  `SendInput` or `SetCursorPos` ignores a recording too. Some anti-cheat
  additionally treats repeated, perfectly timed input as automation.
- **Bind chords the game does not use.** `Ctrl + Alt + Arrow` is a common choice
  precisely because games rarely claim it.
- **Do not use this to gain an unfair advantage in competitive multiplayer.**
  Beyond the ethics, anti-cheat will usually catch it and the account is lost.

## Known-good categories

Turn-based games, strategy, simulation, MMOs, single-player titles, and anything
that does not use kernel anti-cheat. Accessibility use cases — a player who
cannot use a mouse — fall almost entirely in this set.

## Reporting a compatibility result

Open an issue with the game name, whether movement and clicks worked, and
whether the game uses anti-cheat. Results are collected in this file.
