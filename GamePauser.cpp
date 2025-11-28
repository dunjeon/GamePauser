// =============================================================================
// GamePauser - Accessibility-focused process pauser
// Version: 1.2.1, November 2025 - stable, reliable, no freezes
// Author: Dunjeon
//
// Purpose:
// Press a global hotkey → instantly suspend the foreground process
// (usually a game) and capture any keys you type while paused.
// Press the hotkey again → resume the process and replay those keys.
//
// Features:
// • Works on any foreground window (games, emulators, tools, etc.)
// • Low-level keyboard hook that never eats the hotkey itself
// • Captures and faithfully replays keystrokes typed while paused
// • Simple GamePauser.ini next to the exe for hotkey configuration
// • Guaranteed unpause on normal exit, Ctrl+C, or console window close
// • Special behavior: Escape = cancel + discard, Enter = accept without sending Enter
// =============================================================================
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <set>
// -----------------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------------
const int HOTKEY_ID = 9001;
DWORD g_targetPid = 0; // PID of the currently paused process
std::string g_iniPath = "GamePauser.ini"; // Config file placed next to the exe
std::vector<INPUT> g_capturedWhilePaused; // Keystrokes queued for replay
HHOOK g_kbHook = nullptr; // Low-level keyboard hook handle
WORD g_pauseVK = 'P'; // Virtual-key code for the pause hotkey
UINT g_pauseMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
std::set<WORD> g_heldKeys; // Keys physically held when resuming (during-pause only, post-clear)
bool g_unpauseOnNextEsc = false; // True only while paused - Esc cancels
bool g_unpauseOnNextEnter = false; // True only while paused - Enter accepts without sending itself
// -----------------------------------------------------------------------------
// Forward declarations (required due to function ordering and hook callback)
// -----------------------------------------------------------------------------
static void CleanupAndExit();
static void SetCaptureHook(bool enable);
static void SuspendOrResumeProcess(bool suspend);
static void SendCapturedInputs();
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
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
static WORD StringToVK(const std::string& s)
{
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low == "space") return VK_SPACE;
    if (low == "enter") return VK_RETURN;
    if (low == "esc" || low == "escape") return VK_ESCAPE;
    if (low == "tab") return VK_TAB;
    if (low == "pause") return VK_PAUSE;
    if (low == "left") return VK_LEFT;
    if (low == "right") return VK_RIGHT;
    if (low == "up") return VK_UP;
    if (low == "down") return VK_DOWN;
    if (low.size() >= 2 && low[0] == 'f') {
        try {
            int n = std::stoi(low.substr(1));
            if (n >= 1 && n <= 24) return VK_F1 + n - 1;
        }
        catch (...) {}
    }
    if (low.size() == 1) {
        char c = static_cast<char>(toupper(low[0]));
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) return c;
    }
    return 0;
}
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
static void CreateDefaultIni()
{
    std::ofstream f(g_iniPath);
    if (!f) return;
    f << "; GamePauser configuration\n"
        << "; Hotkey to pause/resume the current foreground process\n\n"
        << "; Examples of valid keys: P, Pause, F24, Space, Enter, Esc\n"
        << "; Valid modifiers: Ctrl, Alt, Shift, Win (combine with +)\n\n"
        << "PauseKey = P\n"
        << "Modifiers = Ctrl+Alt\n";
    std::cout << "Created default " << g_iniPath << "\n\n";
}
// -----------------------------------------------------------------------------
// Low-level keyboard hook
// -----------------------------------------------------------------------------
static void SetCaptureHook(bool enable)
{
    if (enable && !g_kbHook) {
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
            GetModuleHandle(nullptr), 0);
        if (g_kbHook) {
            std::cout << "[Capturing keystrokes - they will be delivered on resume]\n";
            std::cout << "[Keyboard is globally blocked while paused - this is normal]\n";
        }
    }
    else if (!enable && g_kbHook) {
        UnhookWindowsHookEx(g_kbHook);
        g_kbHook = nullptr;
    }
}
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0 || g_targetPid == 0)
        return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (kbd->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    // Let the configured pause hotkey pass through untouched
    if (kbd->vkCode == g_pauseVK) {
        UINT current = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) current |= MOD_CONTROL;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) current |= MOD_ALT;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) current |= MOD_SHIFT;
        if (GetAsyncKeyState(VK_LWIN) & 0x8000) current |= MOD_WIN;
        UINT required = g_pauseMods & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN);
        if ((current & required) == required)
            return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    }
    // === SPECIAL: Escape – cancel pause (only while actually paused) ===
    if (g_targetPid != 0 && g_unpauseOnNextEsc && kbd->vkCode == VK_ESCAPE && wParam == WM_KEYDOWN) {
        g_unpauseOnNextEsc = false;
        // Order is CRITICAL: unhook FIRST, then resume the process
        SetCaptureHook(false); // ← hook is now gone — no more events will be captured
        SuspendOrResumeProcess(false); // ← game threads resume
        g_targetPid = 0;
        g_capturedWhilePaused.clear();
        g_heldKeys.clear();
        std::cout << "[ESCAPE] Unpaused - all input discarded\n";
        return -1; // eat this Esc down (keyup will never reach us)
    }
    // === SPECIAL: Escape – cancel on first Esc down, suppress Esc + any modifier keyups ===
    if (g_unpauseOnNextEsc && kbd->vkCode == VK_ESCAPE && wParam == WM_KEYDOWN) {
        g_unpauseOnNextEsc = false;
        SetCaptureHook(false);
        SuspendOrResumeProcess(false);
        g_targetPid = 0;
        g_capturedWhilePaused.clear();
        g_heldKeys.clear();
        std::cout << "[ESCAPE] Unpaused - all input discarded\n";
        return -1; // eat Esc down
    }
    if (g_unpauseOnNextEsc && kbd->vkCode == VK_ESCAPE && wParam == WM_KEYUP) {
        g_unpauseOnNextEsc = false; // disarm on keyup too (safety)
        return -1; // eat Esc up
    }
    // === SPECIAL: Enter – accept on first Enter down, suppress Enter + any modifier keyups ===
    if (g_unpauseOnNextEnter && kbd->vkCode == VK_RETURN && wParam == WM_KEYDOWN) {
        g_unpauseOnNextEnter = false;
        SetCaptureHook(false);
        SuspendOrResumeProcess(false);
        SendCapturedInputs(); // only sends keys typed BEFORE this Enter
        g_targetPid = 0;
        std::cout << "[ENTER] Unpaused - input replayed (Enter + modifiers suppressed)\n";
        return -1; // eat Enter down
    }
    if (g_unpauseOnNextEnter && kbd->vkCode == VK_RETURN && wParam == WM_KEYUP) {
        g_unpauseOnNextEnter = false; // disarm on keyup too
        return -1; // eat Enter up
    }
    // Normal capture
    if (wParam == WM_KEYDOWN) {
        g_heldKeys.insert(static_cast<WORD>(kbd->vkCode));
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = static_cast<WORD>(kbd->vkCode);
        if (kbd->flags & LLKHF_EXTENDED) inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        g_capturedWhilePaused.push_back(inp);
    }
    else if (wParam == WM_KEYUP) {
        g_heldKeys.erase(static_cast<WORD>(kbd->vkCode));
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = static_cast<WORD>(kbd->vkCode);
        inp.ki.dwFlags = KEYEVENTF_KEYUP;
        if (kbd->flags & LLKHF_EXTENDED) inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        g_capturedWhilePaused.push_back(inp);
    }
    return -1; // Block all other keys while paused
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
// Replay captured keystrokes
// -----------------------------------------------------------------------------
static void SendCapturedInputs()
{
    if (g_capturedWhilePaused.empty() && g_heldKeys.empty()) {
        std::cout << "[Nothing to send]\n";
        return;
    }
    std::cout << "[Delivering input";
    if (!g_heldKeys.empty()) std::cout << " (chord of " << g_heldKeys.size() << " keys)";
    std::cout << "...]\n";
    Sleep(380);
    HWND fg = GetForegroundWindow();
    DWORD fgThreadId = GetWindowThreadProcessId(fg, nullptr);
    bool attached = false;
    if (fgThreadId && fgThreadId != GetCurrentThreadId()) {
        attached = AttachThreadInput(GetCurrentThreadId(), fgThreadId, TRUE);
        if (attached) Sleep(15);
    }
    // Press all currently held keys first
    for (WORD vk : g_heldKeys) {
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        for (const auto& saved : g_capturedWhilePaused) {
            if (saved.ki.wVk == vk && !(saved.ki.dwFlags & KEYEVENTF_KEYUP)) {
                if (saved.ki.dwFlags & KEYEVENTF_EXTENDEDKEY)
                    inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                break;
            }
        }
        SendInput(1, &inp, sizeof(INPUT));
    }
    Sleep(1);
    // Replay recorded events
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> jitter(18, 45);
    for (size_t i = 0; i < g_capturedWhilePaused.size(); ++i) {
        SendInput(1, &g_capturedWhilePaused[i], sizeof(INPUT));
        if (i + 1 < g_capturedWhilePaused.size())
            Sleep(jitter(gen));
    }
    if (attached) AttachThreadInput(GetCurrentThreadId(), fgThreadId, FALSE);
    std::cout << "[Finished replay]\n";
    g_capturedWhilePaused.clear();
    g_heldKeys.clear();
}
// -----------------------------------------------------------------------------
// Configuration and cleanup
// -----------------------------------------------------------------------------
static void LoadConfig()
{
    std::ifstream file(g_iniPath);
    if (!file.is_open()) {
        CreateDefaultIni();
        file.open(g_iniPath);
        if (!file.is_open()) return;
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
    g_pauseVK = StringToVK(settings.count("PauseKey") ? settings["PauseKey"] : "P");
    if (g_pauseVK == 0) g_pauseVK = 'P';
    g_pauseMods = ModifiersFromString(settings.count("Modifiers") ? settings["Modifiers"] : "Ctrl+Alt")
        | MOD_NOREPEAT;
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
static void CleanupAndExit()
{
    std::cout << "\n[Shutting down - ensuring nothing stays paused]\n";
    SetCaptureHook(false);
    if (g_targetPid) {
        SuspendOrResumeProcess(false);
        g_targetPid = 0;
    }
    UnregisterHotKey(nullptr, HOTKEY_ID);
}
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        CleanupAndExit();
        if (dwCtrlType == CTRL_CLOSE_EVENT) {
            Sleep(100);
            exit(0);
        }
        return TRUE;
    }
    return FALSE;
}
// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int main()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = exePath;
    size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
    g_iniPath = dir + "GamePauser.ini";
    atexit(CleanupAndExit);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    std::cout << "GamePauser - Accessibility-focused process pauser\n";
    std::cout << "Special keys: Escape = cancel, Enter = accept (without sending Enter)\n\n";
    LoadConfig();
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            HWND fg = GetForegroundWindow();
            if (!fg) continue;
            DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            if (pid == GetCurrentProcessId()) continue;
            if (g_targetPid && g_targetPid == pid) {
                // Resume via normal hotkey
                g_unpauseOnNextEsc = g_unpauseOnNextEnter = false;
                SetCaptureHook(false);
                SuspendOrResumeProcess(false);
                SendCapturedInputs();
                g_targetPid = 0;
            }
            else {
                // Pause
                if (g_targetPid) {
                    SetCaptureHook(false);
                    SuspendOrResumeProcess(false);
                }
                g_targetPid = pid;
                g_capturedWhilePaused.clear();
                g_heldKeys.clear();  // Reset for during-pause tracking only
                // Clear any held keys before suspending
                std::set<WORD> initialHeld;
                for (int vk = 1; vk < 256; ++vk) {  // Skip 0, cover common range
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        initialHeld.insert(static_cast<WORD>(vk));
                    }
                }
                DWORD fgThreadId = GetWindowThreadProcessId(fg, nullptr);
                bool attached = false;
                if (fgThreadId && fgThreadId != GetCurrentThreadId()) {
                    attached = AttachThreadInput(GetCurrentThreadId(), fgThreadId, TRUE);
                    if (attached) Sleep(15);
                }
                for (WORD vk : initialHeld) {
                    INPUT inp = {};
                    inp.type = INPUT_KEYBOARD;
                    inp.ki.wVk = vk;
                    inp.ki.dwFlags = KEYEVENTF_KEYUP;
                    // Basic extended handling: set for numpad/right-side keys
                    if ((vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) || vk >= VK_F1) {
                        inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                    }
                    SendInput(1, &inp, sizeof(INPUT));
                }
                if (attached) {
                    AttachThreadInput(GetCurrentThreadId(), fgThreadId, FALSE);
                }
                std::cout << "[Cleared " << initialHeld.size() << " held keys]\n";
                g_unpauseOnNextEsc = true;
                g_unpauseOnNextEnter = true;
                SuspendOrResumeProcess(true);
                SetCaptureHook(true);
            }
        }
    }
    return 0;
}
