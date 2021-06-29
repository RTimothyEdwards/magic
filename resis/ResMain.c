#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResMain.c,v 1.4 2010/06/24 12:37:56 tim Exp $";
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
#include "select/select.h"
#include "textio/txcommands.h"
#include "resis/resis.h"

CellUse 		*ResUse = NULL;		/* Our use and def */
CellDef 		*ResDef = NULL;
TileTypeBitMask 	ResConnectWithSD[NT];	/* A mask that goes from  */
						/* SD's to devices.	  */
TileTypeBitMask 	ResCopyMask[NT];	/* Indicates which tiles  */
						/* are to be copied.      */
resResistor 		*ResResList = NULL;	/* Resistor list	  */
resNode     		*ResNodeList = NULL;	/* Processed Nodes 	  */
resDevice 		*ResDevList = NULL;	/* Devices		  */
ResContactPoint		*ResContactList = NULL;	/* Contacts		  */
resNode			*ResNodeQueue = NULL;	/* Pending nodes	  */
resNode			*ResOriginNode = NULL;	/* node where R=0	  */
resNode			*resCurrentNode;
int			ResTileCount = 0;	/* Number of tiles rn_status */
extern Region 		*ResFirst();
extern Tile		*FindStartTile();
extern int		ResEachTile();
extern int		ResLaplaceTile();
extern ResSimNode	*ResInitializeNode();

extern HashTable	ResNodeTable;

/*
 *--------------------------------------------------------------------------
 *
 * ResInitializeConn--
 *
 *  Sets up mask by Source/Drain type of devices. This is
 *  exts_deviceSDtypes turned inside out.
 *
 *  Results: none
 *
 * Side Effects: Sets up ResConnectWithSD.
 *
 *-------------------------------------------------------------------------
 */

void
ResInitializeConn()
{
    TileType dev, diff;
    char *dev_name;
    ExtDevice *devptr;

    for (dev = TT_TECHDEPBASE; dev < TT_MAXTYPES; dev++)
    {
	devptr = ExtCurStyle->exts_device[dev];
	if ((devptr != NULL) && ((dev_name = devptr->exts_deviceName) != NULL)
		&& (strcmp(dev_name, "None")))
	{
	    for (diff = TT_TECHDEPBASE; diff < TT_MAXTYPES; diff++)
	    {
		if TTMaskHasType(&(devptr->exts_deviceSDTypes[0]), diff)
		    TTMaskSetType(&ResConnectWithSD[diff], dev);

		if TTMaskHasType(&(devptr->exts_deviceSubstrateTypes), diff)
		    TTMaskSetType(&ResConnectWithSD[diff], dev);
	    }
	}
	TTMaskSetMask(&ResConnectWithSD[dev], &DBConnectTbl[dev]);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 *  ResGetReCell --
 *
 * 	This procedure makes sure that ResUse,ResDef
 *	have been properly initialized to refer to a cell definition
 *	named "__RESIS__".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new cell use and/or def are created if necessary.
 *
 * --------------------------------------------------------------------------
 */

void
ResGetReCell()
{
    if (ResUse != NULL) return;
    ResDef = DBCellLookDef("__RESIS__");
    if (ResDef == NULL)
    {
	ResDef = DBCellNewDef("__RESIS__");
	ASSERT (ResDef != (CellDef *) NULL, "ResGetReCell");
	DBCellSetAvail(ResDef);
	ResDef->cd_flags |= CDINTERNAL;
    }
    ResUse = DBCellNewUse(ResDef, (char *) NULL);
    DBSetTrans(ResUse, &GeoIdentityTransform);
    ResUse->cu_expandMask = CU_DESCEND_SPECIAL;
}

/*
 *--------------------------------------------------------------------------
 *
 *  ResDissolveContacts--
 *
 *  results:  none
 *
 *  Side Effects:  All contacts in the design are broken into their
 *    constituent
 *    layers.  There should be no contacts in ResDef after this procedure
 *    runs.
 *
 *
 *------------------------------------------------------------------------
 */
void
ResDissolveContacts(contacts)
	ResContactPoint *contacts;
{
    TileType t, oldtype;
    Tile *tp;
    TileTypeBitMask residues;

    for (; contacts != (ResContactPoint *) NULL; contacts = contacts->cp_nextcontact)

    {
        oldtype=contacts->cp_type;
#ifdef PARANOID
	if (oldtype == TT_SPACE)
	{
	    TxError("Error in Contact Dissolving for %s \n",ResCurrentNode);
	}
#endif

	DBFullResidueMask(oldtype, &residues);

	DBErase(ResUse->cu_def, &(contacts->cp_rect), oldtype);
	for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	{
	    if (TTMaskHasType(&residues, t))
	    {
		if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, t))
		    continue;
		DBPaint(ResUse->cu_def, &(contacts->cp_rect), t);
	    }
	}

	tp = ResDef->cd_planes[DBPlane(contacts->cp_type)]->pl_hint;
	GOTOPOINT(tp, &(contacts->cp_rect.r_ll));
#ifdef PARANOID
	if (TiGetTypeExact(tp) == contacts->cp_type)
	{
	    TxError("Error in Contact Preprocess Routines\n");
	}
#endif
    }
}

/*
 *---------------------------------------------------------------------------
 *
 *  ResMakePortBreakpoints --
 *
 *  Search for nodes which are ports, and force them to be breakpoints
 *  in the "tileJunk" field of their respective tiles in ResUse.  This
 *  ensures that connected nodes that stretch between two ports will
 *  not be assumed to be "hanging" nodes.
 *
 *  Do the same thing for labels.
 *
 *----------------------------------------------------------------------------
 */
void
ResMakePortBreakpoints(def)
    CellDef *def;
{
    Plane	*plane;
    Rect	*rect;
    TileTypeBitMask mask;
    HashSearch  hs;
    HashEntry   *entry;
    ResSimNode  *node;
    int ResAddBreakpointFunc();	/* Forward Declaration */

    HashStartSearch(&hs);
    while((entry = HashNext(&ResNodeTable,&hs)) != NULL)
    {
	node=(ResSimNode *) HashGetValue(entry);
	if (node->status & PORTNODE)
	{
	    plane = def->cd_planes[DBPlane(node->rs_ttype)];
	    rect  = &(node->rs_bbox);

	    /* Beware of zero-area ports */
	    if (rect->r_xbot == rect->r_xtop)
	    {
		rect->r_xbot--;
		rect->r_xtop++;
	    }
	    if (rect->r_ybot == rect->r_ytop)
	    {
		rect->r_ybot--;
		rect->r_ytop++;
	    }

	    TTMaskSetOnlyType(&mask, node->rs_ttype);
	    (void) DBSrPaintArea((Tile *) NULL, plane, rect, &mask,
			ResAddBreakpointFunc, (ClientData)node);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 *  ResMakeLabelBreakpoints --
 *
 *  Search for labels that are part of a node, and force them to be
 *  breakpoints in the "tileJunk" field of their respective tiles in
 *  ResUse.  This ensures (among other things) that pins of a top level
 *  cell will be retained and become the endpoint of a net.
 *
 *----------------------------------------------------------------------------
 */
void
ResMakeLabelBreakpoints(def)
    CellDef *def;
{
    Plane	*plane;
    Rect	*rect;
    TileTypeBitMask mask;
    HashEntry   *entry;
    ResSimNode  *node;
    Label	*slab;
    int ResAddBreakpointFunc();	/* Forward Declaration */

    for (slab = def->cd_labels; slab != NULL; slab = slab->lab_next)
    {
	entry = HashFind(&ResNodeTable, slab->lab_text);
	node = ResInitializeNode(entry);

        node->drivepoint = slab->lab_rect.r_ll;
        node->rs_bbox = slab->lab_rect;
        node->location = slab->lab_rect.r_ll;
        node->rs_ttype = slab->lab_type;
        node->type = slab->lab_type;

	plane = def->cd_planes[DBPlane(slab->lab_type)];
	rect  = &(node->rs_bbox);

	TTMaskSetOnlyType(&mask, slab->lab_type);
	(void) DBSrPaintArea((Tile *) NULL, plane, rect, &mask,
			ResAddBreakpointFunc, (ClientData)node);

    }
}

/*
 *----------------------------------------------------------------------------
 *
 * ResAddBreakpointFunc --
 *
 *	Add a breakpoint to the "tileJunk" structure of the tile
 *
 *----------------------------------------------------------------------------
 */

int
ResAddBreakpointFunc(tile, node)
   Tile *tile;
   ResSimNode *node;
{
    tileJunk *junk;

    if (tile->ti_client == (ClientData) CLIENTDEFAULT)
	return 0;

    NEWPORT(node, tile);

    return 0;
}


/*
 *---------------------------------------------------------------------------
 *
 *  ResFindNewContactTiles --
 *
 *
 *  Results:  none
 *
 *  Side Effects:  dissolving contacts eliminated the tiles that
 *  contacts->nextcontact pointed to. This procedure finds the tile now under
 *  center and sets that tile's ti_client field to point to the contact.  The
 *  old value of clientdata is set to nextTilecontact.
 *
 *----------------------------------------------------------------------------
 */

void
ResFindNewContactTiles(contacts)
    ResContactPoint *contacts;
{
    int pNum;
    Tile *tile;
    TileTypeBitMask mask;

    for (; contacts != (ResContactPoint *) NULL; contacts = contacts->cp_nextcontact)
    {
	DBFullResidueMask(contacts->cp_type, &mask);
     	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	{
	    tile = ResDef->cd_planes[pNum]->pl_hint;
	    GOTOPOINT(tile, &(contacts->cp_center));
#ifdef PARANOID
	    if (tile == (Tile *) NULL)
	    {
		TxError("Error: setting contact tile to null\n");
	    }
#endif
	    if ((IsSplit(tile) && TTMaskHasType(&mask, TiGetRightType(tile)))
			|| TTMaskHasType(&mask, TiGetType(tile)))
	    {
		tileJunk *j = (tileJunk *)tile->ti_client;
		cElement *ce;

		ce = (cElement *) mallocMagic((unsigned) (sizeof(cElement)));
		contacts->cp_tile[contacts->cp_currentcontact] = tile;
		ce->ce_thisc = contacts;
		ce->ce_nextc = j->contactList;
		(contacts->cp_currentcontact) += 1;
		j->contactList = ce;
	    }
	}
#ifdef PARANOID
	if (contacts->cp_currentcontact > LAYERS_PER_CONTACT)
	{
	    TxError("Error: Not enough space allocated for contact nodes\n");
	}
#endif
    }
}

/*
 *--------------------------------------------------------------------------
 *
 * ResProcessTiles--Calls ResEachTile with processed tiles belonging to
 *		nodes in ResNodeQueue.  When all the tiles corresponding
 *		to a node have been processed, the node is moved to
 *		ResNodeList.
 *
 *  Results:  Return 1 if any error occurred, 0 otherwise.
 *
 *  Side Effects: Cleans extraneous linked lists from nodes.
 *
 *--------------------------------------------------------------------------
 */

int
ResProcessTiles(goodies, origin)
    Point		*origin;
    ResGlobalParams	*goodies;

{
    Tile 	*startTile;
    int 	tilenum, merged;
    resNode	*resptr2;
    jElement	*workingj;
    cElement	*workingc;
    ResFixPoint	*fix;
    resNode	*resptr;
    int		(*tilefunc)();

#ifdef LAPLACE
    tilefunc = (ResOptionsFlags & ResOpt_DoLaplace) ? ResLaplaceTile : ResEachTile;
#else
    tilefunc = ResEachTile;
#endif

    if (ResOptionsFlags & ResOpt_Signal)
    {
        startTile = FindStartTile(goodies, origin);
        if (startTile == NULL) return(1);
	resCurrentNode = NULL;
	(void) (*tilefunc)(startTile, origin);
    }
#ifdef ARIEL
    else if (ResOptionsFlags & ResOpt_Power)
    {
    	for (fix = ResFixList; fix != NULL; fix = fix->fp_next)
	{
      	    Tile *tile = fix->fp_tile;
	    if (tile == NULL)
	    {
		tile = ResDef->cd_planes[DBPlane(fix->fp_ttype)]->pl_hint;
		GOTOPOINT(tile, &(fix->fp_loc));
		if (TiGetTypeExact(tile) != TT_SPACE)
		{
		    fix->fp_tile = tile;
		}
		else
		{
		    tile = NULL;
		}
	    }
	    if (tile != NULL)
	    {
	        int x = fix->fp_loc.p_x;
	        int y = fix->fp_loc.p_y;
		resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
		InitializeNode(resptr, x, y, RES_NODE_ORIGIN);
	        resptr->rn_status = TRUE;
	        resptr->rn_noderes = 0;
	        ResAddToQueue(resptr, &ResNodeQueue);
		fix->fp_node = resptr;
		NEWBREAK(resptr, tile, x, y, NULL);
	    }
	}
    	for (fix = ResFixList; fix != NULL; fix = fix->fp_next)
	{
      	    Tile    *tile = fix->fp_tile;

	    if (tile != NULL && (((tileJunk *)tile->ti_client)->tj_status &
			RES_TILE_DONE) == 0)
	    {
	        resCurrentNode = fix->fp_node;
	      	(void) (*tilefunc)(tile, (Point *)NULL);
	    }
	}
    }
#endif
#ifdef PARANOID
    else
    {
    	TxError("Unknown analysis type in ResProcessTiles\n");
    }
#endif

    /* Process Everything else */

    while (ResNodeQueue != NULL)
    {
	/*
	 * merged keeps track of whether another node gets merged into
	 * the current one.  If it does, then the node must be processed
	 * because additional junctions or contacts were added
	 */

	resptr2 = ResNodeQueue;
	merged = FALSE;

	/* Process all junctions associated with node */

	for (workingj = resptr2->rn_je; workingj != NULL; workingj = workingj->je_nextj)
	{
	    ResJunction	*rj = workingj->je_thisj;
	    if (rj->rj_status == FALSE)
	    {
		for (tilenum = 0; tilenum < TILES_PER_JUNCTION; tilenum++)
		{
	      	    Tile *tile = rj->rj_Tile[tilenum];
		    tileJunk *j = (tileJunk *)tile->ti_client;

		    if ((j->tj_status & RES_TILE_DONE) == 0)
		    {
			resCurrentNode = resptr2;
			merged |= (*tilefunc)(tile,(Point *)NULL);
		    }
		    if (merged & ORIGIN) break;
		}
		if (merged & ORIGIN) break;
		rj->rj_status = TRUE;
	    }
	}

	/* Next, Process all contacts. */

	for (workingc = resptr2->rn_ce;workingc != NULL;workingc = workingc->ce_nextc)
	{
	    ResContactPoint	*cp = workingc->ce_thisc;

	    if (merged & ORIGIN) break;
	    if (cp->cp_status == FALSE)
	    {
		int newstatus = TRUE;
		for (tilenum = 0; tilenum < cp->cp_currentcontact; tilenum++)
		{
	      	    Tile *tile = cp->cp_tile[tilenum];
		    tileJunk *j = (tileJunk *) tile->ti_client;

		    if ((j->tj_status & RES_TILE_DONE) == 0)
		    {
			if (cp->cp_cnode[tilenum] == resptr2)
			{
			    resCurrentNode = resptr2;
			    merged |= (*tilefunc)(tile,(Point *)NULL);
			}
			else
			{
			    newstatus = FALSE;
			}
		    }
		    if (merged & ORIGIN) break;
		}
		if (merged & ORIGIN) break;
		cp->cp_status = newstatus;
	    }
	}

	/*
	 * If nothing new has been added via a merge, then the node is
	 * finished. It is removed from the pending queue, added to the
	 * done list, cleaned up, and passed to ResDoneWithNode
	 */

	if (merged == FALSE)
	{
	    ResRemoveFromQueue(resptr2, &ResNodeQueue);
	    resptr2->rn_more = ResNodeList;
	    resptr2->rn_less = NULL;
	    resptr2->rn_status &= ~PENDING;
	    resptr2->rn_status |= FINISHED | MARKED;
	    if (ResNodeList != NULL)
	    {
		ResNodeList->rn_less = resptr2;
	    }
	    if (resptr2->rn_noderes == 0)
	    {
		ResOriginNode=resptr2;
	    }
	    ResNodeList = resptr2;
	    ResCleanNode(resptr2, FALSE, &ResNodeList, &ResNodeQueue);
	    ResDoneWithNode(resptr2);
	}
    }
    return(0);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResCalcPerimOverlap ---
 *
 *  Given a device tile, compute simple perimeter and overlap of the device
 *  by the net under consideration.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The ResDevTile structure is updated with the overlap and perimeter
 *	values.
 *
 *-------------------------------------------------------------------------
 */

void
ResCalcPerimOverlap(tile, dev)
    Tile	*tile;
    ResDevTile	*dev;
{
    Tile	    *tp;
    int		    t1;
    int		    overlap;
    TileTypeBitMask *omask;

    dev->perim = (TOP(tile) - BOTTOM(tile) - LEFT(tile) + RIGHT(tile)) << 1;
    overlap = 0;

    t1 = TiGetType(tile);
    omask = &(ExtCurStyle->exts_nodeConn[t1]);

    /* left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	if TTMaskHasType(omask, TiGetType(tp))
	    overlap += MIN(TOP(tile), TOP(tp)) - MAX(BOTTOM(tile), BOTTOM(tp));
    }

    /* right */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp=LB(tp))
    {
	if TTMaskHasType(omask, TiGetType(tp))
	    overlap += MIN(TOP(tile), TOP(tp)) - MAX(BOTTOM(tile), BOTTOM(tp));
    }

    /* top */
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	if TTMaskHasType(omask, TiGetType(tp))
	    overlap += MIN(RIGHT(tile), RIGHT(tp)) - MAX(LEFT(tile), LEFT(tp));
    }

    /* bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp=TR(tp))
    {
	if TTMaskHasType(omask, TiGetType(tp))
	      overlap += MIN(RIGHT(tile), RIGHT(tp)) - MAX(LEFT(tile), LEFT(tp));
    }
    dev->overlap = overlap;
}
/*
 *-------------------------------------------------------------------------
 *
 * resMakeDevFunc --
 *
 *  Callback function from ResExtractNet.  For each device in a node's
 *  device list pulled from the .sim file, find the tile corresponding
 *  to the device in the source tree, and fill out the complete device
 *  record (namely the full device area).
 *
 * Result:
 *	Return 1 to stop the search because the device has been found.
 *
 *-------------------------------------------------------------------------
 */

int
resMakeDevFunc(tile, cx)
    Tile	*tile;
    TreeContext *cx;
{
    ResDevTile	*thisDev = (ResDevTile *)cx->tc_filter->tf_arg;
    Rect	devArea;

    TiToRect(tile, &devArea);
    GeoTransRect(&cx->tc_scx->scx_trans, &devArea, &thisDev->area);
    ResCalcPerimOverlap(tile, thisDev);

    return 1;
}

 
/*
 *-------------------------------------------------------------------------
 *
 * ResExtractNet-- extracts the resistance net at the specified
 *	rn_loc. If the resulting net is greater than the tolerance,
 *	simplify and return the resulting network.
 *
 * Results:  0 iff it worked.
 *
 * Side effects: Produces a resistance network for the node.
 *
 *-------------------------------------------------------------------------
 */

bool
ResExtractNet(node, goodies, cellname)
    ResSimNode		*node;
    ResGlobalParams	*goodies;
    char		*cellname;
{
    SearchContext 	scx;
    int			pNum;
    TileTypeBitMask	FirstTileMask;
    Point		startpoint;
    static int		first = 1;
    ResDevTile		*DevTiles, *thisDev;
    ResFixPoint		*fix;
    devPtr		*tptr;
    int			resMakeDevFunc();

    /* Make sure all global network variables are reset */

    ResResList = NULL;
    ResNodeList = NULL;
    ResDevList = NULL;
    ResNodeQueue = NULL;
    ResContactList = NULL;
    ResOriginNode = NULL;

    /* Pass back network pointers */

    goodies->rg_maxres = 0;
    goodies->rg_tilecount = 0;

    /* Set up internal stuff if this is the first time through */

    if (first)
    {
    	ResInitializeConn();
        first = 0;
        ResGetReCell();
    }

    /* Initialize Cell */

    if (cellname)
    {
	CellDef *def = DBCellLookDef(cellname);
	if (def == (CellDef *)NULL)
	{
	    TxError("Error:  No such cell \"%s\"\n", cellname);
	    return TRUE;
	}
	scx.scx_use = DBCellNewUse(def, (char *)NULL);
	DBSetTrans (scx.scx_use, &GeoIdentityTransform);
	scx.scx_trans = GeoIdentityTransform;
    }
    else
    {
	MagWindow *w = ToolGetBoxWindow(&scx.scx_area, (int *)NULL);
	if (w == (MagWindow *)NULL)
	{
	    TxError("Sorry, the box must appear in one of the windows.\n");
	    return TRUE;
	}
	scx.scx_use = (CellUse *) w->w_surfaceID;
	scx.scx_trans = GeoIdentityTransform;
    }

    DBCellClearDef(ResUse->cu_def);

#ifdef ARIEL
    if ((ResOptionsFlags & ResOpt_Power) &&
	 		strcmp(node->name, goodies->rg_name) != 0) continue;
#endif

    /* Copy Paint */

    scx.scx_area.r_ll.p_x = node->location.p_x - 2;
    scx.scx_area.r_ll.p_y = node->location.p_y - 2;
    scx.scx_area.r_ur.p_x = node->location.p_x + 2;
    scx.scx_area.r_ur.p_y = node->location.p_y + 2;
    startpoint = node->location;

    /* Because node->type might come from a label with a sticky type
     * that does not correspond exactly to the layer underneath, include
     * all connecting types.
     */
    TTMaskZero(&FirstTileMask);
    TTMaskSetMask(&FirstTileMask, &DBConnectTbl[node->type]);

    DBTreeCopyConnect(&scx, &FirstTileMask, 0, ResCopyMask, &TiPlaneRect,
					SEL_DO_LABELS, ResUse);

    /* Add devices to ResUse from list in node */
    DevTiles = NULL;
    for (tptr = node->firstDev; tptr; tptr = tptr->nextDev)
    {
	TileTypeBitMask devMask;

	TTMaskSetOnlyType(&devMask, tptr->thisDev->rs_ttype);
	thisDev = (ResDevTile *)mallocMagic(sizeof(ResDevTile));
	thisDev->devptr = tptr->thisDev->rs_devptr;
	thisDev->type = tptr->thisDev->rs_ttype;
	scx.scx_area.r_ll.p_x = tptr->thisDev->location.p_x;
	scx.scx_area.r_ll.p_y = tptr->thisDev->location.p_y;
	scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
	scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
	DBTreeSrTiles(&scx, &devMask, 0, resMakeDevFunc, (ClientData)thisDev);
	thisDev->nextDev = DevTiles;
	DevTiles = thisDev;

	/* Paint the type into ResUse */
	pNum = DBPlane(thisDev->type);
	DBPaintPlane(ResUse->cu_def->cd_planes[pNum], &thisDev->area,
		DBStdPaintTbl(thisDev->type, pNum), (PaintUndoInfo *)NULL);
    }
    DBReComputeBbox(ResUse->cu_def);

    ExtResetTiles(scx.scx_use->cu_def, extUnInit);

    /* Find all contacts in design and note their position */

    ResContactList = (ResContactPoint *)ExtFindRegions(ResUse->cu_def,
				     &(ResUse->cu_def->cd_bbox),
				     &DBAllButSpaceAndDRCBits,
				     ResConnectWithSD, extUnInit, ResFirst,
				     ResEach);
    ExtResetTiles(ResUse->cu_def, extUnInit);

    /*
     * dissolve the contacts and find which tiles now cover the point
     * where the tile used to be.
     */

    ResDissolveContacts(ResContactList);

    /* Add "junk" fields to tiles */

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
    	Plane	*plane = ResUse->cu_def->cd_planes[pNum];
	Rect	*rect  = &ResUse->cu_def->cd_bbox;
	ResFracture(plane, rect);
	(void) DBSrPaintClient((Tile *) NULL, plane, rect,
	 		&DBAllButSpaceAndDRCBits,
			(ClientData) CLIENTDEFAULT, ResAddPlumbing,
			(ClientData) &ResDevList);
    }

    /* Finish preprocessing. */

    ResMakePortBreakpoints(ResUse->cu_def);
    ResMakeLabelBreakpoints(ResUse->cu_def);
    ResFindNewContactTiles(ResContactList);
    ResPreProcessDevices(DevTiles, ResDevList, ResUse->cu_def);

#ifdef LAPLACE
    if (ResOptionsFlags & ResOpt_DoLaplace)
    {
        for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
        {
    	    Plane   *plane = ResUse->cu_def->cd_planes[pNum];
	    Rect    *rect  = &ResUse->cu_def->cd_bbox;
	    Res1d(plane, rect);
        }
    }
#endif

#ifdef ARIEL
    if (ResOptionsFlags & ResOpt_Power)
    {
    	for (fix = startlist; fix != NULL; fix = fix->fp_next)
	{
     	    fix->fp_tile = ResUse->cu_def->cd_planes[DBPlane(fix->fp_ttype)]->pl_hint;
	    GOTOPOINT(fix->fp_tile, &fix->fp_loc);
	    if (TiGetTypeExact(fix->fp_tile) == TT_SPACE) fix->fp_tile = NULL;
	}
    }
#endif

    /* do extraction */
    if (ResProcessTiles(goodies, &startpoint) != 0) return TRUE;
    return FALSE;
}


/*
 *-------------------------------------------------------------------------
 *
 * ResCleanUpEverything--After each net is extracted by ResExtractNet,
 *	the resulting memory must be freed up, and varius trash swept under
 *	the carpet in preparation for the next extraction.
 *
 * Results: none
 *
 * Side Effects: Frees up memory formerly occupied by network elements.
 *
 *-------------------------------------------------------------------------
 */

void
ResCleanUpEverything()
{

    int		    pNum;
    resResistor	    *oldRes;
    resDevice	    *oldDev;
    ResContactPoint *oldCon;

    /* Check integrity of internal database. Free up lists. */

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
    	(void) DBSrPaintClient((Tile *)NULL, ResUse->cu_def->cd_planes[pNum],
	 		&(ResUse->cu_def->cd_bbox), &DBAllButSpaceAndDRCBits,
			(ClientData)CLIENTDEFAULT, ResRemovePlumbing,
			(ClientData)NULL);
    }

    while (ResNodeList != NULL)
    {
	ResCleanNode(ResNodeList, TRUE, &ResNodeList, &ResNodeQueue);
    }
    while (ResContactList != NULL)
    {
    	 oldCon = ResContactList;
	 ResContactList = oldCon->cp_nextcontact;
	 freeMagic((char *)oldCon);
    }
    while (ResResList != NULL)
    {
    	 oldRes = ResResList;
	 ResResList = ResResList->rr_nextResistor;
	 freeMagic((char *)oldRes);
    }
    while (ResDevList != NULL)
    {
    	 oldDev = ResDevList;
	 ResDevList = ResDevList->rd_nextDev;
	 if ((oldDev->rd_status & RES_DEV_SAVE) == 0)
	 {
	      freeMagic((char *)oldDev->rd_terminals);
	      freeMagic((char *)oldDev);
	 }
    }
    DBCellClearDef(ResUse->cu_def);
}

/*
 *-------------------------------------------------------------------------
 *
 * FindStartTile-- To start the extraction, we need to find the first driver.
 *	The sim file gives us the location of a point in  or near (within 1
 *	unit) of the device. FindStartTile looks for the device, then
 *	for adjoining diffusion. The diffusion tile is returned.
 *
 * Results: returns source diffusion tile, if it exists. Otherwise, return
 *	NULL.
 *
 * Side Effects: none
 *
 *-------------------------------------------------------------------------
 */

Tile *
FindStartTile(goodies, SourcePoint)
    Point		*SourcePoint;
    ResGlobalParams	*goodies;

{
    Point	workingPoint;
    Tile	*tile, *tp;
    int		pnum, t1, t2;
    ExtDevice   *devptr;

    /* If the drive point is on a contact, check for the contact residues   */
    /* first, then the contact type itself.				    */

    if (DBIsContact(goodies->rg_ttype))
    {
	TileTypeBitMask *rmask = DBResidueMask(goodies->rg_ttype);
	TileType savtype = goodies->rg_ttype;
	TileType rtype;

	for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
	    if (TTMaskHasType(rmask, rtype))
	    {
		goodies->rg_ttype = rtype;
		if ((tile = FindStartTile(goodies, SourcePoint)) != NULL)
		{
		    goodies->rg_ttype = savtype;
		    return tile;
		}
	    }
	goodies->rg_ttype = savtype;
    }

    workingPoint.p_x = goodies->rg_devloc->p_x;
    workingPoint.p_y = goodies->rg_devloc->p_y;

    pnum = DBPlane(goodies->rg_ttype);

    /* for drivepoints, we don't have to find a device */
    if (goodies->rg_status & DRIVEONLY)
    {
	tile = ResUse->cu_def->cd_planes[pnum]->pl_hint;
	GOTOPOINT(tile, &workingPoint);
	SourcePoint->p_x = workingPoint.p_x;
	SourcePoint->p_y = workingPoint.p_y;

	if (TiGetTypeExact(tile) == goodies->rg_ttype)
	    return tile;
	else
	{
	    /* On the other hand, drivepoints derived from subcircuit	*/
	    /* boundaries lie on tile boundaries, and GOTOPOINT() will	*/
	    /* pick the tile on the wrong side for TOP and RIGHT	*/
	    /* segment coincidences.					*/

	    if (workingPoint.p_x == LEFT(tile))
	    {
		for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp=RT(tp))
		    if (TiGetRightType(tp) == goodies->rg_ttype)
			return(tp);
	    }
	    else if (workingPoint.p_y == BOTTOM(tile))
	    {
		for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp=TR(tp))
		    if (TiGetTopType(tp) == goodies->rg_ttype)
			return(tp);
	    }
	}
	TxError("Couldn't find wire at %d %d\n",
			goodies->rg_devloc->p_x, goodies->rg_devloc->p_y);
	return NULL;
    }

    tile = ResUse->cu_def->cd_planes[pnum]->pl_hint;
    GOTOPOINT(tile, &workingPoint);

    if (IsSplit(tile))
    {
        if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, TiGetLeftType(tile)) != 0)
	{
	    t1 = TiGetLeftType(tile);
	    TiSetBody(tile, t1 & ~TT_SIDE);
	}
        else if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, TiGetRightType(tile)) != 0)
	{
	    t1 = TiGetRightType(tile);
	    TiSetBody(tile, t1 & TT_SIDE);
	}
	else
	{
	    TxError("Couldn't find device at %d %d\n",
			goodies->rg_devloc->p_x, goodies->rg_devloc->p_y);
	    return(NULL);
	}
    }
    else if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, TiGetType(tile)) == 0)
    {
	TxError("Couldn't find device at %d %d\n",
		goodies->rg_devloc->p_x, goodies->rg_devloc->p_y);
	return(NULL);
    }
    else
	t1 = TiGetType(tile);

    devptr = ExtCurStyle->exts_device[t1];

    /* left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	t2 = TiGetRightType(tp);
	if (TTMaskHasType(&(devptr->exts_deviceSDTypes[0]), t2))
	{
	    SourcePoint->p_x = LEFT(tile);
	    SourcePoint->p_y = (MIN(TOP(tile),TOP(tp)) +
		   			MAX(BOTTOM(tile), BOTTOM(tp))) >> 1;
	    return(tp);
	}
    }

    /* right */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
    {
	t2 = TiGetLeftType(tp);
	if (TTMaskHasType(&(devptr->exts_deviceSDTypes[0]), t2))
	{
	    SourcePoint->p_x = RIGHT(tile);
	    SourcePoint->p_y = (MIN(TOP(tile), TOP(tp))+
		   			MAX(BOTTOM(tile), BOTTOM(tp))) >> 1;
	    return(tp);
	}
    }

    /* top */
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	t2 = TiGetBottomType(tp);
	if (TTMaskHasType(&(devptr->exts_deviceSDTypes[0]), t2))
	{
	    SourcePoint->p_y = TOP(tile);
	    SourcePoint->p_x = (MIN(RIGHT(tile),RIGHT(tp)) +
		   			MAX(LEFT(tile), LEFT(tp))) >> 1;
	    return(tp);
	}
    }

    /* bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
    {
	t2 = TiGetTopType(tp);
	if (TTMaskHasType(&(devptr->exts_deviceSDTypes[0]), t2))
	{
	    SourcePoint->p_y = BOTTOM(tile);
	    SourcePoint->p_x = (MIN(RIGHT(tile), RIGHT(tp)) +
		   			MAX(LEFT(tile), LEFT(tp))) >> 1;
	    return(tp);
	}
    }
    return((Tile *) NULL);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResGetDevice -- Once the net is extracted, we still have to equate
 *	the sim file devices with the layout devices. ResGetDevice
 *	looks for a device at the given location.
 *
 * Results: returns device structure at location DevicePoint, if it
 *	exists.
 *
 * Side Effects: none
 *
 *-------------------------------------------------------------------------
 */

resDevice *
ResGetDevice(pt)
    Point	*pt;

{
    Point   workingPoint;
    Tile    *tile;
    int	    pnum;

    workingPoint.p_x = (*pt).p_x;
    workingPoint.p_y = (*pt).p_y;

    for (pnum = PL_TECHDEPBASE; pnum < DBNumPlanes; pnum++)
    {
     	if (TTMaskIntersect(&ExtCurStyle->exts_deviceMask, &DBPlaneTypes[pnum]) == 0)
	    continue;

	/* Start at hint tile for device plane */

	tile = ResUse->cu_def->cd_planes[pnum]->pl_hint;
	GOTOPOINT(tile, &workingPoint);

	if (IsSplit(tile))
	{
            if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, TiGetLeftType(tile))
              	   || TTMaskHasType(&ExtCurStyle->exts_deviceMask, TiGetRightType(tile)))
                return (((tileJunk *)tile->ti_client)->deviceList);
	}
	else if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, TiGetType(tile)))
        {
            return (((tileJunk *)tile->ti_client)->deviceList);
        }
    }
    return NULL;
}
