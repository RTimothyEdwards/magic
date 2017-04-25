/*
 * DBtech.c --
 *
 * Technology initialization for the database module.
 * This file handles overall initialization, construction
 * of the general-purpose exported TileTypeBitMasks, and
 * the "connect" section.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBtech.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "utils/malloc.h"

/* Name of this technology */
char *DBTechName = 0;
char *DBTechVersion = 0;
char *DBTechDescription = 0;

/* Connectivity */
TileTypeBitMask	 DBConnectTbl[NT];
TileTypeBitMask	 DBNotConnectTbl[NT];
PlaneMask	 DBConnPlanes[NT];
PlaneMask	 DBAllConnPlanes[NT];

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechInit --
 *
 * Clear technology description information for database module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechInit()
{
    /* StrDup() takes care of reallocating DBTechName */

    /* TECH_FORMAT_VERSION is defined in utils/tech.h,	*/
    /* and should be 27, the last version to be		*/
    /* included as part of the tech filename suffix.	*/

    TechFormatVersion = TECH_FORMAT_VERSION;

    /* Initialization of bezier coefficients for font vectors */
    DBFontInitCurves();
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBTechSetTech --
 *
 * Set the name for the technology.
 *
 * Results:
 *	Returns FALSE if there were an improper number of
 *	tokens on the line.
 *
 * Side effects:
 *	Sets DBTechName to the name of the technology.
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
bool
DBTechSetTech(sectionName, argc, argv)
    char *sectionName;
    int argc;
    char *argv[];
{
    if (argc != 1)
    {
	if (argc == 2)
	{
	    if (strncmp(argv[0], "format", 6) == 0 ||
			strncmp(argv[0], "version", 7) == 0)
	    {
		if (StrIsInt(argv[1]))
		{
		    TechFormatVersion = atoi(argv[1]);	
		    return TRUE;
		}
		else
		{
		    TechError("Bad format version number. . . assuming %d\n",
				TECH_FORMAT_VERSION);
		    return TRUE;
		}
	    }
	}
	TechError("Badly formed technology name\n");
	return FALSE;
    }
    (void) StrDup(&DBTechName, argv[0]);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechInitVersion --
 *
 *	Clean up memory allocated by the "version" section
 * 
 * ----------------------------------------------------------------------------
 */

void
DBTechInitVersion()
{
    /* StrDup() takes care of reallocating DBTechVersion and	*/
    /* DBTechDescription.					*/
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechSetVersion --
 *
 * Set the version number & description for the technology.
 *
 * Results:
 *	Returns FALSE if there were an improper number of
 *	tokens on the line.
 *
 * Side effects:
 *	Sets DBTechVersion and DBTechDescription.
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
bool
DBTechSetVersion(sectionName, argc, argv)
    char *sectionName;
    int argc;
    char *argv[];
{
    char *contline;
    int n, slen;

    if (argc < 2) goto usage;
    if (strcmp(argv[0], "version") == 0)
    {
	(void) StrDup(&DBTechVersion, argv[1]);
	for (n = 2; n < argc; n++)
	{
	    slen = strlen(DBTechVersion);
	    contline = mallocMagic(strlen(argv[n]) + slen + 1);
	    sprintf(contline, "%s\n%s", DBTechVersion, argv[n]);
	    freeMagic(DBTechVersion);
	    DBTechVersion = contline;
	}
	return TRUE;
    }
    if (strcmp(argv[0], "description") == 0)
    {
	(void) StrDup(&DBTechDescription, argv[1]);
	for (n = 2; n < argc; n++)
	{
	    slen = strlen(DBTechDescription);
	    contline = mallocMagic(strlen(argv[n]) + slen + 1);
	    sprintf(contline, "%s\n%s", DBTechDescription, argv[n]);
	    freeMagic(DBTechDescription);
	    DBTechDescription = contline;
	}
	return TRUE;
    }

usage:
    TechError("Badly formed version line\nUsage: {version text}|{description text}\n");
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechInitConnect --
 *
 * Initialize the connectivity tables.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes DBConnectTbl[], DBConnPlanes[], and DBAllConnPlanes[].
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechInitConnect()
{
    int i;

    for (i = 0; i < TT_MAXTYPES; i++)
    {
	TTMaskSetOnlyType(&DBConnectTbl[i], i);
	DBConnPlanes[i] = 0;
	DBAllConnPlanes[i] = 0;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechAddConnect --
 *
 * Add connectivity information.
 * Record the fact that material of the types in the comma-separated
 * list types1 connects to material of the types in the list types2.
 *
 * Results:
 *	TRUE if successful, FALSE on error
 *
 * Side effects:
 *	Updates DBConnectTbl[].
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
bool
DBTechAddConnect(sectionName, argc, argv)
    char *sectionName;
    int argc;
    char *argv[];
{
    TileTypeBitMask types1, types2;
    TileType t1, t2;

    if (argc != 2)
    {
	TechError("Line must contain exactly 2 lists of types\n");
	return FALSE;
    }

    DBTechNoisyNameMask(argv[0], &types1);
    DBTechNoisyNameMask(argv[1], &types2);
    for (t1 = 0; t1 < DBNumTypes; t1++)
	if (TTMaskHasType(&types1, t1))
	    for (t2 = 0; t2 < DBNumTypes; t2++)
		if (TTMaskHasType(&types2, t2))
		{
		    TTMaskSetType(&DBConnectTbl[t1], t2);
		    TTMaskSetType(&DBConnectTbl[t2], t1);
		}


    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTechFinalConnect --
 *
 * Postprocessing for the connectivity information.
 * Modify DBConnectTbl[] so that:
 *
 *	(1) Any type connecting to one of the images of a contact
 *	    connects to all images of the contact.
 *	(2) Each image of a contact connects to the union of what
 *	    all the images connect to.
 *
 * Modify DBConnPlanes[] so that only types belonging to a contact
 * appear to connect to any plane other than their own.
 *
 * Constructs DBAllConnPlanes, which will be non-zero for those planes
 * to which each type connects, exclusive of that type's home plane and
 * those planes to which it connects as a contact.
 *
 * Create DBNotConnectTbl[], the complement of DBConnectTbl[].
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies DBConnPlanes[], DBAllConnPlanes[], and DBConnectTbl[]
 *	as above.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTechFinalConnect()
{
    /* TileTypeBitMask saveConnect[TT_MAXTYPES]; */
    TileTypeBitMask *cMask, *rMask;
    TileType base, s;
    LayerInfo *lp, *ls;
    int n;

    for (s = 0; s < DBNumTypes; s++)
	DBConnPlanes[s] = 0;

    /*
     * Each stacked contact type must necessarily connect to its
     * residual contact types, and the connecting types of those
     * contacts.  Each stacked contact type also connects to
     * other contact types that share (first) residues.
     */
    for (base = DBNumUserLayers; base < DBNumTypes; base++)
    {
	TileTypeBitMask *smask, *rmask = DBResidueMask(base), cmask;
	TTMaskSetMask(&DBConnectTbl[base], rmask);
	for (s = TT_TECHDEPBASE; s < DBNumUserLayers; s++)
	    if (TTMaskHasType(rmask, s))
		TTMaskSetMask(&DBConnectTbl[base], &DBConnectTbl[s]);

	/* Only need to start above "base";  the mirror-image	*/
	/* connection is ensured by the code below.		*/

	for (s = base + 1; s < DBNumTypes; s++)
	{
	    smask = DBResidueMask(s);
	    TTMaskAndMask3(&cmask, smask, rmask);
	    if (!TTMaskIsZero(&cmask))
		TTMaskSetType(&DBConnectTbl[base], s);
	}
    }

    /* Make the connectivity matrix symmetric */
    for (base = TT_TECHDEPBASE; base < DBNumTypes; base++)
	for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	    if (TTMaskHasType(&DBConnectTbl[base], s))
		TTMaskSetType(&DBConnectTbl[s], base);

    /* Construct DBNotConnectTbl[] */
    /* For purposes of connectivity searching, this is not exactly the	*/
    /* complement of DBConnectTbl[].  For non-contact layers, it is.	*/
    /* For contact images, we compute differently to account for	*/
    /* contacts being stacked by having different parts in different	*/
    /* cells, which requires a different mask.				*/

    for (base = 0; base < TT_MAXTYPES; base++)
	TTMaskCom2(&DBNotConnectTbl[base], &DBConnectTbl[base]);

    for (n = 0; n < dbNumContacts; n++)
    {
	lp = dbContactInfo[n];
	TTMaskSetOnlyType(&DBNotConnectTbl[lp->l_type], lp->l_type);
	rMask = DBResidueMask(lp->l_type);

	/* Different contact types may share residues.		*/
	/* Use TTMaskIntersect(), not TTMaskEqual()---types	*/
	/* which otherwise stack may be in separate cells.	*/

	for (s = 0; s < dbNumContacts; s++)
	{
	    ls = dbContactInfo[s];
	    cMask = DBResidueMask(ls->l_type);
	    if (TTMaskIntersect(rMask, cMask))
		TTMaskSetType(&DBNotConnectTbl[lp->l_type], ls->l_type);
	}

	/* The mask of contact types must include all stacked contacts */

	for (base = DBNumUserLayers; base < DBNumTypes; base++)
	{
	    cMask = DBResidueMask(base);
	    if (TTMaskHasType(cMask, lp->l_type))
		TTMaskSetType(&DBNotConnectTbl[lp->l_type], base);
	}

	/* Note that everything above counted for all the	*/
	/* connecting types.  Invert this to get non-connecting	*/
	/* types.						*/

	TTMaskCom(&DBNotConnectTbl[lp->l_type]);
    }

    /*
     * DBConnPlanes[] is nonzero only for contact images, for which
     * it shows the planes connected to an image.  It is the
     * responsibility of the routine examining this value to exclude
     * the plane being searched, if necessary.
     */
    for (n = 0; n < dbNumContacts; n++)
    {
	lp = dbContactInfo[n];
	DBConnPlanes[lp->l_type] = lp->l_pmask;
    }

    /*
     * Now finally construct DBAllConnPlanes, which will be non-zero
     * for those planes to which each type 'base' connects, exclusive
     * of its home plane and those planes to which it connects as a
     * contact.
     */
    for (base = TT_TECHDEPBASE; base < DBNumTypes; base++)
    {
	DBAllConnPlanes[base] = DBTechTypesToPlanes(&DBConnectTbl[base]);
	DBAllConnPlanes[base] &= ~(PlaneNumToMaskBit(DBPlane(base)));
	DBAllConnPlanes[base] &= ~DBConnPlanes[base];
    }
}
