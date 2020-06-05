/* ResConnectDCS.c --
 *
 * This contains a slightly modified version of DBTreeCopyConnect.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResConDCS.c,v 1.5 2010/06/24 12:37:56 tim Exp $";
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
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"

typedef struct
{
    Rect		area;		/* Area to process */
    TileTypeBitMask	*connectMask;	/* Connection mask for search */
    TileType		dinfo;		/* Info about triangular search areas */
} conSrArea;

struct conSrArg2
{
    CellUse             *csa2_use;      /* Destination use */
    TileTypeBitMask     *csa2_connect;  /* Table indicating what connects
                                         * to what.
                                         */
    SearchContext	*csa2_topscx;	/* Original top-level search context */
    int			 csa2_xMask;	/* Cell window mask for search */
    Rect                *csa2_bounds;   /* Area that limits the search */

    conSrArea		*csa2_list;	/* List of areas to process */
    int			csa2_top;	/* Index of next area to process */
    int			csa2_size;	/* Max. number bins in area list */
};

#define CSA2_LIST_START_SIZE 256

extern int dbcUnconnectFunc();
extern int dbcConnectLabelFunc();
extern int dbcConnectFuncDCS();
#ifdef ARIEL
extern int resSubSearchFunc();
#endif

static ResDevTile  *DevList = NULL;
static TileTypeBitMask	DiffTypeBitMask;
TileTypeBitMask	ResSubsTypeBitMask;

/* Forward declarations */
extern void ResCalcPerimOverlap();


/*
 * ----------------------------------------------------------------------------
 *
 * dbcConnectFuncDCS -- the same as dbcConnectFunc, except that it does
 *	some extra searching around diffusion tiles looking for
 *	devices.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Adds a new record to the current check list. May also add new
 *	ResDevTile structures.
 *
 * ----------------------------------------------------------------------------
 */

int
dbcConnectFuncDCS(tile, cx)
    Tile *tile;
    TreeContext *cx;

{
    struct conSrArg2	*csa2;
    Rect 		tileArea, *srArea, devArea, newarea;
    ResDevTile		*thisDev;
    TileTypeBitMask	notConnectMask, *connectMask;
    Tile		*tp;
    TileType		t2, t1, loctype, ctype;
    TileType		dinfo = 0;
    SearchContext	*scx = cx->tc_scx;
    SearchContext	scx2;
    int			pNum;
    CellDef		*def;
    ExtDevice		*devptr;
    TerminalPath	tpath;
    char pathstring[FLATTERMSIZE];

    TiToRect(tile, &tileArea);
    srArea = &scx->scx_area;

    if (((tileArea.r_xbot >= srArea->r_xtop-1) ||
		(tileArea.r_xtop <= srArea->r_xbot+1)) &&
		((tileArea.r_ybot >= srArea->r_ytop-1) ||
		(tileArea.r_ytop <= srArea->r_ybot+1)))
	return 0;

    t1 = TiGetType(tile);
    if TTMaskHasType(&DiffTypeBitMask,t1)
    {
    /* left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp=RT(tp))
    {
         t2 = TiGetType(tp);
	 devptr = ExtCurStyle->exts_device[t2];
	 if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask),t2) &&
	     TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),t1))
	     {
    	          TiToRect(tp, &devArea);
	          thisDev = (ResDevTile *) mallocMagic((unsigned)(sizeof(ResDevTile)));
		  ResCalcPerimOverlap(thisDev,tp);
	          GeoTransRect(&scx->scx_trans, &devArea, &thisDev->area);
	          thisDev->type = TiGetType(tp);
	          thisDev->nextDev = DevList;
	          DevList = thisDev;
	     }
    }
    /*right*/
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp=LB(tp))
    {
         t2 = TiGetType(tp);
	 devptr = ExtCurStyle->exts_device[t2];
	 if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask),t2) &&
	     TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),t1))
	     {
    	          TiToRect(tp, &devArea);
	          thisDev = (ResDevTile *) mallocMagic((unsigned)(sizeof(ResDevTile)));
	          GeoTransRect(&scx->scx_trans, &devArea, &thisDev->area);
	          thisDev->type = TiGetType(tp);
	          thisDev->nextDev = DevList;
	          DevList = thisDev;
		  ResCalcPerimOverlap(thisDev,tp);
	     }
    }
    /*top*/
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp=BL(tp))
    {
         t2 = TiGetType(tp);
	 devptr = ExtCurStyle->exts_device[t2];
	 if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask),t2) &&
	     TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),t1))
	     {
    	          TiToRect(tp, &devArea);
	          thisDev = (ResDevTile *) mallocMagic((unsigned)(sizeof(ResDevTile)));
	          GeoTransRect(&scx->scx_trans, &devArea, &thisDev->area);
	          thisDev->type = TiGetType(tp);
	          thisDev->nextDev = DevList;
	          DevList = thisDev;
		  ResCalcPerimOverlap(thisDev,tp);
	     }
    }
    /*bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp=TR(tp))
    {
         t2 = TiGetType(tp);
	 devptr = ExtCurStyle->exts_device[t2];
	 if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask),t2) &&
	     TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),t1))
	     {
    	          TiToRect(tp, &devArea);
	          thisDev = (ResDevTile *) mallocMagic((unsigned)(sizeof(ResDevTile)));
	          GeoTransRect(&scx->scx_trans, &devArea, &thisDev->area);
	          thisDev->type = TiGetType(tp);
	          thisDev->nextDev = DevList;
	          DevList = thisDev;
		  ResCalcPerimOverlap(thisDev,tp);
	     }
    }
    }
    else if TTMaskHasType(&(ExtCurStyle->exts_deviceMask),t1)
    {
    	          TiToRect(tile, &devArea);
	          thisDev = (ResDevTile *) mallocMagic((unsigned)(sizeof(ResDevTile)));
		  ResCalcPerimOverlap(thisDev,tile);
	          GeoTransRect(&scx->scx_trans, &devArea, &thisDev->area);
	          thisDev->type = TiGetType(tile);
	          thisDev->nextDev = DevList;
	          DevList = thisDev;
    }
    /* in some cases (primarily bipolar technology), we'll want to extract
       devices whose substrate terminals are part of the given region.
       The following does that check.  (10-11-88)
    */
#ifdef ARIEL
    if (TTMaskHasType(&ResSubsTypeBitMask,t1) && (ResOptionsFlags & ResOpt_DoSubstrate))
    {
	 TileTypeBitMask  *mask = &ExtCurStyle->exts_subsTransistorTypes[t1];

	 for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
         {
	     if (TTMaskIntersect(&DBPlaneTypes[pNum], mask))
	     {
	          (void)DBSrPaintArea((Tile *) NULL,
		  	scx->scx_use->cu_def->cd_planes[pNum],
		        &tileArea,mask,resSubSearchFunc, (ClientData) cx);
	     }
         }
    }
#endif
    GeoTransRect(&scx->scx_trans, &tileArea, &newarea);

    csa2 = (struct conSrArg2 *)cx->tc_filter->tf_arg;
    GeoClip(&newarea, csa2->csa2_bounds);
    if (GEO_RECTNULL(&newarea)) return 0;

    loctype = TiGetTypeExact(tile);

    /* Resolve geometric transformations on diagonally-split tiles */

    if (IsSplit(tile))
    {
	dinfo = DBTransformDiagonal(loctype, &scx->scx_trans);
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    }

    pNum = cx->tc_plane;
    connectMask = &csa2->csa2_connect[loctype];

    if (DBIsContact(loctype))
    {
	/* The mask of contact types must include all stacked contacts */

	TTMaskZero(&notConnectMask);
	TTMaskSetMask(&notConnectMask, &DBNotConnectTbl[loctype]);
    }
    else
    {
	TTMaskCom2(&notConnectMask, connectMask);
    }

    def = csa2->csa2_use->cu_def;

    if (DBSrPaintNMArea((Tile *) NULL, def->cd_planes[pNum],
		dinfo, &newarea, &notConnectMask, dbcUnconnectFunc,
		(ClientData)NULL) == 0)
	return 0;

    DBNMPaintPlane(def->cd_planes[pNum], dinfo,
		&newarea, DBStdPaintTbl(loctype, pNum),
		(PaintUndoInfo *) NULL);

    /* Check the source def for any labels belonging to this	*/
    /* tile area and plane, and add them to the destination	*/

    scx2 = *csa2->csa2_topscx;
    scx2.scx_area = newarea;

    pathstring[0] = '\0';
    tpath.tp_first = tpath.tp_next = pathstring;
    tpath.tp_last = pathstring + FLATTERMSIZE;

    DBTreeSrLabels(&scx2, connectMask, csa2->csa2_xMask, &tpath,
    		TF_LABEL_ATTACH, dbcConnectLabelFunc,
    		(ClientData)csa2);
    // DBCellCopyLabels(&scx2, connectMask, csa2->csa2_xMask, csa2->csa2_use, NULL);

    /* Only extend those sides bordering the diagonal tile */

    if (dinfo & TT_DIAGONAL)
    {
	if (dinfo & TT_SIDE)               /* right */
	    newarea.r_xtop += 1;
	else                                            /* left */
	    newarea.r_xbot -= 1;
	if (((dinfo & TT_SIDE) >> 1)
		== (dinfo & TT_DIRECTION)) /* top */
	    newarea.r_ytop += 1;
	else                                            /* bottom */
	    newarea.r_ybot -= 1;
    }
    else
    {
	newarea.r_xbot -= 1;
	newarea.r_ybot -= 1;
	newarea.r_xtop += 1;
	newarea.r_ytop += 1;
    }

    /* Register the area and connection mask as needing to be processed */

    if (++csa2->csa2_top == csa2->csa2_size)
    {
	/* Reached list size limit---need to enlarge the list	   */
	/* Double the size of the list every time we hit the limit */

	conSrArea *newlist;
	int i, lastsize = csa2->csa2_size;

	csa2->csa2_size *= 2;

	newlist = (conSrArea *)mallocMagic(csa2->csa2_size * sizeof(conSrArea));
	memcpy((void *)newlist, (void *)csa2->csa2_list,
			(size_t)lastsize * sizeof(conSrArea));
	// for (i = 0; i < lastsize; i++)
	// {
	//     newlist[i].area = csa2->csa2_list[i].area;
	//     newlist[i].connectMask = csa2->csa2_list[i].connectMask;
	//     newlist[i].dinfo = csa2->csa2_list[i].dinfo;
	// }
	freeMagic((char *)csa2->csa2_list);
	csa2->csa2_list = newlist;
    }

    csa2->csa2_list[csa2->csa2_top].area = newarea;
    csa2->csa2_list[csa2->csa2_top].connectMask = connectMask;
    csa2->csa2_list[csa2->csa2_top].dinfo = dinfo;

    return 0;
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCalcPerimOverlap--
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *
 *-------------------------------------------------------------------------
 */

void
ResCalcPerimOverlap(dev, tile)
    ResDevTile	*dev;
    Tile	*tile;

{
    Tile	*tp;
    int		t1;
    int		overlap;

    dev->perim = (TOP(tile)-BOTTOM(tile)-LEFT(tile)+RIGHT(tile))<<1;
    overlap =0;

    t1 = TiGetType(tile);
    /* left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp=RT(tp))
    {
	if TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]),TiGetType(tp))
	{
	      overlap += MIN(TOP(tile),TOP(tp))-
	   		  MAX(BOTTOM(tile),BOTTOM(tp));
	}

    }
    /*right*/
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp=LB(tp))
    {
	if TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]),TiGetType(tp))
	{
	      overlap += MIN(TOP(tile),TOP(tp))-
	   		  MAX(BOTTOM(tile),BOTTOM(tp));
	}

    }
    /*top*/
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp=BL(tp))
    {
	if TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]),TiGetType(tp))
	{
	      overlap += MIN(RIGHT(tile),RIGHT(tp))-
	   		  MAX(LEFT(tile),LEFT(tp));
	}

    }
    /*bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp=TR(tp))
    {
	if TTMaskHasType(&(ExtCurStyle->exts_nodeConn[t1]),TiGetType(tp))
	{
	      overlap += MIN(RIGHT(tile),RIGHT(tp))-
	   		  MAX(LEFT(tile),LEFT(tp));
	}

    }
    dev->overlap = overlap;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBTreeCopyConnectDCS --
 *
 * 	Basically the same as DBTreeCopyConnect, except it calls
 *	dbcConnectFuncDCS.
 *
 * Results:
 *	Linked list of devices.
 *
 * Side effects:
 *	The contents of the result cell are modified.
 *
 * ----------------------------------------------------------------------------
 */

ResDevTile *
DBTreeCopyConnectDCS(scx, mask, xMask, connect, area, destUse)
    SearchContext *scx;
    TileTypeBitMask *mask;
    int xMask;
    TileTypeBitMask *connect;
    Rect *area;
    CellUse *destUse;

{
    static int 		first = 1;
    struct conSrArg2	csa2;
    int			dev, pNum;
    char		*dev_name;
    TileTypeBitMask	*newmask;
    ResDevTile		*CurrentT;
    CellDef		*def = destUse->cu_def;
    TileType		newtype;
    ExtDevice		*devptr;

    csa2.csa2_use = destUse;
    csa2.csa2_xMask = xMask;
    csa2.csa2_bounds = area;
    csa2.csa2_connect = connect;
    csa2.csa2_topscx = scx;

    csa2.csa2_size = CSA2_LIST_START_SIZE;
    csa2.csa2_list = (conSrArea *)mallocMagic(CSA2_LIST_START_SIZE
		* sizeof(conSrArea));
    csa2.csa2_top = -1;

    if (first)
    {
	TTMaskZero(&DiffTypeBitMask);
	TTMaskZero(&ResSubsTypeBitMask);
	for (dev = TT_TECHDEPBASE; dev < TT_MAXTYPES; dev++)
	{
	    devptr = ExtCurStyle->exts_device[dev];
	    if ((devptr != NULL) && ((dev_name = devptr->exts_deviceName) != NULL)
		    && (strcmp(dev_name, "None")))
	    {
		TTMaskSetMask(&DiffTypeBitMask,
	      		&(devptr->exts_deviceSDTypes[0]));
		TTMaskSetMask(&ResSubsTypeBitMask,
	      		&(devptr->exts_deviceSubstrateTypes));
	    }
	}
	first = 0;
    }

    DevList = NULL;
    DBTreeSrTiles(scx, mask, xMask, dbcConnectFuncDCS, (ClientData) &csa2);
    while (csa2.csa2_top >= 0)
    {
	newmask = csa2.csa2_list[csa2.csa2_top].connectMask;
	scx->scx_area = csa2.csa2_list[csa2.csa2_top].area;
	newtype = csa2.csa2_list[csa2.csa2_top].dinfo;
	csa2.csa2_top--;
	if (newtype & TT_DIAGONAL)
	    DBTreeSrNMTiles(scx, newtype, newmask, xMask, dbcConnectFuncDCS,
			(ClientData) &csa2);
	else
	    DBTreeSrTiles(scx, newmask, xMask, dbcConnectFuncDCS, (ClientData) &csa2);
    }
    freeMagic((char *)csa2.csa2_list);

    for (CurrentT = DevList; CurrentT != NULL; CurrentT=CurrentT->nextDev)
    {
	TileType  t = CurrentT->type;
	TileType  nt;
	TileTypeBitMask *residues = DBResidueMask(t);

	for (nt = TT_TECHDEPBASE; nt < DBNumTypes; nt++)
	{
	    if (TTMaskHasType(residues, nt))
	    {
		pNum = DBPlane(nt);
		DBPaintPlane(def->cd_planes[pNum], &CurrentT->area,
			DBStdPaintTbl(nt, pNum), (PaintUndoInfo *) NULL);
	    }
	}
    }

    DBReComputeBbox(def);
    return(DevList);
}


#ifdef ARIEL
/*
 *-------------------------------------------------------------------------
 *
 * resSubSearchFunc --
 *
 *	called when DBSrPaintArea finds a device within
 *	a substrate area.
 *
 * Results:
 *	Always return 0 to keep the search alive.
 *
 * Side Effects:
 *
 *-------------------------------------------------------------------------
 */

int
resSubSearchFunc(tile,cx)
	Tile	*tile;
	TreeContext	*cx;


{
     ResDevTile		*thisDev;
     Rect		devArea;
     TileType		t = TiGetType(tile);
     ExtDevice		*devptr;

     /* Right now, we're only going to extract substrate terminals for
     	devices with only one diffusion terminal, principally bipolar
	devices.
     */
     devptr = ExtCurStyle->exts_device[t]
     if (devptr->exts_deviceSDCount >1) return 0;
     TiToRect(tile, &devArea);
     thisDev = (ResDevTile *) mallocMagic((unsigned)(sizeof(ResDevTile)));
     GeoTransRect(&cx->tc_scx->scx_trans, &devArea, &thisDev->area);
     thisDev->type = t;
     thisDev->nextDev = DevList;
     DevList = thisDev;
     ResCalcPerimOverlap(thisDev,tile);

     return 0;
}

#endif 	/* ARIEL */
