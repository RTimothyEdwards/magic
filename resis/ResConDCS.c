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

static ResTranTile  *TransList = NULL;
static TileTypeBitMask	DiffTypeBitMask;
TileTypeBitMask	ResSubsTypeBitMask;

/* Forward declarations */
extern void ResCalcPerimOverlap();


/*
 * ----------------------------------------------------------------------------
 *
 * dbcConnectFuncDCS -- the same as dbcConnectFunc, except that it does
 *	some extra searching around diffusion tiles looking for
 *	transistors.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Adds a new record to the current check list. May also add new
 *	ResTranTile structures.
 *
 * ----------------------------------------------------------------------------
 */

int
dbcConnectFuncDCS(tile, cx)
    Tile *tile;		
    TreeContext *cx;

{
    struct conSrArg2	*csa2;
    Rect 		tileArea, *srArea, tranArea, newarea;
    ResTranTile		*thisTran;
    TileTypeBitMask	notConnectMask, *connectMask;
    Tile		*tp;
    TileType		t2, t1, loctype, ctype;
    TileType		dinfo = 0;
    SearchContext	*scx = cx->tc_scx;
    SearchContext	scx2;
    int			pNum;
    CellDef		*def;

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
	 if (TTMaskHasType(&(ExtCurStyle->exts_transMask),t2) &&
	     TTMaskHasType(&(ExtCurStyle->exts_transSDTypes[t2][0]),t1))
	     {
    	          TiToRect(tp, &tranArea);
	          thisTran = (ResTranTile *) mallocMagic((unsigned)(sizeof(ResTranTile)));
		  ResCalcPerimOverlap(thisTran,tp);
	          GeoTransRect(&scx->scx_trans, &tranArea, &thisTran->area);
	          thisTran->type = TiGetType(tp);
	          thisTran->nextTran = TransList;
	          TransList = thisTran;
	     }
    }
    /*right*/
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp=LB(tp))
    {
         t2 = TiGetType(tp);
	 if (TTMaskHasType(&(ExtCurStyle->exts_transMask),t2) &&
	     TTMaskHasType(&(ExtCurStyle->exts_transSDTypes[t2][0]),t1))
	     {
    	          TiToRect(tp, &tranArea);
	          thisTran = (ResTranTile *) mallocMagic((unsigned)(sizeof(ResTranTile)));
	          GeoTransRect(&scx->scx_trans, &tranArea, &thisTran->area);
	          thisTran->type = TiGetType(tp);
	          thisTran->nextTran = TransList;
	          TransList = thisTran;
		  ResCalcPerimOverlap(thisTran,tp);
	     }
    }
    /*top*/
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp=BL(tp))
    {
         t2 = TiGetType(tp);
	 if (TTMaskHasType(&(ExtCurStyle->exts_transMask),t2) &&
	     TTMaskHasType(&(ExtCurStyle->exts_transSDTypes[t2][0]),t1))
	     {
    	          TiToRect(tp, &tranArea);
	          thisTran = (ResTranTile *) mallocMagic((unsigned)(sizeof(ResTranTile)));
	          GeoTransRect(&scx->scx_trans, &tranArea, &thisTran->area);
	          thisTran->type = TiGetType(tp);
	          thisTran->nextTran = TransList;
	          TransList = thisTran;
		  ResCalcPerimOverlap(thisTran,tp);
	     }
    }
    /*bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp=TR(tp))
    {
         t2 = TiGetType(tp);
	 if (TTMaskHasType(&(ExtCurStyle->exts_transMask),t2) &&
	     TTMaskHasType(&(ExtCurStyle->exts_transSDTypes[t2][0]),t1))
	     {
    	          TiToRect(tp, &tranArea);
	          thisTran = (ResTranTile *) mallocMagic((unsigned)(sizeof(ResTranTile)));
	          GeoTransRect(&scx->scx_trans, &tranArea, &thisTran->area);
	          thisTran->type = TiGetType(tp);
	          thisTran->nextTran = TransList;
	          TransList = thisTran;
		  ResCalcPerimOverlap(thisTran,tp);
	     }
    }
    }
    else if TTMaskHasType(&(ExtCurStyle->exts_transMask),t1)
    {
    	          TiToRect(tile, &tranArea);
	          thisTran = (ResTranTile *) mallocMagic((unsigned)(sizeof(ResTranTile)));
		  ResCalcPerimOverlap(thisTran,tile);
	          GeoTransRect(&scx->scx_trans, &tranArea, &thisTran->area);
	          thisTran->type = TiGetType(tile);
	          thisTran->nextTran = TransList;
	          TransList = thisTran;
    }
    /* in some cases (primarily bipolar technology), we'll want to extract
       transistors whose substrate terminals are part of the given region.
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
    DBTreeSrLabels(&scx2, connectMask, csa2->csa2_xMask, NULL,
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
ResCalcPerimOverlap(trans, tile)
    ResTranTile	*trans;
    Tile	*tile;

{
    Tile	*tp;
    int		t1;
    int		overlap;
    
    trans->perim = (TOP(tile)-BOTTOM(tile)-LEFT(tile)+RIGHT(tile))<<1;
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
    trans->overlap = overlap;
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
 *	Linked list of transistors.
 *
 * Side effects:
 *	The contents of the result cell are modified.
 *
 * ----------------------------------------------------------------------------
 */

ResTranTile *
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
    int			tran, pNum;
    char		*tran_name;
    TileTypeBitMask	*newmask;
    ResTranTile		*CurrentT;
    CellDef		*def = destUse->cu_def;
    TileType		newtype;

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
	for (tran = TT_TECHDEPBASE; tran < TT_MAXTYPES; tran++)
	{
	    tran_name = (ExtCurStyle->exts_transName)[tran];
	    if ((tran_name != NULL) && (strcmp(tran_name, "None")))
	    {
		TTMaskSetMask(&DiffTypeBitMask,
	      		&(ExtCurStyle->exts_transSDTypes[tran][0]));
		TTMaskSetMask(&ResSubsTypeBitMask,
	      		&(ExtCurStyle->exts_transSubstrateTypes[tran]));
	    }
	}
	first = 0;
    }

    TransList = NULL;
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

    for (CurrentT = TransList; CurrentT != NULL; CurrentT=CurrentT->nextTran)
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
    return(TransList);
}


#ifdef ARIEL
/*
 *-------------------------------------------------------------------------
 *
 * resSubSearchFunc --
 *
 *	called when DBSrPaintArea finds a transistor within
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
     ResTranTile	*thisTran;
     Rect		tranArea;
     TileType		t = TiGetType(tile);

     /* Right now, we're only going to extract substrate terminals for 
     	devices with only one diffusion terminal, principally bipolar
	devices.
     */
     if (ExtCurStyle->exts_transSDCount[t] >1) return 0;
     TiToRect(tile, &tranArea);
     thisTran = (ResTranTile *) mallocMagic((unsigned)(sizeof(ResTranTile)));
     GeoTransRect(&cx->tc_scx->scx_trans, &tranArea, &thisTran->area);
     thisTran->type = t;
     thisTran->nextTran = TransList;
     TransList = thisTran;
     ResCalcPerimOverlap(thisTran,tile);
     
     return 0;
}

#endif 	/* ARIEL */
