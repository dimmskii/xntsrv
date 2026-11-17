/*
 * Writing X cursor files (the format libXcursor reads), and the names X
 * programs ask for.  Portable; the Windows part that produces the images is
 * in cursors-win.cpp.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CursorFrame {
    uint32_t width = 0, height = 0;
    uint32_t xhot = 0, yhot = 0;
    uint32_t delayMs = 0;         /* how long to show this frame; 0 for a still cursor */
    std::vector<uint32_t> pixels; /* width * height, 0xAARRGGBB with colour premultiplied by alpha */
};

/* The X cursor size that matches a Windows pointer of `windowsPixels`.
 *
 * TEMPORARY, until the server shows Windows' own pointers.  The two count
 * differently: a Windows pointer of 32 pixels draws its arrow about 19
 * pixels tall, and X cursor themes draw that arrow at their size 24.  Asking
 * guests for 32 gets their themes' larger pointers, half as big again.  Our
 * own theme's files carry the same number, so they are still an exact match
 * and are never resized by libXcursor. */
inline uint32_t XcursorNominal(uint32_t windowsPixels) { return (windowsPixels * 3 + 2) / 4; }

/* The contents of an X cursor file holding these frames, all of one nominal
 * size.  Empty if the frames are unusable. */
std::string EncodeXcursor(const std::vector<CursorFrame> &frames);

/* Combine a picture drawn on black and the same picture drawn on white into
 * one with transparency.  `onBlack` and `onWhite` are 0x00RRGGBB.  Pixels a
 * Windows cursor inverts (white on black, black on white) become solid black,
 * the X convention for such cursors. */
std::vector<uint32_t> PixelsFromTwoBackgrounds(const std::vector<uint32_t> &onBlack,
                                                const std::vector<uint32_t> &onWhite);

/* One Windows pointer and the file names X programs look for it under. */
struct CursorRole {
    int windowsId; /* the IDC_ resource number, e.g. 32512 for the arrow */
    const char *names[16];
};
extern const CursorRole kCursorRoles[];
extern const int kCursorRoleCount;
