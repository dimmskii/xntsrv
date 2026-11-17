/*
 * xntsrv-service - carries connections to the XNTsrv X server.
 *
 * The X server itself only listens on 127.0.0.1.  Each way of reaching it from
 * outside is a separate Windows service running this program in a different
 * mode, so each can be started, stopped and configured on its own in
 * services.msc.  A service accepts connections on its own kind of socket and
 * relays the bytes to the server; it does not interpret them, so X
 * authentication still happens between the client and the server.
 *
 * Each route carries the channels in xnt::kChannels: X, and the audio server
 * started with it.  The TCP route carries audio only when HKLM\SOFTWARE\
 * XNTsrv\TCP\Audio is 1, since the microphone is behind it.
 *
 *   wsl      Listens on the Hyper-V socket of the running WSL virtual machine.
 *            Finding that VM needs administrator rights, which is why this is
 *            a service.  It follows WSL restarts by listening on the new VM.
 *   tcp      Listens on this computer's network addresses (never loopback) and
 *            follows them as adapters come and go.  Limited to the addresses
 *            or adapter names in HKLM\SOFTWARE\XNTsrv\TCP\Listen when set.
 *   vmware   Listens on the VMware (VMCI) socket, which Linux guests under
 *            VMware Workstation or Player reach as the same vsock address the
 *            WSL bridge uses.  Idle when VMware is not installed.
 *   session  Starts the X server (xntsrvw.exe) for each user when they log in,
 *            and for users already logged in when the service starts.  The
 *            server runs as that user, in their session, not elevated.
 *
 * Installed and removed by xntsrv-register and xntsrv-unregister.
 *
 * For troubleshooting, "xntsrv-service MODE --console" runs a mode in the
 * foreground as the current user, saying what it does, until Ctrl+C.
 *
 * SPDX-License-Identifier: MIT
 */
#define FD_SETSIZE 128
#include "../common/xntsrv-common.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <userenv.h>
#include <wtsapi32.h>

#include <vector>

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif
#define HV_PROTOCOL_RAW 1

struct SockaddrHv {
    ADDRESS_FAMILY Family;
    USHORT Reserved;
    GUID VmId;
    GUID ServiceId;
};

static const xnt::ServiceInfo *g_service;
static SERVICE_STATUS_HANDLE g_status;
static HANDLE g_stop;

static bool g_console; /* running in the foreground: say what happens */

static bool Stopping() { return WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0; }

static void Log(const std::wstring &text)
{
    if (g_console)
        xnt::ConsoleWrite(STD_OUTPUT_HANDLE, text + L"\n");
}

/* ---- relay ------------------------------------------------------------- */

static bool SendAll(SOCKET s, const char *buf, int len)
{
    while (len > 0) {
        int n = send(s, buf, len, 0);
        if (n <= 0)
            return false;
        buf += n;
        len -= n;
    }
    return true;
}

/* Copy both directions between a and b until both sides have closed. */
static void Relay(SOCKET a, SOCKET b)
{
    static const int kBuf = 65536;
    char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, kBuf);
    bool open[2] = { true, true }; /* still reading from a, from b */

    while (buf && (open[0] || open[1])) {
        fd_set readable;
        FD_ZERO(&readable);
        if (open[0])
            FD_SET(a, &readable);
        if (open[1])
            FD_SET(b, &readable);
        if (select(0, &readable, NULL, NULL, NULL) <= 0)
            break;

        for (int i = 0; i < 2; i++) {
            SOCKET from = i ? b : a, to = i ? a : b;
            if (!open[i] || !FD_ISSET(from, &readable))
                continue;
            int n = recv(from, buf, kBuf, 0);
            if (n <= 0) {
                shutdown(to, SD_SEND); /* pass the end-of-stream on */
                open[i] = false;
            } else if (!SendAll(to, buf, n)) {
                open[0] = open[1] = false;
            }
        }
    }
    if (buf)
        HeapFree(GetProcessHeap(), 0, buf);
}

/* Send each write at once rather than holding small ones back (Nagle's
 * algorithm).  X sends many small messages: held back, an event such as an
 * Expose waits for the other side's delayed acknowledgement, 40 ms on Linux,
 * and every menu or popup feels it.  Only TCP has this; the Hyper-V and
 * VMware sockets do not. */
static void SendAtOnce(SOCKET s)
{
    sockaddr_storage addr = {};
    int len = sizeof addr;
    if (getsockname(s, (sockaddr *)&addr, &len) != 0 || (addr.ss_family != AF_INET && addr.ss_family != AF_INET6))
        return;
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
}

/* A listening socket and the channel its connections belong to. */
struct Listener {
    SOCKET socket;
    const xnt::Channel *channel;
};

struct Accepted {
    SOCKET client;
    const xnt::Channel *channel;
};

/* Thread: connect an accepted client to its channel's local port and relay. */
static DWORD WINAPI Connection(LPVOID param)
{
    Accepted *accepted = (Accepted *)param;
    SOCKET client = accepted->client;
    const xnt::Channel &channel = *accepted->channel;
    delete accepted;
    SendAtOnce(client);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)channel.localPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::wstring where = std::wstring(channel.name) + L" (127.0.0.1:" + std::to_wstring(channel.localPort) + L")";

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server != INVALID_SOCKET && connect(server, (sockaddr *)&addr, sizeof addr) == 0) {
        SendAtOnce(server);
        Log(L"connection accepted, relaying to " + where);
        Relay(client, server);
        Log(L"connection to " + where + L" closed");
    } else {
        Log(L"connection accepted, but nothing answers for " + where);
    }
    /* If no server is running, the client simply sees the connection close. */
    if (server != INVALID_SOCKET)
        closesocket(server);
    closesocket(client);
    return 0;
}

/* Accept on the given listeners for a few seconds, handing each connection to
 * its own thread.  Returns false if a listener failed and should be rebuilt. */
static bool AcceptFor(const std::vector<Listener> &listeners, DWORD milliseconds)
{
    ULONGLONG until = GetTickCount64() + milliseconds;

    while (!Stopping() && GetTickCount64() < until) {
        fd_set readable;
        timeval wait = { 0, 500000 };
        FD_ZERO(&readable);
        for (const Listener &l : listeners)
            FD_SET(l.socket, &readable);
        if (select(0, &readable, NULL, NULL, &wait) <= 0)
            continue;
        for (const Listener &l : listeners) {
            if (!FD_ISSET(l.socket, &readable))
                continue;
            SOCKET client = accept(l.socket, NULL, NULL);
            if (client == INVALID_SOCKET)
                return false;
            Accepted *accepted = new Accepted{ client, l.channel };
            HANDLE thread = CreateThread(NULL, 0, Connection, accepted, 0, NULL);
            if (thread) {
                CloseHandle(thread);
            } else {
                delete accepted;
                closesocket(client);
            }
        }
    }
    return true;
}

static void CloseAll(std::vector<Listener> *listeners)
{
    for (const Listener &l : *listeners)
        closesocket(l.socket);
    listeners->clear();
}

/* ---- wsl mode ---------------------------------------------------------- */

static SOCKET ListenOnVm(const std::wstring &vmId, unsigned vsockPort)
{
    SockaddrHv addr = {};
    addr.Family = AF_HYPERV;
    /* The Linux side addresses a Hyper-V socket by vsock port; Windows sees
     * that port as the first field of this well-known service ID template. */
    if (FAILED(CLSIDFromString(xnt::kHvSocketTemplate, &addr.ServiceId)) ||
        FAILED(CLSIDFromString(vmId.c_str(), &addr.VmId)))
        return INVALID_SOCKET;
    addr.ServiceId.Data1 = vsockPort;

    SOCKET s = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET)
        return s;
    if (bind(s, (sockaddr *)&addr, sizeof addr) != 0 || listen(s, SOMAXCONN) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* Listen on every channel's port of one VM.  A channel that fails is
 * logged and left out; X is the one that matters. */
static std::vector<Listener> ListenOnVmChannels(const std::wstring &vmId)
{
    std::vector<Listener> listeners;
    for (const xnt::Channel &channel : xnt::kChannels) {
        SOCKET s = ListenOnVm(vmId, channel.vsockPort);
        if (s != INVALID_SOCKET)
            listeners.push_back({ s, &channel });
        else
            Log(std::wstring(L"cannot listen for ") + channel.name + L" on Hyper-V socket port " +
                std::to_wstring(channel.vsockPort) + L" of " + vmId);
    }
    return listeners;
}

static void ServeWsl()
{
    std::wstring boundVm;
    std::vector<Listener> listeners;

    while (!Stopping()) {
        /* The VM's ID changes whenever WSL restarts, so keep checking and
         * move the listeners when it does. */
        std::wstring detail;
        std::wstring vm = xnt::FindWslVmId(&detail);
        if (vm != boundVm || (listeners.empty() && !vm.empty())) {
            CloseAll(&listeners);
            if (!vm.empty())
                listeners = ListenOnVmChannels(vm);
            boundVm = listeners.empty() ? L"" : vm;
        }

        if (listeners.empty()) {
            WaitForSingleObject(g_stop, 3000);
        } else if (!AcceptFor(listeners, 3000)) {
            CloseAll(&listeners);
            boundVm.clear();
        }
    }
    CloseAll(&listeners);
}

/* ---- vmware mode ------------------------------------------------------- */

/* VMware's socket family has no fixed number: its driver, installed with
 * Workstation or Player, hands it out through a device.  Returns -1 when the
 * driver is not there.  The device name, control code and address layout are
 * VMware's published interface (vmci_sockets.h). */
static int VmciAddressFamily()
{
    const DWORD kGetAfValue = 0x81032068;
    unsigned value = (unsigned)-1;
    HANDLE device = CreateFileW(L"\\\\.\\VMCI", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (device == INVALID_HANDLE_VALUE)
        return -1;
    OVERLAPPED ov = {};
    DWORD n = 0;
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!DeviceIoControl(device, kGetAfValue, &value, sizeof value, &value, sizeof value, &n, &ov) &&
        (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(device, &ov, &n, TRUE)))
        value = (unsigned)-1;
    if (ov.hEvent)
        CloseHandle(ov.hEvent);
    CloseHandle(device);
    return (int)value;
}

struct SockaddrVm {
    unsigned short family;
    unsigned short reserved;
    unsigned int port;
    unsigned int cid;
    unsigned char zero[4];
};

static SOCKET ListenOnVmci(int family, unsigned port)
{
    SockaddrVm addr = {};
    addr.family = (unsigned short)family;
    addr.port = port;
    addr.cid = (unsigned int)-1; /* any: guests address the host as CID 2 */

    SOCKET s = socket(family, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        Log(L"cannot create a VMware socket (family " + std::to_wstring(family) + L", error " +
            std::to_wstring(WSAGetLastError()) + L")");
        return s;
    }
    if (bind(s, (sockaddr *)&addr, sizeof addr) != 0 || listen(s, SOMAXCONN) != 0) {
        Log(L"cannot listen on VMware socket port " + std::to_wstring(port) + L" (error " +
            std::to_wstring(WSAGetLastError()) + L")");
        closesocket(s);
        return INVALID_SOCKET;
    }
    Log(L"listening on VMware socket port " + std::to_wstring(port) + L" (family " + std::to_wstring(family) + L")");
    return s;
}

static void ServeVmware()
{
    std::vector<Listener> listeners;

    while (!Stopping()) {
        if (listeners.empty()) {
            int family = VmciAddressFamily();
            if (family < 0)
                Log(L"VMware's socket driver is not available on this computer");
            else
                for (const xnt::Channel &channel : xnt::kChannels) {
                    SOCKET s = ListenOnVmci(family, channel.vsockPort);
                    if (s != INVALID_SOCKET)
                        listeners.push_back({ s, &channel });
                }
        }
        if (listeners.empty()) {
            /* No VMware driver, or it is not ready yet: look again later. */
            WaitForSingleObject(g_stop, 5000);
        } else if (!AcceptFor(listeners, 3000)) {
            CloseAll(&listeners);
        }
    }
    CloseAll(&listeners);
}

/* ---- tcp mode ---------------------------------------------------------- */

struct TcpListener {
    std::wstring address; /* as text, the key for matching */
    Listener listener;
};

/* Audio goes over the network only when the administrator asked for it. */
static bool TcpCarriesAudio()
{
    DWORD value = 0, size = sizeof value;
    return RegGetValueW(HKEY_LOCAL_MACHINE, xnt::kTcpKey, L"Audio", RRF_RT_REG_DWORD, NULL, &value, &size) ==
               ERROR_SUCCESS &&
           value == 1;
}

/* The optional allow-list: addresses or adapter names, one per line of a
 * REG_MULTI_SZ.  Empty means every network address of this computer. */
static std::vector<std::wstring> ReadListenFilter()
{
    std::vector<std::wstring> entries;
    wchar_t buf[4096];
    DWORD size = sizeof buf - 2 * sizeof(wchar_t);

    ZeroMemory(buf, sizeof buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, xnt::kTcpKey, L"Listen", RRF_RT_REG_MULTI_SZ, NULL, buf, &size) == ERROR_SUCCESS)
        for (const wchar_t *p = buf; *p; p += lstrlenW(p) + 1)
            entries.push_back(p);
    return entries;
}

static bool Listed(const std::vector<std::wstring> &filter, const std::wstring &value)
{
    for (const std::wstring &entry : filter)
        if (lstrcmpiW(entry.c_str(), value.c_str()) == 0)
            return true;
    return false;
}

/* Bring `listeners` in line with the addresses this computer has right now. */
static void SyncTcpListeners(std::vector<TcpListener> *listeners)
{
    std::vector<std::wstring> filter = ReadListenFilter();
    std::vector<const xnt::Channel *> channels = { &xnt::kXChannel };
    if (TcpCarriesAudio())
        channels.push_back(&xnt::kAudioChannel);
    std::vector<std::wstring> present;
    ULONG size = 64 * 1024;
    std::vector<BYTE> buf(size);
    IP_ADAPTER_ADDRESSES *adapters = (IP_ADAPTER_ADDRESSES *)buf.data();
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapters, &size) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        adapters = (IP_ADAPTER_ADDRESSES *)buf.data();
        if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapters, &size) != NO_ERROR)
            adapters = NULL;
    }

    for (IP_ADAPTER_ADDRESSES *a = adapters; a; a = a->Next) {
        /* Loopback belongs to the X server itself. */
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
            sockaddr_storage addr = {};
            int len = u->Address.iSockaddrLength;
            wchar_t text[INET6_ADDRSTRLEN] = L"";

            if (len > (int)sizeof addr)
                continue;
            CopyMemory(&addr, u->Address.lpSockaddr, (size_t)len);
            if (addr.ss_family == AF_INET) {
                InetNtopW(AF_INET, &((sockaddr_in *)&addr)->sin_addr, text, ARRAYSIZE(text));
            } else if (addr.ss_family == AF_INET6) {
                sockaddr_in6 *in6 = (sockaddr_in6 *)&addr;
                if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr))
                    continue;
                InetNtopW(AF_INET6, &in6->sin6_addr, text, ARRAYSIZE(text));
            } else {
                continue;
            }
            if (!filter.empty() && !Listed(filter, text) && !Listed(filter, a->FriendlyName))
                continue;

            for (const xnt::Channel *channel : channels) {
                /* One listener per address and channel, keyed by both. */
                std::wstring key = std::wstring(text) + L" " + channel->name;
                present.push_back(key);
                if (listeners->size() >= FD_SETSIZE - 1)
                    continue;

                bool have = false;
                for (const TcpListener &l : *listeners)
                    have = have || l.address == key;
                if (have)
                    continue;

                u_short port = htons((u_short)channel->localPort);
                if (addr.ss_family == AF_INET)
                    ((sockaddr_in *)&addr)->sin_port = port;
                else
                    ((sockaddr_in6 *)&addr)->sin6_port = port;

                /* The stock X server holds its port on every address with
                 * SO_REUSEADDR set; binding one specific address the same way
                 * coexists with it and takes that address's connections. */
                SOCKET s = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
                int on = 1;
                if (s == INVALID_SOCKET)
                    continue;
                setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
                if (addr.ss_family == AF_INET6)
                    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&on, sizeof on);
                if (bind(s, (sockaddr *)&addr, len) == 0 && listen(s, SOMAXCONN) == 0)
                    listeners->push_back({ key, { s, channel } });
                else
                    closesocket(s);
            }
        }
    }

    /* Drop listeners whose address has gone or is no longer allowed. */
    for (size_t i = listeners->size(); i-- > 0;) {
        bool keep = false;
        for (const std::wstring &p : present)
            keep = keep || p == (*listeners)[i].address;
        if (!keep) {
            closesocket((*listeners)[i].listener.socket);
            listeners->erase(listeners->begin() + (ptrdiff_t)i);
        }
    }
}

static void ServeTcp()
{
    std::vector<TcpListener> listeners;

    while (!Stopping()) {
        SyncTcpListeners(&listeners);

        std::vector<Listener> sockets;
        for (const TcpListener &l : listeners)
            sockets.push_back(l.listener);

        if (sockets.empty()) {
            WaitForSingleObject(g_stop, 3000);
        } else if (!AcceptFor(sockets, 3000)) {
            for (const TcpListener &l : listeners)
                closesocket(l.listener.socket);
            listeners.clear();
        }
    }
    for (const TcpListener &l : listeners)
        closesocket(l.listener.socket);
}

/* ---- session mode ------------------------------------------------------ */

/* Start the X server in one login session, as that session's user. */
static DWORD WINAPI StartServerInSession(LPVOID param)
{
    DWORD session = (DWORD)(UINT_PTR)param;
    HANDLE token = NULL;

    /* Fails for sessions nobody is logged in to, such as the services' own. */
    if (!WTSQueryUserToken(session, &token)) {
        Log(L"session " + std::to_wstring(session) + L": no logged-in user (error " +
            std::to_wstring(GetLastError()) + L")");
        return 0;
    }

    std::wstring dir = xnt::ProgramDir();
    std::wstring exe = dir + L"\\xntsrvw.exe";
    std::wstring command = L"\"" + exe + L"\"";
    wchar_t desktop[] = L"winsta0\\default";
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    void *environment = NULL;

    si.cb = sizeof si;
    si.lpDesktop = desktop;
    /* The user's own environment, so the server finds their profile folders. */
    CreateEnvironmentBlock(&environment, token, FALSE);

    /* For an administrator this is the ordinary, unelevated token. */
    if (CreateProcessAsUserW(token, exe.c_str(), &command[0], NULL, NULL, FALSE,
                             CREATE_UNICODE_ENVIRONMENT, environment, dir.c_str(), &si, &pi)) {
        Log(L"session " + std::to_wstring(session) + L": started " + exe);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        Log(L"session " + std::to_wstring(session) + L": cannot start " + exe + L" (error " +
            std::to_wstring(GetLastError()) + L")");
    }
    if (environment)
        DestroyEnvironmentBlock(environment);
    CloseHandle(token);
    return 0;
}

static void ServeSession()
{
    /* Users who are already logged in.  Later logins arrive through the
     * service control handler. */
    WTS_SESSION_INFOW *sessions = NULL;
    DWORD count = 0;

    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        for (DWORD i = 0; i < count; i++)
            StartServerInSession((LPVOID)(UINT_PTR)sessions[i].SessionId);
        WTSFreeMemory(sessions);
    }
    WaitForSingleObject(g_stop, INFINITE);
}

/* ---- service plumbing -------------------------------------------------- */

static void Report(DWORD state)
{
    SERVICE_STATUS s = {};
    s.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s.dwCurrentState = state;
    s.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    if (state == SERVICE_RUNNING && lstrcmpW(g_service->mode, L"session") == 0)
        s.dwControlsAccepted |= SERVICE_ACCEPT_SESSIONCHANGE;
    SetServiceStatus(g_status, &s);
}

static DWORD WINAPI Handler(DWORD control, DWORD eventType, LPVOID eventData, LPVOID)
{
    switch (control) {
    case SERVICE_CONTROL_SESSIONCHANGE:
        /* Only the session service asks for these.  The work is handed to
         * another thread so that this handler returns at once. */
        if (eventType == WTS_SESSION_LOGON && eventData) {
            DWORD session = ((WTSSESSION_NOTIFICATION *)eventData)->dwSessionId;
            QueueUserWorkItem(StartServerInSession, (LPVOID)(UINT_PTR)session, WT_EXECUTEDEFAULT);
        }
        return NO_ERROR;
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        Report(SERVICE_STOP_PENDING);
        SetEvent(g_stop);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/* Run the selected mode until asked to stop. */
static void Serve()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return;
    if (lstrcmpW(g_service->mode, L"wsl") == 0)
        ServeWsl();
    else if (lstrcmpW(g_service->mode, L"tcp") == 0)
        ServeTcp();
    else if (lstrcmpW(g_service->mode, L"vmware") == 0)
        ServeVmware();
    else
        ServeSession();
    WSACleanup();
}

static void WINAPI ServiceMain(DWORD, LPWSTR *)
{
    g_status = RegisterServiceCtrlHandlerExW(g_service->name, Handler, NULL);
    if (!g_status)
        return;
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);

    Report(SERVICE_RUNNING);
    Serve();
    /* Connections still being relayed end with the process. */
    Report(SERVICE_STOPPED);
}

static BOOL WINAPI ConsoleStop(DWORD)
{
    SetEvent(g_stop);
    return TRUE;
}

int wmain(int argc, wchar_t **argv)
{
    g_console = argc == 3 && lstrcmpW(argv[2], L"--console") == 0;
    for (const xnt::ServiceInfo &info : xnt::kServices)
        if ((argc == 2 || g_console) && lstrcmpW(argv[1], info.mode) == 0)
            g_service = &info;

    if (g_service && g_console) {
        g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        SetConsoleCtrlHandler(ConsoleStop, TRUE);
        Log(std::wstring(L"xntsrv-service: running ") + g_service->displayName +
            L" in the foreground; press Ctrl+C to stop");
        Serve();
        return 0;
    }
    if (g_service) {
        wchar_t name[64];
        lstrcpynW(name, g_service->name, ARRAYSIZE(name));
        SERVICE_TABLE_ENTRYW table[] = { { name, ServiceMain }, { NULL, NULL } };
        if (StartServiceCtrlDispatcherW(table))
            return 0;
        if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            return 1;
    }
    xnt::ConsoleWrite(STD_ERROR_HANDLE,
                      L"xntsrv-service: this runs as a Windows service and cannot be started directly.\n"
                      L"Install it with xntsrv-register, then manage it in services.msc.\n");
    return 1;
}
