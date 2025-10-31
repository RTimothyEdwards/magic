
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
 * resNodeIsPort --
 *
 *	If the given position is inside any port declared on the tile,
 *	change the node name to the port name.  Remove the port
 *	declaration if it was used.
 *
 *--------------------------------------------------------------------------
 */

void
resNodeIsPort(node, x, y, tile)
    resNode *node;
    int	    x;
    int	    y;
    Tile    *tile;
{
    Rect 	*rect;
    Point 	p;
    resPort 	*pl, *lp;
    tileJunk	*junk = (tileJunk *)TiGetClientPTR(tile);

    p.p_x = x;
    p.p_y = y;

    for (pl = junk->portList; pl; pl = pl->rp_nextPort)
    {
	rect = &(pl->rp_bbox);
	if (GEO_ENCLOSE(&p, rect))
	{
	    node->rn_name = pl->rp_nodename;
	    if (junk->portList == pl)
		junk->portList = pl->rp_nextPort;
	    else
	    {
	        for (lp = junk->portList; lp && (lp->rp_nextPort != pl);
			lp = lp->rp_nextPort);
		lp->rp_nextPort = pl->rp_nextPort;
	    }
	    freeMagic(pl);
	    break;
	}
    }
}

/*
 *--------------------------------------------------------------------------
 *
 * resAllPortNodes --
 *
 *	Generate new nodes and breakpoints for every unused port declared
 *	on a tile.
 *
 *--------------------------------------------------------------------------
 */

void
resAllPortNodes(tile, list)
    Tile 	*tile;
    resNode	**list;
{
    int		x, y;
    resNode	*resptr;
    resPort 	*pl;
    tileJunk	*junk = (tileJunk *)TiGetClientPTR(tile);

    free_magic1_t mm1 = freeMagic1_init();
    for (pl = junk->portList; pl; pl = pl->rp_nextPort)
    {
	x = pl->rp_loc.p_x;
	y = pl->rp_loc.p_y;
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	InitializeNode(resptr, x, y, RES_NODE_ORIGIN);
	resptr->rn_status = TRUE;
	resptr->rn_noderes = 0;
	resptr->rn_name = pl->rp_nodename;
	ResAddToQueue(resptr, list);
	NEWBREAK(resptr, tile, x, y, NULL);
	freeMagic1(&mm1, pl);
    }
    freeMagic1_end(&mm1);
}

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
ResMultiPlaneFunc(tile, tpptr)
    Tile *tile, **tpptr;
{
    Tile *tp = *tpptr;
    int	 xj, yj;

    xj = (LEFT(tile) + RIGHT(tile)) / 2;
    yj = (TOP(tile) + BOTTOM(tile)) / 2;
    ResNewSDDevice(tp, tile, xj, yj, OTHERPLANE, &ResNodeQueue);

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
ResSubstrateFunc(tile, tpptr)
    Tile *tile, **tpptr;
{
    Tile *tp = *tpptr;
    int	 xj, yj;

    xj = (LEFT(tile) + RIGHT(tile)) / 2;
    yj = (TOP(tile) + BOTTOM(tile)) / 2;
    ResNewSubDevice(tp, tile, xj, yj, OTHERPLANE, &ResNodeQueue);

    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * ResEachTile--for each tile, make a list of all possible current sources/
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

bool
ResEachTile(tile, startpoint)
    Tile 	*tile;
    Point 		*startpoint;

{
    Tile 	*tp;
    resNode	*resptr;
    cElement	*ce;
    TileType	t1, t2;
    int		xj, yj, i;
    bool	merged;
    tElement	*tcell;
    tileJunk	*tstructs= (tileJunk *)TiGetClientPTR(tile);
    ExtDevice   *devptr;

    ResTileCount++;

    /* Process startpoint, if any. */

    if (IsSplit(tile))
    {
	t1 = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    }
    else
	t1 = TiGetTypeExact(tile);

    if (startpoint != (Point *) NULL)
    {
	int x = startpoint->p_x;
	int y = startpoint->p_y;
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	InitializeNode(resptr, x, y, RES_NODE_ORIGIN);
	resptr->rn_status = TRUE;
	resptr->rn_noderes = 0;
	ResAddToQueue(resptr, &ResNodeQueue);
	NEWBREAK(resptr, tile, x, y, NULL);
	resCurrentNode = resptr;
	resNodeIsPort(resptr, x, y, tile);
    }

    if TTMaskHasType(&(ExtCurStyle->exts_deviceMask), t1)
    {
	/*
	 * The device is put in the center of the tile. This is fine
	 * for single tile device, but not as good for multiple ones.
	 */

	if (tstructs->tj_status & RES_TILE_DEV)
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

		InitializeNode(resptr, x, y, RES_NODE_JUNCTION);
		resptr->rn_te = tcell;
		ResAddToQueue(resptr, &ResNodeQueue);
		resNodeIsPort(resptr, x, y, tile);

		NEWBREAK(resptr, tile, resptr->rn_loc.p_x,
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
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp=RT(tp))
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
			ResNewSDDevice(tile, tp, xj, yj, RIGHTEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]), t2)
	{
	    /* tile is junction */
	    xj = LEFT(tile);
	    yj = (MAX(BOTTOM(tile), BOTTOM(tp)) + MIN(TOP(tile), TOP(tp))) >> 1;
	    (void) ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* right */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp=LB(tp))
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
			ResNewSDDevice(tile, tp, xj, yj, LEFTEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if TTMaskHasType(&ExtCurStyle->exts_nodeConn[t1], t2)
	{
	    /* tile is junction */
	    xj = RIGHT(tile);
	    yj = (MAX(BOTTOM(tile),BOTTOM(tp)) + MIN(TOP(tile), TOP(tp))) >> 1;
	    (void)ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* top */
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
			ResNewSDDevice(tile, tp, xj, yj, BOTTOMEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if TTMaskHasType(&ExtCurStyle->exts_nodeConn[t1], t2)
	{
	    /* tile is junction */
	    yj = TOP(tile);
	    xj = (MAX(LEFT(tile),LEFT(tp)) + MIN(RIGHT(tile),RIGHT(tp))) >> 1;
	    ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp=TR(tp))
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
			ResNewSDDevice(tile, tp, xj, yj, TOPEDGE, &ResNodeQueue);
			break;
		    }
		}
		if (i < devptr->exts_deviceSDCount) break;
	    }
	}
	if TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]), t2)
	{
	    /* tile is junction */
	    yj = BOTTOM(tile);
	    xj = (MAX(LEFT(tile),LEFT(tp)) + MIN(RIGHT(tile),RIGHT(tp))) >> 1;
	    ResProcessJunction(tile, tp, xj, yj, &ResNodeQueue);
	}
    }

    /* Check for source/drain on other planes (e.g., capacitors, bipolars, ...) */

    if (TTMaskHasType(&ResSDTypesBitMask, t1))
    {
	Rect r;
	int pNum;
	TileTypeBitMask devMask;

	TiToRect(tile, &r);

	for (pNum = 0; pNum < DBNumPlanes; pNum++)
	{
	    if (DBTypeOnPlane(t1, pNum)) continue;

	    /* NOTE:  This is ridiculously inefficient and should be done
	     * in a different way.
	     */

	    TTMaskZero(&devMask);
	    for (t2 = TT_TECHDEPBASE; t2 < DBNumUserLayers; t2++)
		for (devptr = ExtCurStyle->exts_device[t2]; devptr;
			    devptr = devptr->exts_next)
		    for (i = 0; !TTMaskIsZero(&devptr->exts_deviceSDTypes[i]); i++)
			if (TTMaskHasType(&devptr->exts_deviceSDTypes[i], t1))
			    TTMaskSetType(&devMask, t2);

	    DBSrPaintArea((Tile *)NULL, ResUse->cu_def->cd_planes[pNum],
			&r, &devMask, ResMultiPlaneFunc, (ClientData)&tile);
	}
    }

    /* Check for substrate under device */

    if (TTMaskHasType(&ResSubTypesBitMask, t1))
    {
	Rect r;
	int pNum;
	TileTypeBitMask devMask;

	TiToRect(tile, &r);

	for (pNum = 0; pNum < DBNumPlanes; pNum++)
	{
	    if (DBTypeOnPlane(t1, pNum)) continue;

	    /* NOTE:  This is ridiculously inefficient and should be done
	     * in a different way.
	     */

	    TTMaskZero(&devMask);
	    for (t2 = TT_TECHDEPBASE; t2 < DBNumUserLayers; t2++)
		for (devptr = ExtCurStyle->exts_device[t2]; devptr;
			    devptr = devptr->exts_next)
		    if (TTMaskHasType(&devptr->exts_deviceSubstrateTypes, t1))
			TTMaskSetType(&devMask, t2);

	    DBSrPaintArea((Tile *)NULL, ResUse->cu_def->cd_planes[pNum],
			&r, &devMask, ResSubstrateFunc, (ClientData)&tile);
	}
    }

    tstructs->tj_status |= RES_TILE_DONE;

    resAllPortNodes(tile, &ResNodeQueue);

    merged = ResCalcTileResistance(tile, tstructs, &ResNodeQueue,
			&ResNodeList);

    return(merged);
}
