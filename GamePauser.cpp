// =============================================================================
// GamePauser - Accessibility-focused process pauser
// Version: November 2025 - stable, reliable, no freezes
// Author:  (your name here)
//
// Purpose:
//   Press a global hotkey → instantly suspend the foreground process
//   (usually a game) and capture any keys you type while paused.
//   Press the hotkey again → resume the process and replay those keys.
//
// Features:
//   • Works on any foreground window (games, emulators, tools, etc.)
//   • Low-level keyboard hook that never eats the hotkey itself
//   • Captures and faithfully replays keystrokes typed while paused
//   • Simple GamePauser.ini next to the exe for hotkey configuration
// =============================================================================

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

// -----------------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------------
const int   HOTKEY_ID = 9001;
DWORD       g_targetPid = 0;                     // PID of the currently paused process
std::string g_iniPath = "GamePauser.ini";        // Config file placed next to the exe
std::vector<INPUT> g_capturedWhilePaused;        // Keystrokes queued for replay
HHOOK       g_kbHook = nullptr;                  // Low-level keyboard hook handle
WORD        g_pauseVK = 'P';                     // Virtual-key code for the pause hotkey
UINT        g_pauseMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------
static std::string trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// Convert common key names to virtual-key codes
static WORD StringToVK(const std::string& s)
{
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    if (low == "space")   return VK_SPACE;
    if (low == "enter")   return VK_RETURN;
    if (low == "esc" || low == "escape") return VK_ESCAPE;
    if (low == "tab")     return VK_TAB;
    if (low == "pause")   return VK_PAUSE;
    if (low == "left")    return VK_LEFT;
    if (low == "right")   return VK_RIGHT;
    if (low == "up")      return VK_UP;
    if (low == "down")    return VK_DOWN;

    // F1-F24
    if (low.size() >= 2 && low[0] == 'f') {
        try {
            int n = std::stoi(low.substr(1));
            if (n >= 1 && n <= 24) return VK_F1 + n - 1;
        }
        catch (...) {}
    }

    // Single alphanumeric character
    if (low.size() == 1) {
        char c = static_cast<char>(toupper(low[0]));
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) return c;
    }
    return 0;
}

// Parse modifier string (Ctrl, Alt, Shift, Win) - case-insensitive
static UINT ModifiersFromString(const std::string& s)
{
    UINT mods = 0;
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    if (low.find("ctrl") != std::string::npos) mods |= MOD_CONTROL;
    if (low.find("alt") != std::string::npos) mods |= MOD_ALT;
    if (low.find("shift") != std::string::npos) mods |= MOD_SHIFT;
    if (low.find("win") != std::string::npos) mods |= MOD_WIN;
    return mods;
}

// Create a clean default configuration file
static void CreateDefaultIni()
{
    std::ofstream f(g_iniPath);
    if (!f) return;

    f << "; GamePauser configuration\n"
        << "; Hotkey to pause/resume the current foreground process\n"
        << "\n"
        << "; Examples of valid keys: P, Pause, F24, Space, Enter, Esc\n"
        << "; Valid modifiers: Ctrl, Alt, Shift, Win (combine with +)\n"
        << "\n"
        << "PauseKey  = P\n"
        << "Modifiers = Ctrl+Alt\n";

    std::cout << "Created default " << g_iniPath << "\n\n";
}

// -----------------------------------------------------------------------------
// Low-level keyboard hook
//   - Lets the registered hotkey pass through untouched
//   - Captures everything else while a process is paused
//   - Never interferes with injected events
// -----------------------------------------------------------------------------
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || g_targetPid == 0)
        return CallNextHookEx(g_kbHook, nCode, wParam, lParam);

    auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    // Ignore injected events (critical for SendInput replay)
    if (kbd->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_kbHook, nCode, wParam, lParam);

    // Let our exact pause hotkey pass through
    if (kbd->vkCode == g_pauseVK && (wParam == WM_KEYDOWN || wParam == WM_KEYUP)) {
        UINT current = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) current |= MOD_CONTROL;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) current |= MOD_ALT;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) current |= MOD_SHIFT;
        if (GetAsyncKeyState(VK_LWIN) & 0x8000) current |= MOD_WIN;
        if (GetAsyncKeyState(VK_RWIN) & 0x8000) current |= MOD_WIN;

        UINT required = g_pauseMods & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN);
        if ((current & required) == required)
            return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    }

    // Capture everything else while paused
    if (wParam == WM_KEYDOWN || wParam == WM_KEYUP) {
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = static_cast<WORD>(kbd->vkCode);
        if (wParam == WM_KEYUP) inp.ki.dwFlags |= KEYEVENTF_KEYUP;
        if (kbd->flags & LLKHF_EXTENDED) inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        g_capturedWhilePaused.push_back(inp);
    }

    return -1; // suppress the keystroke
}

// Enable or disable the low-level keyboard hook
static void SetCaptureHook(bool enable)
{
    if (enable && !g_kbHook) {
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
            GetModuleHandle(nullptr), 0);
        if (g_kbHook)
            std::cout << "[Capturing keystrokes - they will be delivered on resume]\n";
    }
    else if (!enable && g_kbHook) {
        UnhookWindowsHookEx(g_kbHook);
        g_kbHook = nullptr;
    }
}

// -----------------------------------------------------------------------------
// Process suspend / resume
// -----------------------------------------------------------------------------
static void SuspendOrResumeProcess(bool suspend)
{
    if (!g_targetPid) return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te = { sizeof(te) };
    int count = 0;

    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == g_targetPid) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (hThread) {
                    suspend ? SuspendThread(hThread) : ResumeThread(hThread);
                    CloseHandle(hThread);
                    ++count;
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    std::cout << (suspend ? "[PAUSED] " : "[RESUMED] ")
        << g_targetPid << " - " << count << " threads\n";
}

// -----------------------------------------------------------------------------
// Replay captured keystrokes after resuming
// -----------------------------------------------------------------------------
static void SendCapturedInputs()
{
    if (g_capturedWhilePaused.empty()) {
        std::cout << "[Nothing to send]\n";
        return;
    }

    Sleep(100); // give the target process a moment to wake up

    UINT sent = SendInput(static_cast<UINT>(g_capturedWhilePaused.size()),
        g_capturedWhilePaused.data(), sizeof(INPUT));

    std::cout << "[Delivered " << (sent / 2) << " keystroke(s) typed while paused]\n";
    g_capturedWhilePaused.clear();
}

// -----------------------------------------------------------------------------
// Configuration loading
// -----------------------------------------------------------------------------
static void LoadConfig()
{
    std::ifstream file(g_iniPath);
    if (!file.is_open()) {
        CreateDefaultIni();
        file.open(g_iniPath);
        if (!file.is_open()) return; // give up silently
    }

    std::map<std::string, std::string> settings;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (!key.empty() && !val.empty()) settings[key] = val;
    }

    // Resolve key and modifiers
    g_pauseVK = StringToVK(settings.count("PauseKey") ? settings["PauseKey"] : "P");
    if (g_pauseVK == 0) g_pauseVK = 'P';

    g_pauseMods = ModifiersFromString(settings.count("Modifiers") ? settings["Modifiers"] : "Ctrl+Alt")
        | MOD_NOREPEAT;

    // Register the global hotkey
    if (!RegisterHotKey(nullptr, HOTKEY_ID, g_pauseMods, g_pauseVK)) {
        std::cerr << "Failed to register hotkey - try running as administrator or choose a different combination\n";
    }
    else {
        std::cout << "Hotkey registered: "
            << (settings.count("Modifiers") ? settings["Modifiers"] : "Ctrl+Alt")
            << " + " << (settings.count("PauseKey") ? settings["PauseKey"] : "P")
            << "\n\n";
    }
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int main()
{
    // Place GamePauser.ini next to the executable
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = exePath;
    size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
    g_iniPath = dir + "GamePauser.ini";

    std::cout << "GamePauser - pause any foreground process with a hotkey\n\n";

    LoadConfig();

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            HWND fg = GetForegroundWindow();
            if (!fg) continue;

            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            if (pid == GetCurrentProcessId()) continue; // ignore self

            if (g_targetPid && g_targetPid == pid) {
                // ----------------- RESUME -----------------
                SetCaptureHook(false);
                SuspendOrResumeProcess(false);
                SendCapturedInputs();
                g_targetPid = 0;
            }
            else {
                // ----------------- PAUSE ------------------
                if (g_targetPid) { // clean any previous session
                    SetCaptureHook(false);
                    SuspendOrResumeProcess(false);
                }

                g_targetPid = pid;
                g_capturedWhilePaused.clear();
                SuspendOrResumeProcess(true);
                SetCaptureHook(true);
            }
        }
    }

    // Clean shutdown
    SetCaptureHook(false);
    if (g_targetPid) SuspendOrResumeProcess(false);
    UnregisterHotKey(nullptr, HOTKEY_ID);
    return 0;
}