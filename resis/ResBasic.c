
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResBasic.c,v 1.3 2010/06/24 12:37:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"

/*
 *--------------------------------------------------------------------------
 *
 * resMakePortBreakpoints --
 *
 *	Generate new nodes and breakpoints for every unused port declared
 *	on a tile.  However, if "startpoint" is inside the port position,
 *	then it has already been processed, so ignore it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds breakpoints where ports (drivers, sinks, or labels) have been
 *	defined as connected to the tile.
 *
 *--------------------------------------------------------------------------
 */

void
resMakePortBreakpoints(tile, list)
    Tile 	*tile;
    resNode	**list;
{
    int		x, y;
    resNode	*resptr;
    resPort 	*pl;
    resInfo	*info = (resInfo *)TiGetClientPTR(tile);
    ResConnect	*connect;

    free_magic1_t mm1 = freeMagic1_init();
    for (pl = info->portList; pl; pl = pl->rp_nextPort)
    {
	x = pl->rp_loc.p_x;
	y = pl->rp_loc.p_y;
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	InitializeResNode(resptr, x, y, RES_NODE_ORIGIN);
	resptr->rn_status = TRUE;
	resptr->rn_noderes = 0;
	resptr->rn_name = pl->rp_nodename;

	/* Link back to the resnode from the ResConnect record */
	connect = pl->rp_connect;
	connect->rc_node = resptr;

	ResAddToQueue(resptr, list);
	ResNewBreak(resptr, tile, x, y, NULL);
	freeMagic1(&mm1, pl);
    }
    freeMagic1_end(&mm1);
}

/*
 * Structure used by ResEachTile for the callback to ResMultiPlaneFunc()
 * to pass a pointer to the tile being processed, and the terminal being
 * searched.
 */

typedef struct tile_and_term
{
    Tile *tat_tile;
    int   tat_term;
} TileAndTerm;

/*
 *--------------------------------------------------------------------------
 *
 * ResMultiPlaneFunc---
 *
 *  If device is found overlapping one of its source/drain types, then
 *  generate a new device at the center of the tile and add to ResNodeQueue.
 *
 * Results:
 *	Always 0 to keep the search going.
 *
 * Side effects:
 *	Adds to ResNodeQueue
 *
 *--------------------------------------------------------------------------
 */

int
ResMultiPlaneFunc(tile, dinfo, tat)
    Tile *tile;
    TileType dinfo;	/* Not used, but needs to be handled */
    TileAndTerm *tat;
{
    Tile *tp = tat->tat_tile;
    int  term = tat->tat_term;
    int	 xj, yj;

    /* Simplified split tile handling---Ignore the right side of
     * tiles that have non-space types on both sides.
     */
    if (IsSplit(tile))
	if (TiGetLeftType(tile) != TT_SPACE && TiGetRightType(tile) != TT_SPACE)
	    if (dinfo & TT_SIDE)
		return 0;

    xj = (LEFT(tile) + RIGHT(tile)) / 2;
    yj = (TOP(tile) + BOTTOM(tile)) / 2;
    ResNewTermDevice(tp, tile, term, xj, yj, OTHERPLANE, &ResNodeQueue);

    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * ResSubstrateFunc---
 *
 *  If device is found overlapping its substrate type, then generate a new
 *  device at the center of the tile and add to ResNodeQueue.
 *
 * Results:
 *	Always 0 to keep the search going.
 *
 * Side effects:
 *	Adds to ResNodeQueue
 *
 *--------------------------------------------------------------------------
 */

int
ResSubstrateFunc(tile, dinfo, tpptr)
    Tile *tile;
    TileType dinfo;
    Tile **tpptr;
{
    Tile *tp = *tpptr;
    int	 xj, yj;

    /* Simplified split tile handling---Ignore the right side of
     * tiles that have non-space types on both sides.
     */
    if (IsSplit(tile))
	if (TiGetLeftType(tile) != TT_SPACE && TiGetRightType(tile) != TT_SPACE)
	    if (dinfo & TT_SIDE)
		return 0;

    xj = (LEFT(tile) + RIGHT(tile)) / 2;
    yj = (TOP(tile) + BOTTOM(tile)) / 2;
    ResNewSubDevice(tp, tile, xj, yj, OTHERPLANE, &ResNodeQueue);

    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * ResStartTile --
 *
 *   For the tile at the starting point of the net, create an initial
 *   resNode entry.
 *
 *  Results:
 *	None.
 *
 *  Side Effects:
 *	creates a node.
 *
 *
 *--------------------------------------------------------------------------
 */

void
ResStartTile(tile, x, y)
    Tile 	*tile;
    int		x, y;

{
    resNode	*resptr;

    resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
    InitializeResNode(resptr, x, y, RES_NODE_ORIGIN);
    resptr->rn_status = TRUE;
    resptr->rn_noderes = 0;
    ResAddToQueue(resptr, &ResNodeQueue);
    ResNewBreak(resptr, tile, x, y, NULL);
    if (resCurrentNode == NULL) resCurrentNode = resptr;
}

/*
 *--------------------------------------------------------------------------
 *
 * ResEachTile --
 *
 *   For each tile, make a list of all possible current sources/
 *   sinks including contacts, devices, and junctions.  Once this
 *   list is made, calculate the resistor network for the tile.
 *
 *  Results: returns TRUE or FALSE depending on whether a node was
 *           involved in a merge.
 *
 *  Side Effects: creates Nodes, devices, junctions, and breakpoints.
 *
 *
 *--------------------------------------------------------------------------
 */

#define IGNORE_LEFT	1
#define IGNORE_RIGHT	2
#define IGNORE_TOP	4
#define IGNORE_BOTTOM	8

bool
ResEachTile(tile, devNodeTable)
    Tile 	*tile;			/* Tile being processed */
    HashTable	*devNodeTable;		/* Table of tiles connected to devices */
{
    Tile 	*tp;
    resNode	*resptr;
    cElement	*ce;
    TileType	t1, t2;
    int		xj, yj, i;
    bool	merged;
    tElement	*tcell;
    resInfo	*tstructs = (resInfo *)TiGetClientPTR(tile);
    ExtDevice   *devptr;
    int		sides;
    HashEntry	*he;

    ResTileCount++;

    /* Simplification:  Split tiles handle either the non-space side,
     * or if neither side is space, then handle the left side.
     */
    if (IsSplit(tile))
    {
	if (TiGetLeftType(tile) == TT_SPACE)
	{
	    t1 = SplitRightType(tile);
	    sides = IGNORE_LEFT;
	    sides |= (SplitDirection(tile)) ? IGNORE_BOTTOM : IGNORE_TOP;
	}
	else
	{
	    t1 = SplitLeftType(tile);
	    sides = IGNORE_RIGHT;
	    sides |= (SplitDirection(tile)) ? IGNORE_TOP : IGNORE_BOTTOM;
	}
    }
    else
    {
	sides = 0;
	t1 = TiGetTypeExact(tile);
    }

    if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask), t1))
    {
	/*
	 * The device is put in the center of the tile. This is fine
	 * for single tile device, but not as good for multiple ones.
	 */

	if (tstructs->ri_status & RES_TILE_DEV)
 	{
	    if (tstructs->deviceList->rd_fet_gate == NULL)
	    {
		int x = (LEFT(tile) + RIGHT(tile)) >> 1;
		int y = (TOP(tile) + BOTTOM(tile)) >> 1;

		resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
		tstructs->deviceList->rd_fet_gate = resptr;
		tcell = (tElement *) mallocMagic((unsigned)(sizeof(tElement)));
		tcell->te_thist = tstructs->deviceList;
		tcell->te_nextt = NULL;

		InitializeResNode(resptr, x, y, RES_NODE_JUNCTION);
		resptr->rn_te = tcell;
		ResAddToQueue(resptr, &ResNodeQueue);
		ResNewBreak(resptr, tile, resptr->rn_loc.p_x,
		     			resptr->rn_loc.p_y, NULL);
	    }
	}
    }

    /* Process all the contact points */
    ce = tstructs->contactList;
    while (ce != (cElement *) NULL)
    {
	ResContactPoint	*cp = ce->ce_thisc;
	cElement	*oldce;
	if (cp->cp_cnode[0] == (resNode *) NULL)
	{
	    ResDoContacts(cp, &ResNodeQueue, &ResResList);
	}
	oldce = ce;
	ce = ce->ce_nextc;
	freeMagic((char *)oldce);
    }
    tstructs->contactList = NULL;

    /*
     * Walk the four sides of the tile looking for adjoining connecting
     * materials.
     */

    /* left */
    if (!(sides & IGNORE_LEFT))
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	t2 = TiGetRightType(tp);
	if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask), t2))
	{
	    for (devptr = ExtCurStyle->exts_device[t2]; devptr;
			devptr = devptr->exts_next)
	    {
		for (i = 0; i < devptr->exts_deviceSDCount; i++)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t1))
		    {
			/* found device */
			xj = LEFT(tile);
			yj = (TOP(tp) + BOTTOM(tp)) >> 1;
			ResNewTermDevice(tile, tp, i, xj, yj, RIGHTEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if (TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]), t2))
	{
	    /* tile is junction */
	    xj = LEFT(tile);
	    yj = (MAX(BOTTOM(tile), BOTTOM(tp)) + MIN(TOP(tile), TOP(tp))) >> 1;
	    (void) ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* right */
    if (!(sides & IGNORE_RIGHT))
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
    {
	t2 = TiGetLeftType(tp);
	if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask), t2))
	{
	    for (devptr = ExtCurStyle->exts_device[t2]; devptr;
			devptr = devptr->exts_next)
	    {
		for (i = 0; i < devptr->exts_deviceSDCount; i++)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t1))
		    {
			/* found device */
			xj = RIGHT(tile);
			yj = (TOP(tp) + BOTTOM(tp)) >> 1;
			ResNewTermDevice(tile, tp, i, xj, yj, LEFTEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if (TTMaskHasType(&ExtCurStyle->exts_nodeConn[t1], t2))
	{
	    /* tile is junction */
	    xj = RIGHT(tile);
	    yj = (MAX(BOTTOM(tile), BOTTOM(tp)) + MIN(TOP(tile), TOP(tp))) >> 1;
	    (void)ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* top */
    if (!(sides & IGNORE_TOP))
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	t2 = TiGetBottomType(tp);
	if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask), t2))
	{
	    for (devptr = ExtCurStyle->exts_device[t2]; devptr;
			devptr = devptr->exts_next)
	    {
		for (i = 0; i < devptr->exts_deviceSDCount; i++)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t1))
		    {
			/* found device */
			yj = TOP(tile);
			xj = (LEFT(tp) + RIGHT(tp)) >> 1;
			ResNewTermDevice(tile, tp, i, xj, yj, BOTTOMEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if (TTMaskHasType(&ExtCurStyle->exts_nodeConn[t1], t2))
	{
	    /* tile is junction */
	    yj = TOP(tile);
	    xj = (MAX(LEFT(tile), LEFT(tp)) + MIN(RIGHT(tile), RIGHT(tp))) >> 1;
	    ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* bottom */
    if (!(sides & IGNORE_BOTTOM))
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
    {
	t2 = TiGetTopType(tp);
	if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask), t2))
	{
	    for (devptr = ExtCurStyle->exts_device[t2]; devptr;
			devptr = devptr->exts_next)
	    {
		for (i = 0; i < devptr->exts_deviceSDCount; i++)
		{
		    if (TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t1))
		    {
			/* found device */
			yj = BOTTOM(tile);
			xj = (LEFT(tp) + RIGHT(tp)) >> 1;
			ResNewTermDevice(tile, tp, i, xj, yj, TOPEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if (TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]), t2))
	{
	    /* tile is junction */
	    yj = BOTTOM(tile);
	    xj = (MAX(LEFT(tile), LEFT(tp)) + MIN(RIGHT(tile), RIGHT(tp))) >> 1;
	    ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* Check for terminals on other planes (e.g., capacitors, bipolars, ...)	*/
    /* Note:  Need to tag these tiles per device to avoid checking through	*/
    /* the device list for each tile. (To be done)				*/

    he = HashLookOnly(devNodeTable, (char *)tile);
    if (he != NULL)
    {
	resDevTerm *resdevRec, *resdevList;
	Tile *devtile;

	resdevList = (resDevTerm *)HashGetValue(he);
	while (resdevList != NULL)
	{
	    resdevRec = resdevList;
	    /* Set the position from the device, not the current tile */
	    devtile = resdevRec->rdt_tile;
	    xj = (RIGHT(devtile) + LEFT(devtile)) / 2;
	    yj = (TOP(devtile) + BOTTOM(devtile)) / 2;
	    if (resdevRec->rdt_term == -1)	/* Substrate */
	    {
		ResNewSubDevice(tile, resdevRec->rdt_tile, xj, yj,
				OTHERPLANE, &ResNodeQueue);
	    }
	    else	/* Terminal */
	    {
		ResNewTermDevice(tile, resdevRec->rdt_tile, resdevRec->rdt_term,
				xj, yj, OTHERPLANE, &ResNodeQueue);
	    }
	    resdevList = resdevList->rdt_next;
	    freeMagic(resdevRec);
	}
	HashSetValue(he, (char *)NULL);		/* Done with hash record */
    }

#if 0
    /* Deprecated:  Searching through all devices for all tiles is 
     * irresponsibly slow.  Do not do this.
     */

    if (TTMaskHasType(&ResTermTypesBitMask, t1))
    {
	Rect r;
	int pNum;
	TileTypeBitMask devMask;
	TileAndTerm tat;
	ResDevTile *devtile;
	ExtDevice *devptr;

	TiToRect(tile, &r);

	for (devtile = devTiles; devtile; devtile = devtile->nextDev)
	{
	    Tile *tp;
	    TileType devtype = devtile->type;

	    devptr = devtile->devptr;
	    for (i = 0; i < devptr->exts_deviceSDCount; i++)
	    {
		if (TTMaskHasType(&devptr->exts_deviceSDTypes[i], t1))
		{
		    if (GEO_OVERLAP(&devtile->area, &r))
		    {
			Plane *plane = ResUse->cu_def->cd_planes[DBPlane(devtype)];
			xj = (r.r_xtop + r.r_xbot) / 2;
			yj = (r.r_ytop + r.r_ybot) / 2;
			tp = PlaneGetHint(plane);
			GOTOPOINT(tp, &devtile->area.r_ll);
			PlaneSetHint(plane, tp);
			ResNewTermDevice(tile, tp, i, xj, yj, OTHERPLANE, &ResNodeQueue);
		    }
		}
	    }
	}
    }

    /* Check for substrate under device */

    if (TTMaskHasType(&ResSubTypesBitMask, t1))
    {
	Rect r;
	int pNum;
	TileTypeBitMask devMask;
	ResDevTile *devtile;
	ExtDevice *devptr;

	TiToRect(tile, &r);

	for (devtile = devTiles; devtile; devtile = devtile->nextDev)
	{
	    Tile *tp;
	    TileType devtype = devtile->type;

	    devptr = devtile->devptr;

	    if (TTMaskHasType(&devptr->exts_deviceSubstrateTypes, t1))
	    {
		if (GEO_OVERLAP(&devtile->area, &r))
		{
		    Plane *plane = ResUse->cu_def->cd_planes[DBPlane(devtype)];
		    xj = (r.r_xtop + r.r_xbot) / 2;
		    yj = (r.r_ytop + r.r_ybot) / 2;
		    tp = PlaneGetHint(plane);
		    GOTOPOINT(tp, &devtile->area.r_ll);
		    PlaneSetHint(plane, tp);
		    ResNewSubDevice(tile, tp, xj, yj, OTHERPLANE, &ResNodeQueue);
		}
	    }
	}
    }
#endif /* 0 */

    tstructs->ri_status |= RES_TILE_DONE;

    resMakePortBreakpoints(tile, &ResNodeQueue);

    merged = ResCalcTileResistance(tile, tstructs, &ResNodeQueue,
			&ResNodeList);

    return(merged);
}
