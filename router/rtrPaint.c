/* rtrPaint.c -
 *
 *	This file provides procedures that do metal-maximization
 *	on channel routing results, then paint the results back
 *	into the edit cell.
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
static char rcsid[] __attribute__ ((unused)) ="$Header: /usr/cvsroot/magic-8.0/router/rtrPaint.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "gcr/gcr.h"
#include "utils/signals.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"

int RtrViaLimit		= 2;
int rtrMetalLength	= 0;
int rtrPolyLength	= 0;
int rtrViaCount		= 0;
bool RtrDoMMax		= TRUE;

/* Forward declarations */

extern void rtrPaintRows();
extern void rtrPaintColumns();
extern void rtrMaxMetal();

bool rtrMetalOkay();
bool rtrDoVia();

/*
 * ----------------------------------------------------------------------------
 * RtrPaintBack --
 *
 *	Maximizes metal, then paints the channel routing results
 *	back into the edit cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Metal-maximize flags are set in the channels, and mucho
 *	paint is added to the edit cell.
 * ----------------------------------------------------------------------------
 */

void
RtrPaintBack(ch, def)
    GCRChannel * ch;
    CellDef *def;
{
    if(RtrDoMMax)		    /*Change poly to metal where possible  */
	rtrMaxMetal(ch);

    rtrPaintRows(def, ch);
    rtrPaintColumns(def, ch);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrPaintRows --
 *
 * 	This procedure scans a given channel for material in the
 *	rows (and also for contacts).  It outputs the relevant
 *	material as paint in a cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The paint planes of def get modified.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrPaintRows(def, ch)
    CellDef *def;	/* Def into which paint will go */
    GCRChannel *ch;	/* Channel being painted */
{
    TileType curType, nextType;
    short **result, code;
    int row, col;
    Rect paint, contact;
    PaintUndoInfo ui;

    ui.pu_def = def;
    result = ch->gcr_result;
    for (row = 0; row <= ch->gcr_width && !SigInterruptPending; row++)
    {
	/*
	 * For each row, this algorithm just marches across the row
	 * processing runs of similar material.  When the material
	 * changes, then paint the previous run (if the previous
	 * run wasn't space).  CurType records the kind of material
	 * currently occupying the track; it's one of RtrMetalType,
	 * RtrPolyType, or TT_SPACE.  NextType records the kind of
	 * material that will occupy the track in the next column.
	 */
	curType = TT_SPACE;
	for (col = 0; col <= ch->gcr_length; col++)
	{
	    if (rtrDoVia(ch, col, row))
	    {
		/*
		 * Contact is needed.  Must use a more general form of
		 * painting here, because contacts appear on several layers.
		 */
		contact.r_xbot = ch->gcr_origin.p_x + (col * RtrGridSpacing)
		    + RtrContactOffset;
		contact.r_xtop = contact.r_xbot + RtrContactWidth;
		contact.r_ybot = ch->gcr_origin.p_y + (row * RtrGridSpacing)
		    + RtrContactOffset;
		contact.r_ytop = contact.r_ybot + RtrContactWidth;
		RtrPaintContact(def, &contact);
	    }

	    /*
	     * Figure out what material is to be present in the track
	     * between this column and the next to the right.
	     */
            code = result[col][row];
	    if (code & GCRR)
	    {
		if ((result[col+1][row] & GCRBLKM)
		    || (code & GCRBLKM))
		    nextType = RtrPolyType;
		else nextType = RtrMetalType;
	    }
	    else nextType = TT_SPACE;

	    /* If the new type is different from the old, paint the old, 
	     * and store the start of the new run.
	     */

	    if (nextType != curType)
	    {
		if (curType != TT_SPACE)
		{
		    paint.r_xtop = ch->gcr_origin.p_x + (col * RtrGridSpacing);
		    RtrPaintStats(curType, paint.r_xtop-paint.r_xbot);
		    if (curType == RtrMetalType)
			paint.r_xtop += RtrMetalWidth;
		    else paint.r_xtop += RtrPolyWidth;
		    ui.pu_pNum = DBPlane(curType);
		    DBPaintPlane(def->cd_planes[ui.pu_pNum], &paint,
			DBStdPaintTbl(curType, ui.pu_pNum), &ui);
		}
		
		paint.r_xbot = ch->gcr_origin.p_x + (col * RtrGridSpacing);
		paint.r_ybot = ch->gcr_origin.p_y + (row * RtrGridSpacing);
		if (nextType == RtrMetalType)
		{
		    paint.r_ytop = paint.r_ybot + RtrMetalWidth;
		}
		else
		{
		    /* This code also gets executed when nextType is TT_SPACE,
		     * but it doesn't matter.
		     */
		    paint.r_ytop = paint.r_ybot + RtrPolyWidth;
		}
		if (col == 0) paint.r_xbot = ch->gcr_area.r_xbot;
	    }
	    curType = nextType;
	}

	/*
	 * At the end of the row, we have to output any ongoing material.
	 * This material extends all the way to the RHS channel boundary.
	 */
	if (curType != TT_SPACE)
	{
	    paint.r_xtop = ch->gcr_area.r_xtop;
	    RtrPaintStats(curType, paint.r_xtop - paint.r_xbot);
	    ui.pu_pNum = DBPlane(curType);
	    DBPaintPlane(def->cd_planes[ui.pu_pNum], &paint,
		DBStdPaintTbl(curType, ui.pu_pNum), &ui);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrPaintColumns --
 *
 * 	This procedure scans the result array for a channel, and paints
 *	material into def to correspond to stuff in the columns of the
 *	channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paint is added to the planes of def, corresponding to vertical
 *	wiring in the channel.  Contacts are not painted by this procedure,
 *	but poly is replaced with metal where maximization has occurred
 *	and where there are obstacles.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrPaintColumns(def, ch)
    CellDef * def;
    GCRChannel * ch;
{
    TileType curType;	/* Describes what kind of material currently
			 * occupies the column.  It's either RtrMetalType,
			 * RtrPolyType, or TT_SPACE.
			 */
    TileType nextType;	/* Material that is going to occupy the column next. */
    Rect paint;		/* Area to paint. */
    PaintUndoInfo ui;
    int row, col;
    short *column, code;

    ui.pu_def = def;
    for (col = 0; col <= ch->gcr_length; col++)
    {
	if (SigInterruptPending) return;
	column = ch->gcr_result[col];
	curType = TT_SPACE;

	for (row = 0; row <= ch->gcr_width; row++)
	{
	    /* Figure out what material needs to be present in this
	     * column between this row and the next one above.
	     */
	    
	    code = column[row];
	    if (code & GCRU)
	    {
		if ((code & GCRVM) || (code & GCRBLKP)
		    || (column[row+1] & GCRBLKP))
		    nextType = RtrMetalType;
		else nextType = RtrPolyType;
	    }
	    else nextType = TT_SPACE;

	    /* If the new type is different from the old, paint the old,
	     * and store the start of the new run.
	     */

	    if (nextType != curType)
	    {
		if (curType != TT_SPACE)
		{
		    paint.r_ytop = ch->gcr_origin.p_y + (row * RtrGridSpacing);
		    RtrPaintStats(curType, paint.r_ytop-paint.r_ybot);
		    if (curType == RtrMetalType)
			paint.r_ytop += RtrMetalWidth;
		    else paint.r_ytop += RtrPolyWidth;
		    ui.pu_pNum = DBPlane(curType);
		    DBPaintPlane(def->cd_planes[ui.pu_pNum], &paint,
			DBStdPaintTbl(curType, ui.pu_pNum), &ui);
		}

		paint.r_xbot = ch->gcr_origin.p_x + (col * RtrGridSpacing);
		paint.r_ybot = ch->gcr_origin.p_y + (row * RtrGridSpacing);
		if (nextType == RtrMetalType)
		{
		    paint.r_xtop = paint.r_xbot + RtrMetalWidth;
		}
		else		/* Poly or space. */
		{
		    paint.r_xtop = paint.r_xbot + RtrPolyWidth;
		}
		if (row == 0) paint.r_ybot = ch->gcr_area.r_ybot;
	    }
	    curType = nextType;
	}

	/*
	 * At the top of the column, we have to output any ongoing material.
	 * This material extends all the way to the top channel boundary.
	 */
	if (curType != TT_SPACE)
	{
	    paint.r_ytop = ch->gcr_area.r_ytop;
	    RtrPaintStats(curType, paint.r_ytop-paint.r_ybot);
	    ui.pu_pNum = DBPlane(curType);
	    DBPaintPlane(def->cd_planes[ui.pu_pNum], &paint,
		DBStdPaintTbl(curType, ui.pu_pNum), &ui);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrDoVia --
 *
 * 	Decide whether a via should be placed, but don't do the actual
 *	painting.  Do this based on the flag bits in adjacent locations.
 *	Look in all four directions from this location to see what kind
 *	of material converges on the location where we already know is
 *	a contact.  If the material is of both metal and poly, then need
 *	a via.
 *
 * Results:
 *	TRUE if a via should be placed.  Otherwise FALSE.
 *
 * Side effects:
 *	May set the flag GCRXX in the current location, to indicate that
 *	a real via was placed.  This is used later when painting the
 *	vertical runs.
 *
 * ----------------------------------------------------------------------------
 */

bool
rtrDoVia(ch, col, row)
    GCRChannel *ch;	/* The channel undergoing display */
    int col;	/* The x coordinate of the location considered */
    int row;	/* The y coordinate of the location considered */
{
    short up, down, left, right, mask;
    short **result, code;

    result = ch->gcr_result;
    code = result[col][row];
    if (code & (GCRBLKM|GCRBLKP))
	return (FALSE);

    if (!(code & GCRX))
    {
	if (col == 0 && (code & GCRR) && (result[1][row] & GCRBLKM))
	    return (TRUE);

	if (col == 1 && (code & GCRR) &&
		(result[0][row] & GCRR) && (result[0][row] & GCRBLKM))
	    return (TRUE);
    /*
     *	else
     *	if((row == 0) && !(code & GCRBLKP) && (code & GCRU) &&
     *		(result[col][1] & GCRBLKP))
     *	    return(TRUE);
     */
	return (FALSE);
    }

    right = result[col+1][row];
    if (col == 0) left = code;
    else left = result[col-1][row];
    up = result[col][row+1];
    if (row == 0) down = 0;
    else down = result[col][row-1];

    /* Build up a mask that indicates what layers route to this point.
     * If both poly (GCRBLKP) and metal (GCRBLKM) are present, then
     * place the contact.
     */
    mask = 0;
    if (code & GCRU)
    {
	if ((code & GCRVM) || (up & GCRBLKP)) mask |= GCRBLKM;
	else mask |= GCRBLKP;
    }
    if (code & GCRR)
    {
	if (right & GCRBLKM) mask |= GCRBLKP;
	else mask |= GCRBLKM;
    }
    if (down & GCRU)
    {
	if (down & (GCRVM|GCRBLKP)) mask |= GCRBLKM;
	else mask |= GCRBLKP;
    }
    if (left & GCRR)
    {
	if (left & GCRBLKM) mask |= GCRBLKP;
        else mask |= GCRBLKM;
    }

    if ((mask & (GCRBLKM|GCRBLKP)) == (GCRBLKM|GCRBLKP))
    {
	result[col][row] |= GCRXX;
	return (TRUE);
    }

    return(FALSE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrMaxMetal --
 *
 * Increase metal in the result by converting vertical polysilicon to
 * metal wherever possible.  Look at adjacent channels to avoid screwing
 * up the channel/channel connections.
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
rtrMaxMetal(ch)
    GCRChannel * ch;
{
    bool needLowX, needHiX, hasLowX, hasHiX, active, cross;
    int x, y, i, bottom, top;
    short **res, *col, code;

/* Scan each column in the channel from bottom to top */
    res=ch->gcr_result;
    for(x=1; x<=ch->gcr_length; x++)	/* For each column in the channel */
    {
    /*  Set the starting layer.  Look down to see if the next channel allows
     *  a starting run in metal.
     */
	col = res[x];
	if((col[0] & GCRU) && rtrMetalOkay(ch, x, GEO_SOUTH) &&
		! (col[0] & GCRBLKM))
	{
	    active = TRUE;
	    bottom = 0;
	    needLowX = FALSE;
	    hasLowX = FALSE;
	}
	else active = FALSE;

	for(y=1; y <= ch->gcr_width+1; y++) /* For each row in the channel */
	{
	/* Scan up from the bottom of the column, looking for the start or end
	 * of a vertical poly run.
	 */
	    code = col[y];
	    cross = ((code & GCRR) && (res[x - 1][y] & GCRR) && !(code & GCRX));
	    if(!active)
	    {
	    /* No current poly run.  See if this location starts one:  up,
	     * not metal blocked, and either a contact or else not crossing
	     * a metal line.
	     */
		if(!(code & GCRBLKM) && (code & GCRU) && !cross)
		{
		    active = TRUE;
		    top = bottom = y;
		    needLowX = !((code & GCRR) && (res[x-1][y] & GCRR));
		    hasLowX = code & GCRX;
		}
		continue;
	    }
	    else
	    {
	    /* Currently extending a poly run.  See if this location ends one:
	     *   a metal obstacle,
	     *   an unconnected track crossing
	     *   an end of column
	     *   top of the channel
	     */
		if((code & GCRBLKM) || cross)
		{
		/* This location is blocked.  Back off one track and convert
		 * the bottom..top span to metal.
		 */
		    top = y - 1;
		    hasHiX = col[top] & GCRX;
		    needHiX = TRUE;
		}
		else
		/* If we've reached the top of the channel, see if metal can
		 * continue into the next channel.  Watch out:  must have this
		 * check before the GCRU check, because the top row NEVER has
		 * GCRU set, even if wire extends to next channel.
		 */
		if(y > ch->gcr_width)
		{
		    if(rtrMetalOkay(ch, x, GEO_NORTH))
		    {
			top = y;
			needHiX = FALSE;
			hasHiX = FALSE;
		    }
		    else
		    {
			top = y -1;
			hasHiX = col[top] & GCRX;
			needHiX = TRUE;
		    }
		}
		else
		if(!(code & GCRU))
		{
		/* The vertical wire ends at this grid point */
		    top = y;
		    needHiX = FALSE;
		    hasHiX = TRUE;
		}
		else continue;
	    }

	/* Can convert the run from bottom to top into metal.  Only
	 * do so if no new vias are created, an existing via is moved,
	 * or at least RtrViaLimit units are converted to metal.
	 */
	    if(  (bottom < top) &&
	 	 ( ((needLowX + needHiX - hasHiX - hasLowX) <= 1) ||
		   ((top - bottom) >= RtrViaLimit) )  )
	    {
		for(i = bottom; i< top; i++)
		    col[i]|=GCRVM;
		if(needLowX) col[bottom]|=GCRX;
		if(needHiX) col[top]|=GCRX;
	    }
	    active = FALSE;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrMetalOkay --
 *
 * Look at the given column in the next channel to see if it is
 * okay to paint metal into it.  It is okay if the corresponding
 * location is not metal blocked, and has no metal first track crossing.
 * It's automatically ok if the adjacent channel is a cell:  stems
 * can connect to either layer.
 *
 * It is not necessary to do this for track crossings, since they
 * are metal unless some metal obstacle prevents the crossing, and
 * the crossing will be found and noted in glBlock.
 *
 * Results:
 *	TRUE if okay to paint metal.  Otherwise FALSE.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
rtrMetalOkay(ch, col, dir)
    GCRChannel *ch;	/* The originating channel for the search */
    int col;		/* The crossing column in the originating channel */
    int dir;		/* Direction of the crossing NORTH or SOUTH */
{
    GCRChannel *newCh;
    GCRPin *pin;
    short flags;
    int newCol;

    switch (dir)
    {
	case GEO_NORTH:	pin = &ch->gcr_tPins[col]; break;
	case GEO_SOUTH:	pin = &ch->gcr_bPins[col]; break;
    }

    /* If adjacent to a cell, can use either layer */
    if (pin->gcr_linked == NULL)
	return (TRUE);

    newCh = pin->gcr_linked->gcr_ch;
    newCol = pin->gcr_linked->gcr_x;

    if (dir == GEO_NORTH)
	flags = newCh->gcr_result[newCol][1];
    else
	flags = newCh->gcr_result[newCol][newCh->gcr_width];

    if (flags & (GCRX|GCRXX))
	return (TRUE);

    return ((flags & (GCRBLKM|GCRR)) == 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrPaintContact --
 *
 * 	This procedure is called to paint contacts in the router.  It
 *	paints in material for the contact, plus for any surrounds that
 *	are required by the technology file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Material is added to the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrPaintContact(def, area)
    CellDef *def;		/* Cell in which to paint contact. */
    Rect *area;			/* Area in which to paint the contact. */
{
    Rect larger;

    RtrPaintStats(RtrContactType, 0);
    DBPaint(def, area, RtrContactType);
    if (RtrMetalSurround != 0)
    {
	GEO_EXPAND(area, RtrMetalSurround, &larger);
	DBPaint(def, &larger, RtrMetalType);
    }
    if (RtrPolySurround != 0)
    {
	GEO_EXPAND(area, RtrPolySurround, &larger);
	DBPaint(def, &larger, RtrPolyType);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrPaintStats --
 *
 * 	Keep statistics on current routing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the global variables for statistics.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrPaintStats(type, distance)
    TileType type;
    int distance;
{
    if (distance < 0) distance = -distance;

    if (type == RtrMetalType) rtrMetalLength += distance;
    else if (type == RtrPolyType) rtrPolyLength += distance;
    else if (type == RtrContactType) rtrViaCount++;
    else
    {
	TxPrintf("Total length %d;  Metal %d;  Poly %d;  Vias %d\n",
		rtrMetalLength + rtrPolyLength, rtrMetalLength, rtrPolyLength,
		rtrViaCount);
	rtrMetalLength = 0;
	rtrPolyLength = 0;
	rtrViaCount = 0;
    }
}
