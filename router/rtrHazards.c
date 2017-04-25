/* rtrHazards.c -
 *
 *	Code to set hazard flags at each grid point in a channel.  These flags
 *	are used during channel routing and global routing.
 *
 *	HAZARD FLAGS indicate that the channel router should try to move
 *	tracks out of the flagged areas, in order to avoid routing over
 *	blocked areas or obstacles which are wider than they are tall.  The
 *	hazards cover the obstacle, an area immediately ahead of the obstacle,
 *	and one grid line above and below the obstacle (considering the
 *	direction of routing) which allows for column contacts.
 *	The size of the hazard is the product of the global variable
 *	GCRObstDist and the height of the obstacle.
 *
 *	Hazards are generated for each of the four possible routing directions
 *	across the channel, though the channel will be routed from left to.
 *	right.  Hazards in the other orientations are used only by the global
 *	router, in choosing channel-to-channel crossings.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrHazards.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "gcr/gcr.h"
#include "utils/malloc.h"

/* Forward declarations */

extern void rtrFindEnds();
extern void rtrFlag();


/*
 * ----------------------------------------------------------------------------
 *
 * RtrHazards --
 *
 *	Scan the channel obstacle map.  Set flags for hazards.  Mark left and
 *	right hazards in one pass, since the height to width ratio is the
 *	same regardless of direction.  Similarly, mark top and bottom hazards
 *	in another pass.
 *
 *	For left and right hazards:
 *	Use a top to bottom, left to right point by point scan, comparing the
 *	width of an obstacle to its height at each grid location.  If the
 *	width is greater than the height, or if the location is blocked:
 *	(1)  Find the height of the obstacle by finding the lowest consecutive
 *	     location for which the condition holds;
 *	(2)  Find the width of the obstacle by extending the line of height to
 *	     the left and right, until some edge of the obstacle is found;
 *	(3)  Paint a hazard over the obstacle plus extensions.  Extend left
 *	     a distance proportional to the height of the obstacle.  Extend up
 *	     and down by 1 grid line.
 *
 *	For top and bottom hazards:
 *	As above, with "top" and "bottom" interchanged with "left" and "right",
 *	and GCRVU and GCRVD substituted for GCRVR and GCRVL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated and released.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrHazards(ch)
    GCRChannel *ch;	/* The channel to be flagged */
{
    short **map, **rtrHeights(), **rtrWidths(), **height, **width;
    short *hcol, *wcol, *mcol;
    int row, col, bot, top, left, right, maxcol;

    height = rtrHeights(ch);
    width  = rtrWidths(ch);

    /*
     * Scan the channel, looking for a place where obstacle height < width or
     * blocked on both layers, and not previously marked.  For each such point,
     * find the tallest, then widest rectangle containing the point.
     */
    map = ch->gcr_result;
    for (col = 1; col <= ch->gcr_length; col++)
    {
	/*
	 * If there is an obstacle over the crossing, set the Size field of the
	 * crossing point to indicate how large the obstacle is in the direction
	 * of the net's travel.  This could not be done at the time the GCROBST
	 * flag was set (RtrChannelObstacles), since heights and widths were not
	 * yet computed.  Extend the obstacle marking outwards 1 grid.
	 */
	if (ch->gcr_bPins[col].gcr_pFlags == GCROBST)
	    ch->gcr_bPins[col].gcr_pSize = height[col][1];
	else if (ch->gcr_bPins[col-1].gcr_pFlags == GCROBST)
	    ch->gcr_bPins[col].gcr_pFlags |= GCRTCC;
	else if (ch->gcr_bPins[col+1].gcr_pFlags == GCROBST)
	    ch->gcr_bPins[col].gcr_pFlags |= GCRTCC;

	if (ch->gcr_tPins[col].gcr_pFlags == GCROBST)
	    ch->gcr_tPins[col].gcr_pSize = height[col][ch->gcr_width];
	else if (ch->gcr_tPins[col-1].gcr_pFlags == GCROBST)
	    ch->gcr_tPins[col].gcr_pFlags |= GCRTCC;
	else if (ch->gcr_tPins[col+1].gcr_pFlags == GCROBST)
	    ch->gcr_tPins[col].gcr_pFlags |= GCRTCC;

	hcol = height[col];
	wcol = width[col];
	mcol = map[col];
	for (row = 1; row <= ch->gcr_width; row++)
	{
	    if ((hcol[row] < wcol[row] && !(mcol[row]&GCRVL))
		    || BLOCK(mcol[row]))
	    {
		/*
		 * Find the far side of the region where height < width.
		 * This is not necessarily row - height - 1 since the
		 * width may vary.
		 */
		for (bot = row; row <= ch->gcr_width; row++)
		    if (hcol[row] >= wcol[row] && !BLOCK(mcol[row]))
			break;
		top = row - 1;

		/*
		 * Extend left and right through contiguous material.
		 * Mark the hazard flags.
		 */
		left = col;
		rtrFindEnds(ch, TRUE, bot, top, &left, &right);
		rtrFlag(ch, left, right, bot, top, TRUE);
	    }
	}
    }

    /* Go the other way to get the upper and lower flags */
    for (row = 1; row <= ch->gcr_width; row++)
    {
	/*
	 * If there is an obstacle over the crossing, set the Size field of the
	 * crossing point to indicate how large the obstacle is in the direction
	 * of the net's travel.  This could not be done at the time the GCROBST
	 * flag was set (RtrChannelObstacles), since heights and widths were not
	 * yet computed.  Put GCRTCC markings at the edges.
	 */
	if (ch->gcr_lPins[row].gcr_pFlags == GCROBST)
	    ch->gcr_lPins[row].gcr_pSize = width[1][row];
	else if (ch->gcr_lPins[row-1].gcr_pFlags == GCROBST)
	    ch->gcr_lPins[row].gcr_pFlags |= GCRTCC;
	else if (ch->gcr_lPins[row+1].gcr_pFlags == GCROBST)
	    ch->gcr_lPins[row].gcr_pFlags |= GCRTCC;

	if (ch->gcr_rPins[row].gcr_pFlags == GCROBST)
	    ch->gcr_rPins[row].gcr_pSize = width[ch->gcr_length][row];
	else if (ch->gcr_rPins[row-1].gcr_pFlags == GCROBST)
	    ch->gcr_rPins[row].gcr_pFlags |= GCRTCC;
	else if (ch->gcr_rPins[row+1].gcr_pFlags == GCROBST)
	    ch->gcr_rPins[row].gcr_pFlags |= GCRTCC;

	for (col = 1; col <= ch->gcr_length; col++)
	{
	    if ((height[col][row] > width[col][row] && !(map[col][row]&GCRVU))
		    || BLOCK(map[col][row]))
	    {
		/*
		 * Find the far side of the region where height > width.
		 * This is not necessarily col + width - 1 since the width
		 * may vary.
		 */
		for (left = col; col <= ch->gcr_length; col++)
		    if (height[col][row] <= width[col][row] &&
			    !BLOCK(map[col][row]))
			break;
		right = col - 1;

		/*
		 * Extend up and down through contiguous material.
		 * Mark the hazard flags.
		 */
		bot = row;
		rtrFindEnds(ch, FALSE, left, right, &bot, &top);
		rtrFlag(ch, left, right, bot, top, FALSE);
	    }
	}
    }

    /* Free storage for height and width */
    maxcol = ch->gcr_length + 1;
    for (col = 0; col <= maxcol; col++)
    {
	freeMagic((char *) height[col]);
	freeMagic((char *) width[col]);
    }
    freeMagic((char *) height);
    freeMagic((char *) width);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrHeights --
 *
 * Find the heights of all obstacles in the given channel.
 *
 * Results:
 * 	A pointer to an array of obstacle heights for the given channel.
 *
 * Side effects:
 *	Mallocs the memory for the heights array.
 *
 * ----------------------------------------------------------------------------
 */

short **
rtrHeights(ch)
    GCRChannel * ch;	/* Channel to be processed */
{
    short **heights, *obstacles, *hcol;
    int i, row;
    int col, start, n;
    unsigned lenWds, widWds;

    /*
     * Malloc and initialize an array to hold the obstacle height
     * at each point.
     */
    lenWds = ch->gcr_length + 2;
    widWds = ch->gcr_width + 2;
    heights = (short **) mallocMagic((unsigned) (lenWds * sizeof (short *)));
    for (col = 0; col < (int) lenWds; col++)
    {
	heights[col] = (short *) mallocMagic((unsigned) (widWds * sizeof (short)));
	for (row = 0; row < (int) widWds; row++)
	    heights[col][row] = 0;
    }

    /*
     * Scan over the obstacle map.
     * At the start of an obstacle, scan downward to find the height
     * of contiguous material on either or both of the layers.  Set
     * the obstacle height for every location in the vertical strip
     * just scanned.
     */
    for (col = 1; col <= ch->gcr_length; col++)
    {
	hcol = heights[col];
	obstacles = ch->gcr_result[col];
	for (row = 1; row <= ch->gcr_width; row++)
	{
	    obstacles++;
	    if (CLEAR(*obstacles)) continue;

	    /* Found an obstacle of some sort, so scan to its end */
	    for (start = row; row <= ch->gcr_width && !CLEAR(*obstacles); row++)
		obstacles++;

	    /*
	     * Set height values at every grid point touched
	     * in this column by the obstacle.
	     */ 
	    n = row - start;
	    for (i = start; i < row; i++)
		hcol[i] = n;
	}
    }

    return (heights);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrWidths --
 *
 * Find the widths of all obstacles in the given channel.
 *
 * Results:
 * 	A pointer to an array of obstacle widths for the given channel.
 *
 * Side effects:
 *	Mallocs the memory for the widths array.
 *
 * ----------------------------------------------------------------------------
 */

short **
rtrWidths(ch)
    GCRChannel * ch;
{
    short **widths, **map;
    int col, i;
    unsigned lenWds, widWds;
    int row, start, n;

    /*
     * Malloc and initialize an array to hold the obstacle width
     * at each point.
     */
    lenWds = ch->gcr_length + 2;
    widWds = ch->gcr_width + 2;
    widths = (short **) mallocMagic((unsigned) (lenWds * sizeof (short *)));
    for (col = 0; col < (int) lenWds; col++)
    {
	widths[col] =  (short *) mallocMagic((unsigned) (widWds * sizeof (short)));
	for (row = 0; row < (int) widWds; row++)
	    widths[col][row] = 0;
    }
    map = ch->gcr_result;

    /*
     * Scan over the obstacle map.
     * At the start of an obstacle, scan right to find the width
     * of contiguous material on either or both of the layers.
     * Set the obstacle width for every location in the horizontal
     * strip just scanned.
     */
    for (row = 1; row <= ch->gcr_width; row++)
    {
	for (col = 1; col <= ch->gcr_length; col++)
	{
	    if (CLEAR(map[col][row])) continue;

	    /* Found an obstacle of some sort, so scan to its end */
	    for (start = col; col <= ch->gcr_length; col++)
		if (CLEAR(map[col][row]))
		    break;

	    /* Set width values at every grid point touched in this column
	     * by the obstacle.
	     */ 
	    n = col - start;
	    for (i = start; i < col; i++)
		widths[i][row] = n;
	}
    }

    return(widths);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrFindEnds --
 *
 * Given the top and bottom y values for an obstacle, extend left and right
 * to find edges (isHoriz == TRUE).
 *
 * Given the left and right x values for an obstacle, extend up and down
 * to find edges (isHoriz == FALSE).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Returns the two values in * lo and * hi.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrFindEnds(ch, isHoriz, bot, top, lo, hi)
    GCRChannel *ch;
    int isHoriz;	/* 1 if to find horizontal ends */
    int bot;	/* Low range to be scanned */
    int top;	/* High range to be scanned */
    int *lo;		/* Low range of result.  Also starting col or row. */
    int *hi;		/* High range of result */
{
    int col, row;
    short **map;

    map = ch->gcr_result;
    if (isHoriz)
    {
	/*
	 * Extend right, through contiguous vertical material from bot to top.
	 * In this and the next loop "bot" and "top" refer to the lower and
	 * upper ranges of the vertical strip whose left and right edges are
	 * sought.
	 */
	for (col = *lo + 1; col <= ch->gcr_length; col++)
	    for (row = bot; row <= top; row++)
		if (CLEAR(map[col][row]))
		    goto gotHH;

gotHH:
	/* Found the right edge of material */
	*hi = col - 1;

	/*
	 * Extend left, through contiguous vertical material
	 * from bot to top.
	 */
	for (col = *lo - 1; col > 0; col--)
	    for (row = bot; row <= top; row++)
		if (CLEAR(map[col][row]))
		    goto gotHL;

gotHL:
	/* Found the left edge of material */
	*lo = col + 1;
    }
    else
    {
	/*
	 * Extend up, through contiguous horizontal material from bot to top.
	 * In this and the next loop, "bot" and "top" actually refer to the
	 * left and right limits of the material whose end is sought.
	 */
	for (row = *lo + 1; row <= ch->gcr_width; row++)
	    for (col = bot; col <= top; col++)
		if (CLEAR(map[col][row]))
		    goto gotVH;
gotVH:
	/* Found the upper edge of material */
	*hi = row - 1;

	/*
	 * Extend down, through contiguous horizontal material
	 * from bot to top.
	 */
        for (row = *lo - 1; row > 0; row--)
	    for (col = bot; col <= top; col++)
		if(CLEAR(map[col][row]))
		    goto gotVL;
gotVL:
	/* Found the lower edge of material */
	*lo = row + 1;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrFlag --
 *
 *   1.	Set hazard flags in the channel flag array for a given hazard generating
 *	region and surrounding area.  For horizontal regions (isHoriz==TRUE)
 *	set flags for left to right routing and for right to left routing.
 *	For vertical regions set flags for top to bottom routing and for
 *	bottom to top routing.
 *
 *	For a given direction set the hazard flags in the region itself, in an
 *	area ahead of the region and on one track on either side of the region.
 *	The size of the area ahead of the region is a product of the height
 *	of the region and the global variable GCRObstDist.
 *
 *	The boxed area generates flags in the area outlined in 'x'.
 *
 *		    xxxxxxxxxxxxxx			     xxxxxxxxxxxxxx
 *	---------   x   ---------x		----------   x---------   x
 *	|	|   x   |        x		|        |   x        |   x
 *	---------   x   ---------x		----------   x---------   x
 *		    xxxxxxxxxxxxxx			     xxxxxxxxxxxxxx
 *
 *   2.	Set GCRHAZRD flags, obstacle size, and obstacle distance for each
 *	pin which falls in the shadow of a hazard.  The size measure says how
 *	much material there is in the direction the net travels, while the
 *	distance measure says how far the material is from the pin.  Obstacle
 *	information (for covered pins) is handled in another location.
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
rtrFlag(ch, cl, cr, rb, rt, isHoriz)
    GCRChannel * ch;	/* Channel whose flag array is to be set	*/
    int cl, cr;		/* Left and right limits of columns to be set	*/
    int rb, rt;		/* Bottom and top limits of rows to be set	*/
    bool isHoriz;	/* TRUE if left/right flags, else top/bottom	*/
{
    int extra, limit, r, c;
    short ** map;

    map = ch->gcr_result;
    if(isHoriz)
    {
	extra = 0.99999 + GCRObstDist * (rt - rb + 1);
	limit = cl - extra;
	if(limit <= 0)
	{
	/* Hazard extends over the left edge of the channel.  Mark the pins.
	 * When setting distance to the nearest obstacle, rely on the left to
	 * right sweep so the closest hazard is found first.
	 */
	    for(r = rb - 1; r <= rt + 1; r++)
		if(!ch->gcr_lPins[r].gcr_pFlags)
		{
		    ch->gcr_lPins[r].gcr_pFlags = GCRHAZRD;
		    ch->gcr_lPins[r].gcr_pDist  = cl;
		    ch->gcr_lPins[r].gcr_pSize  = cr - cl;
		}
	    limit = 0;
	}
	for(c = limit; c < cl;  c++)
	    for(r = rb - 1; r <= rt + 1; r++)
		map[c][r] |= GCRVL;

	for(c = cl; c <= cr;  c++)
	    for(r = rb - 1; r <= rb + 1; r++)
		map[c][r] |= (GCRVL | GCRVR);

	limit = cr + extra;
	if(limit >= ch->gcr_length)
	{
	/* Hazard extends over the right edge of the channel.  Mark the pins.
	 * When setting distance to the nearest obstacle, rely on the left to
	 * right sweep so the closest hazard is found last.
	 */
	    for(r = rb - 1; r <= rt + 1; r++)
	    {
		if(!ch->gcr_rPins[r].gcr_pFlags)
		    ch->gcr_rPins[r].gcr_pFlags = GCRHAZRD;
		if(ch->gcr_rPins[r].gcr_pFlags == GCRHAZRD)
		{
		    ch->gcr_rPins[r].gcr_pDist = ch->gcr_length - cr;
		    ch->gcr_rPins[r].gcr_pSize = cr - cl;
		}
	    }
	    limit = ch->gcr_length;
	}
	for(c = cr + 1; c <= limit;  c++)
	    for(r = rb - 1; r <= rt + 1; r++)
		map[c][r] |= GCRVR;
    }
    else
    {
	extra = 0.99999 + GCRObstDist * (cr - cl + 1);
	limit = rb - extra;
	if(limit < 0)
	{
	/* Hazard extends over the bottom edge of the channel.  Mark the pins.
	 */
	    for(c = cl - 1; c <= cr + 1; c++)
		if(!ch->gcr_bPins[c].gcr_pFlags)
		{
		    ch->gcr_bPins[c].gcr_pFlags = GCRHAZRD;
		    ch->gcr_bPins[c].gcr_pDist = rb;
		    ch->gcr_bPins[c].gcr_pSize = rt - rb;
		}
	    limit = 0;
	}
	for(r = limit; r < rb;  r++)
	    for(c = cl - 1 ; c <= cr + 1; c++)
		map[c][r] |= GCRVD;

	for(r = rb; r <= rt;  r++)
	    for(c = cl - 1; c <= cr + 1; c++)
		map[c][r] |= (GCRVD | GCRVU);

	limit = rt + extra;
	if(limit >= ch->gcr_width)
	{
	/* Hazard extends over the top edge of the channel.  Mark the pins.
	 */
	    for(c = cl - 1; c <= cr + 1; c++)
	    {
		if(!ch->gcr_tPins[c].gcr_pFlags)
		    ch->gcr_tPins[c].gcr_pFlags = GCRHAZRD;
		if(ch->gcr_tPins[c].gcr_pFlags == GCRHAZRD)
		{
		    ch->gcr_tPins[c].gcr_pDist = ch->gcr_width - rt;
		    ch->gcr_tPins[c].gcr_pSize = rt - rb;
		}
	    }
	    limit = ch->gcr_width;
	}
	for(r = rt + 1; r <= limit;  r++)
	    for(c = cl - 1 ; c <= cr + 1; c++)
		map[c][r] |= GCRVU;
    }
}
