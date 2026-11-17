/*
 * See publish.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "publish.h"

#include <cstdio>

static std::string Hex(uint32_t rgb)
{
    char text[8];
    snprintf(text, sizeof text, "#%06x", (unsigned)(rgb & 0xffffff));
    return text;
}

std::string UiFontName(const std::string &family, int points)
{
    if (family.empty())
        return "";
    /* Selawik is Microsoft's open font drawn to Segoe UI's measurements;
     * Liberation Sans, with Arial's, is on most distributions.  Measured with
     * GTK at 9 points, a line of Segoe UI is 396 pixels in Selawik, 412 in
     * Liberation Sans and 455 in DejaVu Sans, fontconfig's usual fallback. */
    std::string name = family;
    if (family == "Segoe UI")
        name += ", Selawik";
    if (family != "Liberation Sans")
        name += ", Liberation Sans";
    return name + " " + std::to_string(points);
}

std::string Points(double points)
{
    char text[32];
    snprintf(text, sizeof text, "%g", (double)(long)(points * 100 + 0.5) / 100);
    return text;
}

std::vector<std::pair<std::string, std::string>> ResourcesFor(const Appearance &a)
{
    /* GDI hints glyphs fully, whether or not it smooths them, and with
     * ClearType only up and down; FreeType's full hinting does the same with
     * sub-pixel rendering. */
    std::vector<std::pair<std::string, std::string>> r = {
        { "Xft.dpi", std::to_string(a.dpi) },
        { "Xft.antialias", a.antialias ? "true" : "false" },
        { "Xft.hinting", "true" },
        { "Xft.hintstyle", "hintfull" },
        { "Xft.rgba", a.subpixel },
        { "Xft.lcdfilter", "lcddefault" },
        { "Xcursor.size", std::to_string(a.cursorSize) },
        /* Programs that ask for a pointer from the old cursor font (xterm's
         * text pointer, Xt and Motif programs) get the theme's one instead. */
        { "Xcursor.theme_core", "true" },
    };
    if (!a.cursorTheme.empty())
        r.push_back({ "Xcursor.theme", a.cursorTheme });
    if (a.palette.size() == 16) {
        /* Read by xterm and the terminals that follow its conventions. */
        for (int i = 0; i < 16; i++)
            r.push_back({ "*color" + std::to_string(i), Hex(a.palette[(size_t)i]) });
        /* Named per program: a bare "*background" would repaint every
         * old-style X program, not only terminals. */
        for (const char *program : { "XTerm", "UXTerm", "URxvt" }) {
            r.push_back({ std::string(program) + "*foreground", Hex(a.palette[(size_t)(a.foreground & 15)]) });
            r.push_back({ std::string(program) + "*background", Hex(a.palette[(size_t)(a.background & 15)]) });
        }
    }
    if (a.terminalPoints > 0) {
        std::string size = Points(a.terminalPoints);
        std::string geometry = std::to_string(a.terminalColumns) + "x" + std::to_string(a.terminalRows);
        std::string lines = std::to_string(a.terminalScrollback);
        std::string underline = a.terminalUnderlineCursor ? "true" : "false";
        /* A Windows console whose buffer is no taller than its window keeps no
         * history.  Passing that on would switch scrollback off in X
         * terminals, so the terminal's own default is left alone then. */
        /* A fontconfig pattern.  If the guest does not have the font, the
         * terminal falls back to its default one at this size. */
        std::string face = a.terminalFont + (a.terminalBold && !a.terminalFont.empty() ? ":bold" : "");

        for (const char *program : { "XTerm", "UXTerm" }) {
            std::string p = program;
            if (!face.empty())
                r.push_back({ p + "*faceName", face });
            r.push_back({ p + "*faceSize", size });
            if (a.terminalColumns > 0 && a.terminalRows > 0)
                r.push_back({ p + "*geometry", geometry });
            if (a.terminalScrollback > 0)
                r.push_back({ p + "*saveLines", lines });
            r.push_back({ p + "*cursorUnderLine", underline });
        }
        r.push_back({ "URxvt*font", "xft:" + (face.empty() ? std::string("monospace") : face) + ":size=" + size });
        if (a.terminalColumns > 0 && a.terminalRows > 0)
            r.push_back({ "URxvt*geometry", geometry });
        if (a.terminalScrollback > 0)
            r.push_back({ "URxvt*saveLines", lines });
        r.push_back({ "URxvt*cursorUnderline", underline });
    }
    return r;
}

Publisher::Result Publisher::Start(XClient *x, const Appearance &a)
{
    x_ = x;
    /* Screen 0's settings manager is whoever owns this selection. */
    XClient::Atom selection = x->InternAtom("_XSETTINGS_S0");
    XClient::Atom manager = x->InternAtom("MANAGER");
    settingsAtom_ = x->InternAtom("_XSETTINGS_SETTINGS");
    resourceAtom_ = x->InternAtom("RESOURCE_MANAGER");
    stringAtom_ = x->InternAtom("STRING");
    if (!selection || !manager || !settingsAtom_ || !resourceAtom_ || !stringAtom_)
        return kFailed;

    /* A desktop session in the guest may run its own settings daemon.  That is
     * the user's choice, and only one can own the display, so step aside. */
    if (x->GetSelectionOwner(selection) != 0)
        return x->IsOpen() ? kOtherManager : kFailed;

    owner_ = x->CreateHiddenWindow();
    /* The property must be in place before anyone is told to look at it. */
    if (!Write(a))
        return kFailed;
    x->SetSelectionOwner(selection, owner_);
    if (x->GetSelectionOwner(selection) != owner_)
        return x->IsOpen() ? kOtherManager : kFailed;
    x->AnnounceManager(manager, selection, owner_);
    return x->Sync() ? kPublished : kFailed;
}

bool Publisher::Update(const Appearance &a)
{
    return Write(a) && x_->Sync();
}

bool Publisher::Write(const Appearance &a)
{
    XSettings settings;
    settings.SetInt("Xft/DPI", a.dpi * 1024); /* in 1024ths of a dot per inch */
    settings.SetInt("Xft/Antialias", a.antialias ? 1 : 0);
    settings.SetInt("Xft/Hinting", 1);
    settings.SetString("Xft/HintStyle", "hintfull");
    settings.SetString("Xft/RGBA", a.subpixel);
    settings.SetInt("Gtk/CursorThemeSize", a.cursorSize);
    if (!a.cursorTheme.empty())
        settings.SetString("Gtk/CursorThemeName", a.cursorTheme);
    settings.SetInt("Gtk/ApplicationPreferDarkTheme", a.preferDark ? 1 : 0);
    if (!a.font.empty())
        settings.SetString("Gtk/FontName", a.font);
    if (!a.gtkTheme.empty())
        settings.SetString("Net/ThemeName", a.gtkTheme);

    settings.SetInt("Net/DoubleClickTime", a.doubleClickMs);
    settings.SetInt("Net/DoubleClickDistance", a.doubleClickDistance);
    settings.SetInt("Net/DndDragThreshold", a.dragThreshold);
    settings.SetInt("Net/CursorBlink", a.caretBlinkMs > 0 ? 1 : 0);
    if (a.caretBlinkMs > 0)
        settings.SetInt("Net/CursorBlinkTime", a.caretBlinkMs);
    settings.SetInt("Gtk/CursorBlinkTimeout", a.caretTimeoutSeconds);
    settings.SetInt("Gtk/EnableAnimations", a.animations ? 1 : 0);
    settings.SetInt("Gtk/AutoMnemonics", a.alwaysUnderlineAccessKeys ? 0 : 1); /* GTK 2 */

    /* How classic Windows programs behave, whatever the settings:
     * scroll bars that stay put, dialogs with a title bar and a row of
     * buttons, the window buttons on the right, a click in a scroll bar's
     * trough moving by a page, no pictures on buttons or in menus, and
     * small tool bars with pictures only (GTK 2). */
    settings.SetInt("Gtk/OverlayScrolling", 0);
    settings.SetInt("Gtk/DialogsUseHeader", 0);
    settings.SetString("Gtk/DecorationLayout", "icon:minimize,maximize,close");
    settings.SetInt("Gtk/PrimaryButtonWarpsSlider", 0);
    settings.SetInt("Gtk/ButtonImages", 0);
    settings.SetInt("Gtk/MenuImages", 0);
    settings.SetString("Gtk/ToolbarStyle", "icons");
    settings.SetString("Gtk/ToolbarIconSize", "small-toolbar");
    x_->ChangeProperty(owner_, settingsAtom_, settingsAtom_, settings.Encode(++serial_));

    /* X resources reach programs that do not read XSETTINGS.  Whatever else is
     * there, such as the user's own xrdb settings, is kept. */
    std::string before = x_->GetProperty(x_->Root(), resourceAtom_);
    std::string after = MergeResources(before, ResourcesFor(a));
    if (after != before)
        x_->ChangeProperty(x_->Root(), resourceAtom_, stringAtom_, after);
    return x_->IsOpen();
}
