/*
 * CMWundo.c --
 *
 * Interface to the undo package for the color map editor.
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
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cmwind/CMWundo.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "cmwind/cmwind.h"
#include "utils/undo.h"

/*
 * Identifiers for the color map window editing
 * operation.
 */
UndoType cmwUndoClientID;

/*
 * Functions to play events forward/backward.
 */
void cmwUndoForw(), cmwUndoBack();
void cmwUndoStart(), cmwUndoDone();

/*
 * A single undo event for the
 * color map module.
 */
    typedef struct
    {
	int cue_color;			/* Index in color map */
	int old_r, old_g, old_b;	/* Old RGB */
	int new_r, new_g, new_b;	/* New RGB */
    } colorUE;

/*
 * Table of colors that were changed as a result
 * of an undo/redo command.
 */
bool cmwColorsChanged[256];

/*
 * ----------------------------------------------------------------------------
 *
 * CMWundoInit --
 *
 * Initialize the color map editor part of the undo package.
 * Makes the functions contained in here known to the
 * undo module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the undo package.
 *
 * ----------------------------------------------------------------------------
 */

void
CMWundoInit()
{
    cmwUndoClientID = UndoAddClient(cmwUndoStart, cmwUndoDone, NULL, NULL,
				cmwUndoForw, cmwUndoBack, "color map");
}


/*
 * ----------------------------------------------------------------------------
 *
 * cmwUndoForw --
 * cmwUndoBack --
 *
 * Play forward/backward a colormap undo event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the colormap.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwUndoForw(up)
    colorUE *up;
{
    (void) GrPutColor(up->cue_color, up->new_r, up->new_g, up->new_b);
    cmwColorsChanged[up->cue_color] = TRUE;
}

void
cmwUndoBack(up)
    colorUE *up;
{
    (void) GrPutColor(up->cue_color, up->old_r, up->old_g, up->old_b);
    cmwColorsChanged[up->cue_color] = TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwUndoColor --
 *
 * Record on the undo list a change in a color map entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the undo list.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwUndoColor(color, oldr, oldg, oldb, newr, newg, newb)
    int color;
    int oldr, oldg, oldb;
    int newr, newg, newb;
{
    colorUE *up;

    up = (colorUE *) UndoNewEvent(cmwUndoClientID, sizeof (colorUE));
    if (up == (colorUE *) NULL)
	return;

    up->cue_color = color;
    up->old_r = oldr;
    up->old_g = oldg;
    up->old_b = oldb;
    up->new_r = newr;
    up->new_g = newg;
    up->new_b = newb;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwUndoStart --
 *
 * Initialization for undo/redo for the color map editor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes cmwColorsChanged[].
 *
 * ----------------------------------------------------------------------------
 */

void
cmwUndoStart()
{
    int i;

    for (i = 0; i < 256; i++)
	cmwColorsChanged[i] = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmwUndoDone --
 *
 * Termination for undo/redo for the color map editor.
 * Forces redisplay of any of the windows containing colors that
 * were changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Causes redisplay.
 *
 * ----------------------------------------------------------------------------
 */

void
cmwUndoDone()
{
    int i;
    extern int cmwRedisplayFunc();

    for (i = 0; i < 256; i++)
	if (cmwColorsChanged[i])
	    (void) WindSearch(CMWclientID, (ClientData) NULL, (Rect *) NULL,
			cmwRedisplayFunc, (ClientData) i);
}
