/*
 * xntsrv-appearance - makes X programs follow the user's Windows appearance.
 *
 * Started by the launcher next to the X server, as the same user.  It reads
 * a few classic Windows settings (display scale, UI font, pointer size, light
 * or dark preference, and the console's colours) and publishes them over X's
 * own channels: the XSETTINGS property, which toolkits watch, and the X
 * resources on the root window.  Nothing is installed in any guest.  It
 * republishes when Windows reports a change, and ends when the server does.
 * If the display already has a settings manager, it leaves it alone and exits.
 *
 *   xntsrv-appearance [--display=N]   run (what the launcher does)
 *   xntsrv-appearance --print         show what would be published, and exit
 *   xntsrv-appearance --write-cursors=DIR
 *                                     convert the pointer scheme into DIR, and exit
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <shlobj.h>

#include "cursors-win.h"
#include "publish.h"
#include "theme-win.h"

using std::wstring;

static void Out(const wstring &text) { xnt::ConsoleWrite(STD_OUTPUT_HANDLE, text + L"\n"); }
static void Err(const wstring &text) { xnt::ConsoleWrite(STD_ERROR_HANDLE, L"xntsrv-appearance: " + text + L"\n"); }

static std::string Utf8(const wstring &text)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], n, NULL, NULL);
    return out;
}

static wstring WideUtf8(const std::string &text)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], n);
    return out;
}

/* ---- reading Windows --------------------------------------------------- */

/* These exist from Windows 10 on, so they are looked up at run time and the
 * program still starts where one is missing. */
template <typename Fn> static Fn Lookup(const wchar_t *dll, const char *name)
{
    HMODULE module = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module ? (Fn)(void *)GetProcAddress(module, name) : NULL;
}

/* Ask Windows for real pixel numbers, not ones scaled on our behalf. */
static void BecomeDpiAware()
{
    typedef BOOL(WINAPI *SetContextFn)(HANDLE);
    SetContextFn set = Lookup<SetContextFn>(L"user32.dll", "SetProcessDpiAwarenessContext");
    if (!set || !set((HANDLE)(LONG_PTR)-4)) /* per monitor, version 2 */
        SetProcessDPIAware();
}

/* The primary monitor's scale as dots per inch: 96 is 100%, 144 is 150%. */
static int PrimaryDpi()
{
    typedef HRESULT(WINAPI *GetDpiFn)(HMONITOR, int, UINT *, UINT *);
    GetDpiFn get = Lookup<GetDpiFn>(L"shcore.dll", "GetDpiForMonitor");
    POINT origin = { 0, 0 };
    UINT x = 0, y = 0;

    if (get && SUCCEEDED(get(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), 0 /* effective */, &x, &y)) && x)
        return (int)x;
    HDC screen = GetDC(NULL);
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    return dpi ? dpi : 96;
}

static DWORD RegistryNumber(const wchar_t *key, const wchar_t *name, DWORD fallback)
{
    DWORD value = 0, size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, NULL, &value, &size) == ERROR_SUCCESS)
        return value;
    return fallback;
}

/* What Windows says now.  `xWidth` is the width of the X screen in pixels, or
 * 0 when not connected. */
static Appearance ReadWindows(unsigned xWidth)
{
    Appearance a;

    /* The pointer scheme, converted afresh each time: it is quick, and it is
     * the simplest way to follow a change of scheme or size. */
    int files = 0;
    wstring theme = CursorThemeDir();
    if (!theme.empty() && WriteCursorTheme(theme, &files) > 0)
        a.cursorTheme = kCursorThemeName;

    int dpi = PrimaryDpi();

    /* The X server may see the desktop in real pixels or in pixels Windows
     * scales for it, depending on how it was started.  Comparing its screen
     * width with the real one tells which, and so what DPI its clients need. */
    int realWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    a.dpi = xWidth && realWidth > 0 ? MulDiv(dpi, (int)xWidth, realWidth) : dpi;
    if (a.dpi < 48 || a.dpi > 960)
        a.dpi = 96;

    /* The font Windows uses for messages and dialogs, in points. */
    typedef UINT(WINAPI *SystemDpiFn)(void);
    SystemDpiFn systemDpi = Lookup<SystemDpiFn>(L"user32.dll", "GetDpiForSystem");
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof metrics;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0) && metrics.lfMessageFont.lfFaceName[0]) {
        int height = metrics.lfMessageFont.lfHeight;
        int points = MulDiv(height < 0 ? -height : height, 72, systemDpi ? (int)systemDpi() : 96);
        if (points < 6 || points > 72)
            points = 9;
        a.font = Utf8(metrics.lfMessageFont.lfFaceName) + " " + std::to_string(points);
    }

    /* The pointer size chosen in Settings, which Windows scales with the display. */
    DWORD base = RegistryNumber(L"Control Panel\\Cursors", L"CursorBaseSize", 32);
    if (base < 16 || base > 256)
        base = 32;
    a.cursorSize = MulDiv((int)base, a.dpi, 96);

    a.preferDark = RegistryNumber(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                  L"AppsUseLightTheme", 1) == 0;

    /* GTK programs get a flat theme in the classic Windows colours.  Those
     * colours stay light when Windows is in dark mode, so there the guest's
     * own dark theme is left to do the job. */
    wstring gtkTheme = GtkThemeDir();
    if (!a.preferDark && !gtkTheme.empty() && WriteGtkTheme(xnt::ProgramDir() + L"\\theme", gtkTheme))
        a.gtkTheme = kGtkThemeName;

    /* The colours of the Windows console (cmd, PowerShell), for X terminals.
     * Where the user has not changed one, Windows' own default applies. */
    static const DWORD kDefault[16] = { /* as 0x00BBGGRR, in Windows' order */
        0x000C0C0C, 0x00DA3700, 0x000EA113, 0x00DD963A, 0x001F0FC5, 0x00981788, 0x00009CC1, 0x00CCCCCC,
        0x00767676, 0x00FF783B, 0x000CC616, 0x00D6D661, 0x005648E7, 0x009E00B4, 0x00A5F1F9, 0x00F2F2F2,
    };
    /* Windows numbers them black, blue, green, cyan, red, magenta, yellow,
     * white; terminals swap red with blue and yellow with cyan. */
    static const int kWindowsIndex[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    static const int kTerminalIndex[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    for (int i = 0; i < 16; i++) {
        int w = kWindowsIndex[i & 7] | (i & 8);
        wchar_t name[16];
        wsprintfW(name, L"ColorTable%02d", w);
        DWORD bgr = RegistryNumber(L"Console", name, kDefault[w]);
        a.palette.push_back(((bgr & 0xff) << 16) | (bgr & 0xff00) | ((bgr >> 16) & 0xff));
    }
    /* Low four bits: the text colour; next four: the background. */
    DWORD screen = RegistryNumber(L"Console", L"ScreenColors", 0x07);
    a.foreground = kTerminalIndex[screen & 7] | (int)(screen & 8);
    a.background = kTerminalIndex[(screen >> 4) & 7] | (int)((screen >> 4) & 8);

    /* The console's font, window size, history and cursor.  The defaults are
     * what Windows 10 and 11 use when nothing has been changed. */
    wchar_t face[LF_FACESIZE] = L"";
    DWORD faceBytes = sizeof face;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Console", L"FaceName", RRF_RT_REG_SZ, NULL, face, &faceBytes) != ERROR_SUCCESS ||
        !face[0] || lstrcmpW(face, L"__DefaultTTFont__") == 0)
        lstrcpyW(face, L"Consolas");
    /* "Terminal" is the old bitmap font, which has no counterpart to ask for. */
    a.terminalFont = lstrcmpiW(face, L"Terminal") == 0 ? "" : Utf8(face);
    a.terminalBold = RegistryNumber(L"Console", L"FontWeight", 400) >= 600;
    /* High half: the character height in pixels at 100% scale. */
    int pixels = (int)(RegistryNumber(L"Console", L"FontSize", 0x00100000) >> 16);
    a.terminalPoints = pixels >= 6 && pixels <= 144 ? MulDiv(pixels, 72, 96) : 12;
    /* High half: rows; low half: columns. */
    DWORD window = RegistryNumber(L"Console", L"WindowSize", 0x001E0078);
    DWORD buffer = RegistryNumber(L"Console", L"ScreenBufferSize", 0x23290078);
    a.terminalColumns = (int)(window & 0xffff);
    a.terminalRows = (int)(window >> 16);
    if (a.terminalColumns < 20 || a.terminalColumns > 1000 || a.terminalRows < 5 || a.terminalRows > 500) {
        a.terminalColumns = 120;
        a.terminalRows = 30;
    }
    int history = (int)(buffer >> 16) - a.terminalRows;
    a.terminalScrollback = history < 0 ? 0 : history > 100000 ? 100000 : history;
    /* The cursor's height as a percentage of the cell: small means a line. */
    a.terminalUnderlineCursor = RegistryNumber(L"Console", L"CursorSize", 25) < 50;
    return a;
}

/* ---- the X connection -------------------------------------------------- */

static std::string ReadCookie()
{
    PWSTR base = NULL;
    std::string contents;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base))) {
        wstring path = wstring(base) + L"\\" + xnt::kUserDataDir + L"\\Xauthority";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            char buf[4096];
            DWORD n = 0;
            if (ReadFile(file, buf, sizeof buf, &n, NULL))
                contents.assign(buf, n);
            CloseHandle(file);
        }
    }
    CoTaskMemFree(base);
    return CookieFromXauthority(contents);
}

/* Connect to the server on this computer, waiting while it starts up. */
static SOCKET ConnectToServer(unsigned display)
{
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)(xnt::kXBasePort + display));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    for (int attempt = 0; attempt < 120; attempt++) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET && connect(s, (sockaddr *)&addr, sizeof addr) == 0)
            return s;
        if (s != INVALID_SOCKET)
            closesocket(s);
        Sleep(500);
    }
    return INVALID_SOCKET;
}

/* ---- noticing changes -------------------------------------------------- */

static bool g_changed;

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    case 0x02E0: /* WM_DPICHANGED */
        g_changed = true;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

/* Windows sends setting changes to top-level windows only, so this is an
 * ordinary window that is never shown. */
static void CreateListenerWindow()
{
    WNDCLASSW cls = {};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"XNTsrvAppearance";
    RegisterClassW(&cls);
    CreateWindowExW(0, cls.lpszClassName, L"XNTsrv appearance", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, cls.hInstance, NULL);
}

/* ---- main -------------------------------------------------------------- */

static void Print(const Appearance &a)
{
    Out(L"dpi=" + std::to_wstring(a.dpi));
    Out(L"cursor=" + std::to_wstring(a.cursorSize));
    Out(L"dark=" + std::to_wstring(a.preferDark ? 1 : 0));
    Out(L"font=" + WideUtf8(a.font));
    Out(L"gtk-theme=" + WideUtf8(a.gtkTheme));
    for (const auto &resource : ResourcesFor(a))
        Out(WideUtf8(resource.first + ": " + resource.second));
}

int wmain(int argc, wchar_t **argv)
{
    unsigned display = 0;
    bool print = false;

    for (int i = 1; i < argc; i++) {
        wstring arg = argv[i];
        if (arg.compare(0, 16, L"--write-cursors=") == 0 && arg.size() > 16) {
            int files = 0;
            BecomeDpiAware();
            int pointers = WriteCursorTheme(arg.substr(16), &files);
            Out(std::to_wstring(pointers) + L" pointers written as " + std::to_wstring(files) + L" files");
            return pointers > 0 ? 0 : 1;
        } else if (arg == L"--print") {
            print = true;
        } else if (arg.compare(0, 10, L"--display=") == 0 && arg.size() > 10) {
            display = (unsigned)_wtoi(arg.c_str() + 10);
        } else if (arg == L"-V" || arg == L"--version") {
            Out(L"xntsrv-appearance " XNTSRV_VERSION);
            return 0;
        } else {
            xnt::ConsoleWrite(arg == L"-h" || arg == L"--help" ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE,
                              L"Usage: xntsrv-appearance [--display=N] | --print\n"
                              L"Publishes the Windows display scale, UI font, pointer size and light or dark\n"
                              L"preference to X programs. Started by xntsrv; see xntsrv --no-appearance.\n");
            return arg == L"-h" || arg == L"--help" ? 0 : 2;
        }
    }

    BecomeDpiAware();
    if (print) {
        Print(ReadWindows(0));
        return 0;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;
    SOCKET s = ConnectToServer(display);
    if (s == INVALID_SOCKET) {
        Err(L"no X server answers on display " + std::to_wstring(display));
        return 1;
    }

    XClient x;
    if (!x.Open(s, ReadCookie())) {
        Err(WideUtf8(x.Error()));
        return 1;
    }

    Appearance current = ReadWindows(x.RootWidth());
    Publisher publisher;
    switch (publisher.Start(&x, current)) {
    case Publisher::kOtherManager:
        return 0; /* the display has its own settings manager */
    case Publisher::kFailed:
        Err(WideUtf8(x.Error()));
        return 1;
    default:
        break;
    }

    CreateListenerWindow();
    /* Console colours are changed without any announcement, so watch their
     * registry key. */
    HKEY console = NULL;
    HANDLE consoleChanged = CreateEventW(NULL, FALSE, FALSE, NULL);
    DWORD watched = 0;
    if (consoleChanged && RegOpenKeyExW(HKEY_CURRENT_USER, L"Console", 0, KEY_NOTIFY, &console) == ERROR_SUCCESS &&
        RegNotifyChangeKeyValue(console, FALSE, REG_NOTIFY_CHANGE_LAST_SET, consoleChanged, TRUE) == ERROR_SUCCESS)
        watched = 1;

    for (;;) {
        /* Wake on a message from Windows or a console colour change, and now
         * and then regardless, to notice the server going away. */
        if (MsgWaitForMultipleObjects(watched, &consoleChanged, FALSE, 3000, QS_ALLINPUT) == WAIT_OBJECT_0 && watched) {
            g_changed = true;
            RegNotifyChangeKeyValue(console, FALSE, REG_NOTIFY_CHANGE_LAST_SET, consoleChanged, TRUE);
        }
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);

        bool lost = false;
        if (!x.ReadEvents(publisher.Owner(), &lost) || lost)
            return 0; /* the server ended, or another manager took over */

        if (g_changed) {
            g_changed = false;
            Appearance now = ReadWindows(x.RootWidth());
            if (!(now == current)) {
                if (!publisher.Update(now))
                    return 0;
                current = now;
            }
        }
    }
}
