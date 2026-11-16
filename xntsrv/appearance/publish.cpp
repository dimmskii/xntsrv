/*
 * See publish.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "publish.h"

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
    settings.SetInt("Gtk/CursorThemeSize", a.cursorSize);
    settings.SetInt("Gtk/ApplicationPreferDarkTheme", a.preferDark ? 1 : 0);
    if (!a.font.empty())
        settings.SetString("Gtk/FontName", a.font);
    x_->ChangeProperty(owner_, settingsAtom_, settingsAtom_, settings.Encode(++serial_));

    /* X resources reach programs that do not read XSETTINGS.  Whatever else is
     * there, such as the user's own xrdb settings, is kept. */
    std::string before = x_->GetProperty(x_->Root(), resourceAtom_);
    std::string after = MergeResources(before, {
        { "Xft.dpi", std::to_string(a.dpi) },
        { "Xcursor.size", std::to_string(a.cursorSize) },
    });
    if (after != before)
        x_->ChangeProperty(x_->Root(), resourceAtom_, stringAtom_, after);
    return x_->IsOpen();
}
