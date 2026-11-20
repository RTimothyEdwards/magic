/*
 * DBpaint2.c --
 *
 * More paint and erase primitives
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBpaint2.c,v 1.6 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <sys/types.h>
#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"

/*
 * ----------------------------------------------------------------------------
 * DBPaint --
 *
 * Paint a rectangular area with a specific tile type.
 * All paint tile planes in cellDef are painted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies potentially all paint tile planes in cellDef.
 * ----------------------------------------------------------------------------
 */

/* When TRUE, DBPaint() does not resolve images (see below) after each
 * call.  It records the area and the types painted instead, and
 * DBResolveDeferredImages() resolves them all at once.  Used by the DEF
 * reader, where resolving images after every route segment was most of
 * the time spent reading a routed design.
 */
bool DBPaintDeferImages = FALSE;
static Rect dbDeferredArea = {{0, 0}, {0, 0}};
static TileTypeBitMask dbDeferredTypes;
static bool dbDeferredAny = FALSE;

void
DBPaint (cellDef, rect, type)
    CellDef  * cellDef;		/* CellDef to modify */
    Rect     * rect;		/* Area to paint */
    TileType   type;		/* Type of tile to be painted */
{
    int pNum;
    PaintUndoInfo ui;
    TileType  loctype = type;   /* Local value of tile type */

    Rect brect;
    GEO_EXPAND(rect, 1, &brect);

    if (type & TT_DIAGONAL)
	loctype = (type & TT_SIDE) ?
		(type & TT_RIGHTMASK) >> 14 : (type & TT_LEFTMASK);

    cellDef->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    ui.pu_def = cellDef;
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (DBPaintOnPlane(loctype, pNum))
	{
	    ui.pu_pNum = pNum;
	    DBNMPaintPlane(cellDef->cd_planes[pNum], type, rect,
			DBStdPaintTbl(loctype, pNum), &ui);
	    DBMergeNMTiles(cellDef->cd_planes[pNum], &brect, &ui);
	}

    /* Resolve images over all their planes.  This allows the	*/
    /* definition of composite types generated from types on	*/
    /* different planes, as well as relaxing the constraints on	*/
    /* the "compose" section of the technology file.		*/

    if ((loctype < DBNumUserLayers) && DBPaintDeferImages)
    {
	if (!dbDeferredAny)
	{
	    dbDeferredArea = *rect;
	    TTMaskZero(&dbDeferredTypes);
	    dbDeferredAny = TRUE;
	}
	else
	    GeoInclude(rect, &dbDeferredArea);
	TTMaskSetType(&dbDeferredTypes, loctype);
    }
    else if (loctype < DBNumUserLayers)
    {
	TileTypeBitMask *rMask, tMask;
	TileType itype;
	int dbResolveImages();

	for (itype = TT_SELECTBASE; itype < DBNumUserLayers; itype++)
	{
	    /* Ignore self, or infinite looping will result */
	    if (itype == loctype) continue;

	    rMask = DBResidueMask(itype);
	    if (TTMaskHasType(rMask, loctype))
	    {
		TTMaskZero(&tMask);
		TTMaskSetType(&tMask, itype);
		for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
		{
		    if (DBPaintOnPlane(itype, pNum))
		    {
			DBSrPaintNMArea((Tile *)NULL, cellDef->cd_planes[pNum],
				type, rect, &tMask, dbResolveImages,
				(ClientData)cellDef);
    		    }
		}
    	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintImagesNeverChange --
 *
 *	Check, from the paint tables of the current technology, that the
 *	image resolution DBPaint() does after painting a type can never
 *	change anything.  That holds if, for every type T and every type J
 *	whose residues include T (the types DBPaint() resolves after
 *	painting T), on every plane p that T is painted on:
 *
 *	  - painting T over J leaves J (J is not changed on any of its
 *	    planes), and
 *	  - painting T over any other type never produces J (J is never
 *	    created on one of its planes only).
 *
 *	Then an image that is on all of its planes stays so after any
 *	paint, and resolving it repaints it unchanged.  This is the case for
 *	the technologies tried (scmos variants, IHP SG13G2), where routes
 *	painted over contacts leave the contacts as they are.
 *
 * Results:
 *	TRUE if resolving images after each DBPaint() is never needed, so
 *	that deferring it (DBPaintDeferImages) gives the same result.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBPaintImagesNeverChange()
{
    TileType t, j, h;
    int pNum;

    for (t = TT_SELECTBASE; t < DBNumUserLayers; t++)
	for (j = TT_SELECTBASE; j < DBNumUserLayers; j++)
	{
	    if ((j == t) || !TTMaskHasType(DBResidueMask(j), t)) continue;
	    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	    {
		if (!DBPaintOnPlane(t, pNum)) continue;
		if (DBPaintOnPlane(j, pNum) && (DBStdPaintEntry(j, t, pNum) != j))
		    return FALSE;
		for (h = TT_SPACE; h < DBNumTypes; h++)
		    if ((h != j) && (DBStdPaintEntry(h, t, pNum) == j))
			return FALSE;
	    }
	}
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBResolveDeferredImages --
 *
 *	Resolve the images that DBPaint() skipped while DBPaintDeferImages
 *	was set:  for every type whose residues include a type painted in
 *	that time (other than the type itself), find its tiles on each of
 *	its planes over the whole area painted, and paint them again so
 *	that the type appears on all of its planes.  This does what DBPaint()
 *	does after each call, once for all of the calls.  Callers defer only
 *	when DBPaintImagesNeverChange() is TRUE, in which case resolving after
 *	each call would have changed nothing, so the result is the same;  this
 *	pass then finds nothing to do and is kept as a safety net (it costs
 *	one search per image type and plane over the area painted).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May paint into cellDef.  Clears the record of deferred paint.
 *
 * ----------------------------------------------------------------------------
 */

void
DBResolveDeferredImages(cellDef)
    CellDef *cellDef;
{
    TileTypeBitMask *rMask, tMask, others;
    TileType itype;
    Rect area;
    int pNum;
    int dbResolveImages();

    if (!dbDeferredAny) return;
    dbDeferredAny = FALSE;
    area = dbDeferredArea;

    for (itype = TT_SELECTBASE; itype < DBNumUserLayers; itype++)
    {
	/* As in DBPaint(), a type is not an image of itself */
	others = dbDeferredTypes;
	TTMaskClearType(&others, itype);
	rMask = DBResidueMask(itype);
	if (!TTMaskIntersect(rMask, &others)) continue;

	TTMaskZero(&tMask);
	TTMaskSetType(&tMask, itype);
	for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	    if (DBPaintOnPlane(itype, pNum))
		DBSrPaintNMArea((Tile *)NULL, cellDef->cd_planes[pNum],
			itype, &area, &tMask, dbResolveImages,
			(ClientData)cellDef);
    }
}

/*
 * dbResolveImages ---
 *
 *	This callback function is called from DBSrPaintArea and
 *	makes a recursive call to DBPaint() for tiles that have
 *	images on multiple planes but which may not have been
 *	painted on all of those planes due to restrictions of
 *	the paint table.
 */

int
dbResolveImages(tile, dinfo, cellDef)
    Tile *tile;
    TileType dinfo;
    CellDef *cellDef;
{
    Rect rect;

    TiToRect(tile, &rect);

    /* Recursive call back to DBPaint---this will ensure that   */
    /* all of the planes of the image type are painted.         */

    DBPaint(cellDef, &rect, TiGetTypeExact(tile) | dinfo);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 * DBErase --
 *
 * Erase a specific tile type from a rectangular area.
 * The plane in which tiles of the given type reside is modified
 * in cellDef.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies potentially all paint tile planes in cellDef.
 * ----------------------------------------------------------------------------
 */

void
DBErase (cellDef, rect, type)
    CellDef  * cellDef;		/* Cell to modify */
    Rect     * rect;		/* Area to paint */
    TileType   type;		/* Type of tile to be painted */
{
    int pNum;
    PaintUndoInfo ui;
    TileType   loctype = type;  /* Local value of tile type */

    Rect brect;
    bool allPlane = FALSE;

    if (GEO_SAMERECT(*rect, TiPlaneRect))
	allPlane = TRUE;
    else
	GEO_EXPAND(rect, 1, &brect);

    if (type & TT_DIAGONAL)
	loctype = (type & TT_SIDE) ?
		(type & TT_RIGHTMASK) >> 14 : (type & TT_LEFTMASK);

    cellDef->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    ui.pu_def = cellDef;
    if (loctype == TT_SPACE)
    {
	/*
	 * Erasing space is the same as erasing everything under
	 * the rectangle.
	 */
	for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	{
	    ui.pu_pNum = pNum;
	    DBNMPaintPlane(cellDef->cd_planes[pNum], type, rect,
			DBStdPaintTbl(loctype, pNum), &ui);
	    if (!allPlane)
		DBMergeNMTiles(cellDef->cd_planes[pNum], &brect, &ui);
	}
    }
    else
    {
	/*
	 * Ordinary type is being erased.
	 * Generate the erase on all planes in cellDef.
	 */
	for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	    if (DBEraseOnPlane(loctype, pNum))
	    {
		ui.pu_pNum = pNum;
		DBNMPaintPlane(cellDef->cd_planes[pNum], type, rect,
			    DBStdEraseTbl(loctype, pNum), &ui);
		if (!allPlane)
		    DBMergeNMTiles(cellDef->cd_planes[pNum], &brect, &ui);
	    }
    }
}

/*
 * ----------------------------------------------------------------------------
 * DBPaintMask --
 *
 * Paint a rectangular area with all tile types specified in the
 * mask supplied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies potentially all paint tile planes in cellDef.
 * ----------------------------------------------------------------------------
 */

void
DBPaintMask(cellDef, rect, mask)
    CellDef	*cellDef;	/* CellDef to modify */
    Rect	*rect;		/* Area to paint */
    TileTypeBitMask *mask;	/* Mask of types to be erased */
{
    TileType t;

    for (t = TT_SPACE + 1; t < DBNumTypes; t++)
	if (TTMaskHasType(mask, t))
	    DBPaint(cellDef, rect, t);
}

/*
 * ----------------------------------------------------------------------------
 * DBPaintValid --
 *
 * Paint a rectangular area with all tile types specified in the
 * mask supplied, ANDed with valid layers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies potentially all paint tile planes in cellDef.
 * ----------------------------------------------------------------------------
 */

void
DBPaintValid(cellDef, rect, mask, dinfo)
    CellDef	*cellDef;	/* CellDef to modify */
    Rect	*rect;		/* Area to paint */
    TileTypeBitMask *mask;	/* Mask of types to be erased */
    TileType 	dinfo;		/* If non-zero, then rect is a triangle and
				 * dinfo contains side and direction information
				 */
{
    TileType t, tt, tloc, dloc;
    TileTypeBitMask rmask, mmask, *tMask;

    dloc = dinfo & (TT_DIAGONAL | TT_SIDE | TT_DIRECTION);

    TTMaskZero(&mmask);
    TTMaskSetMask(&mmask, mask);

    /* Decompose stacked contacts */

    for (t = DBNumUserLayers; t < DBNumTypes; t++)
	if (TTMaskHasType(mask, t))
	    TTMaskSetMask(&mmask, DBResidueMask(t));

    /* Remove any inactive layers */

    TTMaskAndMask(&mmask, &DBActiveLayerBits);

    /* If any residue of a contact is not in the active layers	*/
    /* list, then paint only the valid residue layers.		*/

    for (t = TT_SELECTBASE; t < DBNumUserLayers; t++)
	if (TTMaskHasType(&mmask, t))
	{
	    if (DBIsContact(t))
	    {
		tMask = DBResidueMask(t);
		TTMaskAndMask3(&rmask, tMask, &DBActiveLayerBits);
		if (TTMaskEqual(&rmask, tMask))
		{
		    tloc = dloc | ((dinfo & TT_DIAGONAL) ? ((dinfo & TT_SIDE) ?
				(t << 14) : t) : t);
		    DBPaint(cellDef, rect, tloc);
		}
		else if (!TTMaskIsZero(&rmask))
		{
		    for (tt = TT_SPACE + 1; tt < DBNumTypes; tt++)
			if (TTMaskHasType(&rmask, tt))
			{
			    tloc = dloc | ((dinfo & TT_DIAGONAL) ?
					((dinfo & TT_SIDE) ? (tt << 14) : tt) : tt);
			    DBPaint(cellDef, rect, tloc);
			}
		}
	    }
	    else
	    {
		tloc = dloc | ((dinfo & TT_DIAGONAL) ? ((dinfo & TT_SIDE) ?
				(t << 14) : t) : t);
		DBPaint(cellDef, rect, tloc);
	    }
	}
}

/*
 * ----------------------------------------------------------------------------
 * DBEraseMask --
 *
 * Erase a rectangular area with all tile types specified in the
 * mask supplied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies potentially all paint tile planes in cellDef.
 * ----------------------------------------------------------------------------
 */

void
DBEraseMask(cellDef, rect, mask)
    CellDef	*cellDef;	/* CellDef to modify */
    Rect	*rect;		/* Area to erase */
    TileTypeBitMask *mask;	/* Mask of types to be erased */
{
    TileType t;

    /* Corrected to restore erasing of error layers, which is	*/
    /* functionality lost since magic version 7.1.  Modified by	*/
    /* BIM 8/18/2018						*/

    for (t = DBNumTypes - 1; t >= TT_PAINTBASE; t--)
	if (TTMaskHasType(mask, t))
	    DBErase(cellDef, rect, t);
}

/*
 * ----------------------------------------------------------------------------
 * DBEraseValid --
 *
 * Erase a rectangular area with all tile types specified in the
 * mask supplied, ANDed with valid layers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies potentially all paint tile planes in cellDef.
 * ----------------------------------------------------------------------------
 */

void
DBEraseValid(cellDef, rect, mask, dinfo)
    CellDef	*cellDef;	/* CellDef to modify */
    Rect	*rect;		/* Area to erase */
    TileTypeBitMask *mask;	/* Mask of types to be erased */
    TileType 	dinfo;		/* w/Non-Manhattan geometry, "rect" is a
				 * triangle and dinfo holds side & direction
				 */
{
    TileType t, tt, tloc, dloc;
    TileTypeBitMask rmask, mmask, *tMask;

    dloc = dinfo & (TT_DIAGONAL | TT_SIDE | TT_DIRECTION);

    /* Remove any inactive layers */

    TTMaskAndMask3(&mmask, mask, &DBActiveLayerBits);

    /* If any residue of a contact is not in the active layers	*/
    /* list, then erase the contact in multiple passes.		*/

    for (t = TT_SELECTBASE; t < DBNumUserLayers; t++)
	if (TTMaskHasType(&mmask, t))
	{
	    if (DBIsContact(t))
	    {
		tMask = DBResidueMask(t);
		TTMaskAndMask3(&rmask, tMask, &DBActiveLayerBits);
		if (TTMaskEqual(&rmask, tMask))
		{
		    tloc = dloc | ((dinfo & TT_DIAGONAL) ? ((dinfo & TT_SIDE) ?
				(t << 14) : t) : t);
		    DBErase(cellDef, rect, tloc);
		}
		else if (!TTMaskIsZero(&rmask))
		{
		    for (tt = TT_SELECTBASE; tt < DBNumUserLayers; tt++)
			if (TTMaskHasType(&rmask, tt))
			{
			    tloc = dloc | ((dinfo & TT_DIAGONAL) ?
					((dinfo & TT_SIDE) ?  (tt << 14) : tt) : tt);
			    DBErase(cellDef, rect, tloc);
			}
		}
	    }
	    else
	    {
		tloc = dloc | ((dinfo & TT_DIAGONAL) ? ((dinfo & TT_SIDE) ?
			 (t << 14) : t) : t);
		DBErase(cellDef, rect, tloc);
	    }
	}
}
