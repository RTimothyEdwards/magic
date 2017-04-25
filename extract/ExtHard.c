/*
 * ExtHard.c --
 *
 * Circuit extraction.
 * Procedures for finding the name of a node during hierarchical
 * extraction, when no label for that node could be found in the
 * interaction area.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtHard.c,v 1.2 2010/06/24 12:37:17 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "utils/styles.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/geofast.h"

/* Forward declarations */

void extHardFreeAll();
bool extHardGenerateLabel();
bool extHardSetLabel();

/*
 * ----------------------------------------------------------------------------
 *
 * extLabFirst --
 * extLabEach --
 *
 * Filter functions passed to ExtFindRegions when tracing out extended
 * label regions as part of hierarchical circuit extraction.  We use the
 * TransRegion record because it allows us to save a pointer to a tile
 * along with the usual LabRegion data.  Since treg_area is not needed,
 * we use it to store the plane of the tile.
 *
 * Results:
 *	extLabFirst returns a pointer to a new TransRegion.
 *	extLabEach returns NULL.
 *
 * Side effects:
 *	Memory is allocated by extLabFirst.
 *	We cons the newly allocated region onto the front of the existing
 *	region list.
 *
 * ----------------------------------------------------------------------------
 */

Region *
extLabFirst(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    TransRegion *reg;
    reg = (TransRegion *) mallocMagic((unsigned) (sizeof (TransRegion)));
    reg->treg_next = (TransRegion *) NULL;
    reg->treg_labels = (LabelList *) NULL;
    reg->treg_pnum = DBNumPlanes;
    reg->treg_area = DBNumPlanes;
    reg->treg_tile = tile;

    /* Prepend it to the region list */
    reg->treg_next = (TransRegion *) arg->fra_region;
    arg->fra_region = (Region *) reg;
    return ((Region *) reg);
}

    /*ARGSUSED*/
int
extLabEach(tile, pNum, arg)
    Tile *tile;
    int pNum;
    FindRegion *arg;
{
    TransRegion *reg = (TransRegion *) arg->fra_region;

    /* Avoid setting the region's tile pointer to a split tile if we can */
    if (IsSplit(reg->treg_tile) && !IsSplit(tile))
    {
	reg->treg_tile = tile;
	reg->treg_area = pNum;
    }

    if (reg->treg_area == DBNumPlanes) reg->treg_area = pNum;
    extSetNodeNum((LabRegion *)reg, pNum, tile);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHardProc --
 *
 * Called for each cell use in a tree.  We determine if there is any
 * geometry in this cell whose type is in the mask arg->hw_mask and
 * whose node name we can determine.
 *
 * If so, we set arg->hw_label to point to a newly allocated Label whose
 * name is the full hierarchical path of the node name we just found, and
 * whose location (lab_rect) lies within the geometry of the node, but has
 * been transformed to root coordinates by scx->scx_trans.
 *
 * Several fields in the HardWay struct pointed to by 'arg' control
 * the details of how we assign a label:
 *
 *	arg->hw_prefix	If this is FALSE, we are responsible for constructing
 *			the full hierarchical pathname for the generated label
 *			starting from the immediate children of the root cell
 *			ha->ha_parentUse->cu_def.  We do this by appending
 *			the use-id of scx->scx_use to arg->hw_tpath before
 *			appending the final component of the label name to
 *			arg->hw_tpath.
 *
 *			If TRUE, we are responsible only for appending segments
 *			to arg->hw_tpath for grandchildren of the root cell and
 *			their descendants; the caller is responsible for the
 *			initial part of the pathname.
 *
 *	arg->hw_autogen	If FALSE, attempt to find an existing label for the
 *			geometry in the node.
 *
 *			If TRUE, generate a label from the canonical nodename
 *			for the first piece of geometry we find.
 *
 * Results:
 *	Returns 0 if the caller (DBCellSrArea) should keep going,
 *	or 1 if we've succeeded and the caller may return.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
extHardProc(scx, arg)
    SearchContext *scx;		/* Context of the search to this cell */
    HardWay *arg;		/* See above; this structure provides both
				 * options to govern how we generate labels
				 * and a place to store the label we return.
				 */
{
    HierExtractArg *ha = arg->hw_ha;
    bool isTopLevel = (scx->scx_use->cu_parent == ha->ha_parentUse->cu_def);
    CellDef *def = scx->scx_use->cu_def;
    TransRegion *reg;
    TransRegion *labRegList;
    char *savenext;
    int ret = 0;

    /*
     * Build up next component of label path.
     * If the caller has already taken care of generating a prefix
     * for immediate children of the root cell, arg->hw_prefix will
     * have been set to FALSE.
     */
    savenext = arg->hw_tpath.tp_next;	/* Will be restored before we return */
    if (arg->hw_prefix || !isTopLevel)
    {
	arg->hw_tpath.tp_next =
	    DBPrintUseId(scx, savenext, arg->hw_tpath.tp_last - savenext,
			FALSE);
	*arg->hw_tpath.tp_next++ = '/';
	*arg->hw_tpath.tp_next = '\0';
    }

    /*
     * We use a TransRegion because it holds both a LabelList
     * and a tile pointer, and call extLabEach to make sure
     * that treg_pnum and treg_ll are kept up-to-date (so we
     * can generate a node label if none is found).
     *
     * The call below may return several TransRegions, as when
     * geometry in a parent overlaps two different nodes in a
     * single child.
     */
    labRegList = (TransRegion *) ExtFindRegions(def, &scx->scx_area,
		    &arg->hw_mask, ExtCurStyle->exts_nodeConn, extUnInit,
		    extLabFirst, extLabEach);
    if (labRegList)
    {
	/*
	 * If labels are being generated automatically on this pass,
	 * we don't bother to assign labels to geometry.  Instead, we
	 * construct a new label based on the lower-leftmost tile in
	 * the Region labRegList.
	 */
	if (arg->hw_autogen)
	{
	    (void) extHardGenerateLabel(scx, labRegList, arg);
	    goto success;
	}

	/*
	 * Assign labels to LabRegions.
	 * Tiles in 'def' that belong to nodes other than those in labRegList
	 * will have uninitialized region pointers, and so will not have labels
	 * assigned to them.
	 */
	// ExtLabelRegions(def, ExtCurStyle->exts_nodeConn, &labRegList,
	//		&scx->scx_area);
	ExtLabelRegions(def, ExtCurStyle->exts_nodeConn, NULL, NULL);

	/* Now try to find a region with a node label */
	for (reg = labRegList; reg; reg = reg->treg_next)
	    if (reg->treg_labels && extHardSetLabel(scx, reg, arg))
		goto success;

	/* No luck; it's as though there was no geometry at all */
	extHardFreeAll(def, labRegList);
    }

    /* No luck; check our subcells recursively */
    ret = DBCellSrArea(scx, extHardProc, (ClientData) arg);
    arg->hw_tpath.tp_next = savenext;
    goto done;

success:
    extHardFreeAll(def, labRegList);
    ret = 1;

done:
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHardSetLabel --
 *
 * We found a LabRegion, 'reg', that has a label list.
 * Walk down its label list looking for a node label.
 * If we find one, append it to the TerminalPath we've
 * been constructing from the root, make a new label
 * whose text is this full pathname, and assign it to
 * arg->hw_label.
 *
 * The coordinates of the result label are those of
 * the node label we find, transformed by scx->scx_trans
 * to lie in the root.
 *
 * Results:
 *	Returns TRUE if we found a node label, FALSE if not.
 *
 * Side effects:
 *	If successful, allocates a new Label struct as described
 *	above, and assigns it to arg->hw_label.
 *
 * ----------------------------------------------------------------------------
 */

bool
extHardSetLabel(scx, reg, arg)
    SearchContext *scx;		/* We use scx->scx_trans to transform label
				 * coordinates in the def scx->scx_use->cu_def
				 * up to root coordinates.
				 */
    TransRegion *reg;		/* Region with a label list */
    HardWay *arg;		/* We will set arg->hw_label if a node
				 * label is found on the label list of 'reg'.
				 */
{
    TerminalPath *tpath = &arg->hw_tpath;
    Label *oldlab, *newlab;
    char *srcp, *dstp;
    LabelList *ll;
    int prefixlen;
    char *text;
    int len;
    Rect r;
    int pNum;
    Tile	*tp;

    for (ll = reg->treg_labels; ll; ll = ll->ll_next)
	if (extLabType(ll->ll_label->lab_text, LABTYPE_NAME)) break;

    if (ll == (LabelList *) NULL)
	return (FALSE);
    oldlab = ll->ll_label;

    /* Compute length of new label */
    prefixlen = tpath->tp_next - tpath->tp_first;
    len = strlen(oldlab->lab_text) + prefixlen;

    /* Allocate a Label big enough to hold the complete path */
    newlab = (Label *) mallocMagic((unsigned) (sizeof (Label) + len - 3));
    r=oldlab->lab_rect;
    if (!GEO_SURROUND(&scx->scx_area,&r))
    {
	 GEOCLIP(&r,&scx->scx_area);

	 pNum = DBPlane(oldlab->lab_type);
         tp = scx->scx_use->cu_def->cd_planes[pNum]->pl_hint;
	 GOTOPOINT(tp, &r.r_ll);
	 scx->scx_use->cu_def->cd_planes[pNum]->pl_hint = tp;
	 if ((TransRegion *)extGetRegion(tp) == reg)
	 {
	      /* found an OK point */
	      r.r_ur.p_x =r.r_ll.p_x+1;
              r.r_ur.p_y =r.r_ll.p_y+1;
	 }
	 else
	 {
	      GOTOPOINT(tp, &r.r_ur);
	      if ((TransRegion *)extGetRegion(tp) == reg)
	      {
	      	   r.r_ll = r.r_ur;
	      }
	      else
	      {
	      	   /* forget it; we're never going to find the damn thing */
		   r=oldlab->lab_rect;
	      }
	      
	 }
    }
    GeoTransRect(&scx->scx_trans, &r, &newlab->lab_rect);
    newlab->lab_type = oldlab->lab_type;
    text = oldlab->lab_text;

    /* Don't care, really, which orientation the label has */
    newlab->lab_just = GEO_NORTH;

    /* Construct the text of the new label */
    dstp = newlab->lab_text;
    if (prefixlen)
    {
	srcp = tpath->tp_first;
	do { *dstp++ = *srcp++; } while (--prefixlen > 0);
    }
    srcp = text;
    while (*dstp++ = *srcp++) /* Nothing */;

    arg->hw_label = newlab;
    if (DebugIsSet(extDebugID, extDebHardWay))
	TxPrintf("Hard way: found label = \"%s\"\n", newlab->lab_text);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHardGenerateLabel --
 *
 * Generate a label automatically from reg->treg_pnum and
 * reg->treg_ll according to the conventions used for node
 * labels in ExtBasic.c.  The label is prefixed with the
 * TerminalPath we've been constructing from the root in
 * arg->hw_tpath.
 *
 * The coordinates of the result label are those of reg->treg_tile->ti_ll,
 * transformed by scx->scx_trans to lie in the root.
 *
 * Results:
 *	Always TRUE.
 *
 * Side effects:
 *	Allocates a new Label struct as described above,
 *	and assigns it to arg->hw_label.
 *
 * ----------------------------------------------------------------------------
 */

bool
extHardGenerateLabel(scx, reg, arg)
    SearchContext *scx;		/* We use scx->scx_trans to transform the
				 * generated label's coordinates up to
				 * root coordinates.
				 */
    TransRegion *reg;		/* Region whose treg_ll and treg_pnum we use
				 * to generate a new label name.
				 */
    HardWay *arg;		/* We set arg->hw_label to the new label */
{
    TerminalPath *tpath = &arg->hw_tpath;
    char *srcp, *dstp;
    Label *newlab;
    int prefixlen;
    char gen[100];
    int len;
    Rect r;
    Point p;

    // Modification 9/9/2014 by Tim:
    // Convert the treg_ll value up to top-level coordinates.
    // Otherwise you end up with a node that is apparently in
    // "canonical coordinates", but if you try to find the
    // location of the node using the name, you'll end up in
    // a random place.  It also allows the low-probability
    // but possible conflict between this node and another with
    // the same name in the parent cell.
    //
    // Reverted 10/30/2014:  Apparently this causes worse
    // problems.
    //
    // GeoTransPoint(&scx->scx_trans, &reg->treg_ll, &r.r_ll);
    // extMakeNodeNumPrint(gen, reg->treg_pnum, r.r_ll);

    extMakeNodeNumPrint(gen, reg->treg_pnum, reg->treg_ll);

    prefixlen = tpath->tp_next - tpath->tp_first;
    len = strlen(gen) + prefixlen;
    newlab = (Label *) mallocMagic((unsigned) (sizeof (Label) + len - 3));
    r.r_ll = reg->treg_tile->ti_ll;
    GEOCLIP(&r,&scx->scx_area);
    r.r_ur.p_x = r.r_ll.p_x+1;
    r.r_ur.p_y = r.r_ll.p_y+1;
    GeoTransRect(&scx->scx_trans, &r, &newlab->lab_rect);
    newlab->lab_type = TiGetType(reg->treg_tile);

    /* Don't care, really, which orientation the label has */
    newlab->lab_just = GEO_NORTH;

    /* Mark this as a generated label;  may or may not be useful */
    newlab->lab_flags = LABEL_GENERATE;

    /* Construct the text of the new label */
    dstp = newlab->lab_text;
    if (prefixlen)
    {
	srcp = tpath->tp_first;
	do { *dstp++ = *srcp++; } while (--prefixlen > 0);
    }
    srcp = gen;
    while (*dstp++ = *srcp++) /* Nothing */;
    arg->hw_label = newlab;
    if (DebugIsSet(extDebugID, extDebHardWay))
	TxPrintf("Hard way: generated label = \"%s\"\n", newlab->lab_text);
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHardFreeAll --
 *
 * Reset all the ti_client fields that we set in 'def' that
 * were set to point to regions in the list 'tReg' of TransRegions.
 * Then free all the regions in 'tReg'.
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
extHardFreeAll(def, tReg)
    CellDef *def;
    TransRegion *tReg;
{
    TransRegion *reg;
    LabelList *ll;
    FindRegion arg;

    /* Don't need to initialize arg.fra_first below */
    arg.fra_connectsTo = ExtCurStyle->exts_nodeConn;
    arg.fra_def = def;
    arg.fra_each = (int (*)()) NULL;
    arg.fra_region = (Region *) extUnInit;

    for (reg = tReg; reg; reg = reg->treg_next)
    {
	/* Reset all ti_client fields to extUnInit */
	arg.fra_uninit = (ClientData) reg;
	if (reg->treg_tile)
	{
	    arg.fra_pNum = reg->treg_area;
	    ExtFindNeighbors(reg->treg_tile, arg.fra_pNum, &arg);
	}

	/* Free all LabelLists and then the region */
	for (ll = reg->treg_labels; ll; ll = ll->ll_next)
	    freeMagic((char *) ll);
	freeMagic((char *) reg);
    }
}
