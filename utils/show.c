/* show.c -
 *
 *	Routines for displaying rects with highlights etc.  Useful for
 *      debugging.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/show.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "dbwind/dbwind.h"


/*
 * ----------------------------------------------------------------------------
 *
 * ShowRect --
 *
 * Show an area in the cell 'def', for debugging the maze router.
 * In this implementation, the area is only displayed in windows
 * where 'def' is the root cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *	Because this procedure bypasses the normal display package,
 *	it can leave data on the screen messed up.  Callers should
 *	use styles that affect only the highlight layer to minimize
 *	the amount of damage.
 *
 * ----------------------------------------------------------------------------
 */

int ShowRectStyle;
CellDef *ShowRectDef;

void
ShowRect(def, r, style)
    CellDef *def;	/* The area is in this def */
    Rect *r;		/* Display this area (in coords of def above) */
    int style;		/* in this style */
{
    int ShowRectFunc();

    ShowRectDef = def;
    ShowRectStyle = style;
    (void) WindSearch(DBWclientID, (ClientData) NULL, r, ShowRectFunc,
		(ClientData) r);
}

int
ShowRectFunc(w, r)
    MagWindow *w;
    Rect *r;
{
    Rect screenRect;

    /* Skip if desired def is not root of window */
    if (((CellUse *) w->w_surfaceID)->cu_def != ShowRectDef)
	return (0);

    WindSurfaceToScreen(w, r, &screenRect);
    GrLock(w, TRUE);
    GrClipBox(&screenRect, ShowRectStyle);
    GrUnlock(w);
    (void) GrFlush();
    return (0);
}
