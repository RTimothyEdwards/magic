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
extern void plowEdgeRule(), plowWidthRule(), plowSpacingRule();
PlowRule *plowTechOptimizeRule();

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
		    freeMagic((char *) pCand);
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
 * PlowAfterTech ---
 *
 * Routine to run after the entire techfile has been processed (or reloaded),
 * or when the DRC rules have been rescaled, after an internal grid rescaling.
 * It derives all of the plow width and spacing rules from the DRC rules.
 *
 * ----------------------------------------------------------------------------
 */

void
PlowAfterTech()
{
    /* This remains to be done. . . */
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
	    if (pr = table[i][j])
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


