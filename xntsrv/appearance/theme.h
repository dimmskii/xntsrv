/*
 * The colour files of the XNTsrv GTK themes, generated from the classic
 * Windows system colours.  The themes themselves are static files (theme/);
 * only these change with the user's colours.  Portable.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <string>

/* The classic Windows colours a theme needs, each as 0xRRGGBB. */
struct ClassicColors {
    uint32_t window = 0xffffff, windowText = 0x000000;       /* text areas, lists */
    uint32_t face = 0xf0f0f0, faceText = 0x000000;           /* dialogs, buttons */
    uint32_t shadow = 0xa0a0a0, darkShadow = 0x696969, light = 0xe3e3e3; /* edges */
    uint32_t selection = 0x0078d7, selectionText = 0xffffff;
    uint32_t menu = 0xf0f0f0, menuText = 0x000000;
    uint32_t tooltip = 0xffffe1, tooltipText = 0x000000;
    uint32_t grayText = 0x6d6d6d;
    uint32_t link = 0x0066cc;
    uint32_t captionText = 0x000000;
};

/* colors.css for the GTK 3 theme. */
std::string Gtk3Colors(const ClassicColors &c);
/* colors.rc for the GTK 2 theme. */
std::string Gtk2Colors(const ClassicColors &c);
