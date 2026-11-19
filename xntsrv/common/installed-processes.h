/*
 * Finding the running programs of this XNTsrv installation, in any user's
 * session.  Shared by the uninstall tools (to close them so their files can
 * be replaced) and the Session service (to leave alone a server it did not
 * start).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "xntsrv-common.h"

#include <tlhelp32.h>

#include <initializer_list>

namespace xnt {

/* Calls fn(entry, process) for each running process whose file name is one of
 * `names` and whose program lies in this installation's folder (the folder of
 * the running program), so that a same-named program elsewhere is never
 * touched.  `process` is opened with `access` plus
 * PROCESS_QUERY_LIMITED_INFORMATION and closed after fn returns.  fn returns
 * false to stop the search.  Processes that cannot be opened are skipped. */
template <typename Fn>
inline void ForEachInstalledProcess(std::initializer_list<const wchar_t *> names, DWORD access, Fn fn)
{
    std::wstring dir = ProgramDir() + L"\\";
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry = {};
    bool more = true;

    entry.dwSize = sizeof entry;
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    for (BOOL next = Process32FirstW(snapshot, &entry); next && more; next = Process32NextW(snapshot, &entry)) {
        bool named = false;
        for (const wchar_t *name : names)
            named = named || lstrcmpiW(entry.szExeFile, name) == 0;
        if (!named)
            continue;

        HANDLE process = OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
            continue;
        wchar_t path[MAX_PATH * 4];
        DWORD length = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(process, 0, path, &length) && length > dir.size() &&
            CompareStringOrdinal(path, (int)dir.size(), dir.c_str(), (int)dir.size(), TRUE) == CSTR_EQUAL)
            more = fn(entry, process);
        CloseHandle(process);
    }
    CloseHandle(snapshot);
}

} /* namespace xnt */
