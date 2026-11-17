/*
 * xntsrv - launcher for the XNTsrv X server.
 *
 * Translates a conventional command line into the server's own options,
 * prepares X authentication, and starts xntsrv-server.exe, the appearance
 * helper and, when it is installed, the audio server.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

#include <winsock2.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#define COOKIE_LEN 16
#define PULSE_COOKIE_LEN 256 /* PulseAudio's native protocol cookie */

using std::wstring;

static const wchar_t kHelp[] =
    L"Usage: xntsrv [OPTION]... [-- SERVER-ARGS...]\n"
    L"Start the XNTsrv X server.\n"
    L"\n"
    L"  -d, --display=N         display number (default 0)\n"
    L"  -m, --mode=MODE         multiwindow (default) | desktop | fullscreen\n"
    L"      --no-auth           disable X authentication (server's -ac)\n"
    L"      --no-clipboard      disable clipboard sharing\n"
    L"      --no-gl             disable native OpenGL\n"
    L"      --no-tray           hide the tray icon\n"
    L"      --no-appearance     do not pass the Windows display scale, font, pointer size\n"
    L"                          and light or dark preference on to X programs\n"
    L"  -A, --no-audio          do not start the audio server for X programs\n"
    L"      --dpi=N             override DPI\n"
    L"      --server=PATH       server binary (default: xntsrv-server.exe beside xntsrv.exe)\n"
    L"  -n, --dry-run           print the server command line and exit\n"
    L"  -w, --wait              wait for the server and return its exit code\n"
    L"  -h, --help              show this help\n"
    L"  -V, --version           show the version\n"
    L"\n"
    L"Anything after -- is passed to the server unchanged.\n"
    L"\n"
    L"The server listens on this computer only (TCP on 127.0.0.1). Connections from\n"
    L"WSL and other virtual machines are carried to it by the XNTsrv services.\n";

struct Options {
    unsigned display = 0;
    wstring mode = L"multiwindow";
    bool auth = true;
    bool clipboard = true;
    bool gl = true;
    bool tray = true;
    bool appearance = true;
    bool audio = true;
    unsigned dpi = 0;
    wstring server;
    bool dryRun = false;
    bool wait = false;
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
            MessageBoxW(NULL, text.c_str(), L"XNTsrv", MB_OK | MB_ICONERROR);
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
    { L'd', L"display", true },    { L'm', L"mode", true },        { 0, L"no-auth", false },
    { 0, L"no-clipboard", false }, { 0, L"no-gl", false },         { 0, L"no-tray", false },
    { 0, L"no-appearance", false }, { L'A', L"no-audio", false },
    { 0, L"dpi", true },           { 0, L"server", true },         { L'n', L"dry-run", false },
    { L'w', L"wait", false },      { L'h', L"help", false },       { L'V', L"version", false },
};

enum ParseResult { PARSE_OK, PARSE_EXIT_OK, PARSE_ERROR };

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
    } else if (name == L"no-appearance") {
        o->appearance = false;
    } else if (name == L"no-audio") {
        o->audio = false;
    } else if (name == L"dry-run") {
        o->dryRun = true;
    } else if (name == L"wait") {
        o->wait = true;
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
    return DirName(wstring(self, len)) + L"\\xntsrv-server.exe";
}

/* ---- X authentication -------------------------------------------------- */

/* %LOCALAPPDATA%\XNTsrv, where each user's cookies and generated files live;
 * empty if it cannot be found. */
static wstring UserDataDir()
{
    PWSTR base = NULL;
    wstring path;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base)))
        path = wstring(base) + L"\\" + xnt::kUserDataDir;
    CoTaskMemFree(base);
    return path;
}

static wstring AuthFilePath()
{
    wstring dir = UserDataDir();
    return dir.empty() ? dir : dir + L"\\Xauthority";
}

/* Write `bytes` to a new file; false if it cannot.  An existing file counts
 * as success and is left alone. */
static bool WriteNewFile(const wstring &path, const std::string &bytes)
{
    CreateDirectoryW(DirName(path).c_str(), NULL);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_EXISTS;
    DWORD n = 0;
    bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &n, NULL) && n == bytes.size();
    CloseHandle(h);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

static bool RandomBytes(size_t count, std::string *out)
{
    out->assign(count, '\0');
    return BCryptGenRandom(NULL, (PUCHAR)&(*out)[0], (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
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

    std::string cookie;
    if (!RandomBytes(COOKIE_LEN, &cookie))
        return false;

    std::string entry;
    entry.append(2, '\xff'); /* FamilyWild */
    PutCounted(&entry, "");
    PutCounted(&entry, std::to_string(display));
    PutCounted(&entry, "MIT-MAGIC-COOKIE-1");
    PutCounted(&entry, cookie);
    return WriteNewFile(path, entry);
}

/* ---- audio ------------------------------------------------------------- */

/* The audio server is PulseAudio, installed as an optional component in
 * audio\ beside this program.  It listens on 127.0.0.1 only, behind a cookie
 * kept beside the X cookie, and the XNTsrv services carry guests' connections
 * to it as they do for X.  It runs in a job object that only the X server
 * holds a handle to, so it ends when the server does.
 *
 * In order: -A turns it off; an audio server already listening on its port
 * (another XNTsrv's, or the user's own) is left to serve; without the
 * component there is no audio; otherwise it is started. */
struct AudioPlan {
    wstring exe;     /* empty: do not start one */
    wstring command; /* its command line */
    wstring script;  /* the startup script to write first */
    wstring scriptPath;
    wstring cookiePath;
    wstring stateDir;
};

static wstring AudioDir() { return xnt::ProgramDir() + L"\\audio"; }

static bool PortListening(unsigned port)
{
    ULONG size = 0;
    std::vector<BYTE> buf;
    DWORD rc = ERROR_INSUFFICIENT_BUFFER;

    while (rc == ERROR_INSUFFICIENT_BUFFER) {
        buf.resize(size ? size : 1);
        rc = GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_BASIC_LISTENER, 0);
    }
    if (rc != NO_ERROR)
        return false;
    const MIB_TCPTABLE *table = (const MIB_TCPTABLE *)buf.data();
    for (DWORD i = 0; i < table->dwNumEntries; i++) {
        const MIB_TCPROW &row = table->table[i];
        DWORD addr = ntohl(row.dwLocalAddr);
        if (ntohs((u_short)row.dwLocalPort) == port && (addr == INADDR_ANY || addr == INADDR_LOOPBACK))
            return true;
    }
    return false;
}

/* A value in a PulseAudio module argument: single-quoted, with backslashes
 * and quotes escaped, since its parser treats a backslash as an escape. */
static wstring ModArg(const wstring &value)
{
    wstring out = L"'";
    for (wchar_t c : value) {
        if (c == L'\\' || c == L'\'')
            out.push_back(L'\\');
        out.push_back(c);
    }
    return out + L"'";
}

static AudioPlan PlanAudio(const Options &o)
{
    AudioPlan p; /* p.exe stays empty when there is to be no audio server */
    wstring exe = AudioDir() + L"\\bin\\pulseaudio.exe";
    wstring data = UserDataDir();

    if (!o.audio || !FileExists(exe) || data.empty() || PortListening(xnt::kAudioPort))
        return p;

    p.exe = exe;
    p.stateDir = data + L"\\audio";
    p.cookiePath = data + L"\\pulse-cookie";
    p.scriptPath = p.stateDir + L"\\xntsrv.pa";
    /* Windows' default playback and recording devices, each on its own so
     * that a computer without a microphone still plays (one module opening
     * both fails when either is missing); a silent output on a computer with
     * no sound device at all, since the server would not start otherwise; and
     * the native protocol on loopback only. */
    p.script = L"# Written by xntsrv.exe at every start; changes are lost.\n"
               L".nofail\n"
               L"load-module module-waveout sink_name=windows record=0\n"
               L"load-module module-waveout source_name=windows_input playback=0\n"
               L".fail\n"
               L"load-module module-always-sink\n"
               L"load-module module-native-protocol-tcp listen=127.0.0.1 port=" +
               std::to_wstring(xnt::kAudioPort) + L" auth-anonymous=0 auth-cookie=" + ModArg(p.cookiePath) + L"\n";

    std::vector<wstring> args = {
        L"-n",                                /* none of the default modules: only the script's */
        L"--file=" + p.scriptPath,
        L"--dl-search-path=" + AudioDir() + L"\\bin",
        L"--exit-idle-time=-1",               /* it lives as long as the X server */
        L"--disallow-exit",
        L"--use-pid-file=no",
        L"--log-target=newfile:" + p.stateDir + L"\\pulseaudio.log",
    };
    p.command = Quote(exe);
    for (const wstring &a : args)
        p.command += L" " + Quote(a);
    return p;
}

/* Write the cookie and the startup script.  The cookie, like the X one, is
 * kept, so guests that hold it keep working across restarts. */
static bool PrepareAudio(const AudioPlan &p)
{
    std::string cookie;
    if (!FileExists(p.cookiePath) && (!RandomBytes(PULSE_COOKIE_LEN, &cookie) || !WriteNewFile(p.cookiePath, cookie)))
        return false;

    CreateDirectoryW(DirName(p.stateDir).c_str(), NULL);
    CreateDirectoryW(p.stateDir.c_str(), NULL);
    int len = WideCharToMultiByte(CP_UTF8, 0, p.script.c_str(), (int)p.script.size(), NULL, 0, NULL, NULL);
    std::string utf8((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, p.script.c_str(), (int)p.script.size(), &utf8[0], len, NULL, NULL);
    HANDLE h = CreateFileW(p.scriptPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD n = 0;
    bool ok = WriteFile(h, utf8.data(), (DWORD)utf8.size(), &n, NULL) && n == utf8.size();
    CloseHandle(h);
    return ok;
}

/* A job that ends every process in it once its last handle closes. */
static HANDLE CreateKillOnCloseJob()
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE }; /* inheritable, for the X server */
    HANDLE job = CreateJobObjectW(&sa, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};

    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        CloseHandle(job);
        job = NULL;
    }
    return job;
}

/* Start the audio server in `job`, hidden.  Its state stays in the user's
 * XNTsrv folder rather than in their profile. */
static bool StartAudio(const AudioPlan &p, HANDLE job)
{
    SetEnvironmentVariableW(L"PULSE_RUNTIME_PATH", p.stateDir.c_str());
    SetEnvironmentVariableW(L"PULSE_STATE_PATH", p.stateDir.c_str());

    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi;
    si.cb = sizeof si;
    wstring command = p.command;
    if (!CreateProcessW(p.exe.c_str(), &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL,
                        DirName(p.exe).c_str(), &si, &pi))
        return false;
    bool ok = AssignProcessToJobObject(job, pi.hProcess) != 0;
    if (ok)
        ResumeThread(pi.hThread);
    else
        TerminateProcess(pi.hProcess, 1); /* it would outlive the server */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ok;
}

/* ---- main -------------------------------------------------------------- */

static std::vector<wstring> BuildServerArgs(const Options &o, const wstring &authFile)
{
    std::vector<wstring> a;

    a.push_back(L":" + std::to_wstring(o.display));
    if (o.mode == L"multiwindow")
        a.push_back(L"-multiwindow");
    else if (o.mode == L"fullscreen")
        a.push_back(L"-fullscreen");

    /* TCP is how the server's own window manager and clipboard reach it, and
     * how the XNTsrv services hand it connections; nothing else is opened. */
    a.push_back(L"-listen");
    a.push_back(L"tcp");
    a.push_back(L"-nolisten");
    a.push_back(L"hyperv");

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
    for (const wstring &arg : BuildServerArgs(o, authFile))
        cmdline += L" " + Quote(arg);

    AudioPlan audio = PlanAudio(o);

    if (o.dryRun) {
        Out(cmdline + L"\n");
        if (!audio.exe.empty())
            Out(audio.command + L"\n");
        return 0;
    }

    if (!FileExists(server)) {
        Err(L"server not found: " + server);
        return 1;
    }

    /* The audio server's job.  Only the X server inherits a handle to it, so
     * once this launcher has let go of its own, the job and the audio server
     * end with the X server.  Without a job there is no audio: it would
     * outlive the server. */
    HANDLE job = NULL;
    if (!audio.exe.empty() && PrepareAudio(audio))
        job = CreateKillOnCloseJob();

    STARTUPINFOEXW si = {};
    PROCESS_INFORMATION pi;
    si.StartupInfo.cb = sizeof si;
    std::vector<BYTE> attributes;
    DWORD flags = 0;
    if (job) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &size);
        attributes.resize(size);
        si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
        if (InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) &&
            UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &job, sizeof job,
                                      NULL, NULL)) {
            flags = EXTENDED_STARTUPINFO_PRESENT;
        } else {
            CloseHandle(job);
            job = NULL;
        }
    }
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    BOOL started = CreateProcessW(server.c_str(), buf.data(), NULL, NULL, job != NULL, flags, NULL,
                                  DirName(server).c_str(), &si.StartupInfo, &pi);
    DWORD error = GetLastError();
    if (si.lpAttributeList)
        DeleteProcThreadAttributeList(si.lpAttributeList);
    if (!started) {
        if (job)
            CloseHandle(job);
        Err(L"cannot start " + server + L" (error " + std::to_wstring(error) + L")");
        return 1;
    }

    /* If it fails, X programs simply get no sound; the server is unaffected. */
    if (job) {
        StartAudio(audio, job);
        CloseHandle(job);
    }

    /* The helper that passes Windows appearance settings on to X programs.
     * It waits for the server to come up and ends when the server does, so it
     * needs no supervision; if it is missing or fails, the server is unaffected. */
    wstring helper = xnt::ProgramDir() + L"\\xntsrv-appearance.exe";
    if (o.appearance && FileExists(helper)) {
        wstring command = Quote(helper) + L" --display=" + std::to_wstring(o.display);
        STARTUPINFOW hsi = {};
        PROCESS_INFORMATION hpi;
        hsi.cb = sizeof hsi;
        if (CreateProcessW(helper.c_str(), &command[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &hsi, &hpi)) {
            CloseHandle(hpi.hThread);
            CloseHandle(hpi.hProcess);
        }
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
