/*
 * DBtechname.c --
 *
 * Mapping between tile types and their names.
 * WARNING: with the exception of DB*TechName{Type,Plane}() and
 * DB*ShortName(), * the procedures in this file MUST be called
 * after DBTechFinalType() has been called.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtechname.c,v 1.3 2008/06/01 18:37:39 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/tech.h"
#include "textio/textio.h"
#include "utils/malloc.h"


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechNameType --
 *
 * Map from a type name into a type number.  If the type name has
 * the form "<type>/<plane>" and <type> is a contact, then the
 * type returned is the image of the contact on <plane>.  Of
 * course, in this case, <type> must have an image on <plane>.
 *
 * Results:
 *	Type number.  A value of -2 indicates that the type name was
 *	unknown; -1 indicates that it was ambiguous.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

TileType
DBTechNameType(typename)
    char *typename;	/* The name of the type */
{
    char *slash;
    TileType type;
    int plane;
    LayerInfo *lp;

    slash = strchr(typename, '/');
    if (slash != NULL) *slash = 0;
    type = (TileType)(spointertype) dbTechNameLookup(typename, &dbTypeNameLists);
    if (type < 0)
    {
	/* Check against the alias table.  However, any specified alias	*/
	/* must point to a SINGLE tile type, or we treat it as		*/
	/* ambiguous.							*/

	HashEntry *he;
	TileTypeBitMask *bitmask;
	TileType ttest;

	he = HashLookOnly(&DBTypeAliasTable, typename);
	if (he)
	{
	    bitmask = (TileTypeBitMask *)HashGetValue(he);
	    for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
		if (TTMaskHasType(bitmask, type))
		{
		    for (ttest = type + 1; ttest < DBNumUserLayers; ttest++)
			if (TTMaskHasType(bitmask, ttest))
			{
			    type = -1;
			    break;
			}
		    break;
		}

	    if (type == DBNumUserLayers)
		type = -2;
	}
    }

    if (slash == NULL) return type;
    *slash = '/';
    if (type < 0) return type;

    /* There's a plane qualification.  Make sure the type exists */
    /* on the indicated plane.  If not, return an error.	 */

    plane = (spointertype) dbTechNameLookup(slash + 1, &dbPlaneNameLists);
    if (plane < 0) return -2;

    lp = &dbLayerInfo[type];
    if (PlaneMaskHasPlane(lp->l_pmask, plane))
	return type;

    return -2;
}

TileType
DBTechNameTypeExact(typename)
    char *typename;	/* The name of the type */
{
    char *slash;
    ClientData result;

    slash = strchr(typename, '/');
    if (slash != NULL) return (TileType)(-1);

    result = dbTechNameLookupExact(typename, &dbTypeNameLists);
    return (TileType)((spointertype)result);
}

/*
 *-------------------------------------------------------------------------
 *
 * The following returns a bitmask with the appropriate types set for the
 *	typename supplied.
 *
 * Results: returns the first type found
 *
 * Side Effects: sets bitmask with the appropriate types.
 *
 *-------------------------------------------------------------------------
 */

TileType
DBTechNameTypes(typename, bitmask)
    char *typename;	/* The name of the type */
    TileTypeBitMask	*bitmask;
{
    char *slash;
    TileType type;
    int plane;
    LayerInfo *lp;

    TTMaskZero(bitmask);
    slash = strchr(typename, '/');
    if (slash != NULL) *slash = 0;
    type = (TileType)(spointertype) dbTechNameLookup(typename, &dbTypeNameLists);
    if (type < 0)
    {
	HashEntry *he;

	/* Check against the alias table */
	he = HashLookOnly(&DBTypeAliasTable, typename);
	if (he)
	{
	    TTMaskSetMask(bitmask, (TileTypeBitMask *)HashGetValue(he));
	    for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
		if (TTMaskHasType(bitmask, type))
		    break;

	    if (type == DBNumUserLayers)
		type = -2;
	}
    }
    else
    	 TTMaskSetType(bitmask, type);

    if (slash == NULL)
	return type;
    else
	*slash = '/';

    /* There's a plane qualification.  Locate the image. */

    plane = (spointertype) dbTechNameLookup(slash + 1, &dbPlaneNameLists);
    if (plane < 0) return -2;

    TTMaskAndMask(bitmask, &DBPlaneTypes[plane]);

    /* If the type is no longer in the bitmask, return the first type that is. */

    if (!TTMaskHasType(bitmask, type))
	for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
	    if (TTMaskHasType(bitmask, type))
		break;

    return (type < DBNumUserLayers) ? type : -2;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechNoisyNameType --
 *
 * Map from a type name into a type number, complaining if the type
 * is unknown.
 *
 * Results:
 *	Type number.  A value of -2 indicates that the type name was
 *	unknown; -1 indicates that it was ambiguous.
 *
 * Side effects:
 *	Prints a diagnostic message if the type name is unknown.
 *
 * ----------------------------------------------------------------------------
 */

TileType
DBTechNoisyNameType(typename)
    char *typename;	/* The name of the type */
{
    TileType type;

    switch (type = DBTechNameType(typename))
    {
	case -1:
	    TechError("Ambiguous layer (type) name \"%s\"\n", typename);
	    break;
	case -2:
	    TechError("Unrecognized layer (type) name \"%s\"\n", typename);
	    break;
	default:
	    if (type < 0)
		TechError("Funny type \"%s\" returned %d\n", typename, type);
	    break;
    }

    return (type);
}
/*
 * ----------------------------------------------------------------------------
 *
 * DBTechNamePlane --
 *
 * Map from a plane name into a plane number.
 *
 * Results:
 *	Plane number.  A value of -2 indicates that the plane name was
 *	unknown; -1 indicates that it was ambiguous.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
DBTechNamePlane(planename)
    char *planename;	/* The name of the plane */
{
    return ((spointertype) dbTechNameLookup(planename, &dbPlaneNameLists));
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechNoisyNamePlane --
 *
 * Map from a plane name into a plane number, complaining if the plane
 * is unknown.
 *
 * Results:
 *	Plane number.  A value of -2 indicates that the plane name was
 *	unknown; -1 indicates that it was ambiguous.
 *
 * Side effects:
 *	Prints a diagnostic message if the type name is unknown.
 *
 * ----------------------------------------------------------------------------
 */

int
DBTechNoisyNamePlane(planename)
    char *planename;	/* The name of the plane */
{
    int pNum;

    switch (pNum = DBTechNamePlane(planename))
    {
	case -1:
	    TechError("Ambiguous plane name \"%s\"\n", planename);
	    break;
	case -2:
	    TechError("Unrecognized plane name \"%s\"\n", planename);
	    break;
    }

    return (pNum);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTypeShortName --
 * DBPlaneShortName --
 *
 * Return the short name for a type or plane.
 * The short name is the "official abbreviation" for the type or plane,
 * identified by a leading '*' in the list of names in the technology
 * file.
 *
 * Results:
 *	Pointer to the primary short name for the given type or plane.
 *	If the type or plane has no official abbreviation, returns
 *	a pointer to the string "???".
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
DBTypeShortName(type)
    TileType type;
{
    NameList *tbl;

    for (tbl = dbTypeNameLists.sn_next;
	    tbl != &dbTypeNameLists;
	    tbl = tbl->sn_next)
    {
	if (tbl->sn_value == (ClientData)(pointertype) type && tbl->sn_primary)
	    return (tbl->sn_name);
    }

    if (type < 0) return ("ERROR");
    else if (DBTypeLongNameTbl[type])
	return (DBTypeLongNameTbl[type]);
    return ("???");
}

char *
DBPlaneShortName(pNum)
    int pNum;
{
    NameList *tbl;

    for (tbl = dbPlaneNameLists.sn_next;
	    tbl != &dbPlaneNameLists;
	    tbl = tbl->sn_next)
    {
	if (tbl->sn_value == (ClientData)(pointertype) pNum && tbl->sn_primary)
	    return (tbl->sn_name);
    }

    if (DBPlaneLongNameTbl[pNum])
	return (DBPlaneLongNameTbl[pNum]);
    return ("???");
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechTypesToPlanes --
 *
 * Convert a TileTypeBitMask into a mask of the planes which may
 * contain tiles of that type.
 *
 * Results:
 *	A mask with bits set for those planes in which tiles of
 *	the types specified by the mask may reside.  The mask
 *	is guaranteed only to contain bits corresponding to
 *	paint tile planes.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

PlaneMask
DBTechTypesToPlanes(mask)
    TileTypeBitMask *mask;
{
    TileType t;
    PlaneMask planeMask, noCellMask, retMask;

    /* Space tiles are present in all planes but the cell plane */
    noCellMask = ~(PlaneNumToMaskBit(PL_CELL));
    if (TTMaskHasType(mask, TT_SPACE)) {
	retMask = PlaneNumToMaskBit(DBNumPlanes) - 1;
	retMask &= noCellMask;
	return retMask;
    }

    planeMask = 0;
    for (t = 0; t < DBNumTypes; t++)
	if (TTMaskHasType(mask, t))
	    planeMask |= DBTypePlaneMaskTbl[t];

    retMask = planeMask & noCellMask;
    return retMask;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechPrintTypes --
 *
 * 	This routine prints out all the layer names for types defined
 *	in the current technology.  If "typename" is non-NULL, then
 *	the "canonical" name of typename is printed.
 *
 * Results:
 *	None.  In the Tcl version, the layer names are returned as a Tcl list.
 *
 * Side effects:
 *	Stuff is printed.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechPrintTypes(mask, dolist)
    TileTypeBitMask *mask;  /* Print layers defined by this mask.	*/
    bool dolist;	    /* return as a list and don't print aliases	*/
{
    TileType i;
    NameList *p;
    bool firstline = TRUE;
    bool firstname;
    DefaultType *dtp;
    char *keepname;

    if (!dolist) TxPrintf("Layer names are:\n");

    /* List technology-dependent types */
    for (i = TT_TECHDEPBASE; i < DBNumUserLayers; i++)
    {
	if (!TTMaskHasType(mask, i)) continue;
	firstname = TRUE;
	for (p = dbTypeNameLists.sn_next; p != &dbTypeNameLists;
		p = p->sn_next)
	{
	    if (((TileType)(spointertype) p->sn_value) == i)
	    {
		if (dolist) 
		{
		    if (firstname) keepname = p->sn_name;
		    else if (strlen(p->sn_name) > strlen(keepname))
			keepname = p->sn_name;
		}
		else
		{
		    if (firstname) TxPrintf("    %s", p->sn_name);
		    else TxPrintf(" or %s", p->sn_name);
		}
		firstname = FALSE;
	    }
	}

	if (!firstline)
	{
	    if (dolist)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_AppendResult(magicinterp, " ", (char *)NULL);
#else
		TxPrintf(" ", keepname);
#endif
	    }
	}

	if (dolist)
	{
#ifdef MAGIC_WRAPPER
	    Tcl_AppendResult(magicinterp, keepname, (char *)NULL);
#else
	    TxPrintf("%s", keepname);
#endif
	}
	else TxPrintf("\n");

	firstline = FALSE;
    }

    /* List built-in types that are normally painted by name */
    for (dtp = dbTechDefaultTypes; dtp->dt_names; dtp++)
    {
	if (!TTMaskHasType(mask, dtp->dt_type)) continue;
	if (dtp->dt_print)
	{
	    firstname = TRUE;
	    for (p = dbTypeNameLists.sn_next; p != &dbTypeNameLists;
		p = p->sn_next)
	    {
		if (((TileType)(spointertype) p->sn_value) == dtp->dt_type)
		{
		    if (dolist) 
		    {
		        if (firstname) keepname = p->sn_name;
		        else if (strlen(p->sn_name) > strlen(keepname))
			    keepname = p->sn_name;
		    }
		    else
		    {
		        if (firstname) TxPrintf("    %s", p->sn_name);
		        else TxPrintf(" or %s", p->sn_name);
		    }
		    firstname = FALSE;
		}
	    }

	    if (!firstline)
	    {
		if (dolist)
		{
#ifdef MAGIC_WRAPPER
		    Tcl_AppendResult(magicinterp, " ", (char *)NULL);
#else
		    TxPrintf(" ", keepname);
#endif
		}
	    }

	    if (dolist)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_AppendResult(magicinterp, keepname, (char *)NULL);
#else
		TxPrintf("%s", keepname);
#endif
	    }
	    else TxPrintf("\n");

	    firstline = FALSE;
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechNoisyNameMask --
 *
 *	Parses an argument string that selects a group of layers.
 *	The string may contain one or more layer names separated
 *	by commas.  The special layer name of "0" specifies no layer,
 *      it is used as a place holder, e.g., to specify a null
 *      layer list for the CornerTypes field in a drc edge-rule.
 *      In addition, a tilde may be used to indicate
 *	"all layers but", and parentheses may be used for grouping.
 *	Thus ~x means "all layers but x", and ~(x,y),z means "z plus
 *	everything except x and y)".  When contacts are specified,
 *	ALL images of the contact are automatically included, unless
 *	a specific plane is indicated in the layer specification
 *	using "/".  For example, x/foo refers to the image of contact
 *	"x" on plane "foo".  The layer specification may also follow
 *	a parenthesized group.  For example, ~(x,y)/foo refers to
 *	all layers on plane "foo" except "x" and "y".
 *
 *	Magic version 7.3 defines *x to mean "x and all
 *	contact layers containing x as a residue".  Thus, for example,
 *	*metal1 expands to m1,pc,ndc,pdc,nsc,psc,m2c,...  Because many
 *	connect, cif, extract, and drc rules list all these layers,
 *	the asterisk notation makes the techfile shorter and more
 *	readable.
 *
 *	Magic version 7.3 also defines a hash table "DBTypeAliasTable"
 *	that allows macros to be embedded in the technology file.
 *	Macros are expanded first, prior to applying the parsing
 *	described above.
 *
 * Results:
 *	Returns a plane mask of all the planes specified.
 *
 * Side effects:
 *	Error messages are output if layers aren't understood.
 *	Sets the TileTypeBitMask 'mask' to all the layer names indicated.
 *
 * ----------------------------------------------------------------------------
 */

PlaneMask
DBTechNameMask0(layers, mask, noisy)
    char *layers;			/* String to be parsed. */
    TileTypeBitMask *mask;		/* Where to store the layer mask. */
    bool noisy;				/* Whether or not to output errors */
{
    char *p, *p2, c;
    TileTypeBitMask m2;        /* Each time around the loop, we will
				* form the mask for one section of
				* the layer string.
				*/
    char save;
    bool allBut;
    PlaneMask planemask = 0;
    TileTypeBitMask *rMask;

    TTMaskZero(mask);
    p = layers;
    while (TRUE)
    {
	TTMaskZero(&m2);

	c = *p;
	if (c == 0) break;

	/* Check for a tilde, and remember it in order to do negation. */

	if (c == '~')
	{
	    allBut = TRUE;
	    p += 1;
	    c = *p;
	}
	else allBut = FALSE;

	/* Check for parentheses.  If there's an open parenthesis,
	 * find the matching close parenthesis and recursively parse
	 * the string in-between.
	 */

	if (c == '(')
	{
	    int nesting = 0;

	    p += 1;
	    for (p2 = p; ; p2 += 1)
	    {
		if (*p2 == '(') nesting += 1;
		else if (*p2 == ')')
		{
		    nesting -= 1;
		    if (nesting < 0) break;
		}
		else if (*p2 == 0)
		{
		    TechError("Unmatched parenthesis in layer name \"%s\".\n",
			layers);
		    break;
		}
	    }
	    save = *p2;
	    *p2 = 0;
	    planemask |= DBTechNameMask0(p, &m2, noisy);
	    *p2 = save;
	    if (save == ')') p = p2 + 1;
	    else p = p2;
	}
	else
	{
	    TileType t, rtype;
	    bool allResidues = FALSE;

	    /* No parenthesis, so just parse off a single name.  Layer
	     * name "0" corresponds to no layers at all.
	     */

	    for (p2 = p; ; p2++)
	    {
		c = *p2;
		if ((c == '/') || (c == ',') || (c == 0)) break;
	    }
	    if (p2 == p)
	    {
		TechError("Missing layer name in \"%s\".\n", layers);
	    }
	    else if (strcmp(p, "0") != 0)
	    {
		HashEntry *he;

		save = *p2;
		*p2 = '\0';

		/* Check the alias table for macro definitions */
		he = HashLookOnly(&DBTypeAliasTable, p);
		if (he)
		{
		    TileTypeBitMask *amask;
		    amask = (TileTypeBitMask *)HashGetValue(he);
		    TTMaskSetMask(&m2, amask);
		}
		else
		{
		    /* Check for asterisk notation, meaning to include	*/
		    /* all types which have this type as a residue.	*/
		    if (*p == '*')
		    {
			allResidues = TRUE;
			p++;
		    }

		    if (noisy)
			t = DBTechNoisyNameType(p);
		    else
			t = DBTechNameType(p);
		    if (t >= 0)
			m2 = DBLayerTypeMaskTbl[t];

		    /* Include all types which have t as a residue		*/

		    if (allResidues)
			for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
			{
			    rMask = DBResidueMask(rtype);
			    if (TTMaskHasType(rMask, t))
				TTMaskSetType(&m2, rtype);
			}

		    /* Include all stacking types which have t as a residue	*/
		    /* (this is done regardless of the presence of "*")		*/

		    for (rtype = DBNumUserLayers; rtype < DBNumTypes; rtype++)
		    {
			rMask = DBResidueMask(rtype);
			if (TTMaskIntersect(rMask, &m2))
			    TTMaskSetType(&m2, rtype);
		    }
		}
		*p2 = save;
	    }
	    p = p2;
	}

	/* Now negate the layers, if that is called for. */

	if (allBut) TTMaskCom(&m2);

	/* Restrict to a single plane, if that is called for. */

	if (*p == '/')
	{
	    int plane;

	    p2 = p+1;
	    while ((*p2 != 0) && (*p2 != ',')) p2 += 1;
	    save = *p2;
	    *p2 = 0;
	    if (noisy)
    		plane = DBTechNoisyNamePlane(p+1);
	    else
    		plane = DBTechNamePlane(p+1);
	    *p2 = save;
	    p = p2;
	    if (plane > 0)
	    {
		TTMaskAndMask(&m2, &DBPlaneTypes[plane]);
		planemask = PlaneNumToMaskBit(plane);
	    }
	}
	else
	{
	    TileType t; 

	    for (t = TT_TECHDEPBASE; t < DBNumUserLayers; t++)
		if (TTMaskHasType(&m2, t))
		    planemask |= DBTypePlaneMaskTbl[t];
	}

	TTMaskSetMask(mask, &m2);
	while (*p == ',') p++;
    }

    /* If there are no types, or if "space" is the only type, then	*/
    /* return a full planemask						*/

    if ((TTMaskIsZero(mask) || TTMaskEqual(mask, &DBSpaceBits)) &&
		(planemask == (PlaneMask)0))
	planemask = DBTypePlaneMaskTbl[TT_SPACE];

    return planemask;
}

/* Wrappers for DBTechNameMask0() */

PlaneMask
DBTechNoisyNameMask(layers, mask)
    char *layers;			/* String to be parsed. */
    TileTypeBitMask *mask;		/* Where to store the layer mask. */
{
    return DBTechNameMask0(layers, mask, TRUE);
}

PlaneMask
DBTechNameMask(layers, mask)
    char *layers;			/* String to be parsed. */
    TileTypeBitMask *mask;		/* Where to store the layer mask. */
{
    return DBTechNameMask0(layers, mask, FALSE);
}
