/*
 * DBtechpaint.c --
 *
 * Management of composition rules and the paint/erase tables.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtpaint.c,v 1.2 2010/06/08 19:16:42 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>
#include <string.h>	/* for memset(), memcpy() */

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/tech.h"
#include "textio/textio.h"

    /* Painting and erasing tables */
PaintResultType DBPaintResultTbl[NP][NT][NT];
PaintResultType DBEraseResultTbl[NP][NT][NT];
PaintResultType DBWriteResultTbl[NT][NT];
PaintResultType DBSpecialResultTbl[NT];

PlaneMask DBTypePaintPlanesTbl[NT];
PlaneMask DBTypeErasePlanesTbl[NT];

/* ----------------- Data local to tech file processing --------------- */

/*
 * Tables telling which rules are default, and which have come
 * from user-specified rules.  The bit is CLEAR if the type is
 * a default type.
 */
TileTypeBitMask dbNotDefaultEraseTbl[NT];
TileTypeBitMask dbNotDefaultPaintTbl[NT];

/* --------------------- Data local to this file ---------------------- */

int dbNumSavedRules = 0;
Rule dbSavedRules[NT];

/* Forward declarations */

extern void dbTechBitTypeInit();

bool dbTechAddPaintErase();
bool dbTechSaveCompose();

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechInitCompose --
 *
 * Initialize the painting and erasing rules prior to processing
 * the "compose" section.  The rules for builtin types are computed
 * here, as well as the default rules for all other types.  This
 * procedure must be called after the "types" and "contacts" sections
 * have been read, since we need to know about all existing tile types.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the paint and erase tables.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechInitCompose()
{
    TileType s, t, r;
    int ps;
    PaintResultType *stype, *dtype;
    TileTypeBitMask *ttype;

    /* Default painting rules for error types */

    static TileType errorBitToType[] =
    {
	TT_SPACE,	/* 0 */		TT_ERROR_P,	/* 1 */
	TT_ERROR_S,	/* 2 */		TT_ERROR_PS,	/* 3 */
    };

    /* Painting and erasing are no-ops for undefined tile types */

    /* The following code is the FAST version using memcpy().	*/
    /* See below for the actual slow loops.			*/

    stype = dtype = &(DBEraseResultTbl[0][0][0]);
    for (ps = 0; ps < TT_MAXTYPES; ps++)
	*dtype++ = (PaintResultType)ps; 
    for (ps = 1; ps < PL_MAXTYPES * TT_MAXTYPES; ps++)
    {
	memcpy((void *)dtype, (void *)stype, (size_t)TT_MAXTYPES
		* sizeof(PaintResultType));
	dtype += TT_MAXTYPES;
    }

    /* Fast copy the entire erase table to the paint table memory */
    dtype = &(DBPaintResultTbl[0][0][0]);
    memcpy((void *)dtype, (void *)stype, (size_t)(TT_MAXTYPES
		* TT_MAXTYPES * PL_MAXTYPES * sizeof(PaintResultType)));

    /* The following code is dreadfully slow, but I'm leaving it */
    /* in as a comment because it's easier to read.  The code	 */
    /* above uses memory copying tricks to speed up the process. */
    /*

    for (pNum = 0; pNum < PL_MAXTYPES; pNum++)
    {
	for (s = 0; s < TT_MAXTYPES; s++)
	{
	    for (t = 0; t < TT_MAXTYPES; t++)
	    {
		/- Paint and erase are no-ops -/
		dbSetEraseEntry(s, t, pNum, s);
		dbSetPaintEntry(s, t, pNum, s);
	    }
	}
    }

    for (s = 0; s < TT_MAXTYPES; s++)
    {
	for (t = 0; t < TT_MAXTYPES; t++)
	{
	    /- Write overwrites existing contents -/
	    dbSetWriteEntry(s, t, t);
	}
    }
    */

#if TT_MAXTYPES <= 256
    /* For single-byte values, memset() is fastest.

    dtype = &(DBWriteResultTbl[0][0]);
    for (q = 0; q < TT_MAXTYPES; q++)
    {
	memset((void *)dtype, (int)q, (size_t)TT_MAXTYPES);
	dtype += TT_MAXTYPES;
    }

#else
    /* This is the slow loop, but it still faster than using	*/
    /* macro dbSetWriteEntry().					*/

    dtype = &(DBWriteResultTbl[0][0]);
    for (t = 0; t < TT_MAXTYPES; t++)
	for (s = 0; s < TT_MAXTYPES; s++)
	    *dtype++ = t;

#endif

    /* All painting and erasing rules are default initially */

    /* This is also faster than the loop below. */

    ttype = &(dbNotDefaultEraseTbl[0]);
    for (s = 0; s < DBNumTypes; s++) *ttype++ = DBZeroTypeBits;
    ttype = &(dbNotDefaultPaintTbl[0]);
    for (s = 0; s < DBNumTypes; s++) *ttype++ = DBZeroTypeBits;

    /*
    for (s = 0; s < DBNumTypes; s++)
    {
	dbNotDefaultEraseTbl[s] = DBZeroTypeBits;
	dbNotDefaultPaintTbl[s] = DBZeroTypeBits;
    }
    */

    /*
     *	For each type t:
     *	    erase(t, t, plane(t)) -> SPACE
     *
     *	For each type s, t:
     *	    paint(s, t, plane(t)) -> t
     *	    paint(s, t, ~plane(t)) -> s
     */

    for (s = 0; s < DBNumTypes; s++)
    {
	if ((ps = DBPlane(s)) > 0)
	{
	    for (t = 0; t < DBNumTypes; t++)
	    {
		if (DBPlane(t) > 0)
		{
		    r = (ps == DBPlane(t)) ? t : s;
		    dbSetEraseEntry(s, t, ps, s);
		    dbSetPaintEntry(s, t, ps, r);
		}
	    }

	    /* Everything can be erased to space on its home plane */
	    dbSetEraseEntry(s, s, ps, TT_SPACE);

	    /* Everything paints over space on its home plane */
	    dbSetPaintEntry(TT_SPACE, s, ps, s);
	}
    }

    /*
     * Special handling for check tile and error tile combinations.
     */
#define	PCHK	PL_DRC_CHECK
#define	PERR	PL_DRC_ERROR
#define	tblsize(t)	( (sizeof (t)) / (sizeof (t[0])) )
    dbTechBitTypeInit(errorBitToType, tblsize(errorBitToType), PERR, FALSE);
#undef	tblsize

    /*
     * Paint results are funny for check plane because
     * CHECKPAINT+CHECKSUBCELL = CHECKPAINT
     */
    dbSetPaintEntry(TT_SPACE, TT_CHECKPAINT, PCHK, TT_CHECKPAINT);
    dbSetPaintEntry(TT_SPACE, TT_CHECKSUBCELL, PCHK, TT_CHECKSUBCELL);
    dbSetPaintEntry(TT_CHECKPAINT, TT_CHECKSUBCELL, PCHK, TT_CHECKPAINT);
    dbSetPaintEntry(TT_CHECKSUBCELL, TT_CHECKPAINT, PCHK, TT_CHECKPAINT);
#undef	PCHK
#undef	PERR

    /* Added 5/27/10:  Special table used for painting non-Manhattan	*/
    /* tiles.  Uses TT_CHECKSUBCELL because that type does not exist on	*/
    /* any paintable plane, and the checkpaint plane does not use non-	*/
    /* manhattan tiles.							*/

    for (s = 0; s < DBNumTypes; s++) DBSpecialResultTbl[s] = TT_CHECKSUBCELL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTechBitTypeInit --
 *
 * Handle initialization of the paint and erase result tables for a
 * set of ln2(n) primary types with n distinct mutual overlap types.
 * The table bitToType points to a table containing n TileTypes
 * (the overlap types) with the property that
 *
 *	bitToType[i] and bitToType[j] combine to yield bitToType[i | j]
 *
 * Also (unless composeFlag is set) erasing bitToType[j] from bitToType[i] 
 * gives bitToType[i & (~j)],
 * i.e., it clears all of the j-type material out of the i-type material.
 * The bitToType[k] for which k's binary representation has only a single
 * bit set in it are the "primary" types.
 *
 * If composeFlag is set, the above is modified slightly to be analagous
 * to compose rules, specifically, erase rules for nonprimary types are
 * the default rules, i.e. they only erase precisely themselves.  This
 * makes ":erase *-primary" work in the expected way.
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
dbTechBitTypeInit(bitToType, n, pNum, composeFlag)
    TileType *bitToType;
    int n, pNum;
    bool composeFlag;
{
    int i, j;
    TileType have, type;

    for (i = 0; i < n; i++)
    {
	have = bitToType[i];
	for (j = 0; j < n; j++)
	{
	    type = bitToType[j];
	    dbSetPaintEntry(have, type, pNum, bitToType[i | j]);
	    if(!composeFlag || dbIsPrimary(j))
	    {
	        dbSetEraseEntry(have, type, pNum, bitToType[i & (~j)]);
	    }
	}
    }
}

/* Returns nonzero if exactly one bit set */ 

int
dbIsPrimary(n)
    int n;
{
    int bitCount;

    for(bitCount=0; n>0; n=n>>1)
    {
        if(n&1)
        {
	    bitCount++;
        }
    }

    return (bitCount==1);
}
      

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechAddCompose --
 *
 * Process a single compose/erase rule.  If the type being described is
 * a contact, save the rule and defer processing it until the end of this
 * section, because we need to know the behavior of all non-contact types
 * that might be residues before processing a contact composition rule.
 * Rules for non-contact types are processed here.
 *
 * Results:
 *	TRUE if successful, FALSE on error.
 *
 * Side effects:
 *	Modifies the paint/erase tables if the type being described
 *	is not a contact; otherwise, appends a rule to the list of
 *	contact erase/compose rules for later processing.  Marks the
 *	paint/erase table entries affected to show that they contain
 *	user-specified rules instead of the default ones, so we don't
 *	override them later.
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
bool
DBTechAddCompose(sectionName, argc, argv)
    char *sectionName;
    int argc;
    char *argv[];
{
    TileType type, r, s;
    int pNum, ruleType, i;
    static char *ruleNames[] =
	{ "compose", "decompose", "paint", "erase", 0 };
    static int ruleTypes[] =
	{ RULE_COMPOSE, RULE_DECOMPOSE, RULE_PAINT, RULE_ERASE };

    if (argc < 4)
    {
	TechError("Line must contain at least ruletype, result + pair\n");
	return FALSE;
    }

    /* Look up and skip over type of rule */
    i = Lookup(*argv, ruleNames);
    if (i < 0)
    {
	TechError("%s rule type %s.  Must be one of:\n\t",
		i == -1 ? "Ambiguous" : "Unknown", *argv);
	for (i = 0; ruleNames[i]; i++)
	    TxError("\"%s\" ", ruleNames[i]);
	TxError("\n");
	return FALSE;
    }
    ruleType = ruleTypes[i];
    argv++, argc--;

    /* Paint or erase rules are processed specially */
    switch (ruleType)
    {
	case RULE_PAINT:
	case RULE_ERASE:
	    return (dbTechAddPaintErase(ruleType, sectionName, argc, argv));
    }

    /* Compose or decompose rule: find result type and then skip over it */
    if ((type = DBTechNoisyNameType(*argv)) < 0)
	return FALSE;
    argv++, argc--;
    if (argc & 01)
    {
	TechError("Types on RHS of rule must be in pairs\n");
	return FALSE;
    }

    /* Compose/decompose rules for contacts are saved away */
    if (IsContact(type))
	return dbTechSaveCompose(ruleType, type, argc, argv);

    /* Rules for non-contacts are processed here */
    for ( ; argc > 0; argc -= 2, argv += 2)
    {
	if ((r = DBTechNoisyNameType(argv[0])) < 0
		|| (s = DBTechNoisyNameType(argv[1])) < 0)
	    return FALSE;

	if (IsContact(r) || IsContact(s))
	{
	    TechError("Can't have contact layers on RHS of non-contact rule\n");
	    return FALSE;
	}

	pNum = DBPlane(r);
	switch (ruleType)
	{
	    case RULE_COMPOSE:
		dbSetPaintEntry(r, s, pNum, type);
		dbSetPaintEntry(s, r, pNum, type);
		TTMaskSetType(&dbNotDefaultPaintTbl[r], s);
		TTMaskSetType(&dbNotDefaultPaintTbl[s], r);
		/* Fall through to */
	    case RULE_DECOMPOSE:
		dbSetPaintEntry(type, r, pNum, type);
		dbSetPaintEntry(type, s, pNum, type);
		dbSetEraseEntry(type, r, pNum, s);
		dbSetEraseEntry(type, s, pNum, r);
		TTMaskSetType(&dbNotDefaultPaintTbl[type], r);
		TTMaskSetType(&dbNotDefaultPaintTbl[type], s);
		TTMaskSetType(&dbNotDefaultEraseTbl[type], r);
		TTMaskSetType(&dbNotDefaultEraseTbl[type], s);
		break;
	}
    }

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTechSaveCompose --
 *
 * Save a compose rule for a contact 't' in the table dbSavedRules.
 * Check to make sure the rule is legal.
 *
 * Results:
 *	Returns TRUE if successful, FALSE on error.
 *
 * Side effects:
 *	Updates dbSavedRules[] and increments dbNumSavedRules.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbTechSaveCompose(ruleType, t, argc, argv)
    int ruleType;
    TileType t;
    int argc;
    char *argv[];
{
    TileType r, s;
    Rule *rp;

    rp = &dbSavedRules[dbNumSavedRules++];
    rp->r_ruleType = ruleType;
    rp->r_result = t;
    rp->r_npairs = 0;
    for ( ; argc > 0; argc -= 2, argv += 2)
    {
	r = DBTechNoisyNameType(argv[0]);
	s = DBTechNoisyNameType(argv[1]);
	if (r < 0 || s < 0)
	    return FALSE;

	/* At most one of r and s may be a contact */
	if (IsContact(r) && IsContact(s))
	{
	    TechError("Only one type in each pair may be a contact\n");
	    return FALSE;
	}

	/*
	 * The planes comprising 't' must be a superset of the
	 * planes comprising 'r' and the planes comprising 's'.
	 */
	if (((LayerPlaneMask(r) | LayerPlaneMask(s)) & ~LayerPlaneMask(t)) != 0)
	{
	    TechError("Component planes are a superset of result planes\n");
	    return FALSE;
	}

	if (ruleType == RULE_COMPOSE)
	{
	    /* Types r and s can't appear on planes outside of t's */
	    if ((LayerPlaneMask(r) | LayerPlaneMask(s)) != LayerPlaneMask(t))
	    {
		TechError("Union of pair planes must = result planes\n");
		return FALSE;
	    }

	    /* The following restriction has been lifted due to	*/
	    /* the recursive plane painting method added to	*/
	    /* routine DBPaint().	(Tim, 5/11/04)		*/

//	    if (!dbTechCheckImages(t, r, s) || !dbTechCheckImages(t, s, r))
//		return FALSE;
	}

	rp->r_pairs[rp->r_npairs].rp_a = r;
	rp->r_pairs[rp->r_npairs].rp_b = s;
	rp->r_npairs++;
    }

    return TRUE;
}


#if 0	/* deprecated function (5/11/04) */
/*
 * ----------------------------------------------------------------------------
 *
 * dbTechCheckImages --
 *
 * When processing a compose rule for 't' with RHS components
 * 'r' and 's', check to be sure that the images of 't' on
 * those planes present in 'r' but not in 's' are identical to
 * the images of 'r' on those planes.  This is necessary in order
 * that the result on these planes not depend on types present on
 * other planes.
 *
 * Results:
 *	Returns TRUE if successful, FALSE on error.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbTechCheckImages(t, r, s)
    TileType t;			/* Type that is composed */
    TileType r;	/* First constituent */
    TileType s;			/* Second constituent */
{
    int pNum;
    PlaneMask pMask;

    if (pMask = (LayerPlaneMask(r) & ~LayerPlaneMask(s)))
    {
	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pMask, pNum) && (t != r))
	    {
		TechError("Result image on plane %s must be the same "
			"as image of %s on plane %s\n",
			DBPlaneLongName(pNum),
			DBTypeLongName(r),
			DBPlaneLongName(pNum));
		return FALSE;
	    }
    }

    return TRUE;
}
#endif	/* 0 */


/*
 * ----------------------------------------------------------------------------
 *
 * dbTechAddPaintErase --
 *
 * Add a new entry to the paint or erase table.
 * The semantics is that painting a tile of type1 with paint type2
 * yields a tile of typeres.
 *
 * Results:
 *	Returns TRUE if successful, FALSE on error.
 *
 * Side effects:
 *	Updates the database technology variables.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbTechAddPaintErase(type, sectionName, argc, argv)
    int type;
    char *sectionName;
    int argc;
    char *argv[];
{
    int pNum;
    PlaneMask pMask, rMask;
    TileType t1, t2, tres;

    if (argc < 3)
    {
	TechError("Line must contain at least 3 types\n");
	return FALSE;
    }

    if ((t1 = DBTechNoisyNameType(argv[0])) < 0) return FALSE;
    if ((t2 = DBTechNoisyNameType(argv[1])) < 0) return FALSE;
    if ((tres = DBTechNoisyNameType(argv[2])) < 0) return FALSE;
    if (argc == 3)
    {
	if (t1 == TT_SPACE)
	{
	    TechError("<%s, %s, %s>:\n"
			"Must specify plane in paint table for "
			"painting space\n",
			argv[0], argv[1], argv[2]);
	    return FALSE;
	}
	else
	   pMask = LayerPlaneMask(t1);
    }
    else
    {
	if ((pNum = DBTechNoisyNamePlane(argv[3])) < 0) return FALSE;
	else
	    pMask = PlaneNumToMaskBit(pNum);
    }
    rMask = LayerPlaneMask(tres);

    /* (The plane mask of "tres" may be a subset of t1, but NOT vice	*/
    /* versa. Otherwise we will end up creating rules that are not	*/
    /* dependent on t1 in some planes.)					*/

    /* This restriction lifted 5/11/04 with code in DBPaint() that	*/
    /* recursively paints image types onto planes where they may be	*/
    /* missed by the inability to generate the correct paint table for	*/
    /* the operation.							*/

//  if (rMask & ~pMask)
//  {
//	TechError("Planes of result type must be a subset of those of have-type\n");
//	return FALSE;
//  }

    pMask &= ~rMask;

    if (type == RULE_PAINT)
    {
	/* Apply to all planes of rMask. */

	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(rMask, pNum))
		dbSetPaintEntry(t1, t2, pNum, tres);

	/* For all planes of pMask which are not in rMask, result is space */


	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pMask, pNum))
		dbSetPaintEntry(t1, t2, pNum, TT_SPACE);
    }
    else	/* (type == RULE_ERASE) */
    {
	/* Apply to all planes of rMask. */

	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(rMask, pNum))
		dbSetEraseEntry(t1, t2, pNum, tres);

	/* For all planes of pMask which are not in rMask, result is space */


	for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    if (PlaneMaskHasPlane(pMask, pNum))
		dbSetEraseEntry(t1, t2, pNum, TT_SPACE);
    }
    TTMaskSetType(&dbNotDefaultPaintTbl[t1], t2);
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbTechCheckPaint --
 *
 * DEBUGGING.
 * Check painting and erasing rules to make sure that the result
 * type is legal for the plane being affected.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints stuff in the event of an error.
 *
 * ----------------------------------------------------------------------------
 */

void
dbTechCheckPaint(where)
    char *where;	/* If non-null, print this as header */
{
    TileType have, t, result;
    bool printedHeader = FALSE;

    for (have = TT_TECHDEPBASE; have < DBNumTypes; have++)
    {
	for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	{
	    result = DBStdPaintEntry(have, t, DBPlane(have));
	    if (result != TT_SPACE && DBPlane(result) != DBPlane(have))
	    {
		if (!printedHeader && where)
		    TxPrintf("\n%s:\n", where), printedHeader = TRUE;
		TxPrintf("%s + %s -> %s\n",
			DBTypeShortName(have), DBTypeShortName(t),
			DBTypeShortName(result));
	    }
	    result = DBStdEraseEntry(have, t, DBPlane(have));
	    if (result != TT_SPACE && DBPlane(result) != DBPlane(have))
	    {
		if (!printedHeader && where)
		    TxPrintf("\n%s:\n", where), printedHeader = TRUE;
		TxPrintf("%s - %s -> %s\n",
			DBTypeShortName(have), DBTypeShortName(t),
			DBTypeShortName(result));
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbTechPrintPaint --
 *
 * DEBUGGING.
 * Print painting and erasing rules.  If contactsOnly is TRUe, only
 * print those rules involving pairs of contact types.  The argument
 * "where" is printed as a header if it is non-NULL.  If doPaint is
 * TRUE, we print the paint rules, else we print the erase rules.
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
dbTechPrintPaint(where, doPaint, contactsOnly)
    char *where;	/* If non-null, print this as header */
    bool doPaint;	/* TRUE -> print paint tables, FALSE -> print erase */
    bool contactsOnly;
{
    TileType have, paint, erase, result;
    int plane;
    LayerInfo *lp;

    if (where)
	TxPrintf("\n%s:\n\n", where);

    if (doPaint)
    {
	TxPrintf("PAINTING RULES:\n");
	for (have = TT_TECHDEPBASE; have < DBNumTypes; have++)
	{
	    if (contactsOnly && !IsContact(have)) continue;
	    for (paint = TT_TECHDEPBASE; paint < DBNumTypes; paint++)
	    {
		if (contactsOnly && !IsContact(paint)) continue;
		for (plane = PL_TECHDEPBASE; plane < DBNumPlanes; plane++)
		{
		    lp = &dbLayerInfo[have];
		    if (!PlaneMaskHasPlane(lp->l_pmask, plane))
			continue;

		    result = DBStdPaintEntry(have, paint, plane);
		    if (result != have)
		    {
		        TxPrintf("%s ",
				DBTypeShortName(have));
			if (IsContact(have))
		            TxPrintf("(on %s) ",
					DBPlaneLongName(plane));
		        TxPrintf(" + %s -> %s\n",
				DBTypeShortName(paint),
				DBTypeShortName(result));
		    }
		}
	    }
	}
    }
    else
    {
	TxPrintf("ERASING RULES:\n");
	for (have = TT_TECHDEPBASE; have < DBNumTypes; have++)
	{
	    if (contactsOnly && !IsContact(have)) continue;
	    for (erase = TT_TECHDEPBASE; erase < DBNumTypes; erase++)
	    {
		if (contactsOnly && !IsContact(erase)) continue;
		for (plane = PL_TECHDEPBASE; plane < DBNumPlanes; plane++)
		{
		    lp = &dbLayerInfo[have];
		    if (!PlaneMaskHasPlane(lp->l_pmask, plane))
			continue;

		    result = DBStdEraseEntry(have, erase, plane);
		    if (result != have)
		    {
		        TxPrintf("%s ",
				DBTypeShortName(have));
			if (IsContact(have))
		            TxPrintf("(on %s) ",
					DBPlaneLongName(plane));
		        TxPrintf(" - %s -> %s\n",
				DBTypeShortName(erase),
				DBTypeShortName(result));
		    }
		}
	    }
	}
    }
}
