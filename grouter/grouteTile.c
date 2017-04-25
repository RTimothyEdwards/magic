/* grouteTile.c -
 *
 *	Global signal router code for tile and channel related things.
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
static char sccsid[] = "@(#)grouteTile.c	4.3 MAGIC (Berkeley) 10/31/85";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "debug/debug.h"
#include "gcr/gcr.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "utils/styles.h"
#include "textio/textio.h"

bool GlDebugCrossings = FALSE;

#define isSpaceTile(tile) (TiGetBody(tile) == NULL)


/*
 * ----------------------------------------------------------------------------
 *
 * glAdjacentChannel --
 *
 * Figure out which channel borders the given channel at the given
 * point.  If the given channel is NULL, then assume the point is at
 * the edge of a cell, and return the channel adjacent to the cell.
 *
 * Results:
 *	Pointer to the adjacent channel, or NULL if none exists.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
glAdjacentChannel(ch, point)
    GCRChannel *ch;	/* The tile corresponding to a channel */
    Point *point;	/* A point somewhere on the channel edge */
{
    Point p;
    int side;

    ASSERT(ch!=(GCRChannel *) NULL, "Null channel in glAdjacentChannel");
    side = glPointToSide(ch, point);

    p = * point;
    switch(side)
    {
	case GEO_NORTH:
	case GEO_EAST:
	    break;
	case GEO_SOUTH:
	    p.p_y--;
	    break;
	case GEO_WEST:
	    p.p_x--;
	    break;
	default:
	    ASSERT(FALSE, "glAdjacentChannel point not on channel");
	    return((GCRChannel *) NULL);
	    break;
    }

    return (glTileToChannel(TiSrPointNoHint(RtrChannelPlane, &p)));
}

/*
 * ----------------------------------------------------------------------------
 *
 * glBadChannel --
 *
 * Decide if a prospective channel is okay to use in global routing
 * propagation.  If it creates a loop for the particular path under
 * investigation, then forget it.  If it doesn't exist, then forget it.
 *
 * Results:
 *	FALSE if the channel is okay to use, otherwise TRUE.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
glBadChannel(oldCh, newCh, entryPt)
    GCRChannel *oldCh;		/* The current channel */
    GCRChannel *newCh;		/* See if this new one is okay to use */
    GlPoint    *entryPt;	/* List of previously used points */
{
    GlPoint *temp;

    /* Reject a null channel */
    if ((oldCh == (GCRChannel *) NULL) || (newCh == (GCRChannel *) NULL))
	return (TRUE);

    /* Reject the channel if using it creates a loop in the routing path */
    for (temp = entryPt; temp != (GlPoint *) NULL; temp = temp->gl_parent)
	if (temp->gl_ch == newCh)
	    return (TRUE);

    return (FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glDebug --
 *
 * Code to display crossing points on the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
glDebug(point, text, value, size)
    Point *point;	/* Location to be marked */
    char *text;		/* Text to associate with error paint */
    int value;		/* An integer value to go into the text */
    int	size;		/* How big to make the error paint box */
{
    char buffer1[1024], buffer2[1024];
    Rect area;

    if (!GlDebugCrossings)
	return;

    (void) strcpy(buffer1, text);
    (void) sprintf(buffer2, "(value = %d)", value);
    (void) strcat(buffer1, buffer2);
    area.r_ll.p_x = point->p_x - size;
    area.r_ur.p_x = point->p_x + size;
    area.r_ll.p_y = point->p_y - size;
    area.r_ur.p_y = point->p_y + size;
    DBWFeedbackAdd(&area, buffer1, EditCellUse->cu_def, 1,
    	    STYLE_PALEHIGHLIGHTS);
    GrFlush();
    (void) sprintf(buffer2, "%s --more--", buffer1);
    (void) TxGetLinePrompt(buffer1, sizeof buffer1, buffer2);
    if (buffer1[0] == 'q')
	GlDebugCrossings = FALSE;
    TxPrintf("\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPointToChannel --
 *
 * Given a point and a direction, return a pointer to the channel
 * adjacent to the given point, in the given direction.  The point
 * must be on the edge of a channel.  If on the edge of a cell, the
 * point location must be the modified location returned by RtrStemTip.
 *
 * Results:
 *	Pointer to the channel.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
glPointToChannel(point, dir)
    Point *point;	/* The point from which the channel is found */ 
    int dir;		/* The direction in which the search goes */
{
    Point p;

    p = *point;
    switch (dir)
    {
	case GEO_NORTH:
	case GEO_EAST:
	    break;
	case GEO_SOUTH:
	    p.p_y--;
	    break;
	case GEO_WEST:
	    p.p_x--;
	    break;
	default:
	    ASSERT(FALSE, "glPointToChannel: bad direction argument");
	    return ((GCRChannel *) NULL);
    }
    return (glTileToChannel(TiSrPointNoHint(RtrChannelPlane, &p)));
}

/*
 * ----------------------------------------------------------------------------
 *
 * glPointToSide --
 *
 * Given a point somewhere on the perimeter of a channel, determine
 * which side it is on.  The channel is given to prevent ambiguity,
 * since the point may fall on a border between two channels.
 *
 * Results:
 *	The compass direction corresponding to the side of the channel on
 *	which the point lies.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
glPointToSide(ch, pt)
    GCRChannel * ch;	/* Channel containing the given point	*/
    Point      * pt;	/* The point on some side of the channel*/
{

    /* The point is on the left */
    if (ch->gcr_area->r_xbot == pt->p_x)
	return (GEO_WEST);

    /* The point is on the bottom */
    if (ch->gcr_area->r_ybot == pt->p_y)
	return (GEO_SOUTH);

    /* The point is on the right */
     if (ch->gcr_area->r_xtop == pt->p_x)
	return (GEO_EAST);

    /* The point is on the top */
     if (ch->gcr_area->r_ytop == pt->p_y)
	return (GEO_NORTH);

    ASSERT(FALSE, "glPointToSide point not on edge");
    return (GEO_CENTER);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glSide --
 *
 * Determine which side of a cell a terminal label is on by looking on
 * the channel plane for channel tiles in each of the quadrants.  A
 * terminal label must not fall on space or on a corner.  It must lie
 * on a flat edge of a cell tile.
 *
 * Are all these calls to TiSrPoint really necessary?  I think so,
 * because a label might fall on the edge of a particular tile but
 * still appear over contiguous cell tiles.
 *
 * Results:
 *	A direction GEO_NORTH, SOUTH, EAST, WEST, or CENTER if internal or
 *	a corner.  This indicates the side of the cell on which the terminal
 *	lies.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
glSide(point)
    Point *point;	/* The lower left point of the label rectangle */
{
    int glSrFunc();
    Tile *t;
    Point p;

    p	  = *point;
    t     = TiSrPoint((Tile *) NULL, RtrChannelPlane, &p);

    if(TiGetBody(t) != NULL)		/*   _?|X_	*/
    {					/*    ?|?	*/
	p.p_x--;
	t = TiSrPoint(t, RtrChannelPlane, &p);
	if(TiGetBody(t) != NULL)	/*   _X|X_	*/
	{				/*    ?|?	*/
	    p.p_y--;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) != NULL)	/*   _X|X_	*/
		 return(GEO_CENTER);	/*    X|?	*/
	    p.p_x++;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) != NULL)	/*   _X|X_	*/
		 return(GEO_CENTER);	/*     |X	*/
	    else return(GEO_SOUTH);
	}
	else				/*   __|X_	*/
	{				/*    ?|?	*/
	    p.p_y--;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) != NULL)	/*   __|X_	*/
		 return(GEO_CENTER);	/*    X|?	*/
	    p.p_x++;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) != NULL)	/*   __|X_	*/
		 return(GEO_WEST);	/*     |X	*/
	}
    }
    else				/* Forget about S or W */
    {
	p.p_x--;
	t = TiSrPoint(t, RtrChannelPlane, &p);
	if(TiGetBody(t) != NULL)	/*   _X|__	*/
	{				/*    ?|?	*/
	    p.p_y--;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) == NULL)	/*   _X|__	*/
	        return(GEO_CENTER);	/*     |?	*/
	    p.p_x++;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) != NULL)	/*   _X|__	*/
		 return(GEO_CENTER);	/*    X|X	*/
	    else return(GEO_EAST);
	}
	else				/*   __|__	*/
	{				/*    ?|?	*/
	    p.p_y--;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) == NULL)	/*   __|__	*/
		 return(GEO_CENTER);	/*     |?	*/
	    p.p_x++;
	    t = TiSrPoint(t, RtrChannelPlane, &p);
	    if(TiGetBody(t) != NULL)	/*   __|__	*/
		 return(GEO_NORTH);	/*    X|X	*/
	}
    }
    return(GEO_CENTER);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glTileToChannel --
 *
 * Figure out which channel corresponds to the given tile.
 *
 * Results:
 *	Pointer to the channel.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
glTileToChannel(tile)
    Tile *tile;	/* Should point to a channel (space) tile */
{
    HashEntry *he;

    if (!isSpaceTile(tile))
	return ((GCRChannel *) NULL);
    he = HashLookOnly(&RtrTileToChannel, (char *) tile);
    if (he == (HashEntry *) NULL)
	return ((GCRChannel *) NULL);
    return (GCRChannel *) HashGetValue(he);
}
