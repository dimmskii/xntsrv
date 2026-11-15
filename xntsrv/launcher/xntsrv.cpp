/*
 * xntsrv - launcher for the XNTSrv X server.
 *
 * Translates a conventional command line into the server's own options,
 * prepares X authentication, and starts xntsrv-server.exe.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#define XNTSRV_VERSION L"0.1.0"
#define DEFAULT_VSOCK_PORT 106000u
#define COOKIE_LEN 16

using std::wstring;

static const wchar_t kHelp[] =
    L"Usage: xntsrv [OPTION]... [-- SERVER-ARGS...]\n"
    L"Start the XNTSrv X server.\n"
    L"\n"
    L"  -d, --display=N         display number (default 0)\n"
    L"  -m, --mode=MODE         multiwindow (default) | desktop | fullscreen\n"
    L"  -l, --listen=LIST       comma-separated: hyperv (default), tcp, none\n"
    L"      --vmid=ID           auto (default: current user's WSL VM) | any | {GUID}\n"
    L"      --vsock-port=PORT   Hyper-V socket port (default 106000)\n"
    L"      --no-auth           disable X authentication (server's -ac)\n"
    L"      --no-clipboard      disable clipboard sharing\n"
    L"      --no-gl             disable native OpenGL\n"
    L"      --no-tray           hide the tray icon\n"
    L"      --dpi=N             override DPI\n"
    L"      --server=PATH       server binary (default: xntsrv-server.exe beside xntsrv.exe)\n"
    L"  -n, --dry-run           print the server command line and exit\n"
    L"  -v, --verbose           explain what the launcher is doing\n"
    L"  -w, --wait              wait for the server and return its exit code\n"
    L"  -h, --help              show this help\n"
    L"  -V, --version           show the version\n"
    L"\n"
    L"Anything after -- is passed to the server unchanged.\n";

struct Options {
    unsigned display = 0;
    wstring mode = L"multiwindow";
    bool listenHyperv = true;
    bool listenTcp = false;
    wstring vmid = L"auto";
    unsigned vsockPort = DEFAULT_VSOCK_PORT;
    bool auth = true;
    bool clipboard = true;
    bool gl = true;
    bool tray = true;
    unsigned dpi = 0;
    wstring server;
    bool dryRun = false;
    bool wait = false;
    bool verbose = false;
    std::vector<wstring> extra;
};

/* ---- output ------------------------------------------------------------ */

/* This is a GUI-subsystem program so that it never flashes a console window
 * when started from a shortcut.  Output goes to redirected handles when there
 * are any, otherwise to the parent's console, otherwise to a message box. */
static bool g_haveConsole = false;

static HANDLE OutputHandle(DWORD which)
{
    HANDLE h = GetStdHandle(which);
    if (h && h != INVALID_HANDLE_VALUE)
        return h;
    if (!g_haveConsole && AttachConsole(ATTACH_PARENT_PROCESS))
        g_haveConsole = true;
    if (!g_haveConsole)
        return NULL;
    return CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static void Write(DWORD which, const wstring &text)
{
    HANDLE h = OutputHandle(which);
    DWORD n, mode;

    if (!h) {
        if (which == STD_ERROR_HANDLE)
            MessageBoxW(NULL, text.c_str(), L"XNTSrv", MB_OK | MB_ICONERROR);
        return;
    }
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text.c_str(), (DWORD)text.size(), &n, NULL);
        return;
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string utf8((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], len, NULL, NULL);
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &n, NULL);
}

static void Out(const wstring &text) { Write(STD_OUTPUT_HANDLE, text); }
static void Err(const wstring &text) { Write(STD_ERROR_HANDLE, L"xntsrv: " + text + L"\n"); }

/* ---- small helpers ----------------------------------------------------- */

static bool ParseUnsigned(const wstring &s, unsigned max, unsigned *out)
{
    if (s.empty() || s.size() > 9)
        return false;
    unsigned v = 0;
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9')
            return false;
        v = v * 10 + (unsigned)(c - L'0');
    }
    if (v > max)
        return false;
    *out = v;
    return true;
}

static bool IsGuid(const wstring &s)
{
    if (s.size() != 38 || s[0] != L'{' || s[37] != L'}')
        return false;
    for (size_t i = 1; i < 37; i++) {
        wchar_t c = s[i];
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (c != L'-')
                return false;
        } else if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'))) {
            return false;
        }
    }
    return true;
}

/* Quote one argument the way CommandLineToArgvW and the C runtime parse it. */
static wstring Quote(const wstring &arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\"") == wstring::npos)
        return arg;
    wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            backslashes++;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(c);
            backslashes = 0;
            continue;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
        }
        if (c != L'\\')
            out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static bool FileExists(const wstring &path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static wstring DirName(const wstring &path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == wstring::npos ? L"." : path.substr(0, slash);
}

/* ---- option parsing ---------------------------------------------------- */

struct OptSpec {
    wchar_t shortName; /* 0 when there is none */
    const wchar_t *longName;
    bool takesValue;
};

static const OptSpec kSpecs[] = {
    { L'd', L"display", true },    { L'm', L"mode", true },        { L'l', L"listen", true },
    { 0, L"vmid", true },          { 0, L"vsock-port", true },     { 0, L"no-auth", false },
    { 0, L"no-clipboard", false }, { 0, L"no-gl", false },         { 0, L"no-tray", false },
    { 0, L"dpi", true },           { 0, L"server", true },         { L'n', L"dry-run", false },
    { L'w', L"wait", false },      { L'h', L"help", false },       { L'V', L"version", false },
    { L'v', L"verbose", false },
};

enum ParseResult { PARSE_OK, PARSE_EXIT_OK, PARSE_ERROR };

static bool ParseListen(const wstring &list, Options *o)
{
    bool hyperv = false, tcp = false, none = false, any = false;
    size_t start = 0;

    while (start <= list.size()) {
        size_t comma = list.find(L',', start);
        wstring item = list.substr(start, comma == wstring::npos ? wstring::npos : comma - start);
        if (item == L"hyperv")
            hyperv = true;
        else if (item == L"tcp")
            tcp = true;
        else if (item == L"none")
            none = true;
        else
            return false;
        any = true;
        if (comma == wstring::npos)
            break;
        start = comma + 1;
    }
    if (!any || (none && (hyperv || tcp)))
        return false;
    o->listenHyperv = hyperv;
    o->listenTcp = tcp;
    return true;
}

static ParseResult Apply(const OptSpec &spec, const wstring &value, Options *o)
{
    wstring name = spec.longName;

    if (name == L"display") {
        if (!ParseUnsigned(value, 59535, &o->display))
            goto bad;
    } else if (name == L"mode") {
        if (value != L"multiwindow" && value != L"desktop" && value != L"fullscreen")
            goto bad;
        o->mode = value;
    } else if (name == L"listen") {
        if (!ParseListen(value, o))
            goto bad;
    } else if (name == L"vmid") {
        if (value != L"auto" && value != L"any" && !IsGuid(value))
            goto bad;
        o->vmid = value;
    } else if (name == L"vsock-port") {
        if (!ParseUnsigned(value, 0x7fffffff, &o->vsockPort) || o->vsockPort == 0)
            goto bad;
    } else if (name == L"dpi") {
        if (!ParseUnsigned(value, 9999, &o->dpi) || o->dpi == 0)
            goto bad;
    } else if (name == L"server") {
        if (value.empty())
            goto bad;
        o->server = value;
    } else if (name == L"no-auth") {
        o->auth = false;
    } else if (name == L"no-clipboard") {
        o->clipboard = false;
    } else if (name == L"no-gl") {
        o->gl = false;
    } else if (name == L"no-tray") {
        o->tray = false;
    } else if (name == L"dry-run") {
        o->dryRun = true;
    } else if (name == L"wait") {
        o->wait = true;
    } else if (name == L"verbose") {
        o->verbose = true;
    } else if (name == L"help") {
        Out(kHelp);
        return PARSE_EXIT_OK;
    } else if (name == L"version") {
        Out(L"xntsrv " XNTSRV_VERSION L"\n");
        return PARSE_EXIT_OK;
    }
    return PARSE_OK;

bad:
    Err(L"invalid value for --" + name + L": '" + value + L"'");
    return PARSE_ERROR;
}

static ParseResult ParseArgs(int argc, wchar_t **argv, Options *o)
{
    for (int i = 1; i < argc; i++) {
        wstring arg = argv[i];

        if (arg == L"--") {
            for (i++; i < argc; i++)
                o->extra.push_back(argv[i]);
            break;
        }

        if (arg.size() > 2 && arg[0] == L'-' && arg[1] == L'-') {
            size_t eq = arg.find(L'=');
            wstring name = arg.substr(2, eq == wstring::npos ? wstring::npos : eq - 2);
            const OptSpec *spec = NULL;
            wstring value;

            for (const OptSpec &s : kSpecs)
                if (name == s.longName)
                    spec = &s;
            if (!spec) {
                Err(L"unrecognized option '--" + name + L"'");
                return PARSE_ERROR;
            }
            if (spec->takesValue) {
                if (eq != wstring::npos) {
                    value = arg.substr(eq + 1);
                } else if (i + 1 < argc) {
                    value = argv[++i];
                } else {
                    Err(L"option '--" + name + L"' requires an argument");
                    return PARSE_ERROR;
                }
            } else if (eq != wstring::npos) {
                Err(L"option '--" + name + L"' doesn't allow an argument");
                return PARSE_ERROR;
            }
            ParseResult r = Apply(*spec, value, o);
            if (r != PARSE_OK)
                return r;
            continue;
        }

        if (arg.size() >= 2 && arg[0] == L'-') {
            /* One or more bundled short options; a value ends the bundle. */
            for (size_t j = 1; j < arg.size(); j++) {
                const OptSpec *spec = NULL;
                wstring value;

                for (const OptSpec &s : kSpecs)
                    if (s.shortName && arg[j] == s.shortName)
                        spec = &s;
                if (!spec) {
                    Err(wstring(L"invalid option -- '") + arg[j] + L"'");
                    return PARSE_ERROR;
                }
                if (spec->takesValue) {
                    if (j + 1 < arg.size()) {
                        value = arg.substr(j + 1);
                    } else if (i + 1 < argc) {
                        value = argv[++i];
                    } else {
                        Err(wstring(L"option requires an argument -- '") + arg[j] + L"'");
                        return PARSE_ERROR;
                    }
                    j = arg.size();
                }
                ParseResult r = Apply(*spec, value, o);
                if (r != PARSE_OK)
                    return r;
            }
            continue;
        }

        Err(L"unexpected argument '" + arg + L"' (server options go after --)");
        return PARSE_ERROR;
    }
    return PARSE_OK;
}

/* ---- server location --------------------------------------------------- */

static wstring ResolveServer(const Options &o)
{
    if (!o.server.empty())
        return o.server;

    wchar_t self[MAX_PATH * 4];
    DWORD len = GetModuleFileNameW(NULL, self, ARRAYSIZE(self));
    wstring beside = DirName(wstring(self, len)) + L"\\xntsrv-server.exe";
    if (FileExists(beside))
        return beside;

    /* Until XNTSrv ships its own server build, fall back to an installed VcXsrv. */
    wchar_t dir[MAX_PATH * 2];
    DWORD size = sizeof dir;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VcXsrv", L"Install_Dir_64",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, dir, &size) == ERROR_SUCCESS) {
        wstring vcxsrv = wstring(dir) + L"\\vcxsrv.exe";
        if (FileExists(vcxsrv))
            return vcxsrv;
    }
    return beside;
}

/* ---- WSL virtual machine lookup ---------------------------------------- */

static wstring Hex(HRESULT hr)
{
    wchar_t buf[16];
    wsprintfW(buf, L"0x%08X", (unsigned)hr);
    return buf;
}

/* Ask the Host Compute Service for the running WSL utility VM.  Returns a
 * braced GUID, or an empty string when there is none or the query is refused;
 * *detail then says which, for --verbose and the warning. */
static wstring FindWslVmId(wstring *detail)
{
    typedef void *HCS_OPERATION;
    typedef HCS_OPERATION(WINAPI *CreateOperationFn)(const void *, void *);
    typedef HRESULT(WINAPI *EnumerateFn)(PCWSTR, HCS_OPERATION);
    typedef HRESULT(WINAPI *WaitFn)(HCS_OPERATION, DWORD, PWSTR *);
    typedef void(WINAPI *CloseFn)(HCS_OPERATION);

    wstring id;
    HMODULE dll = LoadLibraryExW(L"computecore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) {
        *detail = L"computecore.dll is not available (Hyper-V platform not installed?)";
        return id;
    }

    CreateOperationFn create = (CreateOperationFn)(void *)GetProcAddress(dll, "HcsCreateOperation");
    EnumerateFn enumerate = (EnumerateFn)(void *)GetProcAddress(dll, "HcsEnumerateComputeSystems");
    WaitFn wait = (WaitFn)(void *)GetProcAddress(dll, "HcsWaitForOperationResult");
    CloseFn close = (CloseFn)(void *)GetProcAddress(dll, "HcsCloseOperation");

    if (create && enumerate && wait && close) {
        HCS_OPERATION op = create(NULL, NULL);
        PWSTR doc = NULL;

        HRESULT hr = op ? enumerate(L"{\"Owners\":[\"WSL\"]}", op) : E_OUTOFMEMORY;

        if (SUCCEEDED(hr))
            hr = wait(op, 5000, &doc);
        if (FAILED(hr)) {
            *detail = L"the Host Compute Service refused the query (" + Hex(hr) + L")";
            if (hr == E_ACCESSDENIED)
                *detail += L"; listing VMs needs administrator or Hyper-V Administrators rights";
        } else if (doc) {
            *detail = wstring(L"Host Compute Service answered: ") + doc;
            /* The result is a JSON array of systems; take the first one's
             * RuntimeId (the VM's Hyper-V socket address), else its Id. */
            wstring json = doc;
            for (const wchar_t *key : { L"\"RuntimeId\":\"", L"\"Id\":\"" }) {
                size_t pos = json.find(key);
                if (pos == wstring::npos)
                    continue;
                wstring guid = L"{" + json.substr(pos + wcslen(key), 36) + L"}";
                if (IsGuid(guid)) {
                    id = guid;
                    break;
                }
            }
        }
        if (doc)
            LocalFree(doc);
        if (op)
            close(op);
    }
    else
        *detail = L"computecore.dll lacks the expected functions";
    FreeLibrary(dll);
    return id;
}

/* ---- X authentication -------------------------------------------------- */

static wstring AuthFilePath()
{
    PWSTR base = NULL;
    wstring path;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base)))
        path = wstring(base) + L"\\XNTSrv\\Xauthority";
    CoTaskMemFree(base);
    return path;
}

static void PutCounted(std::string *out, const std::string &bytes)
{
    out->push_back((char)(bytes.size() >> 8));
    out->push_back((char)(bytes.size() & 0xff));
    out->append(bytes);
}

/* Make sure an Xauthority file with one MIT-MAGIC-COOKIE-1 entry exists.  The
 * entry uses the wildcard family so the same file serves the server (-auth)
 * and any client that points XAUTHORITY at it.  An existing file is kept, so
 * clients that already hold the cookie keep working across restarts. */
static bool EnsureAuthFile(const wstring &path, unsigned display)
{
    if (FileExists(path))
        return true;

    unsigned char cookie[COOKIE_LEN];
    if (BCryptGenRandom(NULL, cookie, sizeof cookie, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return false;

    std::string entry;
    entry.append(2, '\xff'); /* FamilyWild */
    PutCounted(&entry, "");
    PutCounted(&entry, std::to_string(display));
    PutCounted(&entry, "MIT-MAGIC-COOKIE-1");
    PutCounted(&entry, std::string((const char *)cookie, sizeof cookie));

    CreateDirectoryW(DirName(path).c_str(), NULL);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_EXISTS;
    DWORD n = 0;
    bool ok = WriteFile(h, entry.data(), (DWORD)entry.size(), &n, NULL) && n == entry.size();
    CloseHandle(h);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

/* ---- main -------------------------------------------------------------- */

static std::vector<wstring> BuildServerArgs(const Options &o, const wstring &vmid, const wstring &authFile)
{
    std::vector<wstring> a;

    a.push_back(L":" + std::to_wstring(o.display));
    if (o.mode == L"multiwindow")
        a.push_back(L"-multiwindow");
    else if (o.mode == L"fullscreen")
        a.push_back(L"-fullscreen");

    a.push_back(o.listenHyperv ? L"-listen" : L"-nolisten");
    a.push_back(L"hyperv");
    a.push_back(o.listenTcp ? L"-listen" : L"-nolisten");
    a.push_back(L"tcp");
    if (o.listenHyperv) {
        if (!vmid.empty()) {
            a.push_back(L"-vmid");
            a.push_back(vmid);
        }
        a.push_back(L"-vsockport");
        a.push_back(std::to_wstring(o.vsockPort));
    }

    if (o.auth) {
        a.push_back(L"-auth");
        a.push_back(authFile);
    } else {
        a.push_back(L"-ac");
    }
    a.push_back(o.clipboard ? L"-clipboard" : L"-noclipboard");
    a.push_back(o.gl ? L"-wgl" : L"-nowgl");
    if (!o.tray)
        a.push_back(L"-notrayicon");
    if (o.dpi) {
        a.push_back(L"-dpi");
        a.push_back(std::to_wstring(o.dpi));
    }
    a.insert(a.end(), o.extra.begin(), o.extra.end());
    return a;
}

static int Run(int argc, wchar_t **argv)
{
    Options o;

    switch (ParseArgs(argc, argv, &o)) {
    case PARSE_EXIT_OK:
        return 0;
    case PARSE_ERROR:
        Write(STD_ERROR_HANDLE, L"Try 'xntsrv --help' for more information.\n");
        return 2;
    default:
        break;
    }

    /* Resolve which VM may connect.  "any" leaves the server on its wildcard. */
    wstring vmid;
    if (o.listenHyperv && o.vmid != L"any") {
        if (o.vmid == L"auto") {
            wstring detail;
            vmid = FindWslVmId(&detail);
            if (o.verbose)
                Err(detail);
            if (vmid.empty())
                Err(L"warning: could not identify the WSL VM; accepting Hyper-V connections from any VM"
                    L" (use --verbose for the reason)");
        } else {
            vmid = o.vmid;
        }
    }

    wstring authFile;
    if (o.auth) {
        authFile = AuthFilePath();
        if (authFile.empty()) {
            Err(L"cannot locate the local application data folder");
            return 1;
        }
        if (!o.dryRun && !EnsureAuthFile(authFile, o.display)) {
            Err(L"cannot create " + authFile);
            return 1;
        }
    }

    wstring server = ResolveServer(o);
    wstring cmdline = Quote(server);
    for (const wstring &arg : BuildServerArgs(o, vmid, authFile))
        cmdline += L" " + Quote(arg);

    if (o.dryRun) {
        Out(cmdline + L"\n");
        return 0;
    }

    if (!FileExists(server)) {
        Err(L"server not found: " + server +
            L"\nInstall VcXsrv, or point --server at vcxsrv.exe, until XNTSrv ships its own server.");
        return 1;
    }

    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi;
    si.cb = sizeof si;
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(server.c_str(), buf.data(), NULL, NULL, FALSE, 0, NULL,
                        DirName(server).c_str(), &si, &pi)) {
        Err(L"cannot start " + server + L" (error " + std::to_wstring(GetLastError()) + L")");
        return 1;
    }

    DWORD code = 0;
    if (o.wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

/* The same source builds two programs: xntsrv.exe (console subsystem, for
 * terminals and scripts, so the shell waits and output appears in order) and
 * xntsrvw.exe (windows subsystem, for shortcuts and autostart, so no console
 * window ever flashes).  The linker picks the matching entry point. */
int wmain(int argc, wchar_t **argv)
{
    return Run(argc, argv);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    return argv ? Run(argc, argv) : 1;
}
