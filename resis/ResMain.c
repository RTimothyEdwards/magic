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
extern ExtRegion 	*ResFirst();
extern Tile		*FindStartTile();
extern int		ResEachTile();
extern int		ResLaplaceTile();
extern ResSimNode	*ResInitializeNode();
TileTypeBitMask		ResSDTypesBitMask;
TileTypeBitMask		ResSubTypesBitMask;

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
 *    constituent layers.  There should be no contacts in ResDef after
 *    this procedure runs.
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

    for (; contacts != (ResContactPoint *)NULL; contacts = contacts->cp_nextcontact)
    {
        oldtype=contacts->cp_type;

#ifdef PARANOID
	if (oldtype == TT_SPACE)
	    TxError("Error in Contact Dissolving for %s \n",ResCurrentNode);
#endif
	DBFullResidueMask(oldtype, &residues);

	DBErase(ResUse->cu_def, &(contacts->cp_rect), oldtype);
	for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    if (TTMaskHasType(&residues, t))
		DBPaint(ResUse->cu_def, &(contacts->cp_rect), t);

	tp = ResDef->cd_planes[DBPlane(contacts->cp_type)]->pl_hint;
	GOTOPOINT(tp, &(contacts->cp_rect.r_ll));

#ifdef PARANOID
	if (TiGetTypeExact(tp) == contacts->cp_type)
	    TxError("Error in Contact Preprocess Routines\n");
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
	node = (ResSimNode *)HashGetValue(entry);
	if (node->status & PORTNODE)
	{
	    if (node->rs_ttype <= 0)
	    {
		TxError("Warning:  Label \"%s\" is unconnected.\n", node->name);
		continue;
	    }

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

	    /* If label is on a contact, the contact has been dissolved. */
	    /* Assume that the uppermost residue is the port.  This may	 */
	    /* not necessarily be the case.  Could do a boundary scan on */
	    /* each residue plane to see which side of the contact is	 */
	    /* the internal connection in the def. . .			 */

	    if (DBIsContact(node->rs_ttype))
	    {
		TileType type;

		DBFullResidueMask(node->rs_ttype, &mask);
		for (type = DBNumUserLayers - 1; type >= TT_TECHDEPBASE; type--)
		    if (TTMaskHasType(&mask, type))
		    {
			plane = def->cd_planes[DBPlane(type)];
			break;
		    }
	    }
	    else
	    {
		TTMaskSetOnlyType(&mask, node->rs_ttype);
		plane = def->cd_planes[DBPlane(node->rs_ttype)];
	    }

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
ResMakeLabelBreakpoints(def, goodies)
    CellDef *def;
    ResGlobalParams     *goodies;
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
	/* Avoid any empty-string labels, or it will end up as	*/
	/* missing terminal on a device.			*/
	if (*(slab->lab_text) == '\0') continue;

	entry = HashFind(&ResNodeTable, slab->lab_text);
	node = ResInitializeNode(entry);

	/* If the drivepoint position changes and the drivepoint is */
	/* in the "goodies" record, then make sure the tile type in */
	/* "goodies" gets changed to match.			    */

	if (goodies->rg_devloc == &node->drivepoint)
	    goodies->rg_ttype = slab->lab_type;

        node->drivepoint = slab->lab_rect.r_ll;
        node->rs_bbox = slab->lab_rect;
        node->location = slab->lab_rect.r_ll;
        node->rs_ttype = slab->lab_type;
        node->type = slab->lab_type;

	rect = &(node->rs_bbox);

	/* If label is on a contact, the contact has been dissolved.	*/
	/* Assume that the uppermost residue is the port.  This may	*/
	/* not necessarily be the case.  Could do a boundary scan on	*/
	/* each residue plane to see which side of the contact is	*/
	/* the internal connection in the def. . .			*/

	if (DBIsContact(slab->lab_type))
	{
	    TileType type;

	    DBFullResidueMask(slab->lab_type, &mask);
	    for (type = DBNumUserLayers - 1; type >= TT_TECHDEPBASE; type--)
		if (TTMaskHasType(&mask, type))
		{
		    plane = def->cd_planes[DBPlane(type)];
		    break;
		}
	}
	else
	{
		TTMaskSetOnlyType(&mask, slab->lab_type);
		plane = def->cd_planes[DBPlane(slab->lab_type)];
	}

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

	/* Watch for types that connect to the substrate plane or well;	*/
	/* e.g., psubstratepdiff connects to nwell but not through a	*/
	/* contact.							*/

	if (ExtCurStyle->exts_globSubstratePlane != -1)
	{
	    TileTypeBitMask cMask;
	    TTMaskAndMask3(&cMask, &DBConnectTbl[contacts->cp_type],
		&DBPlaneTypes[ExtCurStyle->exts_globSubstratePlane]);

	    if (!TTMaskIsZero(&cMask))
		TTMaskSetMask(&mask, &cMask);
	}
	
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
	if (contacts->cp_currentcontact >= LAYERS_PER_CONTACT)
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
			merged |= (*tilefunc)(tile, (Point *)NULL);
		    }
		    if (merged & ORIGIN) break;
		}
		if (merged & ORIGIN) break;
		rj->rj_status = TRUE;
	    }
	}

	/* Next, Process all contacts. */

	for (workingc = resptr2->rn_ce; workingc != NULL; workingc = workingc->ce_nextc)
	{
	    ResContactPoint *cp = workingc->ce_thisc;

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
    dev->overlap += overlap;
}
/*
 *-------------------------------------------------------------------------
 *
 * resMakeDevFunc --
 *
 *  Callback function from ResExtractNet.  For each device in a node's
 *  device list pulled from the .sim file, find the tile(s) corresponding
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
    TileType	ttype;

    TiToRect(tile, &devArea);
    GeoTransRect(&cx->tc_scx->scx_trans, &devArea, &thisDev->area);

    if (IsSplit(tile))
	ttype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    else
	ttype = TiGetType(tile);

    /* If more than one tile type extracts to the same device, then */
    /* the device type may be different from what was recorded when */
    /* the sim file was read.  Restricted to the plane of the	    */
    /* original type to avoid conflict with completely different    */
    /* devices (like transistors vs. MiM caps).			    */

    if (ttype != thisDev->type)
    {
	if (DBPlane(ttype) != DBPlane(thisDev->type))
	    return 0;	/* Completely different device? */
	thisDev->type = ttype;
    }

    return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * resExpandDevFunc --
 *
 *	Do a boundary search on the first tile found in the search context
 *	path belonging to a device, including all tiles that belong to the
 *	device.  For each compatible tile found, paint the device tile
 *	type into ResUse and calculate the overlap.
 *
 * Returns:
 *	1 to stop the search (only the first tile of a device needs to be
 *	found).
 *
 * Side effects:
 *	Paints into ResUse and recalculates values of thisDev.
 *-------------------------------------------------------------------------
 */

#define DEV_PROCESSED 1

int
resExpandDevFunc(tile, cx)
    Tile	*tile;
    TreeContext *cx;
{
    ResDevTile	    *thisDev = (ResDevTile *)cx->tc_filter->tf_arg;
    static Stack    *devExtentsStack = NULL;
    static Stack    *devResetStack = NULL;
    TileTypeBitMask *rMask;
    Tile *tp, *tp2;
    TileType	ttype;
    int pNum;
    Rect area;

    pNum = DBPlane(thisDev->type);
    if (devExtentsStack == NULL)
	devExtentsStack = StackNew(8);
    if (devResetStack == NULL)
	devResetStack = StackNew(8);

    tile->ti_client = (ClientData)DEV_PROCESSED;
    STACKPUSH((ClientData)tile, devExtentsStack);

    while (!StackEmpty(devExtentsStack))
    {
	tp = (Tile *) STACKPOP(devExtentsStack);
	STACKPUSH((ClientData)tp, devResetStack);
	TiToRect(tp, &area);

	/* Paint type thisDev->type into ResUse over area of tile "tp" */
	DBNMPaintPlane(ResUse->cu_def->cd_planes[pNum], TiGetTypeExact(tp),
		&area, DBStdPaintTbl(thisDev->type, pNum), (PaintUndoInfo *)NULL);

	/* Add source/drain perimeter overlap to the device for this tile */
	ResCalcPerimOverlap(tp, thisDev);

	/* Search boundary of the device tile for more tiles belonging  */
	/* to the device.  If contacts are found, replace them with the */
	/* device type.							*/

	/* top */
	for (tp2 = RT(tp); RIGHT(tp2) > LEFT(tp); tp2 = BL(tp2))
	{
	    if (tp2->ti_client == (ClientData)DEV_PROCESSED) continue;
	    ttype = TiGetBottomType(tp2);
	    if ((ttype == thisDev->type) || (DBIsContact(ttype)
		&& TTMaskHasType(DBResidueMask(ttype), thisDev->type)))
	    {
		tp2->ti_client = (ClientData)DEV_PROCESSED;
		STACKPUSH((ClientData)tp2, devExtentsStack);
	    }
	}

	/* bottom */
	for (tp2 = LB(tp); LEFT(tp2) < RIGHT(tp); tp2 = TR(tp2))
	{
	    if (tp2->ti_client == (ClientData)DEV_PROCESSED) continue;
	    ttype = TiGetTopType(tp2);
	    if ((ttype == thisDev->type) || (DBIsContact(ttype)
		&& TTMaskHasType(DBResidueMask(ttype), thisDev->type)))
	    {
		tp2->ti_client = (ClientData)DEV_PROCESSED;
		STACKPUSH((ClientData)tp2, devExtentsStack);
	    }
	}

	/* right */
	for (tp2 = TR(tp); TOP(tp2) > BOTTOM(tp); tp2 = LB(tp2))
	{
	    if (tp2->ti_client == (ClientData)DEV_PROCESSED) continue;
	    ttype = TiGetLeftType(tp2);
	    if ((ttype == thisDev->type) || (DBIsContact(ttype)
		&& TTMaskHasType(DBResidueMask(ttype), thisDev->type)))
	    {
		tp2->ti_client = (ClientData)DEV_PROCESSED;
		STACKPUSH((ClientData)tp2, devExtentsStack);
	    }
	}

	/* left */
	for (tp2 = BL(tp); BOTTOM(tp2) < TOP(tp); tp2 = RT(tp2))
	{
	    if (tp2->ti_client == (ClientData)DEV_PROCESSED) continue;
	    ttype = TiGetRightType(tp2);
	    if ((ttype == thisDev->type) || (DBIsContact(ttype)
		&& TTMaskHasType(DBResidueMask(ttype), thisDev->type)))
	    {
		tp2->ti_client = (ClientData)DEV_PROCESSED;
		STACKPUSH((ClientData)tp2, devExtentsStack);
	    }
	}
    }

    /* Reset the device tile client records */
    while (!StackEmpty(devResetStack))
    {
	tp = (Tile *) STACKPOP(devResetStack);
	tp->ti_client = (ClientData)CLIENTDEFAULT;
    }

    /* Return 1 to stop the search;  we only need to run this from  */
    /* the first device tile.					    */

    return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResShaveContacts ---
 *
 *	Remove the top layer off of every contact in the design, leaving
 *	only the bottom layer.  This also resolves issues with stacked
 *	contacts by leaving clean contact areas where stacked types
 *	overlap.  Contacts are removed from the plane above the search
 *	plane, so the removal does not corrupt the current plane search.
 *
 * Results:
 *	Return 0 to keep the search going.
 *
 *-------------------------------------------------------------------------
 */

int
ResShaveContacts(tile, def)
    Tile *tile;
    CellDef *def;
{
    TileType ttype;
    TileTypeBitMask *rmask;
    Rect area;
    Plane *plane;
    int pNum;
    int pMask;

    /* To do:  Handle split tiles, although this is unlikely for
     * contact types.
     */
    ttype = TiGetType(tile);

    if (DBIsContact(ttype))
    {
	/* Remove the contact type from the plane above */
	TiToRect(tile, &area);
	pMask = DBTypePlaneMaskTbl[ttype];
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pMask, pNum))
		break;

	for (++pNum; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pMask, pNum))
	    {
		plane = def->cd_planes[pNum];
		DBPaintPlane(plane, &area, DBStdEraseTbl(ttype, pNum),
			(PaintUndoInfo *)NULL);
	    }
    }

    return 0;
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
    TileTypeBitMask	FirstTileMask;
    TileTypeBitMask	tMask;
    Point		startpoint;
    static int		first = 1;
    ResDevTile		*DevTiles, *thisDev;
    ResFixPoint		*fix;
    devPtr		*tptr;
    int			pNum;
    int			resMakeDevFunc();
    int			resExpandDevFunc();

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

    TTMaskZero(&ResSDTypesBitMask);
    TTMaskZero(&ResSubTypesBitMask);

    /* Add devices to ResUse from list in node */
    DevTiles = NULL;
    for (tptr = node->firstDev; tptr; tptr = tptr->nextDev)
    {
	int result;
	int i;
	ExtDevice *devptr;

	thisDev = (ResDevTile *)mallocMagic(sizeof(ResDevTile));
	thisDev->devptr = tptr->thisDev->rs_devptr;
	thisDev->type = tptr->thisDev->rs_ttype;
	thisDev->overlap = 0;
	scx.scx_area.r_ll.p_x = tptr->thisDev->location.p_x;
	scx.scx_area.r_ll.p_y = tptr->thisDev->location.p_y;
	scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
	scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
	result = DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0,
		    resMakeDevFunc, (ClientData)thisDev);
	if (result == 0)
	{
	    freeMagic(thisDev);
	    TxError("No device of type %s found at location %d,%d\n",
		    DBTypeLongNameTbl[thisDev->type],
		    tptr->thisDev->location.p_x,
		    tptr->thisDev->location.p_y);
	    continue;
	}
	thisDev->nextDev = DevTiles;
	DevTiles = thisDev;

	/* Paint the entire device into ResUse */
	TTMaskSetOnlyType(&tMask, thisDev->type);
	DBTreeSrTiles(&scx, &tMask, 0, resExpandDevFunc, (ClientData)thisDev);

	/* If the device has source/drain types in a different plane than   */
	/* the device identifier type, then add the source/drain types to   */
	/* the mask ResSDTypesBitMask.					    */

	devptr = tptr->thisDev->rs_devptr;
	for (i = 0; !TTMaskIsZero(&devptr->exts_deviceSDTypes[i]); i++)
	    TTMaskSetMask(&ResSDTypesBitMask, &devptr->exts_deviceSDTypes[i]);

	/* Add the substrate types to the mask ResSubTypesBitMask	    */
	TTMaskSetMask(&ResSubTypesBitMask, &devptr->exts_deviceSubstrateTypes);

	/* TT_SPACE should be removed from ResSubTypesBitMask */
	TTMaskClearType(&ResSubTypesBitMask, TT_SPACE);
    }
    DBReComputeBbox(ResUse->cu_def);

    ExtResetTiles(scx.scx_use->cu_def, extUnInit);

    /* To avoid issues with overlapping stacked contact types and	*/
    /* double-counting contacts on multiple planes, erase the top	*/
    /* contact layers of all contacts.  ExtFindRegions() will still	*/
    /* find the connectivity above but will only process one tile per	*/
    /* contact.	  This temporarily creates an improper database, but	*/
    /* the contacts are all immediately erased by ResDissolveContacts().*/

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	Plane *plane = ResUse->cu_def->cd_planes[pNum];
	DBSrPaintArea(plane->pl_hint, plane, &(ResUse->cu_def->cd_bbox),
		&DBAllButSpaceAndDRCBits, ResShaveContacts,
		(ClientData)ResUse->cu_def);
    }

    /* Find all contacts in design and note their position */

    /* NOTE:  ExtFindRegions() will call ResFirst or ResEach for BOTH	*/
    /* planes of a contact.  Rather than attempting to limit the	*/
    /* search, ResDoContacts() will just double the resistance per via	*/
    /* so that the final value is correct.				*/

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
    ResMakeLabelBreakpoints(ResUse->cu_def, goodies);
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
 * ResGetTileFunc --
 *
 *	Callback function used by FindStartTile() when searching for
 *	terminal connections of a device that may be on planes other
 *	than the plane of the device identifier type.  Ignore space
 *	tiles.  Otherwise, for any tile found, record the tile in
 *	the client data record and return 1 to stop the search.
 *
 * Results:
 *	Return 0 if tile is a space tile, to keep the search going.
 *	Return 1 otherwise to stop the search immediately because
 *	a valid start tile has been found.
 *
 *-------------------------------------------------------------------------
 */

int
ResGetTileFunc(tile, tpptr)
    Tile *tile, **tpptr;
{
    if (TiGetType(tile) != TT_SPACE)
    {
	*tpptr = tile;
	return 1;
    }
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * FindStartTile-- To start the extraction, we need to find the first driver.
 *	The sim file gives us the location of a point in or near (within 1
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
    int		pnum, t1, t2, i;
    ExtDevice   *devptr;
    Rect	r;

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

    /* NOTE: There must be a way to pass the device type from a device
     * record's rs_devptr instead of groping around for it.
     */

    for (devptr = ExtCurStyle->exts_device[t1]; devptr; devptr = devptr->exts_next)
    {
	for (i = 0; i < devptr->exts_deviceSDCount; i++)
	{
	    /* left */
	    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	    {
		t2 = TiGetRightType(tp);
		if ((t2 != TT_SPACE) &&
			TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t2))
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
		if ((t2 != TT_SPACE) &&
			TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t2))
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
		if ((t2 != TT_SPACE) &&
			TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t2))
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
		if ((t2 != TT_SPACE) &&
			TTMaskHasType(&(devptr->exts_deviceSDTypes[i]), t2))
		{
		    SourcePoint->p_y = BOTTOM(tile);
		    SourcePoint->p_x = (MIN(RIGHT(tile), RIGHT(tp)) +
		   			MAX(LEFT(tile), LEFT(tp))) >> 1;
		    return(tp);
		}
	    }
	}

	/* Didn't find a terminal (S/D) type tile in the perimeter search.	*/
	/* Check if S/D types are in a different plane from the identifier.	*/

	TiToRect(tile, &r);
	tp = NULL;
	for (i = 0; i < devptr->exts_deviceSDCount; i++)
	{
	    for (pnum = 0; pnum < DBNumPlanes; pnum++)
	    {
		DBSrPaintArea((Tile *)NULL, ResUse->cu_def->cd_planes[pnum],
			&r, &(devptr->exts_deviceSDTypes[i]), ResGetTileFunc, &tp);
		if (tp != NULL) return tp;
	    }
	}
    }

    /* Didn't find a terminal (S/D) type tile anywhere.  Flag an error. */

    TxError("Couldn't find a terminal of the device at %d %d\n",
			goodies->rg_devloc->p_x, goodies->rg_devloc->p_y);
    return((Tile *) NULL);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResGetDevice -- Once the net is extracted, we still have to equate
 *	the sim file devices with the layout devices. ResGetDevice
 *	looks for a device at the given location.  "type" is also
 *	specified to that the right plane will be searched.
 *
 * Results: returns device structure at location DevicePoint, if it
 *	exists.
 *
 * Side Effects: none
 *
 *-------------------------------------------------------------------------
 */

resDevice *
ResGetDevice(pt, type)
    Point	*pt;
    TileType	type;
{
    Point   workingPoint;
    Tile    *tile;
    int	    pnum;

    workingPoint.p_x = (*pt).p_x;
    workingPoint.p_y = (*pt).p_y;

    pnum = DBPlane(type);

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
        return (((tileJunk *)tile->ti_client)->deviceList);

    return NULL;
}
