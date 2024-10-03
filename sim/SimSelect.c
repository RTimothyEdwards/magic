/* SimSelect.c -
 *
 *	This file provides routines to make selections for Rsim by copying
 *	things into a special cell named "__SELECT__".  It is based
 *	on code in the select module.
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
 * of California
 */

#ifndef lint
static char sccsid[] = "@(#)SimSelect.c	4.14 MAGIC (Berkeley) 10/3/85";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/undo.h"
#include "commands/commands.h"
#include "select/selInt.h"
#include "utils/main.h"
#include "utils/malloc.h"
#include "utils/signals.h"
#include "sim/sim.h"

/* C99 compat */
#include "textio/textio.h"
#include "extract/extract.h"

/* Two cells worth of information are kept around by the selection
 * module.  SelectDef and SelectUse are for the cells whose contents
 * are the current selection.  Select2Def and Select2Use provide a
 * temporary working space for procedures that manipulate the selection.
 * for example, Select2Def is used to hold nets or regions while they
 * are being extracted by SelectRegion or SelectNet.  Once completely
 * extracted, information is copied to SelectDef.  Changes to
 * SelectDef are undo-able and redo-able (so that the undo package
 * can deal with selection changes), but changes to Select2Def are
 * not undo-able (undoing is always disabled when the cell is modified).
 */

extern CellDef *SelectDef, *Select2Def;
extern CellUse *SelectUse, *Select2Use;

typedef struct def_list_elt
{
    CellDef  	*dl_def;
    struct 	def_list_elt *dl_next;
    bool	dl_isMarked;
} SimDefListElt;

static SimDefListElt *SimCellLabList = (SimDefListElt *) NULL;
				/* list of all the cell defs we have
				 * put RSIM labels in
				 */

/* Data structure for node names extracted from the current selection.  For
 * each node, save the node name, a tile which lies in the node, and
 * the text for the label corresponding to the node's rsim value.
 */

typedef struct TLE {
    char *tl_nodeName;
    Tile *tl_nodeTile;
    char *tl_simLabel;
    struct TLE *tl_next;
} TileListElt;


TileListElt *NodeList = (TileListElt *) NULL;
				/* list of all nodes in the selected area */
HashTable SimNodeNameTbl;	/* node names found in the selected area */
HashTable SimGetnodeTbl;	/* node names to abort name search on */
HashTable SimGNAliasTbl;	/* node name aliases found during search */
HashTable SimAbortSeenTbl;	/* aborted node names found during search */

bool 	  SimRecomputeSel = TRUE;	/* selection has changed */
bool	  SimInitGetnode = TRUE;	/* Getnode called for the 1st time */
bool	  SimGetnodeAlias = FALSE;	/* if node aliases are to be printed */
bool	  SimSawAbortString;	/* if saw string to abort name search */
bool	  SimIsGetnode;		/* true if command was issued from Getnode */
bool	  SimUseCoords;		/* true if we should use trans. position */
bool	  SimIgnoreGlobals = TRUE;	/* If FALSE, node names ending in "!"	*/
					/* are treated as global node names.	*/

#ifndef RSIM_MODULE
bool	  SimRsimRunning = FALSE;	/* Always false if there's no rsim module */
#endif

/*
 * ----------------------------------------------------------------------------
 *
 *   SimSelectNode --
 *
 * 	This procedure selects an entire node.  It is similar to SelectNet.
 *
 * Results:
 *	Returns the node name.
 *
 * Side effects:
 *	Starting from material of type "type" under scx, this procedure
 *	finds all material in all expanded cells that are electrically-
 *	connected to the starting material through a chain of expanded
 *	cells.
 *
 * ----------------------------------------------------------------------------
 */

char *
SimSelectNode(scx, type, xMask, buffer)
    SearchContext *scx;		/* Area to tree-search for material.  The
				 * transform must map to EditRoot coordinates.
				 */
    TileType type;		/* The type of material to be considered. */
    int xMask;			/* Indicates window (or windows) where cells
				 * must be expanded for their contents to be
				 * considered.  0 means treat everything as
				 * expanded.
				 */
    char *buffer;		/* buffer to hold node name */
{
    TileTypeBitMask mask;
    char *strptr;

    TTMaskZero(&mask);
    TTMaskSetType(&mask, type);

    /* Clear out the temporary selection cell and yank all of the
     * connected paint into it.
     */

    UndoDisable();
    DBCellClearDef(Select2Def);
    SimTreeCopyConnect(scx, &mask, xMask, DBConnectTbl,
	    &TiPlaneRect, Select2Use, buffer);
    UndoEnable();

    /* Strip out path if name is global (ends with a "!")	*/
    /* and flag SimIgnoreGlobals is not set.			*/

    if (!SimIgnoreGlobals)
    {
	strptr = buffer + strlen(buffer) - 1;
	if (*strptr == '!') {
	    *strptr = '\0';
	    while (strptr != buffer) {
		if (*strptr == '/') {
		    strptr++;
		    break;
		}
		strptr--;
	    }
	}
	else {
	    strptr = buffer;
	}
    }
    else
	strptr = buffer;

    return(strptr);
}

int
NullFunc()
{
    return(0);
}

/*
 * ----------------------------------------------------------------------------
 * SimFreeNodeList
 *
 *	This procedure frees all space allocated for the node list
 *	data structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	After the list has been deallocated, the head pointer is set to
 *	a NULL value.
 * ----------------------------------------------------------------------------
 */

void
SimFreeNodeList(List)
    TileListElt **List;
{
    TileListElt *current;
    TileListElt *temp;

    temp = *List;
    while (temp != NULL) {
	current = temp;
	temp = temp->tl_next;
	freeMagic(current->tl_nodeName);
	freeMagic(current);
    }
    *List = (TileListElt *) NULL;
}

TileListElt *
simFreeNodeEntry(list, entry)
    TileListElt *list, *entry;
{
    TileListElt *prev, *curr;

    prev = list;
    for( curr = prev->tl_next; curr != NULL; prev = curr,curr = curr->tl_next )
	if( curr == entry )
	{
	    prev->tl_next = curr->tl_next;
	    freeMagic( entry->tl_nodeName );
	    freeMagic( entry );
	    return( prev );		/* next element in list */
	}

    /* should never get here */
    return(entry);
}

/*
 * ----------------------------------------------------------------------------
 * SimSelectArea
 *
 * 	This procedure checks the current selected paint in the circuit and
 *	extracts the names of all the nodes in the selected area.
 *
 * Results:
 *	Returns a list of node name data structures.
 *
 * Side Effects:
 *	The tiles of the selection cell definition are first marked in the
 *	search algorithm.  After finishing the search, these marks are
 *	erased.
 *
 * ----------------------------------------------------------------------------
 */

TileListElt *
SimSelectArea(Rect *rect)
{
    int plane;
    int SimSelectFunc();

    /* only need to extract node names if the selection has changed or
     * if node aliases are to be printed.
     */

    if (SimRecomputeSel || (SimGetnodeAlias && SimIsGetnode)) {
	SimFreeNodeList(&NodeList);
	HashInit(&SimAbortSeenTbl, 20, HT_STRINGKEYS);

	/* find all nodes in the current selection */

	for (plane = PL_TECHDEPBASE; plane < DBNumPlanes; plane++)
	{
	    (void) DBSrPaintArea((Tile *) NULL, SelectDef->cd_planes[plane],
			&TiPlaneRect, &DBAllButSpaceAndDRCBits,
			SimSelectFunc, (ClientData) &NodeList);
	}

	HashKill(&SimAbortSeenTbl);
	ExtResetTiles(SelectDef, (ClientData) CLIENTDEFAULT);
	SimGetNodeCleanUp();
	SimRecomputeSel = FALSE;
    }
    if (SigInterruptPending) {

	/* if caught an interrupt, be sure to recompute the selection
	 * next time around.
	 */

	SimRecomputeSel = TRUE;
    }
    return(NodeList);
}


/*
 * ----------------------------------------------------------------------------
 * SimSelectFunc
 *
 * 	This procedure is called for each tile in the current selection.
 *	It first checks to see if the node the tile belongs to has not
 *	yet been visited.  If it has not been visited, then the node name
 *	is extracted and all other tiles in the selection which belong to
 *	this node are marked.
 *
 * Results:
 *	Return 0 to keep the search going.
 *
 * Side effects:
 *	The tiles in the selection cell definition are left marked.
 *	It is the responsibility of the calling function to erase these
 *	tile marks when finished.
 * ----------------------------------------------------------------------------
 */

int
SimSelectFunc(tile, pHead)
    Tile *tile;			/* Tile in SelectDef. */
    TileListElt **pHead;	/* list of node names found */
{
    TileTypeBitMask 	mask;
    SearchContext 	scx;
    DBWclientRec 	*crec;
    MagWindow 		*window;
    char 		nameBuff[256], *nodeName;
    TileListElt 	*newNodeTile;
    TileType 		type;
    bool		coord;

    window = CmdGetRootPoint((Point *) NULL, &scx.scx_area);
    if (window == NULL) return 1;

    /* check to see if the node has already been extracted */

    if (tile->ti_client == (ClientData) 1) {
	return(0);
    }

    if (IsSplit(tile))
    {
	type = (SplitSide(tile)) ? SplitRightType(tile):
			SplitLeftType(tile);
    }
    else
	type = TiGetTypeExact(tile);

    /* get the tile's area, and initialize the tile's search context. */

    TITORECT(tile, &scx.scx_area);

    /* make sure that search context isn't placed on an empty	*/
    /* corner of a split tile.					*/
    if (IsSplit(tile))
    {
	if (SplitSide(tile))
	    scx.scx_area.r_xbot = scx.scx_area.r_xtop - 1;
	if (!(SplitDirection(tile) ^ SplitSide(tile)))
	    scx.scx_area.r_ybot = scx.scx_area.r_ytop - 1;
    }

    scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
    scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;

    scx.scx_use = (CellUse *) window->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    crec = (DBWclientRec *) window->w_clientData;

    TTMaskZero(&mask);
    TTMaskSetType(&mask, type);
    TTMaskAndMask(&mask, &crec->dbw_visibleLayers);
    TTMaskAndMask(&mask, &DBAllButSpaceAndDRCBits);

    /* Check if the area is above a layer which is not visible. */

    if (TTMaskIsZero(&mask)) {
	return(0);
    }

    /* mark all other tiles in the selection that are part of this node */

    SimSrConnect(SelectDef, &scx.scx_area, &DBAllButSpaceAndDRCBits,
            DBConnectTbl, &TiPlaneRect, NullFunc, (ClientData) NULL);

    /* Pick a tile type to use for selection. */

    for (type = TT_TECHDEPBASE; type < DBNumTypes; type += 1)
    {
	if (TTMaskHasType(&mask, type)) break;
    }

    nodeName = SimSelectNode(&scx, type, CU_DESCEND_ALL, nameBuff);

    /* add the node name to the list only if it has not been seen yet */

    coord = (nodeName[0] == '@' && nodeName[1] == '=') ? TRUE : FALSE;

    if(coord || HashLookOnly(&SimNodeNameTbl, nodeName) == (HashEntry *) NULL)
    {
	if( ! coord )
	    HashFind(&SimNodeNameTbl, nodeName);
	newNodeTile = (TileListElt *) mallocMagic((unsigned) (sizeof (TileListElt)));
	newNodeTile->tl_nodeName = (char *) mallocMagic((unsigned) (strlen(nodeName) + 1));
	strcpy(newNodeTile->tl_nodeName, nodeName);
	newNodeTile->tl_nodeTile = tile;
	newNodeTile->tl_next = *pHead;
	*pHead = newNodeTile;
    }
    return(0);
}

#ifdef RSIM_MODULE

/*
 * ----------------------------------------------------------------------------
 * SimSelection
 *
 *	This procedure applies the specified rsim command to the list of
 *	nodes in the current selection and also attaches the rsim node
 *	value string to each node.
 *
 * Results:
 *	returns FALSE if no nodes are in the selection.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
SimSelection(cmd)
    char *cmd;			/* rsim command to apply to the selection */
{
    static char Hstring[] = "RSIM=1";
    static char Lstring[] = "RSIM=0";
    static char Xstring[] = "RSIM=X";
    static char QUESTstring[] = "?";

    char timeString[256];
    TileListElt	*current, *node_list;
    char 		*replyLine;
    char		*strPtr;
    bool		goodReply;

    extern void		RsimErrorMsg();

    timeString[0] = 0;

    /* check to see if Rsim has been started yet */

    if (!SimRsimRunning) {
	RsimErrorMsg();
	return(FALSE);
    }

    /* get the list of all nodes in the selection */

    SimIsGetnode = FALSE;
    SimUseCoords = SimHasCoords;

    HashInit(&SimNodeNameTbl, 60, HT_STRINGKEYS);
    node_list = SimSelectArea((Rect *)NULL);

    if (node_list == (TileListElt *) NULL) {
	TxPrintf("You must select paint (rather than a cell) to use Rsim on \
the selection.\n");
	goto bad;
    }

    /* Walk the list of node names, apply the rsim command to each node,
     * and then process the results.
     */

    for (current = node_list; current != NULL; current = current->tl_next)
    {
	current->tl_simLabel = QUESTstring;
	SimRsimIt(cmd, current->tl_nodeName);

	if (!SimGetReplyLine(&replyLine)) {
	    goto bad;
	}

	if (!replyLine) {
	    /* Rsim's reponse to the command was just a prompt.  We are done
	     * with the current node, so process the next node in the
	     * selection.
	     */
	    continue;
	}

	/* check for node names not recognized by Rsim */

	if (strncmp(replyLine, "time = ", 7) == 0)   {
	    if (timeString[0] == 0) {
		strcpy(timeString, replyLine);
	    }
	    TxPrintf("%s not recognized in sim file\n", current->tl_nodeName);
	    while(replyLine) {
		/* swallow Rsim reply until no more is left */
		if (!SimGetReplyLine(&replyLine)) {
		    goto bad;
		}
	    }
	    continue;
	}

	/* update the node value label strings  */

	if (*cmd == 'd')
	{
	    char  *name = current->tl_nodeName;
	    bool  coord = (name[0] == '@' && name[1] == '=') ? TRUE : FALSE;

	    strPtr = strrchr( replyLine, '=' );
	    if( strPtr == NULL )
		strPtr = QUESTstring;
	    else if( coord )
	    {
		*strPtr = '\0';
		name = replyLine;
		if( HashLookOnly(&SimNodeNameTbl, name) == (HashEntry *) NULL)
		{
		    freeMagic(current->tl_nodeName);
		    current->tl_nodeName = (char *) mallocMagic((unsigned) (strlen(name) + 1));
		    strcpy(current->tl_nodeName, name);
		    HashFind(&SimNodeNameTbl, current->tl_nodeName);
		    *strPtr++ = '=';
		}
		else
		{
		    current = simFreeNodeEntry( node_list, current );
		    *strPtr = '=';
		}
	    }
	    else
		strPtr++;

	    switch (*strPtr) {
		case '1' :
		    current->tl_simLabel = Hstring;
		    break;
		case '0' :
		    current->tl_simLabel = Lstring;
		    break;
		case 'X' :
		    current->tl_simLabel = Xstring;
		    break;
		case '=' :
		    break;
		default :
		    current->tl_simLabel = QUESTstring;
		    break;
	    }
	}

	/* read all lines of the Rsim reply */

	goodReply = TRUE;
	for (;replyLine; goodReply = SimGetReplyLine(&replyLine)) {
	    if (!goodReply) {
		goto bad;
	    }
	    if (!strncmp(replyLine, "time = ", 7)) {
		if (!(timeString[0])) {
		    strcpy(timeString, replyLine);
		}
		continue;
	    } else if( *strPtr != '=' ) {
		TxPrintf("%s\n", replyLine);
	    }
	}
    }
    if (timeString[0] != 0) {
	TxPrintf("%s\n", timeString);
    }

    HashKill(&SimNodeNameTbl);
    return(TRUE);

  bad:
    HashKill(&SimNodeNameTbl);
    return(FALSE);
}


#endif

/*
 * ----------------------------------------------------------------------------
 * SimAddLabels
 *
 *	This procedure adds the node value labels to the Magic database
 *	so they will be displayed in the layout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cell modified flags are deliberately not set when these labels
 *	are added to the database.
 *
 * ----------------------------------------------------------------------------
 */

void
SimAddLabels(SelectNodeList, rootuse)
    TileListElt *SelectNodeList;
    CellDef *rootuse;		/* the root cell def for the window */
{

    TileListElt *current;
    Rect selectBox;
    int pos;

    /* walk the list of selected nodes, add the node value label to the
     * database.
     */

    for (current = SelectNodeList; current != (TileListElt *) NULL;
	current = current->tl_next) {
	if (*(current->tl_simLabel) == '?') {
	    continue;
	}
	TiToRect(current->tl_nodeTile, &selectBox);
	pos = SimPutLabel(rootuse, &selectBox, GEO_CENTER,
		current->tl_simLabel, TT_SPACE);
	DBReComputeBbox(rootuse);
	DBWLabelChanged(rootuse, current->tl_simLabel, &selectBox,
	    pos, DBW_ALLWINDOWS);
    }
}

#ifdef RSIM_MODULE

/*
 * ----------------------------------------------------------------------------
 * SimRsimMouse
 *
 *	This procedure erases the old rsim node labels and display the
 *	node labels of the current selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
SimRsimMouse(w)
    MagWindow *w;
{

    CellUse *cu;
    bool sawcell;
    SimDefListElt *dummy;

    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    cu = (CellUse *)w->w_surfaceID;	/* get root cell use for wind */

    /* check to see if the cell def is already in our list */
    sawcell = FALSE;
    for (dummy = SimCellLabList; dummy; dummy = dummy->dl_next) {
	if (dummy->dl_def == cu->cu_def) {
	    sawcell = TRUE;
	    break;
	}
    }

    if (!sawcell) {
	/* add the cell def to the list */
	if (SimCellLabList == (SimDefListElt *) NULL) {
	    SimCellLabList = (SimDefListElt *) mallocMagic((unsigned) (sizeof(SimDefListElt)));
	    SimCellLabList->dl_isMarked = FALSE;
	    SimCellLabList->dl_def = cu->cu_def;
	    SimCellLabList->dl_next = (SimDefListElt *) NULL;
	    dummy = SimCellLabList;
	}
	else {
	    dummy = (SimDefListElt *) mallocMagic((unsigned) (sizeof(SimDefListElt)));
	    dummy->dl_isMarked = FALSE;
	    dummy->dl_next = SimCellLabList;
	    dummy->dl_def = cu->cu_def;
	    SimCellLabList= dummy;
	}
    }

    SimEraseLabels();
    if (SimSelection("d")) {
	dummy->dl_isMarked = TRUE;
	SimAddLabels(NodeList, dummy->dl_def);
    }
}

#endif

/*
 * ----------------------------------------------------------------------------
 * SimGetnode
 *
 *	This procedure prints the node names of all selected nodes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
SimGetnode()
{
    TileListElt *current;

    /* get the list of node names */

    SimIsGetnode = TRUE;
    SimUseCoords = FALSE;

    HashInit(&SimNodeNameTbl, 60, HT_STRINGKEYS);
    current = SimSelectArea((Rect *)NULL);
    HashKill(&SimNodeNameTbl);

    if (current == (TileListElt *) NULL) {
	TxPrintf("You must select paint (not a cell) to use getnode.\n");
	return;
    }

    for (; current != (TileListElt *) NULL; current = current->tl_next)
    {
#ifdef MAGIC_WRAPPER
	/* Return the node name as the result of the command */
	Tcl_AppendElement(magicinterp, current->tl_nodeName);
#else
	TxPrintf("node name : %s\n", current->tl_nodeName);
#endif
    }
}

/*
 * ----------------------------------------------------------------------------
 * SimGetsnode
 *
 *	This procedure prints the short node names of all selected nodes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
SimGetsnode()
{
    TileListElt *current;

    /* get the list of node names */

    SimIsGetnode = TRUE;
    SimUseCoords = TRUE;

    HashInit(&SimNodeNameTbl, 60, HT_STRINGKEYS);
    current = SimSelectArea((Rect *)NULL);
    HashKill(&SimNodeNameTbl);

    if (current == (TileListElt *) NULL) {
	TxPrintf("You must select paint (not a cell) to use getnode.\n");
	return;
    }

    for (; current != (TileListElt *) NULL; current = current->tl_next)
    {
#ifdef MAGIC_WRAPPER
	/* Return the node short name as the result of the command */
	Tcl_AppendElement(magicinterp, current->tl_nodeName);
#else
	TxPrintf("short node name : %s\n", current->tl_nodeName);
#endif
    }
}



/*
 * ----------------------------------------------------------------------------
 * SimEraseLabels
 *
 *	This procedure erases the RSIM labels from any cell defs they
 *	may have been added to.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the RSIM labels from "marked" cell defs.
 *
 * ----------------------------------------------------------------------------
 */

void
SimEraseLabels()
{
    SimDefListElt *p;

    for (p = SimCellLabList; p; p = p->dl_next) {
	if (p->dl_isMarked) {
	    p->dl_isMarked = FALSE;
	    DBEraseLabelsByContent(p->dl_def, (Rect *)NULL, -1, "RSIM=X");
	    DBEraseLabelsByContent(p->dl_def, (Rect *)NULL, -1, "RSIM=1");
	    DBEraseLabelsByContent(p->dl_def, (Rect *)NULL, -1, "RSIM=0");
	}
    }
}
