# GamePauser

A simple, accessibility-focused tool for pausing foreground processes (like games or emulators) on Windows. Hit a hotkey to suspend the app, type notes or commands safely, then resume and replay those keystrokes exactly. No freezes, reliable cleanup on exit.

Version 1.2.1 – stable as of November 2025.

## Features
- **Instant pause/resume**: Targets the active window's process via hotkey.
- **Keystroke capture**: Records everything you type while paused; replays faithfully with subtle timing jitter for natural feel.
- **Global hook**: Low-level keyboard interception that doesn't block the hotkey itself.
- **Special keys**: Esc cancels (discards input), Enter accepts without sending itself.
- **Configurable**: Edit `GamePauser.ini` for custom hotkeys (e.g., Ctrl+Alt+P).
- **Safe exit**: Auto-resumes on close, Ctrl+C, or console shutdown.
- **Held key clear**: Releases any stuck keys before pausing to avoid glitches.

Works on any foreground app – tested with games, but flexible for tools or emulators.

## Quick Start
1. Download the latest release exe from [Releases](https://github.com/dunjeon/GamePauser/releases).
2. Run `GamePauser.exe` (place it anywhere; it creates `GamePauser.ini` next to it).
3. Default hotkey: **Ctrl+Alt+P**.
   - Press to pause (app freezes, keys captured).
   - Type your input.
   - Press enter to resume and replay.
   - Or Esc to cancel.

Console output shows status (e.g., "[PAUSED] 1234 - 5 threads").

## Configuration
Edit `GamePauser.ini` (auto-created on first run):
```
; Examples: PauseKey = F12
; Modifiers = Ctrl+Shift+Alt (combine with +)
PauseKey = P
Modifiers = Ctrl+Alt
```
Valid keys: A-Z, 0-9, Space, Enter, Esc, Tab, Arrows, F1-F24, Pause.  
Reload by restarting the exe. If hotkey fails, run as admin or pick another combo.

## License
MIT – use freely, just credit if sharing mods. Authored by Dunjeon. No warranties; test in safe setups.
