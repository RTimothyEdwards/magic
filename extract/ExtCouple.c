/*
 * ExtCouple.c --
 *
 * Circuit extraction.
 * Extraction of coupling capacitance.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtCouple.c,v 1.2 2010/06/24 12:37:17 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>		/* For atan() */

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "textio/textio.h"

/* --------------------- Data local to this file ---------------------- */

/* Pointer to hash table currently being updated with coupling capacitance */
HashTable *extCoupleHashPtr;

/* Clipping area for coupling searches */
Rect *extCoupleSearchArea;

/* Def being processed */
CellDef *extOverlapDef;

/* Forward procedure declarations */
int extBasicOverlap(), extBasicCouple();
int extAddOverlap(), extAddCouple();
int extSideLeft(), extSideRight(), extSideBottom(), extSideTop();
int extWalkLeft(), extWalkRight(), extWalkBottom(), extWalkTop();
int extSideOverlap(), extSideOverlapHalo(), extFindOverlap();
void extSideCommon();

/* Structure to pass on to the coupling and sidewall capacitance	*/
/* routines to include the current cell definition and the current	*/
/* plane being searched.						*/

typedef struct _ecs {
    CellDef *def;
    int	     plane;
} extCapStruct;

/* Structure to pass on two planes to check for coupling and the tile	*/
/* which is doing the coupling.						*/

typedef struct _ecpls {
    Tile *tile;
    int  plane_of_tile;
    int  plane_checked;
} extCoupleStruct;

/* Structure to pass on two planes to check for coupling and the	*/
/* boundary which initiated the check.					*/

typedef struct _esws {
    Boundary *bp;
    int       plane_of_boundary;
    int       plane_checked;
    bool      fringe_halo;
    Rect     *area;
    EdgeCap  *extCoupleList;	/* List of sidewall capacitance rules */
    EdgeCap  *extOverlapList;	/* List of overlap capacitance rules */
    CellDef  *def;
} extSidewallStruct;

/* --------------------- Debugging stuff ---------------------- */
#define CAP_DEBUG	FALSE

void extNregAdjustCap(nr, c, str)
    NodeRegion *nr;
    CapValue c;
    char *str;
{
    char *name;
    name = extNodeName((LabRegion *) nr);
    fprintf(stderr, "CapDebug: %s += %f (%s)\n", name, c, str);
}

void extAdjustCouple(he, c, str)
    HashEntry *he;
    CapValue c;
    char *str;
{
    char *name1;
    char *name2;
    CoupleKey *ck;
    ck = (CoupleKey *) he->h_key.h_words;
    name1 = extNodeName((LabRegion *) ck->ck_1);
    name2 = extNodeName((LabRegion *) ck->ck_2);
    fprintf(stderr, "CapDebug: %s-%s += %f (%s)\n", name1, name2, c, str);
}


/*
 * ----------------------------------------------------------------------------
 *
 * extFindCoupling --
 *
 * Find the coupling capacitances in the cell def.  Such capacitances
 * arise from three causes:
 *
 *	Overlap.  When two tiles on different planes overlap, they
 *		  may have a coupling capacitance proportional to
 *		  their areas.  If this is so, we subtract the substrate
 *		  capacitance of the overlapped type, and add the overlap
 *		  capacitance to the coupling hash table.
 *
 *	Sidewall. When tiles on the same plane are adjacent, they may
 *		  have a coupling capacitance proportional to the
 *		  length of their edges, divided by the distance between
 *		  them.  In this case, we just add the sidewall coupling
 *		  capacitance to the hash table.
 *
 *	Sidewall
 *	overlap.  When the edge of a tile on one plane overlaps a tile
 *		  on a different plane, the two tiles may have a coupling
 *		  capacitance proportional to the length of the overlapping
 *		  edge.  In this case we add the coupling capacitance to the
 *		  hash table.  (We may want to deduct the perimeter capacitance
 *		  to substrate?).
 *
 * Requires that ExtFindRegions has been run on 'def' to label all its
 * tiles with NodeRegions.  Also requires that the HashTable 'table'
 * has been initialized by the caller.
 *
 * If 'clipArea' is non-NULL, search for overlap capacitance only inside
 * the area *clipArea.  Search for sidewall capacitance only from tiles
 * inside *clipArea, although this capacitance may be to tiles outside
 * *clipArea.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When done, the HashTable 'table' will have been filled
 *	in with an entry for each pair of nodes having coupling
 *	capacitance.  Each entry will have a two-word key organized
 *	as an CoupleKey struct, with ck_1 and ck_2 pointing to the
 *	coupled nodes.  The value of the hash entry will be the
 *	coupling capacitance between that pair of nodes.
 *
 * ----------------------------------------------------------------------------
 */

void
extFindCoupling(def, table, clipArea)
    CellDef *def;
    HashTable *table;
    Rect *clipArea;
{
    Rect *searchArea;
    int pNum;
    extCapStruct ecs;

    ecs.def = def;

    extCoupleHashPtr = table;
    extCoupleSearchArea = clipArea;
    searchArea = clipArea ? clipArea : &TiPlaneRect;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	ecs.plane = pNum;

	if (PlaneMaskHasPlane(ExtCurStyle->exts_overlapPlanes, pNum))
	    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
			searchArea, &ExtCurStyle->exts_overlapTypes[pNum],
			extBasicOverlap, (ClientData) &ecs);
	if (PlaneMaskHasPlane(ExtCurStyle->exts_sidePlanes, pNum))
	    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
			searchArea, &ExtCurStyle->exts_sideTypes[pNum],
			extBasicCouple, (ClientData) &ecs);
    }

}

/*
 * ----------------------------------------------------------------------------
 *
 * extRelocateSubstrateCoupling ---
 *
 * Move coupling capacitance to the substrate node from the coupling
 * cap table onto the source node's cap-to-substrate record.
 *
 * ----------------------------------------------------------------------------
 */

void
extRelocateSubstrateCoupling(table, subsnode)
    HashTable *table;		/* Coupling capacitance hash table */
    NodeRegion *subsnode;	/* Node record for substrate */
{
    HashEntry *he;
    CoupleKey *ck;
    HashSearch hs;
    CapValue cap;
    NodeRegion *rtp;
    NodeRegion *rbp;

    HashStartSearch(&hs);
    while (he = HashNext(table, &hs))
    {
	cap = extGetCapValue(he);
	if (cap == 0) continue;

	ck = (CoupleKey *) he->h_key.h_words;
	rtp = (NodeRegion *) ck->ck_1;
	rbp = (NodeRegion *) ck->ck_2;

	if (rtp == subsnode)
	{
	    rbp->nreg_cap += cap;
	    extSetCapValue(he, (CapValue)0);
	}
	else if (rbp == subsnode)
	{
	    rtp->nreg_cap += cap;
	    extSetCapValue(he, (CapValue)0);
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * extOutputCoupling --
 *
 * Output the coupling capacitance table built up by extFindCoupling().
 * Each entry in the hash table is a capacitance between the pair of
 * nodes identified by he->h_key, an CoupleKey struct.
 *
 * ExtFindRegions and ExtLabelRegions should have been called prior
 * to this procedure.
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
extOutputCoupling(table, outFile)
    HashTable *table;	/* Coupling capacitance hash table */
    FILE *outFile;	/* Output file */
{
    HashEntry *he;
    CoupleKey *ck;
    HashSearch hs;
    char *text;
    CapValue cap;  /* value of capacitance. */

    HashStartSearch(&hs);
    while (he = HashNext(table, &hs))
    {
	cap = extGetCapValue(he) / ExtCurStyle->exts_capScale;
	if (cap == 0)
	    continue;

	ck = (CoupleKey *) he->h_key.h_words;
	text = extNodeName((LabRegion *) ck->ck_1);
	fprintf(outFile, "cap \"%s\" ", text);
	text = extNodeName((LabRegion *) ck->ck_2);
	fprintf(outFile, "\"%s\" %lg\n", text, cap);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extBasicOverlap --
 *
 * Filter function for overlap capacitance.
 * Called for each tile that might have coupling capacitance
 * to another node because it overlaps a tile or tiles in that
 * node.  Causes an area search over the area of 'tile' in all
 * planes to which 'tile' has overlap capacitance, for any tiles
 * to which 'tile' has overlap capacitance.
 *
 * Results:
 *	Returns 0 to keep DBSrPaintArea() going.
 *
 * Side effects:
 *	See extAddOverlap().
 *
 * ----------------------------------------------------------------------------
 */

int
extBasicOverlap(tile, ecs)
    Tile *tile;
    extCapStruct *ecs;
{
    int thisType;
    int pNum;
    PlaneMask pMask;
    TileTypeBitMask *tMask;
    Rect r;
    CellDef *def = ecs->def;
    int thisPlane = ecs->plane;
    extCoupleStruct ecpls;

    if (IsSplit(tile))
	thisType = (SplitSide(tile)) ? SplitRightType(tile) :
		SplitLeftType(tile);
    else
	thisType = TiGetTypeExact(tile);

    if (DBIsContact(thisType))
	thisType = DBPlaneToResidue(thisType, thisPlane);

    pMask = ExtCurStyle->exts_overlapOtherPlanes[thisType];
    tMask = &ExtCurStyle->exts_overlapOtherTypes[thisType];

    TITORECT(tile, &r);
    extOverlapDef = def;
    if (extCoupleSearchArea)
    {
	GEOCLIP(&r, extCoupleSearchArea);
    }

    ecpls.tile = tile;
    ecpls.plane_of_tile = thisPlane;

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	/* Skip if nothing interesting on the other plane */
	if (pNum == thisPlane || !PlaneMaskHasPlane(pMask, pNum))
	    continue;

	ecpls.plane_checked = pNum;
	(void) DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum], &r, tMask,
		extAddOverlap, (ClientData) &ecpls);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extAddOverlap --
 *
 * We are called for each tile that is overlapped by the tile passed to
 * extBasicOverlap() above (our argument 'tabove').  The intent is that
 * 'tbelow' actually shields 'tabove' from the substrate, so we should
 * replace node(tabove)'s capacitance to substrate with a capacitance
 * to node(tbelow) whose size is proportional to the area of the overlap.
 *
 * We check to insure that tabove is not shielded from tbelow by any
 * intervening material; if it is, we deduct the capacitance between
 * node(tabove) and node(tbelow) for the area of the overlap.
 *
 * Results:
 *	Returns 0 to keep DBSrPaintArea() going.
 *
 * Side effects:
 *	Updates the HashEntry with key node(tbelow), node(tabove)
 *	by adding the capacitance of the overlap if node(tbelow)
 *	and node(tabove) are different, and if they are not totally
 *	shielded by intervening material.  Also subtracts the capacitance
 *	to substrate from node(tabove) for the area of the overlap.
 *	If node(tbelow) and node(tabove) are the same, we do nothing.
 *
 * ----------------------------------------------------------------------------
 */

struct overlap
{
    Rect		 o_clip;
    int			 o_area;
    PlaneMask		 o_pmask;
    TileTypeBitMask	 o_tmask;
};

struct sideoverlap
{
    Rect		  so_clip;
    double		  so_coupfrac;
    double		  so_subfrac;
    int		  	  so_length;
    extSidewallStruct    *so_esws;
    PlaneMask		  so_pmask;
    TileTypeBitMask	  so_tmask;
    TileType		  so_ctype;
};

int
extAddOverlap(tbelow, ecpls)
    Tile *tbelow;
    extCoupleStruct *ecpls;
{
    int extSubtractOverlap(), extSubtractOverlap2();
    NodeRegion *rabove, *rbelow;
    HashEntry *he;
    struct overlap ov;
    TileType ta, tb;
    CoupleKey ck;
    int pNum;
    CapValue c;
    Tile *tabove = ecpls->tile;

    /* Check if both tiles are connected.  If they are, we don't need   */
    /* to check for shielding material, and we don't want to add any    */
    /* coupling capacitance between them.  However, we *do* want to     */
    /* subtract off any substrate (area) capacitance previously added   */
    /* (Correction made 4/29/04 by Tim from a tip by Jeff Sondeen).     */

    rabove = (NodeRegion *) extGetRegion(tabove);
    rbelow = (NodeRegion *) extGetRegion(tbelow);

    /* Quick check on validity of tile's ti_client record */
    if (rbelow == (NodeRegion *)CLIENTDEFAULT) return 0;
    if (rabove == (NodeRegion *)CLIENTDEFAULT) return 0;

    /* Compute the area of overlap */
    ov.o_clip.r_xbot = MAX(LEFT(tbelow), LEFT(tabove));
    ov.o_clip.r_xtop = MIN(RIGHT(tbelow), RIGHT(tabove));
    ov.o_clip.r_ybot = MAX(BOTTOM(tbelow), BOTTOM(tabove));
    ov.o_clip.r_ytop = MIN(TOP(tbelow), TOP(tabove));
    if (extCoupleSearchArea)
    {
	GEOCLIP(&ov.o_clip, extCoupleSearchArea);
    }
    ov.o_area = (ov.o_clip.r_ytop - ov.o_clip.r_ybot)
	      * (ov.o_clip.r_xtop - ov.o_clip.r_xbot);
    ta = TiGetType(tabove);
    tb = TiGetType(tbelow);

    /* Revert any contacts to their residues */

    if (DBIsContact(ta))
	ta = DBPlaneToResidue(ta, ecpls->plane_of_tile);

    if (DBIsContact(tb))
	tb = DBPlaneToResidue(tb, ecpls->plane_checked);

    /*
     * Find whether rabove and rbelow are shielded by intervening material.
     * Deduct the area shielded from the area of the overlap, so we adjust
     * the overlap capacitance correspondingly.
     */
    if (ov.o_pmask = ExtCurStyle->exts_overlapShieldPlanes[ta][tb])
    {
	ov.o_tmask = ExtCurStyle->exts_overlapShieldTypes[ta][tb];
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	{
	    if (!PlaneMaskHasPlane(ov.o_pmask, pNum)) continue;
	    ov.o_pmask &= ~(PlaneNumToMaskBit(pNum));
	    if (ov.o_pmask == 0)
	    {
		(void) DBSrPaintArea((Tile *) NULL,
		    extOverlapDef->cd_planes[pNum], &ov.o_clip, &ov.o_tmask,
		    extSubtractOverlap, (ClientData) &ov);
	    }
	    else
	    {
		(void) DBSrPaintArea((Tile *) NULL,
		    extOverlapDef->cd_planes[pNum], &ov.o_clip, &DBAllTypeBits,
		    extSubtractOverlap2, (ClientData) &ov);
	    }
	    break;
	}
    }

    /* If any capacitance remains, add this record to the table */
    if (ov.o_area > 0)
    {
	int oa = ExtCurStyle->exts_planeOrder[ecpls->plane_of_tile];
	int ob = ExtCurStyle->exts_planeOrder[ecpls->plane_checked];
	if (oa > ob)
	{
	    Tile *tp;
	    TileType t, tres;
	    TileTypeBitMask *mask;
	    int len;
	    CapValue cp;
	    /*
	     * Subtract the substrate capacitance from tabove's region due to
	     * the area of the overlap, minus any shielded area.  The shielded
	     * areas get handled later, when processing coupling between tabove
	     * and the shielding tile.  (Tabove was the overlapping tile, so it
	     * is shielded from the substrate by tbelow if the Tabove plane is
	     * above the Tbelow plane).
	     */
	    rabove->nreg_cap -= ExtCurStyle->exts_areaCap[ta] * ov.o_area;
	    if (CAP_DEBUG)
		extNregAdjustCap(rabove,
		    -(ExtCurStyle->exts_areaCap[ta] * ov.o_area),
		    "obsolete_overlap");

	} else if (CAP_DEBUG)
	    extNregAdjustCap(rabove, 0.0,
		"obsolete_overlap (skipped, wrong direction)");

        /* If the regions are the same, skip this part */
        if (rabove == rbelow) return (0);

	/* Find the coupling hash record */
	if (rabove < rbelow) ck.ck_1 = rabove, ck.ck_2 = rbelow;
	else ck.ck_1 = rbelow, ck.ck_2 = rabove;
	he = HashFind(extCoupleHashPtr, (char *) &ck);

	/* Add the overlap capacitance to the table */
	c = extGetCapValue(he);
	c += ExtCurStyle->exts_overlapCap[ta][tb] * ov.o_area;
	if (CAP_DEBUG)
	    extAdjustCouple(he, ExtCurStyle->exts_overlapCap[ta][tb] *
		ov.o_area, "overlap");
	extSetCapValue(he, c);
    }
    return (0);
}

/* Simple overlap.  The area of overlap is subtracted from ov->o_area */

int
extSubtractOverlap(tile, ov)
    Tile *tile;
    struct overlap *ov;
{
    Rect r;
    int area;

    TITORECT(tile, &r);
    GEOCLIP(&r, &ov->o_clip);
    area = (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);
    if (area > 0)
	ov->o_area -= area;

    return (0);
}

/* Recursive shielding overlap check.  If the tile shields,	*/
/* then the area of overlap is subtracted from ov->o_area.  If	*/
/* not, then this routine is called recursively on the next	*/
/* shielding plane.						*/

int
extSubtractOverlap2(tile, ov)
    Tile *tile;
    struct overlap *ov;
{
    struct overlap ovnew;
    int area, pNum;
    Rect r;

    TITORECT(tile, &r);
    GEOCLIP(&r, &ov->o_clip);
    area = (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);
    if (area <= 0)
	return (0);

    /* This tile shields everything below */
    if (TTMaskHasType(&ov->o_tmask, TiGetType(tile)))
    {
	ov->o_area -= area;
	return (0);
    }

    /* Tile doesn't shield, so search next plane */
    ovnew = *ov;
    ovnew.o_clip = r;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	if (!PlaneMaskHasPlane(ovnew.o_pmask, pNum)) continue;
	ovnew.o_pmask &= ~(PlaneNumToMaskBit(pNum));
	if (ovnew.o_pmask == 0)
	{
	    (void) DBSrPaintArea((Tile *) NULL,
		extOverlapDef->cd_planes[pNum], &ovnew.o_clip, &ovnew.o_tmask,
		extSubtractOverlap, (ClientData) &ovnew);
	}
	else
	{
	    (void) DBSrPaintArea((Tile *) NULL,
		extOverlapDef->cd_planes[pNum], &ovnew.o_clip, &DBAllTypeBits,
		extSubtractOverlap2, (ClientData) &ovnew);
	}
	break;
    }
    ov->o_area = ovnew.o_area;

    return (0);
}

/* Side overlap shielding.  The fraction of the side overlap (fringe)
 * capacitance over this area is added to ov->o_frac, so that it can
 * be subtracted from the total.
 */

int
extSubtractSideOverlap(tile, sov)
    Tile *tile;
    struct sideoverlap *sov;
{
    Rect r;
    int area, dnear, dfar, length;
    double mult, snear, sfar;
    TileType ta, tb;
    Boundary *bp = sov->so_esws->bp;

    TITORECT(tile, &r);
    GEOCLIP(&r, &sov->so_clip);
    area = (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);
    if (area <= 0) return 0;

    ta = TiGetType(bp->b_inside);
    tb = sov->so_ctype;

    if (bp->b_segment.r_xtop == bp->b_segment.r_xbot)
	length = r.r_ytop - r.r_ybot;
    else
	length = r.r_xtop - r.r_xbot;

    switch (bp->b_direction)
    {
	case BD_LEFT:	/* Tile tp is to the left of the boundary */
	    dnear = bp->b_segment.r_xbot - r.r_xtop;
	    dfar = bp->b_segment.r_xbot - r.r_xbot;
	    break;
	case BD_RIGHT:	/* Tile tp is to the right of the boundary */
	    dnear = r.r_xbot - bp->b_segment.r_xtop;
	    dfar = r.r_xtop - bp->b_segment.r_xtop;
	    break;
	case BD_BOTTOM:	/* Tile tp is below the boundary */
	    dnear = bp->b_segment.r_ybot - r.r_ytop;
	    dfar = bp->b_segment.r_ybot - r.r_ybot;
	    break;
	case BD_TOP:	/* Tile tp is above the boundary */
	    dnear = r.r_ybot - bp->b_segment.r_ytop;
	    dfar = r.r_ytop - bp->b_segment.r_ytop;
	    break;
    }

    if (dnear < 0) dnear = 0;	/* Don't count underlap */
    mult = ExtCurStyle->exts_overlapMult[ta][0];
    snear = 0.6366 * atan(mult * dnear);
    sfar = 0.6366 * atan(mult * dfar);

    /* "sfar - snear" is the fractional portion of the fringe cap	*/
    /* seen by the substrate in the direction perpendicular to the edge	*/
    /* generating the fringe cap.  This is multiplied by the fraction	*/
    /* of the total edge length to get the total fraction of the entire	*/
    /* fringe capacitance being shielded.				*/

    sov->so_subfrac += (sfar - snear) * ((double)length / (double)sov->so_length);

    /* Do the same calculation but the the overlap multiplier for the	*/
    /* coupling layer, since the fringe capacitance has a different	*/
    /* halo than for the substrate.					*/

    if (ExtCurStyle->exts_overlapMult[ta][tb] != mult)
    {
	mult = ExtCurStyle->exts_overlapMult[ta][tb];
	snear = 0.6366 * atan(mult * dnear);
	sfar = 0.6366 * atan(mult * dfar);
    }
    sov->so_coupfrac += (sfar - snear) * ((double)length / (double)sov->so_length);

    return (0);
}

/* Recursive shielding side overlap check.  If the tile shields	*/
/* then the area of overlap is subtracted from ov->o_area.  If	*/
/* not, then this routine is called recursively on the next	*/
/* shielding plane.						*/

int
extSubtractSideOverlap2(tile, sov)
    Tile *tile;
    struct sideoverlap *sov;
{
    struct sideoverlap sovnew;
    int area, pNum;
    Rect r;

    TITORECT(tile, &r);
    GEOCLIP(&r, &sov->so_clip);
    area = (r.r_xtop - r.r_xbot) * (r.r_ytop - r.r_ybot);
    if (area <= 0)
	return (0);

    /* This tile shields everything below */
    if (TTMaskHasType(&sov->so_tmask, TiGetType(tile)))
    {
	extSubtractSideOverlap(tile, sov);
	return (0);
    }

    /* Tile doesn't shield, so search next plane */
    sovnew = *sov;
    sovnew.so_clip = r;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	if (!PlaneMaskHasPlane(sovnew.so_pmask, pNum)) continue;
	sovnew.so_pmask &= ~(PlaneNumToMaskBit(pNum));
	if (sovnew.so_pmask == 0)
	{
	    (void) DBSrPaintArea((Tile *) NULL,
		extOverlapDef->cd_planes[pNum], &sovnew.so_clip, &sovnew.so_tmask,
		extSubtractSideOverlap, (ClientData) &sovnew);
	}
	else
	{
	    (void) DBSrPaintArea((Tile *) NULL,
		extOverlapDef->cd_planes[pNum], &sovnew.so_clip, &DBAllTypeBits,
		extSubtractSideOverlap2, (ClientData) &sovnew);
	}
	break;
    }
    sov->so_subfrac = sovnew.so_subfrac;
    sov->so_coupfrac = sovnew.so_coupfrac;

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extBasicCouple --
 *
 * Filter function for sidewall coupling capacitance.
 * Called for each tile that might have coupling capacitance
 * to another node because it is near tiles on the same plane,
 * or because its edge overlaps tiles on a different plane.
 *
 * Causes an area search over a halo surrounding each edge of
 * 'tile' for edges to which each edge has coupling capacitance
 * on this plane, and a search for tiles on different planes that
 * this edge overlaps.
 *
 * Results:
 *	Returns 0 to keep DBSrPaintArea() going.
 *
 * Side effects:
 *	See extAddCouple().
 *
 * ----------------------------------------------------------------------------
 */

int
extBasicCouple(tile, ecs)
    Tile *tile;
    extCapStruct *ecs;
{
    (void) extEnumTilePerim(tile, ExtCurStyle->exts_sideEdges[TiGetType(tile)],
			ecs->plane, extAddCouple, (ClientData) ecs);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extAddCouple --
 *
 * Called for each segment along the boundary of the tile bp->b_inside
 * that might have coupling capacitance with its neighbors.
 * Causes an area search over a halo surrounding the boundary bp->b_segment
 * on the side outside bp->b_inside for edges to which this one has coupling
 * capacitance on this plane, and for tiles overlapping this edge on different
 * planes.
 *
 * Results:
 *	Returns 0 to keep DBSrPaintArea() going.
 *
 * Side effects:
 *	For each edge (tnear, tfar) we find that has coupling capacitance
 *	to us, update the HashEntry with key node(bp->b_inside), node(tfar)
 *	by adding the sidewall capacitance if node(bp->b_inside) and node(tfar)
 *	are different.  If node(bp->b_inside) and node(tfar) are the same, we
 *	do nothing.
 *
 *	For each tile tp we find on a different plane that overlaps this
 *	edge, update the HashEntry with key node(bp->b_inside), node(tp)
 *	by adding the sidewall overlap capacitance.  If node(bp->b_inside)
 *	and node(tp) are the same, do nothing.
 *
 * ----------------------------------------------------------------------------
 */

int
extAddCouple(bp, ecs)
    Boundary *bp;	/* Boundary being considered */
    extCapStruct *ecs;
{
    TileType tin = TiGetType(bp->b_inside), tout = TiGetType(bp->b_outside);
    int pNum;
    PlaneMask pMask;
    Boundary bpCopy;
    Rect r, ovr;
    extSidewallStruct esws;
    int distFringe;

    /* Check here for a zero exts_sideCoupleOtherEdges mask.
     * that handles cases such as FET types not being declared in
     * defaultperimeter, as the edge between poly and FET will be
     * seen as a boundary.  The lack of any area coupling should
     * then prevent it from being checked for fringe cap.
     */

    if (TTMaskIsZero(&ExtCurStyle->exts_sideCoupleOtherEdges[tin][tout]))
	return 0;

    /* Revert any edge contacts to their residues */
    if (DBIsContact(tin))
	tin = DBPlaneToResidue(tin, ecs->plane);
    if (DBIsContact(tout))
	tout = DBPlaneToResidue(tout, ecs->plane);

    esws.extCoupleList = ExtCurStyle->exts_sideCoupleCap[tin][tout];
    esws.extOverlapList = ExtCurStyle->exts_sideOverlapCap[tin][tout];
    if ((esws.extCoupleList == NULL) && (esws.extOverlapList == NULL))
	return (0);

    esws.def = ecs->def;

    /*
     * Clip the edge of interest to the area where we're searching
     * for coupling capacitance, if such an area has been specified.
     */
    if (extCoupleSearchArea)
    {
	bpCopy = *bp;
	bp = &bpCopy;

	if (!GEO_OVERLAP(&bp->b_segment, extCoupleSearchArea))
	    return 0;

	GEOCLIP(&bp->b_segment, extCoupleSearchArea);
    }
    r = ovr = bp->b_segment;

    /* If considering fringe capacitance to be distributed over	*/
    /* a halo surrounding the edge of a shape, then set the	*/
    /* fringe distance to the halo value.  Otherwise, the	*/
    /* fringe cap is (unrealistically) assumed to couple only 	*/
    /* to shapes that are directly below the edge.		*/

    esws.fringe_halo = (ExtOptions & EXT_DOFRINGEHALO) ? 
		((ExtCurStyle->exts_sideCoupleHalo == 0) ? FALSE : TRUE)
		: FALSE;

    distFringe = (ExtOptions & EXT_DOFRINGEHALO) ?
		ExtCurStyle->exts_sideCoupleHalo : 1;

    if (distFringe == 0) distFringe = 1;

    esws.bp = bp;
    esws.plane_of_boundary = ecs->plane;
    esws.area = &ovr;

    switch (bp->b_direction)
    {
	case BD_LEFT:	/* Along left */
	    r.r_xbot -= ExtCurStyle->exts_sideCoupleHalo;
	    ovr.r_xbot -= distFringe;
	    extWalkLeft(&r,
			&ExtCurStyle->exts_sideCoupleOtherEdges[tin][tout],
			extSideLeft, bp, &esws);
	    break;
	case BD_RIGHT:	/* Along right */
	    r.r_xtop += ExtCurStyle->exts_sideCoupleHalo;
	    ovr.r_xtop += distFringe;
	    extWalkRight(&r,
			&ExtCurStyle->exts_sideCoupleOtherEdges[tin][tout],
			extSideRight, bp, &esws);
	    break;
	case BD_TOP:	/* Along top */
	    r.r_ytop += ExtCurStyle->exts_sideCoupleHalo;
	    ovr.r_ytop += distFringe;
	    extWalkTop(&r,
			&ExtCurStyle->exts_sideCoupleOtherEdges[tin][tout],
			extSideTop, bp, &esws);
	    break;
	case BD_BOTTOM:	/* Along bottom */
	    r.r_ybot -= ExtCurStyle->exts_sideCoupleHalo;
	    ovr.r_ybot -= distFringe;
	    extWalkBottom(&r,
			&ExtCurStyle->exts_sideCoupleOtherEdges[tin][tout],
			extSideBottom, bp, &esws);
	    break;
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extRemoveSubcap --
 *
 * A nearby shape blocks parasitic fringe capacitance from a layer boundary
 * to substrate, and so all parasitic fringe capacitance from the layer's
 * region to substrate that is blocked must be removed from the total
 * substrate capacitance for that region.
 *
 * ----------------------------------------------------------------------------
 */

void
extRemoveSubcap(bp, clip, esws)
    Boundary *bp;		/* Boundary with fringe capacitance */
    Rect *clip;			/* Area not being blocked */
    extSidewallStruct *esws;	/* Overlapping edge and plane information */
{
    int dnear, length;
    double snear, cfrac;
    NodeRegion *rbp;
    TileType ta, tb;
    float mult;
    CapValue subcap;

    if (!esws->fringe_halo) return;

    ta = TiGetType(bp->b_inside);
    tb = TiGetType(bp->b_outside);
    rbp = (NodeRegion *)extGetRegion(bp->b_inside);

    if (bp->b_segment.r_xtop == bp->b_segment.r_xbot)
	length = bp->b_segment.r_ytop - bp->b_segment.r_ybot;
    else
	length = bp->b_segment.r_xtop - bp->b_segment.r_xbot;

    switch (bp->b_direction)
    {
	case BD_LEFT:	/* Tile tp is to the left of the boundary */
	    dnear = bp->b_segment.r_xbot - clip->r_xbot;
	    break;
	case BD_RIGHT:	/* Tile tp is to the right of the boundary */
	    dnear = clip->r_xtop - bp->b_segment.r_xtop;
	    break;
	case BD_BOTTOM:	/* Tile tp is below the boundary */
	    dnear = bp->b_segment.r_ybot - clip->r_ybot;
	    break;
	case BD_TOP:	/* Tile tp is above the boundary */
	    dnear = clip->r_ytop - bp->b_segment.r_ytop;
	    break;
    }

    if (dnear < 0) dnear = 0;	/* Don't count underlap */
    mult = ExtCurStyle->exts_overlapMult[ta][0];
    snear = 0.6366 * atan((double)mult * dnear);

    /* "snear" is the fractional portion of the fringe cap seen by	*/
    /* the substrate, so (1.0 - snear) is the part that is blocked.	*/

    subcap = ExtCurStyle->exts_perimCap[ta][tb] * (1.0 - snear) * length;
    rbp->nreg_cap -= subcap;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extFindOverlap --
 *
 * Callback function for extWalkTop/Bottom/Right/Left to find the
 * side overlap (fringe) capacitance from a material edge (held in
 * esws->bp) and all layers in planes below to which its fringe
 * capacitance may couple.  The area to search will have been reduced
 * by any nearby layers on the same plane that shield the fringe
 * capacitance.
 *
 * Results:
 *	Returns 0 to keep extWalk*() going.
 *
 * Side effects:
 *	See side effects of the called function extSideOverlap()
 *
 * ----------------------------------------------------------------------------
 */


int
extFindOverlap(tp, area, esws)
    Tile *tp;			/* Overlapped tile */
    Rect *area;			/* Area to check for coupling */
    extSidewallStruct *esws;	/* Overlapping edge and plane information */
{
    PlaneMask pMask;
    int pNum;
    Rect *rsave;
    Boundary *bp = esws->bp;
    TileType tin = TiGetType(bp->b_inside);
    TileType tout = TiGetType(bp->b_outside);

    pMask = ExtCurStyle->exts_sideOverlapOtherPlanes[tin][tout];
    extOverlapDef = esws->def;

    /* Replace esws->area with area */
    rsave = esws->area;
    esws->area = area;

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(pMask, pNum))
	{
	    esws->plane_checked = pNum;
	    (void) DBSrPaintArea((Tile *) NULL, esws->def->cd_planes[pNum],
			area, &ExtCurStyle->exts_sideOverlapOtherTypes[tin][tout],
			(esws->fringe_halo) ? extSideOverlapHalo : extSideOverlap,
			(ClientData)esws);
	}
    esws->area = rsave;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSideOverlapHalo --
 *
 * The boundary 'bp' has been found to overlap the tile 'tp', which it
 * has coupling capacitance to.
 *
 * Every tile that couples to an edge is also shielding the substrate
 * from that edge.  To maintain the proper accounting of the amount of
 * substrate shielded, calculate only for the area of the substrate that
 * couples to the *unshielded* portion of the tile.  The part of the
 * substrate that is under a shielded portion of the tile will be handled
 * later when calculating coupling to that shielding tile.
 * 
 *
 * Results:
 *	Returns 0 to keep DBSrPaintArea() going.
 *
 * Side effects:
 *	Update the coupling capacitance between node(bp->t_inside) and
 *	node(tp) if the two nodes are different.  Does so by updating
 *	the value stored in the HashEntry keyed by the two nodes.
 *
 * ----------------------------------------------------------------------------
 */

int
extSideOverlapHalo(tp, esws)
    Tile *tp;			/* Overlapped tile */
    extSidewallStruct *esws;	/* Overlapping edge and plane information */
{
    Boundary *bp = esws->bp;	/* Overlapping edge */
    NodeRegion *rtp = (NodeRegion *) extGetRegion(tp);
    NodeRegion *rbp = (NodeRegion *) extGetRegion(bp->b_inside);
    TileType ta, tb;
    Rect tpr;
    struct sideoverlap sov;
    HashEntry *he;
    EdgeCap *e;
    int length;
    double cfrac, sfrac, afrac, mult, efflength;
    CapValue cap, subcap;
    CoupleKey ck;
    int dfar, dnear;
    double sfar, snear, subfrac;

    /* Nothing to do for space tiles, so just return. */
    /* (TO DO:  Make sure TT_SPACE is removed from all exts_sideOverlapOtherTypes */
    tb = TiGetType(tp);
    if (tb == TT_SPACE) return (0);

    /* Get the area of the coupling tile, and clip to the fringe area	*/
    /* of the tile edge generating the fringe capacitance.		*/
    TITORECT(tp, &sov.so_clip);
    GEOCLIP(&sov.so_clip, esws->area);
 
    /* Calculate the length of the clipped area */

    if (bp->b_segment.r_xtop == bp->b_segment.r_xbot)
	length = sov.so_clip.r_ytop - sov.so_clip.r_ybot;
    else
	length = sov.so_clip.r_xtop - sov.so_clip.r_xbot;

    /* ta is the tile type of the edge generating the fringe cap. */
    ta = TiGetType(bp->b_inside);

    /* Revert any contacts to their residues */
    if (DBIsContact(ta))
	ta = DBPlaneToResidue(ta, esws->plane_of_boundary);
    if (DBIsContact(tb))
	tb = DBPlaneToResidue(tb, esws->plane_checked);

    /* Find the fraction of the fringe cap seen by tile tp (depends	*/
    /* on the tile width and distance from the boundary)		*/
    switch (bp->b_direction)
    {
	case BD_LEFT:	/* Tile tp is to the left of the boundary */
	    dfar = bp->b_segment.r_ll.p_x - sov.so_clip.r_xbot;
	    dnear = bp->b_segment.r_ll.p_x - sov.so_clip.r_xtop;
	    break;
	case BD_RIGHT:	/* Tile tp is to the right of the boundary */
	    dfar = sov.so_clip.r_xtop - bp->b_segment.r_ur.p_x;
	    dnear = sov.so_clip.r_xbot - bp->b_segment.r_ur.p_x;
	    break;
	case BD_BOTTOM:	/* Tile tp is below the boundary */
	    dfar = bp->b_segment.r_ll.p_y - sov.so_clip.r_ybot;
	    dnear = bp->b_segment.r_ll.p_y - sov.so_clip.r_ytop;
	    break;
	case BD_TOP:	/* Tile tp is above the boundary */
	    dfar = sov.so_clip.r_ytop - bp->b_segment.r_ur.p_y;
	    dnear = sov.so_clip.r_ybot - bp->b_segment.r_ur.p_y;
	    break;
    }
    if (dnear < 0) dnear = 0;	/* Don't count underlap */
    mult = ExtCurStyle->exts_overlapMult[ta][tb];
    sfar = 0.6366 * atan(mult * dfar);
    snear = 0.6366 * atan(mult * dnear);

    /* "cfrac" is the fractional portion of the fringe cap seen	*/
    /* by tile tp along its length.  This is independent of the	*/
    /* portion of the boundary length that tile tp occupies.	*/
	
    cfrac = sfar - snear;

    /* The fringe portion extracted from the substrate will be	*/
    /* different than the portion added to the coupling layer.	*/

    if (ExtCurStyle->exts_overlapMult[ta][0] != mult)
    {
	mult = ExtCurStyle->exts_overlapMult[ta][0];
	sfar = 0.6366 * atan(mult * dfar);
	snear = 0.6366 * atan(mult * dnear);
    }
    sfrac = sfar - snear;

    /* Apply each rule, incorporating shielding into the edge length. */
    cap = subcap = (CapValue) 0;
    subfrac = 0.0;
    for (e = esws->extOverlapList; e; e = e->ec_next)
    {
	/* Only apply rules for the plane in which they are declared */
	if (!PlaneMaskHasPlane(e->ec_pmask, esws->plane_checked)) continue;

	/* Does this rule "e" include the tile we found? */
	if (TTMaskHasType(&e->ec_near, TiGetType(tp)))
	{
	    /* We have a possible capacitor, but are the tiles shielded from
	     * each other part of the way?
	     */
	    int pNum;
	    sov.so_pmask = ExtCurStyle->exts_sideOverlapShieldPlanes[ta][tb];
	    sov.so_esws = esws;
	    sov.so_coupfrac = (double)0.0;
	    sov.so_subfrac = (double)0.0;
	    sov.so_length = length;
	    sov.so_ctype = tb;

	    if (sov.so_pmask)
	    {
		sov.so_tmask = e->ec_far;  /* Actually shieldtypes. */
		for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		{
		    /* Each call to DBSrPaintArea has an opportunity to
		     * subtract a partial capacitance from the total.
		     */
		    if (!PlaneMaskHasPlane(sov.so_pmask, pNum)) continue;
		    sov.so_pmask &= ~(PlaneNumToMaskBit(pNum));
		    if (sov.so_pmask == 0)
		    {
			(void) DBSrPaintArea((Tile *) NULL,
			    extOverlapDef->cd_planes[pNum], &sov.so_clip,
			    &sov.so_tmask, extSubtractSideOverlap, (ClientData) &sov);
		    }
		    else
		    {
			(void) DBSrPaintArea((Tile *) NULL,
			    extOverlapDef->cd_planes[pNum], &sov.so_clip,
			    &DBAllTypeBits,
			    extSubtractSideOverlap2, (ClientData) &sov);
		    }
		    break;
		}
	    }
	    if (rtp != rbp)
	    {
		efflength = (cfrac - sov.so_coupfrac) * (double)length;
		cap += e->ec_cap * efflength;

		subfrac += sov.so_subfrac;	/* Just add the shielded fraction */
	    }
	}
    }

    /* Add in the new capacitance. */

    if (tb != TT_SPACE)
    {
	int oa = ExtCurStyle->exts_planeOrder[esws->plane_of_boundary];
	int ob = ExtCurStyle->exts_planeOrder[esws->plane_checked];
	if (oa > ob)
	{
	    /* If the overlapped tile is between the substrate and the boundary
	     * tile, then we subtract the fringe substrate capacitance
	     * from rbp's region due to the area of the sideoverlap, since
	     * we now know it is shielded from the substrate.
	     */
	    TileType outtype = TiGetType(bp->b_outside);

	    /* Decompose contacts into their residues */
	    if (DBIsContact(ta))
		ta = DBPlaneToResidue(ta, esws->plane_of_boundary);
	    if (DBIsContact(outtype))
		outtype = DBPlaneToResidue(outtype, esws->plane_of_boundary);

	    efflength = (sfrac - subfrac) * (double)length;
	    subcap = ExtCurStyle->exts_perimCap[ta][0] * efflength;
	    rbp->nreg_cap -= subcap;
	    /* Ignore residual error at ~zero zeptoFarads.  Probably	*/
	    /* there should be better handling of round-off here.	*/
	    if ((rbp->nreg_cap > -0.001) && (rbp->nreg_cap < 0.001))
		rbp->nreg_cap = 0;
	    if (CAP_DEBUG)
	    	extNregAdjustCap(rbp, -subcap, "obsolete_perimcap");
    	}
	else if (CAP_DEBUG)
	    extNregAdjustCap(rbp, 0.0, "obsolete_perimcap (skipped, wrong direction)");

	/* If the nodes are electrically connected, then we don't add	*/
	/* any side overlap capacitance to the node.			*/
	if (rtp == rbp) return 0;
    	if (rtp == (NodeRegion *)CLIENTDEFAULT) return 0;
    	if (rbp == (NodeRegion *)CLIENTDEFAULT) return 0;

	if (rtp < rbp)
	{
	    ck.ck_1 = rtp;
	    ck.ck_2 = rbp;
	}
	else
	{
	    ck.ck_1 = rbp;
	    ck.ck_2 = rtp;
	}
	he = HashFind(extCoupleHashPtr, (char *) &ck);
	if (CAP_DEBUG) extAdjustCouple(he, cap, "sideoverlap");
	extSetCapValue(he, cap + extGetCapValue(he));
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSideOverlap --
 *
 * The boundary 'bp' has been found to overlap the tile 'tp', which it
 * has coupling capacitance to.  This is legacy behavior when no fringe
 * halo is considered, but the fringe is modeled as being directed
 * downward from an edge onto any layer directly below the edge, and
 * occupying no area.  This approximation is generally only reasonable for
 * processes down to 0.5um or so.
 *
 * Results:
 *	Returns 0 to keep DBSrPaintArea() going.
 *
 * Side effects:
 *	Update the coupling capacitance between node(bp->t_inside) and
 *	node(tp) if the two nodes are different.  Does so by updating
 *	the value stored in the HashEntry keyed by the two nodes.
 *
 * ----------------------------------------------------------------------------
 */

int
extSideOverlap(tp, esws)
    Tile *tp;			/* Overlapped tile */
    extSidewallStruct *esws;	/* Overlapping edge and plane information */
{
    Boundary *bp = esws->bp;	/* Overlapping edge */
    NodeRegion *rtp = (NodeRegion *) extGetRegion(tp);
    NodeRegion *rbp = (NodeRegion *) extGetRegion(bp->b_inside);
    TileType ta, tb;
    Rect tpr;
    struct overlap ov;
    HashEntry *he;
    EdgeCap *e;
    int length, areaAccountedFor, areaTotal;
    double afrac;
    CapValue cap;
    CoupleKey ck;

    /* Nothing to do for space tiles, so just return. */
    /* (TO DO:  Make sure TT_SPACE is removed from all exts_sideOverlapOtherTypes */
    tb = TiGetType(tp);
    if (tb == TT_SPACE) return (0);

    if (bp->b_segment.r_xtop == bp->b_segment.r_xbot)
    {
	length = MIN(bp->b_segment.r_ytop, TOP(tp))
	       - MAX(bp->b_segment.r_ybot, BOTTOM(tp));
    }
    else
    {
	length = MIN(bp->b_segment.r_xtop, RIGHT(tp))
	       - MAX(bp->b_segment.r_xbot, LEFT(tp));
    }

    TITORECT(tp, &ov.o_clip);
    GEOCLIP(&ov.o_clip, esws->area);
    areaTotal = GEO_WIDTH(&ov.o_clip) * GEO_HEIGHT(&ov.o_clip);
    areaAccountedFor = 0;
 
    ta = TiGetType(bp->b_inside);

    /* Revert any contacts to their residues */
    if (DBIsContact(ta))
	ta = DBPlaneToResidue(ta, esws->plane_of_boundary);
    if (DBIsContact(tb))
	tb = DBPlaneToResidue(tb, esws->plane_checked);

    /* Apply each rule, incorporating shielding into the edge length. */
    cap = (CapValue) 0;
    for (e = esws->extOverlapList; e; e = e->ec_next)
    {
	/* Only apply rules for the plane in which they are declared */
	if (!PlaneMaskHasPlane(e->ec_pmask, esws->plane_checked)) continue;

	/* Does this rule "e" include the tile we found? */
	if (TTMaskHasType(&e->ec_near, TiGetType(tp)))
	{
	    /* We have a possible capacitor, but are the tiles shielded from
	     * each other part of the way?
	     */
	    int pNum;
	    ov.o_area = areaTotal;
	    ov.o_pmask = ExtCurStyle->exts_sideOverlapShieldPlanes[ta][tb];
	    if (ov.o_pmask)
	    {
		ov.o_tmask = e->ec_far;  /* Actually shieldtypes. */
		for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		{
		    /* Each call to DBSrPaintArea has an opportunity to
		     * subtract from the area (really length 'cause width=1).
		     */
		    if (!PlaneMaskHasPlane(ov.o_pmask, pNum)) continue;
		    ov.o_pmask &= ~(PlaneNumToMaskBit(pNum));
		    if (ov.o_pmask == 0)
		    {
			(void) DBSrPaintArea((Tile *) NULL,
			    extOverlapDef->cd_planes[pNum], &ov.o_clip,
			    &ov.o_tmask, extSubtractOverlap, (ClientData) &ov);
		    }
		    else
		    {
			(void) DBSrPaintArea((Tile *) NULL,
			    extOverlapDef->cd_planes[pNum], &ov.o_clip,
			    &DBAllTypeBits,
			    extSubtractOverlap2, (ClientData) &ov);
		    }
		    break;
		}
	    }
	    if (rtp != rbp)
		cap += e->ec_cap * ov.o_area;

	    areaAccountedFor += ov.o_area;
	}
    }

    /* Add in the new capacitance. */

    if (tb != TT_SPACE)
    {
	int oa = ExtCurStyle->exts_planeOrder[esws->plane_of_boundary];
	int ob = ExtCurStyle->exts_planeOrder[esws->plane_checked];
	if (oa > ob)
	{
	    /* If the overlapped tile is between the substrate and the boundary
	     * tile, then we subtract the fringe substrate capacitance
	     * from rbp's region due to the area of the sideoverlap, since
	     * we now know it is shielded from the substrate.
	     */
	    CapValue subcap;
	    TileType outtype = TiGetType(bp->b_outside);

	    /* Decompose contacts into their residues */
	    if (DBIsContact(ta))
		ta = DBPlaneToResidue(ta, esws->plane_of_boundary);
	    if (DBIsContact(outtype))
		outtype = DBPlaneToResidue(outtype, esws->plane_of_boundary);

	    afrac = (double)areaAccountedFor / (double)areaTotal;
	    subcap = (ExtCurStyle->exts_perimCap[ta][outtype] *
			MIN(areaAccountedFor, length));
	    rbp->nreg_cap -= subcap;
	    /* Ignore residual error at ~zero zeptoFarads.  Probably	*/
	    /* there should be better handling of round-off here.	*/
	    if ((rbp->nreg_cap > -0.001) && (rbp->nreg_cap < 0.001))
		rbp->nreg_cap = 0;
	    if (CAP_DEBUG)
	    	extNregAdjustCap(rbp, -subcap, "obsolete_perimcap");
    	}
	else if (CAP_DEBUG)
	    extNregAdjustCap(rbp, 0.0, "obsolete_perimcap (skipped, wrong direction)");

	/* If the nodes are electrically connected, then we don't add	*/
	/* any side overlap capacitance to the node.			*/
	if (rtp == rbp) return 0;
    	if (rtp == (NodeRegion *)CLIENTDEFAULT) return 0;
    	if (rbp == (NodeRegion *)CLIENTDEFAULT) return 0;

	if (rtp < rbp)
	{
	    ck.ck_1 = rtp;
	    ck.ck_2 = rbp;
	}
	else
	{
	    ck.ck_1 = rbp;
	    ck.ck_2 = rtp;
	}
	he = HashFind(extCoupleHashPtr, (char *) &ck);
	if (CAP_DEBUG) extAdjustCouple(he, cap, "sideoverlap");
	extSetCapValue(he, cap + extGetCapValue(he));
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extWalkTop ---
 *
 * Search in the area 'area' above the boundary 'bp' for coupling tiles of
 * types in 'mask', starting from the right corner above the boundary and
 * sweeping left.  If a coupling tile is found, process it, clipping the
 * boundary to the width of the tile if needed.  Then recursively call
 * extWalkTop on the areas to the left and right sides of the tile with
 * boundaries reduced to the width of those areas.  This way the edge
 * boundary is subdivided into lengths occupied by the nearest neighbor.
 *
 * Return value:
 *	Return 1 if func() returned 1, otherwise return 0.
 *
 * Side effects:
 *	None.
 * 
 * ----------------------------------------------------------------------------
 */

int
extWalkTop(area, mask, func, bp, esws)
    Rect *area;
    TileTypeBitMask *mask;
    int (*func)();
    Boundary *bp;
    extSidewallStruct *esws;
{
    Tile *tile, *tp;
    TileType ttype;
    Boundary bloc;
    Rect aloc;

    tile = RT(bp->b_outside);	/* Tile above tile on top of the boundary */
    while (BOTTOM(tile) < area->r_ytop)
    {
	while (LEFT(tile) >= area->r_xtop) tile = BL(tile);  /* Walk back to area */
	tp = tile;
	while (RIGHT(tp) > area->r_xbot)
	{
	    if (IsSplit(tp))
		ttype = (SplitSide(tp)) ? SplitRightType(tp) : SplitLeftType(tp);
	    else
		ttype = TiGetTypeExact(tp);

	    if (TTMaskHasType(mask, ttype))
	    {
		bool lookLeft, lookRight;

		bloc = *bp;	/* Copy boundary to bc, then adjust boundary */

		lookLeft = (LEFT(tp) > bp->b_segment.r_xbot) ? TRUE : FALSE;
		lookRight = (RIGHT(tp) < bp->b_segment.r_xtop) ? TRUE : FALSE;

		if (lookLeft)
		    bloc.b_segment.r_xbot = LEFT(tp);
		if (lookRight)
		    bloc.b_segment.r_xtop = RIGHT(tp);

		/* Call sidewall coupling calculation function */
		if (func(tp, &bloc, esws) != 0) return 1;

		/* Clip coupling area and call fringe coupling calculation function */
		aloc = *area;
		aloc.r_ytop = BOTTOM(tp);
		aloc.r_xbot = bloc.b_segment.r_xbot;
		aloc.r_xtop = bloc.b_segment.r_xtop;
		if (extFindOverlap(bp->b_outside, &aloc, esws) != 0) return 1;
		extRemoveSubcap(&bloc, &aloc, esws);

		/* Recurse on tile left side */
		if (lookLeft)
		{
		    aloc = *area;
		    aloc.r_xtop = bloc.b_segment.r_xbot;
		    bloc.b_segment.r_xbot = bp->b_segment.r_xbot;
		    bloc.b_segment.r_xtop = aloc.r_xtop;
		    if (extWalkTop(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Recurse on tile right side */
		if (lookRight)
		{
		    aloc = *area;
		    aloc.r_xbot = RIGHT(tp);
		    bloc.b_segment.r_xtop = bp->b_segment.r_xtop;
		    bloc.b_segment.r_xbot = aloc.r_xbot;
		    if (extWalkTop(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Once a coupling tile is found, it blocks any	*/
		/* coupling to tiles behind it, so return.	*/
		return 0;
	    }

	    /* Continue to walk left until out of bounds */
	    tp = BL(tp);
	}
	/* Continue to walk up from right edge */
	tile = RT(tile);
    }

    /* Any length which does not couple to anything in the	*/
    /* same plane is still checked for coupling to anything	*/
    /* below it.						*/
    return extFindOverlap(bp->b_outside, area, esws);
}
    
/*
 * ----------------------------------------------------------------------------
 *
 * extWalkBottom ---
 *
 * Search in the area 'area' below the boundary 'bp' for coupling tiles of
 * types in 'mask', starting from the left corner below the boundary and
 * sweeping right.  If a coupling tile is found, process it, clipping the
 * boundary to the width of the tile if needed.  Then recursively call
 * extWalkBottom on the areas to the left and right sides of the tile with
 * boundaries reduced to the width of those areas.  This way the edge
 * boundary is subdivided into lengths occupied by the nearest neighbor.
 *
 * Return value:
 *	Return 1 if func() returned 1, otherwise return 0.
 *
 * Side effects:
 *	None.
 * 
 * ----------------------------------------------------------------------------
 */

int
extWalkBottom(area, mask, func, bp, esws)
    Rect *area;
    TileTypeBitMask *mask;
    int (*func)();
    Boundary *bp;
    extSidewallStruct *esws;
{
    Tile *tile, *tp;
    TileType ttype;
    Boundary bloc;
    Rect aloc;

    tile = LB(bp->b_outside);	/* Tile below tile on bottom of the boundary */
    while (TOP(tile) > area->r_ybot)
    {
	while (RIGHT(tile) <= area->r_xbot) tile = TR(tile);  /* Walk back to area */
	tp = tile;
	while (LEFT(tp) < area->r_xtop)
	{
	    if (IsSplit(tp))
		ttype = (SplitSide(tp)) ? SplitRightType(tp) : SplitLeftType(tp);
	    else
		ttype = TiGetTypeExact(tp);

	    if (TTMaskHasType(mask, ttype))
	    {
		bool lookLeft, lookRight;

		bloc = *bp;	/* Copy boundary to bc, then adjust boundary */

		lookLeft = (LEFT(tp) > bp->b_segment.r_xbot) ? TRUE : FALSE;
		lookRight = (RIGHT(tp) < bp->b_segment.r_xtop) ? TRUE : FALSE;

		if (lookLeft)
		    bloc.b_segment.r_xbot = LEFT(tp);
		if (lookRight)
		    bloc.b_segment.r_xtop = RIGHT(tp);

		/* Call sidewall coupling calculation function */
		if (func(tp, &bloc, esws) != 0) return 1;

		/* Clip coupling area and call fringe coupling calculation function */
		aloc = *area;
		aloc.r_ybot = TOP(tp);
		aloc.r_xbot = bloc.b_segment.r_xbot;
		aloc.r_xtop = bloc.b_segment.r_xtop;
		if (extFindOverlap(bp->b_outside, &aloc, esws) != 0) return 1;
		extRemoveSubcap(&bloc, &aloc, esws);

		/* Recurse on tile left side */
		if (lookLeft)
		{
		    aloc = *area;
		    aloc.r_xtop = bloc.b_segment.r_xbot;
		    bloc.b_segment.r_xbot = bp->b_segment.r_xbot;
		    bloc.b_segment.r_xtop = aloc.r_xtop;
		    if (extWalkBottom(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Recurse on tile right side */
		if (lookRight)
		{
		    aloc = *area;
		    aloc.r_xbot = RIGHT(tp);
		    bloc.b_segment.r_xtop = bp->b_segment.r_xtop;
		    bloc.b_segment.r_xbot = aloc.r_xbot;
		    if (extWalkBottom(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Once a coupling tile is found, it blocks any	*/
		/* coupling to tiles behind it, so return.	*/
		return 0;
	    }

	    /* Continue to walk right until out of bounds */
	    tp = TR(tp);
	}
	/* Continue to walk down from left edge */
	tile = LB(tile);
    }

    /* Any length which does not couple to anything in the	*/
    /* same plane is still checked for coupling to anything	*/
    /* below it.						*/
    return extFindOverlap(bp->b_outside, area, esws);
}
    
/*
 * ----------------------------------------------------------------------------
 *
 * extWalkRight ---
 *
 * Search in the area 'area' to the right of the boundary 'bp' for coupling
 * tiles of types in 'mask', starting from the top corner to the right of the
 * boundary and sweeping downward.  If a coupling tile is found, process it,
 * clipping the boundary to the height of the tile if needed.  Then recursively
 * call extWalkRight on the areas above and below the tile with boundaries
 * reduced to the height of those areas.  This way the edge boundary is
 * subdivided into lengths occupied by the nearest neighbor.
 *
 * Return value:
 *	Return 1 if func() returned 1, otherwise return 0.
 *
 * Side effects:
 *	None.
 * 
 * ----------------------------------------------------------------------------
 */

int
extWalkRight(area, mask, func, bp, esws)
    Rect *area;
    TileTypeBitMask *mask;
    int (*func)();
    Boundary *bp;
    extSidewallStruct *esws;
{
    Tile *tile, *tp;
    TileType ttype;
    Boundary bloc;
    Rect aloc;

    tile = TR(bp->b_outside);	/* Tile to the right of tile to right of the boundary */
    while (LEFT(tile) < area->r_xtop)
    {
	while (BOTTOM(tile) >= area->r_ytop) tile = LB(tile);  /* Walk back to area */
	tp = tile;
	while (TOP(tp) > area->r_ybot)
	{
	    if (IsSplit(tp))
		ttype = (SplitSide(tp)) ? SplitRightType(tp) : SplitLeftType(tp);
	    else
		ttype = TiGetTypeExact(tp);

	    if (TTMaskHasType(mask, ttype))
	    {
		bool lookDown, lookUp;

		bloc = *bp;	/* Copy boundary to bc, then adjust boundary */

		lookDown = (BOTTOM(tp) > bp->b_segment.r_ybot) ? TRUE : FALSE;
		lookUp = (TOP(tp) < bp->b_segment.r_ytop) ? TRUE : FALSE;

		if (lookDown)
		    bloc.b_segment.r_ybot = BOTTOM(tp);
		if (lookUp)
		    bloc.b_segment.r_ytop = TOP(tp);

		/* Call sidewall coupling calculation function */
		if (func(tp, &bloc, esws) != 0) return 1;

		/* Clip coupling area and call fringe coupling calculation function */
		aloc = *area;
		aloc.r_xtop = LEFT(tp);
		aloc.r_ybot = bloc.b_segment.r_ybot;
		aloc.r_ytop = bloc.b_segment.r_ytop;
		if (extFindOverlap(bp->b_outside, &aloc, esws) != 0) return 1;
		extRemoveSubcap(&bloc, &aloc, esws);

		/* Recurse on tile bottom side */
		if (lookDown)
		{
		    aloc = *area;
		    aloc.r_ytop = bloc.b_segment.r_ybot;
		    bloc.b_segment.r_ybot = bp->b_segment.r_ybot;
		    bloc.b_segment.r_ytop = aloc.r_ytop;
		    if (extWalkRight(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Recurse on tile top side */
		if (lookUp)
		{
		    aloc = *area;
		    aloc.r_ybot = TOP(tp);
		    bloc.b_segment.r_ytop = bp->b_segment.r_ytop;
		    bloc.b_segment.r_ybot = aloc.r_ybot;
		    if (extWalkRight(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Once a coupling tile is found, it blocks any	*/
		/* coupling to tiles behind it, so return.	*/
		return 0;
	    }

	    /* Continue to walk down until out of bounds */
	    tp = LB(tp);
	}
	/* Continue to walk right from top edge */
	tile = TR(tile);
    }

    /* Any length which does not couple to anything in the	*/
    /* same plane is still checked for coupling to anything	*/
    /* below it.						*/
    return extFindOverlap(bp->b_outside, area, esws);
}
    
/*
 * ----------------------------------------------------------------------------
 *
 * extWalkLeft ---
 *
 * Search in the area 'area' to the left of the boundary 'bp' for coupling
 * tiles of types in 'mask', starting from the bottom corner to the left of the
 * boundary and sweeping upward.  If a coupling tile is found, process it,
 * clipping the boundary to the height of the tile if needed.  Then recursively
 * call extWalkLeft on the areas above and below the tile with boundaries
 * reduced to the height of those areas.  This way the edge boundary is
 * subdivided into lengths occupied by the nearest neighbor.
 *
 * Return value:
 *	Return 1 if func() returned 1, otherwise return 0.
 *
 * Side effects:
 *	None.
 * 
 * ----------------------------------------------------------------------------
 */

int
extWalkLeft(area, mask, func, bp, esws)
    Rect *area;
    TileTypeBitMask *mask;
    int (*func)();
    Boundary *bp;
    extSidewallStruct *esws;
{
    Tile *tile, *tp;
    TileType ttype;
    Boundary bloc;
    Rect aloc;

    tile = BL(bp->b_outside);	/* Tile to the left of tile to left of the boundary */
    while (RIGHT(tile) > area->r_xbot)
    {
	while (TOP(tile) <= area->r_ybot) tile = RT(tile);  /* Walk back to area */
	tp = tile;
	while (BOTTOM(tp) < area->r_ytop)
	{
	    if (IsSplit(tp))
		ttype = (SplitSide(tp)) ? SplitRightType(tp) : SplitLeftType(tp);
	    else
		ttype = TiGetTypeExact(tp);

	    if (TTMaskHasType(mask, ttype))
	    {
		bool lookDown, lookUp;

		bloc = *bp;	/* Copy boundary to bc, then adjust boundary */

		lookDown = (BOTTOM(tp) > bp->b_segment.r_ybot) ? TRUE : FALSE;
		lookUp = (TOP(tp) < bp->b_segment.r_ytop) ? TRUE : FALSE;

		if (lookDown)
		    bloc.b_segment.r_ybot = BOTTOM(tp);
		if (lookUp)
		    bloc.b_segment.r_ytop = TOP(tp);

		/* Call sidewall coupling calculation function */
		if (func(tp, &bloc, esws) != 0) return 1;

		/* Clip coupling area and call fringe coupling calculation function */
		aloc = *area;
		aloc.r_xbot = RIGHT(tp);
		aloc.r_ybot = bloc.b_segment.r_ybot;
		aloc.r_ytop = bloc.b_segment.r_ytop;
		if (extFindOverlap(bp->b_outside, &aloc, esws) != 0) return 1;
		extRemoveSubcap(&bloc, &aloc, esws);

		/* Recurse on tile bottom side */
		if (lookDown)
		{
		    aloc = *area;
		    aloc.r_ytop = bloc.b_segment.r_ybot;
		    bloc.b_segment.r_ybot = bp->b_segment.r_ybot;
		    bloc.b_segment.r_ytop = aloc.r_ytop;
		    if (extWalkLeft(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Recurse on tile top side */
		if (lookUp)
		{
		    aloc = *area;
		    aloc.r_ybot = TOP(tp);
		    bloc.b_segment.r_ytop = bp->b_segment.r_ytop;
		    bloc.b_segment.r_ybot = aloc.r_ybot;
		    if (extWalkLeft(&aloc, mask, func, &bloc, esws) != 0)
			return 1;
		}

		/* Once a coupling tile is found, it blocks any	*/
		/* coupling to tiles behind it, so return.	*/
		return 0;
	    }

	    /* Continue to walk up until out of bounds */
	    tp = RT(tp);
	}
	/* Continue to walk left from top edge */
	tile = BL(tile);
    }

    /* Any length which does not couple to anything in the	*/
    /* same plane is still checked for coupling to anything	*/
    /* below it.						*/
    return extFindOverlap(bp->b_outside, area, esws);
}
    
/*
 * ----------------------------------------------------------------------------
 *
 * extSideLeft --
 *
 * Searching to the left of the boundary 'bp', we found the tile
 * 'tpfar' which may lie on the far side of an edge to which the
 * edge bp->b_inside | bp->b_outside has sidewall coupling capacitance.
 *
 * Walk along the right-hand side of 'tpfar' searching for such
 * edges, and recording their capacitance in the hash table
 * *extCoupleHashPtr.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	If node(tpfar) exists, and node(bp->b_inside) != node(tpfar),
 *	search along the inside edge of tpfar (the one closest to
 *	the boundary bp) for edges having capacitance with bp.  For
 *	each such edge found, update the entry in *extCoupleHashPtr
 *	identified by node(bp->b_inside) and node(tpfar) by adding
 *	the capacitance due to the adjacency of the pair of edges.
 *
 * ----------------------------------------------------------------------------
 */

int
extSideLeft(tpfar, bp, esws)
    Tile *tpfar;
    Boundary *bp;
    extSidewallStruct *esws;
{
    NodeRegion *rinside = (NodeRegion *) extGetRegion(bp->b_inside);
    NodeRegion *rfar = (NodeRegion *) extGetRegion(tpfar);
    Tile *tpnear;

    if (rfar != (NodeRegion *) extUnInit && rfar != rinside)
    {
	int sep = bp->b_segment.r_xbot - RIGHT(tpfar);
	int limit = MAX(bp->b_segment.r_ybot, BOTTOM(tpfar));
	int start = MIN(bp->b_segment.r_ytop, TOP(tpfar));

	for (tpnear = TR(tpfar); TOP(tpnear) > limit; tpnear = LB(tpnear))
	{
	    int overlap = MIN(TOP(tpnear), start) - MAX(BOTTOM(tpnear), limit);
	    if (overlap > 0)
		extSideCommon(rinside, rfar, tpnear, tpfar, overlap, sep,
				esws->extCoupleList);
	}
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSideRight --
 *
 * Searching to the right of the boundary 'bp', we found the tile
 * 'tpfar' which may lie on the far side of an edge to which the
 * edge bp->b_inside | bp->b_outside has sidewall coupling capacitance.
 *
 * Walk along the left-hand side of 'tpfar' searching for such
 * edges, and recording their capacitance in the hash table
 * *extCoupleHashPtr.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See extSideLeft.
 *
 * ----------------------------------------------------------------------------
 */

int
extSideRight(tpfar, bp, esws)
    Tile *tpfar;
    Boundary *bp;
    extSidewallStruct *esws;
{
    NodeRegion *rinside = (NodeRegion *) extGetRegion(bp->b_inside);
    NodeRegion *rfar = (NodeRegion *) extGetRegion(tpfar);
    Tile *tpnear;

    if (rfar != (NodeRegion *) extUnInit && rfar != rinside)
    {
	int sep = LEFT(tpfar) - bp->b_segment.r_xtop;
	int limit = MIN(bp->b_segment.r_ytop, TOP(tpfar));
	int start = MAX(bp->b_segment.r_ybot, BOTTOM(tpfar));

	for (tpnear = BL(tpfar); BOTTOM(tpnear) < limit; tpnear = RT(tpnear))
	{
	    int overlap = MIN(TOP(tpnear), limit) - MAX(BOTTOM(tpnear), start);
	    if (overlap > 0)
		extSideCommon(rinside, rfar, tpnear, tpfar, overlap, sep,
				esws->extCoupleList);
	}
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSideTop --
 *
 * Searching to the top of the boundary 'bp', we found the tile
 * 'tpfar' which may lie on the far side of an edge to which the
 * edge bp->b_inside | bp->b_outside has sidewall coupling capacitance.
 *
 * Walk along the bottom side of 'tpfar' searching for such
 * edges, and recording their capacitance in the hash table
 * *extCoupleHashPtr.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See extSideLeft.
 *
 * ----------------------------------------------------------------------------
 */

int
extSideTop(tpfar, bp, esws)
    Tile *tpfar;
    Boundary *bp;
    extSidewallStruct *esws;
{
    NodeRegion *rinside = (NodeRegion *) extGetRegion(bp->b_inside);
    NodeRegion *rfar = (NodeRegion *) extGetRegion(tpfar);
    Tile *tpnear;

    if (rfar != (NodeRegion *) extUnInit && rfar != rinside)
    {
	int sep = BOTTOM(tpfar) - bp->b_segment.r_ytop;
	int limit = MIN(bp->b_segment.r_xtop, RIGHT(tpfar));
	int start = MAX(bp->b_segment.r_xbot, LEFT(tpfar));

	for (tpnear = LB(tpfar); LEFT(tpnear) < limit; tpnear = TR(tpnear))
	{
	    int overlap = MIN(RIGHT(tpnear), limit) - MAX(LEFT(tpnear), start);
	    if (overlap > 0)
		extSideCommon(rinside, rfar, tpnear, tpfar, overlap, sep,
				esws->extCoupleList);
	}
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSideBottom --
 *
 * Searching to the bottom of the boundary 'bp', we found the tile
 * 'tpfar' which may lie on the far side of an edge to which the
 * edge bp->b_inside | bp->b_outside has sidewall coupling capacitance.
 *
 * Walk along the top side of 'tpfar' searching for such
 * edges, and recording their capacitance in the hash table
 * *extCoupleHashPtr.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See extSideLeft.
 *
 * ----------------------------------------------------------------------------
 */

int
extSideBottom(tpfar, bp, esws)
    Tile *tpfar;
    Boundary *bp;
    extSidewallStruct *esws;
{
    NodeRegion *rinside = (NodeRegion *) extGetRegion(bp->b_inside);
    NodeRegion *rfar = (NodeRegion *) extGetRegion(tpfar);
    Tile *tpnear;

    if (rfar != (NodeRegion *) extUnInit && rfar != rinside)
    {
	int sep = bp->b_segment.r_ybot - TOP(tpfar);
	int limit = MAX(bp->b_segment.r_xbot, LEFT(tpfar));
	int start = MIN(bp->b_segment.r_xtop, RIGHT(tpfar));

	for (tpnear = RT(tpfar); RIGHT(tpnear) > limit; tpnear = BL(tpnear))
	{
	    int overlap = MIN(RIGHT(tpnear), start) - MAX(LEFT(tpnear), limit);
	    if (overlap > 0)
		extSideCommon(rinside, rfar, tpnear, tpfar, overlap, sep,
				esws->extCoupleList);
	}
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extSideCommon --
 *
 * Perform the actual update to the hash table entry for
 * the regions 'rinside' and 'rfar'.  We assume that neither
 * 'rinside' nor 'rfar' are extUnInit, and further that they
 * are not equal.
 *
 * Walk along the rules in extCoupleList, applying the appropriate
 * amount of capacitance for an edge with tpnear on the close side
 * and tpfar on the remote side.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See extSideLeft.
 *
 * ----------------------------------------------------------------------------
 */

void
extSideCommon(rinside, rfar, tpnear, tpfar, overlap, sep, extCoupleList)
    NodeRegion *rinside, *rfar;	/* Both must be valid */
    Tile *tpnear, *tpfar;	/* Tiles on near and far side of edge */
    int overlap, sep;		/* Overlap of this edge with original one,
				 * and distance between the two.
				 */
    EdgeCap  *extCoupleList;	/* List of sidewall capacitance rules */
{
    TileType near = TiGetType(tpnear), far = TiGetType(tpfar);
    HashEntry *he;
    EdgeCap *e;
    CoupleKey ck;
    CapValue cap;

    if (rinside < rfar) ck.ck_1 = rinside, ck.ck_2 = rfar;
    else ck.ck_1 = rfar, ck.ck_2 = rinside;
    he = HashFind(extCoupleHashPtr, (char *) &ck);

    cap = extGetCapValue(he);
    for (e = extCoupleList; e; e = e->ec_next)
	if (TTMaskHasType(&e->ec_near, near) && TTMaskHasType(&e->ec_far, far)) {
	    cap += (e->ec_cap * overlap) / (sep + e->ec_offset);
	    if (CAP_DEBUG)
		extAdjustCouple(he,
			(e->ec_cap * overlap) / (sep + e->ec_offset),
			"sidewall");
	}
    extSetCapValue(he, cap);
}

