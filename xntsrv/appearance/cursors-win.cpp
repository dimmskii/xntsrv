/*
 * Converts the user's Windows pointer scheme into an X cursor theme.
 *
 * Windows renders each of its standard pointers, at the size and in the
 * scheme the user has chosen, onto a black and onto a white background; the
 * two pictures give colour and transparency.  Animated pointers are rendered
 * frame by frame.  Nothing is decoded by hand and no image library is used.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cursors-win.h"

#include <shlobj.h>

#include "xcursor.h"

using std::wstring;

namespace {

/* A 32-bit drawing surface whose pixels can be read directly. */
struct Surface {
    HDC dc = NULL;
    HBITMAP bitmap = NULL, previous = NULL;
    uint32_t *bits = NULL;
    int width = 0, height = 0;

    bool Create(int w, int h)
    {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; /* top row first */
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(NULL);
        void *memory = NULL;
        bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &memory, NULL, 0) : NULL;
        if (!bitmap)
            return false;
        bits = (uint32_t *)memory;
        previous = (HBITMAP)SelectObject(dc, bitmap);
        width = w;
        height = h;
        return true;
    }

    ~Surface()
    {
        if (previous)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }

    /* Draw one frame of `cursor` on a plain background; false if Windows has
     * no such frame. */
    bool Draw(HCURSOR cursor, UINT frame, uint32_t background, std::vector<uint32_t> *out)
    {
        size_t count = (size_t)width * height;
        for (size_t i = 0; i < count; i++)
            bits[i] = background;
        GdiFlush();
        if (!DrawIconEx(dc, 0, 0, cursor, width, height, frame, NULL, DI_NORMAL))
            return false;
        GdiFlush();
        out->resize(count);
        for (size_t i = 0; i < count; i++)
            (*out)[i] = bits[i] & 0xffffff;
        return true;
    }
};

std::vector<CursorFrame> RenderCursor(HCURSOR cursor)
{
    std::vector<CursorFrame> frames;
    ICONINFO info = {};
    BITMAP bitmap = {};
    int width = 0, height = 0;

    if (!GetIconInfo(cursor, &info))
        return frames;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof bitmap, &bitmap)) {
        width = bitmap.bmWidth;
        height = bitmap.bmHeight;
    } else if (info.hbmMask && GetObjectW(info.hbmMask, sizeof bitmap, &bitmap)) {
        width = bitmap.bmWidth;
        height = bitmap.bmHeight / 2; /* a monochrome pointer stacks two masks */
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);

    Surface surface;
    if (width < 8 || height < 8 || width > 512 || height > 512 || !surface.Create(width, height))
        return frames;

    std::vector<uint32_t> first;
    for (UINT step = 0; step < 64; step++) {
        std::vector<uint32_t> onBlack, onWhite;
        if (!surface.Draw(cursor, step, 0x000000, &onBlack) || !surface.Draw(cursor, step, 0xffffff, &onWhite))
            break;
        /* A still pointer draws the same picture for every frame number, and
         * an animation comes round to its first frame again. */
        if (step == 0)
            first = onBlack;
        else if (onBlack == first)
            break;

        CursorFrame frame;
        frame.width = (uint32_t)width;
        frame.height = (uint32_t)height;
        frame.xhot = info.xHotspot < (DWORD)width ? info.xHotspot : 0;
        frame.yhot = info.yHotspot < (DWORD)height ? info.yHotspot : 0;
        frame.pixels = PixelsFromTwoBackgrounds(onBlack, onWhite);
        frames.push_back(frame);
    }
    /* Windows does not say how fast an animation runs; this is its usual pace. */
    if (frames.size() > 1)
        for (CursorFrame &frame : frames)
            frame.delayMs = 50;

    bool visible = false;
    for (const CursorFrame &frame : frames)
        for (uint32_t pixel : frame.pixels)
            visible = visible || (pixel >> 24) != 0;
    if (!visible)
        frames.clear();
    return frames;
}

bool WriteFile(const wstring &path, const std::string &contents)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = ::WriteFile(file, contents.data(), (DWORD)contents.size(), &written, NULL) && written == contents.size();
    CloseHandle(file);
    return ok;
}

} /* namespace */

wstring CursorThemeDir()
{
    PWSTR base = NULL;
    wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &base)))
        dir = wstring(base) + L"\\" + xnt::kUserDataDir + L"\\share\\icons\\" + Wide(kCursorThemeName);
    CoTaskMemFree(base);
    return dir;
}

wstring Wide(const char *text)
{
    wstring out;
    for (; *text; text++)
        out.push_back((wchar_t)(unsigned char)*text); /* ASCII names only */
    return out;
}

int WriteCursorTheme(const wstring &themeDir, int *files)
{
    wstring cursors = themeDir + L"\\cursors";
    int pointers = 0;

    *files = 0;
    SHCreateDirectoryExW(NULL, cursors.c_str(), NULL);
    for (int i = 0; i < kCursorRoleCount; i++) {
        /* The shared system pointer: whatever the user's scheme and size are. */
        HCURSOR cursor = LoadCursorW(NULL, MAKEINTRESOURCEW(kCursorRoles[i].windowsId));
        std::string contents = cursor ? EncodeXcursor(RenderCursor(cursor)) : "";
        if (contents.empty())
            continue;
        pointers++;
        for (const char *const *name = kCursorRoles[i].names; *name; name++)
            if (WriteFile(cursors + L"\\" + Wide(*name), contents))
                (*files)++;
    }
    if (!pointers)
        return 0;
    WriteFile(themeDir + L"\\index.theme",
              std::string("[Icon Theme]\nName=") + kCursorThemeName +
                  "\nComment=Your Windows pointer scheme, converted by XNTsrv\n");
    return pointers;
}
