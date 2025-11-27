# GamePauser v1.1 – True hard pause with perfect key replay  
*NOVEMBER 2025 – tiny single exe, no install, no background junk*

A rock-solid, accessibility-first tool that instantly **suspends every thread** of the foreground process (any game, emulator, tool, whatever) and lets you type freely while paused. When you unpause, everything you typed is replayed perfectly — faster and more accurately than any human could do live.

Perfect for RSI, one-handed play, motor issues, or just nailing frame-perfect sequences without stress.

### What actually happens
1. Press your hotkey (default **Ctrl + Alt + P**) → game freezes instantly.  
2. Keyboard goes completely quiet globally (normal — console says so).  
3. Type or paste whatever you need — chat messages, long menus, TAS-level inputs, anything.  
4. When you’re ready, **tap just the main key** (by default just **P**).  
   You can already have released Ctrl/Alt — only the single key is needed.  
   → Game instantly resumes **and** every keystroke you typed fires in perfect order with natural human timing.

That single-key unpause is deliberate: after typing a long sequence you’re usually relaxed or holding a controller — reaching for the full combo again feels awkward. Just tapping P (or F12 or whatever you set) is effortless.

### What’s new in v1.1
- Nothing ever stays suspended if the game crashes or closes
- Fixed all AttachThreadInput deadlocks
- Cleaner console messages and safer thread handling
- Still one tiny exe + one tiny ini

### How to use
1. Run `GamePauser.exe`  
2. First run creates `GamePauser.ini` next to it  
3. (Optional) Edit the ini to change the hotkey  
4. Play your game, make it foreground  
5. Hit your hotkey → frozen  
6. Type whatever you need  
7. Tap just the main key → back in action with all your inputs perfectly executed

### GamePauser.ini
```ini
; Any normal key name works
PauseKey = P

; Combine with + (case doesn’t matter)
Modifiers = Ctrl+Alt
```

That’s literally it. No services, no admin prompts after the first run, no surprises.  
Just the most reliable hard pause you’ll ever find. Enjoy.
