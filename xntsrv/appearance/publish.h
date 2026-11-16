/*
 * Turning a set of appearance values into what X clients read: the XSETTINGS
 * property and the X resources on the root window.  Shared by the Windows
 * helper and the Linux test program.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "xclient.h"

struct Appearance {
    int dpi = 96;          /* dots per inch X programs should assume */
    std::string font;      /* UI font as "Family Size", e.g. "Segoe UI 9"; may be empty */
    int cursorSize = 32;   /* in pixels */
    bool preferDark = false;
    /* Terminal colours: 16 values as 0xRRGGBB in the order terminals number
     * them (black, red, green, yellow, blue, magenta, cyan, white, then the
     * bright ones), and which of them are the default text and background.
     * Empty when not known. */
    std::vector<uint32_t> palette;
    int foreground = 7, background = 0;
    /* The rest of the console's look, for terminals.  Unset when
     * terminalPoints is 0. */
    std::string terminalFont; /* family, e.g. "Consolas"; may be empty */
    bool terminalBold = false;
    int terminalPoints = 0;
    int terminalColumns = 0, terminalRows = 0;
    int terminalScrollback = 0;         /* lines kept above the window */
    bool terminalUnderlineCursor = false; /* otherwise a block */

    bool operator==(const Appearance &o) const
    {
        return dpi == o.dpi && font == o.font && cursorSize == o.cursorSize && preferDark == o.preferDark &&
               palette == o.palette && foreground == o.foreground && background == o.background &&
               terminalFont == o.terminalFont && terminalBold == o.terminalBold &&
               terminalPoints == o.terminalPoints && terminalColumns == o.terminalColumns &&
               terminalRows == o.terminalRows && terminalScrollback == o.terminalScrollback &&
               terminalUnderlineCursor == o.terminalUnderlineCursor;
    }
};

/* The X resources that carry `a`, as name and value pairs. */
std::vector<std::pair<std::string, std::string>> ResourcesFor(const Appearance &a);

class Publisher {
public:
    enum Result {
        kPublished,
        kOtherManager, /* another settings manager owns the display: left alone */
        kFailed,       /* the connection failed; see XClient::Error() */
    };

    /* Become the display's settings manager and publish `a`. */
    Result Start(XClient *x, const Appearance &a);
    /* Publish changed values. */
    bool Update(const Appearance &a);
    XClient::Window Owner() const { return owner_; }

private:
    bool Write(const Appearance &a);

    XClient *x_ = nullptr;
    XClient::Window owner_ = 0;
    XClient::Atom settingsAtom_ = 0, resourceAtom_ = 0, stringAtom_ = 0;
    uint32_t serial_ = 0;
};
