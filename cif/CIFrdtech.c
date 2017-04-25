/* CIFreadtech.c -
 *
 *	This module processes the portions of technology files that
 *	pertain to reading CIF files, and builds the tables used by
 *	the CIF-reading code.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFrdtech.c,v 1.4 2010/09/15 15:45:30 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "textio/textio.h"
#include "utils/utils.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "calma/calmaInt.h"
#include "utils/malloc.h"

/* Pointer to a list of all the CIF-reading styles: */

CIFReadKeep *cifReadStyleList = NULL;

/* Names of all the CIF layer types used by any read style: */

int cifNReadLayers = 0;
char *(cifReadLayers[MAXCIFRLAYERS]);

/* Table mapping from Calma layer numbers to CIF layers */
HashTable cifCalmaToCif;

/* Variables used to keep track of progress in reading the tech file: */

CIFReadStyle *cifCurReadStyle = NULL;	/* Current style being read. */
CIFReadLayer *cifCurReadLayer;		/* Current layer being processed. */
CIFOp *cifCurReadOp;			/* Last geometric operation seen. */

/* Forward declarations */
void cifReadStyleInit();
void CIFReadLoadStyle();

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadTechLimitScale --
 *
 *	Determine if the scalefactor (ns / ds), applied to the current
 *	grid scaling, would result in a grid finer than the minimum
 *	resolution allowed by the process, as set by the "gridlimit"
 *	statement in the "cifinput" section.
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
CIFReadTechLimitScale(ns, ds)
    int ns, ds;
{
    int gridup, scaledown;
    int scale, limit, mult;

    limit = cifCurReadStyle->crs_gridLimit;
    if (limit == 0) return FALSE;	/* No limit */

    scale = cifCurReadStyle->crs_scaleFactor;
    mult = cifCurReadStyle->crs_multiplier;

    gridup = limit * mult * ds;
    scaledown = scale * ns * 10;

    if ((scaledown / gridup) == 0) return TRUE;
    if ((scaledown % gridup) != 0) return TRUE;
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadNameToType --
 *
 * 	This procedure finds the type (integer index) of a given
 *	layer name.
 *
 * Results:
 *	The return value is the type.  If we ran out of space in
 *	the CIF layer table, or if the layer wasn't recognized and
 *	it isn't OK to make a new layer, -1 gets returned.
 *
 * Side effects:
 *	If no layer exists by the given name and newOK is TRUE, a
 *	new layer is created.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFReadNameToType(name, newOK)
    char *name;		/* Name of a CIF layer. */
    bool newOK;		/* TRUE means OK to create a new layer if this
			 * name is one we haven't seen before.
			 */
{
    int i;
    static bool errorPrinted = FALSE;

    for (i=0; i < cifNReadLayers; i += 1)
    {
	/* Only accept this layer if it's in the current CIF style or
	 * it's OK to add new layers to the current style.
	 */
	
	if (!TTMaskHasType(&cifCurReadStyle->crs_cifLayers, i) && !newOK)
	    continue;
	if (strcmp(cifReadLayers[i], name) == 0)
	{
	    if (newOK) TTMaskSetType(&cifCurReadStyle->crs_cifLayers, i);
	    return i;
	}
    }

    /* This name isn't in the table.  Return an error or make a new entry. */

    if (!newOK) return -1;

    if (cifNReadLayers == MAXCIFRLAYERS)
    {
	if (!errorPrinted)
	{
	    TxError("CIF read layer table ran out of space at %d layers.\n",
		    MAXCIFRLAYERS);
	    TxError("Get your Magic maintainer to increase the table size.\n");
	    errorPrinted = TRUE;
	}
	return -1;
    }

    (void) StrDup(&(cifReadLayers[cifNReadLayers]), name);
    TTMaskSetType(&cifCurReadStyle->crs_cifLayers, cifNReadLayers);
    cifNReadLayers += 1;
    return cifNReadLayers-1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFCalmaLayerToCifLayer --
 *
 * Find the CIF number of the layer matching the supplied Calma
 * layer number and datatype.
 *
 * Results:
 *	Returns the CIF number of the above layer, or -1 if it
 *	can't be found.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFCalmaLayerToCifLayer(layer, datatype, calmaStyle)
    int layer;		/* Calma layer number */
    int datatype;	/* Calma datatype */
    CIFReadStyle *calmaStyle;
{
    CalmaLayerType clt;
    HashEntry *he;

    clt.clt_layer = layer;
    clt.clt_type = datatype;
    if (he = HashLookOnly(&(calmaStyle->cifCalmaToCif), (char *) &clt))
      return ((spointertype) HashGetValue(he));

    /* Try wildcarding the datatype */
    clt.clt_type = -1;
    if (he = HashLookOnly(&(calmaStyle->cifCalmaToCif), (char *) &clt))
      return ((spointertype) HashGetValue(he));

    /* Try wildcarding the layer */
    clt.clt_layer = -1;
    clt.clt_type = datatype;
    if (he = HashLookOnly(&(calmaStyle->cifCalmaToCif), (char *) &clt))
      return ((spointertype) HashGetValue(he));

    /* Try wildcarding them both, for a default value */
    clt.clt_layer = -1;
    clt.clt_type = -1;
    if (he = HashLookOnly(&(calmaStyle->cifCalmaToCif), (char *) &clt))
      return ((spointertype) HashGetValue(he));

    /* No luck */
    return (-1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseReadLayers --
 *
 * 	Given a comma-separated list of CIF layer names, builds a
 *	bit mask of all those layer names.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the parameter pointed to by mask so that it contains
 *	a mask of all the CIF layers indicated.  If any of the CIF
 *	layers didn't exist, new ones are created.  If we run out
 *	of CIF layers, an error message is output.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFParseReadLayers(string, mask)
    char *string;		/* Comma-separated list of CIF layers. */
    TileTypeBitMask *mask;	/* Where to store bit mask. */
{
    int i;
    char *p;

    TTMaskZero(mask);

    /* Break the string up into the chunks between commas. */

    while (*string != 0)
    {
	p = strchr(string, ',');
	if (p != NULL)
	    *p = 0;
	
	i = CIFReadNameToType(string, TRUE);
	if (i >= 0)
	    TTMaskSetType(mask, i);
	else
	{
	    HashEntry *he;
	    TileTypeBitMask *amask;

	    he = HashLookOnly(&DBTypeAliasTable, string);
	    if (he != NULL)
	    {
		amask = (TileTypeBitMask *)HashGetValue(he);
		TTMaskSetMask(mask, amask);
	    }
	}

	if (p == NULL) break;
	*p = ',';
	for (string = p; *string == ','; string += 1) /* do nothing */;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifNewReadStyle --
 *
 * 	This procedure creates a new CIF read style
 *	and initializes it to completely null.  cifCurReadStyle is
 *	set to point to the new structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any information previously in cifCurReadStyle is destroyed and
 *	the memory allocation freed.
 *
 * ----------------------------------------------------------------------------
 */

void
cifNewReadStyle()
{
    int i;
    CIFOp	 *op;
    CIFReadLayer *layer;

    if (cifCurReadStyle != NULL)
    {
	/* Destroy old style and free all memory allocated to it */

	for (i=0; i<MAXCIFRLAYERS; i+=1)
	{
	    layer = cifCurReadStyle->crs_layers[i];
	    if (layer != NULL)
	    {
		for (op = layer->crl_ops; op != NULL; op = op->co_next)
		    freeMagic((char *)op);
		freeMagic((char *)layer);
	    }
	}

	/* Destroy the calma mapping HashTable */
	HashKill(&(cifCurReadStyle->cifCalmaToCif));
	freeMagic((char *)cifCurReadStyle);
    }
    cifCurReadStyle = (CIFReadStyle *) mallocMagic(sizeof(CIFReadStyle));
    cifReadStyleInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifReadStyleInit --
 *
 * Fill in the current cif input style structure with initial values
 *
 * ----------------------------------------------------------------------------
 */

void
cifReadStyleInit()
{
    int i;

    cifCurReadStyle->crs_name = NULL;
    cifCurReadStyle->crs_status = TECH_NOT_LOADED;

    cifCurReadStyle->crs_cifLayers = DBZeroTypeBits;
    cifCurReadStyle->crs_nLayers = 0;
    cifCurReadStyle->crs_scaleFactor = 0;
    cifCurReadStyle->crs_multiplier = 1;
    cifCurReadStyle->crs_gridLimit = 0;
    cifCurReadStyle->crs_flags = 0;
    HashInit(&(cifCurReadStyle->cifCalmaToCif), 64,
      sizeof (CalmaLayerType) / sizeof (unsigned));
    for (i = 0; i < MAXCIFRLAYERS; i++)
    {
	cifCurReadStyle->crs_labelLayer[i] = TT_SPACE;
	cifCurReadStyle->crs_layers[i] = NULL;
    }
}

/*
 *
 * ----------------------------------------------------------------------------
 *
 * CIFReadTechInit --
 *
 * 	Called to delete all structures associated with the CIF istyle
 *	tech prior to reading a new technology.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out the layer data structure.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFReadTechInit()
{
    CIFReadKeep *style;
            
    /* Cleanup any old info. */
 
    cifNewReadStyle();
    freeMagic(cifCurReadStyle);
    cifCurReadStyle = NULL;
 
    /* forget the list of styles */
 
    for (style = cifReadStyleList; style != NULL; style = style->crs_next)
    {   
        freeMagic(style->crs_name);
        freeMagic(style);
    }
    cifReadStyleList = NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadTechStyleInit --
 *
 * 	Called once at the beginning of technology file read-in to
 *	initialize data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out the layer data structure.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFReadTechStyleInit()
{
    cifNReadLayers = 0;
    cifCurReadLayer = NULL;
    cifCurReadOp = NULL;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadTechLine --
 *
 * 	This procedure is called once by the tech module for each line
 *	in the "cifinput" section of the technology file.
 *
 * Results:
 *	Always return TRUE.
 *
 * Side effects:
 *	Sets up information in the tables used to read CIF, and prints
 *	error messages if problems arise.
 *
 * ----------------------------------------------------------------------------
 */
	/* ARGSUSED */
bool
CIFReadTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section ("cifinput"). */
    int argc;			/* Number of fields on line. */
    char *argv[];		/* Values of fields. */
{
    CIFOp *newOp = NULL;
    CIFReadKeep *newStyle, *p;
    HashEntry *he;
    CalmaLayerType clt;
    int calmaLayers[CALMA_LAYER_MAX], calmaTypes[CALMA_LAYER_MAX];
    int nCalmaLayers, nCalmaTypes, l, t, j;

    if (argc <= 0) return TRUE;
    else if (argc >= 2) l = strlen(argv[1]);

    /* See if we're starting a new style.  If so, create it.  If not,
     * make sure there's already a style around, and create one if
     * there isn't.
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
	for (newStyle = cifReadStyleList; newStyle != NULL; 
		newStyle = newStyle->crs_next)
	{
	    if (!strncmp(newStyle->crs_name, argv[1], l))
		break;
	}
	if (newStyle == NULL)
	{
	    if (argc == 2)
	    {
		newStyle = (CIFReadKeep *)mallocMagic(sizeof(CIFReadKeep));
		newStyle->crs_next = NULL;
		newStyle->crs_name = StrDup((char **) NULL, argv[1]);

		/* Append to end of style list */
		if (cifReadStyleList == NULL)
		    cifReadStyleList = newStyle;
		else
		{
		    for (p = cifReadStyleList; p->crs_next; p = p->crs_next);
		    p->crs_next = newStyle;
		}
	    }
	    else	/* Handle style variants */
	    {
		CIFReadKeep *saveStyle = NULL;
		char *tptr, *cptr;

		/* 4th argument is a comma-separated list of variants.	*/
		/* In addition to the default name recorded above,	*/
		/* record each of the variants.				*/

		tptr = argv[3];
		while (*tptr != '\0')
		{
		    cptr = strchr(tptr, ',');
		    if (cptr != NULL) *cptr = '\0';
		    newStyle = (CIFReadKeep *)mallocMagic(sizeof(CIFReadKeep));
		    newStyle->crs_next = NULL;
		    newStyle->crs_name = (char *)mallocMagic(strlen(argv[1])
				+ strlen(tptr) + 1);
		    sprintf(newStyle->crs_name, "%s%s", argv[1], tptr);

		    /* Remember the first variant as the default */
		    if (saveStyle == NULL) saveStyle= newStyle;

		    /* Append to end of style list */
		    if (cifReadStyleList == NULL)
			cifReadStyleList = newStyle;
		    else
		    {
			for (p = cifReadStyleList; p->crs_next; p = p->crs_next);
			p->crs_next = newStyle;
		    }
		    
		    if (cptr == NULL)
			break;
		    else
			tptr = cptr + 1;
		}
		newStyle = saveStyle;
	    }
	}
	
	if (cifCurReadStyle == NULL)
	{
	    cifNewReadStyle();
	    cifCurReadStyle->crs_name = newStyle->crs_name;
	    cifCurReadStyle->crs_status = TECH_PENDING;
	}
	else if ((cifCurReadStyle->crs_status == TECH_PENDING) ||
			(cifCurReadStyle->crs_status == TECH_SUSPENDED))
	    cifCurReadStyle->crs_status = TECH_LOADED;
	else if (cifCurReadStyle->crs_status == TECH_NOT_LOADED)
	{
	    if (cifCurReadStyle->crs_name == NULL)
		return (FALSE);
	    else if (argc == 2)
	    {
		if (!strcmp(argv[1], cifCurReadStyle->crs_name))
		    cifCurReadStyle->crs_status = TECH_PENDING;
	    }
	    else if (argc == 4)
	    {
		/* Verify that the style matches one variant */

		char *tptr, *cptr;

		if (!strncmp(cifCurReadStyle->crs_name, argv[1], l))
		{
		    tptr = argv[3];
		    while (*tptr != '\0')
		    {
			cptr = strchr(tptr, ',');
			if (cptr != NULL) *cptr = '\0';
			if (!strcmp(cifCurReadStyle->crs_name + l, tptr))
			{
			    cifCurReadStyle->crs_status = TECH_PENDING;
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

    /* Only continue past this point if we are loading the cif input style */
    if (cifCurReadStyle == NULL) return FALSE;
    if ((cifCurReadStyle->crs_status != TECH_PENDING) &&
		(cifCurReadStyle->crs_status != TECH_SUSPENDED)) return TRUE;
    
    /* Process scalefactor lines next. */

    if (strcmp(argv[0], "scalefactor") == 0)
    {
	if ((argc < 2) || (argc > 4)) goto wrongNumArgs;
	cifCurReadStyle->crs_scaleFactor = CIFParseScale(argv[1],
		&cifCurReadStyle->crs_multiplier);

	/*
	 * The "nanometers" keyword multiplies the multiplier by 10.
	 * Keyword "calmaonly" is now ignored.
	 */

	if (argc >= 3)
	{
	    if(!strncmp(argv[argc - 1], "nanom", 5))
		cifCurReadStyle->crs_multiplier = 10;
	}

	if (cifCurReadStyle->crs_scaleFactor <= 0)
	{
	    cifCurReadStyle->crs_scaleFactor = 0;
	    TechError("Scalefactor must be a strictly positive value.\n");
	    goto errorReturn;
	}
	return TRUE;
    }

    /* Process "gridlimit" lines. */
    
    if (strncmp(argv[0], "grid", 4) == 0)
    {
        if (StrIsInt(argv[1]))
        {
            cifCurReadStyle->crs_gridLimit = atoi(argv[1]);
            if (cifCurReadStyle->crs_gridLimit < 0)
            {
                TechError("Grid limit must be a positive integer.\n");
                cifCurReadStyle->crs_gridLimit = 0;
            }
        }
        else
	{
            TechError("Unable to parse grid limit value.\n");
	    goto errorReturn;
	}
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
		cifCurReadStyle->crs_status = TECH_PENDING;
		return TRUE;
	    }
	    else
	    {
		l = strlen(cifCurReadStyle->crs_name) - strlen(tptr);
		if (!strcmp(tptr, cifCurReadStyle->crs_name + l))
		{
		    cifCurReadStyle->crs_status = TECH_PENDING;
		    return TRUE;
		}
	    }
	
	    if (cptr == NULL)
		break;
	    else
		tptr = cptr + 1;
	}
	cifCurReadStyle->crs_status = TECH_SUSPENDED;
    }

    /* Anything below this line is not parsed if we're in TECH_SUSPENDED mode */
    if (cifCurReadStyle->crs_status != TECH_PENDING) return TRUE;

    /* Process layer lines next.	*/

    if (strcmp(argv[0], "layer") == 0)
    {
	TileType type;

	cifCurReadLayer = NULL;
	cifCurReadOp = NULL;
	if (cifCurReadStyle->crs_nLayers == MAXCIFRLAYERS)
	{
	    TechError("Can't handle more than %d layers per style.\n",
		    MAXCIFRLAYERS);
	    TechError("Your local Magic wizard can increase the table size.\n");
	    goto errorReturn;
	}
        if ((argc != 2) && (argc != 3)) goto wrongNumArgs;
	type = DBTechNoisyNameType(argv[1]);
	if (type < 0) goto errorReturn;

	cifCurReadLayer = (CIFReadLayer *) mallocMagic(sizeof(CIFReadLayer));
	cifCurReadStyle->crs_layers[cifCurReadStyle->crs_nLayers]
		= cifCurReadLayer;
	cifCurReadStyle->crs_nLayers += 1;
	cifCurReadLayer->crl_magicType = type;
	cifCurReadLayer->crl_ops = NULL;
	cifCurReadLayer->crl_flags = CIFR_SIMPLE;

	/* Handle a special case of a list of layer names on the
	 * layer line.  Turn them into an OR operation.
	 */
	
	if (argc == 3)
	{
	    cifCurReadOp = (CIFOp *) mallocMagic(sizeof(CIFOp));
	    cifCurReadOp->co_opcode = CIFOP_OR;
	    cifCurReadOp->co_client = (ClientData)NULL;
	    CIFParseReadLayers(argv[2], &cifCurReadOp->co_cifMask);
	    TTMaskZero(&cifCurReadOp->co_paintMask);
	    cifCurReadOp->co_next = NULL;
	    cifCurReadOp->co_distance = 0;
	    cifCurReadLayer->crl_ops = cifCurReadOp;
	}
	return TRUE;
    }

    /* Process templayer lines next.  (templayers in cifinput added 5/3/09) */
    /* Fault handling deprecated 5/5/16;  treat as templayer and flag a	    */
    /* warning.								    */

    if ((strcmp(argv[0], "templayer") == 0) || (strcmp(argv[0], "fault") == 0))
    {
	TileType type;

	cifCurReadLayer = NULL;
	cifCurReadOp = NULL;
	if (cifCurReadStyle->crs_nLayers == MAXCIFRLAYERS)
	{
	    TechError("Can't handle more than %d layers per style.\n",
		    MAXCIFRLAYERS);
	    TechError("Your local Magic wizard can increase the table size.\n");
	    goto errorReturn;
	}
        if ((argc != 2) && (argc != 3)) goto wrongNumArgs;
	type = CIFReadNameToType(argv[1], TRUE);
	if (type < 0) goto errorReturn;

	if (*argv[0] == 'f')
	    TechError("Error:  Fault layers deprecated.  Treating as templayer\n");

	cifCurReadLayer = (CIFReadLayer *) mallocMagic(sizeof(CIFReadLayer));
	cifCurReadStyle->crs_layers[cifCurReadStyle->crs_nLayers]
		= cifCurReadLayer;
	cifCurReadStyle->crs_nLayers += 1;
	cifCurReadLayer->crl_magicType = type;
	cifCurReadLayer->crl_ops = NULL;
	cifCurReadLayer->crl_flags = CIFR_TEMPLAYER | CIFR_SIMPLE;

	/* Handle a special case of a list of layer names on the
	 * layer line.  Turn them into an OR operation.
	 */
	
	if (argc == 3)
	{
	    cifCurReadOp = (CIFOp *) mallocMagic(sizeof(CIFOp));
	    cifCurReadOp->co_opcode = CIFOP_OR;
	    cifCurReadOp->co_client = (ClientData)NULL;
	    CIFParseReadLayers(argv[2], &cifCurReadOp->co_cifMask);
	    TTMaskZero(&cifCurReadOp->co_paintMask);
	    cifCurReadOp->co_next = NULL;
	    cifCurReadOp->co_distance = 0;
	    cifCurReadLayer->crl_ops = cifCurReadOp;
	}
	return TRUE;
    }

    /* Process mapping between CIF layers and calma layers/types */
    if ((strcmp(argv[0], "calma") == 0) || (strncmp(argv[0], "gds", 3) == 0))
    {
	int cifnum;

	if (argc != 4) goto wrongNumArgs;
	cifnum = CIFReadNameToType(argv[1], FALSE);
	if (cifnum < 0)
	{
	    TechError("Unrecognized CIF layer: \"%s\"\n", argv[1]);
	    return TRUE;
	}
	nCalmaLayers = cifParseCalmaNums(argv[2], calmaLayers, CALMA_LAYER_MAX);
	nCalmaTypes = cifParseCalmaNums(argv[3], calmaTypes, CALMA_LAYER_MAX);
	if (nCalmaLayers <= 0 || nCalmaTypes <= 0)
	    return (TRUE);

	for (l = 0; l < nCalmaLayers; l++)
	{
	    for (t = 0; t < nCalmaTypes; t++)
	    {
		clt.clt_layer = calmaLayers[l];
		clt.clt_type = calmaTypes[t];
		he = HashFind(&(cifCurReadStyle->cifCalmaToCif),
		  (char *) &clt);
		HashSetValue(he, (ClientData)(pointertype) cifnum);
	    }
	}
	return TRUE;
    }

    /* Figure out which Magic layer should get labels from which
     * CIF layers.
     */
    
    if (strcmp(argv[0], "labels") == 0)
    {
	TileTypeBitMask mask;
	int i;

	if (cifCurReadLayer == NULL)
	{
	    TechError("Must define layer before giving labels it holds.\n");
	    goto errorReturn;
	}
	if (argc != 2)
	{
	    if (argc == 3)
	    {
		if (strcmp(argv[2], "text"))
		    goto wrongNumArgs;
	    }
	    else
		goto wrongNumArgs;
	}
	CIFParseReadLayers(argv[1], &mask);
	for (i=0; i<MAXCIFRLAYERS; i+=1)
	{
	    if (TTMaskHasType(&mask,i))
	    {
		cifCurReadStyle->crs_labelLayer[i]
			= cifCurReadLayer->crl_magicType;
		if (argc == 3)
		    cifCurReadStyle->crs_layers[i]->crl_flags |= CIFR_TEXTLABELS;
	    }
	}
	return TRUE;
    }

    /* Parse "ignore" lines:  look up the layers to enter them in
     * the table of known layers, but don't do anything else.  This
     * will cause the layers to be ignored when encountered in
     * cells.
     */
    
    if (strcmp(argv[0], "ignore") == 0)
    {
	TileTypeBitMask mask;
	int		i;

	if (argc != 2) goto wrongNumArgs;
	CIFParseReadLayers(argv[1], &mask);
	/* trash the value in crs_labelLayer so that any labels on this
	   layer get junked, also. dcs 4/11/90
        */
	for (i=0; i < cifNReadLayers; i++)
	{
	     if (TTMaskHasType(&mask,i))
	     {
	     	  if (cifCurReadStyle->crs_labelLayer[i] == TT_SPACE)
		  {
		       cifCurReadStyle->crs_labelLayer[i] = -1;
		  }
	     }
	}
	return TRUE;
    }

    /* miscellaneous cif-reading boolean options */

    if(strcmp(argv[0], "options") == 0) {
	int i;
	if (argc < 2) goto wrongNumArgs;
	for(i = 1; i < argc; i++) {
	    if(strcmp(argv[i], "ignore-unknown-layer-labels") == 0)
		cifCurReadStyle->crs_flags |= CRF_IGNORE_UNKNOWNLAYER_LABELS;
	    if(strcmp(argv[i], "no-reconnect-labels") == 0)
		cifCurReadStyle->crs_flags |= CRF_NO_RECONNECT_LABELS;
	}
	return TRUE;
    }
    
    /* Anything below here is a geometric operation, so we can
     * do some set-up that is common to all the operations.
     */
    
    if (cifCurReadLayer == NULL)
    {
	TechError("Must define layer before specifying operations.\n");
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
    else if (strcmp(argv[0], "copyup") == 0)
	newOp->co_opcode = CIFOP_COPYUP;
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
	case CIFOP_COPYUP:
	    if (argc != 2) goto wrongNumArgs;
	    CIFParseReadLayers(argv[1], &newOp->co_cifMask);
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
    }

    /* Link the new CIFOp onto the list. */

    if (cifCurReadOp == NULL)
    {
	cifCurReadLayer->crl_ops = newOp;
	if (newOp->co_opcode != CIFOP_OR)
	    cifCurReadLayer->crl_flags &= ~CIFR_SIMPLE;
    }
    else
    {
        cifCurReadOp->co_next = newOp;
	cifCurReadLayer->crl_flags &= ~CIFR_SIMPLE;
    }
    cifCurReadOp = newOp;

    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadTechFinal --
 *
 * 	This procedure is invoked after all the lines of a technology
 *	file have been read.  It checks to make sure that the information
 *	read in "cifinput" sections is reasonably complete.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Error messages may be output.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFReadTechFinal()
{
    /* Reduce the scale by the multiplier, as much as possible while	*/
    /* keeping all CIF input ops in integer units.			*/
    /* Calling with scale 1:1 does no actual scaling, but does find any	*/
    /* common factors which can be divided out and reassigned to the 	*/
    /* cif input multiplier factor.					*/

    if (cifCurReadStyle == NULL) return;

    /* Make sure the current style has a valid scalefactor. */

    if (cifCurReadStyle->crs_scaleFactor <= 0)
    {
	TechError("CIF input style \"%s\" bad scalefactor; using 1.\n",
		    cifCurReadStyle->crs_name);
	 cifCurReadStyle->crs_scaleFactor = 1;
    }

    CIFTechInputScale(1, 1, TRUE);

    /* debug info --- Tim, 1/3/02 */
    TxPrintf("Input style %s: scaleFactor=%d, multiplier=%d\n",
		cifCurReadStyle->crs_name, cifCurReadStyle->crs_scaleFactor,
		cifCurReadStyle->crs_multiplier);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadLoadStyle --
 *
 * Re-read the technology file to load the specified technology cif input
 * style into structure cifCurReadStyle.  This is much more memory-efficient than
 * keeping a separate structure for each cif input style.  It incurs a complete
 * reading of the tech file on startup and every time the cif input style is
 * changed, but we can assume that this does not happen often.  The first
 * style in the technology file is assumed to be default, so that re-reading
 * the tech file is not necessary on startup unless the default cif input
 * style is changed by a call to "cif istyle".
 *
 * ----------------------------------------------------------------------------
 */
void
CIFReadLoadStyle(stylename)
    char *stylename;
{
    SectionID invcifr;

    if (cifCurReadStyle->crs_name == stylename) return;

    cifNewReadStyle();
    cifCurReadStyle->crs_name = stylename;

    invcifr = TechSectionGetMask("cifinput", NULL);
    TechLoad(NULL, invcifr);

    /* CIFReadTechFinal(); */  /* Taken care of by TechLoad() */
    CIFTechInputScale(DBLambda[0], DBLambda[1], TRUE);
}
    
/*
 * ----------------------------------------------------------------------------
 *
 * CIFGetInputScale --
 *
 *      This routine is given here so that the CIF input scale can be
 *      accessed from the "commands" directory source without declaring
 *      external references to CIF global variables.
 *
 * Results:
 *      Internal units-to-(nanometers * convert) conversion factor (float).
 *
 * Side effects:
 *      None.
 *
 * ----------------------------------------------------------------------------
 */

float
CIFGetInputScale(convert)
   int convert;
{
    /* Avoid divide-by-0 error if there is no cif input style	*/
    /* in the tech file.					*/
    if (!cifCurReadStyle)
    {
	TxError("Error: No style is set\n");
	return (float)0;
    }

    /* NOTE: convert = 1000 for centimicrons to microns conversion */
    return ((float)(10 * cifCurReadStyle->crs_scaleFactor)
                / (float)(cifCurReadStyle->crs_multiplier * convert));
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFPrintReadStyle --
 *
 * 	Print the current CIF read style or a list of available styles.
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
CIFPrintReadStyle(dolist, doforall, docurrent)
    bool dolist;		/* Return as a list if true */
    bool doforall;		/* Return list of all styles if true */
    bool docurrent;		/* Return current style if true */
{
    CIFReadKeep *style;

    if (docurrent)
    {
	if (cifCurReadStyle == NULL)
	    TxError("Error: No style is set\n");
	else
	{
	    if (!dolist) TxPrintf("The current style is \"");
#ifdef MAGIC_WRAPPER
	    if (dolist)
		Tcl_SetResult(magicinterp, cifCurReadStyle->crs_name, NULL);
	    else
#endif
	    TxPrintf("%s", cifCurReadStyle->crs_name);
	    if (!dolist) TxPrintf("\".\n");
	}
    }

    if (doforall)
    {

	if (!dolist) TxPrintf("The CIF input styles are: ");

	for (style = cifReadStyleList; style != NULL; style = style->crs_next)
	{
	    if (dolist)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_AppendElement(magicinterp, style->crs_name);
#else
		if (style != cifReadStyleList) TxPrintf(" ");
		TxPrintf("%s", style->crs_name);
#endif
	    }
	    else
	    {
		if (style != cifReadStyleList) TxPrintf(", ");
		TxPrintf("%s", style->crs_name);
	    }
	}
	if (!dolist) TxPrintf(".\n");
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * CIFSetReadStyle --
 *
 * 	This procedure changes the current style used for reading
 *	CIF.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The CIF style is changed to the one specified by name.  If
 *	there is no style by that name, then a list of all valid
 *	styles is output.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFSetReadStyle(name)
    char *name;			/* Name of the new style.  If NULL,
				 * just print the name of the current
				 * style.
				 */
{
    CIFReadKeep *style, *match;
    int length;

    if (name == NULL) return;

    match = NULL;
    length = strlen(name);
    for (style = cifReadStyleList; style != NULL; style = style->crs_next)
    {
	if (strncmp(name, style->crs_name, length) == 0)
	{
	    if (match != NULL)
	    {
		TxError("CIF input style \"%s\" is ambiguous.\n", name);
		CIFPrintReadStyle(FALSE, TRUE, TRUE);
		return;
	    }
	    match = style;
	}
    }

    if (match != NULL)
    {
	CIFReadLoadStyle(match->crs_name);
	TxPrintf("CIF input style is now \"%s\"\n", name);
	return;
    }

    TxError("\"%s\" is not one of the CIF input styles Magic knows.\n", name);
    CIFPrintReadStyle(FALSE, TRUE, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseCalmaNums --
 *
 * Parse a comma-separated list of Calma numbers.  Each number in
 * the list must be between 0 and CALMA_LAYER_MAX, or an asterisk
 * "*".  Store each number in the array 'numArray', which has space
 * for up to 'numNums' numbers.  An asterisk is stored as -1.
 *
 * Results:
 *	Returns the number of numbers added to the array, or
 *	-1 on error.
 *
 * Side effects:
 *	Adds numbers to the array.  If there were too many numbers,
 *	or some of the numbers were not legal Calma numbers, we
 *	print an error message.
 *
 * ----------------------------------------------------------------------------
 */

int
cifParseCalmaNums(str, numArray, numNums)
    char *str;	/* String to parse */
    int *numArray;	/* Array to fill in */
    int numNums;	/* Maximum number of entries in numArray */
{
    int numFilled, num;

    for (numFilled = 0; numFilled < numNums; numFilled++)
    {
	/* Done if at end of string */
	if (*str == '\0')
	    return (numFilled);

	/* Is it a wild-card (*)? */
	if (*str == '*') num = -1;
	else
	{
	    num = atoi(str);
	    if (num < 0 || num > CALMA_LAYER_MAX)
	    {
		TechError("Calma layer and type numbers must be 0 to %d.\n",
		    CALMA_LAYER_MAX);
		return (-1);
	    }
	}

	/* Skip to next number */
	while (*str && *str != ',')
	{
	    if (*str != '*' && !isdigit(*str))
	    {
		TechError("Calma layer/type numbers must be numeric or '*'\n");
		return (-1);
	    }
	    str++;
	}

	while (*str && *str == ',') str++;
	numArray[numFilled] = num;
    }

    TechError("Too many layer/type numbers in line; maximum = %d\n", numNums);
    return (-1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFTechInputScale(n, d, opt)
 *
 *   Scale all CIF input scale factors to make them equivalent
 *   to reducing the magic internal unit spacing by a factor of n/d.
 *
 *   After scaling, we attempt to reduce the ratio scaleFactor : multiplier
 *   if there is a common factor.  If "opt" is TRUE, we reduce both values
 *   by this factor.  If FALSE, we do not reduce multiplier below its
 *   original value.  This is important if we are still in the process of
 *   reading CIF or GDS input.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFTechInputScale(n, d, opt)
    int n, d;
    bool opt;
{
    CIFReadStyle *istyle = cifCurReadStyle;
    CIFReadLayer *cl;
    CIFOp *op;
    int lmult, i, lgcf;

    if (istyle == NULL) return 0;

    /* fprintf(stderr, "CIF input style %s:\n", istyle->crs_name); */

    istyle->crs_scaleFactor *= n;
    istyle->crs_multiplier *= d;

    lmult = istyle->crs_multiplier;
    for (i = 0; i < istyle->crs_nLayers; i++)
    {
	cl = istyle->crs_layers[i];
	for (op = cl->crl_ops; op != NULL; op = op->co_next)
	{
	    if (op->co_distance)
	    {
		op->co_distance *= d;
		lgcf = FindGCF(abs(op->co_distance), istyle->crs_multiplier);
		lmult = FindGCF(lmult, lgcf);
		if (lmult == 1) break;
	    }
	}
    }

    /* fprintf(stderr, "All CIF units divisible by %d\n", lmult); */

    lgcf = FindGCF(istyle->crs_scaleFactor, istyle->crs_multiplier);
    if (lgcf < lmult) lmult = lgcf;
    if (lmult == 0) return 0;

    /* fprintf(stderr, "Multiplier goes from %d to %d\n", istyle->crs_multiplier,
		istyle->crs_multiplier / lmult); */

    if (!opt)
    {
	if ((lmult % d) == 0)
	    lmult = d;
	else
	    lmult = 1;
    }

    if (lmult > 1)
    {
	istyle->crs_scaleFactor /= lmult;
	istyle->crs_multiplier /= lmult;

	for (i = 0; i < istyle->crs_nLayers; i++)
	{
	    cl = istyle->crs_layers[i];
	    for (op = cl->crl_ops; op != NULL; op = op->co_next)
		if (op->co_distance)
		    op->co_distance /= lmult;
	}
    }
    return lmult;
}

