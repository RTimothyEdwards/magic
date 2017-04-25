/*
 * ExtSubtree.c --
 *
 * Circuit extraction.
 * Extracts interactions between subtrees of a parent and the
 * parent itself.  Does not handle extraction of interactions
 * arising between elements of the same array; those are handled
 * by the procedures in ExtArray.c
 *
 * The procedures in this file are not re-entrant.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtSubtree.c,v 1.3 2010/06/24 12:37:17 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

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

#ifdef	exactinteractions
/*
 * If "exactinteractions" is defined, we use an experimental algorithm
 * for finding exact interaction areas.  Currently it doesn't work too
 * well, so we leave it turned off.
 */
int ExtInterBloat = 10;
#endif	/* exactinteractions */

/* Imports from elsewhere in this module */
extern int extHierYankFunc();
extern LabRegion *extSubtreeHardNode();
extern Node *extHierNewNode();
extern ExtTree *extHierNewOne();

/* Global data incremented by extSubtree() */
int extSubtreeTotalArea;	/* Total area of cell */
int extSubtreeInteractionArea;	/* Total area of all interactions, counting the
				 * entire area of the interaction each time.
				 */
int extSubtreeClippedArea;	/* Total area of all interactions, counting only
				 * the area that lies inside each chunk, so no
				 * area is counted more than once.
				 */

/* Local data */

    /* TRUE if processing the first subtree in an interaction area */
bool extFirstPass;

    /* Points to list of subtrees in an interaction area */
ExtTree *extSubList = (ExtTree *) NULL;

/* Forward declarations of filter functions */
char *extSubtreeTileToNode();
int extSubtreeFunc();
int extConnFindFunc();
int extSubtreeHardUseFunc();
int extHardProc();
int extSubtreeCopyPlane();
int extSubtreeShrinkPlane();
void extSubtreeInteraction();
void extSubtreeAdjustInit();
void extSubtreeOutputCoupling();
void extSubtreeHardSearch();


/*
 * ----------------------------------------------------------------------------
 *
 * extSubtree --
 *
 * Do the hierarchical part of extracting the cell 'parentUse->cu_def'.
 * This consists of finding all connections either between geometry in the
 * parent and geometry in a subcell, or between geometry in two overlapping
 * or adjacent subcells.
 *
 * This procedure only finds interaction areas, where subcells are close
 * to each other or to mask information, and then calls extSubtreeInteraction()
 * to do the real work.  See the comments there for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Outputs connections and adjustments to the file 'f'.
 *	There are two kinds of records:
 *
 *		merge node1 node2 deltaC deltaP1 deltaA1 deltaP2 deltaA2 ...
 *		cap node1 node2 deltaC
 *
 *	The first merges node1 and node2, adjusts the substrate capacitance
 *	by adding deltaC (usually negative), and the node perimeter and area
 *	for each resistive layer n by deltaPn deltaAn.
 *
 *	The second adjusts the coupling capacitance between node1 and node2
 *	by deltaC, which may be positive or negative.
 *
 * ----------------------------------------------------------------------------
 */

#define	RECTAREA(r)	(((r)->r_xtop-(r)->r_xbot) * ((r)->r_ytop-(r)->r_ybot))

void
extSubtree(parentUse, reg, f)
    CellUse *parentUse;
    NodeRegion *reg;		/* Node regions of the parent cell */
    FILE *f;
{
    int extSubtreeInterFunc();
    CellDef *def = parentUse->cu_def;
    int halo = ExtCurStyle->exts_sideCoupleHalo	 + 1;
    HierExtractArg ha;
    Rect r, rlab, rbloat, *b;
    Label *lab;
    bool result;

    if ((ExtOptions & (EXT_DOCOUPLING|EXT_DOADJUST))
		   != (EXT_DOCOUPLING|EXT_DOADJUST))
	halo = 1;

    /*
     * The cumulative buffer is initially empty.  It will be built up
     * for each interaction area, and then cleared before processing
     * the next one.
     *
     * The connection hash table is initialized here but doesn't get
     * cleared until the end.  It is responsible for changes to the
     * node structure over the entire cell 'def'.
     */
    extSubtreeTotalArea += RECTAREA(&def->cd_bbox);
    ha.ha_outf = f;
    ha.ha_parentUse = parentUse;
    ha.ha_parentReg = reg;
    ha.ha_nodename = extSubtreeTileToNode;
    ha.ha_cumFlat.et_use = extYuseCum;
    HashInit(&ha.ha_connHash, 32, 0);

#ifndef	exactinteractions
    /*
     * Cookie-cutter up def into pieces ExtCurStyle->exts_stepSize by
     * ExtCurStyle->exts_stepSize.
     * Find all interaction areas (within halo units distance, where
     * halo has been set above to reflect the maximum distance for
     * sidewall coupling capacitance).
     */
    b = &def->cd_bbox;
    for (r.r_ybot = b->r_ybot; r.r_ybot < b->r_ytop; r.r_ybot = r.r_ytop)
    {
	r.r_ytop = r.r_ybot + ExtCurStyle->exts_stepSize;
	for (r.r_xbot = b->r_xbot; r.r_xbot < b->r_xtop; r.r_xbot = r.r_xtop)
	{
	    r.r_xtop = r.r_xbot + ExtCurStyle->exts_stepSize;
	    if (SigInterruptPending)
		goto done;
	    rbloat = r;
	    rbloat.r_xbot -= halo, rbloat.r_ybot -= halo;
	    rbloat.r_xtop += halo, rbloat.r_ytop += halo;
	    result = DRCFindInteractions(def, &rbloat, halo, &ha.ha_interArea);

	    // Check area for labels.  Expand interaction area to include
	    // the labels.  This catches labels that are not attached to
	    // any geometry in the cell and therefore do not get flagged by
	    // DRCFindInteractions().

	    for (lab = def->cd_labels; lab; lab = lab->lab_next)
		if (GEO_OVERLAP(&lab->lab_rect, &r) || GEO_TOUCH(&lab->lab_rect, &r)) {
		    // Clip the label area to the area of rbloat
		    rlab = lab->lab_rect;
		    GEOCLIP(&rlab, &rbloat);
		    if (!result) {
			// If result == FALSE then ha.ha_interArea is invalid.
			ha.ha_interArea = rlab;
			result = TRUE;
		    }	
		    else
		        result |= GeoIncludeAll(&rlab, &ha.ha_interArea);
		}

	    if (result)
	    {
		ha.ha_clipArea = ha.ha_interArea;
		GEOCLIP(&ha.ha_clipArea, &r);
		extSubtreeInteractionArea += RECTAREA(&ha.ha_interArea);
		extSubtreeClippedArea += RECTAREA(&ha.ha_clipArea);
		extSubtreeInteraction(&ha);
	    }
	}
    }
#else	/* exactinteractions */
    {
	static Plane *interPlane = NULL, *bloatPlane = NULL;

	/*
	 * Experimental code to find exact interaction areas.
	 * Currently, this both takes longer to find interactions
	 * and longer to process them than the cookie-cutter
	 * approach above, but maybe it can be turned into a
	 * scheme that is faster.
	 */
	if (interPlane == (Plane *) NULL)
	    interPlane = DBNewPlane((ClientData) TT_SPACE);
	if (bloatPlane == (Plane *) NULL)
	    bloatPlane = DBNewPlane((ClientData) TT_SPACE);
	ExtFindInteractions(def, halo, ExtInterBloat, interPlane);
	if (ExtInterBloat)
	{
	    /* Shrink back down */
	    (void) DBSrPaintArea((Tile *) NULL, interPlane,
			&TiPlaneRect, &DBAllButSpaceBits,
			extSubtreeCopyPlane, (ClientData) bloatPlane);
	    (void) DBSrPaintArea((Tile *) NULL, bloatPlane,
			&TiPlaneRect, &DBSpaceBits,
			extSubtreeShrinkPlane, (ClientData) interPlane);
	    DBClearPaintPlane(bloatPlane);
	}
	(void) DBSrPaintArea((Tile *) NULL, interPlane,
		    &TiPlaneRect, &DBAllButSpaceBits,
		    extSubtreeInterFunc, (ClientData) &ha);
	DBClearPaintPlane(interPlane);
    }
#endif	/* exactinteractions */

done:
    /* Output connections and node adjustments */
    extOutputConns(&ha.ha_connHash, f);
    HashKill(&ha.ha_connHash);
}

#ifdef	exactinteractions
int
extSubtreeCopyPlane(tile, plane)
    Tile *tile;
    Plane *plane;
{
    Rect r;

    TITORECT(tile, &r);
    (void) DBPaintPlane(plane, &r, DBStdWriteTbl(TT_ERROR_P),
		(PaintUndoInfo *) NULL);
    return (0);
}

int
extSubtreeShrinkPlane(tile, plane)
    Tile *tile;
    Plane *plane;
{
    Rect r;

    TITORECT(tile, &r);
    r.r_xbot -= ExtInterBloat; r.r_ybot -= ExtInterBloat;
    r.r_xtop += ExtInterBloat; r.r_ytop += ExtInterBloat;
    GEOCLIP(&r, &TiPlaneRect);
    (void) DBPaintPlane(plane, &r, DBStdWriteTbl(TT_SPACE),
		(PaintUndoInfo *) NULL);
    return (0);
}

int
extSubtreeInterFunc(tile, ha)
    Tile *tile;
    HierExtractArg *ha;
{
    TITORECT(tile, &ha->ha_interArea);
    ha->ha_clipArea = ha->ha_interArea;
    extSubtreeInteractionArea += RECTAREA(&ha->ha_interArea);
    extSubtreeClippedArea += RECTAREA(&ha->ha_clipArea);
    extSubtreeInteraction(ha);
    return (0);
}
#endif	/* exactinteractions */

/*
 * ----------------------------------------------------------------------------
 *
 * extSubtreeInteraction --
 *
 * Having found an interaction area, we process it.
 * The def being extracted is ha->ha_parentUse->cu_def.
 *
 * Clipping:
 *	The cookie-cutter piece we were looking at for the interaction is
 *	ha->ha_clipArea, and the interaction area actually found is
 *	ha->ha_interArea.  It is possible that ha->ha_interArea extends
 *	outside of ha->ha_clipArea; if this is the case, all area and
 *	perimeter outside of ha->ha_clipArea are ignored when making
 *	adjustments.  When computing sidewall coupling capacitance,
 *	we search for an initial tile only inside ha->ha_clipArea.
 *
 * Algorithm:
 *	Extracting an interaction area consists of two passes.
 *
 *	The first pass will build the connection table ha->ha_connHash,
 *	but leave the adjustment for each connection as zero.  At the
 *	end of the first pass, extSubList is a list of ExtTrees containing
 *	each flattened subtree in the area of the interaction (including
 *	the parent geometry), and ha->ha_cumFlat contains everything
 *	flattened.
 *
 *	The second pass will make the adjustments in ha->ha_connHash, and
 *	will build the table et_coupleHash in ha->ha_cumFlat.  All of
 *	the table ha->ha_connHash will be output, but only those entries
 *	in et_coupleHash with non-zero capacitance adjustment (either
 *	positive or negative) will get output.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds more information to ha->ha_connHash and
 *	ha->ha_cumFlat.et_coupleHash.
 *
 * ----------------------------------------------------------------------------
 */

void
extSubtreeInteraction(ha)
    HierExtractArg *ha;	/* Context for hierarchical extraction */
{
    CellDef *oneDef, *cumDef = ha->ha_cumFlat.et_use->cu_def;
    ExtTree *oneFlat, *nextFlat;
    NodeRegion *reg;
    SearchContext scx;

    /* Copy parent paint into ha->ha_cumFlat (which was initially empty) */
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area = ha->ha_interArea;
    scx.scx_use = ha->ha_parentUse;
    DBCellCopyPaint(&scx, &DBAllButSpaceBits, 0, ha->ha_cumFlat.et_use);
#ifdef	notdef
    extCopyPaint(ha->ha_parentUse->cu_def, &ha->ha_interArea, cumDef);
#endif	/* notdef */

    /*
     * First element on the subtree list will be parent mask info.
     * Extract nodes and capacitors.  Node names come from parent.
     */
    oneFlat = extHierNewOne();
    oneDef = oneFlat->et_use->cu_def;
    DBCellCopyPaint(&scx, &DBAllButSpaceBits, 0, oneFlat->et_use);

#ifdef	notdef
    extCopyPaint(ha->ha_parentUse->cu_def, &ha->ha_interArea, oneDef);
#endif	/* notdef */
    oneFlat->et_nodes = extFindNodes(oneDef, &ha->ha_clipArea, FALSE);
    if ((ExtOptions & (EXT_DOCOUPLING|EXT_DOADJUST))
		   == (EXT_DOCOUPLING|EXT_DOADJUST))
    {
	HashInit(&oneFlat->et_coupleHash, 32, HashSize(sizeof (CoupleKey)));
	extFindCoupling(oneDef, &oneFlat->et_coupleHash, &ha->ha_clipArea);
    }
    oneFlat->et_lookNames = ha->ha_parentUse->cu_def;
    oneFlat->et_realuse = (CellUse *) NULL;
    extSubList = oneFlat;

    /*
     * Cumulative yank buffer names also come from parent.
     * Since we only mark nodes for use in naming on the first
     * pass, there's no need to extract nodes in ha_cumFlat
     * until we process the first subcell in extSubtreeFunc.
     */
    ha->ha_cumFlat.et_nodes = (NodeRegion *) NULL;
    ha->ha_cumFlat.et_lookNames = ha->ha_parentUse->cu_def;
    extFirstPass = TRUE;

    /*
     * Process each subcell in the interaction area exactly once.
     * After processing each subcell, we reset ha->ha_cumFlat.et_nodes
     * to NULL.
     */
    (void) DBCellSrArea(&scx, extSubtreeFunc, (ClientData) ha);

    if (ExtOptions & EXT_DOADJUST)
    {
	/*
	 * Re-extract ha->ha_cumFlat, this time getting node capacitance,
	 * perimeter, and area, and coupling capacitances between nodes.
	 * Assign labels from cumDef's label list.
	 * Don't reset ha_lookNames, since we still need to be able to
	 * refer to nodes in the parent.
	 */
	ha->ha_cumFlat.et_nodes = extFindNodes(cumDef, &ha->ha_clipArea, FALSE);
	ExtLabelRegions(cumDef, ExtCurStyle->exts_nodeConn,
			&(ha->ha_cumFlat.et_nodes), &ha->ha_clipArea);
	if (ExtOptions & EXT_DOCOUPLING)
	{
	    HashInit(&ha->ha_cumFlat.et_coupleHash, 32,
			HashSize(sizeof(CoupleKey)));
	    extFindCoupling(cumDef, &ha->ha_cumFlat.et_coupleHash,
			&ha->ha_clipArea);
	}

	/* Process all adjustments */
	ha->ha_subUse = (CellUse *) NULL;
	extSubtreeAdjustInit(ha);
	for (oneFlat = extSubList; oneFlat; oneFlat = oneFlat->et_next)
	    extHierAdjustments(ha, &ha->ha_cumFlat, oneFlat, &ha->ha_cumFlat);

	/*
	 * Output adjustments to substrate capacitance that are not
	 * output anywhere else.  Nodes that connect down into the
	 * hierarchy are part of ha_connHash and have adjustments
	 * that are printed in the "merge" statement.  Nodes not in
	 * the current cell are not considered.  Anything left over
	 * has its adjusted value output. 
	 */
	for (reg = ha->ha_parentReg; reg; reg = reg->nreg_next)
	{
	    Rect r;
	    NodeRegion *treg;
	    CapValue finC;
	    char *text;

	    r.r_ll = reg->nreg_ll;
	    r.r_xtop = r.r_xbot + 1;
	    r.r_ytop = r.r_ybot + 1;

	    /* Use the tile position of the parent region to find the
	     * equivalent region in cumDef.  Then compare the substrate
	     * cap and output the difference.
	     */

	    if (DBSrPaintArea((Tile *)NULL, cumDef->cd_planes[reg->nreg_pnum],
			&r, &DBAllButSpaceBits, extConnFindFunc,
			(ClientData) &treg))
	    {
		text = extNodeName(reg);
		// Output the adjustment made to the substrate cap
		// (should be negative).  Ignore adjustments of zero
		finC = (treg->nreg_cap - reg->nreg_cap) /
						ExtCurStyle->exts_capScale;
		if (finC < -1.0E-6)
		    fprintf(ha->ha_outf, "subcap \"%s\" %lg\n", text, finC);
	    }
	}

	/*
	 * Output adjustments to coupling capacitance.
	 * The names output for coupling capacitors are those on the
	 * label list for each node in the cumulative buffer.
	 */
	if (ExtOptions & EXT_DOCOUPLING)
	{
	    extSubtreeOutputCoupling(ha);
	    extCapHashKill(&ha->ha_cumFlat.et_coupleHash);
	}
    }

    /* Free the subtrees (this must happen after all adjustments are made) */
    for (oneFlat = extSubList; oneFlat; oneFlat = nextFlat)
	nextFlat = oneFlat->et_next, extHierFreeOne(oneFlat);
    extSubList = (ExtTree *) NULL;

    /*
     * Done with the cumulative yank buffer for this interaction.
     * Free the list of nodes, the list of hierarchical labels
     * built when we yanked this def, and then erase all the paint
     * in the buffer.
     */
    if (ha->ha_cumFlat.et_nodes)
	ExtFreeLabRegions((LabRegion *) ha->ha_cumFlat.et_nodes);
    ha->ha_cumFlat.et_nodes = (NodeRegion *) NULL;
    extHierFreeLabels(cumDef);
    DBCellClearDef(cumDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSubtreeAdjustInit --
 *
 * Initialize the node capacitance, perimeter, and area values in
 * all the Nodes in the HashTable ha->ha_connHash.  The initial
 * values come from the NodeRegions in ha->ha_cumFlat.et_nodes,
 * which correspond to extracting the entire flattened interaction
 * area.  We add these values to any already existing from a previous
 * interaction area in case there were any nodes that span the boundary
 * between two interaction areas.  We will then call extHierAdjustments
 * to subtract away the extracted values in each of the individually
 * flattened subtrees.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * Design notes:
 *	We only need to update perimeter, area, or substrate capacitance
 *	when nodes from different subtrees abut or overlap, i.e., connect.
 *	These nodes have already been recorded in the table ha->ha_connHash
 *	by extHierConnections(), so all we have to do is find the appropriate
 *	entries in this table.
 *
 *	Each NodeRegion in ha->ha_cumFlat contains a list of labels with
 *	it.  The first label in each list is guaranteed to correspond to
 *	an entry in the table ha->ha_connHash if this node is a participant
 *	in a connection, so we pass this label to HashFind to obtain the
 *	appropriate Node.
 *
 * ----------------------------------------------------------------------------
 */

void
extSubtreeAdjustInit(ha)
    HierExtractArg *ha;
{
    NodeRegion *np;
    NodeName *nn;
    int n;
    HashEntry *he;
    char *name;

    /*
     * Initialize the capacitance, perimeter, and area values
     * in the Nodes in the hash table ha->ha_connHash.
     */
    for (np = ha->ha_cumFlat.et_nodes; np; np = np->nreg_next)
    {
	if ((name = extNodeName((LabRegion *) np))
		&& (he = HashLookOnly(&ha->ha_connHash, name))
		&& (nn = (NodeName *) HashGetValue(he)))
	{
	    nn->nn_node->node_cap += np->nreg_cap;
	    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	    {
		nn->nn_node->node_pa[n].pa_perim += np->nreg_pa[n].pa_perim;
		nn->nn_node->node_pa[n].pa_area += np->nreg_pa[n].pa_area;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSubtreeOutputCoupling --
 *
 * Output the coupling capacitance table built up by extFindCoupling().
 * Each entry in the hash table is a capacitance between the pair of
 * nodes identified by he->h_key, an CoupleKey struct.  Writes to the
 * FILE ha->ha_outf.
 *
 * Because it is possible that the coupled nodes aren't already named,
 * we have to call extSubtreeTileToNode() to find their actual names.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See the comments above.
 *
 * ----------------------------------------------------------------------------
 */

void
extSubtreeOutputCoupling(ha)
    HierExtractArg *ha;
{
    CapValue cap;
    CoupleKey *ck;
    HashEntry *he;
    Tile *tp;
    HashSearch hs;
    char *name;

    HashStartSearch(&hs);
    while (he = HashNext(&ha->ha_cumFlat.et_coupleHash, &hs))
    {
	cap = extGetCapValue(he) / ExtCurStyle->exts_capScale;
	if (cap == 0)
	    continue;

	ck = (CoupleKey *) he->h_key.h_words;

	tp = extNodeToTile(ck->ck_1, &ha->ha_cumFlat);
	name = extSubtreeTileToNode(tp, ck->ck_1->nreg_pnum, &ha->ha_cumFlat, ha, TRUE);
	fprintf(ha->ha_outf, "cap \"%s\" ", name);

	tp = extNodeToTile(ck->ck_2, &ha->ha_cumFlat);
	name = extSubtreeTileToNode(tp, ck->ck_2->nreg_pnum, &ha->ha_cumFlat, ha, TRUE);
	fprintf(ha->ha_outf, "\"%s\" %lg\n", name, cap);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSubtreeFunc --
 *
 * Called once for each cell use that is a child of the parent def
 * being extracted.  Yanks the subtree into a new ExtTree.  Extract
 * connections between this subtree and ha->ha_cumFlat, then paint
 * the subtree into the cumulative buffer ha->ha_cumFlat and prepend
 * the subtree to extSubList.
 *
 * Results:
 *	Always returns 2, to avoid further elements in arrays.
 *
 * Side effects:
 *	Leaves ha->ha_cumFlat unextracted (all LabRegions free,
 *	and ha->ha_cumFlat.et_nodes set to NULL).
 *	See extHierConnections().
 *
 * ----------------------------------------------------------------------------
 */

int
extSubtreeFunc(scx, ha)
    SearchContext *scx;
    HierExtractArg *ha;
{
    CellUse *cumUse = ha->ha_cumFlat.et_use;
    CellUse *use = scx->scx_use;
    CellDef *oneDef;
    SearchContext newscx;
    ExtTree *oneFlat;
    HierYank hy;
    int x, y;

    /* Allocate a new ExtTree to hold the flattened, extracted subtree */
    oneFlat = extHierNewOne();
    oneFlat->et_realuse = use;

    /* Record information for finding node names the hard way later */
    ha->ha_subUse = use;

    /*
     * Yank all array elements of this subcell that lie in the interaction
     * area.  Transform to parent coordinates.  Prefix is true, meaning that
     * we should prefix each hierarchical name with the subcell use it
     * belongs to.
     */
    ha->ha_subArea = use->cu_bbox;
    GEOCLIP(&ha->ha_subArea, &ha->ha_interArea);
    hy.hy_area = &ha->ha_subArea;
    hy.hy_target = oneFlat->et_use;
    hy.hy_prefix = TRUE;
    (void) DBArraySr(use, &ha->ha_subArea, extHierYankFunc, (ClientData) &hy);

    /*
     * Extract node capacitance, perimeter, area, and coupling capacitance
     * for this subtree.  Labels come from the hierarchical labels yanked
     * above, but may have additional labels added when we find names the
     * hard way.
     */
    oneDef = oneFlat->et_use->cu_def;
    oneFlat->et_nodes = extFindNodes(oneDef, &ha->ha_clipArea, FALSE);
    ExtLabelRegions(oneDef, ExtCurStyle->exts_nodeConn,
		&(oneFlat->et_nodes), &ha->ha_clipArea);
    if ((ExtOptions & (EXT_DOCOUPLING|EXT_DOADJUST))
		   == (EXT_DOCOUPLING|EXT_DOADJUST))
	extFindCoupling(oneDef, &oneFlat->et_coupleHash, &ha->ha_clipArea);

    /*
     * If this is not the first subcell we're processing, mark ha_cumFlat's
     * tiles with LabRegions.  We don't mark it the first time through,
     * since then ha_cumFlat contains only the parent mask geometry and
     * node names will be found by looking in ha_lookNames.
     */
    if (extFirstPass)
    {
	// On the first pass, run through et_lookName's label list.
	// Copy any sticky labels to cumUse->cu_def, so that the labels
	// can be found even when there is no geometry underneath in
	// the parent cell.

	CellDef *cumDef = ha->ha_cumFlat.et_lookNames;

	if (cumDef != NULL)
	{
	    Label *lab, *newlab;
	    unsigned int n;

	    for (lab = cumDef->cd_labels; lab ; lab = lab->lab_next)
	    {
		n = sizeof (Label) + strlen(lab->lab_text)
			- sizeof lab->lab_text + 1;

		newlab = (Label *)mallocMagic(n);
		newlab->lab_type = lab->lab_type;
		newlab->lab_rect = lab->lab_rect;
		newlab->lab_flags = lab->lab_flags;
		strcpy(newlab->lab_text, lab->lab_text);

		newlab->lab_next = cumUse->cu_def->cd_labels;
		cumUse->cu_def->cd_labels = newlab;
	    }
	}
	extFirstPass = FALSE;
    }
    else
    {
	/*
	 * We don't care about the lreg_ll or lreg_pNum for these
	 * nodes (we're only interested in the label list associated
	 * with each node), so we don't pass extHierLabEach() to
	 * ExtFindRegions().
	 */
	ha->ha_cumFlat.et_nodes =
	    (NodeRegion *) ExtFindRegions(cumUse->cu_def, &TiPlaneRect,
				&ExtCurStyle->exts_activeTypes,
				ExtCurStyle->exts_nodeConn,
				extUnInit, extHierLabFirst, (int (*)()) NULL);
	ExtLabelRegions(cumUse->cu_def, ExtCurStyle->exts_nodeConn,
			&(ha->ha_cumFlat.et_nodes), &TiPlaneRect);
    }

    /* Process connections; this updates ha->ha_connHash */
    extHierConnections(ha, &ha->ha_cumFlat, oneFlat);

    /* Process substrate connection.  All substrates should be	*/
    /* connected together in the cell def, so in the case of an	*/
    /* array, just make sure that the first array entry is	*/
    /* connected.						*/
    
    if (use->cu_xhi == use->cu_xlo && use->cu_yhi == use->cu_ylo)
	extHierSubstrate(ha, use, -1, -1);
    else if (use->cu_xhi == use->cu_xlo && use->cu_yhi > use->cu_ylo)
    {	
	for (y = use->cu_ylo; y <= use->cu_yhi; y++)
	    extHierSubstrate(ha, use, -1, y);
    }
    else if (use->cu_xhi > use->cu_xlo && use->cu_yhi == use->cu_ylo)
    {
	for (x = use->cu_xlo; x <= use->cu_xhi; x++)
	extHierSubstrate(ha, use, x, -1);
    }
    else
    {
	for (x = use->cu_xlo; x <= use->cu_xhi; x++)
	    for (y = use->cu_ylo; y <= use->cu_yhi; y++)
		extHierSubstrate(ha, use, x, y);
    }

    /* Free the cumulative node list we extracted above */
    if (ha->ha_cumFlat.et_nodes)
    {
	ExtResetTiles(cumUse->cu_def, extUnInit);
	ExtFreeLabRegions((LabRegion *) ha->ha_cumFlat.et_nodes);
	ha->ha_cumFlat.et_nodes = (NodeRegion *) NULL;
    }

    /*
     * Paint the subtree buffer on top of the cumulative buffer.
     * Copy the labels that were yanked along with the subtree into
     * the cumulative buffer as well.
     */
    newscx.scx_use = oneFlat->et_use;
    newscx.scx_area = ha->ha_subArea;
    newscx.scx_trans = GeoIdentityTransform;
    DBCellCopyPaint(&newscx, &DBAllButSpaceBits, 0, cumUse);
#ifdef	notdef
    extCopyPaint(oneFlat->et_use->cu_def, &ha->ha_subArea, cumUse->cu_def);
#endif	/* notdef */
    extHierCopyLabels(oneFlat->et_use->cu_def, cumUse->cu_def);

    /* Prepend this tree to the list of trees we've processed so far */
    oneFlat->et_next = extSubList;
    extSubList = oneFlat;

    return (2);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSubtreeTileToNode --
 *
 * Map from a Tile in a given yank buffer 'et' to the name of the node
 * containing that tile.
 *
 * The node associated with a tile can be determined in one of the
 * following ways:
 *
 *	(1) Look for a label on the list of the Region pointed to by the
 *	    tile planes of the yank buffer.  If no label was found, then
 *	    try (2).
 *
 *	(2) If et->et_lookNames is non-NULL, see if the tile overlaps a
 *	    connected tile on the same plane of the def et->et_lookNames.
 *
 *	(3) Call extSubtreeHardNode() to do a painful extraction of a label.
 *	    See the comments in extSubtreeHardNode() for a description of
 *	    the algorithm used.  Only do this if doHard is TRUE.
 *
 * Results:
 *	Returns a pointer to the name of the node containing
 *	the tile.  If no node name was found, and doHard was
 *	TRUE, return the string "(none)"; if doHard was FALSE,
 *	return NULL.
 *
 * Side effects:
 *	The string returned is allocated from a static buffer, so
 *	subsequent calls to extSubtreeTileToNode() will overwrite
 *	the results returned in previous calls.
 *
 *	Records an error with the feedback package if no node name
 *	could be found and doHard was TRUE.
 *
 * ----------------------------------------------------------------------------
 */

char *
extSubtreeTileToNode(tp, pNum, et, ha, doHard)
    Tile *tp;	/* Tile whose node name is to be found */
    int pNum;	/* Plane of the tile */
    ExtTree *et;	/* Yank buffer to search */
    HierExtractArg *ha;	/* Extraction context */
    bool doHard;	/* If TRUE, we look up this node's name the hard way
			 * if we can't find it any other way; otherwise, we
			 * return NULL if we can't find the node's name.
			 */
{
    static char warningStr[] =
	"Warning: node labels should be inside overlap area";
    static char errorStr[] =
	"Cannot find the name of this node (probable extractor error)";
    CellDef *parentDef = ha->ha_parentUse->cu_def;
    LabRegion *reg;
    Label *lab;
    Rect r;
    TileType ttype;

    /* If there is a label list, use it */
    if (extHasRegion(tp, extUnInit))
    {
	reg = (LabRegion *) extGetRegion(tp);
	if (reg->lreg_labels)
	    return (extNodeName(reg));
    }

    /*
     * If there is any node at all in the cell et->et_lookNames,
     * use it.  The node doesn't have to have a label list.
     */
    TITORECT(tp, &r);
    if (et->et_lookNames)
    {
	/*
	 * Make sure we've got a valid tile -- interactions with interrupts
	 * can cause problems.
	 */
	if (IsSplit(tp))
	{
	    if (SplitSide(tp))
		ttype = SplitRightType(tp);
	    else
		ttype = SplitLeftType(tp);
	}
	else
	    ttype = TiGetTypeExact(tp);

	if (pNum >= PL_PAINTBASE)
	{
	    if (IsSplit(tp))
	    {
		if (DBSrPaintNMArea((Tile *) NULL,
			et->et_lookNames->cd_planes[pNum],
			TiGetTypeExact(tp), &r, &DBAllButSpaceBits,
			extConnFindFunc, (ClientData) &reg))
		{
		    if (SigInterruptPending)
			return ("(none)");
		    return (extNodeName(reg));
		}
	    }
	    else
	    {
		if (DBSrPaintArea((Tile *) NULL,
			et->et_lookNames->cd_planes[pNum], &r,
			&DBAllButSpaceBits, extConnFindFunc, (ClientData) &reg))
		{
		    if (SigInterruptPending)
			return ("(none)");
		    return (extNodeName(reg));
		}
	    }
	}
    }

    /* We have to do it the hard way */
    if (!doHard) return ((char *) NULL);
    if (extHasRegion(tp, extUnInit)
	    && (reg = extSubtreeHardNode(tp, pNum, et, ha)))
    {
	if (ExtDoWarn & EXTWARN_LABELS)
	{
	    DBWFeedbackAdd(&r, warningStr, parentDef, 1, STYLE_PALEHIGHLIGHTS);
	    extNumWarnings++;
	}
	return (extNodeName(reg));
    }

    extNumFatal++;
    if (!DebugIsSet(extDebugID, extDebNoFeedback))
	DBWFeedbackAdd(&r, errorStr, parentDef, 1, STYLE_MEDIUMHIGHLIGHTS);
    return ("(none)");
}

/*
 * ----------------------------------------------------------------------------
 * extConnFindFunc --
 *
 * Called when searching the area of a tile in the def et->et_lookNames
 * by extSubtreeTileToNode() above.
 *
 * Results:
 *	If we find a tile that has been marked with a node,
 *	return 1; otherwise, return 0.
 *
 * Side effects:
 *	Sets *preg to the node found if we returned 1; otherwise,
 *	leaves *preg alone.
 * ----------------------------------------------------------------------------
 */

int
extConnFindFunc(tp, preg)
    Tile *tp;
    LabRegion **preg;
{
    if (extHasRegion(tp, extUnInit))
    {
	*preg = (LabRegion *) extGetRegion(tp);
	return (1);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSubtreeHardNode --
 *
 * Find a node name for the electrical node containing the tile 'tp'
 * the hard way.  We assume tp->ti_client points to a LabRegion that
 * had no labels associated with it; if we succeed, we leave this
 * LabRegion pointing to a newly allocated LabelList and Label.
 *
 * Results:
 *	Returns a pointer to LabRegion for the node to which the tile
 *	'tp' belongs.  Returns NULL if no region could be found.
 *
 * Side effects:
 *	See above.
 *
 * Algorithm:
 *	For each subcell of the parent that could have contributed
 *	to the yank buffer in question, search the original tree
 *	for geometry in the area of the tile 'tp'.  For each cell
 *	we find, we trace out just those nodes lying in the area
 *	of the overlap, and then do a label assignment for those
 *	nodes.  As soon as we find a label, we're done.  Otherwise,
 *	we reset this def back the way we found it and continue on
 *	to the next cell in our search.
 *
 * ----------------------------------------------------------------------------
 */

LabRegion *
extSubtreeHardNode(tp, pNum, et, ha)
    Tile *tp;
    int pNum;
    ExtTree *et;
    HierExtractArg *ha;
{
    LabRegion *lreg = (LabRegion *) extGetRegion(tp);
    CellDef *def = et->et_use->cu_def;
    TileType ttype;
    char labelBuf[4096];
    LabelList *ll;
    HardWay arg;

    ASSERT(lreg->lreg_labels == NULL, "extSubtreeHardNode");

    if (IsSplit(tp))
    {
	if (SplitSide(tp))
	    ttype = SplitRightType(tp);
	else
	    ttype = SplitLeftType(tp);
    }
    else
	ttype = TiGetTypeExact(tp);

    arg.hw_ha = ha;
    arg.hw_label = (Label *) NULL;
    arg.hw_mask = DBPlaneTypes[pNum];
    TTMaskAndMask(&arg.hw_mask, &DBConnectTbl[ttype]);
    arg.hw_tpath.tp_last = &labelBuf[sizeof labelBuf - 3];
    arg.hw_tpath.tp_first = arg.hw_tpath.tp_next = labelBuf;
    arg.hw_prefix = TRUE;
    TITORECT(tp, &arg.hw_area);

    /*
     * Try to find a label in the area.
     * If we can't find a label, we make one up based on the
     * automatically generated node name in a child cell that
     * contains paint in this node.
     */
    labelBuf[0] = '\0';
    arg.hw_autogen = FALSE;
    extSubtreeHardSearch(et, &arg);
    if (arg.hw_label == NULL)
    {
	labelBuf[0] = '\0';
	arg.hw_autogen = TRUE;
	extSubtreeHardSearch(et, &arg);
    }

    /*
     * If we succeeded (we always should), we now have a label.
     * Make the single LabelList for the region 'lreg' point to
     * this label, and prepend it to the list for 'def'.
     */
    if (arg.hw_label)
    {
	ll = (LabelList *) mallocMagic((unsigned) (sizeof (LabelList)));
	lreg->lreg_labels = ll;
	ll->ll_next = (LabelList *) NULL;
	ll->ll_label = arg.hw_label;
	arg.hw_label->lab_next = def->cd_labels;
	def->cd_labels = arg.hw_label;
	return (lreg);
    }

    return ((LabRegion *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 * extSubtreeHardSearch --
 *
 * Do the actual work of deciding which subtrees to search
 * for extSubtreeHardNode above.  We apply the procedure
 * extHardProc() at each subcell.
 * ----------------------------------------------------------------------------
 */

void
extSubtreeHardSearch(et, arg)
    ExtTree *et;
    HardWay *arg;
{
    HierExtractArg *ha = arg->hw_ha;
    ExtTree *oneFlat;


    arg->hw_proc = extHardProc;
    if (et == &ha->ha_cumFlat)
    {
	/*
	 * Recursively search all children of parent up to, but not
	 * including, ha->ha_subUse.  Don't search parent paint.
	 */
	for (oneFlat = extSubList; oneFlat; oneFlat = oneFlat->et_next)
	{
	    if (oneFlat->et_realuse)
	    {
		if (DBArraySr(oneFlat->et_realuse, &arg->hw_area,
				    extSubtreeHardUseFunc, (ClientData) arg))
		{
		    break;
		}
	    }
	}
    }
    else
    {
	/* Recursively search only the elements of ha->ha_subUse */
	(void) DBArraySr(ha->ha_subUse, &arg->hw_area,
		extSubtreeHardUseFunc, (ClientData) arg);
    }
}

/*
 * ----------------------------------------------------------------------------
 * extSubtreeHardUseFunc --
 *
 * When searching a subtree, this is called once for each element
 * in the array that is the root of the subtree.
 *
 * Results:
 *	Returns 1 if we have successfully found a label, 0 if not
 *	(return value of arg->hw_proc).
 *
 * Side effects:
 *	Calls (*arg->hw_proc)().
 * ----------------------------------------------------------------------------
 */

int
extSubtreeHardUseFunc(use, trans, x, y, arg)
    CellUse *use;	/* Use that is the root of the subtree being searched */
    Transform *trans;	/* Transform from coordinates of use->cu_def to those
			 * in use->cu_parent, for the array element (x, y).
			 */
    int x, y;		/* Indices of this array element */
    HardWay *arg;	/* Context passed down to filter functions */
{
    SearchContext scx;
    Transform tinv;

    scx.scx_use = use;
    scx.scx_trans = *trans;
    scx.scx_x = x;
    scx.scx_y = y;
    GEOINVERTTRANS(trans, &tinv);
    GEOTRANSRECT(&tinv, &arg->hw_area, &scx.scx_area);
    return ((*arg->hw_proc)(&scx, arg));
}
