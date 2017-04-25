/*
 * grouteChan.c --
 *
 * Code to maintain a map of usable channel areas, using a
 * tile plane.  One tile is used for each "unobstructed"
 * channel area, namely one that contains no embedded regions
 * where the channel's density has been exceeded.
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

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "debug/debug.h"
#include "gcr/gcr.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "utils/signals.h"
#include "router/router.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "utils/malloc.h"

/* Plane to hold channel information */
Plane *glChanPlane;

/* Dummy celldef whose PL_DRC_CHECK plane is identical to glChanPlane above */
CellDef *glChanDef;
CellUse *glChanUse;

TileTypeBitMask glMaskRiver;	/* Mask of river channel types */
TileTypeBitMask glMaskNormal;	/* Mask of normal channel type */
TileTypeBitMask	glMaskChannel;	/* Mask of all channel types */

int glChanSplitRiver();
int glChanRiverBlock();
int glChanFloodVFunc(), glChanFloodHFunc();
bool glChanClip();
int glChanClipFunc();
int glChanMergeFunc();
int glChanFeedFunc();
int glChanPaintFunc();
int glChanSetClient();
void glChanFreeMap();
void glChanCheckCover();
void glChanBlockDens();
void glChanFlood();
void glChanShowTiles();
int glChanShowFunc();

typedef struct pa
{
    Rect	 pa_area;
    TileType	 pa_type;
    struct pa	*pa_next;
} PaintArea;

PaintArea *glChanPaintList;

/*
 * ----------------------------------------------------------------------------
 *
 * glChanBuildMap --
 *
 * Build a tile plane that represents all the channels in the routing
 * problem.  This tile plane will contain tiles of four types: CT_BLOCKED,
 * CT_NORMAL, CT_HRIVER, and CT_VRIVER, corresponding to unusable routing
 * area and the three types of channels respectively.
 *
 * Each channel tile's client field points back to the channel it covers.
 * A single channel can be covered by several tiles, however.  Initially,
 * each normal channel is covered by a single tile, but each river tile
 * is covered by several tiles.  For example, the tiles covering a HRIVER
 * channel are chosen such that there is never a vertical tile boundary
 * on either their RHS or LHS except at the top or bottom of the CT_HRIVER
 * tile.  Situations such as the following:
 *
 *		+-------+
 *		|	|
 *		|	| normal
 *		|	|
 *		|HRIVER	+------
 *		|	|
 *		|	| normal
 *		|	|
 *		+-------+
 *
 * aren't allowed to exist; the HRIVER tile gets split:
 *
 *		+-------+
 *		|	|
 *		|HRIVER	| normal
 *		|	|
 *		+-------+------
 *		|	|
 *		|HRIVER	| normal
 *		|	|
 *		+-------+
 *	
 * The reason for splitting CT_HRIVER tiles in this way (and CT_VRIVER tiles
 * vertically using similar criteria) is to ensure that there is a different
 * river tile for each channel on its opposite side.  This fact is crucial
 * to the working of the inner loop of the global router; see the code in
 * groutePair.c for details of the algorithm used.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Builds the tile plane glChanPlane as described above.
 *
 * ----------------------------------------------------------------------------
 */

void
glChanBuildMap(chanList)
    GCRChannel *chanList;	/* List of all channels in routing problem */
{
    GCRChannel *ch;
    bool workDone;

    if (glChanPlane == NULL)
    {
	DBNewYank("__CHANMAP__", &glChanUse, &glChanDef);
	glChanPlane = glChanDef->cd_planes[PL_DRC_CHECK];
	glChanFreeMap();
	TTMaskSetOnlyType(&glMaskRiver, CHAN_HRIVER);
	TTMaskSetType(&glMaskRiver, CHAN_VRIVER);
	TTMaskSetOnlyType(&glMaskNormal, CHAN_NORMAL);
	TTMaskSetMask3(&glMaskChannel, &glMaskNormal, &glMaskRiver);
    }

    /*
     * First pass: use painting to cover the areas of all channels
     * with the appropriate type of tile.
     */
    for (ch = chanList; ch; ch = ch->gcr_next)
	DBPaintPlane(glChanPlane, &ch->gcr_area, DBStdWriteTbl(ch->gcr_type),
			(PaintUndoInfo *) NULL);

    if (DebugIsSet(glDebugID, glDebTiles))
	glChanShowTiles("After painting all channels");

    /*
     * Second pass: use splits and merges to ensure that each channel
     * covers exactly one tile.  Leave this tile's ti_client field
     * pointing to 'ch'.
     */
    do
    {
	workDone = FALSE;
	for (ch = chanList; ch; ch = ch->gcr_next)
	    if (glChanClip(ch))
		workDone = TRUE;
    }
    while (workDone);
    if (DebugIsSet(glDebugID, glDebTiles))
	glChanShowTiles("After splits and merges");
    if (DebugIsSet(glDebugID, glDebChan))
	glChanCheckCover(chanList, &glMaskChannel);

    /*
     * Third pass: find regions of maximum density and "paint" them into
     * the tile map.  Regions where the density is exceeded in only one
     * of the two possible directions through a normal channel result in
     * painting a CHAN_HRIVER or CHAN_VRIVER tile as appropriate, while
     * regions that are totally blocked result in CHAN_BLOCKED tiles.
     * The client fields of the remaining nonblocked tiles are left
     * pointing to the channels those tiles overlap.
     */
    for (ch = chanList; ch; ch = ch->gcr_next)
	glChanBlockDens(ch);
    if (DebugIsSet(glDebugID, glDebTiles))
	glChanShowTiles("After density blockages");

    /*
     * Fourth pass: split river tiles as necessary to ensure
     * that no river tile has a perpendicular tile boundary
     * on either of the sides from which signals can enter.
     */
    while (DBSrPaintArea((Tile *) NULL, glChanPlane, &TiPlaneRect, &glMaskRiver,
		glChanSplitRiver, (ClientData) NULL))
	/* Nothing */;
    if (DebugIsSet(glDebugID, glDebTiles))
	glChanShowTiles("After splitting river tiles");

    /*
     * Final pass: turn any river tiles whose pins are completely
     * blocked on one side into blocked tiles.  This just involves
     * changing the type of the tile.
     */
    (void) DBSrPaintArea((Tile *) NULL, glChanPlane, &TiPlaneRect,
		&glMaskRiver, glChanRiverBlock, (ClientData) NULL);
    if (DebugIsSet(glDebugID, glDebTiles))
	glChanShowTiles("After blocking river tiles");
    if (DebugIsSet(glDebugID, glDebChan))
    {
	glChanCheckCover(chanList, &glMaskNormal);
	(void) DBSrPaintArea((Tile *) NULL, glChanPlane, &TiPlaneRect,
		&glMaskChannel, glChanFeedFunc, (ClientData) NULL);
    }
}

/*
 * glChanFeedFunc --
 *
 * Used for leaving feedback for each channel tile above.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Leaves feedback for each tile found.
 */

int
glChanFeedFunc(tile)
    Tile *tile;
{
    char *mesg;
    Rect r;

    switch (TiGetType(tile))
    {
	case CHAN_NORMAL: mesg = "normal channel"; break;
	case CHAN_HRIVER: mesg = "horizontal river channel"; break;
	case CHAN_VRIVER: mesg = "vertical river channel"; break;
    }
    TITORECT(tile, &r);
    DBWFeedbackAdd(&r, mesg, EditCellUse->cu_def, 1, STYLE_OUTLINEHIGHLIGHTS);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanFreeMap --
 *
 * Free the tile plane built by glChanBuildMap().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * ----------------------------------------------------------------------------
 */

void
glChanFreeMap()
{
    Tile *newCenterTile;

    /* Eliminate all the tiles from this plane */
    DBFreePaintPlane(glChanPlane);

    /* Allocate a new central space tile */
    newCenterTile = TiAlloc();
    glChanPlane->pl_hint = newCenterTile;
    TiSetBody(newCenterTile, CHAN_BLOCKED);
    dbSetPlaneTile(glChanPlane, newCenterTile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanCheckCover --
 *
 * Debugging procedure used to check that each channel whose type is
 * contained in 'mask' is covered by exactly one tile.  Also check
 * all channels to make sure that the tiles they cover are the same
 * as the channel type (except for blockages).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Leaves feedback where errors occur.
 *
 * ----------------------------------------------------------------------------
 */

int glChanCheckCount;

void
glChanCheckCover(chanList, mask)
    GCRChannel *chanList;
    TileTypeBitMask *mask;
{
    int glChanCheckFunc();
    GCRChannel *ch;
    char mesg[1024];

    for (ch = chanList; ch; ch = ch->gcr_next)
    {
	glChanCheckCount = 0;
	(void) DBSrPaintArea((Tile *) NULL, glChanPlane, &ch->gcr_area,
		&DBAllTypeBits, glChanCheckFunc,
		(ClientData) ch);
	if (TTMaskHasType(mask, ch->gcr_type) && glChanCheckCount != 1)
	{
	    (void) sprintf(mesg, "%d tiles over channel", glChanCheckCount);
	    DBWFeedbackAdd(&ch->gcr_area, mesg, EditCellUse->cu_def, 1,
			STYLE_PALEHIGHLIGHTS);
	}
    }
}

/*
 * glChanCheckFunc --
 *
 * Called by above for each tile overlapping a channel.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	May leave feedback if a channel is overlapped by the
 *	wrong type of tile.
 */

int
glChanCheckFunc(tile, ch)
    Tile *tile;
    GCRChannel *ch;
{
    char mesg[1024];
    Rect r;

    glChanCheckCount++;
    if (NOTBLOCKED(tile))
    {
	if (TiGetType(tile) != ch->gcr_type)
	{
	    TITORECT(tile, &r);
	    (void) sprintf(mesg, "Different tile type %d for chan %d",
		    TiGetType(tile), ch->gcr_type);
	    DBWFeedbackAdd(&r, mesg, EditCellUse->cu_def,
		    1, STYLE_MEDIUMHIGHLIGHTS);
	}
	if (tile->ti_client != (ClientData) ch)
	{
	    TITORECT(tile, &r);
	    (void) sprintf(mesg, "Tile client 0x%"DLONG_PREFIX"x doesn't match chan %p",
		    (dlong) tile->ti_client, ch);
	    DBWFeedbackAdd(&r, mesg, EditCellUse->cu_def,
		    1, STYLE_MEDIUMHIGHLIGHTS);
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanClip --
 *
 * Leave a single tile overlapping the area of channel 'ch'.
 * Leave this tile pointing to 'ch'.
 *
 * Results:
 *	Returns TRUE if we had to do any splits or merges,
 *	FALSE if everything was as it should be.
 *
 * Side effects:
 *	May split or merge tiles.
 *
 * ----------------------------------------------------------------------------
 */

bool
glChanClip(ch)
    GCRChannel *ch;
{
    bool ret;

    ret = FALSE;

    /* First part: clip every tile overlapping this channel to its boundary */
    while (DBSrPaintArea((Tile *) NULL, glChanPlane, &ch->gcr_area,
	    &DBAllTypeBits, glChanClipFunc, (ClientData) &ch->gcr_area))
	ret = TRUE;

    /* Second part: set the client field of every tile to 'ch' */
    (void) DBSrPaintArea((Tile *) NULL, glChanPlane, &ch->gcr_area,
	    &DBAllTypeBits, glChanSetClient, (ClientData) ch);

    /*
     * Third part: merge every tile overlapping this channel into a
     * single one.  The procedure called for each tile extracts the
     * channel boundaries from the client field of the tile, and
     * then generates merges within the channel boundary.
     */
    while (DBSrPaintArea((Tile *) NULL, glChanPlane, &ch->gcr_area,
	    &DBAllTypeBits, glChanMergeFunc, (ClientData) NULL))
	ret = TRUE;

    if (DebugIsSet(glDebugID, glDebTiles))
    {
	char mesg[256];

	(void) sprintf(mesg, "After clipping chan %p", ch);
	glChanShowTiles(mesg);
    }

    return ret;
}

int
glChanSetClient(tile, cdata)
    Tile *tile;
    ClientData cdata;
{
    tile->ti_client = cdata;
    return 0;
}

void
glChanShowTiles(mesg)
    char *mesg;
{
    char answer[100], m[1024];

    DBWAreaChanged(glChanDef, &TiPlaneRect, DBW_ALLWINDOWS, 0);
    WindUpdate();
    (void) sprintf(m, "%s: --more-- (t for tiles): ", mesg);
    if (TxGetLinePrompt(answer, sizeof answer, m) == NULL || answer[0] != 't')
	return;

    (void) DBSrPaintArea((Tile *) NULL, glChanPlane, &TiPlaneRect,
	    &DBAllTypeBits, glChanShowFunc, (ClientData) NULL);
}

int
glChanShowFunc(tile)
    Tile *tile;
{
    GCRChannel *ch;
    char mesg[1024];
    Rect r;

    TITORECT(tile, &r);
    ShowRect(EditCellUse->cu_def, &r, STYLE_PALEHIGHLIGHTS);
    (void) sprintf(mesg, "tile ch=%"DLONG_PREFIX"x type=%d",
		(dlong) tile->ti_client, TiGetType(tile));
    TxMore(mesg);
    ShowRect(EditCellUse->cu_def, &r, STYLE_ERASEHIGHLIGHTS);
    if (tile->ti_client == (ClientData) CLIENTDEFAULT)
	return 0;
    ch = (GCRChannel *) tile->ti_client;
    ShowRect(EditCellUse->cu_def, &ch->gcr_area, STYLE_MEDIUMHIGHLIGHTS);
    (void) sprintf(mesg, "chan %p type=%d", ch, ch->gcr_type);
    TxMore(mesg);
    ShowRect(EditCellUse->cu_def, &ch->gcr_area, STYLE_ERASEHIGHLIGHTS);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanClipFunc --
 *
 * Called by DBSrPaintArea() on behalf of glChanClip() above for each
 * tile overlapping 'area': splits 'tile' as necessary to ensure that
 * it is completely contained within area.
 *
 * Results:
 *	Returns 1 if we did any splitting, 0 otherwise.
 *
 * Side effects:
 *	May split tiles.
 *	When a tile is split, the new tile's body and ti_client
 *	field are set to copies of the original tile's.
 *
 * ----------------------------------------------------------------------------
 */

int
glChanClipFunc(tile, area)
    Tile *tile;
    Rect *area;
{
    ClientData tileClient = tile->ti_client;
    TileType type = TiGetType(tile);
    Tile *newTile;
    int ret;

    ret = 0;
    if (LEFT(tile) < area->r_xbot)
    {
	tile = TiSplitX(tile, area->r_xbot);
	TiSetBody(tile, type);
	tile->ti_client = tileClient;
	ret = 1;
    }
    if (BOTTOM(tile) < area->r_ybot)
    {
	tile = TiSplitY(tile, area->r_ybot);
	TiSetBody(tile, type);
	tile->ti_client = tileClient;
	ret = 1;
    }
    if (RIGHT(tile) > area->r_xtop)
    {
	newTile = TiSplitX(tile, area->r_xtop);
	TiSetBody(newTile, type);
	newTile->ti_client = tileClient;
	ret = 1;
    }
    if (TOP(tile) > area->r_ytop)
    {
	newTile = TiSplitY(tile, area->r_ytop);
	TiSetBody(newTile, type);
	newTile->ti_client = tileClient;
	ret = 1;
    }

    return ret;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanMergeFunc --
 *
 * Called by DBSrPaintArea() on behalf of glChanClip() above for each
 * tile contained within some area.  Merges this tile with any of
 * its neighbors within its channel's area, where the channel is
 * pointed to by the ti_client field.  We leave the resulting tile's
 * ti_client field pointing to this channel ('ch').
 *
 * IMPORTANT ASSUMPTION: no tile actually overlaps the boundary of
 * ch->gcr_area.  This is ensured by the caller's having called
 * glChanClipFunc() enough times to have split all the tiles
 * that originally might have crossed the channel's boundary.
 *
 * Results:
 *	Returns 1 if we did any merging, 0 otherwise.
 *
 * Side effects:
 *	May merge tiles.
 *	Leaves tile->ti_client set to 'ch'.
 *
 * ----------------------------------------------------------------------------
 */

int
glChanMergeFunc(tile)
    Tile *tile;
{
    GCRChannel *ch = (GCRChannel *) tile->ti_client;
    Tile *tp;
    int ret;

    ret = 0;
    if (TOP(tile) < ch->gcr_area.r_ytop)
    {
	tp = RT(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& LEFT(tp) == LEFT(tile)
		&& RIGHT(tp) == RIGHT(tile))
	{
	    TiJoinY(tile, tp, glChanPlane);
	    ret = 1;
	}
    }
    if (LEFT(tile) > ch->gcr_area.r_xbot)
    {
	tp = BL(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& TOP(tp) == TOP(tile)
		&& BOTTOM(tp) == BOTTOM(tile))
	{
	    TiJoinX(tile, tp, glChanPlane);
	    ret = 1;
	}
    }
    if (BOTTOM(tile) > ch->gcr_area.r_ybot)
    {
	tp = LB(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& LEFT(tp) == LEFT(tile)
		&& RIGHT(tp) == RIGHT(tile))
	{
	    TiJoinY(tile, tp, glChanPlane);
	    ret = 1;
	}
    }
    if (RIGHT(tile) < ch->gcr_area.r_xtop)
    {
	tp = TR(tile);
	if (TiGetType(tp) == TiGetType(tile)
		&& TOP(tp) == TOP(tile)
		&& BOTTOM(tp) == BOTTOM(tile))
	{
	    TiJoinX(tile, tp, glChanPlane);
	    ret = 1;
	}
    }

    return ret;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanBlockDens --
 *
 * Find regions of maximum density in 'ch' (if 'ch' is a normal channel) and
 * "paint" them into the tile map.  Blockages of a single direction result
 * in CHAN_HRIVER or CHAN_VRIVER tiles being painted as appropriate; if
 * blockages exist in both directions, then we produce a CHAN_BLOCKED tile.
 *
 * We rely on the fact that regions of maximum density extend completely
 * across a channel, and hence completely across any tile (since tiles are
 * never bigger than channels).  The client fields of the any tiles in the
 * area of the channel that aren't totally blocked are left pointing to
 * the overlapping channel.
 *
 * If columns i through j inclusive have maximum density, then the blockage
 * we paint extends from midway between columns i-1 and i to midway between
 * column j and j+1, guaranteeing that tile boundaries will always fall
 * on half-grid lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
glChanBlockDens(ch)
    GCRChannel *ch;
{
    GlobChan *gc = (GlobChan *) ch->gcr_client;
    int halfGrid, shiftedOrigin;
    DensMap *dRow, *dCol;
    PaintArea *pa;
    short *dens;
    int lo, hi;
    Rect area;

    if (ch->gcr_type != CHAN_NORMAL)
	return;

    dCol = &gc->gc_postDens[CZ_COL];
    dRow = &gc->gc_postDens[CZ_ROW];
    halfGrid = RtrGridSpacing / 2;
    glChanPaintList = (PaintArea *) NULL;
    if (dCol->dm_max >= dCol->dm_cap)
    {
	dens = dCol->dm_value;
	area.r_ybot = ch->gcr_area.r_ybot;
	area.r_ytop = ch->gcr_area.r_ytop;
	shiftedOrigin = ch->gcr_origin.p_x - halfGrid;
	for (lo = 1; lo < dCol->dm_size; lo++)
	{
	    if (dens[lo] >= dCol->dm_cap)
	    {
		hi = lo+1;
		while (dens[hi] >= dCol->dm_cap && hi < dCol->dm_size)
		    hi++;

		/* Remember area of blocked tile to paint */
		area.r_xbot = shiftedOrigin + lo * RtrGridSpacing;
		area.r_xtop = shiftedOrigin + hi * RtrGridSpacing;
		pa = (PaintArea *) mallocMagic((unsigned)(sizeof (PaintArea)));
		pa->pa_area = area;
		pa->pa_next = glChanPaintList;
		pa->pa_type = CHAN_VRIVER;
		glChanPaintList = pa;

		/* Will get incremented in loop header */
		lo = hi-1;
	    }
	}
    }

    if (dRow->dm_max >= dRow->dm_cap)
    {
	dens = dRow->dm_value;
	area.r_xbot = ch->gcr_area.r_xbot;
	area.r_xtop = ch->gcr_area.r_xtop;
	shiftedOrigin = ch->gcr_origin.p_y - halfGrid;
	for (lo = 1; lo < dRow->dm_size; lo++)
	{
	    if (dens[lo] >= dRow->dm_cap)
	    {
		hi = lo+1;
		while (dens[hi] >= dRow->dm_cap && hi < dRow->dm_size)
		    hi++;

		/* Remember area of blocked tile to paint */
		area.r_ybot = shiftedOrigin + lo * RtrGridSpacing;
		area.r_ytop = shiftedOrigin + hi * RtrGridSpacing;
		pa = (PaintArea *) mallocMagic((unsigned)(sizeof (PaintArea)));
		pa->pa_area = area;
		pa->pa_next = glChanPaintList;
		pa->pa_type = CHAN_HRIVER;
		glChanPaintList = pa;

		/* Will get incremented in loop header */
		lo = hi-1;
	    }
	}
    }

    /*
     * Now do all the work.
     * First paint all the blocked areas on glChanPaintList.
     * Then make a second pass through this list, searching the
     * area around each blocked area tile we painted for river
     * tiles of the appropriate type, and propagating this blockage
     * to those river tiles as well.  Do this until the wavefront
     * stops, which will happen as soon as we reach a normal channel.
     */
    do
    {
	for (pa = glChanPaintList; pa; pa = pa->pa_next)
	{
	    /* Clip tiles overlapped by pa->pa_area */
	    while (DBSrPaintArea((Tile *) NULL, glChanPlane, &pa->pa_area,
		    &DBAllTypeBits, glChanClipFunc, (ClientData) &pa->pa_area))
		/* Nothing */;

	    /* Change the type of all tiles within the area */
	    (void) DBSrPaintArea((Tile *) NULL, glChanPlane, &pa->pa_area,
		    &DBAllTypeBits, glChanPaintFunc, (ClientData) pa->pa_type);

	    /*
	     * Allow merging, as long as no tiles get merged across
	     * channel boundaries.
	     */
	    while (DBSrPaintArea((Tile *) NULL, glChanPlane, &pa->pa_area,
		    &DBAllTypeBits, glChanMergeFunc, (ClientData) NULL));
	}

	/* Second pass to propagate blockages to nearby areas */
	for (pa = glChanPaintList, glChanPaintList = NULL; pa; pa = pa->pa_next)
	{
	    glChanFlood(&pa->pa_area, pa->pa_type);
	    freeMagic((char *) pa);
	}
    } while (glChanPaintList != NULL);
}

int
glChanPaintFunc(tile, type)
    Tile *tile;
    TileType type;
{
    static TileType changeTable[4][4] = {
	    /* Paint atop CHAN_NORMAL */
	    { CHAN_NORMAL, CHAN_HRIVER, CHAN_VRIVER, CHAN_BLOCKED },

	    /* Paint atop CHAN_HRIVER */
	    { CHAN_HRIVER, CHAN_HRIVER, CHAN_BLOCKED, CHAN_BLOCKED },

	    /* Paint atop CHAN_VRIVER */
	    { CHAN_VRIVER, CHAN_BLOCKED, CHAN_VRIVER, CHAN_BLOCKED },

	    /* Paint atop CHAN_BLOCKED */
	    { CHAN_BLOCKED, CHAN_BLOCKED, CHAN_BLOCKED, CHAN_BLOCKED },
    };

    TiSetBody(tile, changeTable[TiGetType(tile)][type]);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanFlood --
 *
 * Search all four sides of 'area' (an area that has just been painted
 * as type) for river tiles of the appropriate type (where appropriate
 * means tiles that are completely blocked by a tile of type type to
 * the side to which it lies).
 *
 * Each time an appropriate river tiles is found, record a CHAN_BLOCKED
 * rectangle on the list glChanPaintList that extends from 'area' through to
 * the other side of the river channel.
 *
 * The purpose of this procedure is so that the presence of the blockage
 * on one side of a river tile will be visible from its other side.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May prepend stuff to glChanPaintList.
 *
 * ----------------------------------------------------------------------------
 */

void
glChanFlood(area, type)
    Rect *area;
    TileType type;
{
    TileTypeBitMask hMask, vMask;
    Rect outside;

    TTMaskSetOnlyType(&hMask, CHAN_HRIVER);
    TTMaskSetOnlyType(&vMask, CHAN_VRIVER);

    if (type != CHAN_VRIVER)
    {
	/* TOP */
	outside = *area;
	outside.r_ybot = area->r_ytop;
	outside.r_ytop = area->r_ytop + 1;
	(void) DBSrPaintArea((Tile *) NULL, glChanPlane, &outside, &vMask,
		glChanFloodVFunc, (ClientData) area);

	/* BOTTOM */
	outside = *area;
	outside.r_ybot = area->r_ybot - 1;
	outside.r_ytop = area->r_ybot;
	(void) DBSrPaintArea((Tile *) NULL, glChanPlane, &outside, &vMask,
		glChanFloodVFunc, (ClientData) area);
    }

    if (type != CHAN_HRIVER)
    {
	/* LEFT */
	outside = *area;
	outside.r_xbot = area->r_xbot - 1;
	outside.r_xtop = area->r_xbot;
	(void) DBSrPaintArea((Tile *) NULL, glChanPlane, &outside, &hMask,
		glChanFloodHFunc, (ClientData) area);

	/* RIGHT */
	outside = *area;
	outside.r_xbot = area->r_xtop;
	outside.r_xtop = area->r_xtop + 1;
	(void) DBSrPaintArea((Tile *) NULL, glChanPlane, &outside, &hMask,
		glChanFloodHFunc, (ClientData) area);
    }
}

int
glChanFloodVFunc(tile, area)
    Tile *tile;
    Rect *area;
{
    PaintArea *pa;

    pa = (PaintArea *) mallocMagic((unsigned) (sizeof (PaintArea)));
    pa->pa_area.r_xbot = MAX(area->r_xbot, LEFT(tile));
    pa->pa_area.r_xtop = MIN(area->r_xtop, RIGHT(tile));
    pa->pa_area.r_ybot = BOTTOM(tile);
    pa->pa_area.r_ytop = TOP(tile);
    pa->pa_next = glChanPaintList;
    pa->pa_type = CHAN_BLOCKED;
    glChanPaintList = pa;

    return 0;
}

int
glChanFloodHFunc(tile, area)
    Tile *tile;
    Rect *area;
{
    PaintArea *pa;

    pa = (PaintArea *) mallocMagic((unsigned)(sizeof (PaintArea)));
    pa->pa_area.r_ybot = MAX(area->r_ybot, BOTTOM(tile));
    pa->pa_area.r_ytop = MIN(area->r_ytop, TOP(tile));
    pa->pa_area.r_xbot = LEFT(tile);
    pa->pa_area.r_xtop = RIGHT(tile);
    pa->pa_next = glChanPaintList;
    pa->pa_type = CHAN_BLOCKED;
    glChanPaintList = pa;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanSplitRiver --
 *
 * Called for each river tile (CHAN_HRIVER or CHAN_VRIVER) in glChanPlane.
 * Search along the two sides of this tile from which signals can enter
 * (the LHS and RHS for CHAN_HRIVER, or the top and bottom for CHAN_VRIVER),
 * and make sure that there are no tile boundaries involving some type of
 * tile other than CHAN_BLOCKED along that side.  If there is a boundary,
 * split tile at the boundary.
 *
 * Results:
 *	Returns 1 if we did any splitting, 0 if not.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
glChanSplitRiver(tile)
    Tile *tile;
{
    ClientData tileClient = tile->ti_client;
    Tile *tp, *newTile;
    int ret;

    ret = 0;
    if (TiGetType(tile) == CHAN_HRIVER)
    {
	for (tp = BL(tile); TOP(tp) < TOP(tile); tp = RT(tp))
	{
	    if (NOTBLOCKED(tp) || NOTBLOCKED(RT(tp)))
	    {
		tile = TiSplitY(tile, TOP(tp));
		TiSetBody(tile, CHAN_HRIVER);
		tile->ti_client = tileClient;
		ret = 1;
	    }
	}
	for (tp = TR(tile); BOTTOM(tp) > BOTTOM(tile); tp = LB(tp))
	{
	    if (NOTBLOCKED(tp) || NOTBLOCKED(LB(tp)))
	    {
		newTile = TiSplitY(tile, BOTTOM(tp));
		TiSetBody(newTile, CHAN_HRIVER);
		newTile->ti_client = tileClient;
		ret = 1;
	    }
	}
    }
    else
    {
	for (tp = RT(tile); LEFT(tp) > LEFT(tile); tp = BL(tp))
	{
	    if (NOTBLOCKED(tp) || NOTBLOCKED(BL(tp)))
	    {
		newTile = TiSplitX(tile, LEFT(tp));
		TiSetBody(newTile, CHAN_VRIVER);
		newTile->ti_client = tileClient;
		ret = 1;
	    }
	}
	for (tp = LB(tile); RIGHT(tp) < RIGHT(tile); tp = TR(tp))
	{
	    if (NOTBLOCKED(tp) || NOTBLOCKED(TR(tp)))
	    {
		tile = TiSplitX(tile, RIGHT(tp));
		TiSetBody(tile, CHAN_VRIVER);
		tile->ti_client = tileClient;
		ret = 1;
	    }
	}
    }

    return ret;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanRiverBlock --
 *
 * Called for each river routing tile.  Checks to make sure that at least
 * some of the pins along the usable sides of this channel are still free;
 * if they're not, we change this tile's type to CHAN_BLOCKED.
 *
 * Note: river routing tiles can overlay portions of a normal channel
 * in which the density of the channel equals its capacity in either
 * the horizontal or vertical direction.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
glChanRiverBlock(tile)
    Tile *tile;
{
    GCRPin *pin, *pinLast;
    GCRChannel *ch;
    int lo, hi;

    ch = (GCRChannel *) tile->ti_client;
    if (TiGetType(tile) == CHAN_HRIVER)
    {
	lo = (BOTTOM(tile) - ch->gcr_origin.p_y) / RtrGridSpacing;
	hi = (TOP(tile) - ch->gcr_origin.p_y) / RtrGridSpacing;
	if (lo < 1) lo = 1;
	if (hi > ch->gcr_width) hi = ch->gcr_width;
	pinLast = &ch->gcr_lPins[hi];
	for (pin = &ch->gcr_lPins[lo]; pin <= pinLast; pin++)
	    if (pin->gcr_pId == NULL && pin->gcr_linked)
		return 0;
	pinLast = &ch->gcr_rPins[hi];
	for (pin = &ch->gcr_rPins[lo]; pin <= pinLast; pin++)
	    if (pin->gcr_pId == NULL && pin->gcr_linked)
		return 0;
    }
    else
    {
	/* CHAN_VRIVER */
	lo = (LEFT(tile) - ch->gcr_origin.p_x) / RtrGridSpacing;
	hi = (RIGHT(tile) - ch->gcr_origin.p_x) / RtrGridSpacing;
	if (lo < 1) lo = 1;
	if (hi > ch->gcr_length) hi = ch->gcr_length;
	pinLast = &ch->gcr_bPins[hi];
	for (pin = &ch->gcr_bPins[lo]; pin <= pinLast; pin++)
	    if (pin->gcr_pId == NULL && pin->gcr_linked)
		return 0;
	pinLast = &ch->gcr_tPins[hi];
	for (pin = &ch->gcr_tPins[lo]; pin <= pinLast; pin++)
	    if (pin->gcr_pId == NULL && pin->gcr_linked)
		return 0;
    }

    TiSetBody(tile, CHAN_BLOCKED);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * glChanPinToTile --
 *
 * Find the tile corresponding to the pin 'pin'.
 * The pin mustn't be in one of the corners of a channel.
 *
 * Results:
 *	Returns a pointer to the tile found.
 *	Returns NULL if the tile found was of type CHAN_BLOCKED.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

Tile *
glChanPinToTile(hintTile, pin)
    Tile *hintTile;	/* Hint for starting the tile search */
    GCRPin *pin;	/* Find tile containing this pin */
{
    GCRChannel *ch;
    Tile *tp;
    Point p;
    Rect r;

    /* Figure out which channel tile the output point lies in */
    p = pin->gcr_point;
    switch (pin->gcr_side)
    {
	case GEO_NORTH:	p.p_y--; break;
	case GEO_EAST:	p.p_x--; break;
    }

    tp = TiSrPoint(hintTile, glChanPlane, &p);
    if (TiGetType(tp) == CHAN_BLOCKED)
	return (Tile *) NULL;

    ASSERT(tp->ti_client != (ClientData) CLIENTDEFAULT, "glChanPinToTile");
    ch = (GCRChannel *) tp->ti_client;
    TITORECT(tp, &r);
    ASSERT(GEO_SURROUND(&ch->gcr_area, &r), "glChanPinToTile");

    return tp;
}
