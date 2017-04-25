/* rtrVia.c.c -
 *
 *	This file contains procedures that minimize vias and
 *	maximize metal.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrVia.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */


#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "netmenu/netmenu.h"
#include "textio/textio.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "router/routerInt.h"

/*
 * The following structures are used to hold information about
 * areas to be erased/modified/painted during via minimization.
 */

struct arealist
{
    Rect ap_erase;			/* Area to be erased. */
    Rect ap_paint;			/* Area to be painted. */
    int	ap_oldtype;			/* Type to be erased */
    int	ap_newtype;			/* Type to be painted */
    struct arealist *ap_next;		/* Next item in list, or NULL for end. */
};

struct	vialist
{
    Rect vp_area;			/* Via area to be examined.		 */
    struct vialist *vp_next;		/* Next item in list, or NULL for end. 	*/
};

struct	paintlist
{
    Rect	pl_area;		/* Extension segment to paint		*/
    struct	paintlist	*pl_next;/* Next extension to paint		*/
};

struct	srinfo
{
    Rect	*si_area;		/* Expanded area surrounding via	*/
    Rect	*si_varea;		/* Exact area of via			*/
    Rect	si_extend;		/* Extended stub within si_varea	*/
    Tile	*si_tile;		/* Reference tile			*/
    Plane	*si_plane;		/* Plane being searched			*/
    TileTypeBitMask si_mask;		/* Tile type mask			*/
};

struct	vialist	*rtrViaList;		/* List of vias to be processed 	*/
struct	arealist *rtrAreaList;		/* List of areas to be processed	*/
struct	paintlist *rtrPaintList;	/* List of extension segments to paint	*/

int	rtrVias;			/* Count of vias eliminated		*/
int	rtrExamineStack();		/* Examines the tile stack for
					   replacement segments to be converted
					   to the target material */


/*
 * ----------------------------------------------------------------------------
 *
 * rtrFollowLocFunc --
 *
 *	This function is called once for each terminal location in
 *	a netlist. It invokes a procedure to follow all electrically
 *	connected paths from the terminal.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
rtrFollowLocFunc(rect, name, label, area)
    Rect *rect;			/* Area of the terminal, edit cell coords. */
    char *name;			/* Name of the terminal (ignored). */
    Label *label;		/* Pointer to the label, used to find out
				 * what layer the label's attached to.
				 */
    Rect *area;			/* We GeoInclude into this all the areas of
				 * all the tiles we delete.
				 */
{
    CellDef *def = EditCellUse->cu_def;
    Rect initialArea;

    GEO_EXPAND(rect, 1, &initialArea);
    (void) rtrSrTraverse(def, &initialArea,
	    &DBConnectTbl[label->lab_type], DBConnectTbl,
		&TiPlaneRect, rtrExamineStack, 0);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrFollowName --
 *
 * 	This function gets called during via minimization.  It's invoked
 *	once for each terminal name in the netlist.  It calls
 *	DBSrLabelLoc to invoke rtrFollowLocFunc for each terminal
 *	location associated with the name.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Nothing here, but a list of segments of poly are generated.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
rtrFollowName(name, firstInNet, area)
    char *name;			/* Name of terminal. */
    bool firstInNet;		/* Ignored by this procedure. */
    Rect *area;			/* Passed through as ClientData to
				 * rtrFollowLocFunc.
				 */
{
    if ( firstInNet )
    {
	RtrMilestonePrint("#");
	(void) DBSrLabelLoc(EditCellUse, name, rtrFollowLocFunc, (ClientData) area);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrCheckTypes --
 *	Abort area search if both poly and metal are adjacent to via.
 *
 * Results:
 *	Returns 1 if poly and metal connect to via.
 *	Returns zero otherwise.
 *
 * Side effects:
 *	Type of connecting material returned in cdata parameter.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrCheckTypes(tile, cdata)
    Tile *tile;
    ClientData cdata;
{
    int type;
    int lastType = * (int *) cdata;

    type = TiGetType(tile);
    if ( (type == RtrMetalType) || (type == RtrPolyType) )
    {
	if ( lastType )
	{
	    if ( lastType != type )
		return 1;
	}
	else
	    *(int *)cdata = type;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrExtandStub --
 *	Generate an extension of a piece of routing material
 *	by extending into the area of the via.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Extension segment returned in *stub*.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrExtend(tile,area,stub)
    Tile *tile;				/* Tile adjacent to via			*/
    Rect *area;				/* Area occupied by via			*/
    Rect *stub;				/* Extension of routing material
					   into area of via			*/
{
    if ( (TOP(tile) == area->r_ybot) || (BOTTOM(tile) == area->r_ytop) )
    {
	stub->r_xbot = MAX(area->r_xbot, LEFT(tile));
	stub->r_xtop = MIN(area->r_xtop, RIGHT(tile));
	stub->r_ybot = area->r_ybot;
	stub->r_ytop = area->r_ytop;
    }
    else if ( (LEFT(tile) == area->r_xtop) || (RIGHT(tile) == area->r_xbot) )
    {
	stub->r_xbot = area->r_xbot;
	stub->r_xtop = area->r_xtop;
	stub->r_ybot = MAX(area->r_ybot, BOTTOM(tile));
	stub->r_ytop = MIN(area->r_ytop, TOP(tile));
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrStubGen --
 *	Generate an extension segment for a piece of routing material.
 *
 * Results:
 *	Return 0 to keep the search going.
 *
 * Side effects:
 *	Extension segment entry added to rtrPaintList list for later painting.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrStubGen(tile, si)
    Tile *tile;
    struct srinfo *si;
{
    Rect area;
    struct paintlist *pl;

    if ( tile != si->si_tile )
    {
	pl = (struct paintlist *) mallocMagic((unsigned) (sizeof(*pl)));
	pl->pl_next = rtrPaintList;
	rtrPaintList = pl;

	/*
	 * Generate extension segment relative to reference segment.
	 */

	rtrExtend(tile, si->si_varea, &pl->pl_area);
	GeoClip(&pl->pl_area, &si->si_extend);
	TITORECT(tile, &area);
	GeoClip(&area, si->si_area);
	(void) GeoInclude(&area, &pl->pl_area);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrReferenceTile --
 *	Store a reference tile and corresponding extension stub.
 *	Recursively call DBSrPaintArea to generate extension
 *	stubs intersecting the reference stub.	Each tile 
 *	adjacent to the via will in turn be a reference tile.
 *
 * Results:
 *	Always returns 0 to continue search.
 *
 * Side effects:
 *	Tile pointer and extension stub stored in *srinfo* structure.
 *
 * ----------------------------------------------------------------------------
 */


int
rtrReferenceTile(tile, si)
    Tile *tile;
    struct srinfo *si;
{
    si->si_tile = tile;
    rtrExtend(tile, si->si_varea, &si->si_extend);
    (void) DBSrPaintArea(tile, si->si_plane, si->si_area,
	    &si->si_mask, rtrStubGen, (ClientData) si);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * rtrViaCheck --
 *	Check to see if a via is necessary.
 *	If only one routing layer connects to the via,
 *	delete the via and replace with extended segments of routing material.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Via erased.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrViaCheck(area, def)
    Rect *area;
    CellDef *def;
{
    Rect r;
    int type, plane;
    struct paintlist *pl;
    TileTypeBitMask mask;

    /*
     * Search around via for connecting pieces of metal or poly.
     */

    GEO_EXPAND(area, 1, &r);
    type = 0;
    TTMaskZero(&mask);
    TTMaskSetType(&mask, RtrPolyType);
    TTMaskSetType(&mask, RtrMetalType);
    for ( plane = PL_PAINTBASE; plane < DBNumPlanes; plane++ )
	if ( DBPaintOnPlane(RtrPolyType, plane) ||
	     DBPaintOnPlane(RtrMetalType, plane) )
	    if ( DBSrPaintArea((Tile *)NULL, def->cd_planes[plane],
		    &r, &mask, rtrCheckTypes, (ClientData) &type) )
		return;

    /*
     * No metal or poly connects to this via.
     * Enumerate all connecting routing material
     * and build a list if extension segments to replace the via.
     */

    rtrPaintList = (struct paintlist *) NULL;
    for ( plane = PL_PAINTBASE; plane < DBNumPlanes; plane++ )
	if ( DBPaintOnPlane(type, plane) )
	{
	    struct srinfo si;

	    si.si_area = &r;
	    si.si_varea = area;
	    si.si_plane = def->cd_planes[plane];
	    TTMaskZero(&si.si_mask);
	    TTMaskSetType(&si.si_mask, type);
	    (void) DBSrPaintArea((Tile *)NULL, si.si_plane,
		    &r, &mask, rtrReferenceTile, (ClientData) &si);
	}
	    
    /*
     * Erase via and paint extensions.
     */

    DBErase(def, area, RtrContactType);
    for ( pl = rtrPaintList; pl; pl = pl->pl_next)
    {
	DBPaint(def, &pl->pl_area, type);
	freeMagic( (char *)pl );
    }

    rtrVias++;
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrListArea --
 *	Append an entry into a linked list
 *	of areas to be processed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	rtrAreaList points to a new list entry
 *	which is linked into the existing list.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrListArea(tile, oldType, newType, deltax, deltay)
    Tile *tile;
    int oldType;
    int	newType;
    int deltax;
    int deltay;
{
    struct arealist *ap;

    ap = (struct arealist *) mallocMagic((unsigned) (sizeof(*ap)));
    TITORECT(tile, &ap->ap_erase);
    TITORECT(tile, &ap->ap_paint);

    /*
     * Adjust size of paint to correspond to new layer.
     */

    ap->ap_paint.r_xtop += deltax;
    ap->ap_paint.r_ytop += deltay;

    ap->ap_oldtype = oldType;
    ap->ap_newtype = newType;
    ap->ap_next = rtrAreaList;
    rtrAreaList = ap;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrListVia --
 *	Append an entry into a linked list
 *	of areas to be processed.
 *
 * Results:
 *	Always return 0 to keep the search going.
 *
 * Side effects:
 *	rtrViaList points  to new list entry
 *	which is linked in front of existing list.
 *	Always returns 0 to continue search if called from
 *	DBSrPaintArea.
 *
 * ----------------------------------------------------------------------------
 */

int
rtrListVia(tile)
    Tile *tile;
{
    struct vialist *vp;

    vp = (struct vialist *) mallocMagic((unsigned)(sizeof(*vp)));
    TITORECT(tile, &vp->vp_area);
    vp->vp_next = rtrViaList;
    rtrViaList = vp;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrViaMinimize --
 *	Minimize vias and maximize metal.
 *
 * Results:
 *	Return number of vias elminiated.
 *
 * Side effects:
 *	On the first pass, segments of poly
 *	are replaced with metal and connecting vias are erased.
 *	On the second pass, segments of metal 
 *	are replaced with poly and connecting vias are erased.
 *
 * ----------------------------------------------------------------------------
 */

int
RtrViaMinimize(def)
    CellDef *def;
{
    Rect area;
    struct vialist *vp;
    struct arealist *ap;

    /*
     * Pass 1 --
     * Enumerate every terminal in the netlist
     * to follow all electrically connected paths.
     * Generate a list of segments of poly not overlapped by metal
     * and terminated at each end by vias.
     * These segments of poly are replaced with metal
     * and the connecting via's are removed.
     */
    
    rtrVias = 0;
    rtrTarget  = RtrMetalType;
    rtrReplace = RtrPolyType;
    rtrDelta   = RtrMetalWidth - RtrPolyWidth;

    area = GeoNullRect;
    rtrViaList = (struct vialist *) NULL;
    rtrAreaList = (struct arealist *) NULL;
    (void) NMEnumNets(rtrFollowName, (ClientData) &area);

    /*
     * Replace poly with metal where appropriate.
     */

    for ( ap = rtrAreaList; ap; ap = ap->ap_next)
    {
	DBErase(def, &ap->ap_erase, ap->ap_oldtype);
	DBPaint(def, &ap->ap_paint, ap->ap_newtype);
	freeMagic( (char *)ap);
    }

    /*
     * Eliminate unnecessary vias.
     */

    for ( vp = rtrViaList; vp; vp = vp->vp_next)
    {
	rtrViaCheck(&vp->vp_area, def);
	freeMagic( (char *)vp);
    }
    
    /*
     * Pass 2 --
     * Repeat the entire process replacing metal with poly.
     */

    rtrTarget  = RtrPolyType;
    rtrReplace = RtrMetalType;
    rtrDelta   = RtrPolyWidth - RtrMetalWidth;

    area = GeoNullRect;
    rtrViaList = (struct vialist *) NULL;
    rtrAreaList = (struct arealist *) NULL;
    (void) NMEnumNets(rtrFollowName, (ClientData) &area);

    /*
     * Erase poly and replace with metal.
     */

    for ( ap = rtrAreaList; ap; ap = ap->ap_next)
    {
	DBErase(def, &ap->ap_erase, ap->ap_oldtype);
	DBPaint(def, &ap->ap_paint, ap->ap_newtype);
	freeMagic( (char *)ap);
    }

    /*
     * Eliminate unnecessary vias.
     */

    for ( vp = rtrViaList; vp; vp = vp->vp_next)
    {
	rtrViaCheck(&vp->vp_area, def);
	freeMagic( (char *)vp);
    }

    return rtrVias;
}
