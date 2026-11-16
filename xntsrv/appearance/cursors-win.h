/*
 * The Windows pointer scheme as an X cursor theme.  See cursors-win.cpp.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../common/xntsrv-common.h"

/* The theme's name, as X programs are told it. */
const char kCursorThemeName[] = "XNTsrv";

/* Where the theme lives: a folder in the user's XNTsrv data, laid out the way
 * X icon themes are, so a guest that can see it only needs it on its path. */
std::wstring CursorThemeDir();

/* Render every standard Windows pointer into `themeDir`.  Returns how many
 * pointers were converted (0 on failure) and, in *files, how many files that
 * made, since each is written under several names. */
int WriteCursorTheme(const std::wstring &themeDir, int *files);

std::wstring Wide(const char *text);
