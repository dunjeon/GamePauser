# GamePauser

**Version 1.2.0** – November 2025  
*Accessibility-focused process pauser*  
By Dunjeon  

---

## What This Is

GamePauser is a small Windows tool that lets you pause whatever's running in your foreground window—like a game, emulator, or any other app—with a simple hotkey press. While it's paused, it quietly captures any keystrokes you type (without letting them through to the app). Hit the hotkey again to resume, and those keys get replayed right back into the app, smooth as you can get it.

I built this with accessibility in mind. Sometimes you need a quick breath during a tricky section, or to jot down a note without losing your place. No more alt-tabbing out and risking a crash or lost progress. It's low-level stuff under the hood, but from your end, it just works—stable, no freezes, and it cleans up after itself if things go sideways.

This version (1.1.0) refines the key replay timing a bit for more natural feel, adds held-key clearing on pause to avoid ghost inputs, and tightens up the special behaviors for Esc and Enter. It's been tested on Windows 10 and 11, but if you're on something older, give it a spin and let me know.

## Features

- **Global Hotkey Pause/Resume**: Press your configured combo (default: Ctrl+Alt+P) to instantly suspend the foreground process's threads. No UI to fiddle with—just works.
- **Keystroke Capture**: While paused, all your typing gets queued up faithfully. It grabs down and up events, handles extended keys (like arrows or numpad), and even mods if they're part of it.
- **Smart Replay**: On resume, keys are sent back with a tiny random jitter (18-45ms between events) to mimic human typing. Attaches to the app's input thread for reliability.
- **Special Pause Controls**:
  - **Escape**: Cancels the pause entirely—discards all captured keys and resumes without sending the Esc.
  - **Enter**: Accepts the pause—resumes and replays keys up to that point, but skips sending the Enter itself (and any modifier keyups that might tag along).
- **Configurable via INI**: Drop a `GamePauser.ini` next to the exe to tweak the hotkey. Supports common keys (P, F1-F24, Space, etc.) and modifiers (Ctrl, Alt, Shift, Win).
- **Safe Exit**: Always unpauses on normal close, Ctrl+C, or console shutdown. No stuck processes.
- **Universal**: Works on any foreground window, not just games. Low CPU footprint—hooks are efficient.

It doesn't touch mouse input or anything visual, so your screen stays frozen just like the app's logic.

## Quick Start

1. **Download**: Grab the latest release from [Releases](https://github.com/yourusername/GamePauser/releases/tag/v1.1.0). It's a single `GamePauser.exe`—no installers.
2. **Run**: Double-click the exe (or run from command line). It'll create a default `GamePauser.ini` if needed and print setup info to the console.
3. **Test**: Fire up Notepad or something simple. Type a bit, hit Ctrl+Alt+P to pause. Type "hello world" while paused. Hit Ctrl+Alt+P again—watch it appear.
4. **Tweak**: Edit the INI for your hotkey prefs, restart the exe.

Console output looks like this during use:

```
GamePauser - Accessibility-focused process pauser
Special keys: Escape = cancel, Enter = accept (without sending Enter)

Hotkey registered: Ctrl+Alt + P

[PAUSED] 1234 - 5 threads
[Cleared 0 held keys]
[Capturing keystrokes - they will be delivered on resume]
[Keyboard is globally blocked while paused - this is normal]

[RESUMED] 1234 - 5 threads
[Delivering input...]
[Finished replay]
```

## Configuration

Everything lives in `GamePauser.ini`, right next to the exe. It's plain text—edit with Notepad or whatever.

### Example `GamePauser.ini`

```
; GamePauser configuration
; Hotkey to pause/resume the current foreground process

; Examples of valid keys: P, Pause, F24, Space, Enter, Esc
; Valid modifiers: Ctrl, Alt, Shift, Win (combine with +)

PauseKey = F12
Modifiers = Alt+Shift
```

- **PauseKey**: The main key. Defaults to `P`. Supports:
  - Letters/numbers: `A`, `1`, etc.
  - Specials: `Space`, `Enter`, `Esc`, `Tab`, `Pause`.
  - Arrows: `Left`, `Right`, `Up`, `Down`.
  - Function keys: `F1` through `F24`.
- **Modifiers**: Combo with `+`. Defaults to `Ctrl+Alt`. Options: `Ctrl`, `Alt`, `Shift`, `Win`. (Adds `MOD_NOREPEAT` automatically to ignore repeats.)

After editing, restart GamePauser. If the hotkey fails to register (e.g., another app grabbed it), it'll warn you—try admin mode or a different combo.

The INI gets auto-created on first run with defaults and comments for guidance.

## Usage Notes

- **Pausing**: Targets only the foreground window's process. Won't touch background stuff. If you switch windows while paused, the original stays suspended until you resume.
- **While Paused**:
  - Keyboard is blocked globally (by design)—you can't type elsewhere, but that's the point.
  - Mouse still works, so you can click around or alt-tab if needed (but resuming sends to the original window).
  - Captures everything except the hotkey itself, which always passes through.
- **Resuming**:
  - Normal hotkey: Replays all captured keys.
  - Esc: Discards everything, resumes clean.
  - Enter: Replays up to but not including the Enter—great for "confirm and go" without extra noise.
- **Held Keys**: On pause, it detects and releases any keys you're holding down (like a stuck W in a game) to prevent weirdness on resume.
- **Replay Timing**: That little Sleep(380) before sending gives the app a beat to catch up. Jitter between keys keeps it from feeling robotic.

Run it from a command prompt if you want to see logs—otherwise, it chills in the tray-less background until you close the console.

## Building from Source

If you want to tweak or build yourself:

- **Requirements**: Visual Studio (or MinGW), Windows SDK. C++11 or later.
- **Steps**:
  1. Clone the repo: `git clone https://github.com/yourusername/GamePauser.git`
  2. Open in VS, build Release x64 (or x86 if you need it).
  3. Exe spits out in the bin folder. No external libs—just WinAPI.

The code's structured for readability: globals up top, utils, hooks, process stuff, then main. Comments explain the why, especially the finicky bits like hook ordering or thread attach.

## Troubleshooting

- **Hotkey Not Working**: Check for conflicts (e.g., Steam or Discord). Run as admin. Verify INI syntax—no spaces around `=`.
- **Process Won't Pause**: Some apps (antivirus, drivers) resist thread suspend. Test on a basic app first. If it's a game with anti-cheat, it might flag this—use at your own risk.
- **Keys Not Replaying Right**: Ensure the window is still foreground on resume. Extended keys (numpad) should handle fine, but if jitter feels off, peek at the `uniform_int_distribution` in `SendCapturedInputs()`.
- **Stuck Paused?**: Kill via Task Manager (search "GamePauser"), but it shouldn't happen—the atexit and Ctrl handler catch most exits.
- **Console Closes Too Fast**: Run from cmd: `GamePauser.exe` to keep it open.

If something's off, open an issue here—include your Windows version, what app you're pausing, and console output. I'll walk through it.

## Limitations

- Windows-only, obviously.
- Doesn't capture/release timing perfectly—replay is sequential with jitter, not timestamped.
- No mouse support yet; that's a v1.2 maybe.
- Admin rights might be needed for some hotkeys or protected processes.

## License

MIT—do what you want, just keep the header. If you fork or improve, PRs welcome.

---
