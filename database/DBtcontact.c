/*
 * DBtcontact.c --
 *
 * Management of contacts.
 * This file makes a distinction between the following two terms:
 *
 *  Layer	-- Logical type as specified in "types" section of .tech
 *		   file.  A layer may consist of many TileTypes, as is the
 *		   case when it is a contact.
 *  Type	-- TileType stored in a tile
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtcontact.c,v 1.3 2008/09/05 13:56:25 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/tech.h"
#include "textio/textio.h"

/* type-to-bitmask conversion (1 bit per entry) */
TileTypeBitMask DBLayerTypeMaskTbl[NT];

/* Filled in after contact types have been generated */
TileTypeBitMask DBPlaneTypes[PL_MAXTYPES];
TileTypeBitMask DBHomePlaneTypes[PL_MAXTYPES];
PlaneMask        DBTypePlaneMaskTbl[NT];

/* --------------------- Data local to this file ---------------------- */

/* Table of the properties of all layers */
LayerInfo dbLayerInfo[NT];

/* Array of pointers to the entries in above table for contacts only */
LayerInfo *dbContactInfo[NT];
int dbNumContacts;

/* Forward declaration */
void dbTechMatchResidues();
void dbTechAddStackedContacts();
int dbTechAddOneStackedContact();


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechInitContact --
 *
 * Mark all types as being non-contacts initially.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes dbLayerInfo.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechInitContact()
{
    TileType t;
    LayerInfo *lp;

    for (t = 0; t < TT_MAXTYPES; t++)
    {
	lp = &dbLayerInfo[t];
	lp->l_type = t;
	lp->l_isContact = FALSE;
	lp->l_pmask = 0;
	TTMaskZero(&lp->l_residues);
	TTMaskSetOnlyType(&DBLayerTypeMaskTbl[t], t);
    }

    dbNumContacts = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechAddContact --
 *
 * Add the definition of a new contact type.
 * The syntax of each line in the "contact" section is:
 *
 *	contactType res1 res2 [res3...]
 *
 * where res1, res2, res3, etc. are the residue types on the planes
 * connected by the contact.  The home plane of the contact (the
 * plane specified in the "types" section for the type contactType)
 * must be the lowest-numbered plane in the list of residues.  Each
 * listed residue must be on a different plane. There are no other
 * restrictions on contacts.
 *
 * Magic-7.3 additional syntax:
 *
 *	stackable type1 [type2 [alias12] [type3 [alias13] ...]]
 *
 * where type1 and type2 are contact types, allows type1 and type2
 * to be drawn on top of one other by generating an extra contact
 * type to represent the union of type1 and type2 on the shared
 * plane.  If more than two types are specified, then type1 will be
 * set to be stackable with each of the indicated types.  If only
 * type1 is specified, then type1 will be made stackable with all
 * other (existing) contact types.  Wherever a layer name exists
 * after a known type that is not a known layer name, it is
 * considered to be an alias name for the stacked type.  e.g.,
 * "stackable pc via pm12contact" is valid if "pc" and "via" are
 * defined types.  Thie provides compatibility with layout files
 * created with technology files that explicitly define stacked
 * contact types, especially those from magic-7.2 and earlier
 * versions.
 *
 *	stackable
 *
 * allows all contact types to stack, where applicable.  This
 * statement overrides any other "stackable" statement.
 *
 * Notes:
 *	All indicated types must have been declared in the "contact"
 *	section prior to use of the "stackable" keyword.  The 1- and
 *	2-argument variants will create stacking types with all
 *	previously-declared contact types.
 *
 * Expanded syntax:
 *	Now allows the section "image" with statements "contact" and
 *	"device".  Statements beginning with the layer name are
 *	backwardly-compatible with the original syntax.  "contact"
 *	statements are equivalent to the original syntax after skipping
 *	the keyword "contact".  "device" statements have the same
 *	syntax as the "contact" statement but describe a composite
 *	image such as a transistor or capacitor formed by layers on
 *	different planes.
 * 
 * Results:
 *	FALSE on error, TRUE if successful.
 *
 * Side effects:
 *	Adds the definition of a new contact type.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBTechAddContact(sectionName, argc, argv)
    char *sectionName;
    int argc;
    char *argv[];
{
    TileType contactType;
    int nresidues;

    if ((contactType = DBTechNameType(*argv)) < 0)
    {
	if (!strcmp(*argv, "contact") || !strcmp(*argv, "device"))
	{
	    argc--;
	    argv++;
	    if ((contactType = DBTechNameType(*argv)) < 0)
	    {
		DBTechNoisyNameType(*argv);
		return FALSE;
	    }
	}
	else if (!strcmp(*argv, "stackable"))
	{
	    TileType stackType, newType = -1;
	    LayerInfo *lim, *lin;

	    if (argc == 1)
		dbTechAddStackedContacts();
	    else
	    {
		contactType = DBTechNoisyNameType(*++argv);
		if (contactType < 0)
		    return FALSE;
		else if (argc == 2)
		{
		    int n, result;

		    lim = &dbLayerInfo[contactType];

		    for (n = 0; n < dbNumContacts; n++)
		    {
			lin = dbContactInfo[n];
			if (lim == lin) continue;
		 	result = dbTechAddOneStackedContact(lim->l_type, lin->l_type);
			if (result == -3)
			    return FALSE;	/* overran tiletype maximum number */
		    }
		}
		else
		{
		    TileType lastType = TT_SPACE;
		    char *primary;

		    while (--argc > 1)
		    {
			stackType = DBTechNameType(*++argv);
			if (stackType >= 0)
			{
			    newType = dbTechAddOneStackedContact(contactType,
					stackType);
			    if (newType == -1)
				TechError("Contact types %s and %s do not stack\n",
					DBTypeLongNameTbl[contactType],
					DBTypeLongNameTbl[stackType]);
			    lastType = stackType;
			}
			else if (lastType >= TT_SPACE)
			{
			    /* (*argv) becomes an alias name for layer newType */
			    if (newType < 0)
				TechError("Contact type %s unknown or contact "
					"missing in stackable statement\n", *argv);
			    else
				DBTechAddNameToType(*argv, newType, FALSE);
			    lastType = TT_SPACE;
			}
			else
			{
			    DBTechNoisyNameType(*argv);
			    lastType = TT_SPACE;
			}
		    }
		}
	    }
	    return TRUE;
	}
	else
	{
	    DBTechNoisyNameType(*argv);
	    return FALSE;
	}
    }

    /* Read the contact residues and check them for validity */
    nresidues = dbTechContactResidues(--argc, ++argv, contactType);
    if (nresidues < 0)
	return FALSE;

    /* Remember this as a paintable contact */
    dbContactInfo[dbNumContacts++] = &dbLayerInfo[contactType];

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * dbTechAddStackedContacts --
 *
 *	Generate new contact types where existing contact types share a
 *	residue.  These contact types will exist only on the planes
 *	shared between the two contact types.  This method allows contacts
 *	to be stacked without requiring a declaration of every combination
 *	in the techfile.
 *
 *	When searching for contact types with shared planes, we want to
 *	make sure that no existing contact exactly matches the stacked
 *	type, in case stacked contacts are explicitly called out in the
 *	tech file (e.g., pm12contact).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds to the tiletype database.
 *
 * ----------------------------------------------------------------------------
 */

void
dbTechAddStackedContacts()
{
    int m, n;
    LayerInfo *lim, *lin;
    int dbNumUserContacts = dbNumContacts;
    int result;

    for (m = 0; m < dbNumUserContacts; m++)
    {
	lim = dbContactInfo[m];
	for (n = m + 1; n < dbNumUserContacts; n++)
	{
	    lin = dbContactInfo[n];
	    result = dbTechAddOneStackedContact(lim->l_type, lin->l_type);
	    if (result == -3)
		return;		/* overran tiletype maximum number */
	}
    }

    /* Diagnostic */
    /* fprintf(stderr, "DBNumUserLayers = %d, DBNumTypes = %d\n",
		DBNumUserLayers, DBNumTypes);
    fflush(stderr); */
}

/*
 * ----------------------------------------------------------------------------
 * dbTechAddOneStackedContact --
 *
 *	Generate one new stacked contact type representing the union of
 *	types "type1" and "type2" on their shared plane(s) (normally
 *	one, but not necessarily).
 *
 * Results:
 *	tile type of new stacked contact if successful, -1 if stacking
 *	not allowed, -2 if a contact type already exists which stacks
 *	type1 and type2, or -3 on overrun of the total number of layers.
 * ----------------------------------------------------------------------------
 */

int
dbTechAddOneStackedContact(type1, type2)
    TileType type1, type2;
{
    LayerInfo *lim, *lin, *lp;
    TileTypeBitMask ttshared, ttall, mmask;
    TileType stackedType, sres;

    lim = &dbLayerInfo[type1];
    lin = &dbLayerInfo[type2];

    /* Both types must be contacts */
    if (!lim->l_isContact || !lin->l_isContact) return -1;

    /* Contacts do not stack if they share more than one plane */
    /* if (lim->l_pmask == lin->l_pmask) return -1; */
    if (((lim->l_pmask & lin->l_pmask) & ((lim->l_pmask & lin->l_pmask) - 1))
		!= 0)
	return -1;

    TTMaskAndMask3(&ttshared, &lim->l_residues, &lin->l_residues);

    if (!TTMaskEqual(&ttshared, &DBZeroTypeBits))
    {
	/* Find if there exists an image with the same residue	*/
	/* mask as the combination of these two contact types.  */

	TTMaskZero(&ttall);
	TTMaskSetMask3(&ttall, &lim->l_residues, &lin->l_residues);

	dbTechMatchResidues(&ttall, &mmask, TRUE);

	if (!TTMaskEqual(&mmask, &DBZeroTypeBits))
	    return -2; /* Contact type exists, so don't create one. */

	/* Also check if there is a stacking type made of these contact */
	/* images.  If we already have one, don't re-make it.		*/

	else if (DBTechFindStacking(type1, type2) != -1)
	    return -2; /* Stacking type exists, so don't create one. */

	/* All clear to set the residue bitmask for this contact type */

	/* Diagnostic */
	/* fprintf(stderr, "Stackable %s and %s\n",
		DBTypeLongName(lim->l_type),
		DBTypeLongName(lin->l_type));
	fflush(stderr); */

	stackedType = dbTechNewStackedType(lim->l_type, lin->l_type);

	/* Error condition (usually, reached max. no. tile types) */
	if (stackedType < 0) return -3;

	/* fill in layer info */

	lp = &dbLayerInfo[stackedType];
	lp->l_isContact = TRUE;

	/* The residue of a stacked contact is the two contacts	*/
	/* which make it up.  Residues which are contact types	*/
	/* are unique to stacking types.			*/

	TTMaskZero(&lp->l_residues);
	TTMaskSetType(&lp->l_residues, lim->l_type);
	TTMaskSetType(&lp->l_residues, lin->l_type);
	lp->l_pmask = lin->l_pmask | lim->l_pmask;

	/* The home plane of the contact is the plane of the	*/
	/* first shared residue found.				*/
	
	for (sres = TT_TECHDEPBASE; sres < DBNumUserLayers; sres++)
	    if (TTMaskHasType(&ttshared, sres))
	    {
		DBPlane(stackedType) = DBPlane(sres);
		break;
	    }

	/* Remember this as a paintable contact */
	dbContactInfo[dbNumContacts++] = &dbLayerInfo[stackedType];

	return (int)stackedType;	/* success */
    }
    return -1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPlaneToResidue --
 *
 * For the given tile type and plane, return the residue of that type on the
 * plane.
 *
 * Results:
 *	A tile type.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

TileType
DBPlaneToResidue(type, plane)
    TileType type;
    int plane;
{
    TileType rt, rt2;
    LayerInfo *lp = &dbLayerInfo[type], *lr;

    for (rt = TT_TECHDEPBASE; rt < DBNumUserLayers; rt++)
	if (TTMaskHasType(&lp->l_residues, rt))
	{
	    if (type >= DBNumUserLayers)	/* Stacked type */
	    {
		lr = &dbLayerInfo[rt];
		for (rt2 = TT_TECHDEPBASE; rt2 < DBNumUserLayers; rt2++)
		    if (TTMaskHasType(&lr->l_residues, rt2))
			if (DBPlane(rt2) == plane)
			    return rt2;
	    }
	    else if (DBPlane(rt) == plane)	/* Normal type */
		return rt;
	}

    return TT_SPACE;	/* no residue on plane */
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBMaskAddStacking ---
 *
 * A general-purpose routine to add stacked types containing types that are
 * already in the mask.
 *
 * ----------------------------------------------------------------------------
 */

void
DBMaskAddStacking(mask)
    TileTypeBitMask *mask;
{
    TileType ttype;
    TileTypeBitMask *rMask;

    for (ttype = DBNumUserLayers; ttype < DBNumTypes; ttype++)
    {
	rMask = DBResidueMask(ttype);
	if (TTMaskIntersect(rMask, mask))
	    TTMaskSetType(mask, ttype);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbTechContactResidues --
 *
 * Process an argc/argv vector of contact residue type names, creating
 * image types for each, and ensuring that:
 *	No residue is itself a contact
 *	One of the residues is on the home plane of contactType.
 *
 * Results:
 *	Returns the number of residues in the contact,
 *	or -1 in the event of an error.
 *
 * Side effects:
 *	Adds to database of layer types.
 *
 * ----------------------------------------------------------------------------
 */

int
dbTechContactResidues(argc, argv, contactType)
    int argc;
    char **argv;
    TileType contactType;
{
    int       homePlane, residuePlane, nresidues;
    PlaneMask pMask;
    TileType  residueType, imageType;
    bool      residueOnHome;
    LayerInfo *lp;
    TileTypeBitMask rmask, mmask;

    nresidues = 0;
    pMask = 0;
    residueOnHome = FALSE;

    TTMaskZero(&rmask);
    homePlane = DBPlane(contactType);
    for ( ; argc > 0; argc--, argv++)
    {
	if ((residueType = DBTechNoisyNameType(*argv)) < 0)
	    return -1;

	if (IsContact(residueType))
	{
	    TechError("Residue type %s is a contact itself\n",
		DBTypeLongName(residueType));
	    return -1;
	}
 
	/*
	 * Make sure the residue is on the same or an adjacent plane
	 * to the contact's home type.
	 */
	residuePlane = DBPlane(residueType);
	if (residuePlane < 0)
	{
	    TechError("Residue type %s doesn't have a home plane\n",
		DBTypeLongName(residueType));
	    return -1;
	}

	/* Enforce a single residue per plane */
	if (PlaneMaskHasPlane(pMask, residuePlane))
	{
	    TechError("Contact residues (%s) must be on different planes\n",
		DBTypeLongName(residueType));
	    return -1;
	}
	pMask |= PlaneNumToMaskBit(residuePlane);
	if (homePlane == residuePlane)
	    residueOnHome = TRUE;

	TTMaskSetType(&rmask, residueType);
    }

    if (!residueOnHome)
    {
	TechError("Contact type %s missing a residue on its home plane\n",
		DBTypeLongName(contactType));
	return -1;
    }

    /*
     * See if there are any other contact types with identical residues;
     * if so, disallow contactType.
     *
     * Can this restriction be lifted?  ---Tim 07/24/03
     * This restriction is partially lifted, as one can create a non-stacked
     * type such as "pad" having the same residues as one stacked type,
     * such as "m123c".  Due to the way magic handles stacked types, these
     * will not be confused.	---Tim 05/11/04
     *
     * Restriction entirely lifted 6/18/04.  Error message left as a warning
     * until it is clear that there are no unwanted side effects of having
     * two contact types with identical residues.
     */

    /* Find if there exists an image with the same residue mask */

    dbTechMatchResidues(&rmask, &mmask, TRUE);

    /* Ignore self */

    TTMaskClearType(&mmask, contactType);
    
    if (!TTMaskEqual(&mmask, &DBZeroTypeBits))
    {   
	TxPrintf("Contact residues for %s identical to those for ",
		DBTypeLongName(contactType));

	for (imageType = TT_TECHDEPBASE; imageType < DBNumTypes; imageType++)
	    if (TTMaskHasType(&mmask, imageType))
		TxPrintf("%s ", DBTypeLongName(imageType));

	TxPrintf("\n");
    }  

    /* All clear to set the residue bitmask for this contact type */

    lp = &dbLayerInfo[contactType];

    lp->l_isContact = TRUE;
    TTMaskSetMask(&lp->l_residues, &rmask);
    lp->l_pmask = pMask;

    return nresidues;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTechMatchResidues --
 *
 * Find the types whose residues match those of the supplied inMask.
 *
 * All this masking about is rather subtle, so pay attention:  Each TYPE
 * has a RESIDUAL, which for a contact is (generally) the type which surrounds
 * each contact image on its plane (remember, there is one contact image per
 * plane contacted).  So when creating the paint/erase tables, we want to
 * knock out planes from the residual mask of a contact and see if what's
 * left is another kind of contact.  So from the original contact TYPE, we
 * make a mask of RESIDUALS, then this routine compares that to the mask
 * of residuals for all other (contact) types, then create another mask of
 * all the types which had matching residual masks, and return it.  Grok
 * that?
 *
 * Note that the stacked contact mechanism returns the stacked type where
 * residues of two types match.  This is required when composing paint
 * rules for contacts on contacts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Yet another TileType bitmask pointer, outMask, is filled in with
 *	the matching types.
 *
 * ----------------------------------------------------------------------------
 */

void
dbTechMatchResidues(inMask, outMask, contactsOnly)
    TileTypeBitMask *inMask, *outMask;
    bool contactsOnly;
{
    TileType type;
    LayerInfo *li;

    TTMaskZero(outMask);
    for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
    {
	li = &dbLayerInfo[type];
	if (!li->l_isContact && contactsOnly)
	    continue;

	if (TTMaskEqual(inMask, &li->l_residues))
	    TTMaskSetType(outMask, type);
    }
}


/*
 * ----------------------------------------------------------------------------
 * DBTechFindStacking --
 *
 *	Find a stacking tile type which connects the two indicated types.
 * Stacking types have the l_residues field of the LayerInfo entry filled
 * with the mask of the two types which make up the stacked contact type.
 *
 * Results:
 *	The stacking tile type, if it exists, or -1 if there's no stacking
 *	type connecting type1 and type2.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

TileType
DBTechFindStacking(type1, type2)
    TileType type1, type2;
{
    TileType rtype, rtype1, rtype2, stackType;
    LayerInfo *li;

    for (stackType = DBNumUserLayers; stackType < DBNumTypes; stackType++)
    {
	rtype1 = rtype2 = -1;
	li = &dbLayerInfo[stackType];
	for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
	    if (TTMaskHasType(&li->l_residues, rtype))
	    {
		rtype1 = rtype;
		break;
	    }
	for (++rtype; rtype < DBNumUserLayers; rtype++)
	    if (TTMaskHasType(&li->l_residues, rtype))
	    {
		rtype2 = rtype;
		break;
	    }

	if (((rtype1 == type1) && (rtype2 == type2)) ||
		((rtype1 == type2) && (rtype2 == type1)))
	    return stackType;

    }
    return -1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechFinalContact --
 *
 * Conclude reading the "contact" section of a technology file.
 * At this point, all tile types are known so we can call dbTechInitPaint()
 * to fill in the default paint/erase tables, and dbTechInitMasks() to fill
 * in the various exported TileTypeBitMasks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills in the dbLayerInfo table for non-contacts.
 *	Sets DBLayerTypeMaskTbl to its final value.
 *	Initializes DBTypePlaneMaskTbl[] and DBPlaneTypes[].
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechFinalContact()
{
    TileType primaryType;
    LayerInfo *lp;
    int pNum;

    /* Fill in plane and residue info for non-contact types */

    for (primaryType = 0; primaryType < DBNumTypes; primaryType++)
    {
	lp = &dbLayerInfo[primaryType];
	pNum = DBPlane(primaryType);
	if (!lp->l_isContact && (pNum > 0))
	{
	    lp->l_pmask = PlaneNumToMaskBit(pNum);
	    TTMaskSetOnlyType(&lp->l_residues, primaryType);
	}
    }

    /*
     * Initialize the masks of planes on which each type appears.
     * It will contain all planes (except subcell) for space,
     * the home plane for each type up to DBNumTypes, and no
     * planes for undefined types.  Also update the mask of
     * types visible on each plane.
     */

    DBTypePlaneMaskTbl[TT_SPACE] = ~(PlaneNumToMaskBit(PL_CELL));
    for (primaryType = 0; primaryType < DBNumTypes; primaryType++)
    {
	pNum = DBPlane(primaryType);
	if (pNum > 0)
	{
	    DBTypePlaneMaskTbl[primaryType] = PlaneNumToMaskBit(pNum);
	    if (!IsContact(primaryType))
		TTMaskSetType(&DBPlaneTypes[pNum], primaryType);
	    else
	    {
		lp = &dbLayerInfo[primaryType];

		/* if (primaryType < DBNumUserLayers) */
		    DBTypePlaneMaskTbl[primaryType] |= lp->l_pmask;

		for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		    if (PlaneMaskHasPlane(lp->l_pmask, pNum))
			TTMaskSetType(&DBPlaneTypes[pNum], primaryType);
	    }
	}
    }

    /* Create a mask for which each type only appears on one plane.     */
    /* This is useful for non-redundant searches.                       */

    for (pNum = 0;  pNum < PL_MAXTYPES;  pNum++)
	TTMaskZero(&DBHomePlaneTypes[pNum]);

    for (primaryType = TT_SPACE + 1; primaryType < DBNumTypes; primaryType++)
	TTMaskSetType(&DBHomePlaneTypes[DBPlane(primaryType)], primaryType);
}

/*
 * ----------------------------------------------------------------------------
 * DBTechTypesOnPlane --
 *
 * Given a tile type bitmask and a plane index, check if all types in the
 * bitmask have an image on the indicated plane.
 *
 * Results:
 *	TRUE if all types in "src" have at least one image on plane.
 *	FALSE if any type in "src" does not contain any image in plane.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

bool
DBTechTypesOnPlane(src, plane)
    TileTypeBitMask *src;
    int plane;
{
    int i;
    PlaneMask pmask;

    for (i = 0; i < DBNumTypes; i++)
	if (TTMaskHasType(src, i))
	    if (!PlaneMaskHasPlane(DBTypePlaneMaskTbl[i], plane))
		return FALSE;

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechGetContact --
 *
 * Given two tile types, determine the corresponding contact type.
 * (Or rather, for two types, get the first contact type connecting
 * the two planes on which those types lie. . . not quite the same
 * thing.)
 *
 * Results:
 *	Returns a contact type.
 *
 * Side effects:
 *	Prints stuff if it can't find a contact type.
 *
 * ----------------------------------------------------------------------------
 */

TileType
DBTechGetContact(type1, type2)
    TileType type1, type2;
{
    int pmask;
    LayerInfo *lp;
    TileType t;

    pmask = DBTypePlaneMaskTbl[type1] | DBTypePlaneMaskTbl[type2];
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	lp = &dbLayerInfo[t];
	if (lp->l_isContact)
	    if (lp->l_pmask == pmask)
		return t;
    }

    TxPrintf("No contact type for %d %d\n", type1, type2);
    return (TileType) -1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBIsContact --
 *
 *   Like IsContact(), except as a subroutine, not a macro.  For export
 *   to other routines.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBIsContact(type)
    TileType type;
{
    if (IsContact(type)) return TRUE;
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBResidueMask --
 *
 *   Get the residue mask of the specified type.  For export to other
 *   routines.
 * ----------------------------------------------------------------------------
 */

TileTypeBitMask *
DBResidueMask(type)
    TileType type;
{
    LayerInfo *li = &dbLayerInfo[type];
    return (&li->l_residues);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBFullResidueMask --
 *
 *   Get the residue mask of the specified type.  For stacking contacts,
 *   decompose the contact residues into their component residue layers.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	"rmask" is a pointer to a TileTypeBitMask.  The result is placed
 *	in this location.
 * ----------------------------------------------------------------------------
 */

void
DBFullResidueMask(type, rmask)
    TileType type;
    TileTypeBitMask *rmask;
{
    TileType t;
    TileTypeBitMask *lmask;
    LayerInfo *li, *lr;

    li = &dbLayerInfo[type];
    lmask = &li->l_residues;
    TTMaskZero(rmask);

    if (type < DBNumUserLayers)
    {
	TTMaskSetMask(rmask, &li->l_residues);
    }
    else
    {
	for (t = TT_TECHDEPBASE; t < DBNumUserLayers; t++)
	    if (TTMaskHasType(lmask, t))
	    {
		lr = &dbLayerInfo[t];
		TTMaskSetMask(rmask, &lr->l_residues);
	    }
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTechPrintContacts --
 *
 * DEBUGGING.
 * Print a list of the contact types to which each possible contact image
 * belongs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints stuff.
 *
 * ----------------------------------------------------------------------------
 */

void
dbTechPrintContacts()
{
    LayerInfo *lpImage;
    TileType t;
    int m, pNum;

    for (m = 0; m < dbNumContacts; m++)
    {
	lpImage = dbContactInfo[m];
	TxPrintf("Contact %s (on %s) ",
		DBTypeLongName(lpImage->l_type),
		DBPlaneLongName(DBPlane(lpImage->l_type)));

	TxPrintf(" connects:");
	for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    if ( TTMaskHasType(&DBConnectTbl[lpImage->l_type], t) )
		TxPrintf(" %s", DBTypeLongName(t));

	TxPrintf(" planes:");
	for ( pNum = PL_TECHDEPBASE; pNum < PL_MAXTYPES; pNum++ )
	    if ( PlaneNumToMaskBit(pNum) & DBConnPlanes[lpImage->l_type] )
		TxPrintf(" %s", DBPlaneLongName(pNum));

	TxPrintf(" residues:");
	for ( t = TT_TECHDEPBASE; t < DBNumTypes; t++ )
	    if (TTMaskHasType(&lpImage->l_residues, t))
		TxPrintf(" %s on plane %s\n",
				DBTypeLongName(t),
				DBPlaneLongName(DBPlane(t)));

	TxPrintf("\n");
    }
}

