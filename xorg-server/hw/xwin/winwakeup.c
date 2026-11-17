/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL THE XFREE86 PROJECT BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the XFree86 Project
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from the XFree86 Project.
 *
 * Authors:	Dakshinamurthy Karra
 *		Suhaib M Siddiqi
 *		Peter Busch
 *		Harold L Hunt II
 */

#ifdef HAVE_XWIN_CONFIG_H
#include <xwin-config.h>
#endif
#include "win.h"

/* XNTsrv: at most this many Windows messages are handled per wakeup, so
 * that X clients are still served during a flood of them. */
#define WIN_MESSAGES_PER_WAKEUP 64

/* Set when messages were left in the queue; see winBlockHandler. */
Bool g_fWinMessagesLeft = FALSE;

/* See Porting Layer Definition - p. 7 */
void
winWakeupHandler(ScreenPtr pScreen, int iResult)
{
    MSG msg;
    int n;

    /* XNTsrv: handle every message waiting, not one per pass.  With one per
     * pass, a key press (WM_KEYDOWN, WM_CHAR, WM_KEYUP) needed three passes
     * through the server's main loop.  Emptying the queue also matters to
     * the wait in xserver_poll(), which wakes only for messages that
     * arrived since the queue was last looked at. */
    for (n = 0; n < WIN_MESSAGES_PER_WAKEUP; n++) {
        if (!PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            break;
        if ((g_hDlgDepthChange == 0
             || !IsDialogMessage(g_hDlgDepthChange, &msg))
            && (g_hDlgExit == 0 || !IsDialogMessage(g_hDlgExit, &msg))
            && (g_hDlgAbout == 0 || !IsDialogMessage(g_hDlgAbout, &msg))) {
            DispatchMessage(&msg);
        }
    }
    g_fWinMessagesLeft = n == WIN_MESSAGES_PER_WAKEUP;
}
