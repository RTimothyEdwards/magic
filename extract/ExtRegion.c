/*
 * ExtRegion.c --
 *
 * Circuit extraction.
 * This file contains the code to trace out connected Regions
 * in a layout, and to build up or tear down lists of Regions.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtRegion.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"

/*
 * ----------------------------------------------------------------------------
 *
 * ExtFindRegions --
 *
 * Find all the connected geometrical regions in a given area of a CellDef
 * that will correspond to nodes or devices in the extracted circuit.
 * Two procedures are supplied by the caller, 'first' and 'each'.
 *
 * The function 'first' must be non-NULL.  It is called for each tile
 * tile found in the region.  It must return a pointer to a ExtRegion
 * struct (or one of the client forms of a ExtRegion struct; see the
 * comments in extractInt.h).
 *
 *	ExtRegion *
 *	(*first)(tile, arg)
 *	    Tile *tile;		/# Tile is on plane arg->fra_pNum #/
 *	    FindRegion *arg;
 *	{
 *	}
 *
 * If the function 'each' is non-NULL, it is applied once to each tile found
 * in the region:
 *
 *	(*each)(tile, planeNum, arg)
 *	    Tile *tile;
 *	    int planeNum;	/# May be different than arg->fra_pNum #/
 *	    FindRegion *arg;
 *	{
 *	}
 *
 * Results:
 *	Returns a pointer to the first element in the linked list
 *	of ExtRegion structures for this CellDef.  The ExtRegion structs
 *	may in fact contain more than the basic ExtRegion struct; this
 *	will depend on what the function 'first' allocates.
 *
 * Side effects:
 *	Each non-space tile has its ti_client field left pointing
 *	to a ExtRegion structure that describes the region that tile
 *	belongs to.
 *
 * Non-interruptible.  It is the caller's responsibility to check
 * for interrupts.
 *
 * ----------------------------------------------------------------------------
 */

ExtRegion *
ExtFindRegions(def, area, mask, connectsTo, uninit, first, each)
    CellDef *def;		/* Cell definition being searched */
    Rect *area;			/* Area to search initially for tiles */
    TileTypeBitMask *mask;	/* In the initial area search, only visit
				 * tiles whose types are in this mask.
				 */
    TileTypeBitMask *connectsTo;/* Connectivity table for determining regions.
				 * If t1 and t2 are the types of adjacent
				 * tiles, then t1 and t2 belong to the same
				 * region iff:
				 *	TTMaskHasType(&connectsTo[t1], t2)
				 *
				 * We assume that connectsTo[] is symmetric,
				 * so this is the same as:
				 *	TTMaskHasType(&connectsTo[t2], t1)
				 */
    ClientData uninit;		/* Contents of a ti_client field indicating
				 * that the tile has not yet been visited.
				 */
    ExtRegion * (*first)();	/* Applied to first tile in region */
    int (*each)();		/* Applied to each tile in region */
{
    FindRegion arg;
    int extRegionAreaFunc();

    ASSERT(first != NULL, "ExtFindRegions");
    arg.fra_connectsTo = connectsTo;
    arg.fra_def = def;
    arg.fra_uninit = uninit;
    arg.fra_first = first;
    arg.fra_each = each;
    arg.fra_region = (ExtRegion *) NULL;

    /* Make sure temp_subsnode is NULL */
    temp_subsnode = NULL;

    SigDisableInterrupts();
    for (arg.fra_pNum=PL_TECHDEPBASE; arg.fra_pNum<DBNumPlanes; arg.fra_pNum++)
	(void) DBSrPaintClient((Tile *) NULL, def->cd_planes[arg.fra_pNum],
		area, mask, uninit, extRegionAreaFunc, (ClientData) &arg);
    SigEnableInterrupts();

    return (arg.fra_region);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extRegionAreaFunc --
 *
 * Filter function called for each tile found during the area enumeration
 * in ExtFindRegions above.  Only tiles whose ti_client is not already
 * equal to arg->fra_uninit are visited.
 *
 * We call 'fra_first' to allocate a new region struct for it, and then
 * prepend it to the ExtRegion list (ExtRegion *) arg->fra_clientData.  We
 * then call ExtFindNeighbors to trace out recursively all the remaining
 * tiles in the region.
 *
 * Results:
 *	Always returns 0, to cause DBSrPaintClient to continue its search.
 *
 * Side effects:
 *	Allocates a new ExtRegion struct if the tile has not yet been visited.
 *	See also the comments for ExtFindNeighbors.
 *
 * ----------------------------------------------------------------------------
 */

int
extRegionAreaFunc(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    /* Allocate a new region */
    if (arg->fra_first)
	(void) (*arg->fra_first)(tile, arg);

    if (DebugIsSet(extDebugID, extDebAreaEnum))
	extShowTile(tile, "area enum", 0);

    /* Recursively visit all tiles surrounding this one that we connect to */
    (void) ExtFindNeighbors(tile, arg->fra_pNum, arg);
    return (0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * ExtLabelRegions --
 *
 * Given a CellDef whose tiles have been set to point to LabRegions
 * by ExtFindRegions, walk down the label list and assign labels
 * to regions.  If the tile over which a label lies is still uninitialized
 * ie, points to extUnInit, we skip the label.
 *
 * A label is attached to the LabRegion for a tile if the label's
 * type and the tile's type are connected according to the table
 * 'connTo'.  This disambiguates the case where a label lies
 * on the boundary between two tiles of different types.
 *
 * Results:
 *	When called with a NULL nodeList, any default substrate
 *	label found will be returned (in a pointer to a LabelList
 *	structure).  This feature is used by extHardProc().
 *
 * Side effects:
 *	Each LabRegion has labels added to its label list.
 *
 * ----------------------------------------------------------------------------
 */

LabelList *
ExtLabelRegions(def, connTo, nodeList, clipArea)
    CellDef *def;		/* Cell definition being labelled */
    TileTypeBitMask *connTo;	/* Connectivity table (see above) */
    NodeRegion **nodeList;	/* Node list to add to (or NULL)  */
    Rect *clipArea;		/* Area to search for sticky labels */
{
    static Point offsets[] = { { 0, 0 }, { 0, -1 }, { -1, -1 }, { -1, 0 } };
    LabelList *ll;
    Label *lab;
    Tile *tp;
    LabRegion *reg;
    NodeRegion *newNode;
    int quad, pNum, n, nclasses;
    Point p;
    bool found;
    TileType extSubType = 0;
    LabelList *retList = NULL;

    for (lab = def->cd_labels; lab; lab = lab->lab_next)
    {
	found = FALSE;
	pNum = DBPlane(lab->lab_type);
	if (lab->lab_type == TT_SPACE || pNum < PL_TECHDEPBASE)
	    continue;
	/*
	 * See ExtBasic.c:  Labels that do not get output as "equiv"
	 * records in the .ext file cannot be used for merges and
	 * caps.
	 */
	if (lab->lab_port == INFINITY) continue;

	for (quad = 0; quad < 4; quad++)
	{
	    /*
	     * Visit each of the four quadrants surrounding the center
	     * point of the label, searching for a tile whose type matches
	     * that of the label or connects to it.
	     */
	    p.p_x = ((lab->lab_rect.r_xbot + lab->lab_rect.r_xtop) >> 1)
			+ offsets[quad].p_x;
	    p.p_y = ((lab->lab_rect.r_ybot + lab->lab_rect.r_ytop) >> 1)
			+ offsets[quad].p_y;
	    tp = def->cd_planes[pNum]->pl_hint;
	    GOTOPOINT(tp, &p);
	    def->cd_planes[pNum]->pl_hint = tp;
	    if (extConnectsTo(TiGetType(tp), lab->lab_type, connTo)
		    && extHasRegion(tp, extUnInit))
	    {
		found = TRUE;
		reg = (LabRegion *) extGetRegion(tp);
		ll = (LabelList *) mallocMagic((unsigned) (sizeof (LabelList)));
		ll->ll_label = lab;
		if (lab->lab_flags & PORT_DIR_MASK)
		    ll->ll_attr = LL_PORTATTR;
		else
		    ll->ll_attr = LL_NOATTR;

		if ((lab->lab_flags & PORT_DIR_MASK) || (reg->lreg_labels == NULL))
		{
		    ll->ll_next = reg->lreg_labels;
		    reg->lreg_labels = ll;
		}
		else
		{
		    LabelList *fport = reg->lreg_labels;

		    /* Place *after* any labels with LL_PORTATTR */
		    while ((fport->ll_next != NULL) &&
				(fport->ll_next->ll_attr == LL_PORTATTR))
			fport = fport->ll_next;

		    ll->ll_next = fport->ll_next;
		    fport->ll_next = ll;
		}
		break;
	    }
	}
	if (found == FALSE)
	{
	    /* Handle unconnected node label. */

	    /* If the label is the substrate type and is over	*/
	    /* space, then assign the label to the default	*/
	    /* substrate region.  The label need not be in the	*/
	    /* clip area.					*/

	    ll = (LabelList *)NULL;
	    if ((pNum == ExtCurStyle->exts_globSubstratePlane) &&
			TTMaskHasType(&ExtCurStyle->exts_globSubstrateTypes,
			lab->lab_type))
	    {
		if (nodeList != NULL)
		{
		    /* temp_subsnode only defined when extFindNodes()	*/
		    /* was called before ExtLabelRegions()		*/
		    if (temp_subsnode != NULL)
		    {
		    	ll = (LabelList *)mallocMagic(sizeof(LabelList));
		    	ll->ll_label = lab;
		    	if (lab->lab_flags & PORT_DIR_MASK)
			    ll->ll_attr = LL_PORTATTR;
		    	else
			    ll->ll_attr = LL_NOATTR;

			ll->ll_next = temp_subsnode->nreg_labels;
			temp_subsnode->nreg_labels = ll;
		    }
		}
		else
		{
		    ll = (LabelList *)mallocMagic(sizeof(LabelList));
		    ll->ll_label = lab;
		    if (lab->lab_flags & PORT_DIR_MASK)
			ll->ll_attr = LL_PORTATTR;
		    else
			ll->ll_attr = LL_NOATTR;

		    ll->ll_next = (LabelList *)NULL;
		    if (retList != NULL) freeMagic(retList);
		    retList = ll;
		}
	    }

	    /* This may be a "sticky label".  If it is not connected to a
	     * non-electrical type (includes TT_SPACE), then create a new node
	     * region for it.  The label must be within the clip area.
	     */
	    if ((ll == NULL) && (nodeList != NULL) &&
			(GEO_SURROUND(&lab->lab_rect, clipArea) ||
			GEO_TOUCH(&lab->lab_rect, clipArea))
			 && (lab->lab_type != TT_SPACE)
			 && TTMaskHasType(&ExtCurStyle->exts_activeTypes, lab->lab_type))
	    {
		nclasses = ExtCurStyle->exts_numResistClasses;
	    	n = sizeof (NodeRegion) + (sizeof (PerimArea) * (nclasses - 1));
		newNode = (NodeRegion *)mallocMagic((unsigned) n);

		ll = (LabelList *)mallocMagic(sizeof(LabelList));
		ll->ll_label = lab;
		ll->ll_next = NULL;
		if (lab->lab_flags & PORT_DIR_MASK)
		    ll->ll_attr = LL_PORTATTR;
		else
		    ll->ll_attr = LL_NOATTR;

	 	newNode->nreg_next = *nodeList;
		newNode->nreg_pnum = pNum;
		newNode->nreg_type = lab->lab_type;
		newNode->nreg_ll = lab->lab_rect.r_ll;
		newNode->nreg_cap = (CapValue)0;
		newNode->nreg_resist = 0;
		for (n = 0; n < nclasses; n++)
		    newNode->nreg_pa[n].pa_perim = newNode->nreg_pa[n].pa_area = 0;
		newNode->nreg_labels = ll;

		*nodeList = newNode;
	    }
	}
    }
    return retList;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtLabelOneRegion --
 *
 * Same as ExtLabelRegion, but it only assigns labels to one particular
 * region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The region has labels added to its label list.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtLabelOneRegion(def, connTo, reg)
    CellDef *def;		/* Cell definition being labelled */
    TileTypeBitMask *connTo;	/* Connectivity table (see above) */
    NodeRegion  *reg;			/* The region whose labels we want */
{
    static Point offsets[] = { { 0, 0 }, { 0, -1 }, { -1, -1 }, { -1, 0 } };
    LabelList *ll;
    Label *lab;
    Tile *tp;
    int quad, pNum;
    Point p;

    /* Generate segment list for subcircuit boundary, if any */

    for (lab = def->cd_labels; lab; lab = lab->lab_next)
    {
	pNum = DBPlane(lab->lab_type);
	if (lab->lab_type == TT_SPACE || pNum < PL_TECHDEPBASE)
	    continue;
	for (quad = 0; quad < 4; quad++)
	{
	    /*
	     * Visit each of the four quadrants surrounding
	     * the lower-left corner of the label, searching
	     * for a tile whose type matches that of the label
	     * or connects to it.
	     */
	    p.p_x = lab->lab_rect.r_xbot + offsets[quad].p_x;
	    p.p_y = lab->lab_rect.r_ybot + offsets[quad].p_y;
	    tp = def->cd_planes[pNum]->pl_hint;
	    GOTOPOINT(tp, &p);
	    def->cd_planes[pNum]->pl_hint = tp;
	    if (extConnectsTo(TiGetType(tp), lab->lab_type, connTo)
		    && (NodeRegion *) extGetRegion(tp) == reg)
	    {
		ll = (LabelList *) mallocMagic((unsigned) (sizeof (LabelList)));
		ll->ll_label = lab;
		if (lab->lab_flags & PORT_DIR_MASK)
		    ll->ll_attr = LL_PORTATTR;
		else
		    ll->ll_attr = LL_NOATTR;

		if ((lab->lab_flags & PORT_DIR_MASK) || (reg->nreg_labels == NULL))
		{
		    ll->ll_next = reg->nreg_labels;
		    reg->nreg_labels = ll;
		}
		else
		{
		    LabelList *fport = reg->nreg_labels;

		    /* Place *after* any labels with LL_PORTATTR */
		    while ((fport->ll_next != NULL) &&
				(fport->ll_next->ll_attr == LL_PORTATTR))
			fport = fport->ll_next;

		    ll->ll_next = fport->ll_next;
		    fport->ll_next = ll;
		}
		break;
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * ExtResetTiles --
 *
 * Given a CellDef whose tiles have been set to point to Regions
 * by ExtFindRegions, reset all the tiles to uninitialized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the non-space tiles in the CellDef have their ti_client
 *	fields set back to uninitialized.  Does not free the ExtRegion
 *	structs that these tiles point to; that must be done by
 *	ExtFreeRegions, ExtFreeLabRegions, or ExtFreeHierLabRegions.
 *
 * Non-interruptible.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtResetTiles(def, resetTo)
    CellDef *def;
    ClientData resetTo;		/* New value for ti_client */
{
    int pNum;

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	DBResetTilePlane(def->cd_planes[pNum], resetTo);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtFreeRegions --
 * ExtFreeLabRegions --
 * ExtFreeHierLabRegions --
 *
 * Free a list of Regions.
 * ExtFreeLabRegions also frees the LabelLists pointed to by lreg_labels.
 * ExtFreeHierLabRegions, in addition to freeing the LabelLists, frees
 * the labels they point to.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * Non-interruptible.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtFreeRegions(regList)
    ExtRegion *regList;	/* List of regions to be freed */
{
    ExtRegion *reg;

    free_magic1_t mm1 = freeMagic1_init();
    for (reg = regList; reg; reg = reg->reg_next)
	freeMagic1(&mm1, (char *) reg);
    freeMagic1_end(&mm1);
}

void
ExtFreeLabRegions(regList)
    LabRegion *regList;	/* List of regions to be freed */
{
    LabRegion *lreg;
    LabelList *ll;

    free_magic1_t mm1 = freeMagic1_init();
    for (lreg = regList; lreg; lreg = lreg->lreg_next)
    {
	free_magic1_t mm1_ = freeMagic1_init();
	for (ll = lreg->lreg_labels; ll; ll = ll->ll_next)
	    freeMagic1(&mm1_, (char *) ll);
	freeMagic1_end(&mm1_);
	freeMagic1(&mm1, (char *) lreg);
    }
    freeMagic1_end(&mm1);
}

void
ExtFreeHierLabRegions(regList)
    ExtRegion *regList;	/* List of regions to be freed */
{
    ExtRegion *reg;
    LabelList *ll;

    free_magic1_t mm1 = freeMagic1_init();
    for (reg = regList; reg; reg = reg->reg_next)
    {
	free_magic1_t mm1_ = freeMagic1_init();
	for (ll = ((LabRegion *)reg)->lreg_labels; ll; ll = ll->ll_next)
	{
	    freeMagic((char *) ll->ll_label);
	    freeMagic1(&mm1_, (char *) ll);
	}
	freeMagic1_end(&mm1_);
	freeMagic1(&mm1, (char *) reg);
    }
    freeMagic1_end(&mm1);
}
