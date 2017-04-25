/*
 * DRCtech.c --
 *
 * Technology initialization for the DRC module.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCtech.c,v 1.12 2010/10/20 12:04:12 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "utils/tech.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "cif/cif.h"
#include "cif/CIFint.h"

CIFStyle *drcCifStyle = NULL;
bool	 DRCForceReload = FALSE;

/*
 * DRC interaction radius being used (not necessarily the same as
 * what's defined in the current DRC style).
 */
global int DRCTechHalo;
global int DRCStepSize;

/* The following variable can be set to zero to turn off
 * any optimizations of design rule lists.
 */

global int DRCRuleOptimization = TRUE;

/* The following variables count how many rules were specified by
 * the technology file and how many edge rules were optimized away.
 */

static int drcRulesSpecified = 0;
static int drcRulesOptimized = 0;

/*
 * Forward declarations.
 */
int drcWidth(), drcSpacing(), drcEdge(), drcNoOverlap();
int drcExactOverlap(), drcExtend();
int drcSurround(), drcRectOnly(), drcOverhang();
int drcStepSize();
int drcMaxwidth(), drcArea(), drcRectangle(), drcAngles();
int drcCifSetStyle(), drcCifWidth(), drcCifSpacing();
int drcCifMaxwidth(), drcCifArea();

void DRCTechStyleInit();
void drcLoadStyle();
void DRCTechFinal();
void drcTechFinalStyle();

/*
 * ----------------------------------------------------------------------------
 *
 * Given a TileType bit mask, return the plane mask of planes that are
 * coincident with all types.  This is roughly equivalent to the deprecated
 * DBTechMinSetPlanes(), but does not attempt to produce a reduced set of
 * tile types.  Since the collective mask of all possible planes is usually
 * found by a call to DBTechNoisyNameMask, we don't attempt to OR all the
 * plane masks together, but assume this collective mask is available and
 * passed as an argument.
 *
 * ----------------------------------------------------------------------------
 */

PlaneMask
CoincidentPlanes(typeMask, pmask)
    TileTypeBitMask *typeMask;		/* Mask of types to check coincidence */
    PlaneMask pmask;			/* Mask of all possible planes of types */
{
    PlaneMask planes = pmask;
    TileType i;

    /* AND each plane against the collective mask */
    for (i = TT_SELECTBASE; i < DBNumTypes; i++)
	if (TTMaskHasType(typeMask, i))
	    planes &= DBTypePlaneMaskTbl[i];

    return planes;
}
 

/*
 * ----------------------------------------------------------------------------
 *
 * Given a plane mask, return the plane number of the lowest plane in the mask.
 *
 * ----------------------------------------------------------------------------
 */

int
LowestMaskBit(PlaneMask pmask)
{
   PlaneMask pset = pmask;
   int plane = 0;

   if (pmask == 0) return DBNumPlanes;

   while ((pset & 0x1) == 0)
   {
	plane++;
	pset >>= 1;
   }
   return plane;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCPrintStyle --
 *
 *	Print the available and/or current extraction styles.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCPrintStyle(dolist, doforall, docurrent)
    bool dolist, doforall, docurrent;
{
    DRCKeep *style;

    if (docurrent)
    {
	if (DRCCurStyle == NULL)
	    TxError("Error: No style is set\n");
	else
	{
	    if (!dolist) TxPrintf("The current style is \"");
#ifdef MAGIC_WRAPPER
	    if (dolist)
	        Tcl_SetResult(magicinterp, DRCCurStyle->ds_name, NULL);
	    else
#endif
	    TxPrintf("%s", DRCCurStyle->ds_name);
	    if (!dolist) TxPrintf("\".\n");
	}
    }

    if (doforall)
    {
	if (!dolist) TxPrintf("The DRC styles are: ");

	for (style = DRCStyleList; style; style = style->ds_next)
	{
	    if (dolist)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_AppendElement(magicinterp, style->ds_name);
#else
		if (style != DRCStyleList) TxPrintf(" ");
		TxPrintf("%s", style->ds_name);
#endif
	    }
	    else
	    {
		if (style != DRCStyleList) TxPrintf(", ");
		TxPrintf("%s", style->ds_name);
	    }
	}
	if (!dolist) TxPrintf(".\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCSetStyle --
 *
 *	Set the current DRC style to 'name'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output.
 *
 * ----------------------------------------------------------------------------
 */

void
DRCSetStyle(name)
    char *name;
{
    DRCKeep *style, *match;
    int length;

    if (name == NULL) return;

    match = NULL;
    length = strlen(name);
    for (style = DRCStyleList; style; style = style->ds_next)
    {
	if (strncmp(name, style->ds_name, length) == 0)
	{
	    if (match != NULL)
	    {
		TxError("DRC style \"%s\" is ambiguous.\n", name);
		DRCPrintStyle(FALSE, TRUE, TRUE);
		return;
	    }
	    match = style;
	}
    }

    if (match != NULL)
    {
	drcLoadStyle(match->ds_name);
	TxPrintf("DRC style is now \"%s\"\n", name);
	return;
    }

    TxError("\"%s\" is not one of the DRC styles Magic knows.\n", name);
    DRCPrintStyle(FALSE, TRUE, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcTechFreeStyle --
 *
 *	This procedure frees all memory associated with a DRC style.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory free'd.
 *
 * ----------------------------------------------------------------------------
 */

void
drcTechFreeStyle()
{
    int i, j;
    char *old;
    DRCCookie *dp;

    if (DRCCurStyle != NULL)
    {
	/* Remove all old rules from the DRC rules table */

	for (i = 0; i < TT_MAXTYPES; i++)
	    for (j = 0; j < TT_MAXTYPES; j++)
	    {
		dp = DRCCurStyle->DRCRulesTbl[i][j];
		while (dp != NULL)
		{
		    char *old = (char *) dp;
		    dp = dp->drcc_next;
		    freeMagic(old);
		}
	    }

	/* Clear the DRCWhyList */

	while (DRCCurStyle->DRCWhyList != NULL)
	{
	    old = (char *) DRCCurStyle->DRCWhyList;
	    StrDup(&(DRCCurStyle->DRCWhyList->dwl_string), (char *) NULL);
	    DRCCurStyle->DRCWhyList = DRCCurStyle->DRCWhyList->dwl_next;
	    freeMagic(old);
	}

	freeMagic(DRCCurStyle);
	DRCCurStyle = NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcTechNewStyle --
 *
 *	This procedure creates a new DRC style at the end of the list
 *	of styles and initializes it to completely null.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new element is added to the end of DRCStyleList, and DRCCurStyle
 *	is set to point to it.
 *
 * ----------------------------------------------------------------------------
 */

void
drcTechNewStyle()
{
   drcTechFreeStyle();
   DRCTechStyleInit();
}

/*
 * ----------------------------------------------------------------------------
 * drcWhyDup --
 *
 * Duplicate a shared "why" string using StrDup() and remember it so we can
 * free it sometime later, in drcWhyClear().
 *
 * Returns:
 *	A copy of the given string.
 *
 * Side effects:
 *	Adds to the DRCWhyList.  Calls StrDup().
 * ----------------------------------------------------------------------------
 */

char *
drcWhyDup(why)
    char * why;
{
    struct drcwhylist * new;

    new = (struct drcwhylist *) mallocMagic((unsigned) (sizeof *new));
    new->dwl_string = StrDup((char **) NULL, why);
    new->dwl_next = DRCCurStyle->DRCWhyList;
    DRCCurStyle->DRCWhyList = new;

    return new->dwl_string;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcFindBucket --
 *
 *	Find the bucket preceding the point where we with to insert a new DRC
 *	cookie.  Don't insert a cookie in the middle of a pair of coupled
 *	(trigger + check) rules.
 *
 * Results:
 *	Returns a pointer to the location where we want to insert a rule
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

DRCCookie *
drcFindBucket(i, j, distance)
    int i, j, distance;
{
    DRCCookie *dp;

    if (DRCCurStyle == NULL) return NULL;

    /* find bucket preceding the new one we wish to insert */

    for (dp = DRCCurStyle->DRCRulesTbl[i][j];
		dp->drcc_next != (DRCCookie *) NULL;
		dp = dp->drcc_next)
    {
	if (dp->drcc_next->drcc_flags & DRC_TRIGGER)
	{
	    if (dp->drcc_next->drcc_next->drcc_dist >= distance)
		break;
	    else
		dp = dp->drcc_next;
	}
	else if (dp->drcc_next->drcc_dist >= distance) break;
    }

    return dp;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcLoadStyle --
 * 
 * Re-read the technology file to load the specified technology DRC style
 * into structure DRCCurStyle.  It incurs a complete reading of the tech
 * file on startup and every time the extraction style is changed, but we
 * can assume that this does not happen often.  The first style in the
 * technology file is assumed to be the default, so that re-reading the
 * tech file is not necessary on startup unless the default DRC style is
 * changed by a call do "drc style".
 *
 * ----------------------------------------------------------------------------
 */

void
drcLoadStyle(stylename)
    char *stylename;
{
    SectionID invdrc;

    if (DRCCurStyle->ds_name == stylename) return;

    drcTechNewStyle();		/* Reinitialize and mark as not loaded */
    DRCCurStyle->ds_name = stylename;

    invdrc = TechSectionGetMask("drc", NULL);
    TechLoad(NULL, invdrc);

    DRCTechScale(DBLambda[0], DBLambda[1]);
}

/*
 * ----------------------------------------------------------------------------
 * DRCReloadCurStyle ---
 *
 * This routine is used by CIFLoadStyle whenever the DRC section makes
 * reference to CIF layers.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	DRC rule database is deleted and regenerated.
 *	
 * ----------------------------------------------------------------------------
 */

void
DRCReloadCurStyle()
{
    char *stylename;
    DRCKeep * style;

    if (DRCCurStyle == NULL) return;

    for (style = DRCStyleList; style != NULL; style = style->ds_next)
    {
	if (!strcmp(style->ds_name, DRCCurStyle->ds_name))
	{
	    DRCCurStyle->ds_name = NULL;
	    drcLoadStyle(style->ds_name);
	    break;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 * DRCTechInit --
 *
 * Free and re-initialize the technology-specific variables for the DRC module.
 * This routine is *only* called upon a "tech load" command, when all existing
 * DRC data must be cleaned up and free'd.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out all the DRC tables.
 * ----------------------------------------------------------------------------
 */

void
DRCTechInit()
{
    DRCKeep *style;

    /* Clean up any old info */

    drcTechFreeStyle();

    for (style = DRCStyleList; style != NULL; style = style->ds_next)
    {
	freeMagic(style->ds_name);
	freeMagic(style);
    }
    DRCStyleList = NULL;
}

/*
 * ----------------------------------------------------------------------------
 * DRCTechStyleInit --
 *
 * Initialize the technology-specific variables for the DRC module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out all the DRC tables.
 * ----------------------------------------------------------------------------
 */

void
DRCTechStyleInit()
{
    int i, j, plane;
    DRCCookie *dp;
    PaintResultType result;

    drcRulesOptimized = 0;
    drcRulesSpecified = 0;

    if (DRCCurStyle == NULL)
    {
	DRCCurStyle = (DRCStyle *) mallocMagic(sizeof(DRCStyle));
	DRCCurStyle->ds_name = NULL;
    }

    DRCCurStyle->ds_status = TECH_NOT_LOADED;

    TTMaskZero(&DRCCurStyle->DRCExactOverlapTypes);
    DRCCurStyle->DRCWhyList = NULL;
    DRCCurStyle->DRCTechHalo = 0;
    DRCCurStyle->DRCScaleFactorN = 1;
    DRCCurStyle->DRCScaleFactorD = 1;
    DRCCurStyle->DRCStepSize = 0;

    DRCTechHalo = 0;

    /* Put a dummy rule at the beginning of the rules table for each entry */

    for (i = 0; i < TT_MAXTYPES; i++)
    {
	for (j = 0; j < TT_MAXTYPES; j++)
	{
	    dp = DRCCurStyle->DRCRulesTbl[i][j];
	    dp = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
	    dp->drcc_dist = -1;
	    dp->drcc_cdist = -1;
	    dp->drcc_next = (DRCCookie *) NULL;
	    TTMaskZero(&dp->drcc_mask);
	    DRCCurStyle->DRCRulesTbl[i][j] = dp;
	}
    }

    /* Copy the default paint table into the DRC paint table.  The DRC
     * paint table will be modified as we read the drc section.  Also
     * make sure that the error layer is super-persistent (once it
     * appears, it can't be gotten rid of by painting).  Also, make
     * some crossings automatically illegal:  two layers can't cross
     * unless the result of painting one on top of the other is to
     * get one of the layers, and it doesn't matter which is painted
     * on top of which.
     */
    
    for (plane = 0; plane < DBNumPlanes; plane++)
	for (i = 0; i < DBNumTypes; i++)
	    for (j = 0; j < DBNumTypes; j++)
	    {
		result = DBPaintResultTbl[plane][i][j];
		if ((i == TT_ERROR_S) || (j == TT_ERROR_S))
		    DRCCurStyle->DRCPaintTable[plane][i][j] = TT_ERROR_S;
		else if ((i == TT_SPACE) || (j == TT_SPACE)
			|| !DBTypeOnPlane(j, plane)
			|| !DBPaintOnTypePlanes(i, j))
		    DRCCurStyle->DRCPaintTable[plane][i][j] = result;

		/* Modified for stackable types (Tim, 10/3/03) */
		else if ((i >= DBNumUserLayers) ||
			((result >= DBNumUserLayers)
			&& (DBTechFindStacking(i, j) == result)))
		{
		    DRCCurStyle->DRCPaintTable[plane][i][j] = result;
		}
			
		else if ((!TTMaskHasType(&DBLayerTypeMaskTbl[i], result)
			&& !TTMaskHasType(&DBLayerTypeMaskTbl[j], result))
			|| ((result != DBPaintResultTbl[plane][j][i])
			&& DBTypeOnPlane(i, plane)
			&& DBPaintOnTypePlanes(j, i)))
		{
		    DRCCurStyle->DRCPaintTable[plane][i][j] = TT_ERROR_S;
		    /* TxError("Error: %s on %s, was %s\n",
				DBTypeLongNameTbl[i], DBTypeLongNameTbl[j],
				DBTypeLongNameTbl[result]); */
		}
		else
		    DRCCurStyle->DRCPaintTable[plane][i][j] = result;
	    }

    drcCifInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCTechLine --
 *
 * Parse a line in the DRC section from the technology file.  Handle DRC
 * styles.  The rules themselves are handled by DRCTechAddRule.
 *
 * Results:
 *      TRUE if line parsed correctly; FALSE if fatal error condition
 *      encountered.
 *
 * Side effects:
 *	Appends information to the DRC tables.
 *
 * ----------------------------------------------------------------------------
 */

bool
DRCTechLine(sectionName, argc, argv)
    char *sectionName;		/* The name of this section */
    int argc;			/* Number of fields on the line */
    char *argv[];		/* Values of the fields */
{
    int j, l;
    DRCKeep *newStyle, *p;
    char *tptr, *cptr;

    if (argc <= 0) return TRUE;
    else if (argc >= 2) l = strlen(argv[1]);

    if (strcmp(argv[0], "style") == 0)
    {
	if (argc != 2)
	{
	    if ((argc != 4) || (strncmp(argv[2], "variant", 7)))
	    {
		wrongNumArgs:
		TechError("Wrong number of arguments in %s statement.\n",
			argv[0]);
		return TRUE;
	    }
	}
	for (newStyle = DRCStyleList; newStyle != NULL;
		newStyle = newStyle->ds_next)
	{
	    /* Here we're only establishing existence;  break on
	     * the first variant found.
	     */
	    if (!strncmp(newStyle->ds_name, argv[1], l))
		break;
	}
	if (newStyle == NULL)
	{
	    if (argc == 2)
	    {
		newStyle = (DRCKeep *)mallocMagic(sizeof(DRCKeep));
		newStyle->ds_next = NULL;
		newStyle->ds_name = StrDup((char **) NULL, argv[1]);

		/* Append to end of style list */
		if (DRCStyleList == NULL)
		    DRCStyleList = newStyle;
		else
		{
		    for (p = DRCStyleList; p->ds_next; p = p->ds_next);
		    p->ds_next = newStyle;
		}
	    }
	    else	/* Handle style variants */
	    {
		DRCKeep *saveStyle = NULL;

		/* 4th argument is a comma-separated list of variants */

		tptr = argv[3];
		while (*tptr != '\0')
		{
		    cptr = strchr(tptr, ',');
		    if (cptr != NULL) *cptr = '\0';
		    newStyle = (DRCKeep *)mallocMagic(sizeof(DRCKeep));
		    newStyle->ds_next = NULL;
		    newStyle->ds_name = (char *)mallocMagic(l
				+ strlen(tptr) + 1);
		    sprintf(newStyle->ds_name, "%s%s", argv[1], tptr);

		    /* Remember the 1st variant as the default */
		    if (saveStyle == NULL) saveStyle = newStyle;
	
		    /* Append to end of style list */
		    if (DRCStyleList == NULL)
			DRCStyleList = newStyle;
		    else
		    {
			for (p = DRCStyleList; p->ds_next; p = p->ds_next);
			p->ds_next = newStyle;
		    }

		    if (cptr == NULL)
			break;
		    else
			tptr = cptr + 1;
		}
		newStyle = saveStyle;
	    }
	}

	if (DRCCurStyle == NULL)   /* Shouldn't happen, but be safe. . .*/
	{
	    drcTechNewStyle();
	    DRCCurStyle->ds_name = newStyle->ds_name;
	    DRCCurStyle->ds_status = TECH_PENDING;
	}
	else if ((DRCCurStyle->ds_status == TECH_PENDING) ||
			(DRCCurStyle->ds_status == TECH_SUSPENDED))
	    DRCCurStyle->ds_status = TECH_LOADED;
	else if (DRCCurStyle->ds_status == TECH_NOT_LOADED)
	{
	    if (DRCCurStyle->ds_name == NULL) {
		DRCCurStyle->ds_name = newStyle->ds_name;
		DRCCurStyle->ds_status = TECH_PENDING;
	    }
	    else if (argc == 2)
	    {
		if (!strcmp(argv[1], DRCCurStyle->ds_name))
		    DRCCurStyle->ds_status = TECH_PENDING;
	    }
	    else if (argc == 4)
	    {
		/* Verify that the style matches one variant */

		if (!strncmp(DRCCurStyle->ds_name, argv[1], l))
		{
		    tptr = argv[3];
		    while (*tptr != '\0')
		    {
			cptr = strchr(tptr, ',');
			if (cptr != NULL) *cptr = '\0';
			if (!strcmp(DRCCurStyle->ds_name + l, tptr))
			{
			    DRCCurStyle->ds_status = TECH_PENDING;
			    return TRUE;
			}
			if (cptr == NULL)
			    return TRUE;
			else
			    tptr = cptr + 1;
		    }
		}
	    }
	}
	return (TRUE);
    }

    if (DRCCurStyle == NULL)
	return FALSE;

    /* For backwards compatibility, if we have encountered a line that	*/
    /* is not "style" prior to setting a style, then we create a style	*/
    /* called "default".						*/

    if (DRCStyleList == NULL)
    {
	char *locargv[2][10] = {"style", "default"};
	
	if (DRCTechLine(sectionName, 2, locargv) == FALSE)
	    return FALSE;
    }
    else if (DRCStyleList->ds_next == NULL)
    {
	/* On reload, if there is only one style, use it.  This is	*/
	/* necessary for the DRC-CIF extensions, since even though	*/
	/* the one-and-only DRC section may have been loaded, the DRC-	*/
	/* CIF parts of it become enabled or disabled depending on what	*/
	/* CIFCurStyle is active.					*/

	DRCCurStyle->ds_status = TECH_PENDING;
    }

    /* Only continue past this point if we are loading the DRC style */

    if ((DRCCurStyle->ds_status != TECH_PENDING) &&
		(DRCCurStyle->ds_status != TECH_SUSPENDED))
	return TRUE;

    /* Process "scalefactor" line next (if any) */

    if (strcmp(argv[0], "scalefactor") == 0)
    {
	int scaleN, scaleD;

	if (argc != 2 && argc != 3) goto wrongNumArgs;
	
	scaleN = atof(argv[1]);

	if (argc == 3)
	    scaleD = atof(argv[2]);
	else
	    scaleD = 1;

	if (scaleN <= 0 || scaleD <= 0)
	{
	    TechError("Scale factor must be greater than 0.\n");
	    TechError("Setting scale factor to default value 1.\n");
	    DRCCurStyle->DRCScaleFactorN = 1;
	    DRCCurStyle->DRCScaleFactorD = 1;
	    return TRUE;
	}
	DRCCurStyle->DRCScaleFactorN = scaleN;
	DRCCurStyle->DRCScaleFactorD = scaleD;
	return TRUE;
    }

    /* Process "variant" lines next. */

    if (strncmp(argv[0], "variant", 7) == 0)
    {
	/* If our style variant is not one of the ones declared */
	/* on the line, then we ignore all input until we	*/
	/* either reach the end of the style, the end of the 	*/
	/* section, or another "variant" line.			*/

	if (argc != 2) goto wrongNumArgs;
	tptr = argv[1];
	while (*tptr != '\0')
	{
	    cptr = strchr(tptr, ',');
	    if (cptr != NULL)
	    {
		*cptr = '\0';
		for (j = 1; isspace(*(cptr - j)); j++)
		    *(cptr - j) = '\0';
	    }

	    if (*tptr == '*')
	    {
		DRCCurStyle->ds_status = TECH_PENDING;
		return TRUE;
	    }
	    else
	    {
		l = strlen(DRCCurStyle->ds_name) - strlen(tptr);
		if (!strcmp(tptr, DRCCurStyle->ds_name + l))
		{
		    DRCCurStyle->ds_status = TECH_PENDING;
		    return TRUE;
		}
	    }

	    if (cptr == NULL)
		break;
	    else
		tptr = cptr + 1;
	}
	DRCCurStyle->ds_status = TECH_SUSPENDED;
    }

    /* Anything below this line is not parsed if we're in TECH_SUSPENDED mode */
    if (DRCCurStyle->ds_status != TECH_PENDING) return TRUE;

    return DRCTechAddRule(sectionName, argc, argv);
}

void
drcAssign(cookie, dist, next, mask, corner, why, cdist, flags, planeto, planefrom)
    DRCCookie *cookie, *next;
    int dist, cdist;
    TileTypeBitMask *mask, *corner;
    char *why;
    int flags, planeto, planefrom;
{
    /* Diagnostic */
    if (planeto >= DBNumPlanes) {
	TxError("Bad plane in DRC assign!\n");
    }
    (cookie)->drcc_dist = dist;
    (cookie)->drcc_next = next;
    (cookie)->drcc_mask = *mask;
    (cookie)->drcc_corner = *corner;
    (cookie)->drcc_why = why;
    (cookie)->drcc_cdist = cdist;
    (cookie)->drcc_flags = flags;
    (cookie)->drcc_edgeplane = planefrom;
    (cookie)->drcc_plane = planeto;
    (cookie)->drcc_mod = 0;
    (cookie)->drcc_cmod = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCTechAddRule --
 *
 * Add a new entry to the DRC table.
 *
 * Results:
 *	Always returns TRUE so that tech file read-in doesn't abort.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * Organization:
 *	We select a procedure based on the first keyword (argv[0])
 *	and call it to do the work of implementing the rule.  Each
 *	such procedure is of the following form:
 *
 *	int
 *	proc(argc, argv)
 *	    int argc;
 *	    char *argv[];
 *	{
 *	}
 *
 * 	It returns the distance associated with the design rule,
 *	or -1 in the event of a fatal error that should cause
 *	DRCTechAddRule() to return FALSE (currently, none of them
 *	do, so we always return TRUE).  If there is no distance
 *	associated with the design rule, 0 is returned.
 *
 * ----------------------------------------------------------------------------
 */


	/* ARGSUSED */
bool
DRCTechAddRule(sectionName, argc, argv)
    char *sectionName;		/* Unused */
    int argc;
    char *argv[];
{
    int which, distance, mdist;
    char *fmt;
    static struct
    {
	char	*rk_keyword;	/* Initial keyword */
	int	 rk_minargs;	/* Min # arguments */
	int	 rk_maxargs;	/* Max # arguments */
	int    (*rk_proc)();	/* Procedure implementing this keyword */
	char	*rk_err;	/* Error message */
    } ruleKeys[] = {
	"angles",	 4,	4,	drcAngles,
    "layers 45|90 why",
	"edge",		 8,	9,	drcEdge,
    "layers1 layers2 distance okTypes cornerTypes cornerDistance why [plane]",
	"edge4way",	 8,	9,	drcEdge,
    "layers1 layers2 distance okTypes cornerTypes cornerDistance why [plane]",
	"exact_overlap", 2,	2,	drcExactOverlap,
    "layers",
	"extend",	 5,	6,	drcExtend,
    "layers1 layers2 distance why",
	"no_overlap",	 3,	3,	drcNoOverlap,
    "layers1 layers2",
	"overhang",	 5,	5,	drcOverhang,
    "layers1 layers2 distance why",
	"rect_only",	 3,	3,	drcRectOnly,
    "layers why",
	"spacing",	 6,	7,	drcSpacing,
    "layers1 layers2 separation [layers3] adjacency why",
	"stepsize",	 2,	2,	drcStepSize,
    "step_size",
	"surround",	 6,	6,	drcSurround,
    "layers1 layers2 distance presence why",
	"width",	 4,	4,	drcWidth,
    "layers width why",
	"widespacing",	 7,	7,	drcSpacing,
    "layers1 width layers2 separation adjacency why",
        "area",		 5,	5,	drcArea,
    "layers area horizon why",
        "maxwidth",	 4,	5,	drcMaxwidth,
    "layers maxwidth bends why",
	"cifstyle",	 2,	2,	drcCifSetStyle,
    "cif_style",
	"cifwidth",	 4,	4,	drcCifWidth,
    "layers width why",
	"cifspacing",	 6,	6,	drcCifSpacing,
    "layers1 layers2 separation adjacency why",
	"cifarea",	 5,	5,	drcCifArea,
    "layers area horizon why",
	"cifmaxwidth",	 5,	5,	drcCifMaxwidth,
    "layers maxwidth bends why",
	"rectangle",	5,	5,	drcRectangle,
    "layers maxwidth [even|odd|any] why",
	0
    }, *rp;

    drcRulesSpecified += 1;

    which = LookupStruct(argv[0], (LookupTable *) ruleKeys, sizeof ruleKeys[0]);
    if (which < 0)
    {
	TechError("Bad DRC rule type \"%s\"\n", argv[0]);
	TxError("Valid rule types are:\n");
	for (fmt = "%s", rp = ruleKeys; rp->rk_keyword; rp++, fmt = ", %s")
	    TxError(fmt, rp->rk_keyword);
	TxError(".\n");
	return (TRUE);
    }
    rp = &ruleKeys[which];
    if (argc < rp->rk_minargs || argc > rp->rk_maxargs)
    {
	TechError("Rule type \"%s\" usage: %s %s\n",
		rp->rk_keyword, rp->rk_keyword, rp->rk_err);
	return (TRUE);
    }

    distance = (*rp->rk_proc)(argc, argv);
    if (distance < 0)
	return (FALSE);

    /* Update the halo to be the maximum distance (in magic units) of	*/
    /* any design rule							*/

    mdist = distance;

    if (mdist > DRCTechHalo)
	DRCTechHalo = mdist;

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcExtend --
 *
 * Process an extension rule.
 * This is of the form:
 *
 *	extend layers1 layers2 distance [exact_width] why
 *
 * indicating that if "layers1" extends from "layers2", then it must
 * have a width of at least "distance".  This is very much like the
 * "overhang" rule, except that the extension is optional.  Example:
 *
 *	extend nfet ndiff 2 "n-transistor length must be at least 2"
 *
 * "extend" implements the following general-purpose edge rule:
 *
 *	edge4way layers2 layers1 distance layers1 0 0 why
 *
 * Option "exact_width" implements an additional rule that checks for
 * maximum extension at distance.  This is intended for use with, for
 * example, a fixed gate length for a specific type of device.
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcExtend(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1];
    char *layers2 = argv[2];
    int distance = atoi(argv[3]);
    char *why;
    TileTypeBitMask set1, setC;
    DRCCookie *dp, *dpnew, *dptrig;
    TileType i, j;
    int plane, plane2;
    TileTypeBitMask set2, setZ, setN;
    PlaneMask pMask1, pMask2, pset, ptest;
    bool exact = FALSE;

    if (!strncmp(argv[4], "exact_", 6))
    {
	exact = TRUE;
	why = drcWhyDup(argv[5]);
    }
    else
	why = drcWhyDup(argv[4]);

    ptest = DBTechNoisyNameMask(layers1, &set1);
    pMask1 = CoincidentPlanes(&set1, ptest);
    
    if (pMask1 == 0)
    {
	TechError("All layers in first set for \"extend\" must be on "
			"the same plane\n");
	return (0);
    }
    TTMaskCom2(&setN, &set1);

    ptest = DBTechNoisyNameMask(layers2, &set2);
    pMask2 = CoincidentPlanes(&set2, ptest);

    if (pMask2 == 0)
    {
	TechError("All layers in second set for \"extend\" must be on "
			"the same plane\n");
	return (0);
    }
    TTMaskCom2(&setC, &set2);

    /* Zero mask */
    TTMaskZero(&setZ);

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    if (pset = (DBTypesOnSamePlane(i, j) & pMask2))
	    {
		/* Edge depends on whether or not the extension is 	*/
		/* on the same plane as the layer from which it is	*/
		/* measured.						*/

		if ((pset & pMask1) != 0)
		{
		    if (TTMaskHasType(&set2, i) && TTMaskHasType(&set1, j))
		    {
			plane = LowestMaskBit(pset & pMask1);

			/* find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &setZ, why,
				    0, DRC_FORWARD, plane, plane);

			dp->drcc_next = dpnew;

			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &setZ, why,
				    0, DRC_REVERSE, plane, plane);
	
			dp->drcc_next = dpnew;

			if (exact)
			{
			    /* find bucket preceding the new one we wish to insert */
			    dp = drcFindBucket(i, j, distance);
			    dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			    drcAssign(dpnew, distance, dp->drcc_next, &setN, &setZ, why,
				    0, DRC_FORWARD | DRC_OUTSIDE, plane, plane);

			    dp->drcc_next = dpnew;

			    dp = drcFindBucket(j, i, distance);
			    dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			    drcAssign(dpnew, distance, dp->drcc_next, &setN, &setZ, why,
				    0, DRC_REVERSE | DRC_OUTSIDE, plane, plane);
	
			    dp->drcc_next = dpnew;
			}
		    }
		}
		else	/* Multi-plane extend rule */
		{
		    if (TTMaskHasType(&set2, i) && TTMaskHasType(&setC, j))
		    {
			plane = LowestMaskBit(pset);
			plane2 = LowestMaskBit(pMask1);

			/* find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &setZ, why,
				    0, DRC_FORWARD, plane2, plane);
			dptrig = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dptrig, 1, dpnew, &setN, &setZ, why,
				    0, DRC_FORWARD | DRC_TRIGGER, plane2, plane);
			dp->drcc_next = dptrig;

			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &setZ, why,
				    0, DRC_REVERSE, plane2, plane);
			dptrig = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dptrig, 1, dpnew, &setN, &setZ, why,
				    0, DRC_REVERSE | DRC_TRIGGER, plane2, plane);
			dp->drcc_next = dptrig;
		    }
		}
	    }
	}
    }
    return (distance);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcWidth --
 *
 * Process a width rule.
 * This is of the form:
 *
 *	width layers distance why
 *
 * e.g,
 *
 *	width poly,pmc 2 "poly width must be at least 2"
 *
 * Optional "from layers2" is useful when defining a device width;
 * effectively, it represents an overhang rule where the presence of
 * the overhanging material is optional.  The equivalent rule is:
 *
 *	edge4way layers2 layers distance layers 0 0 why
 *
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcWidth(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int distance = atoi(argv[2]);
    char *why = drcWhyDup(argv[3]);
    TileTypeBitMask set, setC;
    PlaneMask pmask, pset, ptest;
    DRCCookie *dp, *dpnew;
    TileType i, j;
    int plane;

    ptest = DBTechNoisyNameMask(layers, &set);
    pmask = CoincidentPlanes(&set, ptest);
    TTMaskCom2(&setC, &set);

    if (pmask == 0)
    {
	TechError("All layers for \"width\" must be on same plane\n");
	return (0);
    }

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    /*
	     * Must have types in 'set' for at least 'distance'
	     * to the right of any edge between a type in '~set'
	     * and a type in 'set'.
	     */

	    if (pset = (DBTypesOnSamePlane(i, j) & pmask))
	    {
		if (TTMaskHasType(&setC, i) && TTMaskHasType(&set, j))
		{
		    plane = LowestMaskBit(pset);

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, distance, dp->drcc_next, &set, &set,
				why, distance, DRC_FORWARD, plane, plane);
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return (distance);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcArea --
 *
 * Process an area rule.
 * This is of the form:
 *
 *	area layers area horizon why
 *
 * e.g,
 *
 *	area pmc 4 2 "poly contact area must be at least 4"
 *
 * "area" is the total area in lambda^2.
 *
 * "horizon" is the halo distance for the check.  Normally, this would
 * be the area (in lambda^2) divided by the minimum width rule (in
 * lambda).  Anything larger would not be a violation as long as the
 * minimum width rule is satisfied.
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcArea(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int distance = atoi(argv[2]);
    int	horizon = atoi(argv[3]);
    char *why = drcWhyDup(argv[4]);
    TileTypeBitMask set, setC;
    DRCCookie *dp, *dpnew;
    TileType i, j;
    PlaneMask pmask, ptest, pset;
    int plane;

    ptest = DBTechNoisyNameMask(layers, &set);
    pmask = CoincidentPlanes(&set, ptest);
    TTMaskCom2(&setC, &set);

    if (pmask == 0)
    {
	TechError("All layers for \"area\" must be on same plane\n");
	return (0);
    }

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    /*
	     * Must have types in 'set' for at least 'distance'
	     * to the right of any edge between a type in '~set'
	     * and a type in 'set'.
	     */
	    if (pset = (DBTypesOnSamePlane(i, j) & pmask))
	    {
		if (TTMaskHasType(&setC, i) && TTMaskHasType(&set, j))
		{
		    plane = LowestMaskBit(pset);

		    /* find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, horizon);
		    dpnew = (DRCCookie *) mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, horizon, dp->drcc_next, &set, &set, why,
			    distance, DRC_AREA|DRC_FORWARD, plane, plane);
	
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return (horizon);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcMaxwidth --
 *
 * Process a maxwidth rule.
 * This is of the form:
 *
 *	maxwidth layers distance [bends] why
 *
 * This routine was updated 3/6/05 to match the "canonical" definition of
 * a maxwidth region, which is any rectangle containing <layers> that is
 * at least <distance> in both width and height.  If the keyword
 * "bend_illegal" is present, then the definition reverts to the original
 * (see below) for backwards-compatibility.  Otherwise ("bend_ok" or
 * nothing), the new routine is used.
 *
 *	maxwidth metal1 389 "metal1 width > 35um must be slotted"
 *	maxwidth pmc 4 bend_illegal "poly contact area must be no wider than 4"
 *	maxwidth trench 4 bend_ok "trench width must be exactly 4"
 *
 *      bend_illegal - means that one_dimension must be distance for any 
 *  		point in the region.  This is used for emitters and contacts
 *		that are rectangular (so we can't generate them with the
 *		squares command) and some exact width in one direction.
 *	bend_ok - Used mainly for wide metal rules where metal greater than
 *		some given width must be slotted.  Also, used for things
 *		like trench, where the width is some fixed value:
 *
 *			XXXXX		XXXXXX
 *			X   X		XXXXXX
 *			X   X		X    X
 *			XXXXX		XXXXXX
 *			
 *			 OK		 BAD		
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcMaxwidth(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int distance = atoi(argv[2]);
    char *bends = argv[3];
    char *why;
    TileTypeBitMask set, setC;
    DRCCookie *dp, *dpnew;
    TileType i, j;
    PlaneMask pmask, ptest, pset;
    int plane;
    int bend;

    ptest = DBTechNoisyNameMask(layers, &set);
    pmask = CoincidentPlanes(&set, ptest);
    TTMaskCom2(&setC, &set);

    if (pmask == 0)
    {
	TechError("All layers for \"maxwidth\" must be on same plane\n");
	return (0);
    }

    if (argc == 4)
    {
	/* "bends" is irrelevent if distance is zero, so choose the	*/
	/* faster algorithm to process					*/

	if (distance == 0)
	    bend = 0;
	else
	    bend = DRC_BENDS;
	why = drcWhyDup(argv[3]);
    }
    else
    {
	if (strcmp(bends,"bend_illegal") == 0) bend = 0;
	else if (strcmp(bends,"bend_ok") == 0) bend = DRC_BENDS;
	else
	{
	    TechError("unknown bend option %s\n",bends);
	    return (0);
	}
	why = drcWhyDup(argv[4]);
    }

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    /*
	     * Must have types in 'set' for at least 'distance'
	     * to the right of any edge between a type in '~set'
	     * and a type in 'set'.
	     */
	    if (pset = (DBTypesOnSamePlane(i, j) & pmask))
	    {
		if (TTMaskHasType(&setC, i) && TTMaskHasType(&set, j))
		{
		    plane = LowestMaskBit(pset);

		    /* find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);
		    dpnew = (DRCCookie *) mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, distance, dp->drcc_next, &set, &set, why,
				    distance, DRC_MAXWIDTH | bend, plane, plane);
	
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return (distance);
}

/*
 * ----------------------------------------------------------------------------
 * drcAngles --
 *
 * Process an "angles" rule specifying that geometry for a certain layer
 * must be limited to 90 degrees or 45 degrees.  If not specified, any
 * angle is allowed, although width rules will flag errors on acute angles.
 *
 * ----------------------------------------------------------------------------
 */

int
drcAngles(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    int angles = atoi(argv[2]);
    char *why = drcWhyDup(argv[3]);
    TileTypeBitMask set;
    DRCCookie *dp, *dpnew;
    int plane;
    TileType i, j;

    DBTechNoisyNameMask(layers, &set);

    angles /= 45;
    angles--;		/* angles now 0 for 45s and 1 for 90s */

    if ((angles != 0) && (angles != 1))
    {
	TechError("angles must be 45 or 90\n");
	return 0;
    }

    for (i = 0; i < DBNumTypes; i++)
    {
	if (TTMaskHasType(&set, i))
	{
	    plane = DBPlane(i);

	    /* Insert rule at boundary of tile and TT_SPACE.  This is	*/
	    /* processed for each tile, separately from other rules, so	*/
	    /* we don't really care what the edge is;  TT_SPACE is	*/
	    /* chosen as an arbitrary place to hold the rule.		*/

	    dp = drcFindBucket(TT_SPACE, i, 1);
	    dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
	    drcAssign(dpnew, 1, dp->drcc_next, &set, &set, why,
			1, DRC_ANGLES | angles, plane, plane);
	    dp->drcc_next = dpnew;
	}
    }
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcSpacing3 --
 *
 * Process a special spacing rule of the form:
 *
 *	spacing layers1 layers2 distance corner_ok layers3 why
 *
 * This spacing rule is not checked when a type from "layers3" fills the
 * corner between "layers1" and "layers2".  This is used, e.g., for
 * diffusion-to-poly spacing, where diffusion and poly may touch at the
 * corner of a FET type.  It is equivalent to:
 *
 *	edge4way layers1 ~(layers3,layers1) distance ~(layers2) 0 0 why
 *
 * ----------------------------------------------------------------------------
 */

int
drcSpacing3(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2 = argv[2];
    char *layers3 = argv[5];
    int distance = atoi(argv[3]);
    char *adjacency = argv[4];
    char *why = drcWhyDup(argv[6]);
    TileTypeBitMask set1, set2, set3;
    int plane;
    DRCCookie *dp, *dpnew;
    TileType i, j;
    PlaneMask pmask, pset, ptest;

    ptest = DBTechNoisyNameMask(layers1, &set1);
    pmask = CoincidentPlanes(&set1, ptest);

    ptest = DBTechNoisyNameMask(layers2, &set2);
    pmask &= CoincidentPlanes(&set2, ptest);

    ptest = DBTechNoisyNameMask(layers3, &set3);
    pmask &= CoincidentPlanes(&set3, ptest);

    if (pmask == 0)
    {
	TechError("Spacing check with \"corner_ok\" must have"
			" all types in one plane.\n");
	return (0);
    }

    /* In this usage everything must fall in the same plane. */

    /* We need masks for (~types2)/plane and for (~(types1,types3))/plane */

    TTMaskCom(&set2);
    TTMaskSetMask(&set3, &set1);
    TTMaskCom(&set3);

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    if (pset = (DBTypesOnSamePlane(i, j) & pmask))
	    {
		if (TTMaskHasType(&set1, i) && TTMaskHasType(&set3, j))
		{
		    plane = LowestMaskBit(pset);

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, distance, dp->drcc_next, &set2, &set3,
				why, distance, DRC_FORWARD | DRC_BOTHCORNERS,
				plane, plane);
		    dp->drcc_next = dpnew;

		    /* find bucket preceding new one we wish to insert */
		    dp = drcFindBucket(j, i, distance);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, distance, dp->drcc_next, &set2, &set3,
				why, distance, DRC_REVERSE | DRC_BOTHCORNERS,
				plane, plane);
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return distance;
}

/*
 *-------------------------------------------------------------------
 *
 * drcMaskSpacing ---
 *
 *	This is the core of the drcSpacing routine.  When the spacing
 *	rule layers independently cover more than one plane, this routine
 *	is invoked for each independent plane.
 *
 * Results:
 *	Returns the rule's maximum distance
 *
 * Side effects:
 *	Adds rules to the DRC rule table.
 *	
 *-------------------------------------------------------------------
 */

int
drcMaskSpacing(set1, set2, pmask1, pmask2, wwidth, distance, adjacency,
		why, widerule, multiplane)
    TileTypeBitMask *set1, *set2;
    PlaneMask pmask1, pmask2;
    int wwidth, distance;
    char *adjacency, *why;
    bool widerule, multiplane;
{
    TileTypeBitMask tmp1, tmp2, setR, setRreverse;
    int plane, plane2;
    PlaneMask pset, ptest;
    DRCCookie *dp, *dpnew;
    int needReverse = (widerule) ? TRUE : FALSE;
    TileType i, j, pref;
    bool needtrigger = FALSE;
    bool touchingok = TRUE;
    bool cornerok = FALSE;

    if (!strcmp(adjacency, "surround_ok"))
    {
	if (multiplane)
	{
	    TechError("\"surround_ok\" requires surrounding types to "
		"be in the same plane.\n");
	    return (0);
	}
	else if ((pmask1 & pmask2) == 0)
	{
	    /* New rule implementation (7/8/06):  When types	*/
	    /* are on different planes and "surround_ok" is	*/
	    /* declared, implement as a triggered rule (but not	*/
	    /* for widespacing, which already uses the		*/
	    /* triggering rule mechanism).			*/

	    if (!widerule)
	    {
		needtrigger = TRUE;
		touchingok = FALSE;

	    }
	    else
	    {
		TechError("Widespacing checks cannot use \"surround_ok\".\n");
		return (0);
	    }
	}
	else
	{
	    TechError("\"surround_ok\" used when spacing rule types are in "
			"the same plane.  Did you mean \"touching_ok\"?\n");
	    touchingok = TRUE;	/* Treat like "touching_ok" */
	}
    }
    else if (!strcmp(adjacency, "touching_ok"))
    {
	/* If touching is OK, everything must fall in the same plane. */
	if (multiplane || ((pmask1 & pmask2) == 0))
	{
	    TechError("Spacing check with \"touching_ok\" must have"
			" all types in one plane.  Possibly you want"
			" \"surround_ok\"?\n");
	    return (0);
	}
	else
	    touchingok = TRUE;
    }
    else if (!strcmp(adjacency, "touching_illegal"))
    {
	touchingok = FALSE;
	needtrigger = FALSE;
    }
    else
    {
	TechError("Badly formed drc spacing line:  need \"touching_ok\", "
			"\"touching_illegal\", or \"surround_ok\".\n");
	return (0);
    }

    if (touchingok)
    {
	/* In "touching_ok rules, spacing to set2  is be checked in FORWARD 
	 * direction at edges between set1 and  (setR = ~set1 AND ~set2).
	 *
	 * In addition, spacing to set1 is checked in FORWARD direction 
	 * at edges between set2 and (setRreverse = ~set1 AND ~set2).
	 *
	 * If set1 and set2 are different, above are checked in REVERSE as
	 * well as forward direction.  This is important since touching
	 * material frequently masks violations in one direction.
	 *
	 * setR and setRreverse are set appropriately below.
	 */

	tmp1 = *set1;
	tmp2 = *set2; 

	/* Restrict planes to those that are coincident */
	pmask1 &= pmask2;
	pmask2 = pmask1;
	TTMaskCom(&tmp1);
	TTMaskCom(&tmp2);
	TTMaskAndMask(&tmp1, &tmp2);
	setR = tmp1;
	setRreverse = tmp1;

	/* If set1 != set2, set flag to check rules in both directions */
	if (!TTMaskEqual(set1, set2))
	    needReverse = TRUE;
    }
    else
    {
	/* In "touching_illegal" rules, spacing to set2 will be checked
	 * in FORWARD direction at edges between set1 and (setR=~set1). 
	 *
	 * In addition, spacing to set1 will be checked in FORWARD direction
	 * at edges between set2 and (setRreverse=  ~set2).
	 *
	 * setR and setRreverse are set appropriately below.
	 */
	TTMaskCom2(&setR, set1);
	TTMaskCom2(&setRreverse, set2);
    }

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    if (pset = (DBTypesOnSamePlane(i, j) & pmask1))
	    {
		plane = LowestMaskBit(pset);

		/* LHS is an element of set1, RHS is an element of setR */
		if (TTMaskHasType(set1, i) && TTMaskHasType(&setR, j))
		{
		    plane2 = LowestMaskBit(pmask2);

		    /*
		     * Must not have 'set2' for 'distance' to the right of
		     * an edge between 'set1' and the types not in 'set1'
		     * (touching_illegal case) or in neither
		     * 'set1' nor 'set2' (touching_ok case).
		     */

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);

		    dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
		    TTMaskClearMask3(&tmp1, &DBPlaneTypes[plane2], set2);
		    TTMaskAndMask3(&tmp2, &DBPlaneTypes[plane], &setR);

		    if (widerule)
		    {
			DRCCookie *dptrig;

			/* Create two contiguous rules, one for spacing	*/
			/* and one for width.  These are created in	*/
			/* reverse order due to the stack property of	*/
			/* the linked list.				*/

			drcAssign(dpnew, distance, dp->drcc_next, &tmp1, &tmp2,
				why, distance, DRC_FORWARD, plane2, plane);
			dptrig = (DRCCookie *) mallocMagic((unsigned)
				(sizeof (DRCCookie)));
			drcAssign(dptrig, wwidth, dpnew, set1, set1, why,
				wwidth, DRC_REVERSE | DRC_MAXWIDTH |
				DRC_TRIGGER | DRC_BENDS, plane2, plane);

			dp->drcc_next = dptrig;
		    }
		    else if (needtrigger)
		    {
			DRCCookie *dptrig;
			
			/* Create two contiguous spacing rules */

			drcAssign(dpnew, distance, dp->drcc_next, &tmp1, &tmp2,
			 	why, wwidth, DRC_FORWARD | DRC_BOTHCORNERS,
				plane2, plane);
			dptrig = (DRCCookie *) mallocMagic((unsigned)
				(sizeof (DRCCookie)));
			drcAssign(dptrig, 1, dpnew, set2, &tmp2, why, 1,
			 	DRC_FORWARD | DRC_TRIGGER,
				plane2, plane);

			dp->drcc_next = dptrig;
		    }
		    else
		    {
			drcAssign(dpnew, distance, dp->drcc_next, &tmp1,
				&tmp2, why, wwidth, DRC_FORWARD, plane2, plane);
			dp->drcc_next = dpnew;
		    }

		    if (needReverse)
			dpnew->drcc_flags |= DRC_BOTHCORNERS;

		    if (needReverse)
		    {
			/* Add check in reverse direction, 
			 * NOTE:  am assuming single plane rule here (since reverse
			 * rules only used with touching_ok which must be 
			 * single plane)
			 */
			 
			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
			if (widerule)
			{
			    DRCCookie *dptrig;

			    /* Assign two coupled rules (see above) */

			    drcAssign(dpnew, distance, dp->drcc_next, &tmp1,
					&tmp2, why, distance,
					DRC_REVERSE | DRC_BOTHCORNERS, plane2, plane);
			    dptrig = (DRCCookie *) mallocMagic((unsigned)
					(sizeof (DRCCookie)));
			    drcAssign(dptrig, wwidth, dpnew, set1, set1, why,
					wwidth, DRC_FORWARD | DRC_MAXWIDTH |
					DRC_TRIGGER | DRC_BENDS, plane2, plane);
			    dp->drcc_next = dptrig;
			}
			else if (needtrigger)
			{
			    DRCCookie *dptrig;
			
			    /* Create two contiguous spacing rules */

			    drcAssign(dpnew, distance, dp->drcc_next, &tmp1, &tmp2,
			 		why, wwidth, DRC_REVERSE | DRC_BOTHCORNERS,
					plane2, plane);
			    dptrig = (DRCCookie *) mallocMagic((unsigned)
					(sizeof (DRCCookie)));
			    drcAssign(dptrig, 1, dpnew, set2, &tmp2, why, 1,
			 		DRC_REVERSE | DRC_TRIGGER,
					plane2, plane);

			    dp->drcc_next = dptrig;
			}
			else
			{
			    drcAssign(dpnew,distance,dp->drcc_next,
					&tmp1, &tmp2, why, wwidth, 
					DRC_REVERSE | DRC_BOTHCORNERS, plane2, plane);
			    dp->drcc_next = dpnew;
		 	}
		    }
		}
	    }

	    if (TTMaskEqual(set1, set2)) continue;
	    if (widerule) continue;	/* Can't determine width of set1    */
					/* when looking at the edge of set2 */

	    /*
	     * Now, if set1 and set2 are distinct apply the rule for LHS in set1
	     * and RHS in set2.
	     */

	    if (pset = (DBTypesOnSamePlane(i, j) & pmask2))
	    {
		plane = LowestMaskBit(pset);

		/* LHS is an element of set2, RHS is an element of setRreverse */
		if (TTMaskHasType(set2, i) && TTMaskHasType(&setRreverse, j))
		{
		    plane2 = LowestMaskBit(pmask1);

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    TTMaskClearMask3(&tmp1, &DBPlaneTypes[plane2], set1);
		    TTMaskAndMask3(&tmp2, &DBPlaneTypes[plane], &setRreverse);

		    if (needtrigger)
		    {
			DRCCookie *dptrig;
			
			/* Create two contiguous spacing rules */

		        drcAssign(dpnew, distance, dp->drcc_next, &tmp1, &tmp2,
					why, distance, DRC_FORWARD | DRC_BOTHCORNERS,
					plane2, plane);
			dptrig = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
			drcAssign(dptrig, 1, dpnew, set1, &tmp2, why, 1,
			 		DRC_FORWARD | DRC_TRIGGER,
					plane2, plane);
			dp->drcc_next = dptrig;
		    }
		    else
		    {
			drcAssign(dpnew, distance, dp->drcc_next, &tmp1, &tmp2,
					why, distance, DRC_FORWARD, plane2, plane);
			dp->drcc_next = dpnew;		    
		    }

		    if (needReverse)
			dpnew->drcc_flags |= DRC_BOTHCORNERS;

		    if (needReverse)
		    {
			/* Add check in reverse direction, 
			 * NOTE:  am assuming single plane rule here (since reverse
			 * rules only used with touching_ok which must be 
			 * single plane)
			 */
			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *) mallocMagic(sizeof (DRCCookie));
			 
			if (needtrigger)
			{
			    DRCCookie *dptrig;
			
			    /* Create two contiguous spacing rules */

		            drcAssign(dpnew, distance, dp->drcc_next, &tmp1, &tmp2,
					why, distance, DRC_REVERSE | DRC_BOTHCORNERS,
					plane2, plane);
			    dptrig = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
			    drcAssign(dptrig, 1, dpnew, set1, &tmp2, why, 1,
			 		DRC_REVERSE | DRC_TRIGGER,
					plane2, plane);
			    dp->drcc_next = dptrig;
			}
			else
			{
			    drcAssign(dpnew, distance, dp->drcc_next,
					&tmp1, &tmp2, why, distance, 
					DRC_REVERSE | DRC_BOTHCORNERS, plane2, plane);
			    dp->drcc_next = dpnew;
			}
		    }
		}
	    }

	    /* Finally, if multiplane rule then check that set2 types
	     * are not present just to right of edges with setR on LHS
	     * and set1 on RHS.  This check is necessary to make sure
	     * that a set1 rectangle doesn't coincide exactly with a
	     * set2 rectangle.  
	     * (This check added by Michael Arnold on 4/10/86.)
	     */

	    if (needtrigger) continue; 

	    if (pset = (DBTypesOnSamePlane(i, j) & pmask1))
	    {
		plane = LowestMaskBit(pset);

		/* LHS is an element of setR, RHS is an element of set1 */
		if (TTMaskHasType(&setR, i) && TTMaskHasType(set1, j))
		{
		    /*
		     * Must not have 'set2' for 'distance' to the right of
		     * an edge between the types not in set1 and set1.
		     * (is only checked for cross plane rules - these are
		     * all of type touching_illegal)
		     */

		    /* Walk list to last check.  New checks ("cookies") go
		     * at end of list since we are checking for distance of
		     * 1 and the list is sorted in order of decreasing distance.
		     */
		    for (dp = DRCCurStyle->DRCRulesTbl [i][j];
		    		dp->drcc_next != (DRCCookie *) NULL;
		    		dp = dp->drcc_next); /* null body */

		    /* Insert one check for each plane involved in set2 */
		    plane2 = LowestMaskBit(pmask2);

		    /* filter out checks that are not cross plane */
		    if (i == TT_SPACE) 
		    {
			if (DBTypeOnPlane(j, plane2))
			    continue;
		    }
		    else
		    {
			if (DBTypeOnPlane(i, plane2))
			    continue;
		    }

		    /* create new check and add it to list */
		    dpnew = (DRCCookie *) mallocMagic(sizeof (DRCCookie));
		    TTMaskClearMask3(&tmp1, &DBPlaneTypes[plane2], set2);
		    TTMaskZero(&tmp2);

		    drcAssign(dpnew, 1, dp->drcc_next, &tmp1, &tmp2, why,
				distance, DRC_FORWARD, plane2, plane);
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return (MAX(wwidth, distance));
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcSpacing --
 *
 * Process a spacing rule.
 * This is of the form:
 *
 *	spacing layers1 layers2 distance adjacency why
 *
 * e.g,
 *
 *	spacing metal,pmc/m,dmc/m metal,pmc/m,dmc/m 4 touching_ok \
 *		"metal spacing must be at least 4"
 *
 * Adjacency may be either "touching_ok" or "touching_illegal"
 * In the first case, no violation occurs when types in layers1 are
 * immediately adjacent to types in layers2.  In the second case,
 * such adjacency causes a violation.
 *
 * Results:
 *	Returns distance.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * Notes:
 * Extended to include the rule syntax:
 *
 *	widespacing layers1 width layers2 distance adjacency why
 *
 * This extension covers rules such as "If m1 width > 10um, then spacing to
 * unconnected m1 must be at least 0.6um".  This assumes that the instantiated
 * edge4way rule is a standard "spacing" rule in which "dist" and "cdist"
 * (corner extension) distances are always the same, so we can use the "cdist"
 * record to encode the width of "layers1" which triggers the rule.  We re-use
 * the CheckMaxwidth() code, but with the following differences: 1) it does not
 * trigger any error painting functions, 2) it returns a value stating whether
 * the maxwidth rule applies or not, 3) it uses the DBLayerTypeMaskTbl[] for
 * the tile type in question, not "oktypes", for its search, and 4) it uses
 * the max width in the "cdist" record, not the "dist" record, for the max
 * width rule.  The "dist" record is copied to "cdist" before actually applying
 * the spacing rule, so that the rule acts like a proper spacing rule.
 *
 * Added adjacency rule "surround_ok" to check spacing to a type that
 * may also abut or surround the first layer---the portion of the layer
 * that is abutting or surrounding is not checked.  This allows checks
 * of, e.g., diffusion in well to a separate well edge, or distance from
 * a poly bottom plate to unconnected poly.
 *
 * (Added 11/6/06) Adjacency may be "corner_ok", in which case it calls
 * routing drcSpacing3() (see above).
 *
 * ----------------------------------------------------------------------------
 */

int
drcSpacing(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2;
    char *adjacency;
    char *why;
    TileTypeBitMask set1, set2, tmp1, tmp2;
    PlaneMask pmask1, pmask2, pmaskA, pmaskB, ptest;
    int wwidth, distance, plane, plane2;
    bool widerule, multiplane = FALSE;

    if ((argc == 7) && (!strcmp(argv[4], "corner_ok")))
	return drcSpacing3(argc, argv);

    widerule = (strncmp(argv[0], "wide", 4) == 0);

    if (widerule)
    {
	wwidth = atoi(argv[2]);
	layers2 = argv[3];
	distance = atoi(argv[4]);
	adjacency = argv[5];
	why = drcWhyDup(argv[6]);
	/* TxPrintf("Info:  DRCtech:  widespacing rule for %s width %d:"
		" spacing must be %d\n", layers1, wwidth, distance); */
    }
    else
    {
	layers2 = argv[2];
	distance = atoi(argv[3]);
	adjacency = argv[4];
	wwidth = distance;
	why = drcWhyDup(argv[5]);
	if (argc == 7)
	{
	    TechError("Unknown argument in spacing line.\n");
	    return(0);
	}
    }

    /* Either list of types may contain independent types on different	*/
    /* planes.  However, if so, then we may not use touching_ok, and	*/
    /* there are other restrictions.					*/

    ptest = DBTechNoisyNameMask(layers1, &set1);
    pmask1 = CoincidentPlanes(&set1, ptest);

    if ((pmask1 == 0) && (ptest != 0))
    {
	pmask1 = ptest;
	multiplane = TRUE;
	for (plane = 0; plane < DBNumPlanes; plane++)
	    for (plane2 = 0; plane2 < DBNumPlanes; plane2++)
	    {
		if (plane == plane2) continue;
		if (PlaneMaskHasPlane(pmask1, plane) &&
			PlaneMaskHasPlane(pmask1, plane2))
		{
		    TTMaskAndMask3(&tmp1, &DBPlaneTypes[plane],
				&DBPlaneTypes[plane2]);
		    TTMaskAndMask(&tmp1, &set1);
		    if (!TTMaskIsZero(&tmp1))
		    {
			TechError("Types in first list must either be in"
				" one plane or else types must not share"
				" planes.\n");
			return (0);
		    }
		}
	    }
    }

    ptest = DBTechNoisyNameMask(layers2, &set2);
    pmask2 = CoincidentPlanes(&set2, ptest);

    if ((pmask2 == 0) && (ptest != 0))
    {
	pmask2 = ptest;
	multiplane = TRUE;
	for (plane = 0; plane < DBNumPlanes; plane++)
	    for (plane2 = 0; plane2 < DBNumPlanes; plane2++)
	    {
		if (plane == plane2) continue;
		if (PlaneMaskHasPlane(pmask2, plane) &&
			PlaneMaskHasPlane(pmask2, plane2))
		{
		    TTMaskAndMask3(&tmp1, &DBPlaneTypes[plane],
				&DBPlaneTypes[plane2]);
		    TTMaskAndMask(&tmp1, &set2);
		    if (!TTMaskIsZero(&tmp1))
		    {
			TechError("Types in second list must either be in"
				" one plane or else types must not share"
				" planes.\n");
			return (0);
		    }
		}
	    }
    }

    if (multiplane)
    {
	/* Loop over independent plane/layer combinations */

	for (plane = 0; plane < DBNumPlanes; plane++)
	    for (plane2 = 0; plane2 < DBNumPlanes; plane2++)
	    {
		if (PlaneMaskHasPlane(pmask1, plane) &&
			PlaneMaskHasPlane(pmask2, plane2))
		{
		    pmaskA = PlaneNumToMaskBit(plane);
		    pmaskB = PlaneNumToMaskBit(plane2);

		    TTMaskAndMask3(&tmp1, &set1, &DBPlaneTypes[plane]);
		    TTMaskAndMask3(&tmp2, &set2, &DBPlaneTypes[plane2]);

		    return drcMaskSpacing(&tmp1, &tmp2, pmaskA, pmaskB,
				wwidth, distance, adjacency, why,
				widerule, multiplane);
		}
	    }
    }
    else
	return drcMaskSpacing(&set1, &set2, pmask1, pmask2, wwidth,
		distance, adjacency, why, widerule, multiplane);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcEdge --
 *
 * Process a primitive edge rule.
 * This is of the form:
 *
 *	edge layers1 layers2 dist OKtypes cornerTypes cornerDist why [plane]
 * or	edge4way layers1 layers2 dist OKtypes cornerTypes cornerDist why [plane]
 *
 * e.g,
 *
 *	edge poly,pmc s 1 diff poly,pmc "poly-diff separation must be 2"
 *
 * An "edge" rule is applied only down and to the left.
 * An "edge4way" rule is applied in all four directions.
 *
 * Results:
 *	Returns greater of dist and cdist.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcEdge(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2 = argv[2];
    int distance = atoi(argv[3]);
    char *okTypes = argv[4], *cornerTypes = argv[5];
    int cdist = atoi(argv[6]);
    char *why = drcWhyDup(argv[7]);
    bool fourway = (strcmp(argv[0], "edge4way") == 0);
    TileTypeBitMask set1, set2, setC, setM;
    DRCCookie *dp, *dpnew;
    int plane, checkPlane, tmpPlane;
    PlaneMask pMask1, pMaskM, pMaskC, pset, ptest;
    TileType i, j;

    /*
     * Edge4way rules produce [j][i] entries as well as [i][j]
     * ones, and check both corners rather than just one corner.
     */

    ptest = DBTechNoisyNameMask(layers1, &set1);
    pMask1 = CoincidentPlanes(&set1, ptest);

    ptest = DBTechNoisyNameMask(layers2, &set2);
    pMask1 &= CoincidentPlanes(&set2, ptest);

    if (pMask1 == 0) 
    {
	TechError("All edges in edge rule must lie in shared planes.\n");
	return (0);
    }

    /* Give warning if types1 and types2 intersect */
    if (TTMaskIntersect(&set1, &set2))
	TechError("Warning:  types1 and types2 have nonempty intersection.  "
		"DRC does not check edges with the same type on both "
		"sides.\n");

    ptest = DBTechNoisyNameMask(cornerTypes, &setC);
    pMaskC = CoincidentPlanes(&setC, ptest);

    if ((pMaskC & pMask1) == 0)
    {
	TechError("Corner types aren't in same plane as edges.\n");
	return (0);
    }

    if (argc == 9)
	tmpPlane = DBTechNoisyNamePlane(argv[8]);

    /*
     * OKtypes determine the checkPlane.  If checkPlane exists, it should
     * only be used to check against the plane of OKtypes.
     */

    ptest = DBTechNoisyNameMask(okTypes, &setM);
    pMaskM = CoincidentPlanes(&setM, ptest);
 
    if (pMaskM == 0 || pMaskM == DBTypePlaneMaskTbl[TT_SPACE])
    {
	/* Technically it should be illegal to specify simply "space"
	 * in the types list for a DRC rule, as it is ambiguous.
	 * However, we will assume that the plane of the edge is
	 * intended.  The "plane" argument may be used to do the
	 * qualification (for backwards compatibility); any other use
	 * gets a warning.
	 */

	if (TTMaskEqual(&DBSpaceBits, &setM))
	{
	    if (argc == 9)
		pMaskM = PlaneNumToMaskBit(tmpPlane);
	    else
	    {
		TechError("OK types \"%s\" in more than one plane.\n"
	        	"	Assuming same plane (%s) as edge.\n",
			okTypes, DBPlaneLongNameTbl[LowestMaskBit(pMask1)]);
		pMaskM = pMask1;
	    }
	}

	/* The case okTypes="0" is explicitly allowed according */
	/* to the manual, so we parse it accordingly.		*/

	else if (!strcmp(okTypes, "0"))
	    pMaskM = pMask1;
	else
	{
	    TechError("All OK types must lie in one plane.\n");
	    return (0);
	}
    }

    /* "plane" argument deprecated; kept for backward compatibility only */

    if ((argc == 9) && (PlaneNumToMaskBit(tmpPlane) != pMaskM))
	    TechError("Ignoring bad plane argument.\n");

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    if (pset = (DBTypesOnSamePlane(i, j) & pMask1))
	    {
		if (TTMaskHasType(&set1, i) && TTMaskHasType(&set2, j))
		{
		    /* For fastest DRC, checkPlane and plane should be	*/
		    /* the same, if possible.				*/

		    if (pset & pMaskM != 0)
		    {
			plane = LowestMaskBit(pset & pMaskM);
			checkPlane = plane;
		    }
		    else
		    {
			plane = LowestMaskBit(pset);
			checkPlane = LowestMaskBit(pMaskM);
		    }

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, distance, dp->drcc_next, &setM, &setC,
				why, cdist, DRC_FORWARD, checkPlane, plane);
		    if (fourway) dpnew->drcc_flags |= DRC_BOTHCORNERS;
		    dp->drcc_next = dpnew;

		    if (!fourway) continue;

		    /* find bucket preceding new one we wish to insert */
		    dp = drcFindBucket(j, i, distance);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew,distance,dp->drcc_next, &setM, &setC,
				why, cdist, DRC_REVERSE, checkPlane, plane);
		    dpnew->drcc_flags |= DRC_BOTHCORNERS;
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return (MAX(distance, cdist));
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcOverhang --
 *
 * Process an overhang rule.
 * This is of the form:
 *
 *	overhang layers2 layers1 dist why
 *
 * indicating that layers2 must overhang layers1 by a distance of at least
 * dist.
 *
 * This rule is equivalent to:
 *
 *	edge4way layers1 space/p2|layers2 dist layers1|layers2 \
 *			 space/p2|layers2 dist why
 *
 * ----------------------------------------------------------------------------
 */

int
drcOverhang(argc, argv)
    int argc;
    char *argv[];
{
    char *layers2 = argv[1], *layers1 = argv[2];
    int distance = atoi(argv[3]);
    char *why = drcWhyDup(argv[4]);
    TileTypeBitMask set1, set2, setM, setC, setN, set2inv;
    DRCCookie *dp, *dpnew, *dptrig;
    int plane, plane2;
    TileType i, j;
    PlaneMask pMask1, pMask2, pset, ptest;

    ptest = DBTechNoisyNameMask(layers1, &set1);
    pMask1 = CoincidentPlanes(&set1, ptest);
    if (pMask1 == 0)
    {
	TechError("All layers in first set for \"overhang\" must be on "
			"the same plane\n");
	return (0);
    }
    TTMaskCom2(&setN, &set1);

    ptest = DBTechNoisyNameMask(layers2, &set2);
    pMask2 = CoincidentPlanes(&set2, ptest);
    if (pMask2 == 0)
    {
	TechError("All layers in second set for \"overhang\" must be on "
			"the same plane\n");
	return (0);
    }
    TTMaskCom2(&set2inv, &set2);

    /* Warn if types1 and types2 intersect */
    if (TTMaskIntersect(&set1, &set2))
	TechError("Warning:  inside and outside types have nonempty intersection.  "
		"DRC does not check edges with the same type on both sides.\n");

    /* SetM is the union of set1 and set2 */
    TTMaskZero(&setM);
    TTMaskSetMask3(&setM, &set1, &set2);

    /* Add space to set2 */
    TTMaskSetType(&set2, TT_SPACE);

    /* SetC is the empty set */
    TTMaskZero(&setC);

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;
	    if (pset = (DBTypesOnSamePlane(i, j) & pMask2))
	    {
		if ((pset & pMask1) != 0)
		{
		    if (TTMaskHasType(&set1, i) && TTMaskHasType(&set2, j))
		    {
			plane = LowestMaskBit(pset);

			/* Find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic (sizeof (DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &setM,
					&setM, why, distance,
					DRC_FORWARD | DRC_BOTHCORNERS,
					plane, plane);
			dp->drcc_next = dpnew;

			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next,
					&setM, &setM, why, distance,
					DRC_REVERSE | DRC_BOTHCORNERS,
					plane, plane);
			dp->drcc_next = dpnew;
		    }
		}
		else	/* Multi-plane overhang rule */
		{
		    if (TTMaskHasType(&set2, i) && TTMaskHasType(&set2inv, j))
		    {
			plane = LowestMaskBit(pset);
			plane2 = LowestMaskBit(pMask1);

			/* find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &set1, why,
				    distance, DRC_FORWARD, plane2, plane);
			dptrig = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dptrig, 1, dpnew, &setN, &setC, why,
				    0, DRC_FORWARD | DRC_TRIGGER, plane2, plane);
			dp->drcc_next = dptrig;

			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &set1, why,
				    distance, DRC_REVERSE, plane2, plane);
			dptrig = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dptrig, 1, dpnew, &setN, &setC, why,
				    0, DRC_REVERSE | DRC_TRIGGER, plane2, plane);
			dp->drcc_next = dptrig;
		    }
		}
	    }
	}
    }
    return distance;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcRectOnly --
 *
 * Process a rectangle-only rule.  This rule prohibits non-rectangular
 * geometry, and is used especially for contacts, as the "squares" operator
 * in the CIF/GDS output generator can't handle non-rectangular areas.
 * The rule is of the form:
 *
 *	rect_only layers why
 *
 * and is equivalent to:
 *
 *	edge4way layers ~(layers)/plane 1 ~(layers)/plane (all_layers)/plane 1
 *
 * The rect_only rule avoids the above contrived construction, especially the
 * requirement of specifying "all_layers" as something like (~(x),x)/p, a sure-
 * fire obfuscation.
 *
 * ----------------------------------------------------------------------------
 */

int
drcRectOnly(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    char *why = drcWhyDup(argv[2]);
    TileTypeBitMask set1, set2, setC;
    PlaneMask pmask, pset, ptest;
    DRCCookie *dp, *dpnew;
    int plane;
    TileType i, j;

    ptest = DBTechNoisyNameMask(layers, &set1);
    pmask = CoincidentPlanes(&set1, ptest);

    if (pmask == 0)
    {
	TechError("All types for \"rect_only\"  must be on the same plane.\n");
	return (0);
    }

    /* set2 is the inverse of set1 */
    TTMaskCom2(&set2, &set1);

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;

	    if (pset = (DBTypesOnSamePlane(i, j) & pmask))
	    {
		if (TTMaskHasType(&set1, i) && TTMaskHasType(&set2, j))
		{
		    plane = LowestMaskBit(pset);
		    /* setC = all types in plane */
		    TTMaskZero(&setC);
		    TTMaskSetMask(&setC, &DBPlaneTypes[plane]);

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, 1);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew, 1, dp->drcc_next, &set2, &setC, why, 1,
				DRC_FORWARD | DRC_BOTHCORNERS, plane, plane);
		    dp->drcc_next = dpnew;

		    /* find bucket preceding new one we wish to insert */
		    dp = drcFindBucket(j, i, 1);
		    dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
		    drcAssign(dpnew,1,dp->drcc_next, &set2, &setC, why, 1,
				DRC_REVERSE | DRC_BOTHCORNERS, plane, plane);
		    dp->drcc_next = dpnew;
		}
	    }
	}
    }
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcSurround --
 *
 * Process a surround rule.
 * This is of the form:
 *
 *	surround layers1 layers2 dist presence why
 *
 * indicating that layers2 must surround layers1 by at least distance
 * dist in all directions.
 *
 * This rule is equivalent to:
 *
 *	edge4way ~(layers2)/plane2 layers2 dist ~(layers1)/plane1 \
 *		layers2 dist why
 *
 * When presence=absence_illegal, the following additional rule is needed:
 *
 *	edge4way layers1 ~(layers2)/plane1 dist NULL ~(layers2)/plane1 \
 *		dist why
 *
 * Extension added July 12, 2014:  For via rules where an asymmetric
 * surround is allowed, with a smaller surround allowed on two sides if
 * the remaining two sides have a larger surround.  This can be implemented
 * with a trigger rule, and is specified by the syntax above with "presence"
 * being "directional".  Note that the rule expresses that the overhang rule
 * requires the presence of the material on one side of a corner.  If the
 * other side has a non-zero minimum surround requirement, then it should
 * be implemented with an additional (absence_illegal) surround rule.
 * Otherwise, any width of material less than "dist" on one side of a
 * corner will trigger the rule requiring at least "dist" width of the same
 * material on the other side of the corner.
 *
 * ----------------------------------------------------------------------------
 */

int
drcSurround(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2 = argv[2];
    int distance = atoi(argv[3]);
    char *presence = argv[4];
    char *why = drcWhyDup(argv[5]);
    TileTypeBitMask set1, set2, setM, invM, setR;
    DRCCookie *dp, *dpnew, *dptrig;
    int plane1, plane2;
    PlaneMask pmask, pmask2, pset, ptest;
    TileType i, j;
    bool isExact = FALSE;
    bool isDirectional = FALSE;

    ptest = DBTechNoisyNameMask(layers1, &setM);
    pmask = CoincidentPlanes(&setM, ptest);
    if (pmask == 0)
    {
	TechError("Inside types in \"surround\" must be on the same plane\n");
	return (0);
    }

    ptest = DBTechNoisyNameMask(layers2, &set2);
    pmask2 = CoincidentPlanes(&set2, ptest);
    if (pmask2 == 0)
    {
	TechError("Outside types in \"surround\" must be on the same plane\n");
	return (0);
    }

    /* "exact_width" rule implemented 9/16/10.  This enforces an exact	*/
    /* surround distance.  "absence_illegal" is implied.  		*/

    if (!strncmp(presence, "exact_", 6)) isExact = TRUE;
    else if (!strncmp(presence, "directional", 11))
    {
	isDirectional = TRUE;
	/* Combined mask */
	TTMaskZero(&setR);
	TTMaskSetMask(&setR, &setM);
	TTMaskSetMask(&setR, &set2);
    }

    /* invert setM */
    TTMaskCom2(&invM, &setM);

    /* set1 is the inverse of set2 */
    TTMaskCom2(&set1, &set2);

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;	/* Ignore false edges */
	    if (pset = (DBTypesOnSamePlane(i, j) & pmask2))
	    {
		if (isDirectional)
		{
		    /* Directional surround is done entirely differently */

		    if (TTMaskHasType(&setM, i) && TTMaskHasType(&invM, j))
		    {
			plane1 = LowestMaskBit(pmask);
			plane2 = LowestMaskBit(pset);

			/* Find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));

			/* Insert triggered rule */
			drcAssign(dpnew, distance, dp->drcc_next, &setR,
				&DBAllTypeBits,
				why, distance,
				DRC_REVERSE | DRC_BOTHCORNERS,
				plane1, plane2);
			dptrig = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dptrig, distance, dpnew, &set2,
				&DBZeroTypeBits, why, 0,
				DRC_FORWARD | DRC_TRIGGER,
				plane1, plane2);
			dp->drcc_next = dptrig;

			/* And the other direction. . . */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));

			/* Insert triggered rule */
			drcAssign(dpnew, distance, dp->drcc_next, &setR,
				&DBAllTypeBits,
				why, distance,
				DRC_FORWARD | DRC_BOTHCORNERS,
				plane1, plane2);
			dptrig = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dptrig, distance, dpnew, &set2,
				&DBZeroTypeBits, why, 0,
				DRC_REVERSE | DRC_TRIGGER,
				plane1, plane2);
			dp->drcc_next = dptrig;
		    }
		}
		else
		{
		    if (TTMaskHasType(&set1, i) && TTMaskHasType(&set2, j))
		    {
			plane1 = LowestMaskBit(pmask);
			plane2 = LowestMaskBit(pset);

			/* Find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &invM, &set2,
				why, distance,
				DRC_FORWARD | DRC_BOTHCORNERS,
				plane1, plane2);
			dp->drcc_next = dpnew;

			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &invM, &set2,
				why, distance,
				DRC_REVERSE | DRC_BOTHCORNERS,
				plane1, plane2);
			dp->drcc_next = dpnew;
		    }
		}
	    }
	}
    }

    if (isExact)
    {
	for (i = 0; i < DBNumTypes; i++)
	{
	    for (j = 0; j < DBNumTypes; j++)
	    {
		if (i == j) continue;	/* Ignore false edges */
		if (pset = (DBTypesOnSamePlane(i, j) & pmask))
		{
		    if (TTMaskHasType(&setM, i) && TTMaskHasType(&set2, j))
		    {
			plane1 = LowestMaskBit(pset);

			/* Find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &set2,
					why, distance,
					DRC_FORWARD | DRC_BOTHCORNERS | DRC_OUTSIDE,
					plane1, plane1);
			dp->drcc_next = dpnew;

			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *)mallocMagic(sizeof(DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, &set1, &set2,
					why, distance,
					DRC_REVERSE | DRC_BOTHCORNERS | DRC_OUTSIDE,
					plane1, plane1);
			dp->drcc_next = dpnew;
		    }
		}
	    }
	}
    }

    if ((!isExact) && strcmp(presence, "absence_illegal")) return distance;

    /* Add an extra rule when presence of the surrounding	*/
    /* layer is required.  Rule is different if planes match.	*/

    if (pset = pmask & pmask2)
    {
	TTMaskZero(&invM);
	TTMaskSetMask(&invM, &setM);
	TTMaskSetMask(&invM, &set2);
	TTMaskCom(&invM);
	TTMaskZero(&set1);

	for (i = 0; i < DBNumTypes; i++)
	    for (j = 0; j < DBNumTypes; j++)
	    {
		if (i == j) continue;
	        if (pset = (DBTypesOnSamePlane(i, j) & pmask & pmask2))
		{
		    plane1 = LowestMaskBit(pset);
		    if (TTMaskHasType(&setM, i) && TTMaskHasType(&invM, j))
		    {
			/* Find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *) mallocMagic((unsigned)
					(sizeof (DRCCookie)));
	 		drcAssign(dpnew, distance, dp->drcc_next,
					&set1, &invM, why, distance,
					DRC_FORWARD | DRC_BOTHCORNERS,
					plane1, plane1);
			dp->drcc_next = dpnew;

			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *) mallocMagic((unsigned)
					(sizeof (DRCCookie)));
			drcAssign(dpnew,distance,dp->drcc_next,
					&set1, &invM, why, distance,
					DRC_REVERSE | DRC_BOTHCORNERS,
					plane1, plane1);
			dp->drcc_next = dpnew;
		    }
		}
	    }
    }
    else
    {
	for (i = 0; i < DBNumTypes; i++)
	    for (j = 0; j < DBNumTypes; j++)
	    {
		if (i == j) continue;
	        if (pset = (DBTypesOnSamePlane(i, j) & pmask))
		{
		    if (TTMaskHasType(&setM, i) && TTMaskHasType(&invM, j))
		    {
			plane1 = LowestMaskBit(pset);
			plane2 = LowestMaskBit(pmask2);

			/* Find bucket preceding the new one we wish to insert */
			dp = drcFindBucket(i, j, distance);
			dpnew = (DRCCookie *) mallocMagic((unsigned)
					(sizeof (DRCCookie)));
			drcAssign(dpnew, distance, dp->drcc_next,
					&set2, &invM, why, distance,
					DRC_FORWARD | DRC_BOTHCORNERS,
					plane2, plane1);
			dp->drcc_next = dpnew;

			/* find bucket preceding new one we wish to insert */
			dp = drcFindBucket(j, i, distance);
			dpnew = (DRCCookie *) mallocMagic((unsigned)
					(sizeof (DRCCookie)));
			drcAssign(dpnew,distance,dp->drcc_next,
					&set2, &invM, why, distance,
					DRC_REVERSE | DRC_BOTHCORNERS,
					plane2, plane1);
			dp->drcc_next = dpnew;
		    }
		}
	    }
    }

    return distance;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcNoOverlap --
 *
 * Process a no-overlap rule.
 * This is of the form:
 *
 *	no_overlap layers1 layers2
 *
 * e.g,
 *
 *	no_overlap poly m2contact
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcNoOverlap(argc, argv)
    int argc;
    char *argv[];
{
    char *layers1 = argv[1], *layers2 = argv[2];
    TileTypeBitMask set1, set2;
    TileType i, j;
    int plane;

    /*
     * Grab up two sets of tile types, and make sure that if
     * any type from one set is painted over any type from the
     * other, then an error results.
     */

    DBTechNoisyNameMask(layers1, &set1);
    DBTechNoisyNameMask(layers2, &set2);

    for (i = 0; i < DBNumTypes; i++)
	for (j = 0; j < DBNumTypes; j++)
	    if (TTMaskHasType(&set1, i) && TTMaskHasType(&set2, j))
		for (plane = 0; plane < DBNumPlanes; plane++)
		{
		    DRCCurStyle->DRCPaintTable[plane][j][i] = TT_ERROR_S;
		    DRCCurStyle->DRCPaintTable[plane][i][j] = TT_ERROR_S;
		}

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcExactOverlap --
 *
 * Process an exact overlap
 * This is of the form:
 *
 *	exact_overlap layers
 *
 * e.g,
 *
 *	exact_overlap pmc,dmc
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	Updates DRCExactOverlapTypes.
 *
 * ----------------------------------------------------------------------------
 */

int
drcExactOverlap(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    TileTypeBitMask set;

    /*
     * Grab up a bunch of tile types, and remember these: tiles
     * of these types cannot overlap themselves in different cells
     * unless they overlap exactly.
     */

    DBTechNoisyNameMask(layers, &set);
    TTMaskSetMask(&DRCCurStyle->DRCExactOverlapTypes, &set);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcRectangle --
 *
 * Process a rectangle rule.  This is of the form:
 *
 *	rectangle layers maxwidth [even|odd|any] why
 *
 * The rule checks to make sure that the region is rectangular and that the
 * width and length are even or odd, as specified.  These two criteria ensure 
 * that the squares rule of the cifout section can properly produce via 
 * holes without misaligning them between cells and without putting the via 
 * holes off grid.  The maxwidth is required to make the extent of this rule
 * a finite size, so that we can set the DRChalo to something finite.
 *
 * Results:
 *	maxwidth
 *
 * Side effects:
 *	Updates the DRC technology variables.
 *
 * ----------------------------------------------------------------------------
 */

int
drcRectangle(argc, argv)
    int argc;
    char *argv[];
{
    char *layers = argv[1];
    char *why = drcWhyDup(argv[4]);
    TileTypeBitMask types, nottypes;
    int maxwidth;
    static char *drcRectOpt[4] = {"any", "even", "odd", 0};
    int i, j, even, plane;
    PlaneMask pMask, pset, ptest;

    /* parse arguments */
    ptest = DBTechNoisyNameMask(layers, &types);
    pMask = CoincidentPlanes(&types, ptest);

    if (pMask == 0) {
	TechError("Layers in rectangle rule must lie in a single plane.");
	return 0;
    }
    TTMaskCom2(&nottypes, &types);

    if (sscanf(argv[2], "%d", &maxwidth) != 1) {
	TechError("bad maxwidth in rectangle rule");
	return 0;
    }
    even = Lookup(argv[3], drcRectOpt);
    if (even < 0) {
	TechError("bad [even|odd|any] selection in rectangle rule");
	return 0;
    }
    even--;  /* -1: any, 0: even, 1: odd */

    /* Install 2 edge rules: one that checks rectangle-ness, and one that
     * checks size
     */
    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (i == j) continue;

	    if (pset = (DBTypesOnSamePlane(i, j) & pMask))
	    {
		if (TTMaskHasType(&types, i) && TTMaskHasType(&nottypes, j))
		{
		    DRCCookie *dp, *dpnew;

		    plane = LowestMaskBit(pset);

		    /* 
		     * A rule that checks rectangle-ness. 
		     *   left:  oktypes, right: other types
		     * This rule needs to be checked in all 4 directions
		     */
		    int distance = 1;

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(i, j, distance);
		    dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
		    drcAssign(dpnew, distance, dp->drcc_next, 
				&nottypes, &DBAllTypeBits, why, distance, 
				DRC_FORWARD, plane, plane);
		    dp->drcc_next = dpnew;

		    /* Find bucket preceding the new one we wish to insert */
		    dp = drcFindBucket(j, i, distance); /* note: j, i not i, j */
		    dpnew = (DRCCookie *) mallocMagic((unsigned) (sizeof (DRCCookie)));
		    drcAssign(dpnew, distance, dp->drcc_next, 
		    		&nottypes, &DBAllTypeBits, why, distance, 
		    		DRC_REVERSE, plane, plane);
		    dp->drcc_next = dpnew;

		    if (maxwidth > 0) {
			/* 
		 	 * A rule that checks size.
		 	 *   left:  other types, right: oktypes
		 	 */
			distance = maxwidth;

			/* note: j, i not i, j */
			for (dp = DRCCurStyle->DRCRulesTbl[j][i];
				dp->drcc_next != (DRCCookie *) NULL &&
				dp->drcc_next->drcc_dist < distance;
				dp = dp->drcc_next); /* null body */

			dpnew = (DRCCookie *)mallocMagic(sizeof (DRCCookie));
			drcAssign(dpnew, distance, dp->drcc_next, 
				&types, &DBZeroTypeBits, why, even, 
				DRC_RECTSIZE, plane, plane);
			dp->drcc_next = dpnew;
		    }
		}
	    }
	}
    }
    return maxwidth;
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcStepSize --
 *
 * Process a declaration of the step size.
 * This is of the form:
 *
 *	stepsize step_size
 *
 * e.g,
 *
 *	stepsize 1000
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	Updates DRCStepSize.
 *
 * ----------------------------------------------------------------------------
 */

int
drcStepSize(argc, argv)
    int argc;
    char *argv[];
{
    if (DRCCurStyle == NULL) return 0;

    DRCCurStyle->DRCStepSize = atoi(argv[1]);
    if (DRCCurStyle->DRCStepSize <= 0)
    {
	TechError("Step size must be a positive integer.\n");
	DRCCurStyle->DRCStepSize = 0;
    }
    else if (DRCCurStyle->DRCStepSize < 16)
    {
	TechError("Warning: abnormally small DRC step size (%d)\n",
		DRCCurStyle->DRCStepSize);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCTechFinal --
 *
 * Called after all lines of the drc section in the technology file have been
 * read.  Ensures that a valid style is in effect, and then calls
 * drcTechFinalStyle().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See drcTechFinalStyle();
 *
 * ----------------------------------------------------------------------------
 */

void
DRCTechFinal()
{
    DRCStyle *ds;

    /* Create a "default" style if there isn't one */

    if (DRCStyleList == NULL)
    {
	DRCStyleList = (DRCKeep *)mallocMagic(sizeof(DRCKeep));
	DRCStyleList->ds_next = NULL;
	DRCStyleList->ds_name = StrDup((char **)NULL, "default");

	drcTechNewStyle();
	DRCCurStyle->ds_name = DRCStyleList->ds_name;
	DRCCurStyle->ds_status = TECH_LOADED;
    }
    drcTechFinalStyle(DRCCurStyle);
}

/*
 * ----------------------------------------------------------------------------
 * drcScaleDown ---
 *
 *	DRC distances may be specified with a scale factor so that physically
 *	based rules can be recorded, but the rules used will be rounded (up)
 *	to the nearest lambda.  The fractional part of the true distance in
 *	lambda is saved, so that the original value can be recovered when
 *	the magic grid is rescaled.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Scales all the DRC distances by dividing by the DRC scale factor.
 * ----------------------------------------------------------------------------
 */

void
drcScaleDown(style, scalefactor)
    DRCStyle *style;
    int scalefactor;
{
    TileType i, j;
    DRCCookie  *dp;
    int dist;

    if (scalefactor > 1)
    {
	for (i = 0; i < TT_MAXTYPES; i++)
	    for (j = 0; j < TT_MAXTYPES; j++)
		for (dp = style->DRCRulesTbl[i][j]; dp != NULL; dp = dp->drcc_next)
		{
		    if  (dp->drcc_dist > 0)
		    {
			dist = dp->drcc_dist;
			dp->drcc_dist /= scalefactor;
			if ((dp->drcc_mod = (unsigned char)(dist % scalefactor)) != 0)
			    if (!(dp->drcc_flags & DRC_MAXWIDTH))
				dp->drcc_dist++;
		    }
		    if  (dp->drcc_cdist > 0)
		    {
			int locscale = scalefactor;
			if (dp->drcc_flags & DRC_AREA)
			    locscale *= scalefactor;

			dist = dp->drcc_cdist;
			dp->drcc_cdist /= locscale;
			if ((dp->drcc_cmod = (unsigned char)(dist % locscale)) != 0)
			    dp->drcc_cdist++;
		    }
		}
    }
}

/*
 * ----------------------------------------------------------------------------
 * drcScaleUp ---
 *
 *	Recovers the original (pre-scaled) values for drcc_dist and
 *	drcc_cdist in the DRC cookies.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Scales all the DRC distances by multiplying by the DRC scale factor.
 * ----------------------------------------------------------------------------
 */

void
drcScaleUp(style, scalefactor)
    DRCStyle *style;
    int scalefactor;
{
    TileType i, j;
    DRCCookie  *dp;
    int dist;

    if (style == NULL) return;

    if (scalefactor > 1)
    {
	for (i = 0; i < TT_MAXTYPES; i++)
	    for (j = 0; j < TT_MAXTYPES; j++)
		for (dp = style->DRCRulesTbl[i][j]; dp != NULL; dp = dp->drcc_next)
		{
		    if  (dp->drcc_dist > 0)
		    {
			dist = dp->drcc_dist;
			if (dp->drcc_mod != 0)
			    if (!(dp->drcc_flags & DRC_MAXWIDTH))
				dp->drcc_dist--;
			dp->drcc_dist *= scalefactor;
			dp->drcc_dist += (short)dp->drcc_mod;
			dp->drcc_mod = 0;
		    }
		    if  (dp->drcc_cdist > 0)
		    {
			dist = dp->drcc_cdist;
			if (dp->drcc_cmod != 0)
			    dp->drcc_cdist--;
			dp->drcc_cdist *= scalefactor;
			if (dp->drcc_flags & DRC_AREA)
			    dp->drcc_cdist *= scalefactor;
			dp->drcc_cdist += (short)dp->drcc_cmod;
			dp->drcc_cmod = 0;
		    }
		}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * drcTechFinalStyle --
 *
 * Called after all lines of the drc section in the technology file have been
 * read.  The preliminary DRC Rules Table is pruned by removing rules covered
 * by other (longer distance) rules, and by removing the dummy rule at the
 * front of each list.  Where edges are completely illegal, the rule list is
 * pruned to a single rule.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May remove DRCCookies from the linked lists of the DRCRulesTbl.
 *
 * ----------------------------------------------------------------------------
 */

void
drcTechFinalStyle(style)
   DRCStyle *style;
{
    TileTypeBitMask tmpMask, nextMask;
    DRCCookie  *dummy, *dp, *next, *dptrig;
    DRCCookie **dpp, **dp2back;
    TileType i, j;

    /* If the scale factor is not 1, then divide all distances by	*/
    /* the scale factor, take the ceiling, and save the (negative)	*/
    /* remainder.							*/

    drcScaleUp(style, style->DRCScaleFactorD);
    drcScaleDown(style, style->DRCScaleFactorN);

    /* Set maximum halo */
    style->DRCTechHalo = DRCTechHalo;

    /* A reasonable chunk size for design-rule checking is about
     * 16 times the maximum design-rule interaction distance.  This
     * results in a halo overhead of about 27%.  If there's no DRC  
     * information at all (TechHalo is zero), just pick any size.
     * (Update 1/13/09:  "any size" needs a bit of modification,
     * because 64 will be way too small for a layout with a small
     * scalefactor.  Assuming that the CIF output style is valid,
     * use its scalefactor to adjust the step size).
     */
    if (style->DRCStepSize == 0)
    {
	if (style->DRCTechHalo == 0)
	{
	    if (CIFCurStyle != NULL)
		style->DRCStepSize = 6400 / CIFCurStyle->cs_scaleFactor;
	    else
		style->DRCStepSize = 64;
	}
	else
	    style->DRCStepSize = 16 * style->DRCTechHalo;
    }
    DRCStepSize = style->DRCStepSize;

    /* Remove dummy buckets */
    for (i = 0; i < TT_MAXTYPES; i++)
    {
	for (j = 0; j < TT_MAXTYPES; j++)
	{
	    dpp = &(style->DRCRulesTbl [i][j]);
	    dummy = *dpp;
	    *dpp = dummy->drcc_next;
	    freeMagic((char *) dummy); 
	}
    }
    drcCifFinal();

    if (!DRCRuleOptimization) return;

    /* Check for edges that are completely illegal.  Where this is the
     * case, eliminate all of the edge's rules except one.
     */
    
    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    DRCCookie *keep = NULL, *dptest, *dptemp, *dpnew;
	    
	    for (dp = style->DRCRulesTbl[i][j]; dp != NULL; dp = dp->drcc_next)
	    {
		if (dp->drcc_flags & (DRC_NONSTANDARD || DRC_OUTSIDE)) continue;
		if (dp->drcc_flags & DRC_REVERSE)
		{
		    if ((i == TT_SPACE) || TTMaskHasType(&dp->drcc_mask, i)) continue;
		}
		else
		{
		    if ((j == TT_SPACE) || TTMaskHasType(&dp->drcc_mask, j)) continue;
		}

		/* Rules where okTypes are in a different plane don't count,	*/
		/* unless i or j also appear in the checked plane.		*/

		if (dp->drcc_plane != dp->drcc_edgeplane)
		{
		    if (dp->drcc_flags & DRC_REVERSE)
		    {
			if ((i == TT_SPACE) || !DBTypeOnPlane(i, dp->drcc_plane))
			    continue;
		    }
		    else
		    {
			if ((j == TT_SPACE) || !DBTypeOnPlane(j, dp->drcc_plane))
			    continue;
		    }
		}

		// if (DBIsContact(i) || DBIsContact(j) || i == TT_SPACE ||
		//	j == TT_SPACE) continue;

		dpnew = NULL;
		keep = dp;

		/* This edge is illegal.  Throw away all rules except the one
		 * needed that is always violated.
		 */
	    
		dptest = style->DRCRulesTbl[i][j];
		while (dptest != NULL)
		{
		    dptemp = dptest->drcc_next;
		    if ((dptest == keep) || (dptest->drcc_edgeplane !=
				keep->drcc_edgeplane))
		    {
			dptest->drcc_next = NULL;
			if (dpnew == NULL)
			    style->DRCRulesTbl[i][j] = dptest;
			else
			    dpnew->drcc_next = dptest;
			dpnew = dptest;

			/* "keep" can't be a trigger rule! */
			if (dptest == keep)
			    keep->drcc_flags &= ~DRC_TRIGGER;
		    }
		    else
		    {
			/* Don't free the shared drcc_why string here! */
			freeMagic((char *)dptest);
			drcRulesOptimized++;
		    }
		    dptest = dptemp;
		}
	    }

	    /* TxPrintf("Edge %s-%s is illegal.\n", DBTypeShortName(i),
		DBTypeShortName(j));
	    */
	}
    }

    /*
     * Remove any rule A "covered" by another rule B, i.e.,
     *		B's distance >= A's distance,
     *		B's corner distance >= A's corner distance,
     *		B's RHS type mask is a subset of A's RHS type mask, and
     *		B's corner mask == A's corner mask
     *		B's check plane == A's check plane
     *		either both A and B or neither is a REVERSE direction rule
     *		if A is BOTHCORNERS then B must be, too
     */

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    for (dp = style->DRCRulesTbl[i][j]; dp != NULL; dp = dp->drcc_next)
	    {
		/* Don't optimize on trigger rules; optimize on the	*/
		/* rule that gets triggered.				*/
		if (dp->drcc_flags & DRC_TRIGGER)
		{
		    dptrig = dp;
		    dp = dp->drcc_next;
		}
		else
		    dptrig = NULL;
		
		/*
		 * Check following buckets to see if any is a superset.
		 */
		if (dp->drcc_flags & DRC_NONSTANDARD) continue;
		for (next = dp->drcc_next; next != NULL;
			next = next->drcc_next)
		{
		    if (next->drcc_flags & DRC_TRIGGER)
		    {
			/* A triggered rule cannot be considered */
			/* a superset of a non-triggered rule or */
			/* a rule with a different trigger, so   */
			/* we skip all triggered rules and their */
			/* triggering rule.			 */
			next = next->drcc_next;
			continue;
		    }
		    tmpMask = nextMask = next->drcc_mask;
		    TTMaskAndMask(&tmpMask, &dp->drcc_mask);
		    if (!TTMaskEqual(&tmpMask, &nextMask)) continue;
		    if (!TTMaskEqual(&dp->drcc_corner, &next->drcc_corner))
			continue;
		    if (dp->drcc_dist > next->drcc_dist) continue;
		    if (dp->drcc_cdist > next->drcc_cdist) continue;
		    if (dp->drcc_plane != next->drcc_plane) continue;
		    if (dp->drcc_flags & DRC_REVERSE)
		    {
			if (!(next->drcc_flags & DRC_REVERSE)) continue;
		    }
		    else if (next->drcc_flags & DRC_REVERSE) continue;
		    if ((next->drcc_flags & DRC_BOTHCORNERS)
			    && (dp->drcc_flags & DRC_BOTHCORNERS) == 0)
			continue;
		    if (next->drcc_flags & DRC_NONSTANDARD) continue;
		    if (dp->drcc_dist == next->drcc_dist)
		    {
			if ((next->drcc_flags & DRC_OUTSIDE) &&
				!(dp->drcc_flags & DRC_OUTSIDE)) continue;
			if (!(next->drcc_flags & DRC_OUTSIDE) &&
				(dp->drcc_flags & DRC_OUTSIDE)) continue;
		    }
		    break;
		}

		if (next == NULL) continue;

		/* "dp" is a subset of "next".  Eliminate it. */

		/* For triggered rules, eliminate both the rule */
		/* and the trigger.				*/
		if (dptrig != NULL) dp = dptrig;

		/* TxPrintf("For edge %s-%s, \"%s\" covers \"%s\"\n",
		    DBTypeShortName(i), DBTypeShortName(j),
		    next->drcc_why, dp->drcc_why);
		*/
		dp2back = &(style->DRCRulesTbl[i][j]);
		while (*dp2back != dp)
		    dp2back = &(*dp2back)->drcc_next;

		/* Trigger rules */
		if (dptrig != NULL)
		{
		    dptrig = dp->drcc_next;
		    freeMagic((char *)dp->drcc_next);
		    *dp2back = dp->drcc_next->drcc_next;

		    /* Replace this entry so on the next cycle	*/
		    /* dp will be the next rule.  This works	*/
		    /* even though dp is free'd (below), due to	*/
		    /* the one-delayed free mechanism.		*/
		    dp->drcc_next = *dp2back;
		}
		else
		    *dp2back = dp->drcc_next;

		/* Don't free the shared drcc_why string here! */
		freeMagic((char *) dp);
		drcRulesOptimized += 1;
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * DRCTechRuleStats --
 *
 * 	Print out some statistics about the design rule database.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A bunch of stuff gets printed on the terminal.
 *
 * ----------------------------------------------------------------------------
 */

#define MAXBIN 10

void
DRCTechRuleStats()
{
    int counts[MAXBIN+1];
    int edgeRules, overflow;
    int i, j;
    DRCCookie *dp;

    /* Count up the total number of edge rules, and histogram them
     * by the number of rules per edge.
     */
    
    edgeRules = 0;
    overflow = 0;
    for (i=0; i<=MAXBIN; i++) counts[i] = 0;

    for (i=0; i<DBNumTypes; i++)
	for (j=0; j<DBNumTypes; j++)
	{
	    int thisCount = 0;
	    for (dp = DRCCurStyle->DRCRulesTbl[i][j]; dp != NULL; dp = dp->drcc_next)
		thisCount++;
	    edgeRules += thisCount;
	    if (!DBTypesOnSamePlane(i, j)) continue;
	    if (thisCount <= MAXBIN) counts[thisCount] += 1;
	    else overflow += 1;
	}
    
    /* Print out the results. */

    TxPrintf("Total number of rules specifed in tech file: %d\n",
	drcRulesSpecified);
    TxPrintf("Edge rules optimized away: %d\n", drcRulesOptimized);
    TxPrintf("Edge rules left in database: %d\n", edgeRules);
    TxPrintf("Histogram of # edges vs. rules per edge:\n");
    for (i=0; i<=MAXBIN; i++)
    {
	TxPrintf("  %2d rules/edge: %d.\n", i, counts[i]);
    }
    TxPrintf(" >%2d rules/edge: %d.\n", MAXBIN, overflow);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DRCTechScale --
 *
 * 	Multiply all DRC rule widths and spacings by a factor of scaled/scalen.
 *	(Don't need to use DBScaleValue() because all values must be positive
 *	and cannot be (M)INFINITY.)
 *
 * ----------------------------------------------------------------------------
 */

void
DRCTechScale(scalen, scaled)
    int scalen, scaled;
{
    DRCCookie  *dp;
    TileType i, j;
    int scalegcf;

    if (DRCCurStyle == NULL) return;
    else if (scalen == scaled == 1) return;

    /* Revert DRC rules to original (unscaled) values */
    drcScaleUp(DRCCurStyle, DRCCurStyle->DRCScaleFactorN);
    drcScaleDown(DRCCurStyle, DRCCurStyle->DRCScaleFactorD);

    DRCCurStyle->DRCScaleFactorD *= scaled;
    DRCCurStyle->DRCScaleFactorN *= scalen;

    /* Reduce scalefactor ratio by greatest common factor */
    scalegcf = FindGCF(DRCCurStyle->DRCScaleFactorD, DRCCurStyle->DRCScaleFactorN);
    DRCCurStyle->DRCScaleFactorD /= scalegcf;
    DRCCurStyle->DRCScaleFactorN /= scalegcf;

    /* Rescale all rules to the new scalefactor */
    drcScaleUp(DRCCurStyle, DRCCurStyle->DRCScaleFactorD);
    drcScaleDown(DRCCurStyle, DRCCurStyle->DRCScaleFactorN);

    DRCTechHalo *= scaled;
    DRCTechHalo /= scalen;

    DRCStepSize *= scaled;
    DRCStepSize /= scalen;

    DRCCurStyle->DRCTechHalo *= scaled;
    DRCCurStyle->DRCTechHalo /= scalen;

    DRCCurStyle->DRCStepSize *= scaled;
    DRCCurStyle->DRCStepSize /= scalen;
}

/* The following routines are used by the "tech" command (and in other places,
 * such as the LEF file reader) to query the DRC database.
 */

/*
 *-----------------------------------------------------------------------------
 *  DRCGetDefaultLayerWidth ---
 *
 *	Determine a default layer width from the DRC width rules
 *	of a layer.  Continue processing until we have processed all
 *	rules, since rules are ordered from shortest to longest distance,
 *	and the maximum distance rule will mask any rules with a shorter
 *	distance.
 *
 * Results:
 *	The minimum width of the magic layer, in magic internal units
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

int
DRCGetDefaultLayerWidth(ttype)
    TileType ttype;
{
    int routeWidth = 0;
    DRCCookie *cptr;
    TileTypeBitMask *set;

    for (cptr = DRCCurStyle->DRCRulesTbl[TT_SPACE][ttype]; cptr != (DRCCookie *) NULL;
	cptr = cptr->drcc_next)
    {
	/* FORWARD rules only, and no MAXWIDTH */
	if ((cptr->drcc_flags & (DRC_REVERSE | DRC_MAXWIDTH)) == 0)
	{
	    set = &cptr->drcc_mask;
	    if (TTMaskHasType(set, ttype) && TTMaskEqual(set, &cptr->drcc_corner))
		if ((cptr->drcc_plane == DBPlane(ttype)) &&
			(cptr->drcc_dist == cptr->drcc_cdist))
		{
		    routeWidth = cptr->drcc_dist;
		    /* Diagnostic */
		    /*
		    TxPrintf("DRC: Layer %s has default width %d\n",
		    DBTypeLongNameTbl[ttype], routeWidth);
		    */
		}
	}
    }
    return routeWidth;
}

/*
 *-----------------------------------------------------------------------------
 *  DRCGetDefaultLayerSpacing ---
 *
 *	Determine a default layer-to-layer spacing from the DRC width
 *	rules of a layer.  Continue processing all rules, since rules
 *	are ordered from shortest to longest distance, and the largest
 *	distance matching the criteria sets the rule.
 *
 * Results:
 *	The minimum spacing between the specified magic layer types,
 *	in magic internal units
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

int
DRCGetDefaultLayerSpacing(ttype1, ttype2)
    TileType ttype1, ttype2;
{
    int routeSpacing = 0;
    DRCCookie *cptr;
    TileTypeBitMask *set;

    for (cptr = DRCCurStyle->DRCRulesTbl[ttype1][TT_SPACE]; cptr != (DRCCookie *) NULL;
	cptr = cptr->drcc_next)
    {
	if (cptr->drcc_flags & DRC_TRIGGER) {		/* Skip widespacing rules */
	    cptr = cptr->drcc_next;
	    continue;
	}
	if ((cptr->drcc_flags & DRC_REVERSE) == 0)	/* FORWARD only */
	{
	    set = &cptr->drcc_mask;
	    if (!TTMaskHasType(set, ttype2))
	        if (PlaneMaskHasPlane(DBTypePlaneMaskTbl[ttype2], cptr->drcc_plane) &&
			(cptr->drcc_dist == cptr->drcc_cdist))
		{
		    routeSpacing = cptr->drcc_dist;
		    /* Diagnostic */
		    /*
		    TxPrintf("DRC: Layer %s has default spacing %d to layer %s\n",
			DBTypeLongNameTbl[ttype1], routeSpacing,
			DBTypeLongNameTbl[ttype2]);
		    */
		}
	}
    }
    return routeSpacing;
}

/*
 *-----------------------------------------------------------------------------
 *  DRCGetDefaultLayerSurround ---
 *
 *	Determine the default minimum required surround amount
 *	of layer type 2 around layer type 1.
 *	Continue processing all rules, since rules are ordered from
 *	shortest to longest distance, and the largest value of the
 *	surround material sets the minimum required width.
 *
 * Results:
 *	The minimum spacing between the specified magic layer types,
 *	in magic internal units
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

int
DRCGetDefaultLayerSurround(ttype1, ttype2)
    TileType ttype1, ttype2;
{
    int layerSurround = 0;
    DRCCookie *cptr;
    TileTypeBitMask *set;

    for (cptr = DRCCurStyle->DRCRulesTbl[ttype1][TT_SPACE]; cptr != (DRCCookie *) NULL;
	cptr = cptr->drcc_next)
    {
	if ((cptr->drcc_flags & DRC_REVERSE) == 0)	/* FORWARD only */
	{
	    set = &cptr->drcc_mask;
	    if (!TTMaskHasType(set, TT_SPACE))
	        if (PlaneMaskHasPlane(DBTypePlaneMaskTbl[ttype2], cptr->drcc_plane) &&
			(cptr->drcc_dist == cptr->drcc_cdist))
		{
		    layerSurround = cptr->drcc_dist;
		    /* Diagnostic */
		    /*
		    TxPrintf("DRC: Layer %s has default surround %d over layer %s\n",
			DBTypeLongNameTbl[ttype2], layerSurround,
			DBTypeLongNameTbl[ttype1]);
		    */
		}
	}
    }
    return layerSurround;
}
