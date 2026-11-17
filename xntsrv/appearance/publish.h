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
    std::string font;      /* UI font as "Families Size", e.g. "Segoe UI, Selawik, Liberation Sans 9"; may be empty */
    int cursorSize = 24;   /* nominal X cursor size; see XcursorNominal() */
    std::string cursorTheme; /* name of the X cursor theme to use; empty for the guest's own */
    bool preferDark = false;
    std::string gtkTheme;  /* name of the GTK theme to use; empty for the guest's own */
    /* Terminal colours: 16 values as 0xRRGGBB in the order terminals number
     * them (black, red, green, yellow, blue, magenta, cyan, white, then the
     * bright ones), and which of them are the default text and background.
     * Empty when not known. */
    std::vector<uint32_t> palette;
    /* How text is drawn: Windows' font smoothing.  `subpixel` is "rgb", "bgr"
     * or "none" (grey smoothing, or none at all). */
    bool antialias = true;
    std::string subpixel = "rgb";
    /* Mouse and keyboard timings, in Windows' own measures turned into GTK's. */
    int doubleClickMs = 500;
    int doubleClickDistance = 2; /* pixels either side of the first click */
    int dragThreshold = 4;       /* pixels either side before a drag starts */
    int caretBlinkMs = 1060;     /* a whole on-and-off cycle; 0 for a steady caret */
    int caretTimeoutSeconds = 5; /* the caret stops blinking after this long idle */
    bool animations = true;
    bool alwaysUnderlineAccessKeys = false;
    int foreground = 7, background = 0;
    /* The rest of the console's look, for terminals.  Unset when
     * terminalPoints is 0. */
    std::string terminalFont; /* family, e.g. "Consolas"; may be empty */
    bool terminalBold = false;
    double terminalPoints = 0; /* the font's em size; may be fractional, e.g. 10.5 */
    int terminalColumns = 0, terminalRows = 0;
    int terminalScrollback = 0;         /* lines kept above the window */
    bool terminalUnderlineCursor = false; /* otherwise a block */

    bool operator==(const Appearance &o) const
    {
        return dpi == o.dpi && font == o.font && cursorSize == o.cursorSize && preferDark == o.preferDark &&
               cursorTheme == o.cursorTheme && gtkTheme == o.gtkTheme &&
               palette == o.palette && foreground == o.foreground && background == o.background &&
               antialias == o.antialias && subpixel == o.subpixel && doubleClickMs == o.doubleClickMs &&
               doubleClickDistance == o.doubleClickDistance && dragThreshold == o.dragThreshold &&
               caretBlinkMs == o.caretBlinkMs && caretTimeoutSeconds == o.caretTimeoutSeconds &&
               animations == o.animations && alwaysUnderlineAccessKeys == o.alwaysUnderlineAccessKeys &&
               terminalFont == o.terminalFont && terminalBold == o.terminalBold &&
               terminalPoints == o.terminalPoints && terminalColumns == o.terminalColumns &&
               terminalRows == o.terminalRows && terminalScrollback == o.terminalScrollback &&
               terminalUnderlineCursor == o.terminalUnderlineCursor;
    }
};

/* The UI font as GTK takes it: `family` first, then fonts of similar size
 * that guests are likely to have, then the size in points.  Without them a
 * guest that lacks the Windows font falls back to a much wider one. */
std::string UiFontName(const std::string &family, int points);

/* A size in points as X programs take it: "12", "10.5", "9.75". */
std::string Points(double points);

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
