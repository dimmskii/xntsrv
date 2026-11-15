/*
 * xntsrv-register - machine-wide setup for XNTSrv.
 *
 * Run from an administrator prompt in the folder that holds the XNTSrv
 * programs.  It records the install location, registers the Hyper-V socket
 * service ID and the App Paths entry, blocks inbound network connections to
 * the server, and installs and starts the XNTSrv services.  Running it again
 * repairs an existing setup.  xntsrv-unregister undoes it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

using std::wstring;

static void Out(const wstring &text) { xnt::ConsoleWrite(STD_OUTPUT_HANDLE, text + L"\n"); }
static void Err(const wstring &text) { xnt::ConsoleWrite(STD_ERROR_HANDLE, L"xntsrv-register: " + text + L"\n"); }

#include "teardown.h"

static const wchar_t kHelp[] =
    L"Usage: xntsrv-register [OPTION]\n"
    L"Set XNTSrv up for all users of this computer.  Needs an administrator prompt.\n"
    L"\n"
    L"  -h, --help      show this help\n"
    L"  -V, --version   show the version\n"
    L"\n"
    L"Registers the folder this program is in, so run it from the installed location.\n"
    L"Use xntsrv-unregister to undo.\n";

static bool SetString(const wchar_t *key, const wchar_t *name, const wstring &value)
{
    HKEY h;
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL);

    if (rc == ERROR_SUCCESS) {
        rc = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)value.c_str(),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(h);
    }
    if (rc != ERROR_SUCCESS)
        Err(wstring(L"cannot write HKLM\\") + key + L" (error " + std::to_wstring(rc) + L")");
    return rc == ERROR_SUCCESS;
}

static bool InstallService(SC_HANDLE scm, const xnt::ServiceInfo &info, const wstring &binary)
{
    wstring command = L"\"" + binary + L"\" " + info.mode;
    const DWORD access = SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_QUERY_STATUS;
    SC_HANDLE svc = CreateServiceW(scm, info.name, info.displayName, access,
                                   SERVICE_WIN32_OWN_PROCESS,
                                   info.autoStart ? SERVICE_AUTO_START : SERVICE_DEMAND_START,
                                   SERVICE_ERROR_NORMAL, command.c_str(), NULL, NULL, NULL, NULL, NULL);
    bool ok = svc != NULL;
    if (!svc && GetLastError() == ERROR_SERVICE_EXISTS) {
        /* Already installed: point it at this copy.  Its start type is left
         * alone, so a service the administrator disabled stays disabled. */
        svc = OpenServiceW(scm, info.name, access);
        ok = svc && ChangeServiceConfigW(svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE,
                                         SERVICE_ERROR_NORMAL, command.c_str(), NULL, NULL, NULL, NULL,
                                         NULL, info.displayName);
    }
    if (!ok) {
        Err(wstring(L"cannot install the ") + info.displayName + L" service (error " +
            std::to_wstring(GetLastError()) + L")");
    } else {
        wstring text = info.description;
        SERVICE_DESCRIPTIONW description = { &text[0] };
        ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &description);

        if (!info.autoStart) {
            Out(wstring(L"Service ") + info.displayName + L":    installed, not started");
            CloseServiceHandle(svc);
            return true;
        }

        SERVICE_STATUS status = {};
        if (StartServiceW(svc, 0, NULL) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
            for (int i = 0; i < 100 && QueryServiceStatus(svc, &status) && status.dwCurrentState != SERVICE_RUNNING; i++)
                Sleep(100);
            if (status.dwCurrentState != SERVICE_RUNNING) {
                Err(wstring(L"the ") + info.displayName + L" service is installed but is not running");
                ok = false;
            }
        } else if (GetLastError() == ERROR_SERVICE_DISABLED) {
            Out(wstring(L"Service ") + info.displayName + L":    installed, left disabled");
            CloseServiceHandle(svc);
            return true;
        } else {
            Err(wstring(L"the ") + info.displayName + L" service is installed but did not start (error " +
                std::to_wstring(GetLastError()) + L")");
            ok = false;
        }
    }
    if (svc)
        CloseServiceHandle(svc);
    if (ok)
        Out(wstring(L"Service ") + info.displayName + L":    installed and running");
    return ok;
}

int wmain(int argc, wchar_t **argv)
{
    for (int i = 1; i < argc; i++) {
        wstring arg = argv[i];
        if (arg == L"-h" || arg == L"--help") {
            xnt::ConsoleWrite(STD_OUTPUT_HANDLE, kHelp);
            return 0;
        }
        if (arg == L"-V" || arg == L"--version") {
            Out(L"xntsrv-register " XNTSRV_VERSION);
            return 0;
        }
        Err(L"unrecognized argument '" + arg + L"'");
        xnt::ConsoleWrite(STD_ERROR_HANDLE, L"Try 'xntsrv-register --help' for more information.\n");
        return 2;
    }

    if (!xnt::IsElevated()) {
        Err(L"this needs an administrator prompt");
        return 1;
    }

    wstring dir = xnt::ProgramDir();
    wstring service = dir + L"\\xntsrv-service.exe";
    if (GetFileAttributesW(service.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Err(L"xntsrv-service.exe is not in " + dir);
        return 1;
    }

    bool ok = true;

    if (SetString(xnt::kInstallKey, L"InstallDir", dir))
        Out(L"Install location:      " + dir);
    else
        ok = false;

    if (SetString(xnt::kHvSocketKey, L"ElementName", L"XNTSrv"))
        Out(L"Hyper-V socket ID:     registered (port 106000)");
    else
        ok = false;

    if (SetString(xnt::kAppPathsKey, NULL, dir + L"\\xntsrv.exe"))
        Out(L"App Paths entry:       xntsrv.exe");
    else
        ok = false;

    HRESULT hr = SetFirewallRules(dir);
    if (SUCCEEDED(hr)) {
        Out(L"Firewall:              server closed to the network; TCP service limited to the local subnet");
    } else {
        Err(L"cannot add the firewall rules (" + xnt::Hex((DWORD)hr) + L")");
        ok = false;
    }

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        Err(L"cannot open the service manager (error " + std::to_wstring(GetLastError()) + L")");
        ok = false;
    } else {
        for (const wchar_t *retired : xnt::kRetiredServices)
            RemoveService(scm, retired, NULL);
        for (const xnt::ServiceInfo &info : xnt::kServices)
            ok = InstallService(scm, info, service) && ok;
        CloseServiceHandle(scm);
    }

    if (ok)
        Out(L"Done. Restart WSL (wsl --shutdown) once so it picks up the socket registration.");
    return ok ? 0 : 1;
}
