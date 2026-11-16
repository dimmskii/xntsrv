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

    bool operator==(const Appearance &o) const
    {
        return dpi == o.dpi && font == o.font && cursorSize == o.cursorSize && preferDark == o.preferDark;
    }
};

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
