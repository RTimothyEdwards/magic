/* rtrTech.c -
 *
 *	This file processes the part of technology files that
 *	provides information to the router.  It sets up information
 *	giving, for example, the layers to use for routing, their
 *	widths, and so on.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrTech.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "router/router.h"

/* The global routing variables are defined below.  See router.h
 * for a description of what they mean.
 */

/* These are used by non-router modules (maybe they shouldn't be?) so	*/
/* they are defined in dbwind/DBWdisplay.c, one of the places where	*/
/* they are used.							*/

/* int RtrMetalWidth, RtrPolyWidth, RtrContactWidth; */

TileType RtrMetalType, RtrPolyType, RtrContactType;
int RtrContactOffset;
int RtrMetalSurround, RtrPolySurround;
int RtrGridSpacing;
int RtrSubcellSepUp, RtrSubcellSepDown;
TileTypeBitMask RtrMetalObstacles, RtrPolyObstacles;
int RtrPaintSepsUp[TT_MAXTYPES], RtrPaintSepsDown[TT_MAXTYPES];

/* The arrays below are used to hold the obstacle information separately
 * for the two routing layers.  These are used temporarily while reading
 * the technology file, but also used directly by the maze router.  Eventually
 * the info is combined and put into RtrPaintSepsUp and RtrPaintSepsDown.
 */

int RtrMetalSeps[TT_MAXTYPES], RtrPolySeps[TT_MAXTYPES];


/*
 * ----------------------------------------------------------------------------
 *
 * RtrTechInit --
 *
 * 	This routine is called once just before reading the router
 *	section of the technology file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the data structures.  The main idea is to make
 *	things consistent so that if there is an empty router section,
 *	the router won't crash if it's invoked (it probably won't
 *	generate nice routing, but at least it won't core dump).
 *
 * ----------------------------------------------------------------------------
 */

void
RtrTechInit()
{
    int i;
    RtrMetalType = RtrPolyType = RtrContactType = TT_SPACE;
    RtrMetalWidth = RtrPolyWidth = RtrContactWidth = 2;
    RtrContactOffset = 0;
    RtrMetalSurround = 0;
    RtrPolySurround = 0;
    RtrGridSpacing = 4;
    RtrSubcellSepUp = 4;
    RtrSubcellSepDown = 4;
    TTMaskZero(&RtrMetalObstacles);
    TTMaskZero(&RtrPolyObstacles);
    for (i=0; i<TT_MAXTYPES; i++)
    {
	RtrMetalSeps[i] = 0;
	RtrPolySeps[i] = 0;
	RtrPaintSepsUp[i] = 0;
	RtrPaintSepsDown[i] = 0;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrTechLine --
 *
 * 	This procedure is called once for each line in the router
 *	section of the technology file.
 *
 * Results:
 *	TRUE result means the line was parse-able.  FALSE means the
 *	line was so fundamentally bad that Magic should abort after
 *	reading the whole technology file.
 *
 * Side effects:
 *	Based on the information in the line, overall routing control
 *	information is set up.
 *
 * ----------------------------------------------------------------------------
 */

bool
RtrTechLine(sectionName, argc, argv)
    char *sectionName;			/* Name of this section. */
    int argc;				/* Number of fields on line. */
    char *argv[];			/* Values of fields. */
{
    TileTypeBitMask mask;
    int type, width, i, distance;
    char **nextArg;

    if (argc <= 0) return TRUE;

    /* Look for descriptions of the two routing layers. */

    if (strcmp(argv[0], "layer1") == 0)
    {
	if (argc < 3)
	{
	    wrongNumArgs:
	    TechError("Wrong number of arguments in router %s statement.\n",
		argv[0]);
	    return TRUE;
	}
	type = DBTechNoisyNameType(argv[1]);
	if (type >= 0) RtrMetalType = type;
	width = atoi(argv[2]);
	if (width <= 0)
	    TechError("Layer1 width must be positive; %d is illegal.\n",
		width);
	else RtrMetalWidth = width;
	TTMaskZero(&RtrMetalObstacles);
	argc -= 3;
	nextArg = &(argv[3]);
	while (argc >= 2)
	{
	    DBTechNoisyNameMask(*nextArg, &mask);
	    distance = atoi(nextArg[1]);
	    if (distance < 0)
	    {
		TechError("Layer1 obstacle separation must be positive; %d is illegal.\n",
		    distance);
	    }
	    else for (i=0; i<TT_MAXTYPES; i++)
	    {
		if (TTMaskHasType(&mask, i))
		{
		    /* Raw distances get stored temporarily in RtrMetalSeps.
		     * RtrTechFinal will compute the final correct values
		     * for RtrPaintSepsUp and RtrPaintSepsDown.
		     */
		    if (RtrMetalSeps[i] < distance)
			RtrMetalSeps[i] = distance;
		}
	    }
	    TTMaskSetMask(&RtrMetalObstacles, &mask);
	    argc -= 2;
	    nextArg += 2;
	}
	if (argc == 1) goto wrongNumArgs;
	return TRUE;
    }

    if (strcmp(argv[0], "layer2") == 0)
    {
	if (argc < 3) goto wrongNumArgs;
	type = DBTechNoisyNameType(argv[1]);
	if (type >= 0) RtrPolyType = type;
	width = atoi(argv[2]);
	if (width <= 0)
	    TechError("Layer2 width must be positive; %d is illegal.\n",
		width);
	else RtrPolyWidth = width;
	TTMaskZero(&RtrPolyObstacles);
	argc -= 3;
	nextArg = &(argv[3]);
	while (argc >= 2)
	{
	    DBTechNoisyNameMask(*nextArg, &mask);
	    distance = atoi(nextArg[1]);
	    if (distance < 0)
	    {
		TechError("Layer2 obstacle separation must be positive: %d is illegal.\n",
		    distance);
	    }
	    else for (i=0; i<TT_MAXTYPES; i++)
	    {
		if (TTMaskHasType(&mask, i))
		{
		    if (RtrPolySeps[i] < distance)
			RtrPolySeps[i] = distance;
		}
	    }
	    TTMaskSetMask(&RtrPolyObstacles, &mask);
	    argc -= 2;
	    nextArg += 2;
	}
	if (argc == 1) goto wrongNumArgs;
	return TRUE;
    }

    /* Look for contact specification. */

    if (strcmp(argv[0], "contacts") == 0)
    {
	if ((argc != 3) && (argc != 5)) goto wrongNumArgs;
	type = DBTechNoisyNameType(argv[1]);
	if (type >= 0) RtrContactType = type;
	width = atoi(argv[2]);
	if (width <= 0)
	    TechError("Contact width must be positive; %d is illegal.\n",
		width);
	else RtrContactWidth = width;
	RtrContactOffset = 0;
	if (argc == 5)
	{
	    if (!StrIsInt(argv[3]))
		TechError("Metal contact surround \"%s\" isn't integral.\n",
			argv[3]);
	    else
	    {
		RtrMetalSurround = atoi(argv[3]);
		if (RtrMetalSurround < 0)
		{
		    TechError("Metal contact surround \"%s\" mustn't be negative.\n", argv[3]);
		    RtrMetalSurround = 0;
		}
	    }
	    if (!StrIsInt(argv[4]))
		TechError("Poly contact surround \"%s\" isn't integral.\n",
			argv[4]);
	    else
	    {
		RtrPolySurround = atoi(argv[4]);
		if (RtrPolySurround < 0)
		{
		    TechError("Poly contact surround \"%s\" mustn't be negative.\n", argv[4]);
		    RtrPolySurround = 0;
		}
	    }
	}
	return TRUE;
    }

    /* Next, look for a gridspacing line. */

    if (strcmp(argv[0], "gridspacing") == 0)
    {
	if (argc != 2) goto wrongNumArgs;
	i = atoi(argv[1]);
	if (i <= 0)
	    TechError("Gridspacing must be positive; %d is illegal.\n", i);
	else RtrGridSpacing = i;
	return TRUE;
    }

    TechError("Unknown router statement \"%s\".\n", argv[0]);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrTechFinal --
 *
 * 	Called once at the very end of the router section of the technology
 *	file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Compute the actual subcell and paint separations, based on
 *	the technology file information.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrTechFinal()
{
    int i, above, below;

    /* Pick a contact offset so that the contacts are more-or-less
     * centered on the routing material.  Use the wider routing material
     * to make this decision;  otherwise, the material might end up
     * sticking out past the edge of the contact.  When rounding, round
     * down (since this works better if the two routing layers are different
     * widths).
     */
    
    i = RtrMetalWidth;
    if (RtrPolyWidth > i) i = RtrPolyWidth;
    RtrContactOffset = - (RtrContactWidth + 1 - i)/2;

    /* The actual actual distance from a paint layer to a grid line
     * is a combination of how far the paint must be from routing
     * material, and where routing material goes relative to the
     * grid line.
     */
    
    RtrSubcellSepUp = RtrSubcellSepDown = 0;

    /* Compute how far above and below grid lines the routing layers run,
     * and compensate the obstacle separations accordingly.  This is
     * determined by the contact size and location.
     */

    above = RtrContactWidth + RtrContactOffset;
    below = - RtrContactOffset;

    for (i=0; i < TT_MAXTYPES; i++)
    {
	int metal, poly;
	if (TTMaskHasType(&RtrMetalObstacles, i))
	    metal = RtrMetalSeps[i] + RtrMetalSurround;
	else metal = 0;
	if (TTMaskHasType(&RtrPolyObstacles, i))
	    poly = RtrPolySeps[i] + RtrPolySurround;
	else poly = 0;
	if (metal < poly) metal = poly;
	RtrPaintSepsDown[i] = metal + above;
	RtrPaintSepsUp[i] = metal + below;
	if (RtrPaintSepsDown[i] > RtrSubcellSepDown)
	    RtrSubcellSepDown = RtrPaintSepsDown[i];
	if (RtrPaintSepsUp[i] > RtrSubcellSepUp)
	    RtrSubcellSepUp = RtrPaintSepsUp[i];
    }

#ifdef	notdef
    TxPrintf("Routing information:\n");
    TxPrintf("  Layer1 is %s, width %d.\n",
	DBTypeLongName(RtrMetalType), RtrMetalWidth);
    if (!TTMaskEqual(&RtrMetalObstacles, &DBZeroTypeBits))
    {
	TxPrintf("  Layer1 obstacles are:");
	for (i=0; i<TT_MAXTYPES; i++)
	{
	    if (TTMaskHasType(&RtrMetalObstacles, i))
		TxPrintf(" %s(%d %d)", DBTypeLongName(i), RtrPaintSepsUp[i],
		    RtrPaintSepsDown[i]);
	}
	TxPrintf(".\n");
    }
    TxPrintf("  Layer2 is %s, width %d.\n",
	DBTypeLongName(RtrPolyType), RtrPolyWidth);
    if (!TTMaskEqual(&RtrPolyObstacles, &DBZeroTypeBits))
    {
	TxPrintf("  Layer2 obstacles are:");
	for (i=0; i<TT_MAXTYPES; i++)
	{
	    if (TTMaskHasType(&RtrPolyObstacles, i))
		TxPrintf(" %s(%d %d)", DBTypeLongName(i), RtrPaintSepsUp[i],
		    RtrPaintSepsDown[i]);
	}
	TxPrintf(".\n");
    }
    TxPrintf("  Contacts are %s, width %d, offset %d.\n",
	DBTypeLongName(RtrContactType), RtrContactWidth, RtrContactOffset);
    TxPrintf("  Grid spacing is %d.\n", RtrGridSpacing);
    TxPrintf("  Subcell separations %d up, %d down.\n",
	RtrSubcellSepUp, RtrSubcellSepDown);
#endif	/* notdef */
}

/*
 *----------------------------------------------------------------------------
 * RtrTechScale --
 *
 *	Scale the router technology parameters as required when magic's
 *	internal grid is redefined relative to the technology lambda.
 *
 *----------------------------------------------------------------------------
 */

int
RtrTechScale(scaled, scalen)
    int scaled, scalen;
{
    int i;

    RtrMetalWidth *= scalen;
    RtrPolyWidth *= scalen;
    RtrContactWidth *= scalen;
    RtrContactOffset *= scalen;
    RtrMetalSurround *= scalen;
    RtrPolySurround *= scalen;
    RtrGridSpacing *= scalen;
    RtrSubcellSepUp *= scalen;
    RtrSubcellSepDown *= scalen;

    RtrMetalWidth /= scaled;
    RtrPolyWidth /= scaled;
    RtrContactWidth /= scaled;
    RtrContactOffset /= scaled;
    RtrMetalSurround /= scaled;
    RtrPolySurround /= scaled;
    RtrGridSpacing /= scaled;
    RtrSubcellSepUp /= scaled;
    RtrSubcellSepDown /= scaled;

    for (i = 0; i < TT_MAXTYPES; i++)
    {
	RtrPaintSepsUp[i] *= scalen;
	RtrPaintSepsDown[i] *= scalen;
	RtrMetalSeps[i] *= scalen;
	RtrPolySeps[i] *= scalen;

	RtrPaintSepsUp[i] /= scaled;
	RtrPaintSepsDown[i] /= scaled;
	RtrMetalSeps[i] /= scaled;
	RtrPolySeps[i] /= scaled;
    }
    return 0;
}

