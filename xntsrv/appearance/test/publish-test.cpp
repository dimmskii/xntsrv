/*
 * Test program for the X side of the appearance helper.  Runs on Linux.
 *
 *   publish-test unit
 *       Checks the encoders with no X server.
 *   publish-test live HOST PORT COOKIEFILE DPI FONT CURSORSIZE DARK [HOLD_SECONDS]
 *       Publishes those values to an X server over TCP, keeps them up for
 *       HOLD_SECONDS so that they can be read back with xprop and xrdb, then
 *       republishes with DARK flipped and exits.  Exits 3 if another settings
 *       manager already owns the display.
 *
 * SPDX-License-Identifier: MIT
 */
#include "../publish.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures = 0;

static void Check(const char *what, bool ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static int Unit()
{
    XSettings s;
    s.SetInt("Xft/DPI", 147456);
    s.SetString("Gtk/FontName", "Segoe UI 9");
    std::string e = s.Encode(7);
    /* header 12; int entry 4+7(+1)+4+4 = 20; string entry 4+12+4+4+10(+2) = 36 */
    Check("XSETTINGS: total length", e.size() == 12 + 20 + 36);
    Check("XSETTINGS: byte order, serial and count",
          e[0] == 0 && (uint8_t)e[4] == 7 && (uint8_t)e[8] == 2);
    Check("XSETTINGS: integer entry", e[12] == 0 && (uint8_t)e[14] == 7 && e.substr(16, 7) == "Xft/DPI" &&
                                          (uint8_t)e[28] == 0x00 && (uint8_t)e[29] == 0x40 && (uint8_t)e[30] == 0x02);
    Check("XSETTINGS: string entry", e[32] == 1 && (uint8_t)e[34] == 12 && e.substr(36, 12) == "Gtk/FontName" &&
                                         (uint8_t)e[52] == 10 && e.substr(56, 10) == "Segoe UI 9");

    Check("resources: added to nothing", MergeResources("", { { "Xft.dpi", "144" } }) == "Xft.dpi:\t144\n");
    Check("resources: others kept, ours replaced",
          MergeResources("XTerm*background: black\nXft.dpi:  96\n*foreground:\twhite\n",
                         { { "Xft.dpi", "144" }, { "Xcursor.size", "48" } }) ==
              "XTerm*background: black\n*foreground:\twhite\nXft.dpi:\t144\nXcursor.size:\t48\n");
    Check("resources: a similar name is not ours",
          MergeResources("Xft.dpiX: 1\n", { { "Xft.dpi", "144" } }) == "Xft.dpiX: 1\nXft.dpi:\t144\n");
    Check("resources: unchanged when already set",
          MergeResources("Xft.dpi:\t144\n", { { "Xft.dpi", "144" } }) == "Xft.dpi:\t144\n");

    Appearance a;
    a.dpi = 144;
    a.cursorSize = 48;
    Check("resources for: no palette, two entries", ResourcesFor(a).size() == 2);
    a.cursorTheme = "XNTsrv";
    Check("resources for: cursor theme", MergeResources("", ResourcesFor(a)).find("Xcursor.theme:\tXNTsrv\n") != std::string::npos);
    for (uint32_t i = 0; i < 16; i++)
        a.palette.push_back(i * 0x111111);
    a.foreground = 15;
    a.background = 4;
    std::string merged = MergeResources("", ResourcesFor(a));
    Check("resources for: palette entries", merged.find("*color0:\t#000000\n") != std::string::npos &&
                                                merged.find("*color15:\t#ffffff\n") != std::string::npos &&
                                                merged.find("*color9:\t#999999\n") != std::string::npos);
    Check("resources for: terminal text and background", merged.find("XTerm*foreground:\t#ffffff\n") != std::string::npos &&
                                                             merged.find("XTerm*background:\t#444444\n") != std::string::npos &&
                                                             merged.find("URxvt*background:\t#444444\n") != std::string::npos);
    Check("resources for: nothing sets a bare *background", merged.find("\n*background") == std::string::npos);
    Check("resources for: no terminal look unless given", merged.find("faceSize") == std::string::npos);
    a.terminalFont = "Consolas";
    a.terminalBold = true;
    a.terminalPoints = 12;
    a.terminalColumns = 120;
    a.terminalRows = 30;
    a.terminalScrollback = 8971;
    a.terminalUnderlineCursor = true;
    merged = MergeResources("", ResourcesFor(a));
    Check("resources for: terminal font", merged.find("XTerm*faceName:\tConsolas:bold\n") != std::string::npos &&
                                              merged.find("XTerm*faceSize:\t12\n") != std::string::npos &&
                                              merged.find("URxvt*font:\txft:Consolas:bold:size=12\n") != std::string::npos);
    Check("resources for: terminal size, history and cursor",
          merged.find("XTerm*geometry:\t120x30\n") != std::string::npos &&
              merged.find("UXTerm*saveLines:\t8971\n") != std::string::npos &&
              merged.find("XTerm*cursorUnderLine:\ttrue\n") != std::string::npos &&
              merged.find("URxvt*cursorUnderline:\ttrue\n") != std::string::npos);

    /* One wildcard-family entry, as the launcher writes it. */
    std::string file("\xff\xff\x00\x00\x00\x01" "0" "\x00\x12" "MIT-MAGIC-COOKIE-1" "\x00\x04" "ABCD", 33);
    Check("Xauthority: cookie found", CookieFromXauthority(file) == "ABCD");
    Check("Xauthority: empty file", CookieFromXauthority("").empty());
    Check("Xauthority: truncated file", CookieFromXauthority(file.substr(0, 20)).empty());

    return failures ? 1 : 0;
}

static int Live(int argc, char **argv)
{
    std::ifstream in(argv[4], std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    std::string cookie = CookieFromXauthority(contents.str());

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)atoi(argv[3]));
    inet_pton(AF_INET, argv[2], &addr.sin_addr);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (sockaddr *)&addr, sizeof addr) != 0) {
        perror("connect");
        return 1;
    }

    XClient x;
    if (!x.Open(fd, cookie)) {
        printf("open: %s\n", x.Error().c_str());
        return 1;
    }
    printf("connected: root 0x%x, %ux%u\n", x.Root(), x.RootWidth(), x.RootHeight());

    Appearance a;
    a.dpi = atoi(argv[5]);
    a.font = argv[6];
    a.cursorSize = atoi(argv[7]);
    a.preferDark = atoi(argv[8]) != 0;
    /* A recognisable palette: every colour distinct. */
    for (uint32_t i = 0; i < 16; i++)
        a.palette.push_back(0x102030 + i * 0x0d0b07);

    Publisher p;
    switch (p.Start(&x, a)) {
    case Publisher::kOtherManager:
        printf("another settings manager owns this display\n");
        return 3;
    case Publisher::kFailed:
        printf("failed: %s\n", x.Error().c_str());
        return 1;
    default:
        break;
    }
    printf("published; settings window 0x%x\n", p.Owner());
    fflush(stdout);

    int hold = argc > 9 ? atoi(argv[9]) : 0;
    for (int i = 0; i < hold; i++) {
        bool lost = false;
        if (!x.ReadEvents(p.Owner(), &lost)) {
            printf("connection closed\n");
            return 1;
        }
        if (lost) {
            printf("another manager took over\n");
            return 3;
        }
        if (i == hold / 2) {
            a.preferDark = !a.preferDark;
            printf("%s\n", p.Update(a) ? "updated: dark flipped" : "update failed");
            fflush(stdout);
        }
        sleep(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "unit")
        return Unit();
    if (argc >= 9 && std::string(argv[1]) == "live")
        return Live(argc, argv);
    fprintf(stderr, "usage: publish-test unit | live HOST PORT COOKIEFILE DPI FONT CURSORSIZE DARK [HOLD]\n");
    return 2;
}
