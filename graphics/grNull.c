/*
 * grNull.c -
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
 * This file contains dummy functions for use when there is no
 * graphics display.
 */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/signals.h"


#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/graphics/grNull.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */


/* Forward declarations */
extern bool nullReturnFalse();
extern void nullDoNothing();
extern int nullReturnZero();

/*
 *---------------------------------------------------------
 *
 * nullDoNothing --
 *
 * This procedure does nothing.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

void
nullDoNothing()
{
}

/*
 * Typed no-op stubs for WASM call_indirect type compatibility.
 * WASM enforces exact type signatures at indirect call sites; assigning
 * a 0-arg nullDoNothing to a pointer called with arguments causes a
 * "null function or function signature mismatch" trap.  These stubs
 * have the correct arity so the WASM type check passes.
 */

/* 1-argument stub (int or pointer) */
static void
nullDoNothingI(int a)
{
    (void) a;
}

/* 2-argument stub */
static void
nullDoNothingII(int a, int b)
{
    (void) a; (void) b;
}

/* 4-argument stub */
static void
nullDoNothingIIII(int a, int b, int c, int d)
{
    (void) a; (void) b; (void) c; (void) d;
}

/* 7-argument stub (for grFontTextPtr) */
static void
nullDoNothingIIIIIII(int a, int b, int c, int d, int e, int f, int g)
{
    (void) a; (void) b; (void) c; (void) d; (void) e; (void) f; (void) g;
}

/* bool-returning stubs — return FALSE so callers treat backing store / window
 * creation as unavailable, which is correct for the headless null driver. */
static bool
nullReturnFalseI(int a)
{
    (void) a;
    return FALSE;
}

static bool
nullReturnFalseII(int a, int b)
{
    (void) a; (void) b;
    return FALSE;
}

/* 3-argument bool-returning stub (for grDrawGridPtr) */
static bool
nullReturnFalseIII(int a, int b, int c)
{
    (void) a; (void) b; (void) c;
    return FALSE;
}

/* 1-argument int-returning stub (for GrWindowIdPtr) */
static int
nullReturnZeroI(int a)
{
    (void) a;
    return 0;
}

/*
 *---------------------------------------------------------
 *
 * nullReturnFalse --
 *
 * This procedure does nothing, and returns FALSE.
 *
 * Results:
 *	Returns FALSE always.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

bool
nullReturnFalse()
{
    return (FALSE);
}

/*
 *---------------------------------------------------------
 *
 * nullReturnZero --
 *
 * This procedure does nothing, and returns 0.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

int
nullReturnZero()
{
    return (0);
}

/*
 *---------------------------------------------------------
 *
 * NullInit --
 *
 * NullInit doesn't do much of anything.
 *
 * Results:
 *	TRUE always.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

bool
NullInit()
{
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NullTextSize --
 *
 *	Determine the size of a text string.
 *
 * Results:
 *	Returns 0 indicating 'r' has been updated.
 *
 * Side effects:
 *	A rectangle is filled in that is the size of the text in pixels.
 *	The origin (0, 0) of this rectangle is located on the baseline
 *	at the far left side of the string.
 *
 * ----------------------------------------------------------------------------
 */

int
NullTextSize(text, size, r)
    char *text;
    int size;
    Rect *r;
{
    ASSERT(r != NULL, "nullTextSize");
    r->r_xbot = 0;
    r->r_xtop = strlen(text);
    r->r_ybot = 0;
    r->r_ytop = 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nullStdin --
 *
 *      Handle the stdin device for the NULL driver.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Adds events to the event queue.
 *
 * ----------------------------------------------------------------------------
 */

/** @typedef cb_textio_input_t */
void
nullStdin(
    int fd,
    ClientData cdata) /* notused */
{
    int ch;
    TxInputEvent *event;

    ch = getc(stdin);
    event = TxNewEvent();
    if (ch == EOF)
	event->txe_button = TX_EOF;
    else
	event->txe_button = TX_NO_BUTTON;
    event->txe_buttonAction = 0;
    event->txe_ch = ch;
    event->txe_wid = WIND_UNKNOWN_WINDOW;
    event->txe_p.p_x = GR_CURSOR_X;
    event->txe_p.p_y = GR_CURSOR_Y;
    TxAddEvent(event);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NullBitBlt --
 *
 *	A no-op BitBlt operation for devices that don't have one (such as
 *	the NULL device).
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None, for sure, for sure.
 *
 * ----------------------------------------------------------------------------
 */

void
NullBitBlt()
{
}


/*
 *---------------------------------------------------------
 *
 * nullSetDisplay --
 *
 * This routine sets the appropriate parameters so that
 * Magic will work with the particular display type.
 *
 * Results:
 *	Returns TRUE.
 *
 * Side Effects:
 *	Depends on the display type.
 *
 *---------------------------------------------------------
 */

bool
nullSetDisplay(dispType, outFileName, mouseFileName)
    char *dispType;
    char *outFileName;
    char *mouseFileName;
{
    TxPrintf("Using NULL graphics device.\n");

#ifndef __EMSCRIPTEN__
    TxAdd1InputDevice(fileno(stdin), nullStdin, (ClientData) NULL);
    if (TxStdinIsatty) SigWatchFile(fileno(stdin), "stdin");
#endif

    /* Set up the procedure values in the indirection table. */

    GrLockPtr = grSimpleLock;
    GrUnlockPtr = grSimpleUnlock;
    GrInitPtr = NullInit;
    GrClosePtr = nullDoNothing;
    GrSetCMapPtr = nullDoNothing;

    GrEnableTabletPtr = nullDoNothing;
    GrDisableTabletPtr = nullDoNothing;
    GrSetCursorPtr = (void (*)()) nullDoNothingI;
    GrTextSizePtr = NullTextSize;
    GrDrawGlyphPtr = (void (*)()) nullDoNothingII;
    GrBitBltPtr = NullBitBlt;
    GrReadPixelPtr = nullReturnZero;
    GrFlushPtr = nullDoNothing;

    /* Window management — null driver has no real windows; return FALSE so
     * callers know the operation wasn't performed. */
    GrCreateWindowPtr     = NULL; /* headless: skip OS window creation, WindCreate stays OK */
    GrDeleteWindowPtr     = (void (*)()) nullDoNothingI;
    GrConfigureWindowPtr  = (void (*)()) nullDoNothingI;
    GrOverWindowPtr       = (void (*)()) nullDoNothingI;
    GrUnderWindowPtr      = (void (*)()) nullDoNothingI;
    GrDamagedPtr          = nullDoNothing;
    GrUpdateIconPtr       = (void (*)()) nullDoNothingII;
    GrEventPendingPtr     = (bool (*)()) nullReturnFalse;
    GrWindowIdPtr         = (int  (*)()) nullReturnZeroI;
    GrWindowNamePtr       = NULL;   /* protected by callers */
    GrGetCursorPosPtr     = (bool (*)()) nullReturnFalseII;
    GrGetCursorRootPosPtr = (bool (*)()) nullReturnFalseII;

    /* Backing store — not available in headless mode */
    GrGetBackingStorePtr    = (bool (*)()) nullReturnFalseII;
    GrScrollBackingStorePtr = (bool (*)()) nullReturnFalseII;
    GrPutBackingStorePtr    = (void (*)()) nullDoNothingII;
    GrFreeBackingStorePtr   = (void (*)()) nullDoNothingI;
    GrCreateBackingStorePtr = (void (*)()) nullDoNothingI;

    /* local indirections */
    grSetSPatternPtr  = (void (*)()) nullDoNothingII;
    grPutTextPtr      = (void (*)()) nullDoNothingIIII;
    grFontTextPtr     = (void (*)()) nullDoNothingIIIIIII;
    grDefineCursorPtr = (void (*)()) nullDoNothingI;
    grFreeCursorPtr   = (void (*)()) nullDoNothingI;
    grDrawGridPtr     = (bool (*)()) nullReturnFalseIII;
    grDrawLinePtr     = (void (*)()) nullDoNothingIIII;
    grSetWMandCPtr    = (void (*)()) nullDoNothingII;
    grFillRectPtr     = (void (*)()) nullDoNothingI;
    grFillPolygonPtr  = (void (*)()) nullDoNothingII;
    grSetStipplePtr   = (void (*)()) nullDoNothingI;
    grSetLineStylePtr = (void (*)()) nullDoNothingI;
    grSetCharSizePtr  = (void (*)()) nullDoNothingI;

    GrScreenRect.r_xtop = 511;
    GrScreenRect.r_ytop = 483;

    /* Set GrDisplayStatus to force graphics updates to be suspended */
    GrDisplayStatus = DISPLAY_SUSPEND;

    return TRUE;
}
