/* gcrFlags.c -
 *
 *	Code to set contact and end flags at each grid point in a channel.
 *	This code assumes the channel has already been transformed into left
 *	to right routing form.  These flags are used during channel routing.
 *
 *	Hazard flags are set in the router module, as they are needed by the
 *	global router.  The contact and end flags can not be set at that
 *	time, since we don't yet know in which direction the channel will be
 *	routed.
 *
 *	CONTACT FLAGS mark clear locations bordering single layer obstacles.
 *	Extending a track horizontally across an obstacle requires a track
 *	contact and a layer switch.  Extending a column vertically across an
 *	obstacle requires a column contact and a layer switch.
 *
 *	END FLAGS mark locations beyond which tracks or columns cannot be
 *	extended due to two-layer obstacles.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrFlags.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"

/*
 * ----------------------------------------------------------------------------
 *
 * gcrSetFlags --
 *
 *	Scan the channel obstacle map.  Set flags for track and column
 *	contacts, and track and column termination.
 *
 *	Use a top to bottom, left to right point by point scan.  Wherever
 *	a vertical boundary between space and a track material obstacle
 *	occurs, make a track contact flag.  Wherever a horizontal boundary
 *	between space and a column material obstacle occurs, make a column
 *	contact flag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 * ----------------------------------------------------------------------------
 */

void
gcrSetFlags(ch)
    GCRChannel *ch;	/* The channel to be flagged */
{
    short *curPtr, *nextPtr;
    short **map, *curCol, *curEnd, *nextCol;
    int col;

    /*
     * Scan the channel bottom to top, left to right.
     * For each location scanned, look up and right to
     * figure out what flags to set.
     */
    map = ch->gcr_result;
    nextCol = map[1];
    for (col = 1; col <= ch->gcr_length; col++)
    {
	curCol = nextCol;
	curEnd = &curCol[ch->gcr_width];
	nextCol = map[col+1];
	for (curPtr = &curCol[1], nextPtr = &nextCol[1];
		curPtr <= curEnd;
		curPtr++, nextPtr++)
	{
	    /*
	     * Set column contact bits at the border between space and poly.
	     * Set column end bits at the border of blocked areas.
	     */
	    if (CLEAR(*curPtr))
	    {
		if (HAS_M(*nextPtr))	     /* Look right for track obstacle */
		    *curPtr |= GCRTC;
		else if (BLOCK(*nextPtr))   /* Look right for blocked area */
		    *curPtr |= GCRTE;

		if (HAS_P(*(curPtr+1)))     /* Look up for column obstacle */
		    *curPtr |= GCRCC;
		else if (BLOCK(*(curPtr+1)))/* Look up for blocked area */
		    *curPtr |= GCRCE;
	    }
	    else if (HAS_P(*curPtr))
	    {
		if (HAS_M(*nextPtr))
		    *curPtr |= GCRTE;
		else if (BLOCK(*nextPtr))
		    *curPtr |= GCRTE;

		if (CLEAR(*(curPtr+1)))	/* Look up for column obstacle end */
		    *(curPtr+1) |= GCRCC;
		else if (HAS_M(*(curPtr+1)) || BLOCK(*(curPtr+1)))
		    *curPtr |= GCRCE;	/* Look up for column blocked	*/
	    }
	    else if (HAS_M(*curPtr))
	    {
		if (CLEAR(*nextPtr))	/* Look right for track obstacle end */
		    *nextPtr |= GCRTC;
		else if (HAS_P(*nextPtr) || BLOCK(*nextPtr))
		    *curPtr |= GCRTE; /* Look right for blocked area */

		if (HAS_P(*(curPtr+1)) || BLOCK(*(curPtr+1)))
		{
		    *curPtr |= GCRCE;	/* Look up for column blocked	*/
		    *(curPtr+1) |= GCRCE;
		}
	    }
	    else
	    {
		/*
		 * When a contact is encountered, mark self and up.
		 * Don't need to mark GCRTE to the right, since this
		 * flag simply means that the track is blocked to the right.
		 */
		ASSERT(BLOCK(*curPtr), "Bizarre case");
		*curPtr |= (GCRTE | GCRCE);
		*(curPtr+1) |= GCRCE;
	    }
	}
    }
}
