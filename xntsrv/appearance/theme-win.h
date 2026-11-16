/*
 * Installing the XNTsrv GTK themes into the user's data folder, with colour
 * files generated from the Windows system colours.  See theme-win.cpp.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../common/xntsrv-common.h"

/* The theme's name, as GTK programs are told it. */
const char kGtkThemeName[] = "XNTsrv";

/* Where the themes live in the user's XNTsrv data, laid out the way a
 * "themes" folder is on Linux, so a guest only needs it on its data path. */
std::wstring GtkThemeDir();

/* Copy the static theme files from `sourceDir` (the "theme" folder beside
 * the programs) into `themeDir` and write the colour files from the current
 * Windows colours.  Returns false if the theme is not usable afterwards. */
bool WriteGtkTheme(const std::wstring &sourceDir, const std::wstring &themeDir);
