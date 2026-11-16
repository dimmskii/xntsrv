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
