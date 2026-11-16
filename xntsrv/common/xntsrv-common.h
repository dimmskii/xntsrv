/*
 * Shared by the XNTsrv Windows programs: names, registry locations, and the
 * WSL virtual machine lookup.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#define XNTSRV_VERSION L"0.1.0"

namespace xnt {

/* One program, xntsrv-service.exe, runs as several Windows services, one per
 * way of reaching the X server.  Each is started with its mode as argument. */
struct ServiceInfo {
    const wchar_t *name;        /* service name, as used by sc.exe */
    const wchar_t *mode;        /* argument passed to xntsrv-service.exe */
    const wchar_t *displayName; /* shown in services.msc */
    bool autoStart;             /* start with Windows; otherwise only when someone starts it */
    const wchar_t *description;
};
const ServiceInfo kServices[] = {
    { L"XNTsrvWSL", L"wsl", L"XNTsrv WSL", true,
      L"Carries connections from WSL to the XNTsrv X server over the Hyper-V socket. "
      L"Stop it to cut WSL off from the X server." },
    { L"XNTsrvTCP", L"tcp", L"XNTsrv TCP", false,
      L"Carries connections from the network to the XNTsrv X server. Not started unless you start it. "
      L"Listens on every network address of this computer, or only on the addresses and adapter names "
      L"listed in HKLM\\SOFTWARE\\XNTsrv\\TCP, value Listen." },
    { L"XNTsrvVMware", L"vmware", L"XNTsrv VMware", false,
      L"Placeholder, not implemented yet. Will carry connections from VMware guests to the XNTsrv X server." },
    { L"XNTsrvSession", L"session", L"XNTsrv Session", false,
      L"Placeholder, not implemented yet. Will start an XNTsrv X server in the background for each user who logs in." },
};
/* Services earlier versions installed; removed on register and unregister. */
const wchar_t *const kRetiredServices[] = { L"XNTsrv" };

/* Port the Linux bridge connects to; kHvSocketKey below is this port's ID. */
const unsigned kVsockPort = 106000;
/* The X server's TCP port is this plus the display number. */
const unsigned kXBasePort = 6000;

/* Machine-wide registry entries, all under HKEY_LOCAL_MACHINE. */
const wchar_t kInstallKey[] = L"SOFTWARE\\XNTsrv";
/* Settings of the XNTsrv TCP service. */
const wchar_t kTcpKey[] = L"SOFTWARE\\XNTsrv\\TCP";
const wchar_t kAppPathsKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\xntsrv.exe";
/* Hyper-V socket service ID for the default vsock port, 106000 (0x19E10).
 * Windows only delivers guest connections to registered service IDs. */
const wchar_t kHvSocketKey[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Virtualization\\GuestCommunicationServices"
    L"\\{00019e10-facb-11e6-bd58-64006a7986d3}";

/* Per-user data lives in files, not in the registry, so that it can be found
 * and removed for users who are not logged in. */
const wchar_t kUserDataDir[] = L"XNTsrv";

inline std::wstring Hex(DWORD value)
{
    wchar_t buf[16];
    wsprintfW(buf, L"0x%08X", value);
    return buf;
}

/* True for a GUID in registry format: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}. */
inline bool IsGuid(const std::wstring &s)
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

/* Write text to a standard handle: UTF-16 to a console, UTF-8 to a file or pipe. */
inline void ConsoleWrite(DWORD which, const std::wstring &text)
{
    HANDLE h = GetStdHandle(which);
    DWORD n, mode;

    if (!h || h == INVALID_HANDLE_VALUE)
        return;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text.c_str(), (DWORD)text.size(), &n, NULL);
        return;
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string utf8((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], len, NULL, NULL);
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &n, NULL);
}

inline bool IsElevated()
{
    HANDLE token;
    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof elevation;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    bool ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size) != 0;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

/* Folder that holds the running program. */
inline std::wstring ProgramDir()
{
    wchar_t self[MAX_PATH * 4];
    DWORD len = GetModuleFileNameW(NULL, self, ARRAYSIZE(self));
    std::wstring path(self, len);
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

/* Ask the Host Compute Service for the running WSL utility VM.  Windows only
 * answers this for administrators and services.  Returns a braced GUID, or an empty
 * string with the reason in *detail. */
inline std::wstring FindWslVmId(std::wstring *detail)
{
    typedef void *HCS_OPERATION;
    typedef HCS_OPERATION(WINAPI *CreateOperationFn)(const void *, void *);
    typedef HRESULT(WINAPI *EnumerateFn)(PCWSTR, HCS_OPERATION);
    typedef HRESULT(WINAPI *WaitFn)(HCS_OPERATION, DWORD, PWSTR *);
    typedef void(WINAPI *CloseFn)(HCS_OPERATION);

    std::wstring id;
    HMODULE dll = LoadLibraryExW(L"computecore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) {
        *detail = L"computecore.dll is not available (is the Virtual Machine Platform installed?)";
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
            *detail = L"the Host Compute Service refused the query (" + Hex((DWORD)hr) + L")";
            /* 0x8037011B is HCS_E_ACCESS_DENIED. */
            if (hr == E_ACCESSDENIED || hr == (HRESULT)0x8037011BL)
                *detail += L"; listing VMs needs administrator rights";
        } else if (doc) {
            /* The result is a JSON array of systems; take the first one's
             * RuntimeId (the VM's Hyper-V socket address), else its Id. */
            std::wstring json = doc;
            for (const wchar_t *key : { L"\"RuntimeId\":\"", L"\"Id\":\"" }) {
                size_t pos = json.find(key);
                if (pos == std::wstring::npos)
                    continue;
                std::wstring guid = L"{" + json.substr(pos + wcslen(key), 36) + L"}";
                if (IsGuid(guid)) {
                    id = guid;
                    break;
                }
            }
            if (id.empty())
                *detail = L"no WSL VM is running";
        }
        if (doc)
            LocalFree(doc);
        if (op)
            close(op);
    } else {
        *detail = L"computecore.dll lacks the expected functions";
    }
    FreeLibrary(dll);
    return id;
}

} /* namespace xnt */
