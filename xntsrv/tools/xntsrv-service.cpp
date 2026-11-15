/*
 * xntsrv-service - the privileged half of XNTSrv.
 *
 * Windows only tells administrators which virtual machine is the WSL VM, and
 * the X server needs that ID to accept Hyper-V socket connections.  This
 * service runs as LocalSystem and answers that one question over a named pipe:
 * a client connects, reads one line holding the VM's GUID (or an empty line
 * when WSL is not running), and the connection is closed.
 *
 * It is installed and removed by xntsrv-register and xntsrv-reset.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

#include <sddl.h>

static SERVICE_STATUS_HANDLE g_status;
static HANDLE g_stop;

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

/* Wait for an overlapped operation or a stop request.  Returns false when the
 * service is stopping or the wait timed out. */
static bool WaitIo(HANDLE pipe, OVERLAPPED *ov, DWORD timeoutMs)
{
    HANDLE handles[2] = { g_stop, ov->hEvent };

    if (WaitForMultipleObjects(2, handles, FALSE, timeoutMs) == WAIT_OBJECT_0 + 1)
        return true;
    CancelIo(pipe);
    return false;
}

static void Serve()
{
    /* Full access for the system and administrators; logged-in users may read. */
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, FALSE };
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;IU)", SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL))
        return;

    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE pipe = CreateNamedPipeW(xnt::kPipeName,
                                   PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   1, 256, 256, 0, &sa);
    LocalFree(sa.lpSecurityDescriptor);

    while (pipe != INVALID_HANDLE_VALUE && ov.hEvent && WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0) {
        ResetEvent(ov.hEvent);
        if (!ConnectNamedPipe(pipe, &ov)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!WaitIo(pipe, &ov, INFINITE))
                    break;
            } else if (err != ERROR_PIPE_CONNECTED) {
                Sleep(100);
                DisconnectNamedPipe(pipe);
                continue;
            }
        }

        std::wstring detail;
        std::wstring id = xnt::FindWslVmId(&detail);
        std::string line;
        for (wchar_t c : id)
            line.push_back((char)c); /* a GUID is plain ASCII */
        line.push_back('\n');

        DWORD n;
        ResetEvent(ov.hEvent);
        if (WriteFile(pipe, line.data(), (DWORD)line.size(), &n, &ov) || GetLastError() == ERROR_IO_PENDING) {
            /* Give the client a moment to read and hang up; the read below
             * completes when it closes its end.  A client that stalls is cut
             * off so it cannot block the next one. */
            char sink[16];
            WaitIo(pipe, &ov, 2000);
            ResetEvent(ov.hEvent);
            if (!ReadFile(pipe, sink, sizeof sink, &n, &ov) && GetLastError() == ERROR_IO_PENDING)
                WaitIo(pipe, &ov, 2000);
        }
        DisconnectNamedPipe(pipe);
    }

    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
    if (ov.hEvent)
        CloseHandle(ov.hEvent);
}

static void WINAPI ServiceMain(DWORD, LPWSTR *)
{
    g_status = RegisterServiceCtrlHandlerExW(xnt::kServiceName, Handler, NULL);
    if (!g_status)
        return;
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    Report(SERVICE_RUNNING);
    Serve();
    Report(SERVICE_STOPPED);
}

int wmain()
{
    wchar_t name[ARRAYSIZE(xnt::kServiceName)];
    lstrcpyW(name, xnt::kServiceName);
    SERVICE_TABLE_ENTRYW table[] = { { name, ServiceMain }, { NULL, NULL } };

    if (StartServiceCtrlDispatcherW(table))
        return 0;
    if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        xnt::ConsoleWrite(STD_ERROR_HANDLE,
                          L"xntsrv-service: this is a Windows service and cannot be run directly.\n"
                          L"Install and start it with xntsrv-register.\n");
    return 1;
}
