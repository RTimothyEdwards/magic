/* grLock.c -
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1985, 1990 Regents of the University of California. * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 *
 * This file contains information related to the locking of windows.
 * Windows are locked before any redisplay is done in them, and then
 * unlocked afterwards.  The procedures grSimpleLock and grSimpleUnlock
 * are provided for device drivers that want to ingore the locking stuff --
 * they can just point GrLockPtr and GrUnlockPtr to them.  More sophisticated
 * drivers (such as for the Sun 160) will want to do some of their own
 * locking themselves.
 *
 * Locking sets up a clipping region (grCurClip and grCurObscure) that is
 * used by the graphics drawing routines to clip things being displayed.
 * It is an error to draw stuff without locking a window first, and device
 * drivers should check for this case by using the macro GR_CHECK_LOCK().
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grLock.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "textio/textio.h"

MagWindow *grLockedWindow = NULL;	/* The window that we are redisplaying */
bool grLockScreen = FALSE;	/* Full screen access? */
bool grTraceLocks = FALSE;	/* For debugging */
bool grLockBorder;		/* Redrawing the border of the window too? */

Rect grCurClip;			/* Clip all output to this rectangle */
LinkedRect * grCurObscure;	/* A list of obscuring areas */

/*
 * ----------------------------------------------------------------------------
 * grWindName --
 *
 *	Return a string describing the window, either the window caption,
 *	or "NULL" for a null MagWindow pointer, or "FULL_SCREEN" when
 *	w is set to GR_LOCK_SCREEN.
 *
 * Results:
 *	A string representing the window or window caption
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

static char *
grWindName(w)
    MagWindow *w;
{
    if (w == NULL) return "<NULL>";
    if (w == GR_LOCK_SCREEN) return "<FULL-SCREEN>";
    return w->w_caption;
}

/*
 * ----------------------------------------------------------------------------
 * grSimpleLock & grSimpleUnlock --
 *
 *	Handy procedures for device drivers.
 *
 *	Lock a window so that we may safely write to it.  Locking is
 *	required before any operation that may modify the window's pixels.
 *	Locking is slow and should be done around large blocks of code.
 *
 *	Only one window may be locked at a time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores away the window and clipping info.
 * ----------------------------------------------------------------------------
 */

void
grSimpleLock(w, inside)
    MagWindow *w;		/* The window to lock, or GR_LOCK_SCREEN if the 
			 * whole screen.
			 */
    bool inside;	/* If TRUE, clip to inside of window, otherwise clip
			 * to outside of window.
			 */
{
    ASSERT(w != NULL, "grSimpleLock");
    grLockScreen = (w == GR_LOCK_SCREEN);
    if (grTraceLocks) TxError("--- Lock %s\n", grWindName(w));
    if (grLockScreen) {
	grCurClip = GrScreenRect;
	grCurObscure = NULL;
    } else {
	if (grLockedWindow != NULL) {
	    TxError("Magic error: Attempt to lock more than one window!\n");
	    TxError("Currently locked window is: '%s'\n", 
		grWindName(grLockedWindow));
	    TxError("Window to be locked is: '%s'\n", grWindName(w));
	    /* dump core here */
	    ASSERT(grLockedWindow == NULL, "grSimpleUnlock");
	}
	if (inside)
	    grCurClip = w->w_screenArea;
	else
	    grCurClip = w->w_allArea;
	grCurObscure = w->w_clipAgainst;
    }
    grLockBorder = !inside;
    grLockedWindow = w;
    GeoClip(&grCurClip, &GrScreenRect);
}


void
grSimpleUnlock(w)
    MagWindow *w;
{
    ASSERT(w != NULL, "grSimpleUnlock");
    if (grTraceLocks) TxError("--- Unlock %s\n", grWindName(w));
    if (w != grLockedWindow) {
	TxError("Magic error: Attempt to unlock a window that wasn't locked\n");
	TxError("Currently locked window is: '%s'\n", 
	    grWindName(grLockedWindow));
	TxError("Window to be unlocked is: '%s'\n", grWindName(w));
	/* dump core here */
	ASSERT(w == grLockedWindow, "grSimpleUnlock");
    }
    grLockedWindow = NULL;
    grLockScreen = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 * GrClipTo
 *
 *	Further restrict the clipping area to a portion of a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
GrClipTo(r)
    Rect *r;
{
    if (grLockedWindow == NULL) return;
    if (grLockScreen)
	grCurClip = GrScreenRect;
    else if (grLockBorder)
	grCurClip = grLockedWindow->w_allArea;
    else
	grCurClip = grLockedWindow->w_screenArea;
    GeoClip(&grCurClip, r);
    GeoClip(&grCurClip, &GrScreenRect);
}

/*
 * ----------------------------------------------------------------------------
 * grNoLock
 *
 *	We have encountered a bad lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Complains or aborts.
 * ----------------------------------------------------------------------------
 */

void
grNoLock()
{
    TxError("Magic error: Attempt to draw graphics without a window lock.\n");
    /*** ASSERT(FALSE, "grNoLock"); ***/
}

/*
 * ----------------------------------------------------------------------------
 * GrHaveLock()
 *
 *	Do we have a window lock?
 *
 * Results:
 *	boolean.
 *
 * Side effects:
 *	none.
 * ----------------------------------------------------------------------------
 */

bool
GrHaveLock()
{
    return (grLockedWindow != NULL);
}
