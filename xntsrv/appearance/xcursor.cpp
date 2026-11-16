/*
 * See xcursor.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "xcursor.h"

static void Put32(std::string *out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        out->push_back((char)(v >> (8 * i)));
}

std::string EncodeXcursor(const std::vector<CursorFrame> &frames)
{
    const uint32_t kImage = 0xfffd0002;
    const uint32_t kFileHeader = 16, kTocEntry = 12, kImageHeader = 36;

    if (frames.empty())
        return "";
    for (const CursorFrame &f : frames)
        if (!f.width || !f.height || f.width > 0x7fff || f.height > 0x7fff ||
            f.pixels.size() != (size_t)f.width * f.height || f.xhot >= f.width || f.yhot >= f.height)
            return "";

    /* Every frame of an animation carries the same nominal size; libXcursor
     * picks the size nearest the one the program asks for. */
    uint32_t nominal = frames[0].width > frames[0].height ? frames[0].width : frames[0].height;

    std::string out = "Xcur";
    Put32(&out, kFileHeader);
    Put32(&out, 0x10000); /* file format version */
    Put32(&out, (uint32_t)frames.size());

    uint32_t position = kFileHeader + kTocEntry * (uint32_t)frames.size();
    for (const CursorFrame &f : frames) {
        Put32(&out, kImage);
        Put32(&out, nominal);
        Put32(&out, position);
        position += kImageHeader + 4 * f.width * f.height;
    }
    for (const CursorFrame &f : frames) {
        Put32(&out, kImageHeader);
        Put32(&out, kImage);
        Put32(&out, nominal);
        Put32(&out, 1); /* image chunk version */
        Put32(&out, f.width);
        Put32(&out, f.height);
        Put32(&out, f.xhot);
        Put32(&out, f.yhot);
        Put32(&out, f.delayMs);
        for (uint32_t pixel : f.pixels)
            Put32(&out, pixel);
    }
    return out;
}

std::vector<uint32_t> PixelsFromTwoBackgrounds(const std::vector<uint32_t> &onBlack,
                                                const std::vector<uint32_t> &onWhite)
{
    std::vector<uint32_t> out(onBlack.size(), 0);

    for (size_t i = 0; i < onBlack.size() && i < onWhite.size(); i++) {
        /* For a colour c with opacity a: on black it shows c*a, on white
         * c*a + (1-a).  The difference gives the opacity, and the picture on
         * black is already the premultiplied colour. */
        int black = (int)((onBlack[i] >> 8) & 0xff), white = (int)((onWhite[i] >> 8) & 0xff);
        int alpha = 255 - (white - black);
        if (white < black) {
            out[i] = 0xff000000; /* inverted by Windows: solid black here */
            continue;
        }
        if (alpha <= 0)
            continue;
        if (alpha > 255)
            alpha = 255;
        uint32_t rgb = onBlack[i] & 0xffffff;
        /* No channel may exceed the opacity in a premultiplied pixel. */
        uint32_t r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff, a = (uint32_t)alpha;
        r = r > a ? a : r;
        g = g > a ? a : g;
        b = b > a ? a : b;
        out[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return out;
}

/* Windows has fourteen standard pointers; X programs ask for many more names.
 * Each Windows pointer is written under every name it can stand in for. */
const CursorRole kCursorRoles[] = {
    { 32512 /* arrow */, { "left_ptr", "default", "arrow", "top_left_arrow", "left-arrow" } },
    { 32513 /* text */, { "xterm", "text", "ibeam" } },
    { 32514 /* busy */, { "watch", "wait" } },
    { 32650 /* working in background */, { "left_ptr_watch", "progress", "half-busy" } },
    { 32515 /* precision */, { "crosshair", "cross", "tcross", "cell" } },
    { 32649 /* link */, { "hand2", "hand1", "hand", "pointer", "pointing_hand", "openhand", "grab" } },
    { 32651 /* help */, { "question_arrow", "help", "whats_this", "left_ptr_help" } },
    { 32648 /* unavailable */, { "not-allowed", "crossed_circle", "forbidden", "no-drop", "circle", "dnd-none" } },
    { 32645 /* vertical resize */, { "sb_v_double_arrow", "ns-resize", "size_ver", "v_double_arrow", "n-resize",
                                    "s-resize", "top_side", "bottom_side", "row-resize", "split_v", "double_arrow" } },
    { 32644 /* horizontal resize */, { "sb_h_double_arrow", "ew-resize", "size_hor", "h_double_arrow", "e-resize",
                                      "w-resize", "left_side", "right_side", "col-resize", "split_h" } },
    { 32642 /* diagonal resize 1 */, { "size_fdiag", "nwse-resize", "bd_double_arrow", "nw-resize", "se-resize",
                                      "top_left_corner", "bottom_right_corner" } },
    { 32643 /* diagonal resize 2 */, { "size_bdiag", "nesw-resize", "fd_double_arrow", "ne-resize", "sw-resize",
                                      "top_right_corner", "bottom_left_corner" } },
    { 32646 /* move */, { "fleur", "move", "size_all", "all-scroll", "grabbing", "closedhand", "dnd-move" } },
    { 32516 /* alternate select */, { "center_ptr", "up-arrow", "sb_up_arrow" } },
};
const int kCursorRoleCount = (int)(sizeof kCursorRoles / sizeof kCursorRoles[0]);
