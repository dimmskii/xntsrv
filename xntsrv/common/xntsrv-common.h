/*
 * Shared by the XNTSrv Windows programs: names, registry locations, and the
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

const wchar_t kServiceName[] = L"XNTSrv";
const wchar_t kPipeName[] = L"\\\\.\\pipe\\XNTSrv";

/* Machine-wide registry entries, all under HKEY_LOCAL_MACHINE. */
const wchar_t kInstallKey[] = L"SOFTWARE\\XNTSrv";
const wchar_t kAppPathsKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\xntsrv.exe";
/* Hyper-V socket service ID for the default vsock port, 106000 (0x19E10).
 * Windows only delivers guest connections to registered service IDs. */
const wchar_t kHvSocketKey[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Virtualization\\GuestCommunicationServices"
    L"\\{00019e10-facb-11e6-bd58-64006a7986d3}";

/* Per-user data lives in files, not in the registry, so that it can be found
 * and removed for users who are not logged in. */
const wchar_t kUserDataDir[] = L"XNTSrv";

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
 * answers this for administrators, so normal programs go through the XNTSrv
 * service instead (AskServiceForVmId).  Returns a braced GUID, or an empty
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

/* Ask the XNTSrv service which VM is the running WSL VM.  Returns a braced
 * GUID, or an empty string with the reason in *detail. */
inline std::wstring AskServiceForVmId(std::wstring *detail)
{
    /* SECURITY_IDENTIFICATION stops whoever owns the pipe from acting as us. */
    HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                              SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        *detail = err == ERROR_FILE_NOT_FOUND
                      ? L"the XNTSrv service is not running (install it with xntsrv-register)"
                      : L"cannot reach the XNTSrv service (error " + std::to_wstring(err) + L")";
        return L"";
    }

    char buf[64];
    DWORD total = 0, n = 0;
    while (total < sizeof buf && ReadFile(pipe, buf + total, sizeof buf - total, &n, NULL) && n > 0)
        total += n;
    CloseHandle(pipe);

    std::wstring reply;
    for (DWORD i = 0; i < total && buf[i] != '\n' && buf[i] != '\r'; i++)
        reply.push_back((wchar_t)(unsigned char)buf[i]);
    if (IsGuid(reply))
        return reply;
    *detail = L"no WSL VM is running";
    return L"";
}

} /* namespace xnt */
