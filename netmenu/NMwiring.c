/* NMwiring.c -
 *
 *	This file contains procedures that manipulate wiring
 *	and netlists.  For example, there are procedures to
 *	generate a net from a wire, to rip up a wire, and to
 *	verify that all the connections in a netlist actually
 *	exist in the circuit.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMwiring.c,v 1.3 2008/12/11 04:20:11 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "netmenu/netmenu.h"
#include "netmenu/nmInt.h"
#include "textio/textio.h"
#include "utils/styles.h"
#include "drc/drc.h"
#include "utils/malloc.h"
#include "router/router.h"
#include "utils/utils.h"

/* The following structure is used to hold information about
 * areas to be erased during net ripup.
 */

struct nmwarea
{
    Rect nmwa_area;		/* Area to be deleted. */
    TileType nmwa_type;		/* Type of material to be erased. */
    struct nmwarea *nmwa_next;	/* Next item in list, or NULL for end. */
};

/* The following static is used by nmwNetTermFunc to signal to nmwNetTileFunc
 * that it found a terminal.
 */

static bool nmwGotTerm;

/* The following arrays (which grow dynamically as the need arises)
 * are used during net-list verification, culling, and routing measurement.
 * They record all of the terminals found in the current net.  Terminals are
 * recorded by the addresses of their names and by their areas.
 */

static char ** nmwVerifyNames;		/* Actual storage (malloc'ed). */
static char ** nmwNonTerminalNames;	/* Actual storage (malloc'ed). */
static Rect *nmwVerifyAreas;		/* Areas of terminals (malloc'ed). */
static int nmwCullDone;			/* Counts number of cull errors */
static int nmwVerifySize = 0;		/* Size of arrays. */
static int nmwNonTerminalSize = 0;	/* Size of arrays. */
static int nmwVerifyCount;		/* Number of valid entries in array.*/
static int nmwNonTerminalCount;		/* Number of valid entries in array.*/
static int nmwVerifyErrors;		/* Counts number of errors found. */
static bool nmwVerifyNetHasErrors;	/* TRUE means an error has already
					 * been found in the current net.
					 */
static bool nmwNetFound = FALSE;	/* TRUE means a net has been found and
					 * we should skip the rest of the
					 * terminals in that net 
					 */
/* The nmMeasureTiles array (which grows dynamically as needed) records
 * all tiles found on the current net.
 */
static int nmMeasureSize = 0;		/* Size of tile array */
static int nmMeasureCount;		/* Number of valid tile array entries */
static Tile ** nmMeasureTiles = NULL;	/* Actual storage for tile array */

/* Counters used to measure net statistics */
static int nmMArea = 0;
static int nmPArea = 0;
static int nmVCount= 0;

/* Maximimum length for terminals allocated here: */

#define TERMLENGTH 200


/*
 * ----------------------------------------------------------------------------
 *
 * nmwRipTileFunc --
 *
 * 	This procedure is invoked by DBSrConnect when ripping up
 *	paint.  It records each tile found on a list, so they can
 *	be deleted later (it isn't safe for us to just delete the
 *	tile here, since there is a search in progress over the
 *	database).
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Adds rectangular areas to the list.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwRipTileFunc(tile, plane, listHead)
    Tile *tile;			/* Tile that is to be deleted. */
    int plane;			/* Plane index of the tile */
    struct nmwarea **listHead;	/* Pointer to list head pointer. */
{
    struct nmwarea *new;

    new = (struct nmwarea *) mallocMagic(sizeof(struct nmwarea));
    TiToRect(tile, &new->nmwa_area);
    new->nmwa_type = TiGetType(tile);
    new->nmwa_next = *listHead;
    *listHead = new;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMRipup --
 *
 * 	Starting from some piece of paint in the edit cell underneath
 *	the box, this procedure rips up all connected paint.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The edit cell is modified.
 *
 * ----------------------------------------------------------------------------
 */

int
NMRipup()
{
    struct nmwarea *list;
    Rect area;
    TileTypeBitMask maskBits;

    /* Collect all the connected areas together into a list. */

    list = NULL;
    if (!ToolGetEditBox(&area)) return 0;

    /* Expand the box to get everything touching it. */

    GEO_EXPAND(&area, 1, &area);

    (void) DBSrConnect(EditCellUse->cu_def, &area,
	&DBAllButSpaceAndDRCBits, DBConnectTbl,
	&TiPlaneRect, nmwRipTileFunc, (ClientData) &list);
    
    /* Now go through the list one item at a time and delete
     * the attached paint.
     */
    
    TTMaskZero(&maskBits);
    while (list != NULL)
    {
	DBErase(EditCellUse->cu_def, &list->nmwa_area, list->nmwa_type);
	TTMaskSetType(&maskBits, list->nmwa_type);
	(void) DBEraseLabel(EditCellUse->cu_def, &list->nmwa_area, &maskBits, NULL);
	TTMaskClearType(&maskBits, list->nmwa_type);
	DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &list->nmwa_area);
	DBWAreaChanged(EditCellUse->cu_def, &list->nmwa_area, DBW_ALLWINDOWS,
	     &DBAllButSpaceBits);
	freeMagic((char *) list);
	list = list->nmwa_next;
    }
    DBReComputeBbox(EditCellUse->cu_def);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmRipLocFunc --
 *
 *	This function is called once for each terminal location in
 *	a netlist being ripped up in its entirety.  It rips up any
 *	paint underneath this terminal, and adds its area into the
 *	total area affected so far.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	A net gets ripped up.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
nmRipLocFunc(rect, name, label, area)
    Rect *rect;			/* Area of the terminal, edit cell coords. */
    char *name;			/* Name of the terminal (ignored). */
    Label *label;		/* Pointer to the label, used to find out
				 * what layer the label's attached to.
				 */
    Rect *area;			/* We GeoInclude into this all the areas of
				 * all the tiles we delete.
				 */
{
    struct nmwarea *list;
    Rect initialArea;
    TileTypeBitMask maskBits;

    /* This code is basically the same as in NMRipup. */

    GEO_EXPAND(rect, 1, &initialArea);
    list = NULL;
    (void) DBSrConnect(EditCellUse->cu_def, &initialArea,
	&DBConnectTbl[label->lab_type], DBConnectTbl,
	&TiPlaneRect, nmwRipTileFunc, (ClientData) &list);
    TTMaskZero(&maskBits);
    TTMaskClearType(&maskBits, label->lab_type);
    while (list != NULL)
    {
	DBErase(EditCellUse->cu_def, &list->nmwa_area, list->nmwa_type);
	TTMaskSetType(&maskBits, list->nmwa_type);
	(void) DBEraseLabel(EditCellUse->cu_def, &list->nmwa_area, &maskBits, NULL);
	TTMaskClearType(&maskBits, list->nmwa_type);
	(void) GeoInclude(&list->nmwa_area, area);
	freeMagic((char *) list);
	list = list->nmwa_next;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmRipNameFunc --
 *
 * 	This function gets called during netlist ripup.  It's invoked
 *	once for each terminal name in the list.  It just calls
 *	DBSrLabelLoc to invoke nmRipLocFunc for each terminal
 *	location associated with the name.
 *
 * Results:
 *	Always returns 0 to keep the search from aborting.
 *
 * Side effects:
 *	Nothing here, but it calls procedures that rip up nets.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
nmRipNameFunc(name, firstInNet, area)
    char *name;			/* Name of terminal. */
    bool firstInNet;		/* Ignored by this procedure. */
    Rect *area;			/* Passed through as ClientData to
				 * nmRipLocFunc.
				 */
{
    (void) DBSrLabelLoc(EditCellUse, name, nmRipLocFunc, (ClientData) area);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMRipupList --
 *
 * 	This function rips up all wiring associated with each net in
 *	the current netlist.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Massive amounts of wiring is destroyed.  The edit cell may never
 *	be the same again.
 *
 * ----------------------------------------------------------------------------
 */

int
NMRipupList()
{
    Rect area;

    /* This is easy.  Just enumerate every terminal in the netlist.
     * For each terminal, ripup all wiring starting at the point of
     * the terminal.  The client data is used to pass around a
     * rectangle that accumlates the total area of the edit cell that
     * was modified.
     */
    
    area = GeoNullRect;
    (void) NMEnumNets(nmRipNameFunc, (ClientData) &area);
    DBReComputeBbox(EditCellUse->cu_def);
    DBWAreaChanged(EditCellUse->cu_def, &area, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &area);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwNetCellFunc --
 *
 * 	This function is called by DBCellSrArea as part of nmwNetTileFunc.
 *	Its function is to print an error message for each cell found.
 *
 * Results:
 *	Returns 2 always, to skip past the current array.
 *
 * Side effects:
 *	An error message is printed.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwNetCellFunc(scx)
    SearchContext *scx;		/* Describes search. */
{
    TxError("Cell id %s touches net but has no terminals.\n",
	scx->scx_use->cu_id);
    return 2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwCheckFunc  --
 *
 * 	This function is called by NMEnumTerms when we're checking to
 *	see if a given terminal is in a given net.
 *
 * Results:
 *	Returns 1 if its two arguments match (terminal is in net).
 *	Returns 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwCheckFunc(name, otherName)
    char *name;			/* Terminal in net. */
    char *otherName;		/* Terminal we want to know if it's in net. */
{
    if (strcmp(name, otherName) == 0) return 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwNetTermFunc --
 *
 * 	This procedure is called by DBTreeSrLabels for each label found
 *	under the current tile being processed during netlist extraction.
 *
 * Results:
 *	Zero is always returned to keep the search alive, unless
 *	there's no current netlist, in which case 1 is returned
 *	to abort the search.
 *
 * Side effects:
 *	If the terminal is not in the edit cell (i.e. it contains a "/")
 *	it is added to the current net.  The current net pointer is
 *	changed to refer to this terminal to provide for our caller a
 *	handle on the net.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
int
nmwNetTermFunc(scx, label, tpath, netPtr)
    SearchContext *scx;		/* Describes state of search (ignored). */
    Label *label;		/* Label (ignored). */
    TerminalPath *tpath;	/* Gives hierarchical label name. */
    char **netPtr;		/* Pointer to a terminal in current net. */
{
    char *p, *p2;
    
    if (strchr(tpath->tp_first, '/') == 0) return 0;

    /* Add the label name onto the end of the terminal path name. */

    p2 = tpath->tp_next;
    for (p = label->lab_text; *p != 0; p += 1)
    {
	if (p2 == tpath->tp_last) break;
	*p2++ = *p;
    }
    *p2 = 0;

    /* If this terminal is already in a net, don't remove it.  But issue a
     * warning if it's not in the net we think it should be.
     */

    nmwGotTerm = TRUE;
    if (NMTermInList(tpath->tp_first))
    {
	if ((*netPtr == NULL)
	    || (NMEnumTerms(*netPtr, nmwCheckFunc,
	    (ClientData) tpath->tp_first) == 0))
	{
	    TxError("Warning: %s was already in a net (I left it there).\n",
	        tpath->tp_first);
	}
	return 0;
    }
    if (*netPtr == NULL)
	*netPtr = NMAddTerm(tpath->tp_first, tpath->tp_first);
    else *netPtr = NMAddTerm(tpath->tp_first, *netPtr);
    if (*netPtr == NULL)
    {
	TxError("No current netlist!  Please select one and retry.\n");
	return 1;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwNetTileFunc --
 *
 * 	This procedure is invoked by DBSrConnect from NMWExtract.
 *	It is called once for each connected tile.  Its purpose
 *	is to do two things.  First, it sees if there are any
 *	labels underneath the tile, other than those in the edit
 *	cell.  If so, it adds them to the net that's being formed
 *	Secondly, if there are no terminals under the tile, then
 *	there better not be any subcells under it either.  If
 *	there are, warning messages are printed by nmNetCellFunc.
 *
 * Results:
 *	Always returns 0 to keep the search alive, unless there's
 *	no current netlist, in which case 1 is returned to abort.
 *
 * Side effects:
 *	A terminal is added to a net.  *netPtr is set to point to
 *	a terminal in the netlist.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwNetTileFunc(tile, plane, netPtr)
    Tile *tile;			/* Tile that is connected to net. */
    int plane;			/* Plane index of the tile */
    char **netPtr;		/* Pointer to pointer to net name. */
{
    SearchContext scx;
    char label[TERMLENGTH];
    TerminalPath tpath;

    /* Add any terminals under this tile to the netlist.  We
     * have to blow up the tile's area by one for searching,
     * so that we get anything that even touches it.
     */
    
    TiToRect(tile, &scx.scx_area);
    scx.scx_area.r_xbot -= 1;
    scx.scx_area.r_xtop += 1;
    scx.scx_area.r_ybot -= 1;
    scx.scx_area.r_ytop += 1;
    scx.scx_use = EditCellUse;
    scx.scx_trans = GeoIdentityTransform;
    tpath.tp_first = label;
    tpath.tp_next = label;
    tpath.tp_last = &label[TERMLENGTH-1];
    nmwGotTerm = FALSE;
    if (DBTreeSrLabels(&scx, &DBConnectTbl[TiGetType(tile)], 0, &tpath,
	TF_LABEL_ATTACH, nmwNetTermFunc, (ClientData) netPtr) != 0)
    {
	/* Non-zero return value means no current netlist.  Quit now. */
	return 1;
    }

    /* If no new terminal was added, then make sure there aren't any
     * subcells underneath the current tile.
     */
    
    if (!nmwGotTerm)
    {
	(void) DBCellSrArea(&scx, nmwNetCellFunc, (ClientData) NULL);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMExtract --
 *
 * 	Starting from all paint in the edit cell that is underneath
 *	the box, this procedure finds all terminals in subcells that
 *	are attached to the net and puts them into the netlist as a
 *	new net.  The new net is highlighted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new net is created and highlighted.
 *
 * ----------------------------------------------------------------------------
 */

int
NMExtract()
{
    Rect area;
    char *net = NULL;

    if (!ToolGetEditBox(&area)) return 0;

    /* Expand the box area so we'll pick up everything touching it. */

    GEO_EXPAND(&area, 1, &area);
    net = NULL;
    (void) DBSrConnect(EditCellUse->cu_def, &area,
	&DBAllButSpaceAndDRCBits, DBConnectTbl,
	&TiPlaneRect, nmwNetTileFunc, (ClientData) &net);
    if (net == NULL)
    {
	TxError("There aren't any terminals connected");
	TxError(" to paint under the box\n");
	TxError("(except those, if any, already in other nets).\n");
    }
    NMSelectNet(net);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwVerifyLabelFunc2 --
 *
 * 	This procedure is invoked once for each label that is actually
 *	wired to the current net being verified.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	If the label is a terminal in the list, it is added to
 *	the array nmwVerifyNames.  In addition, any other terminals
 *	with the same name are searched out and wiring is traced
 *	from them to look for yet other terminals (this is done to
 *	handle feedthroughs).
 *
 * ----------------------------------------------------------------------------
 */
 
int
nmwVerifyLabelFunc2(scx, label, tpath, cd)
    SearchContext *scx;		/* Describes state of search. */
    Label *label;		/* Label. */
    TerminalPath *tpath;	/* Gives hierarchical label name. */
    ClientData cd;		/* Used in nmwVerifyTileFunc */
{
    char *p, *p2;
    char *name;
    extern int nmwVerifyLabelFunc();	/* Forward reference. */

    /* Add the label name onto the end of the terminal path name. */

    p2 = tpath->tp_next;
    for (p = label->lab_text; *p != 0; p += 1)
    {
	if (p2 == tpath->tp_last) break;
	*p2++ = *p;
    }
    *p2 = 0;

    /* See if the label is supposed to be in a net.  If not, forget it. */

    name = NMTermInList(tpath->tp_first);
    /*if (name == NULL) return 0;*/
    if (name == NULL) 
    {
	/*
	** make sure there is enough space in the array to hold this
	** NonTerminal. If not, allocate a new array and copy the old to
	** the new.
	*/
	if (nmwNonTerminalCount == nmwNonTerminalSize)
	{
	    char ** newNames;
	    int i, newSize;

	    newSize = 2*nmwNonTerminalSize;
	    if (newSize < 16) newSize = 16;
	    newNames = (char **) mallocMagic((unsigned) newSize*sizeof(char **));
	    for (i = 0; i < nmwNonTerminalSize; i++)
	    {
	        newNames[i] = nmwNonTerminalNames[i];
	    }
	    for (i = nmwNonTerminalSize; i < newSize; i++)
	    {
		newNames[i] = NULL;
	    }
	    if (nmwNonTerminalSize != 0)
	    {
	        freeMagic((char *) nmwNonTerminalNames);
	    }
	    nmwNonTerminalNames = newNames;
	    nmwNonTerminalSize = newSize;
        }
        (void) StrDup(&(nmwNonTerminalNames[nmwNonTerminalCount]),
							tpath->tp_first);
        nmwNonTerminalCount += 1;

	return 0;
    }

    /* Make sure that there's enough space in the array to hold this
     * terminal.  If not, then allocate a new array and copy the old
     * to the new.
     */
    
    if (nmwVerifyCount == nmwVerifySize)
    {
	char ** newNames;
	Rect *newAreas;
	int i, newSize;

	newSize = 2*nmwVerifySize;
	if (newSize < 16) newSize = 16;
	newNames = (char **) mallocMagic((unsigned) newSize*sizeof(char **));
	newAreas = (Rect *) mallocMagic((unsigned) newSize*sizeof(Rect));
	for (i = 0; i < nmwVerifySize; i++)
	{
	    newNames[i] = nmwVerifyNames[i];
	    newAreas[i] = nmwVerifyAreas[i];
	}
	if (nmwVerifySize != 0)
	{
	    freeMagic((char *) nmwVerifyNames);
	    freeMagic((char *) nmwVerifyAreas);
	}
	nmwVerifyNames = newNames;
	nmwVerifyAreas = newAreas;
	nmwVerifySize = newSize;
    }

    /* Add the name and areas to the arrays.  Use the name from the
     * netlist table, since its address is permanent and can be
     * compared later to see if we got all the terminals in the net.
     */
    
    nmwVerifyNames[nmwVerifyCount] = name;
    GeoTransRect(&scx->scx_trans, &label->lab_rect,
	&nmwVerifyAreas[nmwVerifyCount]);
    nmwVerifyCount += 1;

    /* See if there are any other labels with the same name as this
     * one.  If so, trace out the wiring that connects to them.
     */
    
    (void) DBSrLabelLoc(EditCellUse, name, nmwVerifyLabelFunc, cd);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwVerifyTileFunc --
 *
 * 	This procedure is invoked when tracing out wiring to see what's
 *	actually attached to a net.  It is invoked once for each tile
 *	in the wiring.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Any terminals found under the tile are added to nmwVerifyNames.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwVerifyTileFunc(tile, plane, func)
    Tile *tile;			/* Tile that is connected to this net. */
    int plane;			/* Plane index of the tile. */
    int (*func)();		/* Processing function for each tile. */
{
    SearchContext scx;
    char label[TERMLENGTH];
    TerminalPath tpath;

    /* Search out all the labels underneath the tile, regardless of cell. */

    if(func!=NULL)
	(*func)(tile);
    TiToRect(tile, &scx.scx_area);
    GEO_EXPAND(&scx.scx_area, 1, &scx.scx_area);
    scx.scx_use = EditCellUse;
    scx.scx_trans = GeoIdentityTransform;
    tpath.tp_first = label;
    tpath.tp_next = label;
    tpath.tp_last = &label[TERMLENGTH-1];
    (void) DBTreeSrLabels(&scx, &DBConnectTbl[TiGetType(tile)],
	0, &tpath, TF_LABEL_ATTACH, nmwVerifyLabelFunc2, (ClientData) func);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwVerifyLabelFunc --
 *
 * 	This procedure is invoked for each label of a terminal in
 *	a net.  Its purpose is to trace out all wiring emanating
 *	from each label and add the terminals attached to that
 *	wiring into the arrays of stuff in the current net.  This
 *	procedure is invoked for the first terminal in each net,
 *	and also for each additional terminal in case there are
 *	feedthroughs.
 *
 * Results:
 *	Always returns 0 to keep the search alive.
 *
 * Side effects:
 *	Causes the table nmwVerifyNames to be built.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwVerifyLabelFunc(rect, name, label, cd)
    Rect *rect;			/* Area of the label, in EditUse coords. */
    char *name;			/* Hierarchical name of label. */
    Label *label;		/* Actual label structure. */
    ClientData cd;		/* Client data for nmwVerifyTileFunc */
{
    TileTypeBitMask *mask;
    int i;
    Rect biggerArea;

    /* If this label has already been seen and processed, don't
     * process it again.  That could lead to infinite loops.
     */

    for (i = 0; i < nmwVerifyCount; i++)
    {
	Rect *r;

	r = &nmwVerifyAreas[i];
	if ((r->r_xbot == rect->r_xbot) && (r->r_ybot == rect->r_ybot)
		&& (r->r_xtop == rect->r_xtop) && (r->r_ytop == rect->r_ytop)
		&& (strcmp(name, nmwVerifyNames[i]) == 0))
	    return 0;
    }
    
    /* Trace out all wiring in the edit cell that's attached to this
     * label (to accumulate information about what's actually wired).
     */
    
    if (label->lab_type == TT_SPACE) mask = &DBAllButSpaceAndDRCBits;
    else mask = &DBConnectTbl[label->lab_type];

    /* We want to consider anything that even touches the label. */

    GEO_EXPAND(rect, 1, &biggerArea);
    (void) DBSrConnect(EditCellUse->cu_def, &biggerArea, mask,
	DBConnectTbl, &TiPlaneRect, nmwVerifyTileFunc, cd);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwVErrorLabelFunc --
 *
 * 	Called when a terminal isn't present in its net.  Generates
 *	a feedback area once, then aborts the search.
 *
 * Results:
 *	Always returns 1 to abort the search:  one feedback area is
 *	all that we want.
 *
 * Side effects:
 *	Generates a feedback area.
 *
 * ----------------------------------------------------------------------------
 */

    /* ARGSUSED */
int
nmwVErrorLabelFunc(rect, name, label)
    Rect *rect;			/* Area of label in edit cell coords. */
    char *name;			/* Hierarchical name of label. */
    Label *label;		/* Pointer to the label itself (not used). */
{
    char msg[200];
    Rect biggerArea;

    (void) sprintf(msg, "Net of \"%.100s\" isn't fully connected.", name);

    /* Make the feedback area larger than the label;  otherwise point labels
     * won't be visible at all.
     */
    
    GEO_EXPAND(rect, 1, &biggerArea);
    DBWFeedbackAdd(&biggerArea, msg, EditCellUse->cu_def, 1,
	    STYLE_PALEHIGHLIGHTS);
    nmwVerifyErrors += 1;
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwVerifyTermFunc --
 *
 * 	This procedure is called for each terminal in a net to make sure
 *	that the terminal's name appears in the list of terminals found
 *	in the net.
 *
 * Results:
 *	Returns 0 to keep the search alive.
 *
 * Side effects:
 *	All entries in nmwVerifyNames that match name are NULL'ed out
 *	to show that they were found.
 *
 * ----------------------------------------------------------------------------
 */

    /* ARGSUSED */
int
nmwVerifyTermFunc(name, report)
    char *name;		/* Name of terminal. */
    bool report;	/* TRUE => print error messages */
{
    int i, found;

    found = FALSE;

    /* Find and clear all matching entries in nmwVerifyNames. */

    for (i = 0; i < nmwVerifyCount; i++)
    {
	if (nmwVerifyNames[i] == NULL) continue;
	if (strcmp(nmwVerifyNames[i], name) != 0) continue;
	found = TRUE;
	nmwVerifyNames[i] = NULL;
    }

    /* If the terminal wasn't found, and this is the first error
     * for the net, print an error.  Only print one error per net.
     */

    /* if (found || nmwVerifyNetHasErrors) return 0; */
    if (found) return 0;
    nmwVerifyNetHasErrors = TRUE;
    if(report)
    {
	/*TxError("Net of \"%s\" isn't fully connected.\n", name);*/
	TxError("Terminal \"%s\" not connected.\n", name);
	(void) DBSrLabelLoc(EditCellUse, name, nmwVErrorLabelFunc,
		(ClientData) name);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwVerifyNetFunc --
 *
 * 	This procedure is called during net-list verification.  It
 *	is invoked once for each terminal in each net in the list.
 *	It ignores all but the first terminals in nets.  For each
 *	net, it checks to be sure the net is wired correctly.
 *
 * Results:
 *	Always returns 0 to keep the enumeration alive.
 *
 * Side effects:
 *	Outputs messages for short circuits or open circuits.
 *
 * ----------------------------------------------------------------------------
 */

int
nmwVerifyNetFunc(name, first)
    char *name;			/* Name of terminal. */
    bool first;			/* TRUE means this is first terminal
				 * of a new net.
				 */
{
    int i;

    /*if (!first) return 0;*/
    if (first)
	nmwNetFound = FALSE;
    if (nmwNetFound) return 0;

    /* First, collect all the terminals that can be reached from
     * the current terminal, using wiring in the edit cell and
     * feedthroughs in children (if there are multiple terminals
     * by the same name, they're assumed to be connected).  Start
     * off by enumerating all instances of the first terminal.  Lower-
     * level procedures do the rest.
     */
    
    nmwVerifyCount=0;
    nmwNonTerminalCount=0;
    (void) DBSrLabelLoc(EditCellUse, name, nmwVerifyLabelFunc,
	    (ClientData) NULL);
    
    /* Check whether the label was found, if not report an error.
     */
    if (nmwVerifyCount == 0)
    {
	TxError("Terminal \"%s\" not found\n", name);
	return 0;
    }
    nmwNetFound = TRUE;


    /* Next, enumerate all of the terminals in the net, and make sure
     * that they got found by the search.  NmwVerifyTermFunc takes
     * care of errors and NULLs out entries that it found terminals
     * for.
     */
    
    nmwVerifyNetHasErrors = FALSE;
    (void) NMEnumTerms(name, nmwVerifyTermFunc, (ClientData) 1);

    /* If there are any non-NULL names left in the array nmwVerifyNames,
     * they represent shorts between this net and other nets.  Print
     * errors for them.
     */
    
    for (i = 0; i < nmwVerifyCount; i++)
    {
	if (nmwVerifyNames[i] != NULL)
	{
	    Rect biggerArea;
	    char msg[200];

	    TxError("Net \"%s\" shorted to net \"%s\".\n",
		    name, nmwVerifyNames[i]);
	    GEO_EXPAND(&nmwVerifyAreas[i], 1, &biggerArea);
	    (void) sprintf(msg, "Net \"%.80s\" shorted to net \"%.80s\".\n",
		name, nmwVerifyNames[i]);
	    DBWFeedbackAdd(&biggerArea, msg, EditCellUse->cu_def,
		    1, STYLE_PALEHIGHLIGHTS);
	    nmwVerifyErrors += 1;
	    break;
	}
    }

    /*
    ** If there are any errors in this net, than print all the NonTerminals
    ** that are attached to electrically connected paint.
    */
    if (nmwVerifyNetHasErrors && nmwNonTerminalCount != 0)
    {
	TxError("Error found on net of %s:\n", name);
	TxError("Additional electrically connected labels:\n");
	for (i = 0; i < nmwNonTerminalCount; i++)
	{
	    TxError("\t%s\n", nmwNonTerminalNames[i]);
	}
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMWVerify --
 *
 * 	This procedure examines wiring in the edit cell to make sure
 *	that the connects are implemented exactly as specified in the
 *	current net-list.  This means that nets are fully connected,
 *	and do not connect to any other nets in the list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Error messages are output for open or shorted nets.
 *
 * ----------------------------------------------------------------------------
 */

int
NMVerify()
{
    int i;

    /* Enumerate all the nets and let the search function do the
     * real work.
     */
    
    nmwVerifyErrors = 0;
    (void) NMEnumNets(nmwVerifyNetFunc, (ClientData) NULL);

    /* Free the space allocated for error reporting
    */
    for (i = 0; i < nmwNonTerminalSize; i++)
    {
	if (nmwNonTerminalNames[i] != NULL)
	{
	    freeMagic((char *) nmwNonTerminalNames[i]);
	    nmwNonTerminalNames[i] = NULL;
	}
    }

    if (nmwVerifyErrors == 0)
	TxPrintf("No wiring errors found.\n");
    else
    if (nmwVerifyErrors == 1)
	TxPrintf("One feedback area generated (you're getting close!).\n");
    else
	TxPrintf("%d feedback areas generated.\n", nmwVerifyErrors);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMCull --
 *
 * 	Update the current netlist to remove nets that are already wired.
 *	This allows the user to hand-route some nets and then fix the netlist
 *	to skip those nets.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifys the current netlist information.
 *
 * ----------------------------------------------------------------------------
 */
int
NMCull()
{
    int nmwCullNetFunc();

    /* This implementation is the complement of the verify command.  Instead
     * of finding bad nets and reporting them, find good nets and remove them.
     */
    nmwCullDone = 0;  /* Number of correctly wired nets */
    (void) NMEnumNets(nmwCullNetFunc, (ClientData) NULL);

    if (nmwCullDone == 0)
	TxPrintf("No fully-wired nets found.\n");
    else
    if (nmwCullDone == 1)
	TxPrintf("One fully-wired net deleted from the netlist.\n");
    else
	TxPrintf("%d fully-wired nets deleted from the netlist.\n",
		nmwCullDone);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwCullNetFunc --
 *
 * 	Net enumeration function called during netlist updating.  Ignores all
 *	but the first terminals in nets.  Check to see if a net is wired
 *	correctly.  If so, remove the net from the netlist.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
int
nmwCullNetFunc(name, first)
    char *name;	/* Name of a terminal in a net */
    bool first;	/* TRUE => first terminal of a net */
{
    int i;

    if (!first) return 0;

    /* The implementation parallels nmwVerifyNetFunc.  Begin by collecting all
     * terminals reachable from the current terminal, using wiring in the edit
     * cell and feedthroughs in children (if there are multiple terminals by
     * the same name, they are assumed to be connected).
     */
    nmwVerifyCount=0; /* Number of valid entries in temp array */
    (void) DBSrLabelLoc(EditCellUse, name, nmwVerifyLabelFunc,
	    (ClientData) NULL);

    /* Enumerate all terminals in the net and make sure that they got found
     * by the search.  NmwVerifyTermFunc takes care of errors and NULLs out
     * entries for terminals it finds.
     */
    nmwVerifyNetHasErrors = FALSE;	/* Set only in nmwVerifyTermFunc */
    (void) NMEnumTerms(name, nmwVerifyTermFunc, (ClientData) 0);

    /* If all terminals in the net were found on the list of terminals connected
     * to the first terminal, then check further to see if there are any short
     * circuits.  Any non-NULL names in nmwVerifyNames indicate short circuits.
     * Print errors for them, if any.
     */
    if (!nmwVerifyNetHasErrors)
    {
	for (i = 0; i < nmwVerifyCount; i++)
	{
	    if (nmwVerifyNames[i] != NULL)
	    {
		Rect biggerArea;
		char msg[200];

		TxError("Net \"%s\" shorted to net \"%s\".\n",
			name, nmwVerifyNames[i]);
		GEO_EXPAND(&nmwVerifyAreas[i], 1, &biggerArea);
		(void) sprintf(msg, "Net \"%.80s\" shorted to net \"%.80s\".\n",
			name, nmwVerifyNames[i]);
		DBWFeedbackAdd(&biggerArea, msg, EditCellUse->cu_def,
			1, STYLE_PALEHIGHLIGHTS);
		break;
	    }
	}
	/* If we made it all the way through the loop then all terminals in
	 * the net were found and there were no errors.  In this case it is
	 * okay to delete the net from the netlist.
	 */
	if( i == nmwVerifyCount )
	{
	    nmwCullDone += 1;	/* Count the number of nets deleted */
	    NMDeleteNet(name);
	}
    }

    return 0;
}


#ifdef ROUTE_MODULE

/*
 * ----------------------------------------------------------------------------
 *
 * NMMeasureNet --
 *
 * 	Collect statistics on the current selection, presumably a routed wire:
 *	find the total metal and poly areas in the selection and divide by the
 *	widths of these layers.  Add via widths to the total wire length.
 *	The user must watch out for nets split by feed-throughs: the selection
 *	may not be the entire net!
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increments wire length counters.
 *
 * ----------------------------------------------------------------------------
 */
void
NMMeasureNet()
{
    int nmMeasureFunc();
    TileTypeBitMask nmMeasureMask;

    nmMArea = nmPArea = nmVCount = 0;
    TTMaskZero(&nmMeasureMask);
    TTMaskSetType(&nmMeasureMask, RtrMetalType);
    TTMaskSetType(&nmMeasureMask, RtrPolyType);
    TTMaskSetType(&nmMeasureMask, RtrContactType);
    (void) SelEnumPaint(&nmMeasureMask, TRUE, (bool *) NULL,
	    nmMeasureFunc, (ClientData) NULL);
    TxPrintf("Total: %d;  Metal: %d;  Poly: %d;  Vias: %d\n",
	nmMArea/RtrMetalWidth +nmPArea/RtrPolyWidth +nmVCount*RtrContactWidth,
	nmMArea/RtrMetalWidth,  nmPArea/RtrPolyWidth, nmVCount);
}

	/* ARGSUSED */
int
nmMeasureFunc(r, type, clientData)
    Rect *r;
    TileType type;
    ClientData clientData;
{
    if(type == RtrMetalType)
	nmMArea=nmMArea+(r->r_xtop-r->r_xbot)*(r->r_ytop-r->r_ybot);
    else
    if(type == RtrPolyType)
	nmPArea=nmPArea+(r->r_xtop-r->r_xbot)*(r->r_ytop-r->r_ybot);
    else
    if(type == RtrContactType)
	nmVCount++;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMMeasureAll --
 *
 * 	Measure the length of all wiring attached to all terminals.  This
 *	implementation uses the verify code to enumerate all of the tiles
 *	connected to any terminal of each net.  This routine calls the ones
 *	that do the real work.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints the value.
 *
 * ----------------------------------------------------------------------------
 */
void
NMMeasureAll(fp)
    FILE * fp;
{
    int nmAllFunc();

    nmMArea = nmPArea = nmVCount= 0;
    (void) NMEnumNets(nmAllFunc, (ClientData) fp);
    TxPrintf("Total: %d;  Metal: %d;  Poly: %d;  Vias: %d\n",
	nmMArea/RtrMetalWidth +nmPArea/RtrPolyWidth +nmVCount*RtrContactWidth,
	nmMArea/RtrMetalWidth,  nmPArea/RtrPolyWidth, nmVCount);
}

	/* ARGSUSED */
int
nmAllFunc(name, firstInNet, fp)
    char *name;
    bool firstInNet;
    FILE * fp;
{
    void nmwMeasureTileFunc();
    int saveM, saveP, saveV;

    /* Trace out all wiring connected to the net.  Only process the first
     * terminal in each net.
     */
    if (!firstInNet) return 0;

    /* Save old values for counters in case they need to be written to
     * a log file.
     */
    saveM=nmMArea; saveP=nmPArea; saveV=nmVCount;

    nmwVerifyCount = 0;
    nmMeasureCount = 0;
    (void) DBSrLabelLoc(EditCellUse, name, nmwVerifyLabelFunc,
	    (ClientData) nmwMeasureTileFunc);

    if (fp!=NULL)
    {
    /* Write the wire length statistics for this net to a file.
     */
	saveM=nmMArea-saveM;
	saveP=nmPArea-saveP;
	saveV=nmVCount-saveV;
        fprintf(fp,"Net %s total: %d;  Metal: %d;  Poly: %d;  Vias: %d\n",
		name,
	        saveM/RtrMetalWidth +saveP/RtrPolyWidth +saveV*RtrContactWidth,
	        saveM/RtrMetalWidth,  saveP/RtrPolyWidth, saveV);
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmwMeasureTileFunc --
 *
 * 	This routine is passed to nmwVerifyTileFunc and is called for each
 *	tile connected to some terminal in a net.  If the tile is on a
 *	routing layer and not seen before, then add the tile to the current
 *	routing measurements.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increments the routing statistics variables.
 *
 * ----------------------------------------------------------------------------
 */
void
nmwMeasureTileFunc(tile)
    Tile * tile;
{
    int i;
    Rect r;
    TileType tt;

    /* Process a tile if it is on a routing layer */

    tt=TiGetType(tile);
    if ((tt==RtrMetalType) || (tt==RtrPolyType) || (tt==RtrContactType))
    {
	/* Check to see if the tile is already in the table.  If not, then
	 * process it to add it into the statistics being collected.
	 */
	for (i = 0; i < nmMeasureCount; i++)
	   if (nmMeasureTiles[i] == tile)
	      return;

	TiToRect(tile, &r);

	/* Add the tile's area to the globals for routing statistics.
	 */
	if( tt == RtrMetalType )
	    nmMArea = nmMArea +(r.r_xtop -r.r_xbot) *(r.r_ytop -r.r_ybot);
	else
	if( tt == RtrPolyType )
	    nmPArea = nmPArea +(r.r_xtop- r.r_xbot) *(r.r_ytop -r.r_ybot);
	else
	    nmVCount++;

	/* Add the tile to the array of tiles already seen.
	 */
	if (nmMeasureCount == nmMeasureSize)
	{
	/* Not enough space to hold this tile.  Allocate a new array
	 * and copy the old one to the new.
	 */
	    Tile ** newTiles;
	    int newSize;

	    newSize = 2 * nmMeasureSize;
	    if (newSize < 16) newSize = 16;
	    newTiles =
		(Tile **) mallocMagic((unsigned) newSize * sizeof(Tile *));
	    for (i = 0; i < nmMeasureSize; i++)
		newTiles[i] = nmMeasureTiles[i];

	    if (nmMeasureSize != 0)
		freeMagic((char *) nmMeasureTiles);
	
	    nmMeasureTiles = newTiles;
		nmMeasureSize = newSize;
	}
	nmMeasureTiles[nmMeasureCount++]=tile;
    }
}

#endif	/* ROUTE_MODULE */
