/*
 * See theme-win.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "theme-win.h"

#include <shlobj.h>

#include "cursors-win.h"
#include "theme.h"

using std::wstring;

/* A classic Windows colour, as 0xRRGGBB. */
static uint32_t SystemColor(int index)
{
    COLORREF c = GetSysColor(index);
    return ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c);
}

static ClassicColors ReadColors()
{
    ClassicColors c;
    c.window = SystemColor(COLOR_WINDOW);
    c.windowText = SystemColor(COLOR_WINDOWTEXT);
    c.face = SystemColor(COLOR_3DFACE);
    c.faceText = SystemColor(COLOR_BTNTEXT);
    c.shadow = SystemColor(COLOR_3DSHADOW);
    c.darkShadow = SystemColor(COLOR_3DDKSHADOW);
    c.light = SystemColor(COLOR_3DLIGHT);
    c.selection = SystemColor(COLOR_HIGHLIGHT);
    c.selectionText = SystemColor(COLOR_HIGHLIGHTTEXT);
    c.menu = SystemColor(COLOR_MENU);
    c.menuText = SystemColor(COLOR_MENUTEXT);
    c.tooltip = SystemColor(COLOR_INFOBK);
    c.tooltipText = SystemColor(COLOR_INFOTEXT);
    c.grayText = SystemColor(COLOR_GRAYTEXT);
    c.link = SystemColor(COLOR_HOTLIGHT);
    c.captionText = SystemColor(COLOR_CAPTIONTEXT);
    return c;
}

static bool WriteText(const wstring &path, const std::string &contents)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(file, contents.data(), (DWORD)contents.size(), &written, NULL) && written == contents.size();
    CloseHandle(file);
    return ok;
}

wstring GtkThemeDir()
{
    PWSTR base = NULL;
    wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base)))
        dir = wstring(base) + L"\\" + xnt::kUserDataDir + L"\\share\\themes\\" + Wide(kGtkThemeName);
    CoTaskMemFree(base);
    return dir;
}

bool WriteGtkTheme(const wstring &sourceDir, const wstring &themeDir)
{
    ClassicColors colors = ReadColors();
    bool ok = true;

    SHCreateDirectoryExW(NULL, (themeDir + L"\\gtk-3.0").c_str(), NULL);
    SHCreateDirectoryExW(NULL, (themeDir + L"\\gtk-2.0").c_str(), NULL);
    for (const wchar_t *file : { L"index.theme", L"gtk-3.0\\gtk.css", L"gtk-2.0\\gtkrc" })
        ok = CopyFileW((sourceDir + L"\\" + file).c_str(), (themeDir + L"\\" + file).c_str(), FALSE) && ok;
    ok = WriteText(themeDir + L"\\gtk-3.0\\colors.css", Gtk3Colors(colors)) && ok;
    ok = WriteText(themeDir + L"\\gtk-2.0\\colors.rc", Gtk2Colors(colors)) && ok;
    return ok;
}
