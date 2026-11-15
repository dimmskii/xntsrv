/*
 * Removal of the machine-wide setup made by xntsrv-register.  Shared by
 * xntsrv-unregister and xntsrv-reset, which each define Out() and Err()
 * before including this file.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/* Returns true when the service is gone afterwards. */
static bool RemoveService()
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        Err(L"cannot open the service manager (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }

    bool ok = true;
    SC_HANDLE svc = OpenServiceW(scm, xnt::kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!svc) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            Out(L"Service:               not installed");
        } else {
            Err(L"cannot open the service (error " + std::to_wstring(GetLastError()) + L")");
            ok = false;
        }
    } else {
        SERVICE_STATUS status = {};
        ControlService(svc, SERVICE_CONTROL_STOP, &status);
        for (int i = 0; i < 100 && QueryServiceStatus(svc, &status) && status.dwCurrentState != SERVICE_STOPPED; i++)
            Sleep(100);

        if (DeleteService(svc) || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) {
            Out(L"Service:               removed");
        } else {
            Err(L"cannot remove the service (error " + std::to_wstring(GetLastError()) + L")");
            ok = false;
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

static bool RemoveKey(const wchar_t *key, const wchar_t *label)
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

/* Remove the service and every registry entry xntsrv-register wrote. */
static bool RemoveMachineSetup()
{
    bool ok = RemoveService();
    ok = RemoveKey(xnt::kHvSocketKey, L"Hyper-V socket ID:     ") && ok;
    ok = RemoveKey(xnt::kAppPathsKey, L"App Paths entry:       ") && ok;
    ok = RemoveKey(xnt::kInstallKey, L"Install location:      ") && ok;
    return ok;
}
