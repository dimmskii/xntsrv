/*
 * xntsrv-reset - remove everything XNTSrv set up on this computer.
 *
 * Does what xntsrv-unregister does (removes the service and the machine-wide
 * registry entries) and then deletes every user's XNTSrv data folder.  It does
 * not delete the program files themselves.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

#include <cstdio>

using std::wstring;

static void Out(const wstring &text) { xnt::ConsoleWrite(STD_OUTPUT_HANDLE, text + L"\n"); }
static void Err(const wstring &text) { xnt::ConsoleWrite(STD_ERROR_HANDLE, L"xntsrv-reset: " + text + L"\n"); }

#include "teardown.h"

static const wchar_t kHelp[] =
    L"Usage: xntsrv-reset [OPTION]\n"
    L"Uninstall the XNTSrv service and delete XNTSrv settings for all users and the\n"
    L"system.  Needs an administrator prompt.  Program files are left in place.\n"
    L"\n"
    L"  -y, --yes       do not ask for confirmation\n"
    L"  -h, --help      show this help\n"
    L"  -V, --version   show the version\n";

/* Delete a folder and its contents.  Links and junctions inside it are
 * removed, never followed. */
static bool RemoveTree(const wstring &dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    bool ok = true;

    if (find != INVALID_HANDLE_VALUE) {
        do {
            wstring name = fd.cFileName;
            if (name == L"." || name == L"..")
                continue;
            wstring path = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(path.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                ok = DeleteFileW(path.c_str()) && ok;
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                ok = RemoveDirectoryW(path.c_str()) && ok;
            else
                ok = RemoveTree(path) && ok;
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    return RemoveDirectoryW(dir.c_str()) && ok;
}

/* Delete the XNTSrv data folder from every user profile on the machine. */
static bool RemoveUserData()
{
    const wchar_t profiles[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";
    HKEY list;
    bool ok = true;
    int removed = 0;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, profiles, 0, KEY_READ, &list) != ERROR_SUCCESS) {
        Err(L"cannot list user profiles");
        return false;
    }
    for (DWORD i = 0;; i++) {
        wchar_t sid[256], raw[MAX_PATH * 2], path[MAX_PATH * 2];
        DWORD sidLen = ARRAYSIZE(sid), size = sizeof raw;

        if (RegEnumKeyExW(list, i, sid, &sidLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegGetValueW(list, sid, L"ProfileImagePath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                         NULL, raw, &size) != ERROR_SUCCESS)
            continue;
        if (!ExpandEnvironmentStringsW(raw, path, ARRAYSIZE(path)))
            continue;

        for (const wchar_t *sub : { L"\\AppData\\Local\\", L"\\AppData\\Roaming\\" }) {
            wstring dir = wstring(path) + sub + xnt::kUserDataDir;
            DWORD attr = GetFileAttributesW(dir.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (RemoveTree(dir)) {
                removed++;
            } else {
                Err(L"cannot delete " + dir + L" (is an X server still running?)");
                ok = false;
            }
        }
    }
    RegCloseKey(list);
    Out(L"User settings:         " + std::to_wstring(removed) + L" folder(s) removed");
    return ok;
}

int wmain(int argc, wchar_t **argv)
{
    bool yes = false;

    for (int i = 1; i < argc; i++) {
        wstring arg = argv[i];
        if (arg == L"-h" || arg == L"--help") {
            xnt::ConsoleWrite(STD_OUTPUT_HANDLE, kHelp);
            return 0;
        }
        if (arg == L"-V" || arg == L"--version") {
            Out(L"xntsrv-reset " XNTSRV_VERSION);
            return 0;
        }
        if (arg == L"-y" || arg == L"--yes") {
            yes = true;
            continue;
        }
        Err(L"unrecognized argument '" + arg + L"'");
        xnt::ConsoleWrite(STD_ERROR_HANDLE, L"Try 'xntsrv-reset --help' for more information.\n");
        return 2;
    }

    if (!xnt::IsElevated()) {
        Err(L"this needs an administrator prompt");
        return 1;
    }

    if (!yes) {
        wchar_t answer[16] = L"";

        xnt::ConsoleWrite(STD_OUTPUT_HANDLE,
                          L"This will completely uninstall the XNTSrv services and delete XNTSrv\n"
                          L"settings for all users and the system.\n"
                          L"Continue? [y/N] ");
        if (!fgetws(answer, ARRAYSIZE(answer), stdin) || (answer[0] != L'y' && answer[0] != L'Y')) {
            Out(L"Aborted. Nothing was changed.");
            return 1;
        }
    }

    bool ok = RemoveMachineSetup();
    ok = RemoveUserData() && ok;

    Out(ok ? L"Done. XNTSrv program files were left in place." : L"Finished with errors.");
    return ok ? 0 : 1;
}
