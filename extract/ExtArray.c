/*
 * ExtArray.c --
 *
 * Circuit extraction.
 * Extract interactions between elements of an array.
 * The routines in this file are not re-entrant.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtArray.c,v 1.2 2009/05/30 03:14:00 tim Exp $";
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
#include "utils/styles.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"

/* Canonical interaction areas */
#define	AREA_A	0
#define	AREA_B	1
#define	AREA_C	2

/* Imports from elsewhere in this module */
extern int extHardProc();

/* Local data passed to extArrayTileToNode() and its children */
Point extArrayPrimXY;		/* X, Y indices of primary array element */
Point extArrayInterXY;		/* X, Y indices of intersecting array element */
Transform extArrayPTrans;	/* Transform from primary element to root */
Transform extArrayITrans;	/* Transform from intersecting element ... */
int extArrayWhich;		/* Which interaction area is being processed */
ExtTree *extArrayPrimary;	/* Primary array element */

/* Forward declarations */
int extArrayFunc();
int extArrayPrimaryFunc(), extArrayInterFunc();
char *extArrayRange();
char *extArrayTileToNode();
LabRegion *extArrayHardNode();
char *extArrayNodeName();

void extArrayProcess();
void extArrayAdjust();
void extArrayHardSearch();

#if 0

/* 
 * ----------------------------------------------------------------------------
 * extOutputGeneratedLabels ---
 *
 * Write to the .ext file output "node" lines for labels generated in
 * the parent cell where paint in the subcell is not otherwise
 * represented by a node in the parent.  These nodes have no material
 * in the parent, and therefore have no capacitance or resistance
 * associated with them.
 *
 * ----------------------------------------------------------------------------
 */

void
extOutputGeneratedLabels(parentUse, f)
    CellUse *parentUse;
    FILE *f;
{
    CellDef *parentDef;
    Label *lab;
    int n;

    parentDef = parentUse->cu_def;

    while ((lab = parentDef->cd_labels) != NULL)
    {
	if ((lab->lab_flags & LABEL_GENERATE) == 0) return;

	fprintf(f, "node \"%s\" 0 0 %d %d %s",
		lab->lab_text, lab->lab_rect.r_xbot,
		lab->lab_rect.r_ybot,
		DBTypeShortName(lab->lab_type));
	for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	    fprintf(f, " 0 0");
	putc('\n', f);
	freeMagic(lab);
	parentDef->cd_labels = lab->lab_next;
    }
}

#endif

/*
 * ----------------------------------------------------------------------------
 *
 * extArray --
 *
 * Extract all connections resulting from interactions within each
 * array of subcells in the cell parentUse->cu_def.
 *
 * This procedure only finds arrays, and then calls extArrayFunc() to
 * do the real work.  See the comments there for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Outputs connections and adjustments to the file 'f'.
 *	There are two kinds of records; see extSubtree for a description.
 *	However, when we output nodenames, they may contain implicit
 *	subscripting information, e.g,
 *
 *		cap a[1:3]/In a[2:4]/Phi1 deltaC
 *
 *	which is like 3 separate "cap" records:
 *
 *		cap a[1]/In a[2]/Phi1 deltaC
 *		cap a[2]/In a[3]/Phi1 deltaC
 *		cap a[3]/In a[4]/Phi1 deltaC
 *
 * ----------------------------------------------------------------------------
 */

void
extArray(parentUse, f)
    CellUse *parentUse;
    FILE *f;
{
    SearchContext scx;
    HierExtractArg ha;

    /*
     * The connection hash table is initialized here but doesn't get
     * cleared until the end.  It is responsible for changes to the
     * node structure over the entire cell 'parentUse->cu_def'.
     */
    ha.ha_outf = f;
    ha.ha_parentUse = parentUse;
    ha.ha_nodename = extArrayTileToNode;
    ha.ha_cumFlat.et_use = extYuseCum;
    HashInit(&ha.ha_connHash, 32, 0);

    /* The real work of processing each array is done by extArrayFunc() */
    scx.scx_use = parentUse;
    scx.scx_trans = GeoIdentityTransform;
    scx.scx_area = TiPlaneRect;
    (void) DBCellSrArea(&scx, extArrayFunc, (ClientData) &ha);

#if 0
    /* Output generated labels and remove them from the parent */
    extOutputGeneratedLabels(parentUse, f);
#endif

    /* Output connections and node adjustments */
    extOutputConns(&ha.ha_connHash, f);
    HashKill(&ha.ha_connHash);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayFunc --
 *
 * Given a CellUse as argument, extract and output all the connections that
 * result from interactions between neighboring elements of the array.
 *
 * Results:
 *	Returns 2 always so we stop after the first CellUse in the array.
 *
 * Side effects:
 *	Writes to the file 'ha->ha_outf'
 *
 * Design:
 *	To extract all the connections made between members of an array, we
 *	only have to look for interactions in three canonical areas, shaded as
 *	A, B, and C in the diagram below.  Each interaction area consists only
 *	of the portion of overlap between the canonical cell (1 for A, B, and
 *	2 for C) and its neighbors.  Hence the exact size of the interaction
 *	areas depends on how much overlap there is.  In the extreme cases,
 *	there may be no areas to check at all (instances widely separated),
 *	or there may even be areas with more than four instances overlapping
 *	(spacing less than half the size of the instance).
 *
 * 	-------------------------------------------------
 *	|               |               |               |
 *	|       2       |               |               |
 *	|               |               |               |
 *	|             CCC               |               |
 * 	--------------CCC--------------------------------
 *	|               |               |               |
 *	|               |               |               |
 *	|               |               |               |
 *	|               |               |               |
 * 	AAAAAAAAAAAAAAaba-------------------------------|
 *	AAAAAAAAAAAAAAbab               |               |
 *	|             BBB               |               |
 *	|       1     BBB               |               |
 *	|             BBB               |               |
 * 	--------------BBB--------------------------------
 *
 * In area A, we check for interactions with 1 and the elements directly
 * above, or above and to the right.  In area B, we check for interactions
 * only with elements at the same level but to the right.  In area C, we
 * check for interactions only with elements below and to the right.
 *
 * ----------------------------------------------------------------------------
 */

int
extArrayFunc(scx, ha)
    SearchContext *scx;	/* Describes first element of array */
    HierExtractArg *ha;	/* Extraction context */
{
    int xsep, ysep;	/* X, Y separation in parent coordinates */
    int xsize, ysize;	/* X, Y sizes in parent coordinates */
    int halo = ExtCurStyle->exts_sideCoupleHalo	 + 1;
    CellUse *use = scx->scx_use;
    CellDef *def = use->cu_def;
    Rect tmp, tmp2, primary;

    /* Skip uses that aren't arrays */
    if ((use->cu_xlo == use->cu_xhi) && (use->cu_ylo == use->cu_yhi))
	return (2);

    if ((ExtOptions & (EXT_DOCOUPLING|EXT_DOADJUST))
		   != (EXT_DOCOUPLING|EXT_DOADJUST))
	halo = 1;

    /*
     * Compute the sizes and separations of elements, in coordinates
     * of the parent.  If the array is 1-dimensional, we set the
     * corresponding spacing to an impossibly large distance.
     */
    tmp.r_xbot = tmp.r_ybot = 0;
    if (use->cu_xlo == use->cu_xhi)
	tmp.r_xtop = def->cd_bbox.r_xtop - def->cd_bbox.r_xbot + 2;
    else tmp.r_xtop = use->cu_xsep;
    if (use->cu_ylo == use->cu_yhi)
	tmp.r_ytop = def->cd_bbox.r_ytop - def->cd_bbox.r_ybot + 2;
    else tmp.r_ytop = use->cu_ysep;
    GeoTransRect(&use->cu_transform, &tmp, &tmp2);
    xsep = tmp2.r_xtop - tmp2.r_xbot;
    ysep = tmp2.r_ytop - tmp2.r_ybot;
    GeoTransRect(&use->cu_transform, &def->cd_bbox, &tmp2);
    xsize = tmp2.r_xtop - tmp2.r_xbot;
    ysize = tmp2.r_ytop - tmp2.r_ybot;

    /*
     * For areas A and B, we will be looking at the interactions
     * between the element in the lower-left corner of the array
     * (in parent coordinates) and its neighbors to the top, right,
     * and top-right.
     */
    primary.r_xbot = use->cu_bbox.r_xbot;
    primary.r_xtop = use->cu_bbox.r_xbot + 1;
    primary.r_ybot = use->cu_bbox.r_ybot;
    primary.r_ytop = use->cu_bbox.r_ybot + 1;
    ha->ha_subUse = use;

    /* Area A */
    if (ysep <= ysize)
    {
	ha->ha_clipArea.r_xbot = use->cu_bbox.r_xbot;
	ha->ha_clipArea.r_xtop = use->cu_bbox.r_xbot + xsize + halo;
	ha->ha_clipArea.r_ybot = use->cu_bbox.r_ybot + ysep - halo;
	ha->ha_clipArea.r_ytop = use->cu_bbox.r_ybot + ysize + halo;
	ha->ha_interArea = ha->ha_clipArea;
	extArrayWhich = AREA_A;
	extArrayProcess(ha, &primary);
	if (SigInterruptPending)
	    return (1);
    }

    /* Area B */
    if (xsep <= xsize)
    {
	ha->ha_clipArea.r_xbot = use->cu_bbox.r_xbot + xsep - halo;
	ha->ha_clipArea.r_xtop = use->cu_bbox.r_xbot + xsize + halo;
	ha->ha_clipArea.r_ybot = use->cu_bbox.r_ybot;
	ha->ha_clipArea.r_ytop = use->cu_bbox.r_ybot + ysize + halo;
	ha->ha_interArea = ha->ha_clipArea;
	extArrayWhich = AREA_B;
	extArrayProcess(ha, &primary);
	if (SigInterruptPending)
	    return (1);
    }

    /* Area C */
    if (ysep <= ysize && xsep <= xsize)
    {
	/*
	 * For area C, we will be looking at the interactions between
	 * the element in the upper-left corner of the array (in parent
	 * coordinates) and its neighbors to the bottom-right only.
	 */
	primary.r_ybot = use->cu_bbox.r_ytop - 1;
	primary.r_ytop = use->cu_bbox.r_ytop;
	ha->ha_clipArea.r_xbot = use->cu_bbox.r_xbot + xsep - halo;
	ha->ha_clipArea.r_xtop = use->cu_bbox.r_xbot + xsize + halo;
	ha->ha_clipArea.r_ybot = use->cu_bbox.r_ytop - ysize - halo;
	ha->ha_clipArea.r_ytop = use->cu_bbox.r_ytop - ysep + halo;
	ha->ha_interArea = ha->ha_clipArea;
	extArrayWhich = AREA_C;
	extArrayProcess(ha, &primary);
    }

    return (2);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayProcess --
 *
 * Process a single canonical interaction area for the arrayed CellUse
 * 'ha->ha_subUse'.  The area 'primary', in parent coordinates, should
 * be contained in only one element of the array.  For each other element
 * in the array that appears in the area 'ha->ha_interArea', we determine
 * all connections and R/C adjustments and output them in the form of an
 * implicitly iterated "merge" or "adjust" line for the rest of the array.
 *
 * Expects extArrayWhich to be one of AREA_A, AREA_B, or AREA_C; this
 * is the interaction area being searched.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See extArrayPrimaryFunc and extArrayInterFunc for details.
 *	Trashes ha->ha_cumFlat.et_use.
 *
 * ----------------------------------------------------------------------------
 */

void
extArrayProcess(ha, primary)
    HierExtractArg *ha;
    Rect *primary;	/* Area guaranteed to contain only the primary
			 * element of the array, against which we will
			 * extract all other elements that overlap the
			 * area 'ha->ha_interArea'.
			 */
{
    CellUse *use = ha->ha_subUse;


    /*
     * Yank the primary array element into a new yank buffer
     * that we leave extArrayPrimary pointing to.
     */
    extArrayPrimary = (ExtTree *) NULL;
    if (DBArraySr(use, primary, extArrayPrimaryFunc, (ClientData) ha) == 0)
    {
	DBWFeedbackAdd(primary,
		"System error: expected array element but none found",
		ha->ha_parentUse->cu_def, 1, STYLE_MEDIUMHIGHLIGHTS);
	extNumFatal++;
	return;
    }
    if (SigInterruptPending) goto done;

    /*
     * Find and process all other elements that intersect ha->ha_interArea,
     * extracting connections against extArrayPrimary.
     */
    (void) DBArraySr(use, &ha->ha_interArea, extArrayInterFunc, (ClientData)ha);

done:
    if (extArrayPrimary) extHierFreeOne(extArrayPrimary);
    extArrayPrimary = (ExtTree *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayPrimaryFunc --
 *
 * Called by DBArraySr, which should only find a single array element.
 * We record which element was found by setting extArrayPrimXY.p_x
 * and extArrayPrimXY.p_y, and also the transform in extArrayPTrans
 * for use by extArrayHardNode().
 *
 * We yank the paint and labels of this array element into a new ExtTree,
 * which we leave extArrayPrimary pointing to.  The area, perimeter,
 * capacitance, and coupling capacitance for this element are extracted.
 *
 * Results:
 *	Returns 1 to cause DBArraySr to abort and return 1 itself.
 *	This is so the caller of DBArraySr can tell whether or not
 *	any elements were found (a sanity check).
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
extArrayPrimaryFunc(use, trans, x, y, ha)
    CellUse *use;	/* Use of which this is an array element */
    Transform *trans;	/* Transform from coordinates of use->cu_def to those
			 * in use->cu_parent, for the array element (x, y).
			 */
    int x, y;		/* X, Y indices of this array element */
    HierExtractArg *ha;
{
    CellDef *primDef;
    HierYank hy;

    /*
     * Remember the indices of this array element.
     * When we are looking for all other array elements intersecting
     * this area, we will ignore this element.  We also remember the
     * transform in case we need to use it in extArrayHardNode().
     */
    extArrayPrimXY.p_x = x, extArrayPrimXY.p_y = y;
    extArrayPTrans = *trans;

    /* Restrict searching to interaction area for this element of array */
    GeoTransRect(trans, &use->cu_def->cd_bbox, &ha->ha_subArea);
    GeoClip(&ha->ha_subArea, &ha->ha_interArea);

    /* Yank this element into the primary buffer */
    extArrayPrimary = extHierNewOne();
    hy.hy_area = &ha->ha_subArea;
    hy.hy_target = extArrayPrimary->et_use;
    hy.hy_prefix = FALSE;
    (void) extHierYankFunc(use, trans, x, y, &hy);

    /*
     * Extract extArrayPrimary, getting node capacitance, perimeter,
     * and area, and coupling capacitances between nodes.  Assign
     * labels from primDef's label list.
     */
    primDef = extArrayPrimary->et_use->cu_def;
    extArrayPrimary->et_nodes = extFindNodes(primDef, &ha->ha_clipArea, FALSE);
    ExtLabelRegions(primDef, ExtCurStyle->exts_nodeConn,
		&extArrayPrimary->et_nodes, &ha->ha_clipArea);
    if ((ExtOptions & (EXT_DOADJUST|EXT_DOCOUPLING))
		   == (EXT_DOADJUST|EXT_DOCOUPLING))
	extFindCoupling(primDef, &extArrayPrimary->et_coupleHash,
			&ha->ha_clipArea);

    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayInterFunc --
 *
 * Called by DBArraySr, which should find all array elements inside
 * 'ha->ha_interArea' (in parent coordinates).  If the array element
 * (x, y) is the same as the primary element found by extArrayPrimaryFunc,
 * i.e, the element (extArrayPrimXY.p_x, extArrayPrimXY.p_y), we
 * skip it.  Otherwise, we yank the overlap of this array element with
 * 'ha->ha_interArea' into its own subtree and extract the interactions
 * between it and extArrayPrimary.
 *
 * Results:
 *	Returns 0 to cause DBArraySr to continue.
 *
 * Side effects:
 *	Sets extArrayInterXY.p_x, extArrayInterXY.p_y to the element
 *	(x, y) so that lower-level functions have access to this information.
 *
 * ----------------------------------------------------------------------------
 */

int
extArrayInterFunc(use, trans, x, y, ha)
    CellUse *use;	/* Use of which this is an array element */
    Transform *trans;	/* Transform from use->cu_def to use->cu_parent
			 * coordinates, for the array element (x, y).
			 */
    int x, y;		/* X, Y of this array element in use->cu_def coords */
    HierExtractArg *ha;
{
    CellUse *cumUse = ha->ha_cumFlat.et_use;
    CellDef *cumDef = cumUse->cu_def;
    SearchContext scx;
    CellDef *oneDef;
    ExtTree *oneFlat;
    HierYank hy;

    /* Skip this element if it is the primary one */
    if (x == extArrayPrimXY.p_x && y == extArrayPrimXY.p_y)
	return (0);

    switch (extArrayWhich)
    {
	/*
	 * Area A is above, or above and to the right.
	 * Given where we search, there are no elements below and
	 * to the right of area A.
	 */
	case AREA_A:
	    if (x == extArrayPrimXY.p_x || y == extArrayPrimXY.p_y)
	    {
		/*
		 * Exactly one of X or Y is the same as for
		 * the primary element.
		 */
		if (trans->t_a)
		{
		    /*
		     * X, Y are still X, Y in parent.
		     * If X is different, this element is only to the
		     * right and so belongs to area B.
		     */
		    if (x != extArrayPrimXY.p_x) return (0);
		}
		else
		{
		    /*
		     * X, Y are interchanged in parent.
		     * If Y is different, this element is only to the
		     * right and so belongs to area B.
		     */
		    if (y != extArrayPrimXY.p_y) return (0);
		}
	    }
	    break;
	/*
	 * Area B is only interactions to the right (not
	 * above, or diagonally above or below), in parent
	 * coordinates.
	 */
	case AREA_B:
	    if (trans->t_a)
	    {
		/* x, y are still x, y in parent */
		if (y != extArrayPrimXY.p_y) return (0);
	    }
	    else
	    {
		/* x, y are interchanged in parent */
		if (x != extArrayPrimXY.p_x) return (0);
	    }
	    break;
	/*
	 * Area C checks only diagonal interactions.
	 * Given where we search, there are no interactions
	 * above and to the right of area C; the only diagonal
	 * interactions are below and to the right.
	 */
	case AREA_C:
	    if (x == extArrayPrimXY.p_x || y == extArrayPrimXY.p_y)
		return (0);
	    break;
    }

    /* Indicate which element this is to connection output routines */
    extArrayInterXY.p_x = x, extArrayInterXY.p_y = y;
    extArrayITrans = *trans;

    /* Restrict searching to interaction area for this element of array */
    GeoTransRect(trans, &use->cu_def->cd_bbox, &ha->ha_subArea);
    GeoClip(&ha->ha_subArea, &ha->ha_interArea);

    /* Yank this array element into a new ExtTree */
    oneFlat = extHierNewOne();
    hy.hy_area = &ha->ha_subArea;
    hy.hy_target = oneFlat->et_use;
    hy.hy_prefix = FALSE;
    (void) extHierYankFunc(use, trans, x, y, &hy);

    /*
     * Extract node capacitance, perimeter, area, and coupling capacitance
     * for this subtree.  Labels come from the hierarchical labels yanked
     * above, but may have additional labels added when we find names the
     * hard way.
     */
    oneDef = oneFlat->et_use->cu_def;
    oneFlat->et_nodes = extFindNodes(oneDef, &ha->ha_clipArea, FALSE);
    ExtLabelRegions(oneDef, ExtCurStyle->exts_nodeConn, &oneFlat->et_nodes,
		&ha->ha_clipArea);
    if ((ExtOptions & (EXT_DOADJUST|EXT_DOCOUPLING))
		   == (EXT_DOADJUST|EXT_DOCOUPLING))
	extFindCoupling(oneDef, &oneFlat->et_coupleHash, &ha->ha_clipArea);

    /* Process connections */
    extHierConnections(ha, extArrayPrimary, oneFlat);

    /* Process substrate connection */
    extHierSubstrate(ha, use, x, y);

    ha->ha_cumFlat.et_nodes = (NodeRegion *) NULL;
    if (ExtOptions & EXT_DOADJUST)
    {
	/* Build cumulative buffer from both extArrayPrimary and oneFlat */
	scx.scx_trans = GeoIdentityTransform;
	scx.scx_area = TiPlaneRect;
	scx.scx_use = oneFlat->et_use;
	DBCellCopyPaint(&scx, &DBAllButSpaceBits, 0, cumUse);
	scx.scx_use = extArrayPrimary->et_use;
	DBCellCopyPaint(&scx, &DBAllButSpaceBits, 0, cumUse);

	/*
	 * Extract everything in the cumulative buffer.
	 * Don't bother labelling the nodes, though, since we will never look
	 * at the node labels (we only search extArrayPrimary or oneFlat for
	 * the name of a node).  Finally, compute and output adjustments for
	 * nodes and coupling capacitance.
	 */
	HashInit(&ha->ha_cumFlat.et_coupleHash, 32,
			HashSize(sizeof (CoupleKey)));
	ha->ha_cumFlat.et_nodes = extFindNodes(cumDef, &ha->ha_clipArea, FALSE);
	if (ExtOptions & EXT_DOCOUPLING)
	    extFindCoupling(cumDef, &ha->ha_cumFlat.et_coupleHash,
			&ha->ha_clipArea);
	extArrayAdjust(ha, oneFlat, extArrayPrimary);
	if (ExtOptions & EXT_DOCOUPLING) 
	    extCapHashKill(&ha->ha_cumFlat.et_coupleHash);
    }

    /* Clean up */
    if (oneFlat) extHierFreeOne(oneFlat);
    if (ha->ha_cumFlat.et_nodes)
	ExtFreeLabRegions((LabRegion *) ha->ha_cumFlat.et_nodes);
    ha->ha_cumFlat.et_nodes = (NodeRegion *) NULL;
    DBCellClearDef(cumDef);
    return (0);
}

void
extArrayAdjust(ha, et1, et2)
    HierExtractArg *ha;
    ExtTree *et1, *et2;
{
    CapValue cap;	/* value of capacitance WAS: int */
    NodeRegion *np;
    CoupleKey *ck;
    HashEntry *he;
    NodeName *nn;
    HashSearch hs;
    char *name;

    /*
     * Initialize the capacitance, perimeter, and area values
     * in the Nodes in the hash table ha->ha_connHash, taking
     * their values from the NodeRegions in ha->ha_cumFlat.
     */
    for (np = ha->ha_cumFlat.et_nodes; np; np = np->nreg_next)
    {
	if ((name = extArrayNodeName(np, ha, et1, et2))
		&& (he = HashLookOnly(&ha->ha_connHash, name))
		&& (nn = (NodeName *) HashGetValue(he)))
	{
	    nn->nn_node->node_cap = np->nreg_cap;
	    bcopy((char *) np->nreg_pa, (char *) nn->nn_node->node_pa,
		    ExtCurStyle->exts_numResistClasses * sizeof (PerimArea));
	}
    }

    /*
     * Coupling capacitance from et1 and et2 gets subtracted from that
     * stored in ha->ha_cumFlat.  Also, subtract the node capacitance,
     * perimeter, and area of each subtree from ha->ha_cumFlat's nodes.
     */
    extHierAdjustments(ha, &ha->ha_cumFlat, et1, et1);
    extHierAdjustments(ha, &ha->ha_cumFlat, et2, et2);

    HashStartSearch(&hs);
    while (he = HashNext(&ha->ha_cumFlat.et_coupleHash, &hs))
    {
	cap = extGetCapValue(he)  / ExtCurStyle->exts_capScale;
	if (cap == 0)
	    continue;

	ck = (CoupleKey *) he->h_key.h_words;
	name = extArrayNodeName(ck->ck_1, ha, et1, et2);
	fprintf(ha->ha_outf, "cap \"%s\" ", name);
	name = extArrayNodeName(ck->ck_2, ha, et1, et2);
	fprintf(ha->ha_outf, "\"%s\" %lg\n", name, cap);
    }
}

char *
extArrayNodeName(np, ha, et1, et2)
    NodeRegion *np;
    HierExtractArg *ha;
    ExtTree *et1, *et2;
{
    Tile *tp;

    tp = extNodeToTile(np, et1);
    if (tp && TiGetType(tp) != TT_SPACE && extHasRegion(tp, extUnInit))
	return (extArrayTileToNode(tp, np->nreg_pnum, et1, ha, TRUE));

    tp = extNodeToTile(np, et2);
    if (tp && TiGetType(tp) != TT_SPACE && extHasRegion(tp, extUnInit))
	return (extArrayTileToNode(tp, np->nreg_pnum, et2, ha, TRUE));

    return ("(none)");
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayTileToNode --
 *
 * Map from a Tile in a given ExtTree to the name of the node
 * containing that tile.
 *
 * The node associated with a tile can be determined in one of the
 * following ways:
 *
 *	(1) Look for a label on the list of the Region pointed to by the
 *	    tile planes of 'et->et_use->cu_def'.  If no label was found,
 *	    then try (2).
 *
 *	(2) Call extArrayHardNode() to do a painful extraction of a label.
 *	    See the comments in extArrayHardNode() for a description of
 *	    the algorithm used.  Only do this if doHard is TRUE.
 *
 * The actual name we use will be prefixed by the array use identifier
 * (from ha->ha_subUse), followed by the range of subscripts for that array
 * for which this is valid.  The ExtTree 'et' tells us whether we are looking
 * at the primary element of an array (when it is extArrayPrimary), or at
 * one of the elements against which the primary is being extracted.
 *
 * Results:
 *	Returns a pointer to the name of the node containing
 *	the tile.  If no node name was found, and doHard was
 *	TRUE, return the string "(none)"; if doHard was FALSE,
 *	return NULL.
 *
 * Side effects:
 *	The string returned is allocated from a static buffer, so
 *	subsequent calls to extArrayTileToNode() will overwrite
 *	the results returned in previous calls.
 *
 *	Records an error with the feedback package if no node name
 *	could be found, and doHard was TRUE.
 *
 * ----------------------------------------------------------------------------
 */

char *
extArrayTileToNode(tp, pNum, et, ha, doHard)
    Tile *tp;
    int pNum;
    ExtTree *et;
    HierExtractArg *ha;
    bool doHard;	/* If TRUE, we look up this node's name the hard way
			 * if we can't find it any other way; otherwise, we
			 * return NULL if we can't find the node's name.
			 */
{
    static char name[2048];
    static char errorStr[] =
	"Cannot find the name of this node (probable extractor error)";
    CellDef *def = et->et_use->cu_def;
    CellUse *use = ha->ha_subUse;
    char *srcp, *dstp, *endp;
    bool hasX = (use->cu_xlo != use->cu_xhi);
    bool hasY = (use->cu_ylo != use->cu_yhi);
    int xdiff = extArrayInterXY.p_x - extArrayPrimXY.p_x;
    int ydiff = extArrayInterXY.p_y - extArrayPrimXY.p_y;
    LabRegion *reg;
    Rect r;

    if (extHasRegion(tp, extUnInit))
    {
	reg = (LabRegion *) extGetRegion(tp);
	if (reg->lreg_labels)
	    goto found;
    }

    if (!DebugIsSet(extDebugID, extDebNoHard))
	if (reg = (LabRegion *) extArrayHardNode(tp, pNum, def, ha))
	    goto found;

    /* Blew it */
    if (!doHard) return ((char *) NULL);
    extNumFatal++;
    TiToRect(tp, &r);
    if (!DebugIsSet(extDebugID, extDebNoFeedback))
	DBWFeedbackAdd(&r, errorStr, ha->ha_parentUse->cu_def, 1,
				STYLE_MEDIUMHIGHLIGHTS);
    return ("(none)");

found:
    /* Copy in the use id, leaving room for [%d:%d,%d:%d] at the end */
    srcp = use->cu_id;
    dstp = name;
    endp = &name[sizeof name - 40];
    while (dstp < endp && (*dstp++ = *srcp++))
	/* Nothing */;
    if (dstp >= endp) goto done;
    dstp--;

#define	Far(v, lo, hi)	((v) == (lo) ? (hi) : (lo))
#define	FarX(u)		Far(extArrayPrimXY.p_x, (u)->cu_xlo, (u)->cu_xhi)
#define	FarY(u)		Far(extArrayPrimXY.p_y, (u)->cu_ylo, (u)->cu_yhi)

    /*
     * Append the array subscripts.
     * Remember that 2-d arrays are subscripted [y,x] and not [x,y].
     */
    if (def == extArrayPrimary->et_use->cu_def)
    {
	if (hasY)
	    dstp = extArrayRange(dstp, extArrayPrimXY.p_y,
				FarY(use) - ydiff, FALSE, hasX);
	if (hasX)
	    dstp = extArrayRange(dstp, extArrayPrimXY.p_x,
				FarX(use) - xdiff, hasY, FALSE);
    }
    else
    {
	if (hasY)
	    dstp = extArrayRange(dstp, extArrayInterXY.p_y,
				FarY(use), FALSE, hasX);
	if (hasX)
	    dstp = extArrayRange(dstp, extArrayInterXY.p_x,
				FarX(use), hasY, FALSE);
    }

#undef	Far
#undef	FarX
#undef	FarY

done:
    *dstp++ = '/';
    endp = &name[sizeof name - 1];
    srcp = extNodeName(reg);
    while (dstp < endp && (*dstp++ = *srcp++))
	/* Nothing */;

    *dstp = '\0';
    return (name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayRange --
 *
 * Called by extArrayTileToNode above, we print a range of the form [lo:hi]
 * into the string pointed to by 'dstp'.  Guarantees that lo <= hi.
 *
 * Results:
 *	Returns a pointer to the NULL byte at the end of the string
 *	we print into (dstp).
 *
 * Side effects:
 *	Writes characters into 'dstp', which should be large enough
 *	to hold any possible string of the form [int:int].
 *
 * ----------------------------------------------------------------------------
 */

char *
extArrayRange(dstp, lo, hi, prevRange, followRange)
    char *dstp;
    int lo, hi;
    bool prevRange;	/* TRUE if preceded by a range */
    bool followRange;	/* TRUE if followed by a range */
{
    if (!prevRange) *dstp++ = '[';
    if (hi < lo)
	(void) sprintf(dstp, "%d:%d", hi, lo);
    else
	(void) sprintf(dstp, "%d:%d", lo, hi);
    while (*dstp++) /* Nothing */;
    dstp[-1] = followRange ? ',' : ']';
    *dstp = '\0';
    return (dstp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extArrayHardNode --
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
 *	None.
 *
 * Algorithm:
 *	Search in the appropriate array element to the yank buffer
 *	in question: if 'def' is the primary buffer, search the
 *	element (extArrayPrimXY.p_x, extArrayPrimXY.p_y); otherwise,
 *	search (extArrayInterXY.p_x, extArrayInterXY.p_y).
 *
 *	For each cell we find in the course of a hierarchical search
 *	of this array element in the area of the tile 'tp', trace out
 *	the nodes lying in the area of the overlap, and then do a label
 *	assignment for those nodes.  As soon as we find a label, we're
 *	done.  Otherwise, we reset this def back the way we found it
 *	and continue on to the next cell in our search.
 *
 * ----------------------------------------------------------------------------
 */

LabRegion *
extArrayHardNode(tp, pNum, def, ha)
    Tile *tp;
    int pNum;
    CellDef *def;
    HierExtractArg *ha;
{
    TileType type = TiGetType(tp);
    char labelBuf[4096];
    SearchContext scx;
    HardWay arg;

    arg.hw_ha = ha;
    arg.hw_label = (Label *) NULL;
    arg.hw_mask = DBPlaneTypes[pNum];
    TTMaskAndMask(&arg.hw_mask, &DBConnectTbl[type]);
    arg.hw_tpath.tp_last = &labelBuf[sizeof labelBuf - 3];
    arg.hw_tpath.tp_first = arg.hw_tpath.tp_next = labelBuf;
    arg.hw_prefix = FALSE;
    arg.hw_autogen = FALSE;
    TiToRect(tp, &arg.hw_area);
    scx.scx_use = ha->ha_subUse;

    /* Find a label in the interaction area */
    labelBuf[0] = '\0';
    extArrayHardSearch(def, &arg, &scx, extHardProc);
    if (arg.hw_label == NULL)
    {
	labelBuf[0] = '\0';
	arg.hw_autogen = TRUE;
	extArrayHardSearch(def, &arg, &scx, extHardProc);
    }

    if (arg.hw_label)
    {
	LabRegion *lreg;
	LabelList *ll;

	lreg = (LabRegion *) extGetRegion(tp);
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
 *
 * extArrayHardSearch --
 *
 * Do the actual work of calling (*proc)() either to find a label the hard
 * way, or to create a new label.  Called from extArrayHardNode above.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Those of (*proc)().
 *
 * ----------------------------------------------------------------------------
 */

void
extArrayHardSearch(def, arg, scx, proc)
    CellDef *def;
    HardWay *arg;
    SearchContext *scx;
    int (*proc)();
{
    Transform tinv;

    if (def == extArrayPrimary->et_use->cu_def)
    {
	scx->scx_trans = extArrayPTrans;
	scx->scx_x = extArrayPrimXY.p_x;
	scx->scx_y = extArrayPrimXY.p_y;
    }
    else
    {
	scx->scx_trans = extArrayITrans;
	scx->scx_x = extArrayInterXY.p_x;
	scx->scx_y = extArrayInterXY.p_y;
    }
    GeoInvertTrans(&scx->scx_trans, &tinv);
    GeoTransRect(&tinv, &arg->hw_area, &scx->scx_area);
    (void) (*proc)(scx, arg);
}
