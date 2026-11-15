/*
 * xntsrv-service - carries connections to the XNTSrv X server.
 *
 * The X server itself only listens on 127.0.0.1.  Each way of reaching it from
 * outside is a separate Windows service running this program in a different
 * mode, so each can be started, stopped and configured on its own in
 * services.msc.  A service accepts connections on its own kind of socket and
 * relays the bytes to the server; it does not interpret them, so X
 * authentication still happens between the client and the server.
 *
 *   wsl   Listens on the Hyper-V socket of the running WSL virtual machine.
 *         Finding that VM needs administrator rights, which is why this is a
 *         service.  It follows WSL restarts by listening on the new VM.
 *
 * Installed and removed by xntsrv-register and xntsrv-unregister.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <objbase.h>

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

static bool Stopping() { return WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0; }

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

static DWORD WINAPI Connection(LPVOID param)
{
    SOCKET client = (SOCKET)(UINT_PTR)param;

    /* Display :0 for now; choosing a display per user comes with multi-user support. */
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)xnt::kXBasePort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server != INVALID_SOCKET && connect(server, (sockaddr *)&addr, sizeof addr) == 0) {
        int on = 1;
        setsockopt(server, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
        Relay(client, server);
    }
    /* If no X server is running, the client simply sees the connection close. */
    if (server != INVALID_SOCKET)
        closesocket(server);
    closesocket(client);
    return 0;
}

/* ---- wsl mode ---------------------------------------------------------- */

static SOCKET ListenOnVm(const std::wstring &vmId)
{
    SockaddrHv addr = {};
    addr.Family = AF_HYPERV;
    /* The Linux side addresses a Hyper-V socket by vsock port; Windows sees
     * that port as the first field of this well-known service ID template. */
    if (FAILED(CLSIDFromString(L"{00000000-facb-11e6-bd58-64006a7986d3}", &addr.ServiceId)) ||
        FAILED(CLSIDFromString(vmId.c_str(), &addr.VmId)))
        return INVALID_SOCKET;
    addr.ServiceId.Data1 = xnt::kVsockPort;

    SOCKET s = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET)
        return s;
    if (bind(s, (sockaddr *)&addr, sizeof addr) != 0 || listen(s, SOMAXCONN) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static void ServeWsl()
{
    std::wstring boundVm;
    SOCKET listener = INVALID_SOCKET;

    while (!Stopping()) {
        /* The VM's ID changes whenever WSL restarts, so keep checking and
         * move the listener when it does. */
        std::wstring detail;
        std::wstring vm = xnt::FindWslVmId(&detail);
        if (vm != boundVm || (listener == INVALID_SOCKET && !vm.empty())) {
            if (listener != INVALID_SOCKET)
                closesocket(listener);
            listener = vm.empty() ? INVALID_SOCKET : ListenOnVm(vm);
            boundVm = listener == INVALID_SOCKET ? L"" : vm;
        }

        if (listener == INVALID_SOCKET) {
            WaitForSingleObject(g_stop, 3000);
            continue;
        }

        /* Accept for a few seconds, then go round and look at the VM again. */
        ULONGLONG until = GetTickCount64() + 3000;
        while (!Stopping() && GetTickCount64() < until) {
            fd_set readable;
            timeval wait = { 0, 500000 };
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            if (select(0, &readable, NULL, NULL, &wait) <= 0)
                continue;
            SOCKET client = accept(listener, NULL, NULL);
            if (client == INVALID_SOCKET)
                break;
            HANDLE thread = CreateThread(NULL, 0, Connection, (LPVOID)(UINT_PTR)client, 0, NULL);
            if (thread)
                CloseHandle(thread);
            else
                closesocket(client);
        }
    }
    if (listener != INVALID_SOCKET)
        closesocket(listener);
}

/* ---- service plumbing -------------------------------------------------- */

static void Report(DWORD state)
{
    SERVICE_STATUS s = {};
    s.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s.dwCurrentState = state;
    s.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    SetServiceStatus(g_status, &s);
}

static DWORD WINAPI Handler(DWORD control, DWORD, LPVOID, LPVOID)
{
    switch (control) {
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

static void WINAPI ServiceMain(DWORD, LPWSTR *)
{
    g_status = RegisterServiceCtrlHandlerExW(g_service->name, Handler, NULL);
    if (!g_status)
        return;
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        Report(SERVICE_RUNNING);
        ServeWsl(); /* the only mode so far */
        WSACleanup();
    }
    /* Connections still being relayed end with the process. */
    Report(SERVICE_STOPPED);
}

int wmain(int argc, wchar_t **argv)
{
    for (const xnt::ServiceInfo &info : xnt::kServices)
        if (argc == 2 && lstrcmpW(argv[1], info.mode) == 0)
            g_service = &info;

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
