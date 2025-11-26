# GamePauser v1.1 — True hard pause with safe, perfect key replay

Press a global hotkey → instantly **suspends every thread** of the current foreground window (game, emulator, tool, anything).  
While paused, you can still type freely — every single key is captured.  
Press the hotkey again → the process resumes and **all captured keystrokes are faithfully replayed** with human-like timing, exactly as you typed them.

Built primarily for accessibility (RSI, limited mobility, one-handed gaming, motor disabilities), but equally useful for anyone who wants a real, rock-solid pause that no game can ignore.

### What’s new in v1.1 (November 2025)
- No more frozen games — if the target process crashes or is killed while paused, nothing stays suspended forever  
- AttachThreadInput deadlock completely eliminated (no more random hangs with overlays/RTSS/Discord)  
- Cleaner thread handling and a gentle console reminder when the keyboard goes quiet  
- Still the same tiny single executable, no dependencies

### Features
- True process-wide suspension using SuspendThread/ResumeThread on every thread  
- Global hotkey fully configurable via GamePauser.ini (default Ctrl+Alt+P)  
- Low-level keyboard hook that never blocks the hotkey itself  
- Perfect keystroke capture + replay via SendInput with natural jitter (40–110 ms hold, 30–90 ms between keys)  
- Replays short messages instantly; longer typing sequences play back in full (a few seconds if you hammered the keyboard)  
- Safely unpauses on normal exit, Ctrl+C, or console close  
- Single .exe + tiny .ini, no installer, no background services

### How to use
1. Download and run `GamePauser.exe`  
2. On first run it creates `GamePauser.ini` next to the executable  
3. (Optional) Edit the .ini to change the hotkey  
4. Launch your game, bring it to the foreground  
5. Press your hotkey → game freezes instantly, keyboard is globally blocked (this is normal — shown in console)  
6. Type whatever you need (chat, menu navigation, macros…)  
7. Press the hotkey again → game resumes and everything you typed lands exactly as intended

### Configuration (GamePauser.ini)
```ini
; Valid keys: P, Pause, F24, Space, Enter, Esc, Left, F1…F24, etc.
PauseKey = P

; Valid modifiers: Ctrl, Alt, Shift, Win — combine with +
; Example: Ctrl+Shift or Alt+Win
Modifiers = Ctrl+Alt
```

That’s it. No background process, no UAC prompts after the first run, no surprises.  
Just a quiet, reliable hard pause when you need it most.
