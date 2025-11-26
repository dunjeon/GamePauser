# GamePauser

Press a global hotkey → instantly suspends **all threads** of the current foreground process (usually a game).  
Keeps capturing every key you type while paused.  
Press the hotkey again → resumes the process and faithfully replays everything you typed.

Made for accessibility (RSI, motor issues, one-handed play) but works for anyone who wants a real hard pause.

## Features
- Global hotkey (default Ctrl+Alt+P, fully configurable)
- True process suspension (SuspendThread on every thread)
- Low-level keyboard hook that never eats the hotkey itself
- Captures and replays keystrokes perfectly via SendInput
- Tiny single .exe + GamePauser.ini config
- No freezes, no input lag, no dependencies

## How to use
1. Run `GamePauser.exe`
2. First run creates `GamePauser.ini` next to the exe with the default hotkey
3. Edit the .ini if you want a different key (F24, Pause, etc.)
4. Play your game, press the hotkey → everything freezes but you can still type
5. Press the hotkey again → game resumes and your typing lands instantly

## Configuration (GamePauser.ini)
```ini
; Examples
PauseKey = P          ; or Pause, F24, Space, Enter, Esc, etc.
Modifiers = Ctrl+Alt  ; Ctrl, Alt, Shift, Win — combine with +
