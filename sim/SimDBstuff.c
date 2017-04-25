
/* SimDBstuff.c -
 *
 *	This file contains routines that extract electrically connected
 *	regions of a layout for Magic.   This extractor operates 
 *	hierarchically, across cell boundaries (SimTreeCopyConnect), as
 *	well as within a single cell (SimSrConnect).
 *
 *	This also contains routines corresponding to those in the DBWind
 *	module.
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
 * University of California
 */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "textio/textio.h"
#include "utils/signals.h"
#include "utils/malloc.h"
#include "extract/extractInt.h"
#include "sim/sim.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "commands/commands.h"
#include "textio/txcommands.h"
#include "utils/styles.h"
#include "graphics/graphics.h"

/* The following structure is used to hold several pieces
 * of information that must be passed through multiple
 * levels of search function.
 */
	
struct conSrArg
{
    CellDef *csa_def;			/* Definition being searched. */
    Plane *csa_plane;			/* Current plane being searched. */
    TileTypeBitMask *csa_connect;	/* Table indicating what connects
					 * to what.
					 */
    int (*csa_clientFunc)();		/* Client function to call. */
    ClientData csa_clientData;		/* Argument for clientFunc. */
    bool csa_clear;			/* FALSE means pass 1, TRUE
					 * means pass 2.
					 */
    Rect csa_bounds;			/* Area that limits search. */
};

/* For SimTreeSrConnect, the extraction proceeds in one pass, copying
 * all connected stuff from a hierarchy into a single cell.  A list
 * is kept to record areas that still have to be searched for
 * hierarchical stuff.
 */

typedef struct
{
    Rect		area;		/* Area to process */
    TileTypeBitMask	*connectMask;	/* Connection mask for search */
    TileType		dinfo;		/* Info about triangular search areas */
} conSrArea;

struct conSrArg2
{
    CellUse		*csa2_use;	/* Destination use */
    TileTypeBitMask	*csa2_connect;	/* Table indicating what connects
					 * to what.
					 */
    Rect		*csa2_bounds;	/* Area that limits the search */

    conSrArea		*csa2_list;	/* List of areas to process */
    int			csa2_top;	/* Index of next area to process */
    int			csa2_size;	/* Max. number bins in area list */
};

#define CSA2_LIST_START_SIZE 256

/* Forward declarations */

extern char *DBPrintUseId();
extern int  dbcUnconnectFunc();
extern void SimInitScxStk();
extern void SimPopScx();
extern void SimMakePathname();

static char 		bestName[256];


/*
 * ----------------------------------------------------------------------------
 *
 *	SimConnectFunc
 *
 *	This procedure is based upon the function dbcConnectFunc in the
 *	database module.
 *
 * 	This procedure is invoked by SimTreeSrTiles from SimTreeCopyConnect,
 *	whenever a tile is found that is connected to the current area
 *	being processed.  If the tile overlaps the search area in a non-
 *	trivial way (i.e. more than a 1x1 square of overlap at a corner)
 *	then the area of the tile is added onto the list of things to check.
 *	The "non-trivial" overlap check is needed to prevent caddy-corner
 *	tiles from being considered as connected.
 *
 * Results:
 *	Returns 0 normally, 1 if an abort condition has been encountered.
 *
 * Side effects:
 *	Paints into the destination definition.
 *
 * ----------------------------------------------------------------------------
 */

int
SimConnectFunc(tile, cx)
    Tile *tile;			/* Tile found. */
    TreeContext *cx;		/* Describes context of search.  The client
				 * data is a pointer to the list head of
				 * the conSrArg2's describing the areas
				 * left to check.
				 */
{
    struct conSrArg2	*csa2;
    Rect 		tileArea, *srArea, newarea;
    SearchContext	*scx = cx->tc_scx;
    TileTypeBitMask	notConnectMask, *connectMask;
    TileType		loctype, ctype;
    TileType		dinfo = 0;
    int 		i, pNum;
    static char		nodeName[256];
    CellDef 		*def;
    TerminalPath	*tpath = cx->tc_filter->tf_tpath;

    TiToRect(tile, &tileArea);
    srArea = &scx->scx_area;

    if (((tileArea.r_xbot >= srArea->r_xtop-1) ||
        (tileArea.r_xtop <= srArea->r_xbot+1)) &&
	((tileArea.r_ybot >= srArea->r_ytop-1) ||
	(tileArea.r_ytop <= srArea->r_ybot+1)))
    {
	/* If the search area is only one unit wide or tall, then it's
	 * OK to have only a small overlap.  This happens only when
	 * looking for an initial search tile.
	 */

	if (((srArea->r_xtop-1) != srArea->r_xbot)
		&& ((srArea->r_ytop-1) != srArea->r_ybot)) return 0;
    }
    GeoTransRect(&scx->scx_trans, &tileArea, &newarea);

    /* Clip the current area down to something that overlaps the
     * area of interest.
     */
	
    csa2 = (struct conSrArg2 *)cx->tc_filter->tf_arg;
    GeoClip(&newarea, csa2->csa2_bounds);
    if (GEO_RECTNULL(&newarea)) return 0;

    /* Stuff unique to the nodename search follows. */

    if (tpath != (TerminalPath *)NULL)
    {
        /* Extract the node name */

	char *n = tpath->tp_next;
	char c = *n;

	SigDisableInterrupts();
	strcpy(nodeName, SimGetNodeName(cx->tc_scx, tile, tpath->tp_first)); 
	SigEnableInterrupts();

	*n = c;

	/* save the "best" name for this node */

	if (bestName[0] == '\0' || efPreferredName(nodeName, bestName))
	    strcpy(bestName, nodeName);
    }

    loctype = TiGetTypeExact(tile);

    /* Resolve geometric transformations on diagonally-split tiles */
   
    if (IsSplit(tile))
    {
	dinfo = DBTransformDiagonal(loctype, &scx->scx_trans);
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    }

    /* See if the destination cell contains stuff over the whole
     * current area (on its home plane) that is connected to it.
     * If so, then there's no need to process the current area,
     * since any processing that is needed was already done before.
     */

    pNum = DBPlane(loctype);
    connectMask = &csa2->csa2_connect[loctype];

    if (DBIsContact(loctype))
    {
	TileTypeBitMask *rMask = DBResidueMask(loctype);
	TileTypeBitMask *cMask;

	TTMaskSetOnlyType(&notConnectMask, loctype);

	/* Differenct contact types may share residues (6/18/04) */     
	for (ctype = TT_TECHDEPBASE; ctype < DBNumUserLayers; ctype++)
	{
	    if (DBIsContact(ctype))
	    {
		cMask = DBResidueMask(ctype);
		if (TTMaskIntersect(rMask, cMask))
		    TTMaskSetType(&notConnectMask, ctype);
	    }
	}

	/* The mask of contact types must include all stacked contacts */
	for (ctype = DBNumUserLayers; ctype < DBNumTypes; ctype++)   
	{
	    cMask = DBResidueMask(ctype);
	    if TTMaskHasType(cMask, loctype)
		TTMaskSetType(&notConnectMask, ctype);
	}

	TTMaskCom(&notConnectMask);
    }
    else
    {
	TTMaskCom2(&notConnectMask, connectMask);
    }

    def = csa2->csa2_use->cu_def;
    if (DBSrPaintNMArea((Tile *) NULL, def->cd_planes[pNum],
		dinfo, &newarea, &notConnectMask, dbcUnconnectFunc,
		(ClientData) connectMask) == 0)
	return 0;

    /* Paint this tile into the destination cell. */
	
    DBNMPaintPlane(def->cd_planes[pNum], dinfo, &newarea,
		DBStdPaintTbl(loctype, pNum), (PaintUndoInfo *) NULL);

    /* Since the whole area of this tile hasn't been recorded,
     * we must process its area to find any other tiles that
     * connect to it.  Add each of them to the list of things
     * to process.  We have to expand the search area by 1 unit
     * on all sides because SimTreeSrTiles only returns things
     * that overlap the search area, and we want things that
     * even just touch.
     */

    /* Only extend those sides bordering the diagonal tile */

    if (dinfo & TT_DIAGONAL)
    {
	if (dinfo & TT_SIDE)			/* right */
	    newarea.r_xtop += 1;
	else					/* left */
	    newarea.r_xbot -= 1;
	if (((dinfo & TT_SIDE) >> 1)
		== (dinfo & TT_DIRECTION))	/* top */
	    newarea.r_ytop += 1;
	else					/* bottom */
	    newarea.r_ybot -= 1;
    }
    else
    {
	newarea.r_xbot -= 1;
	newarea.r_ybot -= 1;
	newarea.r_xtop += 1;
	newarea.r_ytop += 1;
    }

    /* Abort the name search if the name is in the abort name search table 
     * or if the name is global and the SimIgnoreGlobals flag is not set.
     */

    if (SimSawAbortString  || SigInterruptPending)
	return 1;
    else if (SimIsGetnode && !SimIgnoreGlobals)
    {
	i = strlen(nodeName);
	if (nodeName[i - 1] == '!')
	    return 1;
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
	for (i = 0; i < lastsize; i++)
	{
	    newlist[i].area = csa2->csa2_list[i].area;
	    newlist[i].connectMask = csa2->csa2_list[i].connectMask;
	    newlist[i].dinfo = csa2->csa2_list[i].dinfo;
	}
	freeMagic((char *)csa2->csa2_list);
	csa2->csa2_list = newlist;
    }

    csa2->csa2_list[csa2->csa2_top].area = newarea;
    csa2->csa2_list[csa2->csa2_top].connectMask = connectMask;
    csa2->csa2_list[csa2->csa2_top].dinfo = dinfo;
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 *	SimTreeCopyConnect
 *
 *	This procedure is very similar to DBTreeCopyConnect.
 *
 * 	This procedure copies connected information from a given cell
 *	hierarchy to a given (flat) cell.  Starting from the tile underneath
 *	the given area, this procedure finds all paint in all cells
 *	that is connected to that information.  All such paint is
 *	copied into the result cell.  If there are several electrically
 *	distinct nets underneath the given area, one of them is picked
 *	at more-or-less random.
 *
 *	Modified so the result cell is NOT first cleared of all paint.  This
 *	allows multiple calls, to highlight incomplete routing nets.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The contents of the result cell are modified.
 *
 * ----------------------------------------------------------------------------
 */

#define MAXPATHNAME 256

void
SimTreeCopyConnect(scx, mask, xMask, connect, area, destUse, Node_Name)
    SearchContext *scx;			/* Describes starting area.  The
					 * scx_use field gives the root of
					 * the hierarchy to search, and the
					 * scx_area field gives the starting
					 * area.  An initial tile must overlap
					 * this area.  The transform is from
					 * coords of scx_use to destUse.
					 */
    TileTypeBitMask *mask;		/* Tile types to start from in area. */
    int xMask;				/* Information must be expanded in all
					 * of the windows indicated by this
					 * mask.  Use 0 to consider all info
					 * regardless of expansion.
					 */
    TileTypeBitMask *connect;		/* Points to table that defines what
					 * each tile type is considered to
					 * connect to.  Use DBConnectTbl as
					 * a default.
					 */
    Rect *area;				/* The resulting information is
					 * clipped to this area.  Pass
					 * TiPlaneRect to get everything.
					 */
    CellUse *destUse;			/* Result use in which to place
					 * anything connected to material of
					 * type mask in area of rootUse.
					 */
    char *Node_Name;			/* Name of node returned.
					 * NOTE:  Don't call this "NodeName",
					 * because that conflicts with reserved
					 * words in some compilers.
					 */
{
    TerminalPath tpath;
    char pathName[MAXPATHNAME];
    TileTypeBitMask *newmask;
    struct conSrArg2 csa2;
    TileType newtype;

    csa2.csa2_use = destUse;
    csa2.csa2_bounds = area;
    csa2.csa2_connect = connect;

    csa2.csa2_size = CSA2_LIST_START_SIZE;
    csa2.csa2_list = (conSrArea *)mallocMagic(CSA2_LIST_START_SIZE
			* sizeof(conSrArea));
    csa2.csa2_top = -1;

    tpath.tp_first = tpath.tp_next = pathName;
    tpath.tp_last = pathName + MAXPATHNAME; 

    pathName[0] = '\0';
    bestName[0] = '\0';

    (void) SimTreeSrTiles(scx, mask, xMask, &tpath, SimConnectFunc,
		(ClientData) &csa2);
    while (csa2.csa2_top >= 0)
    {
	newmask = csa2.csa2_list[csa2.csa2_top].connectMask;
	scx->scx_area = csa2.csa2_list[csa2.csa2_top].area;
	newtype = csa2.csa2_list[csa2.csa2_top].dinfo;
	csa2.csa2_top--;

	if (newtype & TT_DIAGONAL)
	    SimTreeSrNMTiles(scx, newtype, newmask, xMask, &tpath,
			SimConnectFunc, (ClientData) &csa2);
	else
	    SimTreeSrTiles(scx, newmask, xMask, &tpath, SimConnectFunc,
			(ClientData) &csa2);
    }
    freeMagic((char *)csa2.csa2_list);

    /* Recompute the bounding box of the destination and record
     * its area for redisplay.
     */
    
    strcpy(Node_Name, bestName);
    DBReComputeBbox(destUse->cu_def);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efPreferredName --
 *
 * This is the same function used in the ext2sim module.  We need this
 * function for the rsim interface to Magic.
 *
 * Determine which of two names is more preferred.  The most preferred
 * name is a global name.  Given two non-global names, the one with the
 * fewest pathname components is the most preferred.  If the two names
 * have equally many pathname components, we choose the shortest.
 *
 * Results:
 *	TRUE if 'name1' is preferable to 'name2', FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
efPreferredName(name1, name2)
    char *name1, *name2;
{
    int nslashes1, nslashes2;
    char *np1, *np2;

    if( name1[0] == '@' && name1[1] == '=' )
	return( TRUE );
    else if( name2[0] == '@' && name2[1] == '=' )
	return( FALSE );

    for (nslashes1 = 0, np1 = name1; *np1; ) {
	if (*np1++ == '/')
	    nslashes1++;
    }

    for (nslashes2 = 0, np2 = name2; *np2; ) {
	if (*np2++ == '/')
	    nslashes2++;
    }

    --np1;
    --np2;

    if (!SimIgnoreGlobals)
    {
	/* both are global names */
	if ((*np1 == '!') && (*np2 == '!')) {
	    /* check # of pathname components */
	    if (nslashes1 < nslashes2) return (TRUE);
	    if (nslashes1 > nslashes2) return (FALSE);

	    /* same # of pathname components; check length */
            if (np1 - name1 < np2 - name2) return (TRUE);
    	    if (np1 - name1 > np2 - name2) return (FALSE);

	    /* same # of pathname components; same length; use lex order */
	    if (strcmp(name1, name2) > 0) 
		return(TRUE);
	    else
		return(FALSE);
	}
	if (*np1 == '!') return(TRUE);
	if (*np2 == '!') return(FALSE);
    }

    /* neither name is global */
    /* chose label over generated name */
    if (*np1 != '#' && *np2 == '#') return (TRUE);
    if (*np1 == '#' && *np2 != '#') return (FALSE);

    /* either both are labels or generated names */
    /* check pathname components */
    if (nslashes1 < nslashes2) return (TRUE);
    if (nslashes1 > nslashes2) return (FALSE);

    /* same # of pathname components; check length */
    if (np1 - name1 < np2 - name2) return (TRUE);
    if (np1 - name1 > np2 - name2) return (FALSE);

    /* same # of pathname components; same length; use lex ordering */
    if (strcmp(name1, name2) > 0) 
	return(TRUE);
    else
	return(FALSE);
}



/*
 * ----------------------------------------------------------------------------
 *
 *	SimSrConnect
 *
 *	This is similar to the procedure DBSrConnect, except that the
 *	marks on each tile in the cell are not erased.
 *
 * 	Search through a cell to find all paint that is electrically
 *	connected to things in a given starting area.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	The search starts from one (random) non-space tile in "startArea"
 *	that matches the types in the mask parameter.  For every paint
 *	tile that is electrically connected to the initial tile and that
 *	intersects the rectangle "bounds", func is called.  Func should
 *	have the following form:
 *
 *	    int
 *	    func(tile, clientData)
 *		Tile *tile;
 *		ClientData clientData;
 *    	    {
 *	    }
 *
 *	The clientData passed to func is the same one that was passed
 *	to us.  Func returns 0 under normal conditions;  if it returns
 *	1 then the search is aborted.
 *
 *				*** WARNING ***
 *	
 *	Func should not modify any paint during the search, since this
 *	will mess up pointers kept by these procedures and likely cause
 *	a core-dump.  
 *
 * ----------------------------------------------------------------------------
 */

int
SimSrConnect(def, startArea, mask, connect, bounds, func, clientData)
    CellDef *def;		/* Cell definition in which to carry out
				 * the connectivity search.  Only paint
				 * in this definition is considered.
				 */
    Rect *startArea;		/* Area to search for an initial tile.  Only
				 * tiles OVERLAPPING the area are considered.
				 * This area should have positive x and y
				 * dimensions.
				 */
    TileTypeBitMask *mask;	/* Only tiles of one of these types are used
				 * as initial tiles.
				 */
    TileTypeBitMask *connect;	/* Pointer to a table indicating what tile
				 * types connect to what other tile types.
				 * Each entry gives a mask of types that
				 * connect to tiles of a given type.
				 */
    Rect *bounds;		/* Area, in coords of scx->scx_use->cu_def,
				 * that limits the search:  only tiles
				 * overalapping this area will be returned.
				 * Use TiPlaneRect to search everywhere.
				 */
    int (*func)();		/* Function to apply at each connected tile. */
    ClientData clientData;	/* Client data for above function. */

{
    struct conSrArg csa;
    int startPlane, result;
    Tile *startTile;			/* Starting tile for search. */
    extern int dbSrConnectFunc();	/* Forward declaration. */
    extern int dbSrConnectStartFunc();

    result = 0;
    csa.csa_def = def;
    csa.csa_bounds = *bounds;

    /* Find a starting tile (if there are many tiles underneath the
     * starting area, pick any one).  The search function just saves
     * the tile address and returns.
     */

    startTile = NULL;
    for (startPlane = PL_TECHDEPBASE; startPlane < DBNumPlanes; startPlane++)
    {
	if (DBSrPaintArea((Tile *) NULL,
	    def->cd_planes[startPlane], startArea, mask,
	    dbSrConnectStartFunc, (ClientData) &startTile) != 0) break;
    }
    if (startTile == NULL) return 0;

    /* Pass 1.  During this pass the client function gets called. */

    csa.csa_clientFunc = func;
    csa.csa_clientData = clientData;
    csa.csa_clear = FALSE;
    csa.csa_connect = connect;
    csa.csa_plane = def->cd_planes[startPlane];
    if (dbSrConnectFunc(startTile, &csa) != 0) result = 1;

    return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * SimTreeSrTiles
 *
 * Similar to the procedure DBTreeSrTiles, although having a terminal
 * path similar to procedure DBTreeSrLabels.
 *
 * Recursively search downward from the supplied CellUse for
 * all visible paint tiles matching the supplied type mask.
 *
 * The procedure should be of the following form:
 *	int
 *	func(tile, cxp)
 *	    Tile *tile;
 *	    TreeContext *cxp;
 *	{
 *	}
 *
 * The SearchContext is stored in cxp->tc_scx, the user's arg is stored
 * in cxp->tc_filter->tf_arg, and the terminal path is stored in
 * cxp->tc_filter->tf_tpath.
 *
 * In the above, the scx transform is the net transform from the coordinates
 * of tile to "world" coordinates (or whatever coordinates the initial
 * transform supplied to SimTreeSrTiles was a transform to).  Func returns
 * 0 under normal conditions.  If 1 is returned, it is a request to
 * abort the search.
 *
 *			*** WARNING ***
 *
 * The client procedure should not modify any of the paint planes in
 * the cells visited by SimTreeSrTiles, because we use DBSrPaintArea
 * instead of TiSrArea as our paint-tile enumeration function.
 *
 * Results:
 *	0 is returned if the search finished normally.  1 is returned
 *	if the search was aborted.
 *
 * Side effects:
 *	Whatever side effects are brought about by applying the
 *	procedure supplied.
 *
 *-----------------------------------------------------------------------------
 */

int
SimTreeSrTiles(scx, mask, xMask, tpath, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    TileTypeBitMask *mask;	/* Only tiles with a type for which
				 * a bit in this mask is on are processed.
				 */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    TerminalPath *tpath;	/* Pointer to a structure describing a
				 * partially filled-in terminal pathname.
				 * Add new components as encountered.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    int SimCellTileSrFunc();
    TreeFilter filter;

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_planes = DBTechTypesToPlanes(mask);
    filter.tf_tpath = tpath;
    filter.tf_dinfo = 0;

    return SimCellTileSrFunc(scx, &filter);
}

/*
 * SimTreeSrNMTiles ---
 *	This is a variant of the above in which the search is over
 *	a non-Manhattan triangular area.
 */

int
SimTreeSrNMTiles(scx, dinfo, mask, xMask, tpath, func, cdarg)
    SearchContext *scx;		/* Pointer to search context specifying
				 * a cell use to search, an area in the
				 * coordinates of the cell's def, and a
				 * transform back to "root" coordinates.
				 */
    TileType dinfo;		/* Type containing information about the
				 * triangular area to search.
				 */
    TileTypeBitMask *mask;	/* Only tiles with a type for which
				 * a bit in this mask is on are processed.
				 */
    int xMask;			/* All subcells are visited recursively
				 * until we encounter uses whose flags,
				 * when anded with xMask, are not
				 * equal to xMask.
				 */
    TerminalPath *tpath;	/* Pointer to a structure describing a
				 * partially filled-in terminal pathname.
				 * Add new components as encountered.
				 */
    int (*func)();		/* Function to apply at each qualifying tile */
    ClientData cdarg;		/* Client data for above function */
{
    int SimCellTileSrFunc();
    TreeFilter filter;

    filter.tf_func = func;
    filter.tf_arg = cdarg;
    filter.tf_mask = mask;
    filter.tf_xmask = xMask;
    filter.tf_dinfo = dinfo;
    filter.tf_planes = DBTechTypesToPlanes(mask);
    filter.tf_tpath = tpath;

    return SimCellTileSrFunc(scx, &filter);
}

/*
 * Filter procedure applied to subcells by SimTreeSrTiles().
 */

int
SimCellTileSrFunc(scx, fp)
    SearchContext *scx;
    TreeFilter *fp;
{
    TreeContext context;
    TerminalPath *tp;
    CellDef *def = scx->scx_use->cu_def;
    int pNum, result;
    char *tnext;

    ASSERT(def != (CellDef *) NULL, "SimCellTileSrFunc");
    if (!DBDescendSubcell(scx->scx_use, fp->tf_xmask))
	return 0;
    if ((def->cd_flags & CDAVAILABLE) == 0)
	if (!DBCellRead(def, (char *) NULL, TRUE, NULL)) return 0;

    context.tc_scx = scx;
    context.tc_filter = fp;

    /* Create the path prefix */
    /* Don't prepend the "Topmost cell" ID of the top-level cell. */

    if ((fp->tf_tpath != (TerminalPath *)NULL)
		&& (scx->scx_use->cu_parent != NULL))
    {
	tp = fp->tf_tpath;
	tnext = tp->tp_next;
	tp->tp_next = DBPrintUseId(scx, tp->tp_next, tp->tp_last -
		tp->tp_next, FALSE);
	if (tp->tp_next < tp->tp_last)
	{
	    *(tp->tp_next++) = '/';
	    *(tp->tp_next) = '\0';
	}
    }

    /*
     * Apply the function first to any of the tiles in the planes
     * for this CellUse's CellDef that match the mask.
     */

    result = 0;
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(fp->tf_planes, pNum))
	{
	    if (fp->tf_dinfo & TT_DIAGONAL)
	    {
		TileType dinfo = DBTransformDiagonal(fp->tf_dinfo, &scx->scx_trans);
		if (DBSrPaintNMArea((Tile *) NULL, def->cd_planes[pNum],
			dinfo, &scx->scx_area, fp->tf_mask,
			fp->tf_func, (ClientData) &context))
		{
		    result = 1;
		    goto cleanup;
		}
	    }
	    else
		if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
			&scx->scx_area, fp->tf_mask,
			fp->tf_func, (ClientData) &context))
		{
		    result = 1;
		    goto cleanup;
		}
	}

    /*
     * Now apply ourselves recursively to each of the CellUses
     * in our tile plane.
     */

    if (DBCellSrArea(scx, SimCellTileSrFunc, (ClientData) fp))
	result = 1;

cleanup:
    /* Remove the trailing pathname component from the TerminalPath */
    if ((fp->tf_tpath != (TerminalPath *)NULL)
		&& (scx->scx_use->cu_parent != NULL))
    {
	fp->tf_tpath->tp_next = tnext;
	*tnext = '\0';
    }
    return (result);
}


/*
 * ----------------------------------------------------------------------------
 *
 * SimPutLabel --
 *
 * Same as DBPutLabel, except this does not set the cell modified flag.
 *
 * Place a rectangular label in the database, in a particular cell.
 *
 * It is the responsibility of higher-level routines to insure that
 * the material to which the label is being attached really exists at
 * this point in the cell, and that TT_SPACE is used if there is
 * no single material covering the label's entire area.  The routine
 * DBAdjustLabels is useful for this.
 *
 * Results:
 *	The return value is the actual alignment position used for
 *	the label.  This may be different from align, if align is
 *	defaulted.
 *
 * Side effects:
 *	Updates the label list in the CellDef to contain the label.
 *
 * ----------------------------------------------------------------------------
 */

int
SimPutLabel(cellDef, rect, align, text, type)
    CellDef *cellDef;	/* Cell in which label is placed */
    Rect *rect;		/* Location of label; see above for description */
    int align;		/* Orientation/alignment of text.  If this is < 0,
			 * an orientation will be picked to keep the text
			 * inside the cell boundary.
			 */
    char *text;		/* Pointer to actual text of label */
    TileType type;	/* Type of tile to be labeled */
{
    Label *lab;
    int len, x1, x2, y1, y2, tmp, labx, laby;

    len = strlen(text) + sizeof (Label) - sizeof lab->lab_text + 1;
    lab = (Label *) mallocMagic((unsigned) len);
    strcpy(lab->lab_text, text);

    /* Pick a nice alignment if the caller didn't give one.  If the
     * label is more than BORDER units from an edge of the cell,
     * use GEO_NORTH.  Otherwise, put the label on the opposite side
     * from the boundary, so it won't stick out past the edge of
     * the cell boundary.
     */
    
#define BORDER 5
    if (align < 0)
    {
	tmp = (cellDef->cd_bbox.r_xtop - cellDef->cd_bbox.r_xbot)/3;
	if (tmp > BORDER) tmp = BORDER;
	x1 = cellDef->cd_bbox.r_xbot + tmp;
	x2 = cellDef->cd_bbox.r_xtop - tmp;
	tmp = (cellDef->cd_bbox.r_ytop - cellDef->cd_bbox.r_ybot)/3;
	if (tmp > BORDER) tmp = BORDER;
	y1 = cellDef->cd_bbox.r_ybot + tmp;
	y2 = cellDef->cd_bbox.r_ytop - tmp;
	labx = (rect->r_xtop + rect->r_xbot)/2;
	laby = (rect->r_ytop + rect->r_ybot)/2;

	if (labx <= x1)
	{
	    if (laby <= y1) align = GEO_NORTHEAST;
	    else if (laby >= y2) align = GEO_SOUTHEAST;
	    else align = GEO_EAST;
	}
	else if (labx >= x2)
	{
	    if (laby <= y1) align = GEO_NORTHWEST;
	    else if (laby >= y2) align = GEO_SOUTHWEST;
	    else align = GEO_WEST;
	}
	else
	{
	    if (laby <= y1) align = GEO_NORTH;
	    else if (laby >= y2) align = GEO_SOUTH;
	    else align = GEO_NORTH;
	}
    }

    lab->lab_just = align;
    lab->lab_type = type;
    lab->lab_rect = *rect;
    lab->lab_next = NULL;
    lab->lab_flags = 0;
    if (cellDef->cd_labels == NULL)
	cellDef->cd_labels = lab;
    else
    {
	ASSERT(cellDef->cd_lastLabel->lab_next == NULL, "SimPutLabel");
	cellDef->cd_lastLabel->lab_next = lab;
    }
    cellDef->cd_lastLabel = lab;

    DBUndoPutLabel(cellDef, lab);
    return align;
}


#ifdef RSIM_MODULE

/*
 * ----------------------------------------------------------------------------
 *
 * SimRsimHandler
 *
 * 	This procedure is the button handler for the rsim tool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Left button:  used to move the whole box by the lower-left corner.
 *	Right button: used to re-size the box by its upper-right corner.
 *		If one of the left or right buttons is pushed, then the
 *		other is pushed, the corner is switched to the nearest
 *		one to the cursor.  This corner is remembered for use
 *		in box positioning/sizing when both buttons have gone up.
 *	Middle button: used to display the rsim node values of whatever
 *		paint is selected.
 *
 * ----------------------------------------------------------------------------
 */

void
SimRsimHandler(w, cmd)
    MagWindow *w;			/* Window containing cursor. */
    TxCommand *cmd;		/* Describes what happened. */
{

    static int buttonCorner = TOOL_ILG;
    int button = cmd->tx_button;

    if (button == TX_MIDDLE_BUTTON)
    {
	if (cmd->tx_buttonAction == TX_BUTTON_DOWN)
	    SimRsimMouse(w);
	return;
    }

    if (cmd->tx_buttonAction == TX_BUTTON_DOWN)
    {
	if ((WindNewButtons & (TX_LEFT_BUTTON|TX_RIGHT_BUTTON))
		== (TX_LEFT_BUTTON|TX_RIGHT_BUTTON))
	{
	    /* Both buttons are now down.  In this case, the FIRST
	     * button pressed determines whether we move or size,
	     * and the second button is just used as a signal to pick
	     * the closest corner.
	     */

	    buttonCorner = ToolGetCorner(&cmd->tx_p);
	    if (button == TX_LEFT_BUTTON) button = TX_RIGHT_BUTTON;
	    else button = TX_LEFT_BUTTON;
	}
	else if (button == TX_LEFT_BUTTON) buttonCorner = TOOL_BL;
	else buttonCorner = TOOL_TR;
	dbwButtonSetCursor(button, buttonCorner);
    }
    else
    {
	/* A button has just come up.  If both buttons are down and one
	 * is released, we just change the cursor to reflect the current
	 * corner and the remaining button (i.e. move or size box).
	 */

	if (WindNewButtons != 0)
	{
	    if (button == TX_LEFT_BUTTON)
		dbwButtonSetCursor(TX_RIGHT_BUTTON, buttonCorner);
	    else dbwButtonSetCursor(TX_LEFT_BUTTON, buttonCorner);
	    return;
	}

	/* The last button has been released.  Reset the cursor to normal
	 * form and then move or size the box.
	 */

	GrSetCursor(STYLE_CURS_RSIM);
	switch (button)
	{
	    case TX_LEFT_BUTTON:
		ToolMoveBox(buttonCorner, &cmd->tx_p, TRUE, (CellDef *) NULL);
		break;
	    case TX_RIGHT_BUTTON:
		ToolMoveCorner(buttonCorner, &cmd->tx_p, TRUE,
			(CellDef *) NULL);
	}
    }
}

#endif
