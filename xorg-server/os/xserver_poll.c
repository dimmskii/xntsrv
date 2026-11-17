/*---------------------------------------------------------------------------*\
  $Id$

  NAME

	poll - select(2)-based poll() emulation function for BSD systems.

  SYNOPSIS
	#include "poll.h"

	struct pollfd
	{
	    int     fd;
	    short   events;
	    short   revents;
	}

	int poll (struct pollfd *pArray, unsigned long n_fds, int timeout)

  DESCRIPTION

	This file, and the accompanying "poll.h", implement the System V
	poll(2) system call for BSD systems (which typically do not provide
	poll()).  Poll() provides a method for multiplexing input and output
	on multiple open file descriptors; in traditional BSD systems, that
	capability is provided by select().  While the semantics of select()
	differ from those of poll(), poll() can be readily emulated in terms
	of select() -- which is how this function is implemented.

  REFERENCES
	Stevens, W. Richard. Unix Network Programming.  Prentice-Hall, 1990.

  NOTES
	1. This software requires an ANSI C compiler.

  LICENSE

  This software is released under the following BSD license, adapted from
  http://opensource.org/licenses/bsd-license.php

  Copyright (c) 1995-2011, Brian M. Clapper
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  * Neither the name of the clapper.org nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*\
				 Includes
\*---------------------------------------------------------------------------*/

#include <dix-config.h>

#include <unistd.h>			     /* standard Unix definitions */
#include <sys/types.h>                       /* system types */
#include <sys/time.h>                        /* time definitions */
#include <assert.h>                          /* assertion macros */
#include <string.h>                          /* string functions */

#if defined(WIN32) && !defined(__CYGWIN__)
#include <X11/Xwinsock.h>
#endif

#include "os/xserver_poll.h"

/*---------------------------------------------------------------------------*\
				  Macros
\*---------------------------------------------------------------------------*/

#ifndef MAX
#define MAX(a,b)	((a) > (b) ? (a) : (b))
#endif

/*---------------------------------------------------------------------------*\
			     Private Functions
\*---------------------------------------------------------------------------*/

static int map_poll_spec
			(struct pollfd *pArray,
                         nfds_t         n_fds,
			  fd_set        *pReadSet,
			  fd_set        *pWriteSet,
			  fd_set        *pExceptSet)
{
    register nfds_t  i;                      /* loop control */
    register struct  pollfd *pCur;           /* current array element */
    register int     max_fd = -1;            /* return value */

    /*
       Map the poll() structures into the file descriptor sets required
       by select().
    */
    for (i = 0, pCur = pArray; i < n_fds; i++, pCur++)
    {
        /* Skip any bad FDs in the array. */

        if (pCur->fd < 0)
            continue;

	if (pCur->events & POLLIN)
	{
	    /* "Input Ready" notification desired. */
	    FD_SET (pCur->fd, pReadSet);
	}

	if (pCur->events & POLLOUT)
	{
	    /* "Output Possible" notification desired. */
	    FD_SET (pCur->fd, pWriteSet);
	}

	if (pCur->events & POLLPRI)
	{
	    /*
	       "Exception Occurred" notification desired.  (Exceptions
	       include out of band data.
	    */
	    FD_SET (pCur->fd, pExceptSet);
	}

	max_fd = MAX (max_fd, pCur->fd);
    }

    return max_fd;
}

static struct timeval *map_timeout
			(int poll_timeout, struct timeval *pSelTimeout)
{
    struct timeval *pResult;

    /*
       Map the poll() timeout value into a select() timeout.  The possible
       values of the poll() timeout value, and their meanings, are:

       VALUE	MEANING

       -1	wait indefinitely (until signal occurs)
        0	return immediately, don't block
       >0	wait specified number of milliseconds

       select() uses a "struct timeval", which specifies the timeout in
       seconds and microseconds, so the milliseconds value has to be mapped
       accordingly.
    */

    assert (pSelTimeout != (struct timeval *) NULL);

    switch (poll_timeout)
    {
	case -1:
	    /*
	       A NULL timeout structure tells select() to wait indefinitely.
	    */
	    pResult = (struct timeval *) NULL;
	    break;

	case 0:
	    /*
	       "Return immediately" (test) is specified by all zeros in
	       a timeval structure.
	    */
	    pSelTimeout->tv_sec  = 0;
	    pSelTimeout->tv_usec = 0;
	    pResult = pSelTimeout;
	    break;

	default:
	    /* Wait the specified number of milliseconds. */
	    pSelTimeout->tv_sec  = poll_timeout / 1000; /* get seconds */
	    poll_timeout        %= 1000;                /* remove seconds */
	    pSelTimeout->tv_usec = poll_timeout * 1000; /* get microseconds */
	    pResult = pSelTimeout;
	    break;
    }


    return pResult;
}

static void map_select_results
			 (struct pollfd *pArray,
			  unsigned long  n_fds,
			  fd_set        *pReadSet,
			  fd_set        *pWriteSet,
			  fd_set        *pExceptSet)
{
    register unsigned long  i;                   /* loop control */
    register struct	    pollfd *pCur;        /* current array element */

    for (i = 0, pCur = pArray; i < n_fds; i++, pCur++)
    {
        /* Skip any bad FDs in the array. */

        if (pCur->fd < 0)
            continue;

	/* Exception events take priority over input events. */

	pCur->revents = 0;
	if (FD_ISSET (pCur->fd, pExceptSet))
	    pCur->revents |= POLLPRI;

	else if (FD_ISSET (pCur->fd, pReadSet))
	    pCur->revents |= POLLIN;

	if (FD_ISSET (pCur->fd, pWriteSet))
	    pCur->revents |= POLLOUT;
    }

    return;
}

/*---------------------------------------------------------------------------*\
			     Public Functions
\*---------------------------------------------------------------------------*/

#if defined(WIN32) && !defined(__CYGWIN__)
/*
 * XNTsrv: waiting for sockets and Windows messages together.
 *
 * select() can only wait for sockets, so the server used to wait in it for a
 * millisecond at a time and look at its Windows message queue in between;
 * Windows rounds such short waits up to its timer tick, about 15 ms, so
 * mouse and keyboard input could wait that long.  Here every socket the
 * server polls is tied to one event object (WSAEventSelect), for just the
 * events it is waiting for, and MsgWaitForMultipleObjectsEx waits for that
 * event and for new Windows messages at once.  select() with no timeout
 * still decides what is ready, so the event only ever wakes the server: a
 * stray signal costs one extra pass, never a missed socket.
 *
 * The ties are made by ospoll when the server starts or stops waiting on a
 * socket (xserver_poll_watch), not before every wait: WSAEventSelect is a
 * slow call, and the server waits hundreds of times a second while a large
 * request arrives in pieces.
 */

/* Upper bound on any one wait, as a safety net: if a wake-up were ever
 * missed, the server would still look around this often. */
#define XNT_MAX_WAIT_MS 100

static HANDLE xnt_socket_event;
static int xnt_watch_failed;    /* a socket could not be tied: wait the old way */

void
xserver_poll_watch(int fd, short events)
{
    long want = 0;

    if (fd < 0)
        return;
    if (!xnt_socket_event)
        xnt_socket_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!xnt_socket_event) {
        xnt_watch_failed = 1;
        return;
    }
    if (events & (POLLIN | POLLPRI))
        want |= FD_READ | FD_ACCEPT | FD_CLOSE | FD_OOB;
    if (events & POLLOUT)
        want |= FD_WRITE | FD_CONNECT | FD_CLOSE;
    /* With no events this unties the socket: one the server is ignoring for
     * now cannot keep waking it.  Failing to untie a socket that has just
     * been closed does no harm. */
    if (WSAEventSelect((SOCKET) fd, xnt_socket_event, want) != 0 && want)
        xnt_watch_failed = 1;
}

static int xnt_select(struct pollfd *pArray, unsigned long n_fds, int timeout);

int xserver_poll
	(struct pollfd *pArray, unsigned long n_fds, int timeout)
{
    int ready;
    DWORD wait;

    if (timeout == 0)
        return xnt_select(pArray, n_fds, 0);
    /* The old way, if the sockets are not all watched: wait in select() a
     * millisecond at a time, so that Windows messages are still seen. */
    if (!xnt_socket_event || xnt_watch_failed)
        return xnt_select(pArray, n_fds, timeout < 0 || timeout > 1 ? 1 : timeout);

    /* Cleared before looking, so that anything arriving from now on either
     * shows in this look or sets the event. */
    ResetEvent(xnt_socket_event);
    ready = xnt_select(pArray, n_fds, 0);
    if (ready != 0)
        return ready;

    wait = timeout < 0 || timeout > XNT_MAX_WAIT_MS ? XNT_MAX_WAIT_MS : (DWORD) timeout;
    MsgWaitForMultipleObjectsEx(1, &xnt_socket_event, wait, QS_ALLINPUT, 0);
    return xnt_select(pArray, n_fds, 0);
}

static int xnt_select
	(struct pollfd *pArray, unsigned long n_fds, int timeout)
#else
int xserver_poll
	(struct pollfd *pArray, unsigned long n_fds, int timeout)
#endif
{
    fd_set  read_descs;                          /* input file descs */
    fd_set  write_descs;                         /* output file descs */
    fd_set  except_descs;                        /* exception descs */
    struct  timeval stime;                       /* select() timeout value */
    int	    ready_descriptors;                   /* function result */
    int	    max_fd;                              /* maximum fd value */
    struct  timeval *pTimeout;                   /* actually passed */

    FD_ZERO (&read_descs);
    FD_ZERO (&write_descs);
    FD_ZERO (&except_descs);

    assert (pArray != (struct pollfd *) NULL);

    /* Map the poll() file descriptor list in the select() data structures. */

    max_fd = map_poll_spec (pArray, n_fds,
			    &read_descs, &write_descs, &except_descs);

    /* Map the poll() timeout value in the select() timeout structure. */

    pTimeout = map_timeout (timeout, &stime);

    /* Make the select() call. */

    ready_descriptors = select (max_fd + 1, &read_descs, &write_descs,
				&except_descs, pTimeout);

    if (ready_descriptors >= 0)
    {
	map_select_results (pArray, n_fds,
			    &read_descs, &write_descs, &except_descs);
    }

    return ready_descriptors;
}
