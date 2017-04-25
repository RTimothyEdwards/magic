/* CIFtech.c -
 *
 *	This module processes the portions of technology
 *	files pertaining to CIF, and builds the tables
 *	used by the CIF generator.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFtech.c,v 1.7 2010/10/20 20:34:19 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>	/* for atof()	*/
#include <string.h>
#include <ctype.h>
#include <math.h>	/* for pow()	*/

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "cif/CIFint.h"
#include "calma/calmaInt.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "cif/cif.h"
#include "drc/drc.h"	/* For WRL's DRC-CIF extensions */

/* The following statics are used to keep track of things between
 * calls to CIFTechLine.
 */

static CIFLayer *cifCurLayer;	/* Current layer whose spec. is being read. */
static CIFOp *cifCurOp;		/* Last geometric operation read in. */
static bool cifGotLabels;	/* TRUE means some labels have been assigned
				 * to the current layer.
				 */

/* The following is a TileTypeBitMask array with only the CIF_SOLIDTYPE
 * bit set in it.
 */
TileTypeBitMask CIFSolidBits;

/* Forward Declarations */

void cifTechStyleInit();
bool cifCheckCalmaNum();

/*
 * ----------------------------------------------------------------------------
 *
 * cifTechFreeStyle --
 *
 * 	This procedure frees memory for the current CIF style, and
 *	sets the current style to NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is free'd.
 *
 * ----------------------------------------------------------------------------
 */

void
cifTechFreeStyle()
{
    int i;
    CIFOp	*op;
    CIFLayer	*layer;

    if (CIFCurStyle != NULL)
    {
	/* Destroy old style structure and free memory allocated to it */

	for (i = 0; i < MAXCIFLAYERS; i++)
	{
	    layer = CIFCurStyle->cs_layers[i];
	    if (layer != NULL)
	    {
		for (op = layer->cl_ops; op != NULL; op = op->co_next)
		{
		    if (op->co_client != (ClientData)NULL)
		    {
			switch (op->co_opcode)
			{
			    case CIFOP_OR:
			    case CIFOP_BBOX:
			    case CIFOP_MAXRECT:
				/* These options use co_client to hold a single	*/
				/* integer value, so it is not allocated.	*/
				break;
			    default:
				freeMagic((char *)op->co_client);
				break;
			}
		    }
		    freeMagic((char *)op);
		}
		freeMagic((char *)layer);
	    }
	}
	freeMagic(CIFCurStyle);
	CIFCurStyle = NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifTechNewStyle --
 *
 * 	This procedure creates a new CIF style at the end of
 *	the list of style and initializes it to completely
 *	null.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new element is added to the end of CIFStyleList, and CIFCurStyle
 *	is set to point to it.
 *
 * ----------------------------------------------------------------------------
 */

void
cifTechNewStyle()
{
    cifTechFreeStyle();
    cifTechStyleInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifTechStyleInit --
 *
 * Fill in the current cif input style structure with initial values
 *
 * ----------------------------------------------------------------------------
 */

void
cifTechStyleInit()
{
    int i;

    if (CIFCurStyle == NULL)
	CIFCurStyle = (CIFStyle *) mallocMagic(sizeof(CIFStyle));

    CIFCurStyle->cs_name = NULL;
    CIFCurStyle->cs_status = TECH_NOT_LOADED;

    CIFCurStyle->cs_nLayers = 0;
    CIFCurStyle->cs_scaleFactor = 0;
    CIFCurStyle->cs_stepSize = 0;
    CIFCurStyle->cs_gridLimit = 0;
    CIFCurStyle->cs_reducer = 0;
    CIFCurStyle->cs_expander = 1;
    CIFCurStyle->cs_yankLayers = DBZeroTypeBits;
    CIFCurStyle->cs_hierLayers = DBZeroTypeBits;
    CIFCurStyle->cs_flags = 0;
    for (i=0;  i<TT_MAXTYPES;  i+=1)
	CIFCurStyle->cs_labelLayer[i] = -1;
    for (i = 0; i < MAXCIFLAYERS; i++)
	CIFCurStyle->cs_layers[i] = NULL;
}


/*
 * ----------------------------------------------------------------------------
 *
 * cifParseLayers --
 *
 * 	Takes a comma-separated list of layers and turns it into two
 *	masks, one of paint layers and one of previously-defined CIF
 *	layers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The masks pointed to by the paintMask and cifMask parameters
 *	are modified.  If some of the layers are unknown, then an error
 *	message is printed.
 *
 * ----------------------------------------------------------------------------
 */

void
cifParseLayers(string, style, paintMask, cifMask,spaceOK)
    char *string;		/* List of layers. */
    CIFStyle *style;		/* Gives CIF style for parsing string.*/
    TileTypeBitMask *paintMask;	/* Place to store mask of paint layers.  If
				 * NULL, then only CIF layer names are
				 * considered.
				 */
    TileTypeBitMask *cifMask;	/* Place to store mask of CIF layers.  If
				 * NULL, then only paint layer names are
				 * considered.
				 */
    int	spaceOK;		/* are space layers permissible in this cif
    				   layer?
				*/
{
    TileTypeBitMask curCifMask, curPaintMask;
    char curLayer[40], *p, *cp;
    TileType paintType;
    int i;
    bool allResidues;

    if (paintMask != NULL) TTMaskZero(paintMask);
    if (cifMask != NULL) TTMaskZero(cifMask);

    while (*string != 0)
    {
	p = curLayer;

	if (*string == '*')
	{
	    allResidues = TRUE;
	    string++;
	}
	else
	    allResidues = FALSE;

	while ((*string != ',') && (*string != 0))
	    *p++ = *string++;
	*p = 0;
	while (*string == ',') string += 1;

	/* See if this is a paint type. */

	if (paintMask != NULL)
	{
	    paintType = DBTechNameTypes(curLayer, &curPaintMask);
	    if (paintType >= 0) goto okpaint;
	}
	else paintType = -2;

okpaint:
	/* See if this is the name of another CIF layer.  Be
	 * careful not to let the current layer be used in
	 * generating itself.  Exact match is requred on CIF
	 * layer names, but the same name can appear multiple
	 * times in different styles.
	 */

	TTMaskZero(&curCifMask);
	if (cifMask != NULL)
	{
	    for (i = 0;  i < style->cs_nLayers; i++)
	    {
		if (style->cs_layers[i] == cifCurLayer) continue;
		if (strcmp(curLayer, style->cs_layers[i]->cl_name) == 0)
		{
		    TTMaskSetType(&curCifMask, i);
		}
	    }
	}

	/* Make sure that there's exactly one match among cif and
	 * paint layers together.
	 */
	
	if ((paintType == -1)
	    || ((paintType >= 0) && !TTMaskEqual(&curCifMask, &DBZeroTypeBits)))
	{
	    TechError("Ambiguous layer (type) \"%s\".\n", curLayer);
	    continue;
	}
	if (paintType >= 0)
	{
	    if (paintType == TT_SPACE && spaceOK ==0)
		TechError("\"Space\" layer not permitted in CIF rules.\n");
	    else
	    {
		TileType rtype;
		TileTypeBitMask *rMask;

		TTMaskSetMask(paintMask, &curPaintMask);

		/* Add residues from '*' notation */
		if (allResidues)
		    for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
		    {
			rMask = DBResidueMask(rtype);
			if (TTMaskHasType(rMask, paintType))
			    TTMaskSetType(paintMask, rtype);
		    }
	    } 
	}
	else if (!TTMaskEqual(&curCifMask, &DBZeroTypeBits))
	{
	    TTMaskSetMask(cifMask, &curCifMask);
	}
	else
	{
	    HashEntry *he;
	    TileTypeBitMask *amask;

	    he = HashLookOnly(&DBTypeAliasTable, curLayer);
	    if (he != NULL)
	    {
		amask = (TileTypeBitMask *)HashGetValue(he);
		TTMaskSetMask(paintMask, amask);
	    }
	    else
		TechError("Unrecognized layer (type) \"%s\".\n", curLayer);
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechInit --
 *
 * 	Called before loading a new technology.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out list of styles and resets the current style.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFTechInit()
{
    CIFKeep *style;

    /* Cleanup any old info. */

    cifTechFreeStyle();

    /* forget the list of styles */

    for (style = CIFStyleList; style != NULL; style = style->cs_next)
    {
	freeMagic(style->cs_name);
	freeMagic(style);
    }
    CIFStyleList = NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechStyleInit --
 *
 * 	Called once at beginning of technology file read-in to
 *	initialize data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Just clears out the layer data structures.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFTechStyleInit()
{
    CalmaTechInit();

    /* Create the TileTypeBitMask array with only the CIF_SOLIDTYPE bit set */
    TTMaskZero(&CIFSolidBits);
    TTMaskSetType(&CIFSolidBits, CIF_SOLIDTYPE);

    cifCurOp = NULL;
    cifCurLayer = NULL;
    cifGotLabels = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechLimitScale --
 *
 *	Determine if the scalefactor (ns / ds), applied to the current
 *	grid scaling, would result in a grid finer than the minimum
 *	resolution allowed by the process, as set by the "gridlimit"
 *	statement in the "cifoutput" section (note that the scaling
 *	depends on the output style chosen, and can be subverted by
 *	scaling while a fine-grid output style is active, then switching
 *	to a coarse-grid output style).
 *
 *	Note that even if the scalefactor is larger than the minimum
 *	grid, it must be a MULTIPLE of the minimum grid, or else geometry
 *	can be generated off-grid.
 *
 * Results:
 *	TRUE if scaling by (ns / ds) would violate minimum grid resolution,
 *	FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFTechLimitScale(ns, ds)
    int ns, ds;
{
    int gridup, scaledown;
    int scale, limit, expand;

    if (CIFCurStyle == NULL) return FALSE;

    scale = CIFCurStyle->cs_scaleFactor;
    limit = CIFCurStyle->cs_gridLimit;
    expand = CIFCurStyle->cs_expander;

    if (limit == 0) limit = 1;

    gridup = limit * expand * ds;
    scaledown = scale * ns * 10;

    if ((scaledown / gridup) == 0) return TRUE;
    if ((scaledown % gridup) != 0) return TRUE;

    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseScale --
 *
 *	Read a scale value and determine scaleFactor and expander values
 *
 * Results:
 *	Returns the value for cs_scaleFactor
 *
 * Side effects:
 *	Alters the value of expander (pointer to cs_expander)
 *
 * ----------------------------------------------------------------------------
 */

int
CIFParseScale(true_scale, expander)
    char *true_scale;
    int  *expander;
{
    char *decimal;
    short places;
    int n, d;

    decimal = strchr(true_scale, '.');

    if (decimal == NULL)	/* true_scale is integer */
    {
	*expander = 1;
	return atoi(true_scale);
    }
    else
    {
	*decimal = '\0';
	places = strlen(decimal + 1);
	d = pow(10,places);
	n = atoi(true_scale);
	*decimal = '.';
	n *= d;
	n += atoi(decimal + 1);
	ReduceFraction(&n, &d);
	*expander = d;
	return n;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechLine --
 *
 * 	This procedure is called once for each line in the "cif"
 *	section of the technology file.
 *
 * Results:
 *	TRUE if line parsed correctly; FALSE if fatal error condition
 *	encountered.
 *
 * Side effects:
 *	Sets up information in the tables of CIF layers, and
 *	prints error messages where there are problems.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFTechLine(sectionName, argc, argv)
    char *sectionName;		/* The name of this section. */
    int argc;			/* Number of fields on line. */
    char *argv[];		/* Values of fields. */
{
    TileTypeBitMask mask, tempMask, bloatLayers;
    int i, j, l, distance;
    CIFLayer *newLayer;
    CIFOp *newOp = NULL;
    CIFKeep *newStyle, *p;
    char **bloatArg;
    BloatData *bloats;
    SquaresData *squares;
    SlotsData *slots;

    if (argc <= 0) return TRUE;
    else if (argc >= 2) l = strlen(argv[1]);

    /* See if we're starting a new CIF style.  If not, make
     * sure that the current (maybe default?) CIF style has
     * a name.
     */
    
    if (strcmp(argv[0], "style") == 0)
    {
	if (argc != 2)
	{
	    if ((argc != 4) || (strncmp(argv[2], "variant", 7)))
	    {
		wrongNumArgs:
		TechError("Wrong number of arguments in %s statement.\n",
				argv[0]);
		errorReturn:
		if (newOp != NULL)
		    freeMagic((char *) newOp);
		return TRUE;
	    }
	}
	for (newStyle = CIFStyleList; newStyle != NULL; 
		newStyle = newStyle->cs_next)
	{
	    /* Here we're only establishing existence;		*/
	    /* break on the first variant found.		*/

	    if (!strncmp(newStyle->cs_name, argv[1], l))
		break;
	}
	if (newStyle == NULL)
	{
	    if (argc == 2)
	    {
		newStyle = (CIFKeep *)mallocMagic(sizeof(CIFKeep));
		newStyle->cs_next = NULL;
		newStyle->cs_name = StrDup((char **) NULL, argv[1]);

		/* Append to end of style list */
		if (CIFStyleList == NULL)
		    CIFStyleList = newStyle;
		else
		{
		    for (p = CIFStyleList; p->cs_next; p = p->cs_next);
		    p->cs_next = newStyle;
		}
	    }
	    else	/* Handle style variants */
	    {
		CIFKeep *saveStyle = NULL;
		char *tptr, *cptr;

		/* 4th argument is a comma-separated list of variants.	*/
		/* In addition to the default name recorded above,	*/
		/* record each of the variants.				*/

		tptr = argv[3];
		while (*tptr != '\0')
		{
		    cptr = strchr(tptr, ',');
		    if (cptr != NULL) *cptr = '\0';
		    newStyle = (CIFKeep *)mallocMagic(sizeof(CIFKeep));
		    newStyle->cs_next = NULL;
		    newStyle->cs_name = (char *)mallocMagic(l
				+ strlen(tptr) + 1);
		    sprintf(newStyle->cs_name, "%s%s", argv[1], tptr);

		    /* Remember the first variant as the default */
		    if (saveStyle == NULL) saveStyle= newStyle;

		    /* Append to end of style list */
		    if (CIFStyleList == NULL)
			CIFStyleList = newStyle;
		    else
		    {
			for (p = CIFStyleList; p->cs_next; p = p->cs_next);
			p->cs_next = newStyle;
		    }
		    
		    if (cptr == NULL)
			break;
		    else
			tptr = cptr + 1;
		}
		newStyle = saveStyle;
	    }
	}

	if (CIFCurStyle == NULL)
	{
	    cifTechNewStyle();
	    CIFCurStyle->cs_name = newStyle->cs_name;
	    CIFCurStyle->cs_status = TECH_PENDING;
	}
	else if ((CIFCurStyle->cs_status == TECH_PENDING) ||
			(CIFCurStyle->cs_status == TECH_SUSPENDED))
	    CIFCurStyle->cs_status = TECH_LOADED;
	else if (CIFCurStyle->cs_status == TECH_NOT_LOADED)
	{
	    if (CIFCurStyle->cs_name == NULL)
		return (FALSE);
	    else if (argc == 2)
	    {
		if (!strcmp(argv[1], CIFCurStyle->cs_name))
		    CIFCurStyle->cs_status = TECH_PENDING;
	    }
	    else if (argc == 4)
	    {
		/* Verify that the style matches one variant */

		char *tptr, *cptr;

		if (!strncmp(CIFCurStyle->cs_name, argv[1], l))
		{
		    tptr = argv[3];
		    while (*tptr != '\0')
		    {
			cptr = strchr(tptr, ',');
			if (cptr != NULL) *cptr = '\0';
			if (!strcmp(CIFCurStyle->cs_name + l, tptr))
			{
			    CIFCurStyle->cs_status = TECH_PENDING;
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

    /* Only continue past this point if we are loading the cif output style */
    if (CIFCurStyle == NULL) return FALSE;
    if ((CIFCurStyle->cs_status != TECH_PENDING) &&
		(CIFCurStyle->cs_status != TECH_SUSPENDED))
	return TRUE;

    /* Process scalefactor lines next. */

    if (strcmp(argv[0], "scalefactor") == 0)
    {
	if ((argc < 2) || (argc > 4)) goto wrongNumArgs;
	CIFCurStyle->cs_scaleFactor = CIFParseScale(argv[1],
		&CIFCurStyle->cs_expander);

	/*
	 * The "nanometers" keyword multiplies the expander by 10.
	 * Any reducer value and keyword "calmaonly" are now both ignored.
	 */

	if (argc >= 3)
	{
	    if (strncmp(argv[argc - 1], "nanom", 5) == 0)
		CIFCurStyle->cs_expander *= 10;
	    else if (strncmp(argv[argc - 1], "angstr", 6) == 0)
		CIFCurStyle->cs_expander *= 100;
	}

	CIFCurStyle->cs_reducer = 1;	/* initial value only */

	if (CIFCurStyle->cs_scaleFactor <= 0)
	{
	    CIFCurStyle->cs_scaleFactor = 0;
	    TechError("Scalefactor must be a strictly positive value.\n");
	    goto errorReturn;
	}
	return TRUE;
    }

    /* New for magic-7.3.100---allow GDS database units to be other	*/
    /* than nanometers.  Really, there is only one option here, which	*/
    /* is "units angstroms".   This option does not affect CIF output,	*/
    /* whose units are fixed at centimicrons.				*/

    if (strcmp(argv[0], "units") == 0)
    {
	if (argc != 2) goto wrongNumArgs;
	if (!strncmp(argv[1], "angstr", 6))
	    CIFCurStyle->cs_flags |= CWF_ANGSTROMS;
	return TRUE;
    }

    if (strcmp(argv[0], "stepsize") == 0)
    {
	if (argc != 2) goto wrongNumArgs;
	CIFCurStyle->cs_stepSize = atoi(argv[1]);
	if (CIFCurStyle->cs_stepSize <= 0)
	{
	    TechError("Step size must be positive integer.\n");
	    CIFCurStyle->cs_stepSize = 0;
	}
	return TRUE;
    }

    /* Process "gridlimit" line next. */
    if (strncmp(argv[0], "grid", 4) == 0)
    {
	if (StrIsInt(argv[1]))
	{
	    CIFCurStyle->cs_gridLimit = atoi(argv[1]);
	    if (CIFCurStyle->cs_gridLimit < 0)
	    {
		TechError("Grid limit must be a positive integer.\n");
		CIFCurStyle->cs_gridLimit = 0;
	    }
	}
	else
	    TechError("Unable to parse grid limit value.\n");

	return TRUE;
    }

    /* Process "variant" lines next. */

    if (strncmp(argv[0], "variant", 7) == 0)
    {
	int l;
	char *cptr, *tptr;

	/* If our style variant is not one of the ones declared */
	/* on the line, then we ignore all input until we 	*/
	/* either reach the end of the style, the end of the	*/
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
		CIFCurStyle->cs_status = TECH_PENDING;
		return TRUE;
	    }
	    else
	    {
		l = strlen(CIFCurStyle->cs_name) - strlen(tptr);
		if (!strcmp(tptr, CIFCurStyle->cs_name + l))
		{
		    CIFCurStyle->cs_status = TECH_PENDING;
		    return TRUE;
		}
	    }
	
	    if (cptr == NULL)
		break;
	    else
		tptr = cptr + 1;
	}
	CIFCurStyle->cs_status = TECH_SUSPENDED;
    }

    /* Anything below this line is not parsed if we're in TECH_SUSPENDED mode */
    if (CIFCurStyle->cs_status != TECH_PENDING) return TRUE;

    newLayer = NULL;
    if ((strcmp(argv[0], "templayer") == 0)
	|| (strcmp(argv[0], "layer") == 0))
    {
	if (CIFCurStyle->cs_nLayers == MAXCIFLAYERS)
	{
	    cifCurLayer = NULL;
	    TechError("Can't handle more than %d CIF layers.\n", MAXCIFLAYERS);
	    TechError("Your local Magic wizard can fix this.\n");
	    goto errorReturn;
	}
	if (argc != 2 && argc != 3)
	{
	    cifCurLayer = NULL;
	    goto wrongNumArgs;
	}
	newLayer = CIFCurStyle->cs_layers[CIFCurStyle->cs_nLayers]
	    = (CIFLayer *) mallocMagic(sizeof(CIFLayer));
	CIFCurStyle->cs_nLayers += 1;
	if ((cifCurOp == NULL) && (cifCurLayer != NULL) && !cifGotLabels)
	{
	    TechError("Layer \"%s\" contains no material.\n",
		cifCurLayer->cl_name);
	}
	newLayer->cl_name = NULL;
	(void) StrDup(&newLayer->cl_name, argv[1]);
	newLayer->cl_ops = NULL;
	newLayer->cl_flags = 0;
	newLayer->cl_calmanum = newLayer->cl_calmatype = -1;
	newLayer->min_width = 0; /* for growSlivers */
#ifdef THREE_D
	newLayer->cl_height = 0.0;
	newLayer->cl_thick = 0.0;
	newLayer->cl_renderStyle = STYLE_PALEHIGHLIGHTS - TECHBEGINSTYLES;
#endif
	if (strcmp(argv[0], "templayer") == 0)
	    newLayer->cl_flags |= CIF_TEMP;
	cifCurLayer = newLayer;
	cifCurOp = NULL;
	cifGotLabels = FALSE;

	/* Handle a special case of a list of layer names on the layer
	 * line.  Turn them into an OR operation. 
	 */
	
	if (argc == 3)
	{
	    cifCurOp = (CIFOp *) mallocMagic(sizeof(CIFOp));
	    cifCurOp->co_opcode = CIFOP_OR;
	    cifParseLayers(argv[2], CIFCurStyle, &cifCurOp->co_paintMask,
		&cifCurOp->co_cifMask,FALSE);
	    cifCurOp->co_distance = 0;
	    cifCurOp->co_next = NULL;
	    cifCurOp->co_client = (ClientData)NULL;
	    cifCurLayer->cl_ops = cifCurOp;
	}
	return TRUE;
    }

    if (strcmp(argv[0], "labels") == 0)
    {
	if (cifCurLayer == NULL)
	{
	    TechError("Must define layer before giving labels it holds.\n");
	    goto errorReturn;
	}
	if (cifCurLayer->cl_flags & CIF_TEMP)
	    TechError("Why are you attaching labels to a temporary layer?\n");
	if (argc != 2) goto wrongNumArgs;
	DBTechNoisyNameMask(argv[1], &mask);
	for (i=0; i<TT_MAXTYPES; i+=1)
	{
	    if (TTMaskHasType(&mask, i))
	        CIFCurStyle->cs_labelLayer[i] = CIFCurStyle->cs_nLayers-1;
	}
	cifGotLabels = TRUE;
	return TRUE;
    }

    if ((strcmp(argv[0], "calma") == 0) || (strncmp(argv[0], "gds", 3) == 0))
    {
	if (cifCurLayer == NULL)
	{
	    TechError("Must define layers before giving their Calma types.\n");
	    goto errorReturn;
	}
	if (cifCurLayer->cl_flags & CIF_TEMP)
	    TechError("Why assign a Calma number to a temporary layer?\n");
	if (argc != 3) goto wrongNumArgs;
	if (!cifCheckCalmaNum(argv[1]) || !cifCheckCalmaNum(argv[2]))
	    TechError("Calma layer and type numbers must be 0 to %d.\n",
		CALMA_LAYER_MAX);
	cifCurLayer->cl_calmanum = atoi(argv[1]);
	cifCurLayer->cl_calmatype = atoi(argv[2]);
	return TRUE;
    }
    if (strcmp(argv[0], "min-width") == 0) /* used in growSliver */
    {
	if (cifCurLayer == NULL)
	{
	    TechError("Must define layers before assigning a minimum width.\n");
	    goto errorReturn;
	}
	if (argc != 2) goto wrongNumArgs;
	cifCurLayer->min_width = atoi(argv[1]);
	CIFCurStyle->cs_flags |= CWF_GROW_SLIVERS;
	return TRUE;
    }

    if (strcmp(argv[0], "render") == 0) /* used by specialopen wind3d client */
    {
#ifdef THREE_D
	float height, thick;
	int i, style, lcnt;
	CIFLayer *layer;

	if (argc != 5) goto wrongNumArgs;

	cifCurLayer = NULL;	/* This is not in a layer definition */

	style = DBWTechParseStyle(argv[2]);
	if (style < 0)
	{
	    TechError("Error:  Bad render style for CIF layer.\n");
	    goto errorReturn;
	}

	if (!StrIsNumeric(argv[3]) || !StrIsNumeric(argv[4]))
	{
	    TechError("Syntax: render <layer> <style> <height> <thick>\n");
	    goto errorReturn;
	}
	height = (float)atof(argv[3]);
	thick = (float)atof(argv[4]);

	lcnt = 0;
	for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
	{
	    layer = CIFCurStyle->cs_layers[i];
	    if (!strcmp(argv[1], layer->cl_name))
	    {
		layer->cl_height = height;
		layer->cl_thick = thick;
		layer->cl_renderStyle = style;
		lcnt++;
	    }
	}
	if (lcnt == 0)
	{
	    TechError("Unknown layer name.\n");
	    goto errorReturn;
	}
#endif
        return TRUE;
    }

    /* 
     * miscellaneous cif/calma-writing boolean options
     */

    if (strcmp(argv[0], "options") == 0)
    {
	int i;
	if (argc < 2) goto wrongNumArgs;
	for (i = 1; i < argc; i++)
	{
	    if (strcmp(argv[i], "calma-permissive-labels") == 0)
		CIFCurStyle->cs_flags |= CWF_PERMISSIVE_LABELS;
	    else if (strcmp(argv[i], "grow-euclidean") == 0)
		CIFCurStyle->cs_flags |= CWF_GROW_EUCLIDEAN;
	    else if (strcmp(argv[i], "see-vendor") == 0)
		CIFCurStyle->cs_flags |= CWF_SEE_VENDOR;
	    else if (strcmp(argv[i], "no-errors") == 0)
		CIFCurStyle->cs_flags |= CWF_NO_ERRORS;
	}
	return TRUE;
    }

    /* Anything below here is a geometric operation, so we can
     * do some set-up that is common to all the operations.
     */
    
    if (cifCurLayer == NULL)
    {
	TechError("Must define layer before specifying operation.\n");
	goto errorReturn;
    }
    newOp = (CIFOp *) mallocMagic(sizeof(CIFOp));
    TTMaskZero(&newOp->co_paintMask);
    TTMaskZero(&newOp->co_cifMask);
    newOp->co_opcode = 0;
    newOp->co_distance = 0;
    newOp->co_next = NULL;
    newOp->co_client = (ClientData)NULL;

    if (strcmp(argv[0], "and") == 0)
	newOp->co_opcode = CIFOP_AND;
    else if (strcmp(argv[0], "and-not") == 0)
	newOp->co_opcode = CIFOP_ANDNOT;
    else if (strcmp(argv[0], "or") == 0)
	newOp->co_opcode = CIFOP_OR;
    else if (strcmp(argv[0], "grow") == 0)
	newOp->co_opcode = CIFOP_GROW;
    else if (strcmp(argv[0], "grow-grid") == 0)
	newOp->co_opcode = CIFOP_GROW_G;
    else if (strcmp(argv[0], "shrink") == 0)
	newOp->co_opcode = CIFOP_SHRINK;
    else if (strcmp(argv[0], "bloat-or") == 0)
	newOp->co_opcode = CIFOP_BLOAT;
    else if (strcmp(argv[0], "bloat-max") == 0)
	newOp->co_opcode = CIFOP_BLOATMAX;
    else if (strcmp(argv[0], "bloat-min") == 0)
	newOp->co_opcode = CIFOP_BLOATMIN;
    else if (strcmp(argv[0], "bloat-all") == 0)
	newOp->co_opcode = CIFOP_BLOATALL;
    else if (strcmp(argv[0], "squares") == 0)
	newOp->co_opcode = CIFOP_SQUARES;
    else if (strcmp(argv[0], "squares-grid") == 0)
	newOp->co_opcode = CIFOP_SQUARES_G;
    else if (strcmp(argv[0], "slots") == 0)
	newOp->co_opcode = CIFOP_SLOTS;
    else if (strcmp(argv[0], "bbox") == 0)
	newOp->co_opcode = CIFOP_BBOX;
    else if (strcmp(argv[0], "net") == 0)
	newOp->co_opcode = CIFOP_NET;
    else if (strcmp(argv[0], "maxrect") == 0)
	newOp->co_opcode = CIFOP_MAXRECT;
    else
    {
	TechError("Unknown statement \"%s\".\n", argv[0]);
	goto errorReturn;
    }

    switch (newOp->co_opcode)
    {
	case CIFOP_AND:
	case CIFOP_ANDNOT:
	case CIFOP_OR:
	    if (argc != 2) goto wrongNumArgs;
	    cifParseLayers(argv[1], CIFCurStyle, &newOp->co_paintMask,
		&newOp->co_cifMask,FALSE);
	    break;
	
	case CIFOP_GROW:
	case CIFOP_GROW_G:
	case CIFOP_SHRINK:
	    if (argc != 2) goto wrongNumArgs;
	    newOp->co_distance = atoi(argv[1]);
	    if (newOp->co_distance <= 0)
	    {
		TechError("Grow/shrink distance must be greater than zero.\n");
		goto errorReturn;
	    }
	    break;
	
	case CIFOP_BLOATALL:
	    if (argc != 3) goto wrongNumArgs;
	    cifParseLayers(argv[1], CIFCurStyle, &newOp->co_paintMask,
		(TileTypeBitMask *)NULL, FALSE);
	    bloatLayers = newOp->co_paintMask;
	    bloats = (BloatData *)mallocMagic(sizeof(BloatData));
	    for (i = 0; i < TT_MAXTYPES; i++)
		bloats->bl_distance[i] = 0;
	    newOp->co_client = (ClientData)bloats;

	    cifParseLayers(argv[2], CIFCurStyle, &mask, &tempMask, TRUE);
	    TTMaskSetMask(&bloatLayers, &mask);
	    if (!TTMaskEqual(&tempMask, &DBZeroTypeBits))
		TechError("Can't use templayers in bloat statement.\n");

	    for (i = 0; i < TT_MAXTYPES;  i++)
		if (TTMaskHasType(&mask, i))
		    bloats->bl_distance[i] = 1;
	    goto bloatCheck;
		
	case CIFOP_BLOAT:
	case CIFOP_BLOATMIN:
	case CIFOP_BLOATMAX:
	    if (argc < 4) goto wrongNumArgs;
	    cifParseLayers(argv[1], CIFCurStyle, &newOp->co_paintMask,
		(TileTypeBitMask *) NULL,FALSE);
	    argc -= 2;
	    bloatArg = argv + 2;
	    bloatLayers = newOp->co_paintMask;
	    bloats = (BloatData *)mallocMagic(sizeof(BloatData));
	    for (i = 0; i < TT_MAXTYPES; i++)
		bloats->bl_distance[i] = 0;
	    newOp->co_client = (ClientData)bloats;

	    while (argc > 0)
	    {
		if (argc == 1) goto wrongNumArgs;
		if (strcmp(*bloatArg, "*") == 0)
		{
		    mask = DBAllTypeBits;
		    tempMask = DBZeroTypeBits;
		}
		else
		{
		    cifParseLayers(*bloatArg, CIFCurStyle, &mask, &tempMask,TRUE);
		    TTMaskSetMask(&bloatLayers, &mask);
		}
		if (!TTMaskEqual(&tempMask, &DBZeroTypeBits))
		    TechError("Can't use templayers in bloat statement.\n");
		
		distance = atoi(bloatArg[1]);
		if ((distance < 0) && (newOp->co_opcode == CIFOP_BLOAT))
		{
		    TechError("Bloat-or distances must not be negative.\n");
		    distance = 0;
		}
		for (i = 0; i < TT_MAXTYPES;  i++)
		    if (TTMaskHasType(&mask, i))
			bloats->bl_distance[i] = distance;

		argc -= 2;
		bloatArg += 2;
	    }

bloatCheck:
	    /* Don't do any bloating at boundaries between tiles of the
	     * types being bloated.  Otherwise a bloat could pass right
	     * through a skinny tile and out the other side.
	     */
	    for (i = 0; i < TT_MAXTYPES; i++)
		if (TTMaskHasType(&newOp->co_paintMask, i))
		    bloats->bl_distance[i] = 0;

	    /* Make sure that all the layers specified in the statement
	     * fall in a single plane.
	     */
	
	    for (i = 0; i < PL_MAXTYPES; i++)
	    {
		tempMask = bloatLayers;
		TTMaskAndMask(&tempMask, &DBPlaneTypes[i]);
		if (TTMaskEqual(&tempMask, &bloatLayers)) {
		    bloats->bl_plane = i;
		    goto bloatDone;
		}
	    }
	    TechError("Not all bloat layers fall in the same plane.\n");
	    bloatDone: break;

	case CIFOP_NET:
	    if (argc != 3) goto wrongNumArgs;
	    newOp->co_client = (ClientData)StrDup((char **)NULL, argv[1]);
	    cifParseLayers(argv[2], CIFCurStyle, &newOp->co_paintMask,
		&newOp->co_cifMask, FALSE);
	    break;

	case CIFOP_MAXRECT:
	    if (argc == 2)
	    {
		if (!strncmp(argv[1], "ext", 3))
		    newOp->co_client = (ClientData)1;
		else if (strncmp(argv[1], "int", 3))
		    TechError("Maxrect takes only one optional argument "
				"\"external\" or \"internal\" (default).\n");
	    }
	    else if (argc != 1)
		goto wrongNumArgs;
	    break;

	case CIFOP_BBOX:
	    if (argc == 2)
	    {
		if (!strcmp(argv[1], "top"))
		    newOp->co_client = (ClientData)1;
		else
		    TechError("BBox takes only one optional argument \"top\".\n");
	    }
	    else if (argc != 1)
		goto wrongNumArgs;
	    break;

	case CIFOP_SQUARES_G:
	case CIFOP_SQUARES:
	
	    squares = (SquaresData *)mallocMagic(sizeof(SquaresData));
	    newOp->co_client = (ClientData)squares;

	    if (argc == 2)
	    {
		i = atoi(argv[1]);
		squares->sq_border = atoi(argv[1]);
		if ((i <= 0) || (i & 1))
		{
		    TechError("Squares must have positive even sizes.\n");
		    goto errorReturn;
		}
		squares->sq_border = i/2;
		squares->sq_size = i;
		squares->sq_sep = i;
		squares->sq_gridx = 1; /* set default grid */
		squares->sq_gridy = 1; /* set default grid */
	    }
	    else if (argc == 4 || ((argc == 5 || argc == 6)
			&& newOp->co_opcode==CIFOP_SQUARES_G))
	    {
		squares->sq_border = atoi(argv[1]);
		if (squares->sq_border < 0)
		{
		    TechError("Square border must not be negative.\n");
		    goto errorReturn;
		}
		squares->sq_size = atoi(argv[2]);
		if (squares->sq_size <= 0)
		{
		    TechError("Squares must have positive sizes.\n");
		    goto errorReturn;
		}
		squares->sq_sep = atoi(argv[3]);
		if (squares->sq_sep <= 0)
		{
		    TechError("Square separation must be positive.\n");
		    goto errorReturn;
		}
		if (argc >= 5)
		{
		    squares->sq_gridx = squares->sq_gridy = atoi(argv[4]);
		    if (squares->sq_gridx <= 0)
		    {
		        TechError("Square grid must be strictly positive.\n");
			squares->sq_gridx = squares->sq_gridy = 1;
		        goto errorReturn;
		    }
		}
		else
		{
		    squares->sq_gridx = 1; /* set default grid */
		    squares->sq_gridy = 1; /* set default grid */
		}
		if (argc == 6)
		{
		    squares->sq_gridy = atoi(argv[5]);
		    if (squares->sq_gridy <= 0)
		    {
		        TechError("Square y-grid must be strictly positive.\n");
			squares->sq_gridy = 1;
		        goto errorReturn;
		    }
		}
	    }
	    else goto wrongNumArgs;

	    /* Ensure that squares are never placed at less than the	*/
	    /* minimum allowed mask resolution.  This may require that	*/
	    /* operation "squares" be changed to "squares-grid".	*/

	    if (squares->sq_gridx < CIFCurStyle->cs_gridLimit)
	    {
		squares->sq_gridx = CIFCurStyle->cs_gridLimit;
		newOp->co_opcode = CIFOP_SQUARES_G;
	    }
	    if (squares->sq_gridy < CIFCurStyle->cs_gridLimit)
	    {
		squares->sq_gridy = CIFCurStyle->cs_gridLimit;
		newOp->co_opcode = CIFOP_SQUARES_G;
	    }
	    break;

	case CIFOP_SLOTS:
	
	    slots = (SlotsData *)mallocMagic(sizeof(SlotsData));
	    newOp->co_client = (ClientData)slots;

	    if (argc >= 4)
	    {
		i = atoi(argv[1]);
		slots->sl_sborder = i;
		if (i < 0)
		{
		    TechError("Slot border must be non-negative.\n");
		    goto errorReturn;
		}
		i = atoi(argv[2]);
		slots->sl_ssize = i;
		if (i <= 0)
		{
		    TechError("Slot short-side size must be strictly positive.\n");
		    goto errorReturn;
		}
		i = atoi(argv[3]);
		slots->sl_ssep = i;
		if (i <= 0)
		{
		    TechError("Slot separation must be strictly positive.\n");
		    goto errorReturn;
		}
		/* Initialize other values, in case they are not specified */
		slots->sl_lborder = 0;
		slots->sl_lsize = 0;
		slots->sl_lsep = 0;
		slots->sl_offset = 0;
	    }
	    if (argc >= 5)
	    {
		i = atoi(argv[4]);
		slots->sl_lborder = i;
		if (i < 0)
		{
		    TechError("Slot border must be non-negative.\n");
		    goto errorReturn;
		}
	    }
	    if (argc >= 6)
	    {
		i = atoi(argv[5]);
		slots->sl_lsize = i;
		if (i < 0)
		{
		    TechError("Slot long-side size must be positive or zero.\n");
		    goto errorReturn;
		}
		i = atoi(argv[6]);
		slots->sl_lsep = i;
		if (i < 0)
		{
		    TechError("Slot long-side separation must be non-negative.\n");
		    goto errorReturn;
		}
		else if (i == 0 && (slots->sl_lsize > 0))
		{
		    TechError("Slot long-side separation must be strictly positive"
				" when long-side size is nonzero\n");
		    goto errorReturn;
		}
	    }
	    if (argc == 8)
	    {
		i = atoi(argv[7]);
		slots->sl_offset = i;
		if (i < 0)
		{
		    TechError("Slot offset must be non-negative.\n");
		    goto errorReturn;
		}
	    }
	    if ((argc < 4) || (argc == 6) || (argc > 8))
		goto wrongNumArgs;
	    break;
    }

    /* Link the new CIFOp into the list. */

    if (cifCurOp == NULL)
	cifCurLayer->cl_ops = newOp;
    else
	cifCurOp->co_next = newOp;
    cifCurOp = newOp;

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifCheckCalmaNum --
 *
 * 	This local procedure checks whether its argument is the ASCII
 *	representation of a positive integer between 0 and CALMA_LAYER_MAX.
 *
 * Results:
 *	TRUE if the argument string is valid as described above, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifCheckCalmaNum(str)
    char *str;
{
    int n = atoi(str);

    if (n < 0 || n > CALMA_LAYER_MAX)
	return (FALSE);

    while (*str) {
	char ch = *str++;
	if (ch < '0' || ch > '9') 
	    return (FALSE);
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifComputeRadii --
 *
 * 	This local procedure computes and fills in the grow and
 *	shrink distances for a layer.  Before calling this procedure,
 *	the distances must have been computed for all temporary
 *	layers used by this layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies cl_growDist and cl_shrinkDist in layer.
 *
 * ----------------------------------------------------------------------------
 */

void
cifComputeRadii(layer, des)
    CIFLayer *layer;		/* Layer for which to compute distances. */
    CIFStyle *des;		/* CIF style (used to find temp layer
				 * distances.
				 */
{
    int i, grow, shrink, curGrow, curShrink;
    CIFOp *op;
    BloatData *bloats;

    grow = shrink = 0;

    for (op = layer->cl_ops; op != NULL; op = op->co_next)
    {
	/* If CIF layers are used, switch to the max of current
	 * distances and those of the layers used.
	 */
	
	if (!TTMaskEqual(&op->co_cifMask, &DBZeroTypeBits))
	{
	    for (i=0; i < des->cs_nLayers; i++)
	    {
		if (TTMaskHasType(&op->co_cifMask, i))
		{
		    if (des->cs_layers[i]->cl_growDist > grow)
			grow = des->cs_layers[i]->cl_growDist;
		    if (des->cs_layers[i]->cl_shrinkDist > shrink)
			shrink = des->cs_layers[i]->cl_shrinkDist;
		}
	    }
	}

	/* Add in grows and shrinks at this step. */

	switch (op->co_opcode)
	{
	    case CIFOP_AND: break;

	    case CIFOP_ANDNOT: break;

	    case CIFOP_OR: break;

	    case CIFOP_GROW:
	    case CIFOP_GROW_G:
		grow += op->co_distance;
		break;
	    
	    case CIFOP_SHRINK:
		shrink += op->co_distance;
		break;
	    
	    /* For bloats use the largest distances (negative values are
	     * for shrinks).
	     */

	    case CIFOP_BLOAT:
		curGrow = curShrink = 0;
		bloats = (BloatData *)op->co_client;
		for (i = 0; i < TT_MAXTYPES; i++)
		{
		    if (bloats->bl_distance[i] > curGrow)
			curGrow = bloats->bl_distance[i];
		    else if ((-bloats->bl_distance[i]) > curShrink)
			curShrink = -bloats->bl_distance[i];
		}
		grow += curGrow;
		shrink += curShrink;
		break;
	    
	    case CIFOP_SQUARES: break;
	    case CIFOP_SQUARES_G: break;
	}
    }

    layer->cl_growDist = grow;
    layer->cl_shrinkDist = shrink;

    /* TxPrintf("Radii for %s: grow %d, shrink %d.\n", layer->cl_name,
	grow, shrink);
     */
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifComputeHalo --
 *
 * Compute grow and shrink distances for each layer, and remember the
 * largest.  Convert from CIF/GDS to Magic internal dimensions.
 *
 * Results:  None.
 * Side effects:  Sets cs_radius value in the current cifoutput style.
 * ----------------------------------------------------------------------------
 */

void
cifComputeHalo(style)
    CIFStyle *style;
{
    int maxGrow, maxShrink, i;

    maxGrow = maxShrink = 0;
    for (i = 0; i < style->cs_nLayers; i++)
    {
	cifComputeRadii(style->cs_layers[i], style);
	if (style->cs_layers[i]->cl_growDist > maxGrow)
	    maxGrow = style->cs_layers[i]->cl_growDist;
	if (style->cs_layers[i]->cl_shrinkDist > maxShrink)
	    maxShrink = style->cs_layers[i]->cl_shrinkDist;
    }
    if (maxGrow > maxShrink)
	style->cs_radius = 2*maxGrow;
    else style->cs_radius = 2*maxShrink;
    style->cs_radius /= style->cs_scaleFactor;
    style->cs_radius++;

    /* TxPrintf("Radius for %s CIF is %d.\n",
     *  style->cs_name, style->cs_radius);
     */
}
	

/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechFinal --
 *
 * 	This procedure is invoked after all the lines of a technology
 *	file have been read.  It checks to make sure that the
 *	section ended at a consistent point, and computes the interaction
 *	distances for hierarchical CIF processing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Error messages are output if there's incomplete stuff left.
 *	Interaction distances get computed for each CIF style
 *	in two steps.  First, for each layer the total grow and
 *	shrink distances are computed.	These are the maximum distances
 *	that edges may move because of grows and shrinks in creating
 *	the layer.  Second, the	radius for the style is computed.
 *	The radius is used in two ways: first to determine how far
 *	apart two subcells may be and still interact during CIF
 *	generation;  and second, to see how much material to yank in
 *	order to find all additional CIF resulting from interactions.
 *	Right now, a conservative approach is used:  use the greater
 *	of twice the largest grow distance or twice the largest shrink
 *	distance for both.  Twice the grow distance must be considered
 *	because two pieces of material may each grow towards the other
 *	and interact in the middle.  Twice the largest shrink distance
 *	is needed because subcells considered individually may each
 *	shrink away from a boundary where they touch;  the parent must
 *	fill in the gap.  To do this, it must include 2S additional
 *	material:  S is the size of the gap that must be filled, but
 *	its outside edge will shrink in by S, so we must start with
 *	2S material to have S left after the shrink.  Finally, one extra
 *	unit gets added because two pieces of material one radius apart
 *	can interact:  to find all this material we must look one unit
 *	farther out for anything overlapping (the search routines only
 *	look for overlapping material and ignore abutting material).
 *
 * ----------------------------------------------------------------------------
 */

void
CIFTechFinal()
{
    CIFStyle *style = CIFCurStyle;
    CIFOp *op;
    int i, minReduce;

    /* Allow the case where there's no CIF at all.  This is indicated
     * by a NULL CIFCurStyle.
     */
    
    if (!style) return;

    if ((cifCurLayer != NULL) && (cifCurOp == NULL) && !cifGotLabels)
    {
	TechError("Layer \"%s\" contains no material.\n",
	    cifCurLayer->cl_name);
    }
    cifCurLayer = NULL;

    /*
     * If cs_expander > 1, then all CIF op values must be scaled accordingly.
     * This routine loops through all CIF output styles.
     */

    CIFTechOutputScale(1, 1);

    if (style->cs_scaleFactor <= 0)
    {
	TechError("No valid scale factor was given for %s CIF.\n",
		style->cs_name);
	style->cs_scaleFactor = 1;
	return;
    }

    /*
     * Make sure that all contact layers include stacked contact types
     */
    for (i = 0; i < style->cs_nLayers; i++)
	for (op = style->cs_layers[i]->cl_ops; op != NULL; op = op->co_next)
	{
	    TileType d, s;
	    TileTypeBitMask *rMask;

	    for (d = TT_TECHDEPBASE; d < DBNumUserLayers; d++)
		if (TTMaskHasType(&op->co_paintMask, d) && DBIsContact(d))
		    for (s = DBNumUserLayers; s < DBNumTypes; s++)
		    {
			rMask = DBResidueMask(s);
			if (TTMaskHasType(rMask, d))
			    TTMaskSetType(&op->co_paintMask, s);
		    }
	}

    /*
     * Find the largest reducer value which divides into all of the CIF values.
     */

    minReduce = style->cs_scaleFactor;
    for (i = 0; i < style->cs_nLayers; i++)
    {
	for (op = style->cs_layers[i]->cl_ops; op != NULL; op = op->co_next)
	{
	    int j, c, bvalue;
	    if (op->co_distance > 0)
	    {
		c = FindGCF(style->cs_scaleFactor, op->co_distance);
		if (c < minReduce) minReduce = c;
	    }
	    if (op->co_client)
	    {
		BloatData *bloats;
		SquaresData *squares;
		SlotsData *slots;
		if (op->co_opcode == CIFOP_SLOTS)
		{
		    slots = (SlotsData *)op->co_client;

		    for (j = 0; j < 7; j++)
		    {
			switch (j) {
			   case 0: bvalue = slots->sl_sborder; break;
			   case 1: bvalue = slots->sl_ssize; break;
			   case 2: bvalue = slots->sl_ssep; break;
			   case 3: bvalue = slots->sl_lborder; break;
			   case 4: bvalue = slots->sl_lsize; break;
			   case 5: bvalue = slots->sl_lsep; break;
			   case 6: bvalue = slots->sl_offset; break;
			}
			if (bvalue != 0)
			{
			    if ((j == 1) || (j == 2) || (j == 4) || (j == 5))
			    {
				if (bvalue & 0x1)
				    TxError("Internal error: slot size/sep %d"
						" cannot be halved.\n", bvalue);
				bvalue >>= 1;
			    }
			    c = FindGCF(style->cs_scaleFactor, bvalue);
			    if (c < minReduce) minReduce = c;
			}
		    }
		}
		if (op->co_opcode == CIFOP_SQUARES)
		{
		    squares = (SquaresData *)op->co_client;

		    for (j = 0; j < 3; j++)
		    {
			switch (j) {
			   case 0: bvalue = squares->sq_border; break;
			   case 1: bvalue = squares->sq_size; break;
			   case 2: bvalue = squares->sq_sep; break;
			}
			if (bvalue != 0)
			{
			    if ((j == 1) || (j == 2))
			    {
				if (bvalue & 0x1)
				    TxError("Internal error: contact size/sep %d"
						" cannot be halved.\n", bvalue);
				bvalue >>= 1;
			    }
			    c = FindGCF(style->cs_scaleFactor, bvalue);
			    if (c < minReduce) minReduce = c;
			}
		    }
		}
		else if (op->co_opcode == CIFOP_SQUARES_G)
		{
		    squares = (SquaresData *)op->co_client;
		    for (j = 0; j < TT_MAXTYPES; j++)
		    {
			switch (j) {
			   case 0: bvalue = squares->sq_border; break;
			   case 1: bvalue = squares->sq_size; break;
			   case 2: bvalue = squares->sq_sep; break;
			   case 3: bvalue = squares->sq_gridx; break;
			   case 4: bvalue = squares->sq_gridy; break;
			}
			if (bvalue != 0)
			{
			    c = FindGCF(style->cs_scaleFactor, bvalue);
			    if (c < minReduce) minReduce = c;
			}
		    }
		}
		/* Presence of op->co_opcode in CIFOP_OR indicates a copy */
		/* of the SquaresData pointer from a following operator	  */
		/* CIFOP_BBOX and CIFOP_MAXRECT uses the co_client field  */
		/* as a flag field, while CIFOP_NET uses it for a string. */
		else
		{
		    switch (op->co_opcode)
		    {
			case CIFOP_OR:
			case CIFOP_BBOX:
			case CIFOP_MAXRECT:
			case CIFOP_NET:
			    break;
			default:
			    bloats = (BloatData *)op->co_client;
			    for (j = 0; j < TT_MAXTYPES; j++)
			    {
				if (bloats->bl_distance[j] != 0)
				{
				    c = FindGCF(style->cs_scaleFactor,
						bloats->bl_distance[j]);
				    if (c < minReduce) minReduce = c;
				}
			    }
		    }
		}
	    }
	    if (minReduce == 1) break;
	}
    }
    style->cs_reducer = minReduce;

    /* Debug info --- Tim, 1/3/02 */
    /* TxPrintf("Output style %s: scaleFactor=%d, reducer=%d, expander=%d.\n",
		style->cs_name, style->cs_scaleFactor,
		style->cs_reducer, style->cs_expander); */

    /* Compute grow and shrink distances for each layer,
     * and remember the largest.
     */
    cifComputeHalo(style);

    /* Go through the layers to see which ones depend on which
     * other ones.  The purpose of this is so that we don't
     * have to yank unnecessary layers in processing subcell
     * interactions.  Also find out which layers involve only
     * a single OR operation, and remember the others specially
     * (they'll require fancy CIF geometry processing).
     */

    for (i = style->cs_nLayers-1; i >= 0; i -= 1)
    {
	TileTypeBitMask ourDepend, ourYank;
	bool needThisLayer;
	int j;

	ourDepend = DBZeroTypeBits;
	ourYank = DBZeroTypeBits;

	/* This layer must be computed hierarchically if it is needed
	 * by some other layer that is computed hierarchically, or if
	 * it includes operations that require hierarchical processing.
	 */

	needThisLayer = TTMaskHasType(&style->cs_hierLayers, i);

	for (op = style->cs_layers[i]->cl_ops; op != NULL;
		    op = op->co_next)
	{
	    BloatData *bloats;

	    TTMaskSetMask(&ourDepend, &op->co_cifMask);
	    TTMaskSetMask(&ourYank, &op->co_paintMask);
	    switch (op->co_opcode)
	    {
		case CIFOP_BLOAT:
		case CIFOP_BLOATMAX:
		case CIFOP_BLOATMIN:
		case CIFOP_BLOATALL:
		    bloats = (BloatData *)op->co_client;
		    for (j = 0; j < TT_MAXTYPES; j++)
		    {
			if (bloats->bl_distance[j] != bloats->bl_distance[TT_SPACE])
			    TTMaskSetType(&ourYank, j);
		    }
		    needThisLayer = TRUE;
		    break;

		case CIFOP_AND:
		case CIFOP_ANDNOT:
		case CIFOP_SHRINK:
		    needThisLayer = TRUE;
		    break;
	    }
	}

	if (needThisLayer)
	{
	    TTMaskSetMask(&style->cs_yankLayers, &ourYank);
	    TTMaskSetType(&style->cs_hierLayers, i);
	    TTMaskSetMask(&style->cs_hierLayers, &ourDepend);
	}
    }

    /* Added by Tim, 10/18/04					*/

    /* Go through the layer operators looking for those that	*/
    /* contain only OR operators followed by a single SQUARES	*/
    /* operator.  If found, set the clientdata record of the OR	*/
    /* operator to be a copy of that for the SQUARES operator.	*/
    /* This is used by the GDS generator when the "gds contact	*/
    /* on" option is enabled (CalmaContactArrays is TRUE) to	*/
    /* translate contact areas into contact subcell arrays.	*/

    for (i = 0; i < style->cs_nLayers; i++)
    {
	ClientData clientdata;
	for (op = style->cs_layers[i]->cl_ops; (op != NULL) &&
			(op->co_opcode == CIFOP_OR) &&
			(TTMaskIsZero(&op->co_cifMask)); op = op->co_next);
	if (op && (op->co_opcode == CIFOP_SQUARES) && (op->co_next == NULL))
	{
	    clientdata = op->co_client;
	    for (op = style->cs_layers[i]->cl_ops; op->co_opcode == CIFOP_OR;
			op = op->co_next)
	    {
		/* Copy the client record from the CIFOP_SQUARES operator
		 * into the CIFOP_OR operator.
		 */
		op->co_client = clientdata;
	    }
	}
    }

    /* Uncomment this code to print out information about which
     * layers have to be processed hierarchically.

    for (i = 0; i < DBNumUserLayers; i++)
    {
	if (TTMaskHasType(&style->cs_yankLayers, i))
	    TxPrintf("Will have to yank %s in style %s.\n",
			DBTypeLongName(i), style->cs_name);
    }
    for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
    {
	if (TTMaskHasType(&style->cs_hierLayers, i))
	    TxPrintf("Layer %s must be processed hierarchically.\n",
			style->cs_layers[i]->cl_name);
    }

     */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFLoadStyle --
 *
 * Re-read the technology file to load the specified technology cif output
 * style into structure CIFCurStyle.  This is much more memory-efficient than
 * keeping a separate structure for each cif output style.  It incurs a complete
 * reading of the tech file on startup and every time the cif output style is
 * changed, but we can assume that this does not happen often.  The first
 * style in the technology file is assumed to be default, so that re-reading
 * the tech file is not necessary on startup unless the default cif output
 * style is changed by a call to "cif ostyle".
 *
 * ----------------------------------------------------------------------------
 */

void
CIFLoadStyle(stylename)
    char *stylename;
{
    SectionID invcif;

    if (CIFCurStyle->cs_name == stylename) return;

    cifTechNewStyle();
    CIFCurStyle->cs_name = stylename;

    invcif = TechSectionGetMask("cifoutput", NULL);
    TechLoad(NULL, invcif);

    /* CIFTechFinal(); */  /* handled by TechLoad() */
    CIFTechOutputScale(DBLambda[0], DBLambda[1]);

    /* If the DRC section makes reference to CIF layers, then	*/
    /* we need to re-read the DRC section as well.		*/

    if ((DRCForceReload == TRUE) && (DRCCurStyle != NULL))
	DRCReloadCurStyle();
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFGetContactSize --
 *
 *   Return the smallest allowable contact size in lambda units corresponding
 *   to the "squares" function operating on the indicated type.  This value
 *   is computed as the (cut size) + 2 * (cut border).
 *
 *   9/12/2013:  Added "squares-grid" and "slots" to the functions understood
 *   by the routine.  "squares-grid" behaves the same as "squares".  "slots"
 *   can be used for contact cuts with differing metal overlap on different
 *   sides.  Normally this would define a square slot;  this routine finds
 *   the minimum cut size for the slot.
 *
 * Results:
 *	Contact minimum dimension, in CIF/GDS units
 *
 * Side effects:
 *	If any of edge, border, or spacing is non-NULL, the appropriate
 *	cut dimension is filled in.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFGetContactSize(type, edge, spacing, border)
    TileType type;
    int *edge;
    int *border;
    int *spacing;
{
    CIFStyle *style = CIFCurStyle;
    CIFOp *op, *sop;
    int i;
    SquaresData *squares;
    SlotsData *slots;

    if (style == NULL)
    {
	edge = spacing = border = 0;
	return 0;
    }

    for (i = 0; i < style->cs_nLayers; i++)
    {
	for (op = style->cs_layers[i]->cl_ops; (op != NULL) &&
			(op->co_opcode == CIFOP_OR) &&
			(TTMaskIsZero(&op->co_cifMask)); op = op->co_next)
	    if (TTMaskHasType(&op->co_paintMask, type))
		for (sop = op->co_next; sop != NULL; sop = sop->co_next)
		{
		    if (sop->co_opcode == CIFOP_SQUARES ||
				sop->co_opcode == CIFOP_SQUARES_G)
		    {
			squares = (SquaresData *)sop->co_client;
			if (edge != NULL) *edge = squares->sq_size;
			if (border != NULL) *border = squares->sq_border;
			if (spacing != NULL) *spacing = squares->sq_sep;
			return (squares->sq_size + (squares->sq_border << 1));
		    }
		    else if (sop->co_opcode == CIFOP_SLOTS)
		    {
			slots = (SlotsData *)sop->co_client;
			if (edge != NULL) *edge = slots->sl_ssize;
			if (border != NULL) *border = slots->sl_sborder;
			if (spacing != NULL) *spacing = slots->sl_ssep;
			return (slots->sl_ssize + (slots->sl_sborder << 1));
		    }

		    /* Anything other than an OR function will break	*/
		    /* the relationship between magic layers and cuts.	*/
		    else if (sop->co_opcode != CIFOP_OR)
			break;
		}
    }
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechOutputScale(n, d) --
 *
 *   Scale all CIF output scale factors to make them equivalent
 *   to reducing the magic internal unit spacing by a factor of n/d.
 *   It is important not to reduce the scaleFactor to zero!  So, we
 *   multiply scaleFactor by n and the expander by d, then reduce
 *   the fraction using the FindGCF routine (utils/fraction.c).
 *
 *   Because all CIF operation distances (grow, shrink, bloat, squares)
 *   are in units of centimicrons multiplied by the expander (e.g.,
 *   expander = 10 means units are in nanometers), we need to rescale them
 *   by the expander.  To optimize CIF output, the fraction scale/expander
 *   is reduced to minimize expander;  that is, to make the numbers in the
 *   CIF output as small as possible while making sure all output can be
 *   represented by integers.
 *
 *   Note that because contacts may be placed at 1/2 grid spacing to
 *   center them, the size and spacing of contacts as given in the 
 *   "squares" function must *always* be an even number.  To ensure this,
 *   we check for odd numbers in these positions.  If there are any, and
 *   "d" is also an odd number, then we multiply both "n" and "d" by 2.
 *
 *   (Added 2/23/05)---The above is not sufficient!  If the contact size
 *   is an odd number, then centering may not be possible even if all
 *   the contact parameters are even numbers.  e.g., for size=22 in a 0.18
 *   process (scalefactor=9), a 5x5 lambda contact is 45x45 centimicrons.
 *   45 - 22 = 23, so border is 11.5 centimicrons.  Therefore, when
 *   the scalefactor is even, the contact size must be even, but when the
 *   scalefactor is odd, then we have to multiply both "n" and "d" by 2
 *   regardless, since contact areas may be either odd or even (compare
 *   the above example to one where the contact is drawn 6x6 lambda).
 *
 * ----------------------------------------------------------------------------
 */

void
CIFTechOutputScale(n, d)
    int n, d;
{
    int i, j, lgcf, lexpand;
    CIFStyle *ostyle = CIFCurStyle;
    CIFLayer *cl;
    CIFOp *op;
    SquaresData *squares;
    SlotsData *slots;
    BloatData *bloats;
    bool has_odd_space;

    if (ostyle == NULL) return;

    /* For contact half-grid centering, check for odd numbers. . . */

    if (ostyle->cs_scaleFactor & 0x1)
    {
	n *= 2;
	d *= 2;
	has_odd_space = TRUE;
    }
    else if (d & 0x1)
    {
	has_odd_space = FALSE;
	for (i = 0; i < ostyle->cs_nLayers; i++)
	{
	    cl = ostyle->cs_layers[i];
	    for (op = cl->cl_ops; op != NULL; op = op->co_next)
	    {
		if (op->co_opcode == CIFOP_SQUARES)
		{
		    squares = (SquaresData *)op->co_client;
		    if ((squares->sq_size & 0x01) || (squares->sq_sep & 0x01))
		    {
			has_odd_space = TRUE;
			break;
		    }
		}
		else if (op->co_opcode == CIFOP_SLOTS)
		{
		    slots = (SlotsData *)op->co_client;
		    if ((slots->sl_lsize & 0x01) || (slots->sl_lsep & 0x01)
			|| (slots->sl_ssize & 0x01) || (slots->sl_ssep & 0x01))
		    {
			has_odd_space = TRUE;
			break;
		    }
		}
	    }

	    if (has_odd_space)
	    {
		n *= 2;
		d *= 2;
		has_odd_space = FALSE;
		break;
	    }
	}
    }

    /* fprintf(stderr, "CIFTechOutputScale(%d, %d)\n", n, d); */

    ostyle->cs_scaleFactor *= n;
    ostyle->cs_expander *= d;

    /* fprintf(stderr, "CIFStyle %s:\n", ostyle->cs_name); */

    lexpand = ostyle->cs_expander;
    for (i = 0; i < ostyle->cs_nLayers; i++)
    {
	cl = ostyle->cs_layers[i];
	for (op = cl->cl_ops; op != NULL; op = op->co_next)
	{
	    if (op->co_distance)
	    {
		op->co_distance *= d;
		lgcf = FindGCF(abs(op->co_distance), ostyle->cs_expander);
		lexpand = FindGCF(lexpand, lgcf);
	    }
	    if (op->co_client)
	    {
		int bvalue, *bptr;
		if (op->co_opcode == CIFOP_SQUARES)
		{
		    squares = (SquaresData *)op->co_client;
		    for (j = 0; j < 3; j++)
		    {
			switch (j) {
			    case 0: bptr = &squares->sq_border; break;
			    case 1: bptr = &squares->sq_size; break;
			    case 2: bptr = &squares->sq_sep; break;
			}
			if (*bptr != 0)
			{
			    (*bptr) *= d;
			    bvalue = abs(*bptr);
			    if ((j == 1) || (j == 2)) bvalue >>= 1;	/* half-grid */
			    if (has_odd_space) bvalue >>= 1;  /* force half-grid */
		            lgcf = FindGCF(bvalue, ostyle->cs_expander);
			    lexpand = FindGCF(lexpand, lgcf);
			}
		    }
		}
		else if (op->co_opcode == CIFOP_SLOTS)
		{
		    slots = (SlotsData *)op->co_client;
		    for (j = 0; j < 7; j++)
		    {
			switch (j) {
			    case 0: bptr = &slots->sl_sborder; break;
			    case 1: bptr = &slots->sl_ssize; break;
			    case 2: bptr = &slots->sl_ssep; break;
			    case 3: bptr = &slots->sl_lborder; break;
			    case 4: bptr = &slots->sl_lsize; break;
			    case 5: bptr = &slots->sl_lsep; break;
			    case 6: bptr = &slots->sl_offset; break;
			}
			if (*bptr != 0)
			{
			    (*bptr) *= d;
			    bvalue = abs(*bptr);
			    if ((j == 1) || (j == 2) || (j == 4) || (j == 5))
				bvalue >>= 1;	/* half-grid */
			    if (has_odd_space) bvalue >>= 1;  /* force half-grid */
		            lgcf = FindGCF(bvalue, ostyle->cs_expander);
			    lexpand = FindGCF(lexpand, lgcf);
			}
		    }
		}
		else if (op->co_opcode == CIFOP_SQUARES_G)
		{
		    squares = (SquaresData *)op->co_client;
		    for (j = 0; j < 5; j++)
		    {
			switch (j) {
			    case 0: bptr = &squares->sq_border; break;
			    case 1: bptr = &squares->sq_size; break;
			    case 2: bptr = &squares->sq_sep; break;
			    case 3: bptr = &squares->sq_gridx; break;
			    case 4: bptr = &squares->sq_gridy; break;
			}
			if (*bptr != 0)
			{
			    (*bptr) *= d;
		            lgcf = FindGCF(abs(*bptr), ostyle->cs_expander);
			    lexpand = FindGCF(lexpand, lgcf);
			}
		    }
		}
		/* Presence of op->co_opcode in CIFOP_OR indicates a copy */
		/* of the SquaresData pointer from a following operator	  */
		/* CIFOP_BBOX uses the co_client field as a flag field.	  */
		else
		{
		    switch (op->co_opcode)
		    {
			case CIFOP_OR:
			case CIFOP_BBOX:
			case CIFOP_MAXRECT:
			case CIFOP_NET:
			    break;
			default:
			    bloats = (BloatData *)op->co_client;
			    for (j = 0; j < TT_MAXTYPES; j++)
			    {
				if (bloats->bl_distance[j] != 0)
				{
				    bloats->bl_distance[j] *= d;
		        	    lgcf = FindGCF(abs(bloats->bl_distance[j]),
						ostyle->cs_expander);
				    lexpand = FindGCF(lexpand, lgcf);
				}
			    }
		    }
		}
	    }
	    /* time-saving quick check */
	    if ((lexpand == 1) && (d == 1)) break;
	}
    }

    /* Recompute drc-cif rule distances */
    drcCifScale(d, 1);

    /* Reduce the scale and all distances by the greatest common	*/
    /* factor of everything.						*/

    /* fprintf(stderr, "All CIF units divisible by %d\n", lexpand);	*/
    /* fflush(stderr); */

    lgcf = FindGCF(ostyle->cs_scaleFactor, ostyle->cs_expander);
    if (lgcf < lexpand) lexpand = lgcf;
    if (lexpand <= 1) return;

    /* fprintf(stderr, "Expander goes from %d to %d\n", ostyle->cs_expander,
     	ostyle->cs_expander / lexpand); */
    /* fprintf(stderr, "All CIF op distances are divided by %d\n", lexpand); */
    /* fflush(stderr); */

    ostyle->cs_scaleFactor /= lexpand;
    ostyle->cs_expander /= lexpand;

    for (i = 0; i < ostyle->cs_nLayers; i++)
    {
	cl = ostyle->cs_layers[i];
	for (op = cl->cl_ops; op != NULL; op = op->co_next)
	{
	    if (op->co_distance)
		op->co_distance /= lexpand;

	    if (op->co_client)
	    {
		int nlayers;
		switch (op->co_opcode)
		{
		    case CIFOP_SLOTS:
			slots = (SlotsData *)op->co_client;
			if (slots->sl_sborder != 0)
			    slots->sl_sborder /= lexpand;
			if (slots->sl_ssize != 0)
			    slots->sl_ssize /= lexpand;
			if (slots->sl_ssep != 0)
			    slots->sl_ssep /= lexpand;
			if (slots->sl_lborder != 0)
			    slots->sl_lborder /= lexpand;
			if (slots->sl_lsize != 0)
			    slots->sl_lsize /= lexpand;
			if (slots->sl_lsep != 0)
			    slots->sl_lsep /= lexpand;
			if (slots->sl_offset != 0)
			    slots->sl_offset /= lexpand;
			break;
		    case CIFOP_SQUARES_G:
			squares = (SquaresData *)op->co_client;
			if (squares->sq_gridx != 0)
			    squares->sq_gridx /= lexpand;
			if (squares->sq_gridy != 0)
			    squares->sq_gridy /= lexpand;
			/* (drop through) */
		    case CIFOP_SQUARES:
			squares = (SquaresData *)op->co_client;
			if (squares->sq_border != 0)
			    squares->sq_border /= lexpand;
			if (squares->sq_size != 0)
			    squares->sq_size /= lexpand;
			if (squares->sq_sep != 0)
			    squares->sq_sep /= lexpand;
			break;
		    case CIFOP_BLOAT:
		    case CIFOP_BLOATMIN:
		    case CIFOP_BLOATMAX:
			bloats = (BloatData *)op->co_client;
			for (j = 0; j < TT_MAXTYPES; j++)
			    if (bloats->bl_distance[j] != 0)
				bloats->bl_distance[j] /= lexpand;
			break;
		    default:
			/* op->co_opcode in CIFOP_OR is a pointer copy	*/
			/* and in CIFOP_BBOX and CIFOP_MAXRECT is a	*/
			/* flag, and in CIFOP_NET is a string.		*/
			break;
		}
	    }
	}
    }

    /* Recompute drc-cif rule distances */
    drcCifScale(1, lexpand);

    /* Recompute value of cs_radius */
    cifComputeHalo(ostyle);
}
