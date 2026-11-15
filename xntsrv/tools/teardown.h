/*
 * Removal of the machine-wide setup made by xntsrv-register.  Shared by
 * xntsrv-unregister and xntsrv-reset, which each define Out() and Err()
 * before including this file.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "firewall.h"

/* Stop and delete one service.  `label` is what to call it in the output, or
 * NULL to stay quiet (used for services from earlier versions).  Returns true
 * when the service is gone afterwards. */
static inline bool RemoveService(SC_HANDLE scm, const wchar_t *name, const wchar_t *label)
{
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!svc) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            if (label)
                Out(std::wstring(L"Service ") + label + L":    not installed");
            return true;
        }
        Err(std::wstring(L"cannot open the ") + name + L" service (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }

    SERVICE_STATUS status = {};
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    for (int i = 0; i < 100 && QueryServiceStatus(svc, &status) && status.dwCurrentState != SERVICE_STOPPED; i++)
        Sleep(100);

    bool ok = DeleteService(svc) || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE;
    if (!ok)
        Err(std::wstring(L"cannot remove the ") + name + L" service (error " + std::to_wstring(GetLastError()) + L")");
    else if (label)
        Out(std::wstring(L"Service ") + label + L":    removed");
    CloseServiceHandle(svc);
    return ok;
}

static inline bool RemoveKey(const wchar_t *key, const wchar_t *label)
{
    LONG rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);

    if (rc == ERROR_SUCCESS) {
        Out(wstring(label) + L"removed");
    } else if (rc == ERROR_FILE_NOT_FOUND) {
        Out(wstring(label) + L"not present");
    } else {
        Err(wstring(L"cannot delete HKLM\\") + key + L" (error " + std::to_wstring(rc) + L")");
        return false;
    }
    return true;
}

/* Remove the services, the firewall rule and every registry entry
 * xntsrv-register made. */
static inline bool RemoveMachineSetup()
{
    bool ok = true;
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);

    if (!scm) {
        Err(L"cannot open the service manager (error " + std::to_wstring(GetLastError()) + L")");
        ok = false;
    } else {
        for (const xnt::ServiceInfo &info : xnt::kServices)
            ok = RemoveService(scm, info.name, info.displayName) && ok;
        for (const wchar_t *retired : xnt::kRetiredServices)
            ok = RemoveService(scm, retired, NULL) && ok;
        CloseServiceHandle(scm);
    }

    HRESULT hr = SetFirewallRule(L"");
    if (SUCCEEDED(hr)) {
        Out(L"Firewall:              rule removed");
    } else {
        Err(L"cannot remove the firewall rule (" + xnt::Hex((DWORD)hr) + L")");
        ok = false;
    }

    ok = RemoveKey(xnt::kHvSocketKey, L"Hyper-V socket ID:     ") && ok;
    ok = RemoveKey(xnt::kAppPathsKey, L"App Paths entry:       ") && ok;
    ok = RemoveKey(xnt::kInstallKey, L"Install location:      ") && ok;
    return ok;
}
