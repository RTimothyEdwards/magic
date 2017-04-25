/*
 * lefTech.c --      
 *
 * This module incorporates the LEF/DEF format for standard-cell place and
 * route.  Defines technology file layer mapping between LEF and magic layer
 * types.
 *
 * Version 0.1 (December 5, 2003):  LEF section of the technology file.
 * Lets the technology file state how layers declared in the LEF and DEF
 * files should correspond to magic layout units.  If these are not
 * declared, magic will attempt to relate layer names in the LEF file to
 * magic layer names and their aliases.  The LEF section allows the declaration
 * of obstruction layer names, which may be the same as actual layer names in
 * the LEF or DEF file, but which should declare a type that takes up space
 * on the routing plane but does not connect to any other material.  Otherwise,
 * some LEF/DEF files will end up merging ports together with the obstruction
 * layer.
 *
 * Layer name case sensitivity should depend on the
 * NAMESCASESENSITIVE [ON|OFF] LEF instruction!
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/lef/lefTech.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "lef/lefInt.h"
#include "drc/drc.h"

/* ---------------------------------------------------------------------*/

/* Layer and Via routing information table.  */

HashTable LefInfo;

/*
 *-----------------------------------------------------------------------------
 *  LefInit --
 *
 *	Things that need to be initialized on startup, before the technology
 *	file is read in.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Hash Table initialization.
 *
 *-----------------------------------------------------------------------------
 */

void
LefInit()
{
    /* Ensure that the table has a null entry so we don't run HashKill	*/
    /* on it when we to the tech initialization.			*/

    LefInfo.ht_table = (HashEntry **) NULL;
}

/*
 *-----------------------------------------------------------------------------
 *	LefTechInit --
 *
 *	Called once at beginning of technology file read-in to initialize
 *	data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears out the LEF layer info table.  Because multiple LEF names
 *	can point to the same record, this is handled by reference count.
 * ----------------------------------------------------------------------------
 */

void
LefTechInit()
{
    HashSearch hs;
    HashEntry *he;
    lefLayer *lefl;

    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (!lefl) continue;
	    lefl->refCnt--;
	    if (lefl->refCnt <= 0)
	    {
		/* Via detailed information, if it exists,	*/
		/* needs to have its allocated memory free'd.	*/

		if (lefl->lefClass == CLASS_VIA)
		    if (lefl->info.via.lr != NULL)
			freeMagic(lefl->info.via.lr);

		freeMagic(lefl);
	    }
	}
	HashKill(&LefInfo);
    }
    HashInit(&LefInfo, 32, HT_STRINGKEYS);
}

/*
 *-----------------------------------------------------------------------------
 * lefRemoveGeneratedVias --
 *
 *	Remove the Generated Vias created during a DEF file write
 *	from the LEF layer hash table.  By design, each of these
 *	generated layers gets a refCnt of zero, so they are easy
 *	to find.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory freed in the hash table.
 * 
 *-----------------------------------------------------------------------------
 */

void
lefRemoveGeneratedVias()
{
    HashSearch hs;
    HashEntry *he;
    lefLayer *lefl;

    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (!lefl) continue;
	    if (lefl->refCnt == 0)
	    {
		if (lefl->lefClass == CLASS_VIA)
		    if (lefl->info.via.lr != NULL)
			freeMagic(lefl->info.via.lr);

		freeMagic(lefl);
		HashSetValue(he, NULL);
	    }
	}
    }
}

/*
 *-----------------------------------------------------------------------------
 *  LefTechLine --
 *
 *	This procedure is invoked by the technology module once for
 *	each line in the "lef" section of the technology file.
 *
 * Results:
 *	Always returns TRUE (otherwise the technology module would
 *	abort Magic with a fatal error).
 *
 * Side effects:
 *	Builds up the LEF layer table, prints error messages if necessary.
 * ----------------------------------------------------------------------------
 */

#define LEFTECH_OBS	0
#define LEFTECH_LAYER	1
#define LEFTECH_ROUTE	2
#define LEFTECH_ROUTING 3
#define LEFTECH_MASTER	4
#define LEFTECH_CUT	5
#define LEFTECH_CONTACT	6
#define LEFTECH_OVERLAP 7
#define LEFTECH_BOUND	8
#define LEFTECH_IGNORE	9

bool
LefTechLine(sectionName, argc, argv)
    char *sectionName;          /* Name of this section (unused). */
    int argc;                   /* Number of arguments on line. */
    char *argv[];               /* Pointers to fields of line. */
{
    bool isObstruction, isContact, isInactive;
    HashEntry *he;
    TileType mtype, mtype2 = -1;
    TileTypeBitMask mmask;
    lefLayer *lefl, *newlefl;
    int i, option;
    static char *keywords[] = {
	"obstruction", "layer", "route", "routing", "masterslice",
	"cut", "contact", "overlap", "bound", "ignore", NULL
    };

    option = Lookup(argv[0], keywords);
    if (option < 0)
    {
        TechError("Unknown LEF section keyword: %s.  Line ignored.\n", argv[0]);
        return TRUE;
    }

    if ((option != LEFTECH_IGNORE) && (argc < 3))
    {
        TechError("No LEF layer names present!\n");
	return TRUE;
    }

    isInactive = FALSE;
    switch (option)
    {
	case LEFTECH_IGNORE:
	    isInactive = TRUE;
	    break;
	case LEFTECH_OBS:
	    isObstruction = TRUE;
	    break;
	default:
	    isObstruction = FALSE;
    }

    TTMaskZero(&mmask);
    i = 0;

    if (!isInactive)
	{
	DBTechNoisyNameMask(argv[1], &mmask);
	for (mtype2 = TT_TECHDEPBASE; mtype2 < DBNumUserLayers; mtype2++)
	{
	    if (TTMaskHasType(&mmask, mtype2))
	    {
		if (++i == 1)
		    mtype = mtype2;
		else
	            break;
	    }
	}
	if (mtype2 == DBNumUserLayers) mtype2 = -1;

	if (i == 0)
	{
	    LefError("Bad magic layer type \"%s\" in LEF layer definition.\n", argv[1]);
	    return TRUE;
	}
	else if ((i == 2) && (option != LEFTECH_OBS))
	{
	    LefError("Can only define multiple types for via obstruction layers.\n");
	    return TRUE;
	}
	else if (i > 2)
	{
	    LefError("Too many types in LEF layer definition.\n");
	    return TRUE;
	}

	isContact = DBIsContact(mtype);
	if (option == LEFTECH_LAYER)
	    option = (isContact) ? LEFTECH_CUT : LEFTECH_ROUTE;
	else if (isContact && (option != LEFTECH_CUT && option != LEFTECH_CONTACT))
            TechError("Attempt to define cut type %s as %s.\n",
			DBTypeLongNameTbl[mtype], keywords[option]);
	else if (!isContact && (option == LEFTECH_CUT || option == LEFTECH_CONTACT))
            TechError("Attempt to define non-cut type %s as a cut.\n",
			DBTypeLongNameTbl[mtype]);
    }

    /* All other aliases are stuffed in the hash table but point to the	*/
    /* same record as the first.  If any name is repeated, then report	*/
    /* an error condition.						*/

    newlefl = NULL;
    for (i = 2 - isInactive; i < argc; i++)
    {
	he = HashFind(&LefInfo, argv[i]);
	lefl = (lefLayer *)HashGetValue(he);
	if (lefl == NULL)
	{
	    /* Create an entry in the hash table for this layer or obstruction */

	    if (newlefl == NULL)
	    {
		float oscale = CIFGetOutputScale(1000);

		newlefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
		newlefl->refCnt = 0;
		newlefl->type = -1;
		newlefl->obsType = -1;
		if (!isInactive)
		{
		    if (isObstruction)
			newlefl->obsType = mtype;
		    else
			newlefl->type = mtype;
		}
		newlefl->canonName = (char *)he->h_key.h_name;

		/* Fill in default values per type */

		switch (option)
		{
		    case LEFTECH_CUT:
			newlefl->lefClass = CLASS_VIA;
			newlefl->info.via.area = GeoNullRect;
			newlefl->info.via.cell = (CellDef *)NULL;
			newlefl->info.via.lr = (LinkedRect *)NULL;
			newlefl->info.via.obsType = mtype2;
			break;
		    case LEFTECH_CONTACT:
			newlefl->lefClass = CLASS_VIA;
			newlefl->info.via.area.r_xtop = DRCGetDefaultLayerWidth(mtype);
			newlefl->info.via.area.r_ytop = newlefl->info.via.area.r_xtop;
			newlefl->info.via.area.r_xbot = -newlefl->info.via.area.r_xtop;
			newlefl->info.via.area.r_ybot = -newlefl->info.via.area.r_ytop;
			newlefl->info.via.cell = (CellDef *)NULL;
			newlefl->info.via.lr = (LinkedRect *)NULL;
			newlefl->info.via.obsType = mtype2;
			break;
		    case LEFTECH_ROUTE:
		    case LEFTECH_ROUTING:
		    case LEFTECH_OBS:
			newlefl->lefClass = CLASS_ROUTE;
			newlefl->info.route.width = DRCGetDefaultLayerWidth(mtype);
			if (newlefl->info.route.width == 0)
			    newlefl->info.route.width = DEFAULT_WIDTH;
			newlefl->info.route.spacing =
					DRCGetDefaultLayerSpacing(mtype, mtype);
			if (newlefl->info.route.spacing == 0)
			    newlefl->info.route.spacing = DEFAULT_SPACING;
			newlefl->info.route.pitch = 0;
			newlefl->info.route.hdirection = TRUE;
			break;
		    case LEFTECH_BOUND:
			newlefl->lefClass = CLASS_BOUND;
			break;
		    case LEFTECH_MASTER:
			newlefl->lefClass = CLASS_MASTER;
			break;
		    case LEFTECH_OVERLAP:
			newlefl->lefClass = CLASS_OVERLAP;
			break;
		    case LEFTECH_IGNORE:
			newlefl->lefClass = CLASS_IGNORE;
			break;
		}

	    }
	    HashSetValue(he, newlefl);
	    newlefl->refCnt++;
	}
	else if (lefl->lefClass != CLASS_IGNORE)
	{
	    if ((lefl->obsType == -1) && isObstruction)
	    {
//		if (DBIsContact(lefl->type) != isContact)
//		    TechError("Error: Cannot mix layer and via types\n");
//		else
		{
		    lefl->obsType = mtype;
		    if (lefl->lefClass == CLASS_VIA)
			lefl->info.via.obsType = mtype2;
		}
	    }
	    else if ((lefl->type == -1) && !isObstruction)
	    {
//		if (DBIsContact(lefl->obsType) != isContact)
//		    TechError("Error: Cannot mix layer and via types\n");
//		else
		    lefl->type = mtype;

	    }
	    else
		TechError("LEF name %s already used for magic type %s\n",
				argv[i], DBTypeLongNameTbl[lefl->type]);
	}
    }
    return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 *  LefTechScale --
 *
 *	Change parameters of the LEF section as required when
 *	redefining magic's internal grid relative to the technology lambda. 
 *
 * ----------------------------------------------------------------------------
 */

void
LefTechScale(scalen, scaled)
    int scalen, scaled;
{
    HashSearch hs;
    HashEntry *he;
    lefLayer *lefl;

    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (!lefl) continue;
	    if (lefl->refCnt >= 1)
	    {
		/* Avoid scaling more than once. . . */
		if (lefl->refCnt > 1) lefl->refCnt = -lefl->refCnt;
		if (lefl->lefClass == CLASS_VIA)
		{
		    DBScalePoint(&lefl->info.via.area.r_ll, scaled, scalen);
		    DBScalePoint(&lefl->info.via.area.r_ur, scaled, scalen);
		}
		else if (lefl->lefClass == CLASS_ROUTE)
		{
		    lefl->info.route.width *= scaled;
		    lefl->info.route.width /= scalen;
		    lefl->info.route.spacing *= scaled;
		    lefl->info.route.spacing /= scalen;
		    lefl->info.route.pitch *= scaled;
		    lefl->info.route.pitch /= scalen;
		}
	    }
	}

	/* Return all refCnt values to normal */
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (!lefl) continue;
	    if (lefl->refCnt < 0)
		lefl->refCnt = -lefl->refCnt;
	}
    }
}
