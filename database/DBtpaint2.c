/*
 * DBtechpaint2.c --
 *
 * Default composition rules.
 * Pretty complicated, unfortunately, so it's in a separate file.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtpaint2.c,v 1.4 2009/12/30 13:42:33 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/tech.h"
#include "textio/textio.h"

#define	SETPAINT(have,paint,plane,get) \
	if (IsDefaultPaint((have), (paint)) \
		&& TTMaskHasType(&DBPlaneTypes[(plane)], (have))) \
	    dbSetPaintEntry((have), (paint), (plane), (get))
#define	SETERASE(have,erase,plane,get) \
	if (IsDefaultErase((have), (erase)) \
		&& TTMaskHasType(&DBPlaneTypes[(plane)], (have))) \
	    dbSetEraseEntry((have), (erase), (plane), (get))

LayerInfo *dbTechLpPaint;

/* Forward declarations */

extern void dbTechPaintErasePlanes();
extern void dbComposePaintAllImages();
extern void dbComposeResidues();
extern void dbComposeContacts();
extern void dbComposePaintContact();
extern void dbComposeEraseContact();
extern void dbComposeSavedRules();
extern void dbComposeCompose();
extern void dbComposeDecompose();


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechFinalCompose --
 *
 * Process all the contact erase/compose rules saved by DBTechAddCompose
 * when it was reading in the "compose" section of a technology file.
 * Also sets up the default paint/erase rules for contacts.
 *
 * Since by the end of this section we've processed all the painting
 * rules, we initialize the tables that say which planes get affected
 * by painting/erasing a given type.
 *
 * There's a great deal of work done here, so it's broken up into a
 * number of separate procedures, each of which implements a single
 * operation or default rule.  Most of the work deals with painting
 * and erasing contacts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the paint/erase tables.
 *	Initializes DBTypePaintPlanesTbl[] and DBTypeErasePlanesTbl[].
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechFinalCompose()
{
    TileType i;
    TileTypeBitMask testmask, *rMask;

    /* Default rules for painting/erasing contacts */
    dbComposePaintAllImages();
    dbComposeResidues();
    dbComposeContacts();

    /* Process rules saved from reading the "compose" section */
    dbComposeSavedRules();

    /* Build up exported tables */
    dbTechPaintErasePlanes();

    /* Adjust paint tables for any locked layers */
    for (i = TT_TECHDEPBASE; i < DBNumUserLayers; i++)
	if (!TTMaskHasType(&DBActiveLayerBits, i))
	    if (DBIsContact(i))
		DBLockContact(i);
    for (i = DBNumUserLayers; i < DBNumTypes; i++)
    {
	rMask = DBResidueMask(i);
	TTMaskAndMask3(&testmask, &DBActiveLayerBits, rMask);
	if (!TTMaskEqual(&testmask, rMask))
	{
	    TTMaskClearType(&DBActiveLayerBits, i);
	    DBLockContact(i);
	}
    }

    /* Diagnostic */
    /* dbTechPrintPaint("DBTechFinalCompose", TRUE, FALSE); */
    /* dbTechPrintPaint("DBTechFinalCompose", FALSE, FALSE); */
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTechPaintErasePlanes --
 *
 * Fill in the tables telling which planes get affected
 * by painting and erasing.  One may take the naive view that only
 * the planes on which a type is defined can be affected, but then
 * one can't define arbitrary composite types.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in DBTypePaintPlanesTbl[] and DBTypeErasePlanesTbl[].
 *
 * ----------------------------------------------------------------------------
 */

void
dbTechPaintErasePlanes()
{
    TileType t, s;
    int pNum;

    /* Space tiles are special: they may appear on any plane */
    DBTypePaintPlanesTbl[TT_SPACE] = ~(PlaneNumToMaskBit(PL_CELL));
    DBTypeErasePlanesTbl[TT_SPACE] = ~(PlaneNumToMaskBit(PL_CELL));

    /* Skip TT_SPACE */
    for (t = 1; t < DBNumTypes; t++)
    {
	DBTypePaintPlanesTbl[t] = DBTypeErasePlanesTbl[t] = 0;

	for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	    for (s = 0; s < DBNumTypes; s++)
	    {
		if (DBStdPaintEntry(s, t, pNum) != s)
		    DBTypePaintPlanesTbl[t] |= PlaneNumToMaskBit(pNum);
		if (DBStdEraseEntry(s, t, pNum) != s)
		    DBTypeErasePlanesTbl[t] |= PlaneNumToMaskBit(pNum);
	    }
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbComposePaintAllImages --
 *
 * Painting the primary type of a contact layer paints its image
 * over all types on each image's plane.  (The only types that
 * should ever be painted are primary types.)
 *
 * This rule is called first because it may be overridden by later
 * rules, or by explicit composition rules.
 *
 * Only affects paint entries that haven't already been set to other
 * values by explicit paint rules.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies paint/erase tables.
 *
 * ----------------------------------------------------------------------------
 */

void
dbComposePaintAllImages()
{
    TileType tPaint, s, res;
    LayerInfo *lp;
    int p, n;

    /* Iterate over primary types only */
    for (n = 0; n < dbNumContacts; n++)
    {
        lp = dbContactInfo[n];
        tPaint = lp->l_type;
	if (tPaint >= DBNumUserLayers) continue;
	for (res = TT_TECHDEPBASE; res < DBNumTypes; res++)
	{
	    if (TTMaskHasType(&lp->l_residues, res))
	    {
		p = DBPlane(res);
		for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
		    if (DBPlane(s) == p)
			SETPAINT(s, tPaint, p, tPaint);

		if (IsDefaultPaint(TT_SPACE, tPaint))
		    dbSetPaintEntry(TT_SPACE, tPaint, p, tPaint);
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbComposeResidues --
 *
 * The behavior of a contact type when other, non-contact types are
 * painted over it or erased from it is derived from the behavior of
 * its residue types.
 *
 *  1.	If painting doesn't affect a contact's residue on a plane,
 *	it doesn't affect the contact's image on that plane either.
 *	This allows, for example, painting metal1 over a contact
 *	"containing" metal1 without breaking the contact.
 *
 *  2.	If painting or erasing a type affects a residue of a
 *	contact, the image's connectivity to adjacent planes
 *	is broken and the image is replaced by the result of
 *	painting or erasing over the residue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies paint/erase tables.
 *
 * ----------------------------------------------------------------------------
 */

void
dbComposeResidues()
{
    LayerInfo *lp;
    TileType s, res;
    int n;

    /* Painting that doesn't affect the residue doesn't affect the contact. */

    for (n = 0; n < dbNumContacts; n++)
    {
	lp = dbContactInfo[n];

	for (res = TT_TECHDEPBASE; res < DBNumUserLayers; res++)
	{
	    if (TTMaskHasType(&lp->l_residues, res))
	    {
		for (s = TT_TECHDEPBASE; s < DBNumUserLayers; s++)
		    if (!PAINTAFFECTS(res, s))
			SETPAINT(lp->l_type, s, DBPlane(res), lp->l_type);
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbComposeContacts --
 *
 * This procedure handles the rules for composition of contact types.
 * We look at the results of painting each type of contact in
 * dbContactInfo[] over all other contact types.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies paint/erase tables.
 *
 * ----------------------------------------------------------------------------
 */

void
dbComposeContacts()
{
    LayerInfo *lpImage, *lpPaint;
    int m, pNum;
    TileTypeBitMask *rmask;
    TileType n, ttype, itype, presult, eresult;

    for (m = 0; m < dbNumContacts; m++)
    {
        lpImage = dbContactInfo[m];	/* Existing contact image */
	for (n = TT_TECHDEPBASE; n < DBNumUserLayers; n++)
	{
	    lpPaint = &dbLayerInfo[n];	/* Layer being painted or erased */
	    if (lpImage->l_type != n)
		dbComposePaintContact(lpImage, lpPaint);
	    dbComposeEraseContact(lpImage, lpPaint);
	}
    }

    /* For stacking types, determine the result of painting or erasing	*/
    /* each stacking type by applying the paint and erase results of	*/
    /* each of its residues in sequence.				*/

    for (itype = 0; itype < DBNumTypes; itype++)
    {
	for (n = DBNumUserLayers; n < DBNumTypes; n++)
	{
	    lpPaint = &dbLayerInfo[n];	/* Layer being painted or erased */
	    rmask = &lpPaint->l_residues;
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    {
		presult = eresult = itype;
		for (ttype = TT_TECHDEPBASE; ttype < DBNumUserLayers; ttype++)
		    if (TTMaskHasType(rmask, ttype))
		    {
			presult = DBStdPaintEntry(presult, ttype, pNum);
			eresult = DBStdEraseEntry(eresult, ttype, pNum);
		    }
		SETPAINT(itype, n, pNum, presult);
		SETERASE(itype, n, pNum, eresult);
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 * dbComposePaintContact --
 *
 * Construct the painting rules for painting type lpPaint over
 * the contact image lpImage.
 * ----------------------------------------------------------------------------
 */

void
dbComposePaintContact(lpImage, lpPaint)
    LayerInfo *lpImage, *lpPaint;
{
    int pNum;
    PlaneMask pmask, pshared;
    LayerInfo *lp;
    TileTypeBitMask rmask, cmask;
    TileType newtype, ptype, itype;
    bool overlap;

    /*
     * If the residues of lpImage and lpPaint can be merged without
     * affecting any of the layers, then we merge them, and look for
     * any contact matching the merged residues.  If none is found,
     * and the planes do not overlap, then just paint the new
     * contact.  If they cannot be merged, then the original contact
     * is dissolved and replaced by its residues.
     */

    pmask = lpImage->l_pmask & lpPaint->l_pmask;
    overlap = (pmask == 0) ? FALSE : TRUE;

    if (overlap)
    {
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	{
	    if (PlaneMaskHasPlane(pmask, pNum))
	    {
		ptype = DBPlaneToResidue(lpPaint->l_type, pNum);
		itype = DBPlaneToResidue(lpImage->l_type, pNum);
		if (ptype != itype)
		    break;
	    }
	}
	if (pNum == DBNumPlanes)
	{
	    /* Residues are compatible; check for a contact type with	*/
	    /* the merged residues (explicitly defined stacked contact	*/
	    /* type)							*/

	    TTMaskZero(&rmask);
	    TTMaskSetMask3(&rmask, &lpImage->l_residues, &lpPaint->l_residues);
	    dbTechMatchResidues(&rmask, &cmask, TRUE);

	    /* Implicitly-defined stacking types override any	*/
	    /* explicitly-defined types, or havoc results.	*/
	    /* This allows one to create a type such as "pad"	*/
	    /* having the same residues as "m123c" without	*/
	    /* magic confusing them in the paint tables.	*/

	    newtype = DBTechFindStacking(lpImage->l_type, lpPaint->l_type);

	    if (TTMaskIsZero(&cmask) || (newtype != -1))
	    {
		/* If there is a stacking contact type, use it */

		if (newtype >= DBNumUserLayers)
		{
		    pshared = lpImage->l_pmask & lpPaint->l_pmask;
		    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
			if (PlaneMaskHasPlane(pshared, pNum))
			    SETPAINT(lpImage->l_type, lpPaint->l_type,
					pNum, newtype);
		}

		else if (lpPaint->l_isContact && (lpImage->l_type < DBNumUserLayers))
		{
		    /* Original contact is replaced by the new one where
		     * the planes overlap, and is dissolved into its
		     * residues where they don't.
		     * In this condition, the residues of image must be
		     * non-contact types.
		     */

		    for (itype = TT_TECHDEPBASE; itype < DBNumUserLayers; itype++)
		    {
			if (TTMaskHasType(&lpImage->l_residues, itype))
			{
		            if (TTMaskHasType(&lpPaint->l_residues, itype))
			    {
				SETPAINT(lpImage->l_type, lpPaint->l_type,
						DBPlane(itype), lpPaint->l_type);
			    }
			    else
			    {
				SETPAINT(lpImage->l_type, lpPaint->l_type,
						DBPlane(itype), itype);
			    }
			}
		    }
		}
		else if (lpPaint->l_isContact &&
			!TTMaskHasType(&lpImage->l_residues, lpPaint->l_type))
		{
		    /* Original contact is replaced by the new one where
		     * the planes overlap, and is dissolved into its
		     * residues where they don't.
		     * In this condition, the residues of image are contact
		     * types.
		     */

		    for (itype = TT_TECHDEPBASE; itype < DBNumUserLayers; itype++)
		    {
			if (TTMaskHasType(&lpImage->l_residues, itype))
			{
		            if (TTMaskHasType(&lpPaint->l_residues, itype))
			    {
				SETPAINT(lpImage->l_type, lpPaint->l_type,
						DBPlane(itype), lpPaint->l_type);
			    }
			}
		    }
		}
		else
		{
		    /* Painting a residue type on top of a contact	*/
		    /* with a compatible residue does nothing, as does	*/
		    /* painting one of the types of a stacked contact	*/
		    /* on the stacking contact type.  In the plane of	*/
		    /* the image type, this is non-default behavior.	*/

		    SETPAINT(lpImage->l_type, lpPaint->l_type,
				DBPlane(lpImage->l_type), lpImage->l_type);
		}
	    }
	    else
	    {
		/* Presumably there is at most one contact type here	*/
		for (newtype = TT_TECHDEPBASE; newtype < DBNumUserLayers; newtype++)
		{
		    if (TTMaskHasType(&cmask, newtype))
		    {
			lp = &dbLayerInfo[newtype];
			for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
			    if (PlaneMaskHasPlane(lp->l_pmask, pNum))
				SETPAINT(lpImage->l_type, lpPaint->l_type,
					pNum, newtype);
			break;
		    }
		}
	    }
	}
	else
	{
	    /* Image and paint types overlap in a plane, but	*/
	    /* residues are not compatible.  Replace image with the	*/
	    /* residues of image, except on the overlapping plane.	*/

	    for (ptype = TT_TECHDEPBASE; ptype < DBNumUserLayers; ptype++)
		if (TTMaskHasType(&lpImage->l_residues, ptype))
		    if (ptype != itype)
			SETPAINT(lpImage->l_type, lpPaint->l_type,
					DBPlane(ptype), ptype);
	}
    }
    else if (lpPaint->l_isContact)
    {
	/* No overlapping planes, and both paint & image types are contacts */

	TTMaskZero(&rmask);
	TTMaskSetMask3(&rmask, &lpImage->l_residues, &lpPaint->l_residues);
	dbTechMatchResidues(&rmask, &cmask, TRUE);
	if (!TTMaskIsZero(&cmask))
	{
	    /* Replace image with the new contact type */
	    for (newtype = TT_TECHDEPBASE; newtype < DBNumUserLayers; newtype++)
	    {
		if (TTMaskHasType(&cmask, newtype))
		{
		    lp = &dbLayerInfo[newtype];
		    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
			if (PlaneMaskHasPlane(lp->l_pmask, pNum))
			    SETPAINT(lpImage->l_type, lpPaint->l_type,
					pNum, newtype);
		}
		break;
	    }
	}
	/* else default paint/erase behavior */
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbComposeSubsetResidues --
 *
 *	Create a mask of all contact types whose residues are subsets
 *	of the "have" type but are not supersets of the "erase" type.
 *
 * Results:
 *	True if residues of types in outMask overlap, False if not.
 *
 * Side Effects:
 *	outMask is filled with the mask of types.
 *
 * ----------------------------------------------------------------------------
 */

bool 
dbComposeSubsetResidues(lpImage, lpErase, outMask)
    LayerInfo *lpImage, *lpErase;
    TileTypeBitMask *outMask;
{
    TileTypeBitMask ires;
    TileTypeBitMask smask, overlapmask;
    LayerInfo *li;
    int n;
    bool rval = FALSE;

    /* The residues of lpImage must themselves be decomposed if lpImage	*/
    /* is a stacking type.						*/

    TTMaskZero(&ires);
    if (lpImage->l_type >= DBNumUserLayers)
    {
	for (n = 0; n < dbNumContacts; n++)
	{
	    li = dbContactInfo[n];
	    if (TTMaskHasType(&lpImage->l_residues, li->l_type))
		TTMaskSetMask(&ires, &li->l_residues);
	}
    }
    else
	TTMaskSetMask(&ires, &lpImage->l_residues);

    /*
     * Generate a mask of all contact types whose residue masks are
     * subsets of the residues of lpImage.  These are all types
     * which contain no residues that are not part of lpImage.
     */

    TTMaskZero(outMask);
    TTMaskZero(&overlapmask);
    for (n = 0; n < dbNumContacts; n++)
    {
        li = dbContactInfo[n];
	TTMaskAndMask3(&smask, &li->l_residues, &ires);
	if (TTMaskEqual(&smask, &li->l_residues))
	{
	    /* The residues of type cannot be a superset of the */
	    /* residues of the erase type			*/

	    TTMaskAndMask3(&smask, &li->l_residues, &lpErase->l_residues);
	    if (!TTMaskEqual(&smask, &lpErase->l_residues))
	    {
		TTMaskSetType(outMask, li->l_type);

		/* Check if the residues of type overlap one of	*/
		/* the types already generated.			*/

		TTMaskAndMask3(&smask, &overlapmask, &li->l_residues);
		if (TTMaskIsZero(&smask))
		    TTMaskSetMask(&overlapmask, &li->l_residues);
		else
		    rval = TRUE;
	    }
	}
    }
    return rval;
}


/*
 * ----------------------------------------------------------------------------
 * dbComposeEraseContact --
 *
 * Construct the erasing rules for erasing type lpErase from
 * the contact image lpImage.
 * ----------------------------------------------------------------------------
 */

void
dbComposeEraseContact(lpImage, lpErase)
    LayerInfo *lpImage, *lpErase;
{
    int pNum;
    PlaneMask pmask;
    LayerInfo *lp;
    TileTypeBitMask cmask;
    TileType itype;
    bool overlap;

    /* The erased planes generally end up with space, so we generate	*/
    /* space as default behavior.  This may be altered in specific	*/
    /* cases, below.							*/

    /* The specific check for lpImage as a stacking contact is not	*/
    /* necessary, but prevents generation of non-default rules in	*/
    /* cases which cannot occur.					*/

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(lpErase->l_pmask, pNum))
	    if ((lpImage->l_type < DBNumUserLayers)
			|| (pNum == DBPlane(lpImage->l_type))) 
		SETERASE(lpImage->l_type, lpErase->l_type, pNum, TT_SPACE);

    /* Erasing self should always leave space; otherwise    */
    /* the "undo" records aren't symmetric, which screws    */
    /* everything up.					    */

    if (lpImage->l_type == lpErase->l_type) return;

    /* If planes of HAVE and ERASE types don't overlap, we can erase	*/
    /* the ERASE type without affecting the image, so we're done.	*/

    pmask = lpImage->l_pmask & lpErase->l_pmask;
    overlap = (pmask == 0) ? FALSE : TRUE;
    if (!overlap) return;

    /* Find what contacts are subsets of this one and generate a mask	*/
    /* of the types that might be left over after erasing this one.	*/
    /* If those types overlap, leave the existing type alone.	If they	*/
    /* don't, then paint them.						*/

    overlap = dbComposeSubsetResidues(lpImage, lpErase, &cmask);

    if (overlap)
    {
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(lpImage->l_pmask, pNum))
//		SETERASE(lpImage->l_type, lpErase->l_type, pNum, TT_SPACE);
		SETERASE(lpImage->l_type, lpErase->l_type, pNum, lpImage->l_type);
    }
    else
    {
	pmask = lpImage->l_pmask & (~(lpErase->l_pmask));
	for (itype = TT_TECHDEPBASE; itype < DBNumTypes; itype++)
	    if (TTMaskHasType(&cmask, itype))
	    {
		lp = &dbLayerInfo[itype];
		pmask &= ~(lp->l_pmask);
		for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		    if (PlaneMaskHasPlane(lp->l_pmask, pNum))
			if ((lpImage->l_type < DBNumUserLayers)
				|| (pNum == DBPlane(lpImage->l_type)))
			    SETERASE(lpImage->l_type, lpErase->l_type, pNum, itype);
	    }

	/* If there are any planes in the image which have not been	*/
	/* accounted for, then we decompose these into the residues	*/
	/* of the image.						*/

	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pmask, pNum))
	    {
		itype = DBPlaneToResidue(lpImage->l_type, pNum);
		SETERASE(lpImage->l_type, lpErase->l_type, pNum, itype);
	    }
	
    }

    /* Diagnostic (to be removed or commented out) */

/*
    if (!TTMaskIsZero(&cmask))
    {
	TxPrintf("Have %s, Erase %s: ", DBTypeLongNameTbl[lpImage->l_type],
		DBTypeLongNameTbl[lpErase->l_type]);
	for (itype = TT_TECHDEPBASE; itype < DBNumTypes; itype++)
	    if (TTMaskHasType(&cmask, itype))
		TxPrintf("%s ", DBTypeLongNameTbl[itype]);
	if (overlap)
	    TxPrintf("overlapping");
	TxPrintf("\n");
    }
*/

}

/*
 * ----------------------------------------------------------------------------
 * DBLockContact --
 *
 * This procedure modifies the erase tables so that the specified contact
 * type cannot be erased by erasing one of its residues, which is the default
 * behavior.
 * ----------------------------------------------------------------------------
 */

void
DBLockContact(ctype)
   TileType ctype;
{
    LayerInfo *lpImage, *lpPaint;
    TileType c, n, itype, eresult;
    TileTypeBitMask *rmask;
    int m, pNum;

    /* Have type, Erase * --> Result is type */

    lpPaint = &dbLayerInfo[ctype];
    for (n = TT_TECHDEPBASE; n < DBNumTypes; n++)
    {
	if (n == ctype) continue;

	/* Avoid the case, e.g., if ctype is pc+v, then pc+v - pc = v	*/
	/* is still valid if pc is an active layer.			*/

	if (ctype >= DBNumUserLayers)
	{
	    rmask = DBResidueMask(ctype);
	    if (TTMaskHasType(rmask, n))
		if (TTMaskHasType(&DBActiveLayerBits, n))
		    continue;
	}

	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(lpPaint->l_pmask, pNum))
		SETERASE(ctype, n, pNum, ctype);
    }
}

/*
 * ----------------------------------------------------------------------------
 * DBUnlockContact --
 *
 * This procedure reverses the operation of DBLockContact, allowing a contact
 * to be erased by erasing one of its residues.  This is the same code as
 * dbComposeContacts(), for a single contact type.
 * ----------------------------------------------------------------------------
 */

void
DBUnlockContact(ctype)
   TileType ctype;
{
    LayerInfo *lpImage, *lpPaint;
    TileType n, itype, eresult;
    TileTypeBitMask *rmask;
    int m, pNum;

    lpImage = &dbLayerInfo[ctype];

    for (n = TT_TECHDEPBASE; n < DBNumUserLayers; n++)
    {
	    lpPaint = &dbLayerInfo[n];
	    dbComposeEraseContact(lpImage, lpPaint);
    }

    /* To be done (maybe):  revert rules for stacked contact types */
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbComposeSavedRules --
 *
 * Process all the contact compose/decompose rules saved
 * when the compose section of the tech file was read.
 * Each pair on the RHS of one of these rules must contain
 * exactly one contact type that spans the same set of planes
 * as the image type.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies entries in the paint and erase tables.
 *
 * ----------------------------------------------------------------------------
 */

void
dbComposeSavedRules()
{
    LayerInfo *lpContact;
    TileType imageType;
    TypePair *pair;
    Rule *rule;
    int n;

    for (n = 0; n < dbNumSavedRules; n++)
    {
	rule = &dbSavedRules[n];
	lpContact = &dbLayerInfo[rule->r_result];
	imageType = lpContact->l_type;
   
	for (pair = rule->r_pairs; pair < &rule->r_pairs[rule->r_npairs];
		    	pair++)
    	{
	    dbComposeDecompose(imageType, pair->rp_a, pair->rp_b);
	    dbComposeDecompose(imageType, pair->rp_b, pair->rp_a);
	    if (rule->r_ruleType == RULE_COMPOSE)
	    {
	        dbComposeCompose(imageType, pair->rp_a, pair->rp_b);
	        dbComposeCompose(imageType, pair->rp_b, pair->rp_a);
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * dbComposeDecompose --
 *
 * Painting componentType over imageType is a no-op.
 * Erasing componentType from imageType gives either the image of
 * remainingType, if one exists on DBPlane(imageType), or else
 * gives the residue of imageType.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the paint/erase tables as described above.
 *	Indicates that these modifications are not default
 *	rules by setting the corresponding bits in the tables
 *	dbNotDefaultPaintTbl[] and dbNotDefaultEraseTbl[].
 * ----------------------------------------------------------------------------
 */

void
dbComposeDecompose(imageType, componentType, remainingType)
    TileType imageType;
    TileType componentType;
    TileType remainingType;
{
    int pNum = DBPlane(imageType);
    TileType resultType;

    /* Painting componentType is a no-op */
    dbSetPaintEntry(imageType, componentType, pNum, imageType);
    TTMaskSetType(&dbNotDefaultPaintTbl[imageType], componentType);

    /* Which residue belongs to the plane pNum? */
    resultType = DBPlaneToResidue(imageType, pNum);

    /*
     * Erasing componentType gives remainingType or breaks
     * imageType's contact.
     */
    dbSetEraseEntry(imageType, componentType, pNum, resultType);
    TTMaskSetType(&dbNotDefaultEraseTbl[imageType], componentType);
}

/*
 * ----------------------------------------------------------------------------
 * dbComposeCompose --
 *
 * Painting paintType over existingType gives imageType.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the paint/erase tables as described above.
 *	Indicates that these modifications are not default
 *	rules by setting the corresponding bits in the tables
 *	dbNotDefaultPaintTbl[] and dbNotDefaultEraseTbl[].
 * ----------------------------------------------------------------------------
 */

void
dbComposeCompose(imageType, existingType, paintType)
    TileType imageType;
    TileType existingType;
    TileType paintType;
{
    int pNum = DBPlane(imageType);

    if (PlaneMaskHasPlane(LayerPlaneMask(existingType), pNum))
    {
	dbSetPaintEntry(existingType, paintType, pNum, imageType);
	TTMaskSetType(&dbNotDefaultPaintTbl[existingType], paintType);
    }
}
