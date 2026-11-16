#!/usr/bin/env python3
"""Renames VcXsrv to XNTsrv in the source tree, just before a build.

The committed tree stays identical to upstream so that merging upstream is
painless; this script makes the rename in the build's working copy instead.
Every replacement is listed explicitly and must match exactly the stated
number of times, so a change upstream that moves or rewords one of these
strings makes the build fail here rather than ship a half-renamed server.

  rebrand.py [--check] [REPOSITORY_ROOT]

--check reports what would change without writing anything.

SPDX-License-Identifier: MIT
"""
import os
import sys

NAME = "XNTsrv"
EXE = "xntsrv-server"
URL = "https://github.com/dimmskii/xntsrv"

# (file, [(old, new, expected number of occurrences), ...])
RULES = [
    # The executable and its import library, and everything that links to them.
    ("xorg-server/makefile", [
        ("TTYAPP=vcxsrv", f"TTYAPP={EXE}", 1),
        ("WINAPP=vcxsrv", f"WINAPP={EXE}", 1),
    ]),
    ("mesalib/src/makefile", [
        (r"$(SERVOBJDIR)\vcxsrv.lib", rf"$(SERVOBJDIR)\{EXE}.lib", 1),
    ]),
    ("xorg-server/hw/xwin/swrastwgl_dri/makefile", [
        (r"$(SERVOBJDIR)\vcxsrv.exe", rf"$(SERVOBJDIR)\{EXE}.exe", 1),
        (r"$(SERVOBJDIR)\vcxsrv.lib", rf"$(SERVOBJDIR)\{EXE}.lib", 1),
    ]),
    ("xorg-server/installer/vcxsrv-64.nsi", [
        (r'File "..\obj64\servrelease\vcxsrv.exe"', rf'File "..\obj64\servrelease\{EXE}.exe"', 1),
    ]),
    ("xorg-server/hw/xwin/xlaunch/main.cc", [
        ('buffer = "vcxsrv ";', f'buffer = "{EXE} ";', 1),
    ]),

    # What X clients and users see: vendor strings, titles, dialogs, the log.
    ("include/dix-config.h", [
        ('#define XVENDORNAME "The VcXsrv Project"', f'#define XVENDORNAME "The {NAME} Project"', 1),
        ('#define BUILDERADDR "https://github.com/marchaesen"', f'#define BUILDERADDR "{URL}"', 1),
    ]),
    ("xorg-server/include/xwin-config.h", [
        ('#define XVENDORNAMESHORT "VcXsrv"', f'#define XVENDORNAMESHORT "{NAME}"', 1),
    ]),
    ("xorg-server/hw/xwin/winwindow.h", [
        ('#define PROJECT_NAME\t\t"VcXsrv"', f'#define PROJECT_NAME\t\t"{NAME}"', 1),
        ('#define EXECUTABLE_NAME         "VcXsrv"', f'#define EXECUTABLE_NAME         "{NAME}"', 1),
        ('#define WINDOW_CLASS            "VcXsrv/x"', f'#define WINDOW_CLASS            "{NAME}/x"', 1),
        ('"vcxsrv/x X rl"', '"xntsrv/x X rl"', 1),
        ('"vcxsrv/x X msg"', '"xntsrv/x X msg"', 1),
    ]),
    ("xorg-server/hw/xwin/XWin.rc", [
        (r'"VcXsrv windows xserver\0"', rf'"{NAME} X server\0"', 1),
        (r'VALUE "InternalName",     "VcXsrv\0"', rf'VALUE "InternalName",     "{NAME}\0"', 1),
        (r'"vcxsrv.exe\0"', rf'"{EXE}.exe\0"', 1),
        (r'VALUE "ProductName",      "VcXsrv\0"', rf'VALUE "ProductName",      "{NAME}\0"', 1),
        # The About box keeps the credit to the project this is built on.
        ('"VcXsrv X Server "', f'"{NAME} X Server, based on VcXsrv "', 1),
        ('"https://github.com/marchaesen/vcxsrv"', f'"{URL}"', 1),
    ]),
    ("xorg-server/hw/xwin/winprocarg.c", [
        ("Welcome to the VcXsrv X Server", f"Welcome to the {NAME} X Server", 1),
    ]),
    ("xorg-server/hw/xwin/winprefs.c", [
        ("VcXsrv Exec Command Error!", f"{NAME} Exec Command Error!", 1),
    ]),
    ("xorg-server/hw/xwin/InitOutput.c", [
        ('"VCXSrv.%s.log"', f'"{NAME}.%s.log"', 1),
    ]),
    ("xorg-server/os/utils.c", [
        ("Usage...\\nVcxsrv [:<display>]", f"Usage...\\n{EXE} [:<display>]", 1),
        ("\\tVcxsrv runs as the given display-number", f"\\t{EXE} runs as the given display-number", 1),
    ]),

    # Names of kernel objects and window properties, so that XNTsrv and a
    # VcXsrv on the same machine never mistake each other's.
    ("xorg-server/os/serverlock.c", [
        (r"Global\\VcXsrv_Mutex_%d", rf"Global\\{NAME}_Mutex_%d", 1),
    ]),
    ("xorg-server/hw/xwin/glx/indirect.c", [
        (r"Local\\VCXSRV_WINDOWSDRI_%08x", r"Local\\XNTSRV_WINDOWSDRI_%08x", 1),
    ]),
    ("xorg-server/hw/xwin/winglobals.c", [
        ('"vcxsrv_', '"xntsrv_', 5),
    ]),
]


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    check = "--check" in sys.argv[1:]
    root = args[0] if args else "."
    problems = []
    changed = 0
    results = []

    for path, replacements in RULES:
        full = os.path.join(root, path)
        try:
            # newline="" keeps each file's line endings exactly as they are.
            with open(full, encoding="latin-1", newline="") as f:
                text = f.read()
        except OSError as error:
            problems.append(f"{path}: {error.strerror}")
            continue

        for old, new, expected in replacements:
            found = text.count(old)
            if found != expected:
                problems.append(f"{path}: expected {expected} of {old!r}, found {found}")
                continue
            text = text.replace(old, new)
            changed += found

        results.append((full, text))

    if problems:
        print("rebrand: the source tree does not match what this script expects:", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1
    # Nothing is written unless every replacement matched.
    if not check:
        for full, text in results:
            with open(full, "w", encoding="latin-1", newline="") as f:
                f.write(text)
    print(f"rebrand: {changed} replacement(s) in {len(RULES)} file(s){' (check only)' if check else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
