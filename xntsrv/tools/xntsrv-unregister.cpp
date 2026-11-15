/*
 * xntsrv-unregister - undo xntsrv-register.
 *
 * Stops and removes the XNTSrv service and deletes the machine-wide registry
 * entries.  User settings and program files are left alone; xntsrv-reset
 * removes the settings as well.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/xntsrv-common.h"

using std::wstring;

static void Out(const wstring &text) { xnt::ConsoleWrite(STD_OUTPUT_HANDLE, text + L"\n"); }
static void Err(const wstring &text) { xnt::ConsoleWrite(STD_ERROR_HANDLE, L"xntsrv-unregister: " + text + L"\n"); }

#include "teardown.h"

static const wchar_t kHelp[] =
    L"Usage: xntsrv-unregister [OPTION]\n"
    L"Remove the XNTSrv service and its machine-wide registration.  Needs an\n"
    L"administrator prompt.  User settings and program files are kept.\n"
    L"\n"
    L"  -h, --help      show this help\n"
    L"  -V, --version   show the version\n"
    L"\n"
    L"Use xntsrv-register to set it up again, or xntsrv-reset to delete settings too.\n";

int wmain(int argc, wchar_t **argv)
{
    for (int i = 1; i < argc; i++) {
        wstring arg = argv[i];
        if (arg == L"-h" || arg == L"--help") {
            xnt::ConsoleWrite(STD_OUTPUT_HANDLE, kHelp);
            return 0;
        }
        if (arg == L"-V" || arg == L"--version") {
            Out(L"xntsrv-unregister " XNTSRV_VERSION);
            return 0;
        }
        Err(L"unrecognized argument '" + arg + L"'");
        xnt::ConsoleWrite(STD_ERROR_HANDLE, L"Try 'xntsrv-unregister --help' for more information.\n");
        return 2;
    }

    if (!xnt::IsElevated()) {
        Err(L"this needs an administrator prompt");
        return 1;
    }

    bool ok = RemoveMachineSetup();
    Out(ok ? L"Done. User settings and program files were left in place." : L"Finished with errors.");
    return ok ? 0 : 1;
}
