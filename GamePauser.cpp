// =============================================================================
// GamePauser - Accessibility-focused process pauser
// Version: 1.2.3, November 2025 - stable, with enhanced retro logging and detailed INI
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
// • Retro logging: Old-terminal style with amber tint (toggle in ini)
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
#include <iomanip> // For timestamp formatting
#include <sstream> // For std::ostringstream
#include <ctime> // For std::tm and localtime_s
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
bool g_unpauseOnNextEnter = false; // True only while paused - Enter accepts without sending Enter
bool g_retroLogs = true; // Toggle for amber/retro styling
HANDLE g_console = nullptr; // For color control
const WORD AMBER_COLOR = 14; // Bright yellow/orange for that phosphor glow
const WORD NORMAL_COLOR = 7; // Default white
// -----------------------------------------------------------------------------
// Forward declarations (required due to function ordering and hook callback)
// -----------------------------------------------------------------------------
static void CleanupAndExit();
static void SetCaptureHook(bool enable);
static void SuspendOrResumeProcess(bool suspend);
static void SendCapturedInputs();
static void LogRetro(const std::string& msg); // Enhanced: Styled logging with more events
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
    f << "; =============================================================================\n"
        << "; GamePauser.ini - Configuration File\n"
        << "; =============================================================================\n"
        << ";\n"
        << "; This is a simple text file for customizing GamePauser. Open it in Notepad or any text editor.\n"
        << "; Lines starting with ';' are comments and ignored. Edit the values after '=' signs.\n"
        << "; Save the file and restart GamePauser for changes to take effect.\n"
        << ";\n"
        << "; --- HOTKEY SETTINGS ---\n"
        << "; The hotkey pauses/resumes the foreground process (e.g., your game).\n"
        << "; Format: Set the key and any modifier keys (Ctrl, Alt, Shift, Win).\n"
        << "; Combine modifiers with '+' (e.g., Ctrl+Alt+Shift).\n"
        << ";\n"
        << "; Valid key examples:\n"
        << ";   - Single letters: A, B, P (case doesn't matter)\n"
        << ";   - Numbers: 1, 2, ...\n"
        << ";   - Special keys: Space, Enter, Esc, Tab, Pause, Left, Right, Up, Down\n"
        << ";   - Function keys: F1, F2, ..., F24\n"
        << ";\n"
        << "; Examples:\n"
        << "; PauseKey = F12\n"
        << "; Modifiers = Ctrl+Alt  (default: Ctrl+Alt + P)\n"
        << "; Modifiers = Shift+Win+F1  (Shift + Windows key + F1)\n"
        << "; Modifiers = Alt  (just Alt + your PauseKey, no other modifiers)\n"
        << ";\n"
        << "; --- LOGGING SETTINGS ---\n"
        << "; RetroLogs: Enables old-school terminal-style logging with timestamps and borders.\n"
        << ";            Set to 0 for plain text logs (easier on modern displays).\n"
        << ";\n"
        << "; Default values below - edit as needed.\n"
        << ";\n"
        << "[Hotkey]\n"
        << "PauseKey = P\n"
        << "Modifiers = Ctrl+Alt\n"
        << "\n"
        << "[Logging]\n"
        << "RetroLogs = 1  ; 0 = plain logs, 1 = retro amber style with timestamps and borders\n"
        << ";\n"
        << "; =============================================================================\n"
        << "; End of file. For support, check the console output or contact the author.\n"
        << "; =============================================================================\n";
    LogRetro("Generated detailed default configuration: " + g_iniPath);
    LogRetro("Edit the INI file in a text editor to customize hotkeys and logging.");
}
// -----------------------------------------------------------------------------
// Enhanced: Retro-styled logging with broader usage
// -----------------------------------------------------------------------------
static void LogRetro(const std::string& msg)
{
    if (!g_retroLogs) {
        std::cout << msg << "\n";
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm timeinfo;
    std::string timestamp;
    if (localtime_s(&timeinfo, &time_t) == 0) {
        std::ostringstream oss;
        oss << "[" << std::put_time(&timeinfo, "%H:%M:%S") << "."
            << std::setfill('0') << std::setw(3) << ms.count() << "] ";
        timestamp = oss.str();
    }
    else {
        // Fallback: basic time without local formatting (rare, but safe)
        std::ostringstream oss_fallback;
        oss_fallback << "[" << time_t << "." << ms.count() << "] ";
        timestamp = oss_fallback.str();
    }
    std::string border = std::string(timestamp.length() + msg.length() + 4, '-'); // Simple underline for that teletype feel
    SetConsoleTextAttribute(g_console, AMBER_COLOR);
    std::cout << border << "\n";
    std::cout << "| " << timestamp << msg << " |\n";
    std::cout << border << "\n";
    SetConsoleTextAttribute(g_console, NORMAL_COLOR);
    std::cout << std::flush; // Ensure it draws immediately, like a slow terminal
    // Subtle "flicker" pause—old CRTs weren't instant
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
            LogRetro("*** KEYBOARD CAPTURE ACTIVATED *** - Inputs queued for replay on resume");
            LogRetro("Note: Global keyboard input blocked during pause - this is by design");
        }
        else {
            LogRetro("WARNING: Failed to install keyboard hook - inputs may not capture properly");
        }
    }
    else if (!enable && g_kbHook) {
        UnhookWindowsHookEx(g_kbHook);
        g_kbHook = nullptr;
        LogRetro("*** KEYBOARD CAPTURE DEACTIVATED *** - Normal input restored");
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
        LogRetro("*** ESCAPE DETECTED: PAUSE CANCELLED *** - Discarding all captured input");
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
        LogRetro("*** ESCAPE DETECTED: PAUSE CANCELLED *** - Discarding all captured input");
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
        LogRetro("*** ENTER DETECTED: PAUSE ACCEPTED *** - Replaying input (Enter suppressed)");
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
    if (snap == INVALID_HANDLE_VALUE) {
        LogRetro("ERROR: Could not snapshot threads for PID " + std::to_string(g_targetPid));
        return;
    }
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
    std::ostringstream oss;
    oss << (suspend ? "*** PROCESS PAUSED ***" : "*** PROCESS RESUMED ***") << " PID: " << g_targetPid << " (" << count << " threads affected)";
    LogRetro(oss.str());
}
// -----------------------------------------------------------------------------
// Replay captured keystrokes
// -----------------------------------------------------------------------------
static void SendCapturedInputs()
{
    if (g_capturedWhilePaused.empty() && g_heldKeys.empty()) {
        LogRetro("*** INPUT REPLAY: Nothing queued - proceeding empty-handed ***");
        return;
    }
    std::ostringstream oss;
    oss << "*** INPUT REPLAY INITIATED ***";
    if (!g_heldKeys.empty()) oss << " (Releasing chord of " << g_heldKeys.size() << " held keys)";
    if (!g_capturedWhilePaused.empty()) oss << " (" << g_capturedWhilePaused.size() / 2 << " key events)";
    LogRetro(oss.str());
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
    LogRetro("*** INPUT REPLAY COMPLETE *** - Target process fully updated");
    g_capturedWhilePaused.clear();
    g_heldKeys.clear();
}
// -----------------------------------------------------------------------------
// Configuration and cleanup
// -----------------------------------------------------------------------------
static void LoadConfig()
{
    LogRetro("*** LOADING CONFIGURATION *** - Scanning for GamePauser.ini");
    std::ifstream file(g_iniPath);
    if (!file.is_open()) {
        LogRetro("No config found - generating detailed default INI file");
        CreateDefaultIni();
        file.open(g_iniPath);
        if (!file.is_open()) {
            LogRetro("ERROR: Could not create or reopen INI file - using built-in defaults");
            return;
        }
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
    g_retroLogs = settings.count("RetroLogs") ? (trim(settings["RetroLogs"]) == "1") : true;
    if (!RegisterHotKey(nullptr, HOTKEY_ID, g_pauseMods, g_pauseVK)) {
        LogRetro("*** HOTKEY REGISTRATION FAILED *** - Run as administrator or change in INI (Modifiers/PauseKey)");
        std::cerr << "Failed to register hotkey - try running as administrator or choose a different combination\n";
    }
    else {
        std::ostringstream oss;
        oss << "*** HOTKEY READY *** " << (settings.count("Modifiers") ? settings["Modifiers"] : "Ctrl+Alt")
            << " + " << (settings.count("PauseKey") ? settings["PauseKey"] : "P");
        LogRetro(oss.str());
        LogRetro("Press the hotkey to pause/resume the foreground process");
    }
    LogRetro("*** CONFIG LOAD COMPLETE *** - System armed and waiting...");
}
static void CleanupAndExit()
{
    LogRetro("*** SHUTDOWN SEQUENCE INITIATED *** - Final safety checks");
    SetCaptureHook(false);
    if (g_targetPid) {
        LogRetro("Force-resuming lingering process PID " + std::to_string(g_targetPid));
        SuspendOrResumeProcess(false);
        g_targetPid = 0;
    }
    UnregisterHotKey(nullptr, HOTKEY_ID);
    LogRetro("*** GOODBYE *** - GamePauser signing off. Stay accessible.");
    SetConsoleTextAttribute(g_console, NORMAL_COLOR); // Reset color on exit
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
    // Retro boot sequence
    g_console = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(g_console, NORMAL_COLOR); // Start neutral
    LogRetro("==========================================");
    LogRetro("|     GAMEPAUSER v1.2.3 - BOOTING...     |");
    LogRetro("==========================================");
    LogRetro("| Accessibility tool for instant process |");
    LogRetro("| pause/resume with keystroke capture.   |");
    LogRetro("==========================================");

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = exePath;
    size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
    g_iniPath = dir + "GamePauser.ini";
    atexit(CleanupAndExit);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    LogRetro("*** SPECIAL CONTROLS ACTIVE *** - ESC = cancel pause, ENTER = accept (no Enter sent)");
    LoadConfig();
    LogRetro("==========================================");
    LogRetro("|          MONITORING FOREGROUND         |");
    LogRetro("|     Hotkey press detected? STANDBY.    |");
    LogRetro("==========================================");
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
                g_heldKeys.clear(); // Reset for during-pause tracking only
                // Clear any held keys before suspending
                std::set<WORD> initialHeld;
                for (int vk = 1; vk < 256; ++vk) { // Skip 0, cover common range
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
                std::ostringstream oss;
                oss << "*** PRE-PAUSE CLEANUP *** - Released " << initialHeld.size() << " held keys to prevent stuck input";
                LogRetro(oss.str());
                g_unpauseOnNextEsc = true;
                g_unpauseOnNextEnter = true;
                SuspendOrResumeProcess(true);
                SetCaptureHook(true);
                LogRetro("*** PAUSE MODE ENGAGED *** - Type freely; replay on resume or special keys");
            }
        }
    }
    return 0;
}
