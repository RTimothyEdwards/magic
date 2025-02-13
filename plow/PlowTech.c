/*
 * PlowTech.c --
 *
 * Plowing.
 * Read the "drc" section of the technology file and construct the
 * design rules used by plowing.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowTech.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "plow/plowInt.h"
#include "drc/drc.h"

/* C99 compat */
#include "utils/tech.h"
#include "drc/drc.h"

/* Imports from DRC */
extern char *maskToPrint();

/* Rule tables */
PlowRule *plowSpacingRulesTbl[TT_MAXTYPES][TT_MAXTYPES];
PlowRule *plowWidthRulesTbl[TT_MAXTYPES][TT_MAXTYPES];

/* Special type masks */

TileTypeBitMask PlowContactTypes;	/* All types that are contacts */
TileTypeBitMask PlowCoveredTypes;	/* All types that can't be uncovered
					 * (i.e, have an edge slide past them)
					 */
TileTypeBitMask PlowDragTypes;		/* All types that drag along trailing
					 * minimum-width material when they
					 * move.
					 */
TileTypeBitMask PlowFixedTypes;		/* Fixed-width types (e.g, fets).
					 * Note that this is the one variable
					 * used outside the plow module (see
					 * select/selOps.c), for the "stretch"
					 * command.
					 */

/*
 * Entry [t] of the following table is the maximum distance associated
 * with any design rules in a bucket with type 't' on the LHS.
 */
int plowMaxDist[TT_MAXTYPES];

/* Forward declarations */
extern int plowEdgeRule(), plowWidthRule(), plowSpacingRule();
PlowRule *plowTechOptimizeRule();

/*
 * ----------------------------------------------------------------------------
 *
 * PlowInit() --
 *
 *
 * One-time-only initialization (clearing) of plow tables on startup.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowInit()
{
    int i, j;

    for (i = 0; i < TT_MAXTYPES; i++)
    {
	for (j = 0; j < TT_MAXTYPES; j++)
	{
	    plowWidthRulesTbl[i][j] = (PlowRule *)NULL;
	    plowSpacingRulesTbl[i][j] = (PlowRule *)NULL;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowDRCInit --
 *
 * Initialization before processing the "drc" section for plowing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears the rules table.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowDRCInit()
{
    register int i, j;
    register PlowRule *pr;

    /* Remove all old rules from the plowing rules table */
    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    {
		free_magic1_t mm1 = freeMagic1_init();
		for (pr = plowWidthRulesTbl[i][j]; pr; pr = pr->pr_next)
		    freeMagic1(&mm1, (char *)pr);
		freeMagic1_end(&mm1);
	    }

	    {
		free_magic1_t mm1 = freeMagic1_init();
		for (pr = plowSpacingRulesTbl[i][j]; pr; pr = pr->pr_next)
		    freeMagic1(&mm1, (char *)pr);
		freeMagic1_end(&mm1);
	    }

	    plowWidthRulesTbl[i][j] = NULL;
	    plowSpacingRulesTbl[i][j] = NULL;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowDRCLine --
 *
 * Process a single line from the "drc" section.
 *
 * Results:
 *	TRUE always.
 *
 * Side effects:
 *	Adds rules to our plowing rule tables.
 *
 * Organization:
 *	We select a procedure based on the first keyword (argv[0])
 *	and call it to do the work of implementing the rule.  Each
 *	such procedure is of the following form:
 *
 *	void
 *	proc(argc, argv)
 *	    int argc;
 *	    char *argv[];
 *	{
 *	}
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
bool
PlowDRCLine(sectionName, argc, argv)
    char *sectionName;		/* Unused */
    int argc;
    char *argv[];
{
    int which;
    static const struct
    {
	const char *rk_keyword;	/* Initial keyword */
	int	 rk_minargs;	/* Min # arguments */
	int	 rk_maxargs;	/* Max # arguments */
	int    (*rk_proc)();	/* Procedure implementing this keyword */
    } ruleKeys[] = {
	{"edge",	 8,	9,	plowEdgeRule},
	{"edge4way",	 8,	9,	plowEdgeRule},
	{"spacing",	 6,	6,	plowSpacingRule},
	{"width",	 4,	4,	plowWidthRule},
	{0}
    }, *rp;

    /*
     * Leave the job of printing error messages to the DRC tech file reader.
     * We only process a few of the various design-rule types here anyway.
     */
    which = LookupStruct(argv[0], (const LookupTable *) ruleKeys, sizeof ruleKeys[0]);
    if (which >= 0)
    {
	rp = &ruleKeys[which];
	if (argc >= rp->rk_minargs && argc <= rp->rk_maxargs)
	    (*rp->rk_proc)(argc, argv);
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowWidthRule --
 *
 * Process a width rule.
 * This is of the form:
 *
 *	width layers distance why
 * e.g, width poly,pmc 2 "poly width must be at least 2"
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the plowing width rule table.
 *
 * ----------------------------------------------------------------------------
 */

int
plowWidthRule(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int distance = atoi(argv[2]);
    TileTypeBitMask set, setC;
    PlaneMask ptest, pmask;
    register PlowRule *pr;
    register TileType i, j;
    int pNum;

    /*
     * All layers in a width rule must be on the same plane;
     * CoincidentPlanes() below maps contacts to their proper images.
     */
    ptest = DBTechNoisyNameMask(layers, &set);
    pmask = CoincidentPlanes(&set, ptest);

    if (pmask == 0)
	return 0;
    pNum = LowestMaskBit(pmask);

    TTMaskCom2(&setC, &set);
    TTMaskAndMask(&setC, &DBPlaneTypes[pNum]);

    /*
     * Must have types in 'set' for at least 'distance' to the right of
     * any edge between a type in '~set' and a type in 'set'.
     */
    for (i = 0; i < DBNumTypes; i++)
    {
	if (TTMaskHasType(&setC, i))
	{
	    for (j = 0; j < DBNumTypes; j++)
	    {
		if (DBTypesOnSamePlane(i, j) && TTMaskHasType(&set, j))
		{
		    pr = (PlowRule *)mallocMagic(sizeof(PlowRule));
		    pr->pr_dist = distance;
		    pr->pr_mod = 0;
		    pr->pr_ltypes = setC;
		    pr->pr_oktypes = set;
		    pr->pr_pNum = pNum;
		    pr->pr_flags = PR_WIDTH;
		    pr->pr_next = plowWidthRulesTbl[i][j];
		    plowWidthRulesTbl[i][j] = pr;
		}
	    }
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowSpacingRule --
 *
 * Process a spacing rule.
 * This is of the form:
 *
 *	spacing layers1 layers2 distance adjacency why
 * e.g, spacing metal,pmc/m,dmc/m metal,pmc/m,dmc/m 4 touching_ok \
 *		"metal spacing must be at least 4"
 *
 * Adjacency may be either "touching_ok" or "touching_illegal".  In
 * the former case, no violation occurs when types in layers1 are
 * immediately adjacent to types in layers2.  In the second case,
 * such adjacency causes a violation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the plowing spacing rules.
 *
 * ----------------------------------------------------------------------------
 */

int
plowSpacingRule(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2 = argv[2];
    int distance = atoi(argv[3]);
    char *adjacency = argv[4];
    TileTypeBitMask set1, set2, tmp1, tmp2, setR, setRreverse;
    int pNum;
    PlaneMask ptest, planes1, planes2;
    register PlowRule *pr;
    register TileType i, j;

    ptest = DBTechNoisyNameMask(layers1, &set1);
    planes1 = CoincidentPlanes(&set1, ptest);
    ptest = DBTechNoisyNameMask(layers2, &set2);
    planes2 = CoincidentPlanes(&set2, ptest);

    if (planes1 == 0 || planes2 == 0)
	return 0;

    if (strcmp (adjacency, "touching_ok") == 0)
    {
	/* If touching is OK, everything must fall in the same plane. */
	if (planes1 != planes2)
	    return 0;
	pNum = LowestMaskBit(planes1);

	/*
	 * Must not have 'set2' for 'distance' to the right of an edge between
	 * 'set1' and the types in neither 'set1' nor 'set2' (ie, 'setR').
	 */
	tmp1 = set1;
	tmp2 = set2;
	planes1 = planes2 = PlaneNumToMaskBit(pNum);
	TTMaskCom(&tmp1);
	TTMaskCom(&tmp2);
	TTMaskAndMask(&tmp1, &tmp2);
	TTMaskAndMask(&tmp2, &DBPlaneTypes[pNum]);
	setRreverse = setR = tmp1;
    }
    else if (strcmp (adjacency, "touching_illegal") == 0)
    {
	/*
	 * Must not have 'set2' for 'distance' to the right of an edge between
	 * 'set1' and the types not in 'set1' (ie, 'setR').
	 */
	TTMaskCom2(&setR, &set1);
	TTMaskCom2(&setRreverse, &set2);
    }
    else return 0;

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j || !DBTypesOnSamePlane(i, j)) continue;

	    /* LHS is an element of set1 and RHS is an element of setR */
	    if (TTMaskHasType(&set1, i) && TTMaskHasType(&setR, j))
	    {
		/* May have to insert several buckets on different planes */
		for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		{
		    if (!PlaneMaskHasPlane(planes2, pNum))
			continue;
		    pr = (PlowRule *)mallocMagic(sizeof(PlowRule));
		    TTMaskClearMask3(&tmp1, &DBPlaneTypes[pNum], &set2);
		    TTMaskCom2(&tmp2, &setRreverse);
		    TTMaskAndMask3(&pr->pr_ltypes, &DBPlaneTypes[pNum], &tmp2);
		    pr->pr_oktypes = tmp1;
		    pr->pr_dist = distance;
		    pr->pr_mod = 0;
		    pr->pr_pNum = pNum;
		    pr->pr_flags = 0;
		    pr->pr_next = plowSpacingRulesTbl[i][j];
		    plowSpacingRulesTbl[i][j] = pr;
		}
	    }

	    /* Also apply backwards, unless it would create duplicates */
	    if (TTMaskEqual(&set1, &set2)) continue;

	    /* LHS is an element of set2, RHS is an element of setRreverse */
	    if (TTMaskHasType(&set2, i) && TTMaskHasType(&setRreverse, j))
	    {
		/* May have to insert several buckets on different planes */
		for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		{
		    if (!PlaneMaskHasPlane(planes1, pNum)) continue;
		    pr = (PlowRule *)mallocMagic(sizeof(PlowRule));
		    TTMaskClearMask3(&tmp1, &DBPlaneTypes[pNum], &set1);
		    TTMaskCom2(&tmp2, &setRreverse);
		    TTMaskAndMask3(&pr->pr_ltypes, &DBPlaneTypes[pNum], &tmp2);
		    pr->pr_oktypes = tmp1;
		    pr->pr_dist = distance;
		    pr->pr_mod = 0;
		    pr->pr_pNum = pNum;
		    pr->pr_flags = 0;
		    pr->pr_next = plowSpacingRulesTbl[i][j];
		    plowSpacingRulesTbl[i][j] = pr;
		}
	    }
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowEdgeRule --
 *
 * Process a primitive edge rule.
 * This is of the form:
 *
 *	edge layers1 layers2 dist OKtypes cornerTypes cornerDist why [plane]
 * or	edge4way layers1 layers2 dist OKtypes cornerTypes cornerDist why [plane]
 * e.g, edge poly,pmc s 1 diff poly,pmc "poly-diff separation must be 2"
 *
 * An "edge" rule is applied only down and to the left.
 * An "edge4way" rule is applied in all four directions.
 *
 * For plowing, we consider edge rules to be spacing rules.
 * Ordinary "edge" rules can be handled exactly (taking the distance
 * to be the maximum of dist and cornerDist above), because they are
 * always applied in the proper direction.  Each edge rule produces
 * one normal spacing rule, and possibly an additional spacing rule
 * that is only applied in the penumbra (if cornerTypes and layers2
 * are different).
 *
 * An "edge4way" rule also requires a conservative approximation to
 * handle the case when it is being applied in the opposite direction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the plowing spacing rules.
 *
 * ----------------------------------------------------------------------------
 */

int
plowEdgeRule(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2 = argv[2];
    int distance = atoi(argv[3]);
    char *okTypes = argv[4], *cornerTypes = argv[5];
    int cdist = atoi(argv[6]);
    TileTypeBitMask set1, set2, setC, setM;
    TileTypeBitMask setOK, setLeft, setRight;
    int pNum, checkPlane, flags;
    PlaneMask ptest, planes1, planes2, pmask;
    bool needPenumbraOnly;
    bool isFourWay = (strcmp(argv[0], "edge4way") == 0);
    register PlowRule *pr;
    register TileType i, j;

    ptest = DBTechNoisyNameMask(layers1, &set1);
    planes1 = CoincidentPlanes(&set1, ptest);
    ptest = DBTechNoisyNameMask(layers2, &set2);
    planes2 = CoincidentPlanes(&set2, ptest);
    distance = MAX(distance, cdist);

    /* Make sure that all edges between the two sets exist on one plane */
    if (planes1 == 0 || planes2 == 0)
	return 0;
    if (planes1 != planes2)
	return 0;

    ptest = DBTechNoisyNameMask(cornerTypes, &setC);
    pmask = CoincidentPlanes(&setC, ptest);
    if (pmask == 0)
	return 0;

    pNum = LowestMaskBit(pmask);

    /* If an explicit check plane was specified, use it */
    checkPlane = pNum;
    if (argc == 9)
    {
	checkPlane = DBTechNamePlane(argv[8]);
	if (checkPlane < 0)
	    return 0;
    }

    /* Get the images of everything in okTypes on the check plane */
    ptest = DBTechNoisyNameMask(okTypes, &setM);
    pmask = CoincidentPlanes(&setM, ptest);
    if (pmask == 0)
	return 0;

    needPenumbraOnly = !TTMaskEqual(&set2, &setC);
    TTMaskCom2(&setLeft, &setC);
    TTMaskAndMask(&setLeft, &DBPlaneTypes[pNum]);
    TTMaskCom2(&setRight, &set2);
    TTMaskAndMask(&setRight, &DBPlaneTypes[pNum]);
    flags = isFourWay ? PR_EDGE4WAY : PR_EDGE;
    for (i = 0; i < DBNumTypes; i++)
    {
	if (TTMaskHasType(&set1, i))
	{
	    for (j = 0; j < DBNumTypes; j++)
	    {
		if (TTMaskHasType(&set2, j))
		{
		    pr = (PlowRule *)mallocMagic(sizeof(PlowRule));
		    pr->pr_ltypes = setLeft;
		    pr->pr_oktypes = setM;
		    pr->pr_dist = distance;
		    pr->pr_mod = 0;
		    pr->pr_pNum = checkPlane;
		    pr->pr_next = plowSpacingRulesTbl[i][j];
		    pr->pr_flags = flags;
		    plowSpacingRulesTbl[i][j] = pr;
		}

		if (needPenumbraOnly && TTMaskHasType(&setC, j))
		{
		    pr = (PlowRule *)mallocMagic(sizeof(PlowRule));
		    pr->pr_ltypes = setRight;
		    pr->pr_oktypes = setM;
		    pr->pr_dist = distance;
		    pr->pr_mod = 0;
		    pr->pr_pNum = checkPlane;
		    pr->pr_next = plowSpacingRulesTbl[i][j];
		    pr->pr_flags = flags|PR_PENUMBRAONLY;
		    plowSpacingRulesTbl[i][j] = pr;
		}
	    }
	}
    }

    if (!isFourWay)
	return 0;

    /*
     * Four-way edge rules are applied by the design-rule checker
     * both forwards and backwards.  Since plowing can only look
     * forward, we need to approximate the backward rules with
     * a collection of forward rules.
     *
     * Suppose we have the following 4-way rule:
     *
     *   CORNER
     *	--------+
     *	  LEFT	|  RIGHT : OKTypes
     *
     * To check it in the following (backward) configuration, using
     * only rightward-looking rules,
     *
     *	       OKTypes : RIGHT	|  LEFT
     *				+--------
     *				  CORNER
     *
     * we generate the following forward rules (with the same distance):
     *
     *    ~t
     *	--------+
     *	   t	|  ~t : ~LEFT
     *
     * for each t in ~OKTypes.  In plowing terms, each rule will have LTYPES of
     * t and OKTYPES of ~LEFT.  In effect, this is creating a forward spacing
     * rule between each of the types ~OKTypes, and the materials in LEFT.
     * The edge is found on checkPlane, and checked on plane pNum.
     *
     * Because the corner and right-hand types for these rules are the same,
     * we don't need to generate any PR_PENUMBRAONLY rules.
     */

    setRight = setM;
    TTMaskCom2(&setLeft, &setM);
    TTMaskAndMask(&setLeft, &DBPlaneTypes[checkPlane]);
    TTMaskCom2(&setOK, &set1);
    TTMaskAndMask(&setOK, &DBPlaneTypes[pNum]);
    for (i = 0; i < DBNumTypes; i++)
    {
	if (TTMaskHasType(&setLeft, i))
	{
	    for (j = 0; j < DBNumTypes; j++)
	    {
		if (TTMaskHasType(&setRight, j))
		{
		    pr = (PlowRule *)mallocMagic(sizeof(PlowRule));
		    TTMaskSetOnlyType(&pr->pr_ltypes, i);
		    pr->pr_oktypes = setOK;
		    pr->pr_dist = distance;
		    pr->pr_mod = 0;
		    pr->pr_pNum = pNum;
		    pr->pr_flags = flags|PR_EDGEBACK;
		    pr->pr_next = plowSpacingRulesTbl[i][j];
		    plowSpacingRulesTbl[i][j] = pr;
		}
	    }
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowDRCFinal --
 *
 * Called after all lines of the drc section in the technology file have been
 * read.  The preliminary plowing rules tables are pruned by removing rules
 * covered by other (longer distance) rules.
 *
 * We also construct plowMaxDist[] to contain for entry 't' the maximum
 * distance associated with any plowing rule in a bucket with 't' on its
 * LHS.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May remove PlowRules from the linked lists of the width and
 *	spacing rules tables.  Sets the values in plowMaxDist[].
 *
 * ----------------------------------------------------------------------------
 */

void
PlowDRCFinal()
{
    register PlowRule *pr;
    register TileType i, j;

    for (i = 0; i < DBNumTypes; i++)
    {
	plowMaxDist[i] = 0;
	for (j = 0; j < DBNumTypes; j++)
	{
	    if ((pr = plowWidthRulesTbl[i][j]))
	    {
		pr = plowWidthRulesTbl[i][j] = plowTechOptimizeRule(pr);
		for ( ; pr; pr = pr->pr_next)
		    if (pr->pr_dist > plowMaxDist[i])
			plowMaxDist[i] = pr->pr_dist;
	    }
	    if ((pr = plowSpacingRulesTbl[i][j]))
	    {
		pr = plowSpacingRulesTbl[i][j] = plowTechOptimizeRule(pr);
		for ( ; pr; pr = pr->pr_next)
		    if (pr->pr_dist > plowMaxDist[i])
			plowMaxDist[i] = pr->pr_dist;
	    }
	}
    }
}
/*
 * ----------------------------------------------------------------------------
 *
 * plowTechOptimizeRule --
 *
 * Called to optimize the chain of rules in a single bin of either
 * the spacing or width rules tables.
 *
 * In general, we want to remove any rule A "covered" by another
 * rule B, i.e.,
 *
 *	B's distance >= A's distance,
 *	B's OKTypes is a subset of A's OKTypes, and
 *	B's Ltypes == A's Ltypes
 *	B's check plane == A's check plane
 *
 * Results:
 *	Returns a pointer to the new chain of rules for this bin.
 *
 * Side effects:
 *	May deallocate memory.
 *
 * ----------------------------------------------------------------------------
 */

PlowRule *
plowTechOptimizeRule(ruleList)
    PlowRule *ruleList;
{
    PlowRule *pCand, *pCandLast, *pr;
    TileTypeBitMask tmpMask;

    /*
     * The pointer 'pCand' in the following loop will iterate over
     * candidates for deletion, and pCandLast will trail by one.
     */
    pCand = ruleList;
    pCandLast = (PlowRule *) NULL;
    free_magic1_t mm1 = freeMagic1_init();
    while (pCand)
    {
	for (pr = ruleList; pr; pr = pr->pr_next)
	{
	    if (pr != pCand
		    && pr->pr_dist >= pCand->pr_dist
		    && pr->pr_flags == pCand->pr_flags
		    && pr->pr_pNum == pCand->pr_pNum
		    && TTMaskEqual(&pr->pr_ltypes, &pCand->pr_ltypes))
	    {
		/*
		 * Is pr->pr_oktypes a subset of pCand->pr_oktypes,
		 * i.e, is it more restrictive?
		 */
		TTMaskAndMask3(&tmpMask, &pr->pr_oktypes, &pCand->pr_oktypes);
		if (TTMaskEqual(&tmpMask, &pr->pr_oktypes))
		{
		    /*
		     * Delete pCand, and resume outer loop with the
		     * new values of pCand and pCandLast set below.
		     */
		    freeMagic1(&mm1, (char *) pCand);
		    if (pCandLast)
			pCandLast->pr_next = pCand->pr_next;
		    else
			ruleList = pCand->pr_next;
		    pCand = pCand->pr_next;
		    goto next;
		}
	    }
	}

	/* Normal continuation: advance to next rule in bin */
	pCandLast = pCand, pCand = pCand->pr_next;

next:	;
    }
    freeMagic1_end(&mm1);

    return (ruleList);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowTechInit --
 *
 * Initialize the masks of types PlowFixedTypes, PlowCoveredTypes,
 * and PlowDragTypes to be empty.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Zeroes PlowFixedTypes, PlowCoveredTypes, and PlowDragTypes.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowTechInit()
{
    register TileType i, j;
    PlowRule *pr;

    PlowFixedTypes = DBZeroTypeBits;
    PlowCoveredTypes = DBZeroTypeBits;
    PlowDragTypes = DBZeroTypeBits;

}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowTechLine --
 *
 * Process a line from the plowing section of a technology file.
 * Such a line is currently of the following form:
 *
 *	keyword types
 *
 * where 'types' is a comma-separated list of type names.
 *
 * Keywords:
 *
 *	fixed		each of 'types' is fixed-width; regions consisting
 *			of fixed-width types are not deformed by plowing.
 *			Contacts are automatically fixed size and so do not
 *			need to be included in this list.  Space can never
 *			be fixed-size and so is automatically omitted from
 *			the list.
 *
 *	covered		each of 'types' cannot be uncovered as a result of
 *			plowing.  This means that if material of type X
 *			covers a horizontal edge initially, it will continue
 *			to cover it after plowing.
 *
 *	drag		each of 'types' will drag along with it the LHS of
 *			any trailing minimum-width material when it moves.
 *			This is so transistors will drag their gates when
 *			the transistor moves, so we don't leave huge amounts
 *			of poly behind us.
 *
 * Results:
 *	Returns TRUE always.
 *
 * Side effects:
 *	Updates PlowFixedTypes, PlowCoveredTypes, and PlowDragTypes.
 *
 * ----------------------------------------------------------------------------
 */

bool
PlowTechLine(sectionName, argc, argv)
    char *sectionName;	/* Unused */
    int argc;
    char *argv[];
{
    TileTypeBitMask types;

    if (argc != 2)
    {
	TechError("Malformed line\n");
	return (TRUE);
    }

    DBTechNoisyNameMask(argv[1], &types);

    TTMaskAndMask(&types, &DBAllButSpaceBits);

    if (strcmp(argv[0], "fixed") == 0)
    {
	TTMaskSetMask(&PlowFixedTypes, &types);
    }
    else if (strcmp(argv[0], "covered") == 0)
    {
	TTMaskSetMask(&PlowCoveredTypes, &types);
    }
    else if (strcmp(argv[0], "drag") == 0)
    {
	TTMaskSetMask(&PlowDragTypes, &types);
    }
    else
    {
	TechError("Illegal keyword \"%s\".\n", argv[0]);
    }
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlowTechFinal --
 *
 * Final processing of the lines from the plowing section of a technology
 * file.  Add all contacts to the list of fixed types.  Also sets the mask
 * PlowContactTypes to have bits set for each image of each contact.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates PlowFixedTypes and PlowContactTypes.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowTechFinal()
{
    TileType t;

    TTMaskZero(&PlowContactTypes);
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	if (DBIsContact(t))
	    TTMaskSetType(&PlowContactTypes, t);

    TTMaskSetMask(&PlowFixedTypes, &PlowContactTypes);
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowScaleUp ---
 *
 * Scale all plow distances according to the current DRC scale factor.
 * ----------------------------------------------------------------------------
 */

void plowScaleUp(PlowRule *pr, int scalefactor)
{
    int dist;

    if (pr->pr_dist > 0)
    {
	dist = pr->pr_dist;
	if (pr->pr_mod != 0)
	    pr->pr_dist--;
	pr->pr_dist *= scalefactor;
	pr->pr_dist += (short)pr->pr_mod;
	pr->pr_mod = 0;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * plowScaleDown ---
 *
 * Scale all plow distances according to the current DRC scale factor.
 * ----------------------------------------------------------------------------
 */

void plowScaleDown(PlowRule *pr, int scalefactor)
{
    int dist;

    if (pr->pr_dist > 0)
    {
	dist = pr->pr_dist;
	pr->pr_dist /= scalefactor;
	if ((pr->pr_mod = (unsigned char)(dist % scalefactor)) != 0)
	    pr->pr_dist++;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCPlowScale ---
 *
 * Routine to run after the entire techfile has been processed (or reloaded),
 * or when the DRC rules have been rescaled, after an internal grid rescaling.
 * It derives all of the plow width and spacing rules from the DRC rules.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCPlowScale(int scaled, int scalen, bool adjustmax)
{
    PlowRule *pr;
    TileType i, j;

    for (i = 0; i < TT_MAXTYPES; i++)
    {
	for (j = 0; j < TT_MAXTYPES; j++)
	{
	    for (pr = plowWidthRulesTbl[i][j]; pr; pr = pr->pr_next)
	    {
		plowScaleUp(pr, scaled);
		plowScaleDown(pr, scalen);
	    }

	    for (pr = plowSpacingRulesTbl[i][j]; pr; pr = pr->pr_next)
	    {
		plowScaleUp(pr, scaled);
		plowScaleDown(pr, scalen);
	    }
	}

	/* Scale plowMaxDist */
	if (adjustmax)
	{
	    plowMaxDist[i] *= scaled;
	    plowMaxDist[i] /= scalen;
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * plowTechPrintRule --
 * plowTechShowTable --
 * plowTechShow --
 *
 * For debugging purposes.
 * Print the complete table of all plowing rules.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints to the file 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
plowTechPrintRule(pr, f)
    PlowRule *pr;
    FILE *f;
{
    fprintf(f, "\tDISTANCE=%d, PLANE=%s, FLAGS=",
		pr->pr_dist, DBPlaneLongName(pr->pr_pNum));
    if (pr->pr_flags & PR_WIDTH) fprintf(f, " Width");
    if (pr->pr_flags & PR_PENUMBRAONLY) fprintf(f, " PenumbraOnly");
    if (pr->pr_flags & PR_EDGE) fprintf(f, " Edge");
    if (pr->pr_flags & PR_EDGE4WAY) fprintf(f, " Edge4way");
    if (pr->pr_flags & PR_EDGEBACK) fprintf(f, " EdgeBack");
    fprintf(f, "\n");
    fprintf(f, "\tLTYPES = %s\n", maskToPrint(&pr->pr_ltypes));
    fprintf(f, "\tOKTYPES = %s\n", maskToPrint(&pr->pr_oktypes));
    fprintf(f, "\t-------------------------------\n");
}

void
plowTechShowTable(table, header, f)
    PlowRule *table[TT_MAXTYPES][TT_MAXTYPES];
    char *header;
    FILE *f;
{
    PlowRule *pr;
    TileType i, j;

    fprintf(f, "\n\n------------ %s ------------\n", header);
    for (i = 0; i < DBNumTypes; i++)
	for (j = 0; j < DBNumTypes; j++)
	    if ((pr = table[i][j]))
	    {
		fprintf(f, "\n%s -- %s:\n",
		    DBTypeLongName(i), DBTypeLongName(j));
		for ( ; pr; pr = pr->pr_next)
		    plowTechPrintRule(pr, f);
	    }
}

void
plowTechShow(f)
    FILE *f;
{
    plowTechShowTable(plowWidthRulesTbl, "Width Rules", f);
    plowTechShowTable(plowSpacingRulesTbl, "Spacing Rules", f);
}


