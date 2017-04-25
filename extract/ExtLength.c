/*
 * ExtLength.c --
 *
 * Circuit extraction.
 * Computation of the length of the shortest path from a driver
 * to a receiver.  This information is intended to be used in
 * computing delays of signals propagating in a transmission
 * line mode, where delay is proportional to the driver-to-receiver
 * distance.
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
 *		      Lawrence Livermore National Laboratory
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtLength.c,v 1.3 2009/09/10 20:32:52 tim Exp $";
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
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "utils/stack.h"
#include "utils/main.h"
#include "utils/utils.h"

/* Temporary cell for holding an entire flattened net */
CellDef *extPathDef = NULL;
CellUse *extPathUse = NULL;

/*
 * Tables that hold information describing each driver and receiver
 * in the circuit.
 *
 * Each entry in the driver table will be initially 0, and later
 * will be made to point to a list of hierarchical labels (i.e.,
 * specially constructed labels whose text field contains a full
 * hierarchical pathname) where the driver label appears in the
 * design.
 *
 * Each entry in the receiver table is initially 0, and is set to 1
 * when that receiver is processed as being connected to some driver.
 */
HashTable extDriverHash;
HashTable extReceiverHash;

/* Initial size of the hash tables used in this file */
#define	INITHASHSIZE	32

/* Max length of a hierarchical name */
#define	MAXNAMESIZE	2048

/*
 * List of labels being built up hierarchically by extLengthYank().
 * This is used within this file to pass data down to filter
 * procedures of search functions.
 */
static Label *extLengthLabelList;

/*
 * Used to hold information while tracing out paths.
 * Passed directly down to filter procedures of search functions.
 */
struct extPathArg
{
    int		 epa_min, epa_max;
    int		 epa_pNum;
    Label	*epa_lab1, *epa_lab2;
};

/*
 * Additional information passed down to extPathFloodFunc()
 */
struct extPathFloodArg
{
    int			 epfa_distance;
    Point		*epfa_srcPoint;
    Tile		*epfa_srcTile;
    Rect		 epfa_srcArea;
    struct extPathArg	*epfa_epa;
};

/* Used to mark tiles during path tracing */
#define	MARKED	((ClientData) 1)

/* Forward declarations */
Label *extPathLabel();
Label *extLengthYank();
int extLengthLabels();
int extLengthLabelsFunc();
int extPathPairFunc();
int extPathResetClient();
int extPathFloodFunc();

void extLengthInit();
void extPathPairDistance();
void extPathFlood();
void extPathFloodTile();


/*
 * ----------------------------------------------------------------------------
 *
 * ExtSetDriver --
 * ExtSetReceiver --
 *
 * Add a terminal name to either the driver or the receiver table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds an entry to the hash tables extDriverHash or extReceiverHash
 *	respectively.  The initial value of the hash entry is 0.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtSetDriver(name)
    char *name;
{
    HashEntry *he;

    he = HashFind(&extDriverHash, name);
    HashSetValue(he, 0);
}

void
ExtSetReceiver(name)
    char *name;
{
    HashEntry *he;

    he = HashFind(&extReceiverHash, name);
    HashSetValue(he, 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtLengthClear --
 *
 * Kill extDriverHash and extReceiverHash, and re-initialize them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtLengthClear()
{
    HashKill(&extDriverHash);
    HashKill(&extReceiverHash);
    extLengthInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * extLengthInit --
 *
 * Allocates and initializes the hash tables extDriverHash
 * and extReceiverHash.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
extLengthInit()
{
    HashInit(&extDriverHash, INITHASHSIZE, 0);
    HashInit(&extReceiverHash, INITHASHSIZE, 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extLength --
 *
 * Using the driver and receiver tables, compute the distances from
 * each driver to each receiver on its (flattened) net.  Output to
 * the file 'f' lines of the following format:
 *
 *	distance drivername receivername min max
 *
 * e.g,
 *
 *	distance a/b/cOUT d/e/fIN 1234 2345
 *
 * The units of distance are lambda.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Outputs to the FILE 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
extLength(rootUse, f)
    CellUse *rootUse;	/* The names stored in the driver and receiver tables
			 * should all be relative to this root cell.  It is
			 * the responsibility of the caller to ensure this.
			 */
    FILE *f;		/* Open output file */
{
    Label *dList, *rList, *dLab, *rLab;
    int min, max;
    HashSearch hs;
    HashEntry *he;

    /* Create the yank cell if it doesn't already exist */
    if (extPathDef == (CellDef *) NULL)
	DBNewYank("__PATHYANK__", &extPathUse, &extPathDef);

    /*
     * Initialize the entries in the driver table to point to
     * a list of hierarchical labels describing the locations
     * where that driver appears.  These labels should all
     * be from a single cell.
     */
    HashStartSearch(&hs);
    while (he = HashNext(&extDriverHash, &hs))
    {
	dList = extPathLabel(rootUse, he->h_key.h_name);
	HashSetValue(he, (ClientData) dList);
    }

    /*
     * Main loop.
     * For each driver, find all the receivers connected to it, and
     * then compute and output the distance to each.
     */
    HashStartSearch(&hs);
    while (he = HashNext(&extDriverHash, &hs))
    {
	/* Ignore drivers whose labels couldn't be found */
	dList = (Label *) HashGetValue(he);
	if (dList == (Label *) NULL)
	    continue;

	/*
	 * Flatten this net into extPathDef.
	 * Find all the labels that overlap material we yanked and
	 * whose names appear in the receiver table.  Build a hierarchical
	 * list of these labels.
	 */
	rList = extLengthYank(rootUse, dList);

	/*
	 * Now compute the distance from the driver label to
	 * each of the receivers.  Free each driver label
	 * as it is processed.
	 */
	for (dLab = dList; dLab; dLab = dLab->lab_next)
	{
	    for (rLab = rList; rLab; rLab = rLab->lab_next)
	    {
		extPathPairDistance(dLab, rLab, &min, &max);
		fprintf(f, "distance %s %s %d %d\n",
			dLab->lab_text, rLab->lab_text, min, max);
	    }

	    /* Free the driver label */
	    freeMagic((char *) dLab);
	}

	/* Free all the receiver labels built up during this iteration */
	for (rLab = rList; rLab; rLab = rLab->lab_next)
	    freeMagic((char *) rLab);

	/* For sanity since we've freed the driver label list */
	HashSetValue(he, (ClientData) NULL);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * extLengthYank --
 *
 * Trace out all material connected to each location on the label
 * list 'labList', both in the root cell use->cu_def and hierarchically
 * in all of its children.  Flatten this material into the cell
 * 'extPathDef'.
 *
 * Results:
 *	Returns a list of the hierarchical labels whose names appear
 *	in the receiver table (extReceiverHash) that are overlapped
 *	by material we yanked.
 *
 * Side effects:
 *	Adds material to extPathDef after erasing its previous contents.
 *
 * ----------------------------------------------------------------------------
 */

Label *
extLengthYank(use, labList)
    CellUse *use;	/* Cell whose material is to be traced */
    Label *labList;	/* List of labels whose attached net is to be traced */
{
    SearchContext scx;
    char mesg[512];
    Label *lab;
    int pNum;

    /* Eliminate previous contents of yank cell */
    if (DebugIsSet(extDebugID, extDebLength))
    {
	DBReComputeBbox(extPathDef);
	DBWAreaChanged(extPathDef, &extPathDef->cd_bbox, DBW_ALLWINDOWS,
		    &DBAllButSpaceBits);
    }
    DBCellClearDef(extPathDef);

    /*
     * Search out all material connected to each label.
     * Bloat the label's rectangle to consider even material
     * that only touches the label.
     */
    for (lab = labList; lab; lab = lab->lab_next)
    {
	if (lab->lab_type == TT_SPACE)
	    continue;
	scx.scx_use = use;
	scx.scx_trans = GeoIdentityTransform;
	GEO_EXPAND(&lab->lab_rect, 1, &scx.scx_area);
	DBTreeCopyConnect(&scx, &DBConnectTbl[lab->lab_type], 0,
		DBConnectTbl, &TiPlaneRect, extPathUse);
    }

    if (DebugIsSet(extDebugID, extDebLength))
    {
	DBReComputeBbox(extPathDef);
	DBWAreaChanged(extPathDef, &extPathDef->cd_bbox, DBW_ALLWINDOWS,
		    &DBAllButSpaceBits);
	WindUpdate();
	(void) sprintf(mesg, "Yanked %s",
		labList ? labList->lab_text : "(NONE)");
	TxMore(mesg);
    }

    /*
     * Now find all the labels appearing in the receiver table that are
     * overlapped by any of the material we just yanked.  This may not
     * be the most efficient way to do things: we're searching the label
     * list of at least the root cell every time we process a tile in
     * the yanked net.  The hope is that this is still fast enough.
     * Possibly a better way would be to identify the CELLS that are
     * overlapped by tiles in the net, and then to process each label
     * list just once.
     */
    extLengthLabelList = (Label *) NULL;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	(void) DBSrPaintArea((Tile *) NULL, extPathDef->cd_planes[pNum],
		&TiPlaneRect, &DBAllButSpaceBits, extLengthLabels,
		(ClientData) use);
    }

    return (extLengthLabelList);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extLengthLabels --
 *
 * Called for each paint tile in extPathDef to find all the labels
 * in the original search tree that overlap the tile.  We bloat each
 * tile to the top and right by one unit to be certain to catch
 * labels appearing on these edges.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	May cons hierarchical labels (newly created labels whose text
 *	is the full hierarchical path of a label in a subcell) to the
 *	list extLengthLabelList.
 *
 * ----------------------------------------------------------------------------
 */

int
extLengthLabels(tile, rootUse)
    Tile *tile;	/* Some tile in extPathDef */
    CellUse *rootUse;		/* The original root cell */
{
    char name[MAXNAMESIZE];
    TileTypeBitMask mask;
    TerminalPath tpath;
    SearchContext scx;

    /* Grow the search area to include labels on the top and right */
    TITORECT(tile, &scx.scx_area);
    scx.scx_area.r_xtop++;
    scx.scx_area.r_ytop++;
    scx.scx_use = rootUse;
    scx.scx_trans = GeoIdentityTransform;
    tpath.tp_first = tpath.tp_next = name;
    tpath.tp_last = &name[sizeof name - 2];

    TTMaskSetOnlyType(&mask, TiGetType(tile));
    (void) DBTreeSrLabels(&scx, &mask, 0, &tpath, TF_LABEL_ATTACH,
		extLengthLabelsFunc, (ClientData) NULL);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extLengthLabelsFunc --
 *
 * Called for each label found while searching hierarchically the area
 * beneath one of the tiles in extPathDef.  If the hierarchical label
 * name matches a name appearing in the receiver table (extReceiverHash)
 * we cons a newly created hierarchical label onto the front of
 * extLengthLabelList.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	May cons hierarchical labels (newly created labels whose text
 *	is the full hierarchical path of a label in a subcell) to the
 *	list extLengthLabelList.  Also, for each receiver label we
 *	did find, leaves a value of 1 (via HashSetValue()) in the
 *	receiver hash table, so we can know at the end which receiver
 *	labels weren't driven by any driver.
 *
 * ----------------------------------------------------------------------------
 */

int
extLengthLabelsFunc(scx, label, tpath)
    SearchContext *scx;		/* Where in the search tree we are */
    Label *label;		/* The label itself */
    TerminalPath *tpath;	/* Identifies hierarchical prefix for label.
				 * The full hierarchical pathname will be the
				 * concatenation of the string tpath->tp_first
				 * and the string label->lab_text.
				 */
{
    Label *newLab;
    HashEntry *he;
    int len;

    /* Concatenate the prefix and label to get the full hierarchical name */
    (void) strcpy(tpath->tp_next, label->lab_text);

    /* Only bother with labels in the receiver table */
    he = HashLookOnly(&extReceiverHash, tpath->tp_first);
    if (he == NULL)
	return (0);

    /* Mark this receiver as being seen */
    HashSetValue(he, (ClientData) 1);

    /* Allocate and fill in a new hierarchical label */
    len = strlen(tpath->tp_first) + sizeof (Label)
	- sizeof newLab->lab_text + 1;
    newLab = (Label *) mallocMagic((unsigned) len);
    newLab->lab_type = label->lab_type;
    newLab->lab_just = GeoTransPos(&scx->scx_trans, label->lab_just);
    GeoTransRect(&scx->scx_trans, &label->lab_rect, &newLab->lab_rect);
    newLab->lab_next = extLengthLabelList;
    extLengthLabelList = newLab;
    (void) strcpy(newLab->lab_text, tpath->tp_first);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathLabel --
 *
 * Find all the locations of labels matching the hierarchical
 * name 'text' and return a linked list of newly allocated
 * labels with the full hierarchical name.
 *
 * Results:
 *	Returns a pointer to the newly allocated Label list
 *
 * Side effects:
 *	Allocates memory.
 *	Complains if the label couldn't be found.
 *
 * ----------------------------------------------------------------------------
 */

Label *
extPathLabel(use, text)
    CellUse *use;
    char *text;
{
    int extPathLabelFunc();
    Label *lab;

    lab = (Label *) NULL;
    (void) DBSrLabelLoc(use, text, extPathLabelFunc, (ClientData) &lab);
    if (lab == NULL)
	TxError("Can't find terminal \"%s\"\n", text);
    return (lab);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathLabelFunc --
 *
 * Called via DBSrLabelLoc() on behalf of extPathLabel() above.
 * Creates a Label whose text is the string pointed to by text,
 * whose lab_rect is *rect, and whose type is childLab->lab_type.
 * Cons it onto the front of the list *pLabList.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

int
extPathLabelFunc(rect, text, childLab, pLabList)
    Rect *rect;		/* Transformed location of the label */
    char *text;		/* Full hierarchical name of the label */
    Label *childLab;	/* The label itself */
    Label **pLabList;	/* Cons the newly allocated label onto the front of
			 * this list.
			 */
{
    Label *lab;
    int len;

    len = strlen(text) + sizeof (Label) - sizeof lab->lab_text + 1;
    lab = (Label *) mallocMagic((unsigned) len);
    lab->lab_type = childLab->lab_type;
    lab->lab_rect = *rect;
    lab->lab_just = GEO_CENTER;	/* Irrelevant */
    lab->lab_next = *pLabList;

    /* Cons to front of list */
    *pLabList = lab;
    (void) strcpy(lab->lab_text, text);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathPairDistance --
 *
 * Compute the actual delay between two locations 'lab1' and 'lab2'.
 * The delay is computed using optimistic and pessimistic assumptions
 * to get a min and max delay respectively.
 *
 * Algorithm:
 *	The algorithm we use here is simplistic, assuming that all
 *	wires are of essentially uniform width.  We use a tile-based
 *	depth-first flooding algorithm that computes a delay on the
 *	forward pass based on the wire's length.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the min and max delay in *pMin and *pMax respectively.
 *	Uses the ti_client fields of the tiles in extPathDef, since
 *	these are used while tracing out paths.
 *
 * ----------------------------------------------------------------------------
 */

void
extPathPairDistance(lab1, lab2, pMin, pMax)
    Label *lab1, *lab2;
    int *pMin, *pMax;
{
    struct extPathArg epa;
    TileTypeBitMask mask;
    PlaneMask pMask;
    int pNum;
    Rect r;

    /* Skip if either type is space (sanity check) */
    if (lab1->lab_type == TT_SPACE || lab2->lab_type == TT_SPACE)
	return;

    /* Include all tiles touching lab1 that are connected to it */
    GEO_EXPAND(&lab1->lab_rect, 1, &r);
    mask = DBConnectTbl[lab1->lab_type];

    /*
     * Find min and max delays, considering each plane that
     * lab1 is connected to.  Don't reset the ti_client fields
     * until after we've found all paths.
     */
    epa.epa_min = INFINITY;
    epa.epa_max = MINFINITY;
    epa.epa_lab1 = lab1;
    epa.epa_lab2 = lab2;
    pMask = DBTechTypesToPlanes(&mask);
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(pMask, pNum))
	{
	    epa.epa_pNum = pNum;
	    (void) DBSrPaintClient((Tile *) NULL, extPathDef->cd_planes[pNum],
		    &r, &mask, (ClientData) CLIENTDEFAULT,
		    extPathPairFunc, (ClientData) &epa);
	}

    /* Pass the min and max delay back to our caller */
    *pMin = epa.epa_min;
    *pMax = epa.epa_max;

    /* Reset ti_client fields in tiles */
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	(void) DBSrPaintClient((Tile *) NULL, extPathDef->cd_planes[pNum],
		&TiPlaneRect, &DBAllButSpaceBits, MARKED,
		extPathResetClient, (ClientData) NULL);
}

/*
 * ----------------------------------------------------------------------------
 * extPathResetClient --
 *
 * Called by DBSrPaintClient() on behalf of extPathPairDistance()
 * above to reset each tile's ti_client field to CLIENTDEFAULT.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Sets tile->ti_client to CLIENTDEFAULT.
 * ----------------------------------------------------------------------------
 */

int
extPathResetClient(tile)
    Tile *tile;
{
    tile->ti_client = (ClientData) CLIENTDEFAULT;
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathPairFunc --
 *
 * Called by DBSrPaintClient() on behalf of extPathPairDistance()
 * above for each unprocessed tile (ti_client == CLIENTDEFAULT)
 * overlapped by epa->epa_lab1 that is connected to it.
 * Floods outward in depth-first search toward the destination
 * epa->epa_lab2.  Remembers the min and max delay to the
 * destination in epa->epa_min and epa->epa_max.
 *
 * We use depth-first search instead of breadth-first because
 * it's easier, we need to consider both the longest and shortest
 * path, and we expect there to be only a few paths.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See above.
 *	Marks the ti_client fields of the tiles we visit as MARKED.
 *
 * ----------------------------------------------------------------------------
 */

int
extPathPairFunc(tile, epa)
    Tile *tile;
    struct extPathArg *epa;
{
    Point startPoint;
    Rect r;

    /*
     * Visit all this tile's neighbors.
     * Our initial delay is zero, and our initial starting point
     * is in the center of the overlap of epa->epa_lab1 and tile.
     */
    TITORECT(tile, &r);
    GEOCLIP(&r, &epa->epa_lab1->lab_rect);
    startPoint.p_x = (r.r_xtop + r.r_xbot) / 2;
    startPoint.p_y = (r.r_ytop + r.r_ybot) / 2;
    extPathFlood(tile, &startPoint, 0, epa);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathFlood --
 *
 * Flood from a tile to all its connected and unprocessed neighbors.
 * As we flood to each neighbor, we estimate a delay for the increment
 * from the point 'p' (usually on the center of 'tile', and at distance
 * 'distance' from the starting point) to the central point of the new
 * tile.  Contacts are processed in a similar way, except the point 'p'
 * doesn't change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
extPathFlood(tile, p, distance, epa)
    Tile *tile;	/* Tile whose neighbors we are to visit */
    Point *p;			/* Usually at center of 'tile' */
    int distance;		/* Distance to 'p' */
    struct extPathArg *epa;	/* Update epa_min and epa_max when we reach
				 * the destination epa_lab2.
				 */
{
    TileType type = TiGetType(tile);
    Label *lab2 = epa->epa_lab2;
    int pNum, newdistance;
    PlaneMask pMask;
    Tile *tp;
    char mesg[512];
    Point p2;
    Rect r;

    /* Mark the tile as being visited */
    tile->ti_client = MARKED;

    /*
     * Are we at the destination yet?
     * If so, compute final delay and just return.
     * Update the min and max delay if necessary.
     * Don't propagate to neighboring tiles since doing so
     * can only lengthen the path.
     */
    TITORECT(tile, &r);
    if (DebugIsSet(extDebugID, extDebLength))
    {
	ShowRect(extPathDef, &r, STYLE_SOLIDHIGHLIGHTS);
	TxMore("Visit tile");
	ShowRect(extPathDef, &r, STYLE_ERASEHIGHLIGHTS);
    }

    if (GEO_TOUCH(&r, &lab2->lab_rect) && DBConnectsTo(type, lab2->lab_type))
    {
	/* Find distance to closest point in 'lab2->lab_rect' to 'p' */
	p2 = *p;
	GeoClipPoint(&p2, &lab2->lab_rect);
	newdistance = extPathTileDist(p, &p2, tile, distance);

	if (DebugIsSet(extDebugID, extDebLength))
	{
	    (void) sprintf(mesg, "Reached destination, dist = %d", newdistance);
	    TxMore(mesg);
	}

	/* Update min and max distance */
	if (newdistance < epa->epa_min) epa->epa_min = newdistance;
	if (newdistance > epa->epa_max) epa->epa_max = newdistance;
	return;
    }

    /* Walk around the perimeter to connected tiles */

	/* TOP */
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
	if (tp->ti_client != MARKED && DBConnectsTo(TiGetType(tp), type))
	    extPathFloodTile(tile, p, distance, tp, epa);

	/* RIGHT */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
	if (tp->ti_client != MARKED && DBConnectsTo(TiGetType(tp), type))
	    extPathFloodTile(tile, p, distance, tp, epa);

	/* BOTTOM */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
	if (tp->ti_client != MARKED && DBConnectsTo(TiGetType(tp), type))
	    extPathFloodTile(tile, p, distance, tp, epa);

	/* LEFT */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
	if (tp->ti_client != MARKED && DBConnectsTo(TiGetType(tp), type))
	    extPathFloodTile(tile, p, distance, tp, epa);

    /* Try connections to other planes */
    if (DBIsContact(type))
    {
	int saveplane = epa->epa_pNum;
	PlaneMask pMask = DBConnPlanes[type];
	pMask &= ~(PlaneNumToMaskBit(epa->epa_pNum));

	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pMask, pNum))
	    {
		Plane *plane = extPathDef->cd_planes[pNum];
		 
		/* Find the point on the new plane */
		tp = plane->pl_hint;
		GOTOPOINT(tp, &tile->ti_ll);
		plane->pl_hint = tp;

		/* If not yet visited, process tp */
		if (tp->ti_client == (ClientData) CLIENTDEFAULT
			&& DBConnectsTo(type, TiGetType(tp)))
		{
		    epa->epa_pNum = pNum;
		    extPathFlood(tp, p, distance, epa);
		}
	    }
	epa->epa_pNum = saveplane;
    }

    /*
     * The hairiest case is when this type connects to stuff on
     * other planes, but isn't itself connected as a contact.
     * For example, a CMOS pwell connects to diffusion of the
     * same doping (p substrate diff).  In a case like this,
     * we need to search the entire AREA of the tile plus a
     * 1-lambda halo to find everything it overlaps or touches
     * on the other plane.
     */
    if (pMask = DBAllConnPlanes[type])
    {
	int saveplane = epa->epa_pNum;
	struct extPathFloodArg epfa;
	Rect biggerArea;

	TITORECT(tile, &epfa.epfa_srcArea);
	GEO_EXPAND(&epfa.epfa_srcArea, 1, &biggerArea);
	epfa.epfa_distance = distance;
	epfa.epfa_epa = epa;
	epfa.epfa_srcPoint = p;
	epfa.epfa_srcTile = tile;
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (pNum != epa->epa_pNum && PlaneMaskHasPlane(pMask, pNum))
	    {
		epa->epa_pNum = pNum;
		(void) DBSrPaintClient((Tile *) NULL,
			extPathDef->cd_planes[pNum], &biggerArea,
			&DBConnectTbl[type], (ClientData) CLIENTDEFAULT,
			extPathFloodFunc, (ClientData) &epfa);
	    }
	epa->epa_pNum = saveplane;
    }
}

int
extPathFloodFunc(dstTile, epfa)
    Tile *dstTile;
    struct extPathFloodArg *epfa;
{
    Rect srcRect, dstRect;
    Point dstPoint, *p;
    int dstDist;

    /*
     * If dstTile overlaps epfa->epfa_srcArea, use epfa->epfa_srcPoint;
     * otherwise, pick a point along the boundary of epfa->epfa_srcArea
     * that's in common with dstTile.
     */
    dstDist = epfa->epfa_distance;
    srcRect = epfa->epfa_srcArea;
    TITORECT(dstTile, &dstRect);
    if (GEO_OVERLAP(&srcRect, &dstRect))
	p = epfa->epfa_srcPoint;
    else
    {
	/* Pick a point along the boundary */
	GEOCLIP(&srcRect, &dstRect);
	dstPoint.p_x = (srcRect.r_xbot + srcRect.r_xtop) / 2;
	dstPoint.p_y = (srcRect.r_ybot + srcRect.r_ytop) / 2;

	/* Compute the incremental delay */
	dstDist = extPathTileDist(epfa->epfa_srcPoint, &dstPoint,
			epfa->epfa_srcTile, dstDist);
	p = &dstPoint;
    }

    /* Recurse */
    extPathFlood(dstTile, p, dstDist, epfa->epfa_epa);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathFloodTile --
 *
 * Propagate from a tile 'srcTile' to one of its neighbors 'dstTile'.
 * The delay 'srcDelay' has been computed to 'srcPoint' (which is
 * contained within 'srcTile' or is on its border).  We pick the
 * midpoint of the overlap between 'srcTile' and 'dstTile' as the
 * point to which the next cost is computed, and then recursively
 * call extPathFlood() with dstTile and the new point.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
extPathFloodTile(srcTile, srcPoint, srcDist, dstTile, epa)
    Tile *srcTile;	/* Tile through which we're propagating */
    Point *srcPoint;		/* Point inside or on srcTile */
    int srcDist;		/* Distance to srcPoint so far */
    Tile *dstTile;	/* Tile on border of srcTile */
    struct extPathArg *epa;
{
    Rect srcRect, dstRect;
    Point dstPoint;
    int dstDist;

    /*
     * Pick the central point along the boundary of srcTile and dstTile
     * for purposes of computing costs.
     */
    TITORECT(srcTile, &srcRect);
    TITORECT(dstTile, &dstRect);
    GEOCLIP(&srcRect, &dstRect);
    dstPoint.p_x = (srcRect.r_xbot + srcRect.r_xtop) / 2;
    dstPoint.p_y = (srcRect.r_ybot + srcRect.r_ytop) / 2;

    /* Compute the incremental delay */
    dstDist = extPathTileDist(srcPoint, &dstPoint, srcTile, srcDist);

    /* Recurse */
    extPathFlood(dstTile, &dstPoint, dstDist, epa);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extPathTileDist --
 *
 * Update delay information to include the costs of passing through
 * 'tile' from p1 to p2.  The old distance is 'oldDist'.  We account
 * for the distance from p1 to p2 through the tile 'tile'.
 *
 * Results:
 *	Returns the distance described above.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
extPathTileDist(p1, p2, tile, oldDist)
    Point *p1, *p2;
    Tile *tile;
    int oldDist;
{
    int newDist;

    newDist = oldDist + ABSDIFF(p1->p_x, p2->p_x) + ABSDIFF(p1->p_y, p2->p_y);

    /*
     * If both points were on the same side, include a little extra
     * for passing through the middle of the tile (which wasn't counted).
     */
    if (p1->p_x == p2->p_x)
    {
	if (p1->p_x == LEFT(tile) || p1->p_x == RIGHT(tile))
	    newDist += RIGHT(tile) - LEFT(tile);
    }
    if (p1->p_y == p2->p_y)
    {
	if (p1->p_y == BOTTOM(tile) || p1->p_y == TOP(tile))
	    newDist += TOP(tile) - BOTTOM(tile);
    }

    return (newDist);
}
