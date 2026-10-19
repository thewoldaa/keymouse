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

### Elevated games

A non-elevated KeyMouse cannot inject into an elevated process because of UIPI.
Run KeyMouse as administrator if you need this. Note that running elevated also
means the dashboard runs elevated, which is a larger trust surface.

## Practical advice

- **Try it first.** Most games work. Test with a harmless binding before relying
  on it in a competitive match.
- **Use `pass` swallow mode** if the game also needs the key. Movement keys
  bound with `swallow` will not reach the game.
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
