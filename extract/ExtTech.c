/*
 * ExtTech.c --
 *
 * Circuit extraction.
 * Code to read and process the sections of a technology file
 * that are specific to circuit extraction.
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
static char sccsid[] = "@(#)ExtTech.c	4.8 MAGIC (Berkeley) 10/26/85";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>		/* for strtod() */
#include <string.h>
#include <math.h>
#include <ctype.h>		/* for isspace() */

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "utils/tech.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "cif/CIFint.h"

/* Whether we are converting units from microns to lambda */
bool doConvert;

/* Current extraction style */
ExtStyle *ExtCurStyle = NULL;

/* List of all styles */
ExtKeep *ExtAllStyles = NULL;

/* Forward declarations */
void extTechFinalStyle();
void ExtLoadStyle();
void ExtTechScale(int, int);

/*
 * Table used for parsing the extract section of a .tech file
 * Each line in the extract section is of a type determined by
 * its first keyword.  There is one entry in the following table
 * for each such keyword.
 */

typedef enum
{
    AREAC, CONTACT, CSCALE,
    DEFAULTAREACAP, DEFAULTOVERLAP, DEFAULTPERIMETER, DEFAULTSIDEOVERLAP,
    DEFAULTSIDEWALL,
    DEVICE, FET, FETRESIST, HEIGHT, LAMBDA, OVERC,
    PERIMC, PLANEORDER, NOPLANEORDER, RESIST, RSCALE, SIDEHALO, SIDEOVERLAP,
    SIDEWALL, STEP, STYLE, SUBSTRATE, UNITS, VARIANT
} Key;

typedef struct
{
    char	*k_name;
    int		 k_key;
    int		 k_minargs;
    int		 k_maxargs;
    char	*k_usage;
} keydesc;

static keydesc keyTable[] = {
    "areacap",		AREAC,		3,	3,
"types capacitance",

    "contact",		CONTACT,	3,	6,
"type resistance",

    "cscale",		CSCALE,		2,	2,
"capacitance-scalefactor",

    "defaultareacap",	DEFAULTAREACAP,	4,	6,
"types plane capacitance",

    "defaultoverlap",	DEFAULTOVERLAP,	6,	6,
"types plane otertypes otherplane capacitance",

    "defaultperimeter",	DEFAULTPERIMETER, 4,	6,
"types plane capacitance",

    "defaultsideoverlap", DEFAULTSIDEOVERLAP, 6, 6,
"types plane othertypes otherplane capacitance",

    "defaultsidewall",	DEFAULTSIDEWALL, 4,	4,
"types plane capacitance",

    "device",		DEVICE,		4,	10,
"device dev-type types options...",

    "fet",		FET,		8,	9,
"types terminal-types min-#-terminals name [subs-types] subs-node gscap gate-chan-cap",

    "fetresist",	FETRESIST,	4,	4,
"type region ohms-per-square",

    "height",		HEIGHT,		4,	4,
"type height-above-subtrate thickness",

    "lambda",		LAMBDA,		2,	2,
"units-per-lambda",

    "overlap",		OVERC,		4,	5,
"toptypes bottomtypes capacitance [shieldtypes]",

    "perimc",		PERIMC,		4,	4,
"intypes outtypes capacitance",

    "planeorder",	PLANEORDER,	3,	3,
"plane index",
    "noplaneordering",	NOPLANEORDER,	1,	1,
"(no arguments needed)",

    "resist",		RESIST,		3,	4,
"types resistance",

    "rscale",		RSCALE,		2,	2,
"resistance-scalefactor",

    "sidehalo",		SIDEHALO,	2,	2,
"halo",

    "sideoverlap",	SIDEOVERLAP,	5,	6,
"intypes outtypes ovtypes capacitance [shieldtypes]",

    "sidewall",		SIDEWALL,	6,	6,
"intypes outtypes neartypes fartypes capacitance",

    "step",		STEP,		2,	2,
"size",

    "style",		STYLE,		2,	4,
"stylename",

    "substrate",	SUBSTRATE,	3,	3,
"types plane",

    "units",		UNITS,		2,	2,
"lambda|microns",

    "variants",		VARIANT,	2,	2,
"style,...",

    0
};


/*
 * Table used for parsing the "device" keyword types
 *
 * (Note: "10" for max types in subcircuit is arbitrary---the parser
 * ignores max types for DEV_SUBCKT and DEV_MSUBCKT).
 */

/* types are enumerated in extract.h */

static keydesc devTable[] = {
    "mosfet",		DEV_MOSFET,		5,	10,
"name gate-types src-types [drn-types] sub-types|None sub-node [gscap gccap]",

    "bjt",		DEV_BJT,		5,	5,
"name base-types emitter-types collector-types",

    "capacitor",	DEV_CAP,		4,	8,
"name top-types bottom-types [sub-types|None sub-node] [[perimcap] areacap]",

    "capreverse",	DEV_CAPREV,		4,	8,
"name bottom-types top-types [sub-types|None sub-node] [[perimcap] areacap]",

    "resistor",		DEV_RES,		4,	6,
"name|None res-types terminal-types [sub-types|None sub-node]",

    "diode",		DEV_DIODE,		4,	6,
"name pos-types neg-types [sub-types|None sub-node]",

    "pdiode",		DEV_PDIODE,		4,	6,
"name pos-types neg-types [sub-types|None sub-node]",

    "ndiode",		DEV_NDIODE,		4,	6,
"name neg-types pos-types [sub-types|None sub-node]",

    "subcircuit",	DEV_SUBCKT,		3,	11,
"name dev-types [N] [term1-types ... termN-types [sub-types|None sub-node]] [options]",

    "rsubcircuit",	DEV_RSUBCKT,		4,	7,
"name dev-types terminal-types [sub-types|None sub-node] [options]",

    "msubcircuit",	DEV_MSUBCKT,		3,	11,
"name dev-types [N] [term1-types ... termN-types [sub-types|None sub-node]] [options]",

    0
};

#ifdef MAGIC_WRAPPER

/*
 * ----------------------------------------------------------------------------
 *
 * ExtCompareStyle --
 *
 *	This routine is designed to work with embedded exttosim and
 *	exttospice.  It determines whether the current extract style
 *	matches the string (picked up from the .ext file).  If so, it
 *	returns TRUE.  If not, it checks whether the style exists in
 *	the list of known files for this technology.  If so, it loads
 *	the correct style and returns TRUE.  If not, it returns FALSE.
 *
 * ----------------------------------------------------------------------------
 */

bool
ExtCompareStyle(stylename)
    char *stylename;
{
    ExtKeep *style;

    if (!strcmp(stylename, ExtCurStyle->exts_name))
	return TRUE;
    else
    {
	for (style = ExtAllStyles; style != NULL; style = style->exts_next)
	{
	    if (!strcmp(stylename, style->exts_name))
	    {
		ExtLoadStyle(stylename);
		return TRUE;
	    }
	}
    }
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtGetDevInfo --
 *
 *	This routine is designed to work with the embedded exttosim and
 *	exttospice commands under the Tcl-based magic, such that all
 *	device information needed by these commands can be picked up
 *	from the current extraction style (as it should!).  This
 *	really should be set up when the extract file is read, which is
 *	why the routine is here, although this is not a very efficient
 *	way to do it (but it needs to be this way to keep backward
 *	compatibility with the non-Tcl, standalone programs ext2sim and
 *	ext2spice).
 *
 *	Note that finding the device by index ("idx") is horribly
 *	inefficient, but keeps the netlist generator separated from
 *	the extractor.  Some of this code is seriously schizophrenic,
 *	and should not be investigated too closely.
 * 
 * Results:
 *	Return FALSE if no device corresponds to index "idx".  TRUE
 *	otherwise.
 *
 * Side Effects:
 *	Fills values in the argument list.
 * ----------------------------------------------------------------------------
 */

bool
ExtGetDevInfo(idx, devnameptr, sd_rclassptr, sub_rclassptr, subnameptr)
    int idx;
    char **devnameptr;
    short *sd_rclassptr;	/* First SD type only---needs to be updated! */
    short *sub_rclassptr;
    char **subnameptr;
{
    TileType t;
    TileTypeBitMask *rmask, *tmask;
    int n, i = 0, j;
    bool repeat;
    char *locdname;
    char **uniquenamelist = (char **)mallocMagic(DBNumTypes * sizeof(char *));


    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	locdname = ExtCurStyle->exts_transName[t];
	if (locdname != NULL)
	{
	    repeat = FALSE;
	    for (j = 0; j < i; j++)
		if (!strcmp(uniquenamelist[j], locdname))
		{
		    repeat = TRUE;
		    break;
		}
	    if (repeat == FALSE)
	    {
		if (i == idx) break;
		uniquenamelist[i] = locdname;
		i++;
	    }
	}
    }
    if (t == DBNumTypes) return FALSE;

    *devnameptr = locdname;
    *subnameptr = ExtCurStyle->exts_transSubstrateName[t];

    tmask = &ExtCurStyle->exts_transSDTypes[t][0];
    *sd_rclassptr = (short)(-1);	/* NO_RESCLASS */

    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
    {
	rmask = &ExtCurStyle->exts_typesByResistClass[n];
	if (TTMaskIntersect(rmask, tmask))
	{
	    *sd_rclassptr = (short)n;
	    break;
	}
    }

    tmask = &ExtCurStyle->exts_transSubstrateTypes[t];
    *sub_rclassptr = (short)(-1);	/* NO_RESCLASS */

    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
    {
        rmask = &ExtCurStyle->exts_typesByResistClass[n];
	if (TTMaskIntersect(rmask, tmask))
	{
	    *sub_rclassptr = (short)(n);
	    break;
	}
    }

    freeMagic(uniquenamelist);
    return TRUE;
}

#endif /* MAGIC_WRAPPER */


#ifdef THREE_D
/*
 * ----------------------------------------------------------------------------
 *
 * ExtGetZAxis --
 * 
 *	Get the height and thickness parameters for a layer (used by the
 *	graphics module which does not have access to internal variables
 *	in the extract section).
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Fills values "height" and "thick" in argument list.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtGetZAxis(tile, height, thick)
    Tile *tile;
    float *height, *thick;
{
    TileType ttype;

    if (ExtCurStyle == NULL) return;

    ttype = TiGetLeftType(tile);        /* Ignore non-Manhattan for now */

    /* Note that w_scale is multiplied by SUBPIXEL to get fixed-point accuracy. */
    /* However, we downshift by only 9 (divide by 512) so that heights are      */
    /* exaggerated in the layout by a factor of 8 (height exaggeration is       */
    /* standard practice for VLSI cross-sections).                              */

    *height = ExtCurStyle->exts_height[ttype];
    *thick = ExtCurStyle->exts_thick[ttype];
}
#endif  /* THREE_D */


/*
 * ----------------------------------------------------------------------------
 *
 * ExtPrintStyle --
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
ExtPrintStyle(dolist, doforall, docurrent)
    bool dolist;
    bool doforall;
    bool docurrent;
{
    ExtKeep *style;

    if (docurrent)
    {
	if (ExtCurStyle == NULL)
	    TxError("Error: No style is set\n");
	else
	{
	    if (!dolist) TxPrintf("The current style is \"");
#ifdef MAGIC_WRAPPER
	    if (dolist)
		Tcl_SetResult(magicinterp, ExtCurStyle->exts_name, NULL);
	    else
#endif
	    TxPrintf("%s", ExtCurStyle->exts_name);
	    if (!dolist) TxPrintf("\".\n");
	}
    }

    if (doforall)
    {
	if (!dolist) TxPrintf("The extraction styles are: ");

	for (style = ExtAllStyles; style; style = style->exts_next)
	{
	    if (dolist)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_AppendElement(magicinterp, style->exts_name);
#else
		if (style != ExtAllStyles) TxPrintf(" ");
		TxPrintf("%s", style->exts_name);
#endif
	    }
	    else
	    {
		if (style != ExtAllStyles) TxPrintf(", ");
		TxPrintf("%s", style->exts_name);
	    }
	}
	if (!dolist) TxPrintf(".\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtSetStyle --
 *
 * Set the current extraction style to 'name', or print
 * the available and current styles if 'name' is NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Just told you.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtSetStyle(name)
    char *name;
{
    ExtKeep *style, *match;
    int length;

    if (name == NULL) return;

    match = NULL;
    length = strlen(name);
    for (style = ExtAllStyles; style; style = style->exts_next)
    {
	if (strncmp(name, style->exts_name, length) == 0)
	{
	    if (match != NULL)
	    {
		TxError("Extraction style \"%s\" is ambiguous.\n", name);
		ExtPrintStyle(FALSE, TRUE, TRUE);
		return;
	    }
	    match = style;
	}
    }

    if (match != NULL)
    {
	ExtLoadStyle(match->exts_name);
	TxPrintf("Extraction style is now \"%s\"\n", name);
	return;
    }

    TxError("\"%s\" is not one of the extraction styles Magic knows.\n", name);
    ExtPrintStyle(FALSE, TRUE, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTechStyleAlloc --
 *
 * Allocate memory for a new extract style structure.
 *
 * ----------------------------------------------------------------------------
 */

ExtStyle *
extTechStyleAlloc()
{
    ExtStyle *style;
    TileType r;

    style = (ExtStyle *) mallocMagic(sizeof (ExtStyle));

    /* Make sure that the memory for character strings is NULL, */
    /* because we want the Init section to free memory if it	*/
    /* has been previously allocated.				*/

    for (r = 0; r < NT; r++)
    {
	style->exts_transSubstrateName[r] = (char *) NULL;
	style->exts_transName[r] = (char *) NULL;
	style->exts_transSDTypes[r] = (TileTypeBitMask *) NULL;
	style->exts_deviceParams[r] = (ParamList *) NULL;
	style->exts_deviceClass[r] = (char) 0;
	style->exts_transResist[r].ht_table = (HashEntry **) NULL;
    }
    return style;
}

/*
 * ----------------------------------------------------------------------------
 *
 * extTechStyleInit --
 *
 * Fill in the extract style structure with initial values.
 *
 * ----------------------------------------------------------------------------
 */

void
extTechStyleInit(style)
    ExtStyle *style;
{
    TileType r, s;

    style->exts_name = NULL;
    style->exts_status = TECH_NOT_LOADED;

    style->exts_sidePlanes = style->exts_overlapPlanes = 0;
    TTMaskZero(&style->exts_transMask);
    style->exts_activeTypes = DBAllButSpaceAndDRCBits;

    for (r = 0; r < NP; r++)
    {
	TTMaskZero(&style->exts_sideTypes[r]);
	TTMaskZero(&style->exts_overlapTypes[r]);
	style->exts_planeOrder[r] = -1;
    }

    for (r = 0; r < NT; r++)
    {
	TTMaskZero(&style->exts_nodeConn[r]);
	TTMaskZero(&style->exts_resistConn[r]);
	TTMaskZero(&style->exts_transConn[r]);
	style->exts_allConn[r] = DBAllTypeBits;

	style->exts_sheetResist[r] = 0;
	style->exts_cornerChop[r] = 1.0;
	style->exts_viaResist[r] = 0;
	style->exts_height[r] = 0.0;
	style->exts_thick[r] = 0.0;
	style->exts_areaCap[r] = (CapValue) 0;
	style->exts_overlapOtherPlanes[r] = 0;
	TTMaskZero(&style->exts_overlapOtherTypes[r]);
	TTMaskZero(&style->exts_sideEdges[r]);
	for (s = 0; s < NT; s++)
	{
	    TTMaskZero(&style->exts_sideCoupleOtherEdges[r][s]);
	    TTMaskZero(&style->exts_sideOverlapOtherTypes[r][s]);
	    style->exts_sideOverlapOtherPlanes[r][s] = 0;
	    style->exts_sideCoupleCap[r][s] = (EdgeCap *) NULL;
	    style->exts_sideOverlapCap[r][s] = (EdgeCap *) NULL;
	    style->exts_perimCap[r][s] = (CapValue) 0;
	    style->exts_overlapCap[r][s] = (CapValue) 0;

	    TTMaskZero(&style->exts_overlapShieldTypes[r][s]);
	    style->exts_overlapShieldPlanes[r][s] = 0;
	    style->exts_sideOverlapShieldPlanes[r][s] = 0;
	}

	TTMaskZero(&style->exts_perimCapMask[r]);
#ifdef ARIEL
	TTMaskZero(&style->exts_subsTransistorTypes[r]);
#endif
	if (style->exts_transSDTypes[r] != NULL)
	    freeMagic(style->exts_transSDTypes[r]);
	style->exts_transSDTypes[r] = NULL;
	style->exts_transSDCount[r] = 0;
	style->exts_transGateCap[r] = (CapValue) 0;
	style->exts_transSDCap[r] = (CapValue) 0;
	if (style->exts_transSubstrateName[r] != (char *) NULL)
	{
	    freeMagic(style->exts_transSubstrateName[r]);
	    style->exts_transSubstrateName[r] = (char *) NULL;
	}
	if (style->exts_transName[r] != (char *) NULL)
	{
	    freeMagic(style->exts_transName[r]);
	    style->exts_transName[r] = (char *) NULL;
	}
	while (style->exts_deviceParams[r] != (ParamList *) NULL)
	{
	    /* Parameter lists are shared.  Only free the last one! */

	    if (style->exts_deviceParams[r]->pl_count > 1)
	    {
		style->exts_deviceParams[r]->pl_count--;
		style->exts_deviceParams[r] = (ParamList *)NULL;
	    }
	    else
	    {
		freeMagic(style->exts_deviceParams[r]->pl_name);
		freeMagic(style->exts_deviceParams[r]);
		style->exts_deviceParams[r] = style->exts_deviceParams[r]->pl_next;
	    }
	}
	style->exts_deviceClass[r] = (char)0;
	if (style->exts_transResist[r].ht_table != (HashEntry **) NULL)
	    HashKill(&style->exts_transResist[r]);
	HashInit(&style->exts_transResist[r], 8, HT_STRINGKEYS);
	style->exts_linearResist[r] = 0;
    }

    style->exts_sideCoupleHalo = 0;
    style->exts_stepSize = 100;
    style->exts_unitsPerLambda = 100.0;
    style->exts_resistScale = 1000;
    style->exts_capScale = 1000;
    style->exts_numResistClasses = 0;

    style->exts_planeOrderStatus = needPlaneOrder ;

    for (r = 0; r < DBNumTypes; r++)
    {
	style->exts_resistByResistClass[r] = 0;
	TTMaskZero(&style->exts_typesByResistClass[r]);
	style->exts_typesResistChanged[r] = DBAllButSpaceAndDRCBits;
	TTMaskSetType(&style->exts_typesResistChanged[r], TT_SPACE);
	style->exts_typeToResistClass[r] = -1;
    }
    doConvert = FALSE;

    // The exts_globSubstratePlane setting of -1 will be used to set a
    // backwards-compatibility mode matching previous behavior with
    // respect to the substrate when there is no "substrate" line in
    // the techfile.

    style->exts_globSubstratePlane = -1;
    TTMaskZero(&style->exts_globSubstrateTypes);
}


/*
 * ----------------------------------------------------------------------------
 *
 * extTechStyleNew --
 *
 * Allocate a new style with zeroed technology variables.
 *
 * Results:
 *	Allocates a new ExtStyle, initializes it, and returns it.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

ExtStyle *
extTechStyleNew()
{
    ExtStyle *style;

    style = extTechStyleAlloc();
    extTechStyleInit(style);
    return style;
}

/*
 * ----------------------------------------------------------------------------
 *
 * aToCap --
 *
 *    Utility procedure for reading capacitance values.
 *
 * Returns:
 *    A value of type CapValue.
 *
 * Side effects:
 *    none.
 * ----------------------------------------------------------------------------
 */

CapValue
aToCap(str)
    char *str;
{
    CapValue capVal;
    if (sscanf(str, "%lf", &capVal) != 1) {
	capVal = (CapValue) 0;
	TechError("Capacitance value %s must be a number\n", str);
    }
    return capVal;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtLoadStyle --
 *
 * Re-read the technology file to load the specified technology extraction
 * style into structure ExtCurStyle.  This is much more memory-efficient than
 * keeping a separate (and huge!) ExtStyle structure for each extraction style.
 * It incurs a complete reading of the tech file on startup and every time the 
 * extraction style is changed, but we can assume that this does not happen
 * often.  The first style in the technology file is assumed to be default, so
 * that re-reading the tech file is not necessary on startup unless the default
 * extraction style is changed by a call to "extract style".
 *
 * ----------------------------------------------------------------------------
 */
void
ExtLoadStyle(stylename)
   char *stylename;
{
    SectionID invext;

    extTechStyleInit(ExtCurStyle);	/* Reinitialize and mark as not	*/
    ExtCurStyle->exts_name = stylename; /* loaded.			*/

    /* Invalidate the extract section, and reload it. 			*/
    /* The second parameter to TechSectionGetMask is NULL because	*/
    /* no other tech client sections depend on the extract section.	*/

    invext = TechSectionGetMask("extract", NULL);
    TechLoad(NULL, invext);

    /* extTechFinalStyle(ExtCurStyle); */  /* Taken care of by TechLoad() */
    ExtTechScale(DBLambda[0], DBLambda[1]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechInit --
 *
 *	Ensure that all memory allocated to the extract database has
 *	been freed up.  Called before loading a new technology.
 *
 * ----------------------------------------------------------------------------
 */
void
ExtTechInit()
{
    ExtKeep *style;
    int r;

    /* Delete everything in ExtCurStyle */

    if (ExtCurStyle != NULL)
    {
	extTechStyleInit(ExtCurStyle);

	/* Everything has been freed except the hash tables, which */
	/* were just reinitialized by extTechStyleInit().	   */
        for (r = 0; r < NT; r++)
	{
	    if (ExtCurStyle->exts_transResist[r].ht_table != (HashEntry **) NULL)
		HashKill(&ExtCurStyle->exts_transResist[r]);
	}
	ExtCurStyle = NULL;
    }

    /* Forget all the extract style names */

    for (style = ExtAllStyles; style != NULL; style = style->exts_next)
    {
	freeMagic(style->exts_name);
	freeMagic(style);
    }
    ExtAllStyles = NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechSimpleAreaCap --
 *
 *	Parse the techfile line for the "defaultareacap" keyword.
 *	This is equivalent to the "areacap" line but also applies
 *	to "overlap" of types on the second plane (if specified) and
 *	all planes below it, with appropriate intervening types. 
 *
 * Usage:
 *	defaultareacap types plane [[subtypes] subplane] value
 *
 *	where "types" are the types for which to compute area cap,
 *	"plane" is the plane of "types", "subplane" is a plane
 *	containing wells or any types that have the same coupling
 *	as the substrate.  If absent, it is assumed that nothing
 *	shields "types" from the subtrate.  Additional optional
 *	"subtypes" is a list of types in "subplane" that shield.
 *	If absent, then all types in "subplane" are shields to the
 *	substrate.  "value" is the area capacitance in aF/um^2.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds information into the ExtCurStyle records.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTechSimpleAreaCap(argc, argv)
    int  argc;
    char *argv[];
{
    TileType s, t;
    TileTypeBitMask types, subtypes, shields;
    CapValue capVal;
    int plane1, plane2, plane3, pnum1, pnum2, pnum3;
    PlaneMask pshield;

    if (ExtCurStyle->exts_planeOrderStatus != seenPlaneOrder) 
    {
	TechError("Cannot parse area cap line without plane ordering!\n");
	return;
    }

    DBTechNoisyNameMask(argv[1], &types);
    plane1 = DBTechNoisyNamePlane(argv[2]);
    TTMaskAndMask(&types, &DBPlaneTypes[plane1]);

    capVal = aToCap(argv[argc - 1]);

    if (argc == 4)
	plane2 = -1;
    else
	plane2 = DBTechNoisyNamePlane(argv[argc - 2]);

    if (argc > 5)
	DBTechNoisyNameMask(argv[argc - 3], &subtypes);
    else
	subtypes = DBAllButSpaceAndDRCBits;

    /* Part 1: Area cap */
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	if (TTMaskHasType(&types, t))
	    ExtCurStyle->exts_areaCap[t] = capVal;

    if (plane2 == -1) return;		/* No "virtual" overlaps */
    else if (plane1 == plane2) return;  /* shouldn't happen */

    pnum1 = ExtCurStyle->exts_planeOrder[plane1];
    pnum2 = ExtCurStyle->exts_planeOrder[plane2];

    /* Part 2: Overlap cap on types equivalent to substrate */
    /* Find all types in or below plane2 (i.e., ~(space)/plane2)	   */
    /* Shield types are everything in the planes between plane1 and plane2 */

    TTMaskZero(&shields);

    pshield = 0;
    for (plane3 = PL_TECHDEPBASE; plane3 < DBNumPlanes; plane3++)
    {
	pnum3 = ExtCurStyle->exts_planeOrder[plane3];
	if (pnum3 > pnum2 && pnum3 < pnum1)
	{
	    TTMaskSetMask(&shields, &DBPlaneTypes[plane3]);
	    pshield |= PlaneNumToMaskBit(plane3);
	}
	else if (pnum3 <= pnum2)
	{
	    TTMaskAndMask(&subtypes, &DBPlaneTypes[plane3]);
	    TTMaskClearType(&subtypes, TT_SPACE);
	}
	TTMaskClearType(&shields, TT_SPACE);
    }

    /* Now record all of the overlap capacitances */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	if (TTMaskHasType(&types, s))
	{
	    /* Contact overlap caps are determined from residues */
    	    if (DBIsContact(s)) continue;

	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    {
		if (!TTMaskHasType(&subtypes, t)) continue;

		if (s == t) continue;	/* shouldn't happen */
		if (ExtCurStyle->exts_overlapCap[s][t] > (CapValue) 0)
		    continue;	/* redundant overlap */

		ExtCurStyle->exts_overlapCap[s][t] = capVal;
		ExtCurStyle->exts_overlapPlanes |= PlaneNumToMaskBit(plane1);
		ExtCurStyle->exts_overlapOtherPlanes[s] |= PlaneNumToMaskBit(plane2);
		TTMaskSetType(&ExtCurStyle->exts_overlapTypes[plane1], s);
		TTMaskSetType(&ExtCurStyle->exts_overlapOtherTypes[s], t);

		ExtCurStyle->exts_overlapShieldPlanes[s][t] = pshield;
		ExtCurStyle->exts_overlapShieldTypes[s][t] = shields;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechSimplePerimCap --
 *
 *	Parse the techfile line for the "defaultperimeter" keyword.
 *	This comprises both the "perimc" statement and the "sideoverlap"
 *	statement for overlaps to types that are effectively substrate
 *	(e.g., well, implant, marker layers, etc.)
 *
 * Usage:
 *	defaultperimeter types plane [[subtypes] subplane] value
 *
 *	where "types" are the types for which to compute fringing cap,
 *	"plane" is the plane of the types, "subplane" is an optional
 *	plane that shields "types" from substrate, and "value" is the
 *	fringing cap in aF/micron.  If "subplane" is omitted, then
 *	nothing shields "types" from the substrate.  Optional "subtypes"
 *	lists the types in "subplane" that shield.  Otherwise, it is
 *	assumed that all types in "subplane" shield "types" from the
 *	substrate.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds information into the ExtCurStyle records.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTechSimplePerimCap(argc, argv)
    int  argc;
    char *argv[];
{
    TileType r, s, t;
    TileTypeBitMask types, nottypes, subtypes, shields;
    CapValue capVal;
    int plane1, plane2, plane3, pnum1, pnum2, pnum3;
    PlaneMask pshield;
    EdgeCap *cnew;

    if (ExtCurStyle->exts_planeOrderStatus != seenPlaneOrder) 
    {
	TechError("Cannot parse area cap line without plane ordering!\n");
	return;
    }

    DBTechNoisyNameMask(argv[1], &types);
    // TTMaskCom2(&nottypes, &types);
    // For general use, only consider space and space-like types.
    // For device fringing fields, like poly to diffusion on a FET,
    // use perimc commands to augment the defaults.
    TTMaskZero(&nottypes);
    TTMaskSetType(&nottypes, TT_SPACE);
    plane1 = DBTechNoisyNamePlane(argv[2]);
    TTMaskAndMask(&types, &DBPlaneTypes[plane1]);
    TTMaskAndMask(&nottypes, &DBPlaneTypes[plane1]);

    capVal = aToCap(argv[argc - 1]);

    if (argc >= 4)
	plane2 = DBTechNoisyNamePlane(argv[argc - 2]);
    else
	plane2 = -1;

    if (argc > 5)
	DBTechNoisyNameMask(argv[argc - 3], &subtypes);
    else
	subtypes = DBAllButSpaceAndDRCBits;

    /* Part 1: Perimeter cap */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	for (t = 0; t < DBNumTypes; t++)
	    if (TTMaskHasType(&types, s) && TTMaskHasType(&nottypes, t))
	    {
		ExtCurStyle->exts_perimCap[s][t] = capVal;
		TTMaskSetType(&ExtCurStyle->exts_perimCapMask[s], t);
	    }

    if (plane2 == -1) return;		/* No "virtual" overlaps */
    else if (plane1 == plane2) return;  /* shouldn't happen */

    pnum1 = ExtCurStyle->exts_planeOrder[plane1];
    pnum2 = ExtCurStyle->exts_planeOrder[plane2];

    /* Part 2: Sidewall overlap cap on types equivalent to substrate	   */
    /* Find all types in or below plane2 (i.e., ~(space)/plane2)	   */
    /* Shield types are everything in the planes between plane1 and plane2 */

    TTMaskZero(&shields);

    pshield = 0;
    for (plane3 = PL_TECHDEPBASE; plane3 < DBNumPlanes; plane3++)
    {
	pnum3 = ExtCurStyle->exts_planeOrder[plane3];
	if (pnum3 > pnum2 && pnum3 < pnum1)
	{
	    TTMaskSetMask(&shields, &DBPlaneTypes[plane3]);
	    pshield |= PlaneNumToMaskBit(plane3);
	}
	else if (pnum3 <= pnum2)
	{
	    TTMaskAndMask(&subtypes, &DBPlaneTypes[plane3]);
	}
    }
    TTMaskClearType(&shields, TT_SPACE);
    TTMaskClearType(&subtypes, TT_SPACE);

    /* Record all of the sideoverlap capacitances */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	/* Side overlap computed from residues */
    	if (DBIsContact(s)) continue;

	if (TTMaskHasType(&types, s))	// Corrected, 2/21/2017
	{
	    ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(plane1);
	    TTMaskSetType(&ExtCurStyle->exts_sideTypes[plane1], s);
	    TTMaskSetMask(&ExtCurStyle->exts_sideEdges[s], &nottypes);
	    for (t = 0; t < DBNumTypes; t++)
	    {
		if (!TTMaskHasType(&nottypes, t)) continue;
  		if (DBIsContact(t)) continue;

		TTMaskSetMask(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t], &subtypes);
		ExtCurStyle->exts_sideOverlapOtherPlanes[s][t] |=
				PlaneNumToMaskBit(plane2);
		cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		cnew->ec_cap = capVal;
		cnew->ec_far = shields;		/* Types that shield */
		cnew->ec_near = subtypes;	/* Types we create cap with */
		cnew->ec_pmask = PlaneNumToMaskBit(plane2);
		cnew->ec_next = ExtCurStyle->exts_sideOverlapCap[s][t];
		ExtCurStyle->exts_sideOverlapCap[s][t] = cnew;

		for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
		    if (TTMaskHasType(&subtypes, r))
			ExtCurStyle->exts_sideOverlapShieldPlanes[s][r] |= pshield;
	    }
	}

	/* Reverse case (swap "types" and "subtypes") */

	if (TTMaskHasType(&subtypes, s))
	{
	    ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(plane2);
	    TTMaskSetType(&ExtCurStyle->exts_sideTypes[plane2], s);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    {
  		if (DBIsContact(t)) continue;

		TTMaskSetMask(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t], &types);
		ExtCurStyle->exts_sideOverlapOtherPlanes[s][t] |=
				PlaneNumToMaskBit(plane1);
		cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		cnew->ec_cap = capVal;
		cnew->ec_far = shields;		/* Types that shield */
		cnew->ec_near = types;		/* Types we create cap with */
		cnew->ec_pmask = PlaneNumToMaskBit(plane1);
		cnew->ec_next = ExtCurStyle->exts_sideOverlapCap[s][t];
		ExtCurStyle->exts_sideOverlapCap[s][t] = cnew;

		for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
		    if (TTMaskHasType(&types, r))
			ExtCurStyle->exts_sideOverlapShieldPlanes[s][r] |= pshield;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechSimpleSidewallCap --
 *
 *	Parse the techfile line for the "defaultsidewall" keyword.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds information into the ExtCurStyle records.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTechSimpleSidewallCap(argv)
    char *argv[];
{
    /* Like ExtTechLine, but with near = types2 and far = types1 */

    TileType s, t;
    TileTypeBitMask types1, types2;
    CapValue capVal;
    EdgeCap *cnew;
    int plane;

    DBTechNoisyNameMask(argv[1], &types1);
    plane = DBTechNoisyNamePlane(argv[2]);
    capVal = aToCap(argv[3]);

    // Like perimeter cap, treat only space and space-like types
    // TTMaskCom2(&types2, &types1);
    TTMaskZero(&types2);
    TTMaskSetType(&types2, TT_SPACE);

    TTMaskAndMask(&types1, &DBPlaneTypes[plane]);
    TTMaskAndMask(&types2, &DBPlaneTypes[plane]);

    if (TTMaskHasType(&types1, TT_SPACE))
	TechError("Can't have space on inside of edge [ignored]\n");

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	if (TTMaskHasType(&types1, s))
	{
	    ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(DBPlane(s));
	    TTMaskSetType(&ExtCurStyle->exts_sideTypes[DBPlane(s)], s);
	    TTMaskSetMask(&ExtCurStyle->exts_sideEdges[s], &types2);
	    for (t = 0; t < DBNumTypes; t++)
	    {
		if (!TTMaskHasType(&types2, t))
		    continue;
		TTMaskSetMask(&ExtCurStyle->exts_sideCoupleOtherEdges[s][t], &types1);
		cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		cnew->ec_cap = capVal;
		cnew->ec_near = types2;
		cnew->ec_far = types1;
		cnew->ec_next = ExtCurStyle->exts_sideCoupleCap[s][t];
		cnew->ec_pmask = 0;
		ExtCurStyle->exts_sideCoupleCap[s][t] = cnew;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechSimpleOverlapCap --
 *
 *	Parse the techfile line for the "defaultoverlap" keyword.
 *	This is the same as the "overlap" statement excet that shield
 *	types are determined automatically from the planeorder.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds information into the ExtCurStyle records.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTechSimpleOverlapCap(argv)
    char *argv[];
{
    TileType s, t;
    TileTypeBitMask types1, types2, shields;
    CapValue capVal;
    int plane1, plane2, plane3, pnum1, pnum2, pnum3;
    PlaneMask pshield;

    if (ExtCurStyle->exts_planeOrderStatus != seenPlaneOrder) 
    {
	TechError("Cannot parse area cap line without plane ordering!\n");
	return;
    }

    DBTechNoisyNameMask(argv[1], &types1);
    plane1 = DBTechNoisyNamePlane(argv[2]);
    TTMaskAndMask(&types1, &DBPlaneTypes[plane1]);

    DBTechNoisyNameMask(argv[3], &types2);
    plane2 = DBTechNoisyNamePlane(argv[4]);
    TTMaskAndMask(&types2, &DBPlaneTypes[plane2]);

    capVal = aToCap(argv[5]);

    pnum1 = ExtCurStyle->exts_planeOrder[plane1];
    pnum2 = ExtCurStyle->exts_planeOrder[plane2];

    /* Find all types in or below plane2 (i.e., ~(space)/plane2)	   */
    /* Shield types are everything in the planes between plane1 and plane2 */

    TTMaskZero(&shields);

    pshield = 0;
    for (plane3 = PL_TECHDEPBASE; plane3 < DBNumPlanes; plane3++)
    {
	pnum3 = ExtCurStyle->exts_planeOrder[plane3];
	if (pnum3 > pnum2 && pnum3 < pnum1)
	{
	    TTMaskSetMask(&shields, &DBPlaneTypes[plane3]);
	    pshield |= PlaneNumToMaskBit(plane3);
	}
    }
    TTMaskClearType(&shields, TT_SPACE);

    /* Now record all of the overlap capacitances */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	if (TTMaskHasType(&types1, s))
	{
	    /* Contact overlap caps are determined from residues */
  	    if (DBIsContact(s)) continue;

	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    {
		if (!TTMaskHasType(&types2, t)) continue;
  		if (DBIsContact(t)) continue;

		if (s == t) continue;	/* shouldn't happen */
		if (plane1 == plane2) continue;  /* shouldn't happen */
		if (ExtCurStyle->exts_overlapCap[s][t] > (CapValue) 0)
		    continue;	/* redundant overlap */

		ExtCurStyle->exts_overlapCap[s][t] = capVal;
		ExtCurStyle->exts_overlapPlanes |= PlaneNumToMaskBit(plane1);
		ExtCurStyle->exts_overlapOtherPlanes[s] |= PlaneNumToMaskBit(plane2);
		TTMaskSetType(&ExtCurStyle->exts_overlapTypes[plane1], s);
		TTMaskSetType(&ExtCurStyle->exts_overlapOtherTypes[s], t);

		ExtCurStyle->exts_overlapShieldPlanes[s][t] = pshield;
		ExtCurStyle->exts_overlapShieldTypes[s][t] = shields;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechSimpleSideOverlapCap --
 *
 *	Parse the techfile line for the "defaultsideoverlap" keyword.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds information into the ExtCurStyle records.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTechSimpleSideOverlapCap(argv)
    char *argv[];
{
    TileType r, s, t;
    TileTypeBitMask types, nottypes, ov, notov, shields;
    CapValue capVal;
    int plane1, plane2, plane3, pnum1, pnum2, pnum3;
    PlaneMask pshield;
    EdgeCap *cnew;

    if (ExtCurStyle->exts_planeOrderStatus != seenPlaneOrder) 
    {
	TechError("Cannot parse area cap line without plane ordering!\n");
	return;
    }

    DBTechNoisyNameMask(argv[1], &types);
    plane1 = DBTechNoisyNamePlane(argv[2]);

    // TTMaskCom2(&nottypes, &types);
    TTMaskZero(&nottypes);
    TTMaskSetType(&nottypes, TT_SPACE);

    TTMaskAndMask(&types,    &DBPlaneTypes[plane1]);
    TTMaskAndMask(&nottypes, &DBPlaneTypes[plane1]);

    DBTechNoisyNameMask(argv[3], &ov);
    plane2 = DBTechNoisyNamePlane(argv[4]);

    // TTMaskCom2(&notov, &ov);
    TTMaskZero(&notov);
    TTMaskSetType(&notov, TT_SPACE);

    TTMaskAndMask(&ov,    &DBPlaneTypes[plane2]);
    TTMaskAndMask(&notov, &DBPlaneTypes[plane2]);

    capVal = aToCap(argv[5]);

    pnum1 = ExtCurStyle->exts_planeOrder[plane1];
    pnum2 = ExtCurStyle->exts_planeOrder[plane2];

    /* Find all types in or below plane2 (i.e., ~(space)/plane2)	   */
    /* Shield planes are the ones between plane1 and plane2 */

    TTMaskZero(&shields);
    pshield = 0;
    for (plane3 = PL_TECHDEPBASE; plane3 < DBNumPlanes; plane3++)
    {
	pnum3 = ExtCurStyle->exts_planeOrder[plane3];
	if (pnum3 > pnum2 && pnum3 < pnum1)
	{
	    TTMaskSetMask(&shields, &DBPlaneTypes[plane3]);
	    pshield |= PlaneNumToMaskBit(plane3);
	}
    }
    TTMaskClearType(&shields, TT_SPACE);

    /* Now record all of the sideoverlap capacitances */

    if (TTMaskHasType(&types, TT_SPACE) || TTMaskHasType(&ov, TT_SPACE))
    {
	TechError("Overlap types can't contain space [ignored]\n");
	return;
    }

    /* Record all of the sideoverlap capacitances */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	/* Side overlap computed from residues */
  	if (DBIsContact(s)) continue;

	if (TTMaskHasType(&types, s))
	{
	    ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(plane1);
	    TTMaskSetType(&ExtCurStyle->exts_sideTypes[plane1], s);
	    TTMaskSetMask(&ExtCurStyle->exts_sideEdges[s], &nottypes);
	    for (t = TT_SPACE; t < DBNumTypes; t++)
	    {
		if (!TTMaskHasType(&nottypes, t)) continue;
		if (DBIsContact(t)) continue;

		TTMaskSetMask(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t], &ov);
		ExtCurStyle->exts_sideOverlapOtherPlanes[s][t] |=
				PlaneNumToMaskBit(plane2);
		cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		cnew->ec_cap = capVal;
		cnew->ec_far = shields;		/* Types that shield */
		cnew->ec_near = ov;		/* Types we create cap with */
		cnew->ec_pmask = PlaneNumToMaskBit(plane2);
		cnew->ec_next = ExtCurStyle->exts_sideOverlapCap[s][t];
		ExtCurStyle->exts_sideOverlapCap[s][t] = cnew;

		for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
		    if (TTMaskHasType(&ov, r))
			ExtCurStyle->exts_sideOverlapShieldPlanes[s][r] |= pshield;
	    }
	}

	/* Reverse case (swap "types" and "ov") */

#if 0
	if (TTMaskHasType(&ov, s))
	{
	    ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(plane2);
	    TTMaskSetType(&ExtCurStyle->exts_sideTypes[plane2], s);
	    TTMaskSetMask(&ExtCurStyle->exts_sideEdges[s], &notov);
	    for (t = TT_SPACE; t < DBNumTypes; t++)
	    {
		if (!TTMaskHasType(&notov, t)) continue;
  		if (DBIsContact(t)) continue;

		TTMaskSetMask(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t], &types);
		ExtCurStyle->exts_sideOverlapOtherPlanes[s][t] |=
				PlaneNumToMaskBit(plane1);
		cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		cnew->ec_cap = capVal;
		cnew->ec_far = shields;		/* Types that shield */
		cnew->ec_near = types;		/* Types we create cap with */
		cnew->ec_pmask = PlaneNumToMaskBit(plane1);
		cnew->ec_next = ExtCurStyle->exts_sideOverlapCap[s][t];
		ExtCurStyle->exts_sideOverlapCap[s][t] = cnew;

		for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
		    if (TTMaskHasType(&types, r))
			ExtCurStyle->exts_sideOverlapShieldPlanes[s][r] |= pshield;
	    }
	}
#endif /* 0 */
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechLine --
 *
 * Process a line from the "extract" section of a technology file.
 *
 * Each line in the extract section of a technology begins
 * with a keyword that identifies the format of the rest of
 * the line.
 *
 * The following three kinds of lines are used to define the resistance
 * and parasitic capacitance to substrate of each tile type:
 *
 *	resist	 types resistance
 *	areacap	 types capacitance
 *	perimcap inside outside capacitance
 *
 * where 'types', 'inside', and 'outside' are comma-separated lists
 * of tile types, 'resistance' is an integer giving the resistance
 * per square in milli-ohms, and 'capacitance' is an integer giving
 * capacitance (per square lambda for areacap, or per lambda perimeter
 * for perimcap) in attofarads.
 *
 * The perimeter (sidewall) capacitance depends both on the types
 * inside and outside the perimeter.  For a given 'perimcap' line,
 * any segment of perimeter with a type in 'inside' inside the
 * perimeter and a type in 'outside' ontside the perimeter will
 * have the indicated capacitance.
 *
 * Both area and perimeter capacitance computed from the information
 * above apply between a given node and the substrate beneath it, as
 * determined by extSubstrate[].
 *
 * Contact resistances are specified by:
 *
 *	contact	type	minsize	resistance
 *
 * where type is the type of contact tile, minsize is chosen so that contacts
 * that are integer multiples of minsize get an additional contact cut for each
 * increment of minsize, and resistance is in milliohms.
 *
 * Overlap coupling capacitance is specified by:
 *
 *	overlap	 toptypes bottomtypes capacitance [shieldtypes]
 *
 * where 'toptypes' and 'bottomtypes' are comma-separated lists of tile types,
 * and 'capacitance' is an integer giving capacitance in attofarads per
 * square lambda of overlap.  The sets 'toptypes' and 'bottomtypes' should
 * be disjoint.  Also, the union of the planes of 'toptypes' should be disjoint
 * from the union of the planes of 'bottomtypes'.  If 'shieldtypes' are
 * present, they should also be a comma-separated list of types, on
 * planes disjoint from those of either 'toptypes' or 'bottomtypes'.
 *
 * Whenever a tile of a type in 'toptypes' overlaps one of a type in
 * 'bottomtypes', we deduct the capacitance to substrate of the 'toptypes'
 * tile for the area of the overlap, and create an overlap capacitance
 * between the two nodes based on 'capacitance'.  When material in
 * 'shieldtypes' appears over any of this overlap area, however, we
 * only deduct the substrate capacitance; we don't create an overlap
 * capacitor.
 *
 * Sidewall coupling capacitance is specified by:
 *
 *	sidewall  intypes outtypes neartypes fartypes capacitance
 *
 * where 'intypes', 'outtypes', 'neartypes', and 'fartypes' are all comma-
 * separated lists of types, and 'capacitance' is an integer giving capacitance
 * in attofarads.  All of the tiles in all four lists should be on the same
 * plane.
 *
 * Whenever an edge of the form i|j is seen, where 'i' is in intypes and
 * 'j' is in outtypes, we search on the 'j' side for a distance of
 * ExtCurStyle->exts_sideCoupleHalo for edges with 'neartypes' on the
 * close side and 'fartypes' on the far side.  We create a capacitance
 * equal to the length of overlap, times capacitance, divided by the
 * separation between the edges (poor approximation, but better than
 * none).
 *
 * Sidewall overlap coupling capacitance is specified by:
 *
 *	sideoverlap  intypes outtypes ovtypes capacitance
 *
 * where 'intypes', 'outtypes', and 'ovtypes' are comma-separated lists
 * of types, and 'capacitance' is an integer giving capacitance in attofarads
 * per lambda.  Both intypes and outtypes should be in the same plane, and
 * ovtypes should be in a different plane from intypes and outtypes.
 *
 * The next kind of line describes transistors:
 *
 *	fet	 types terminals min-#terminals names substrate gscap gccap
 *
 * where 'types' and 'terminals' are comma-separated lists of tile types.
 * The meaning is that each type listed in 'types' is a transistor, whose
 * source and drain connect to any of the types listed in 'terminals'.
 * These transistors must have exactly min-#terminals terminals, in addition
 * to the gate (whose connectivity is specified in the system-wide connectivity
 * table in the "connect" section of the .tech file).  Currently gscap and
 * gccap are unused, but refer to the gate-source (or gate-drain) capacitance
 * and the gate-channel capacitance in units of attofarads per lambda and
 * attofarads per square lambda respectively.
 *
 * The resistances of transistors is specified by:
 *
 *	fetresist type region ohms
 *
 * where type is a type of tile that is a fet, region is a string ("linear"
 * is treated specially), and ohms is the resistance per square of the fet
 * type while operating in "region".  The values of fets in the "linear"
 * region are stored in a separate table.
 *
 * Results:
 *	Returns TRUE normally, or FALSE if the line from the
 *	technology file is so malformed that Magic should abort.
 *	Currently, we always return TRUE.
 *
 * Side effects:
 *	Initializes the per-technology variables that appear at the
 *	beginning of this file.
 *
 * ----------------------------------------------------------------------------
 */

#define MAXSD 6

    /*ARGSUSED*/
bool
ExtTechLine(sectionName, argc, argv)
    char *sectionName;
    int argc;
    char *argv[];
{
    int n, l, i, j, size, val, p1, p2, p3, nterm, iterm, class;
    PlaneMask pshield, pov;
    CapValue capVal, gscap, gccap;
    TileTypeBitMask types1, types2, termtypes[MAXSD];
    TileTypeBitMask near, far, ov, shield, subsTypes;
    char *subsName, *transName, *cp, *endptr, *paramName;
    TileType s, t, r, o;
    keydesc *kp, *dv;
    bool isLinear;
    HashEntry *he;
    EdgeCap *cnew;
    ExtKeep *es, *newStyle;
    ParamList *subcktParams, *newParam;
    int refcnt;
    double dhalo;
    bool bad;

    if (argc < 1)
    {
	TechError("Each line must begin with a keyword\n");
	return (TRUE);
    }

    n = LookupStruct(argv[0], (LookupTable *) keyTable, sizeof keyTable[0]);
    if (n < 0)
    {
	TechError("Illegal keyword.  Legal keywords are:\n\t");
	for (n = 0; keyTable[n].k_name; n++)
	    TxError(" %s", keyTable[n].k_name);
	TxError("\n");
	return (TRUE);
    }

    kp = &keyTable[n];
    if (argc < kp->k_minargs)
	goto usage;

    /* Handle maxargs for DEVICE type separately */
    if ((argc > kp->k_maxargs) && (kp->k_key != DEVICE))
	goto usage;

    else if (argc >= 2) l = strlen(argv[1]);

    /* If ExtCurStyle is NULL, this is a first pass, and we should	*/
    /* immediately load this style as default.  Otherwise, check if	*/
    /* the style name is in the table of styles, and add it if it is	*/
    /* not.								*/

    if (kp->k_key == STYLE)
    {
	if (argc != 2)
	    if ((argc != 4) || (strncmp(argv[2], "variant", 7)))
		goto usage;

	for (newStyle = ExtAllStyles; newStyle != NULL;
		newStyle = newStyle->exts_next)
	{
	    if (!strncmp(newStyle->exts_name, argv[1], l))
		break;
	}
	if (newStyle == NULL)
	{
	    if (argc == 2)
	    {
		newStyle = (ExtKeep *)mallocMagic(sizeof(ExtKeep));
		newStyle->exts_next = NULL;
		newStyle->exts_name = StrDup((char **) NULL, argv[1]);

		if (ExtAllStyles == NULL)
		    ExtAllStyles = newStyle;
		else
		{
	            /* Append to end of style list */
	            for (es = ExtAllStyles; es->exts_next; es = es->exts_next);
	            es->exts_next = newStyle;
		}
	    }
	    else	/* Handle style variants */
	    {
		ExtKeep *saveStyle = NULL;
		char *tptr, *cptr;

		/* 4th argument is a comma-separated list of variants.	*/
		/* In addition to the default name recorded above,	*/
		/* record each of the variants.				*/

		tptr = argv[3];
		while (*tptr != '\0')
		{
		    cptr = strchr(tptr, ',');
		    if (cptr != NULL) *cptr = '\0';
		    newStyle = (ExtKeep *)mallocMagic(sizeof(ExtKeep));
		    newStyle->exts_next = NULL;
		    newStyle->exts_name = (char *)mallocMagic(l
				+ strlen(tptr) + 1);
		    sprintf(newStyle->exts_name, "%s%s", argv[1], tptr);

		    /* Remember the first variant as the default */
		    if (saveStyle == NULL) saveStyle= newStyle;

		    /* Append to end of style list */
		    if (ExtAllStyles == NULL)
			ExtAllStyles = newStyle;
		    else
		    {
			for (es = ExtAllStyles; es->exts_next; es = es->exts_next);
			es->exts_next = newStyle;
		    }
		    
		    if (cptr == NULL)
			break;
		    else
			tptr = cptr + 1;
		}
		newStyle = saveStyle;
	    }
	}

	/* Load style as default extraction style if this is the first	*/
	/* style encountered.  Otherwise, if we are changing styles, 	*/
	/* load this style only if the name matches that in ExtCurStyle.*/

	if (ExtCurStyle == NULL) 
	{
	    ExtCurStyle = extTechStyleNew();
	    ExtCurStyle->exts_name = newStyle->exts_name;
	    ExtCurStyle->exts_status = TECH_PENDING;
	}
	else if ((ExtCurStyle->exts_status == TECH_PENDING) ||
		(ExtCurStyle->exts_status == TECH_SUSPENDED))
	    /* Finished loading; stop */
	    ExtCurStyle->exts_status = TECH_LOADED;
	else if (ExtCurStyle->exts_status == TECH_NOT_LOADED)
	{
	    if (ExtCurStyle->exts_name == NULL)
	        return (FALSE);		/* Don't know what to load! */
	    else if (argc == 2)
	    {
		if (!strcmp(argv[1], ExtCurStyle->exts_name))
		     ExtCurStyle->exts_status = TECH_PENDING; 	/* load pending */
	    }
	    else if (argc == 4)
	    {
		/* Verify that the style matches one variant */
		char *tptr, *cptr;
		if (!strncmp(ExtCurStyle->exts_name, argv[1], l))
		{
		    tptr = argv[3];
		    while (*tptr != '\0')
		    {
			cptr = strchr(tptr, ',');
			if (cptr != NULL) *cptr = '\0';
			if (!strcmp(ExtCurStyle->exts_name + l, tptr))
			{
			    ExtCurStyle->exts_status = TECH_PENDING;
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

    /* Only continue past this point if we are loading the extraction style */
    if (ExtCurStyle == NULL) return FALSE;
    if ((ExtCurStyle->exts_status != TECH_PENDING) &&
		(ExtCurStyle->exts_status != TECH_SUSPENDED))
	return TRUE;

    /* Process "variant" lines next */

    if (kp->k_key == VARIANT)
    {
	int l;
	char *cptr, *tptr;

	/* If our style variant is not one of the ones declared */
	/* on the line, then we ignore all input until we 	*/
	/* either reach the end of the style, the end of the	*/
	/* section, or another "variant" line.			*/

	if (argc != 2) goto usage;
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

	    if (*tptr == '*')	/* Wildcard for "all variants" */
	    {
		ExtCurStyle->exts_status = TECH_PENDING;
		return TRUE;
	    }
	    else
	    {
		l = strlen(ExtCurStyle->exts_name) - strlen(tptr);
		if (!strcmp(tptr, ExtCurStyle->exts_name + l))
		{
		    ExtCurStyle->exts_status = TECH_PENDING;
		    return TRUE;
		}
	    }
	
	    if (cptr == NULL)
		break;
	    else
		tptr = cptr + 1;
	}
	ExtCurStyle->exts_status = TECH_SUSPENDED;
    }

    /* Anything below this line is not parsed if we're in TECH_SUSPENDED mode */
    if (ExtCurStyle->exts_status != TECH_PENDING) return TRUE;

    switch (kp->k_key)
    {
	case AREAC:
	case CONTACT:
	case FET:
	case FETRESIST:
	case HEIGHT:
	case OVERC:
	case PERIMC:
	case RESIST:
	case SIDEWALL:
	case SIDEOVERLAP:
	case SUBSTRATE:
	    DBTechNoisyNameMask(argv[1], &types1);
	    break;
	case DEVICE:
	    DBTechNoisyNameMask(argv[3], &types1);
	    break;
	case PLANEORDER:
	case NOPLANEORDER:
	default:
	    break;
    }

    switch (kp->k_key)
    {
	case AREAC:
	    capVal = aToCap(argv[2]);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		if (TTMaskHasType(&types1, t))
		    ExtCurStyle->exts_areaCap[t] = capVal;
	    break;
	case CONTACT:
	    /* Contact size, border, spacing deprecated (now taken from	*/
	    /* cifoutput "squares" generation parameters).		*/
	    if (argc != 3)
	    {
		if (argc == 4)
		    TxPrintf("Contact size value ignored "
				"(using GDS generation rules).\n");
		else
		    TxPrintf("Contact size, spacing, and border values ignored "
				"(using GDS generation rules).\n");
	    }

	    if (!StrIsInt(argv[argc - 1]))
	    {
		TechError("Contact resistivity %s must be an integer value "
			"(in milliohms/square).\n", argv[argc - 1]);
		break;
	    }
	    val = atoi(argv[argc - 1]);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		if (TTMaskHasType(&types1, t))
		    ExtCurStyle->exts_viaResist[t] = val;
	    break;
	case CSCALE:
	    ExtCurStyle->exts_capScale = strtol(argv[1], &endptr, 10);
	    if (endptr == argv[1])
	    {
		TechError("Cannot parse cap scale value \"%s\"\n", argv[1]);
		ExtCurStyle->exts_capScale = 1;
	    }
	    break;
	case FET:

	    /* Original FET format, kept for backwards compatibility */

	    DBTechNoisyNameMask(argv[2], &termtypes[0]);
	    nterm = atoi(argv[3]);
	    transName = argv[4];
	    subsName = argv[5];

	    // From magic version 8.1, subs name can be a nonfunctional
	    // throwaway (e.g., "error"), so don't throw a warning. 
	
	    cp = strchr(subsName, '!');
	    if (cp == NULL || cp[1] != '\0')
	    {
		if (strcasecmp(subsName, "error"))
		{
		    TechError("Fet substrate node %s is not a global name\n",
				subsName);
		}
	    }

	    subsTypes = DBZeroTypeBits;
	    if (sscanf(argv[6], "%lf", &capVal) != 1)
	    {
		DBTechNoisyNameMask(argv[6], &subsTypes);
		gscap = aToCap(argv[7]);
		gccap = (argc > 8) ? aToCap(argv[8]) : (CapValue) 0;
	    }
	    else
	    {
		gscap = aToCap(argv[6]);
		gccap = (argc > 7) ? aToCap(argv[7]) : (CapValue) 0;
	    }

	    TTMaskSetMask(&ExtCurStyle->exts_transMask, &types1);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		if (TTMaskHasType(&types1, t))
		{
		    TTMaskSetMask(ExtCurStyle->exts_transConn+t,&types1);
		    ExtCurStyle->exts_transSDTypes[t] = (TileTypeBitMask *)
				mallocMagic(2 * sizeof(TileTypeBitMask));
		    ExtCurStyle->exts_transSDTypes[t][0] = termtypes[0];
		    ExtCurStyle->exts_transSDTypes[t][1] = DBZeroTypeBits;
		    ExtCurStyle->exts_transSDCount[t] = nterm;
		    ExtCurStyle->exts_transSDCap[t] = gscap;
		    ExtCurStyle->exts_transGateCap[t] = gccap;
		    ExtCurStyle->exts_deviceClass[t] = DEV_FET;
		    ExtCurStyle->exts_transName[t] =
			    StrDup((char **) NULL, transName);
		    ExtCurStyle->exts_transSubstrateName[t] =
			    StrDup((char **) NULL, subsName);
		    ExtCurStyle->exts_transSubstrateTypes[t] = subsTypes;
#ifdef ARIEL
		    {
			int z;

			for (z = TT_TECHDEPBASE; z < DBNumTypes; z++)
			{
			    if (TTMaskHasType(&subsTypes, z))
				TTMaskSetType(&ExtCurStyle->exts_subsTransistorTypes[z],
					t);
			}
		    }
#endif
		}
	    break;

	case DEFAULTAREACAP:
	    ExtTechSimpleAreaCap(argc, argv);
	    break;
	case DEFAULTOVERLAP:
	    ExtTechSimpleOverlapCap(argv);
	    break;
	case DEFAULTPERIMETER:
	    ExtTechSimplePerimCap(argc, argv);
	    break;
	case DEFAULTSIDEOVERLAP:
	    ExtTechSimpleSideOverlapCap(argv);
	    break;
	case DEFAULTSIDEWALL:
	    ExtTechSimpleSidewallCap(argv);
	    break;

	case DEVICE:

	    /* Parse second argument for device type */

	    n = LookupStruct(argv[1], (LookupTable *) devTable, sizeof devTable[0]);
	    if (n < 0)
	    {
		TechError("Illegal device.  Legal devices are:\n\t");
		for (n = 0; devTable[n].k_name; n++)
		    TxError(" %s", devTable[n].k_name);
		TxError("\n");
		return (TRUE);
	    }

	    dv = &devTable[n];
	    if ((argc - 1) < dv->k_minargs)
		goto usage;

	    /* Parse parameters from the end of the argument list.	*/
	    /* Parameters may be provided for any device.		*/

	    /* Check final arguments for "x=y" statements showing what	*/
	    /* parameter names the device uses.				*/

	    subcktParams = NULL;
	    while ((paramName = strchr(argv[argc - 1], '=')) != NULL)
	    {
		char *mult;

		paramName++;
		newParam = (ParamList *)mallocMagic(sizeof(ParamList));
		newParam->pl_count = 0;
		newParam->pl_param[0] = *argv[argc - 1];
		newParam->pl_param[1] = '\0';

		if (paramName - argv[argc - 1] == 3)
		    newParam->pl_param[1] = *(argv[argc - 1] + 1);

		else if (paramName - argv[argc - 1] > 3)
		    TechError("Parameter name %s can be no more than"
			"two characters.\n", argv[argc - 1]);

		// Parameter syntax "<type>=<name>*<scale>" indicates
		// that the subcircuit has internal scaling, and the
		// extractor should multiply the parameter by this value
		// before passing it to the subcircuit.

		if ((mult = strchr(paramName, '*')) != NULL)
		{
		    *mult = '\0';
		    mult++;
		    newParam->pl_scale = atof(mult);
		}
		else
		    newParam->pl_scale = 1.0;

		newParam->pl_name = StrDup((char **)NULL, paramName);
		newParam->pl_next = subcktParams;
		subcktParams = newParam;
		argc--;
	    }

	    /* Check the number of arguments after splitting out	*/
	    /* parameter entries.  There is no limit on arguments in	*/
	    /* DEV_SUBCKT and DEV_MSUBCKT.				*/

	    class = dv->k_key;
	    switch (class)
	    {
		case DEV_SUBCKT:
		case DEV_MSUBCKT:
		    break;
		default:
		    /* If parameters were saved but the	*/
		    /* argument list indicates a bad	*/
		    /* device entry, then free up the	*/
		    /* parameters.			*/

		    if ((argc - 1) > dv->k_maxargs)
		    {
			while (subcktParams != NULL)
			{
			    freeMagic(subcktParams->pl_name);
			    freeMagic(subcktParams);
			    subcktParams = subcktParams->pl_next;
			}
			goto usage;
		    }
		    break;
	    }

	    gscap = (CapValue) 0;
	    gccap = (CapValue) 0;
	    subsName = NULL;
	    subsTypes = DBZeroTypeBits;
	    transName = argv[2];

	    switch (dv->k_key)
	    {
		case DEV_BJT:
		    DBTechNoisyNameMask(argv[4], &termtypes[0]); /* emitter */
		    termtypes[1] = DBZeroTypeBits;
		    DBTechNoisyNameMask(argv[5], &subsTypes);	 /* collector */
		    nterm = 1;	/* emitter is the only "terminal type" expected */
		    break;
		case DEV_MOSFET:
		    if ((argc > 7) && (!StrIsNumeric(argv[7])))
		    {
			/* Asymmetric device with different source and drain types */

			DBTechNoisyNameMask(argv[4], &termtypes[0]); /* source */
			DBTechNoisyNameMask(argv[5], &termtypes[1]); /* drain */
			TTMaskAndMask3(&termtypes[2], &termtypes[0], &termtypes[1]);

			if (TTMaskEqual(&termtypes[0], &termtypes[1]))
			    termtypes[1] = DBZeroTypeBits;	/* Make it symmetric */
			else if (!TTMaskIsZero(&termtypes[2]))
			{
			    TechError("Device mosfet %s has overlapping drain"
				" and source types!\n", transName);
			    /* Should this device be disabled? */
			}
			termtypes[2] = DBZeroTypeBits;
			if (strcmp(argv[6], "None"))
		    	DBTechNoisyNameMask(argv[6], &subsTypes);   /* substrate */
			subsName = argv[7];
			if (argc > 8) gscap = aToCap(argv[8]);
			if (argc > 9) gccap = aToCap(argv[9]);
			nterm = 2;
			class = DEV_ASYMMETRIC;
		    }
		    else
		    {
			/* Normal symmetric device with swappable source/drain */
			
			DBTechNoisyNameMask(argv[4], &termtypes[0]); /* source/drain */
			termtypes[1] = DBZeroTypeBits;
			if (strcmp(argv[5], "None"))
			    DBTechNoisyNameMask(argv[5], &subsTypes);   /* substrate */
			if (argc > 6) subsName = argv[6];
			if (argc > 7) gscap = aToCap(argv[7]);
			if (argc > 8) gccap = aToCap(argv[8]);
			/* nterm = 1; */	/* Symmetric devices can be MOScaps */
			nterm = 2;
		    }
		    break;

		case DEV_DIODE:
		case DEV_PDIODE:
		case DEV_NDIODE:
		    DBTechNoisyNameMask(argv[4], &termtypes[0]); /* negative types */
		    termtypes[1] = DBZeroTypeBits;
		    nterm = 1;
		    if ((argc > 4) && strcmp(argv[4], "None"))
			DBTechNoisyNameMask(argv[4], &subsTypes);   /* substrate */
		    else
			subsTypes = DBZeroTypeBits;
		    if (argc > 5) subsName = argv[5];
		    break;

		case DEV_RES:
		    DBTechNoisyNameMask(argv[4], &termtypes[0]); /* terminals */
		    termtypes[1] = DBZeroTypeBits;
		    nterm = 2;
		    if ((argc > 5) && strcmp(argv[5], "None"))
			DBTechNoisyNameMask(argv[5], &subsTypes);   /* substrate */
		    else
			subsTypes = DBZeroTypeBits;
		    if (argc > 6) subsName = argv[6];
		    break;

		case DEV_CAP:
		case DEV_CAPREV:
		    DBTechNoisyNameMask(argv[4], &termtypes[0]); /* bottom */
		    termtypes[1] = DBZeroTypeBits;

		    if (argc > 5)
			gccap = aToCap(argv[argc - 1]);		/* area cap */
		    if ((argc > 6) && StrIsNumeric(argv[argc - 2]))
		    {
			gscap = aToCap(argv[argc - 2]);		/* perimeter cap */
			argc--;
		    }
		    nterm = 1;

		    if ((argc > 6) && strcmp(argv[5], "None"))
			DBTechNoisyNameMask(argv[5], &subsTypes);   /* substrate */
		    else
			subsTypes = DBZeroTypeBits;
		    if (argc > 7) subsName = argv[6];
		    break;

		case DEV_SUBCKT:
		case DEV_MSUBCKT:
		    // Determine if [substrate, name] optional arguments
		    // are present by checking if the last argument
		    // parses as a layer list.

		    if (DBTechNameMask(argv[argc - 1], &termtypes[0]) <= 0)
		    {
			if (strcmp(argv[argc - 2], "None"))
			    DBTechNoisyNameMask(argv[argc - 2], &subsTypes);
			else
			    subsTypes = DBZeroTypeBits;
			subsName = argv[argc - 1];
			argc -= 2;
		    }

		    if (StrIsInt(argv[4]))
		    {
			nterm = atoi(argv[4]);
			iterm = 5;
			if (nterm > argc - 5)
			{
			    TechError("Not enough terminals for subcircuit, "
					"%d were required, %d found.\n",
					nterm, argc - 5);
			    nterm = argc - 5;
			}
		    }
		    else
		    {
			nterm = argc - 4;
			iterm = 4;
		    }
		    
		    /* terminals */
		    for (i = iterm; i < iterm + nterm; i++)
			DBTechNoisyNameMask(argv[iterm], &termtypes[i - iterm]);
		    termtypes[nterm] = DBZeroTypeBits;

		    if (nterm == 0) i++;

		    // Type MSUBCKT:  If source and drain are symmetric (both
		    // have the same types), then they must both be declared,
		    // but only one is used (same policy as "device mosfet").

		    if ((nterm == 2) && TTMaskEqual(&termtypes[nterm - 1],
				&termtypes[nterm - 2]))
			termtypes[nterm - 1] = DBZeroTypeBits;

		    break;

		case DEV_RSUBCKT:
		    nterm = 2;
		    DBTechNoisyNameMask(argv[4], &termtypes[0]);	/* terminals */
		    termtypes[1] = DBZeroTypeBits;

		    if ((argc > 5) && strcmp(argv[5], "None"))
			DBTechNoisyNameMask(argv[5], &subsTypes);   /* substrate */
		    else
			subsTypes = DBZeroTypeBits;
		    if (argc > 6) subsName = argv[6];
		    break;
	    }

	    TTMaskSetMask(&ExtCurStyle->exts_transMask, &types1);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	    {
		if (TTMaskHasType(&types1, t))
		{
		    TTMaskSetMask(ExtCurStyle->exts_transConn + t, &types1);

		    for (i = 0; !TTMaskIsZero(&termtypes[i]); i++);
		    ExtCurStyle->exts_transSDTypes[t] = (TileTypeBitMask *)
					mallocMagic((i + 1) * sizeof(TileTypeBitMask));
			
		    for (i = 0; !TTMaskIsZero(&termtypes[i]); i++)
			ExtCurStyle->exts_transSDTypes[t][i] = termtypes[i];
		    ExtCurStyle->exts_transSDTypes[t][i] = DBZeroTypeBits;

		    ExtCurStyle->exts_transSDCount[t] = nterm;
		    ExtCurStyle->exts_transSDCap[t] = gscap;
		    ExtCurStyle->exts_transGateCap[t] = gccap;
		    ExtCurStyle->exts_deviceClass[t] = class;
		    ExtCurStyle->exts_transName[t] =
			    StrDup((char **) NULL, transName);
		    if (subsName != NULL)
			ExtCurStyle->exts_transSubstrateName[t] =
				StrDup((char **) NULL, subsName);
		    ExtCurStyle->exts_transSubstrateTypes[t] = subsTypes;
#ifdef ARIEL
		    {
			int z;

			for (z = TT_TECHDEPBASE; z < DBNumTypes; z++)
			{
			    if (TTMaskHasType(&subsTypes, z))
				TTMaskSetType(&ExtCurStyle->
					exts_subsTransistorTypes[z], t);
			}
		    }
#endif
		    if (subcktParams != NULL)
		    {
			ExtCurStyle->exts_deviceParams[t] = subcktParams;
			subcktParams->pl_count++;
		    }
		}
	    }
	    break;

	case FETRESIST:
	    if (!StrIsInt(argv[3]))
	    {
		TechError("Fet resistivity %s must be numeric\n", argv[3]);
		break;
	    }
	    val = atoi(argv[3]);
	    isLinear = (strcmp(argv[2], "linear") == 0);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		if (TTMaskHasType(&types1, t))
		{
		    he = HashFind(&ExtCurStyle->exts_transResist[t], argv[2]);
		    HashSetValue(he, (spointertype)val);
		    if (isLinear)
			ExtCurStyle->exts_linearResist[t] = val;
		}
	    break;
	case HEIGHT: {
	    float height, thick;

	    if (!StrIsNumeric(argv[2]))
	    {
		TechError("Layer height %s must be numeric\n", argv[2]);
		break;
	    }
	    if (!StrIsNumeric(argv[3]))
	    {
		TechError("Layer thickness %s must be numeric\n", argv[3]);
		break;
	    }
	    height = (float)strtod(argv[2], NULL);
	    thick = (float)strtod(argv[3], NULL);
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		if (TTMaskHasType(&types1, t))
		{
		    ExtCurStyle->exts_height[t] = height;
		    ExtCurStyle->exts_thick[t] = thick;
		}
	    }
	    break;
	case UNITS:
	    if (!strcmp(argv[1], "microns"))
		doConvert = TRUE;
	    else if (!strcmp(argv[1], "um"))
		doConvert = TRUE;
	    else if (strcmp(argv[1], "lambda"))
	 	TechError("Units must be microns or lambda.  Using the "
			"default value (lambda).\n");
	    break;
	case LAMBDA:
	    ExtCurStyle->exts_unitsPerLambda = (float)atof(argv[1]);
	    break;
	case OVERC:
	    DBTechNoisyNameMask(argv[2], &types2);
	    capVal = aToCap(argv[3]);
	    bad = FALSE;
	    shield = DBZeroTypeBits;
	    if (argc > 4)
		DBTechNoisyNameMask(argv[4], &shield);
	    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	    {
		if (!TTMaskHasType(&types1, s)) continue;

		/* Contact overlap caps are determined from residues */
		if (DBIsContact(s)) continue;

		for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		{
		    if (!TTMaskHasType(&types2, t)) continue;

		    /* Contact overlap caps are determined from residues */
		    if (DBIsContact(t)) continue;

		    if (s == t)
		    {
			TechError("Can't have overlap capacitance between"
				" tiles of the same type (%s)\n",
				DBTypeLongName(s));
			bad = TRUE;
			continue;
		    }
  		    p1 = DBPlane(s), p2 = DBPlane(t);
		    if (p1 == p2)
		    {
			TechError("Can't have overlap capacitance between"
				" tiles on the same plane (%s, %s)\n",
				DBTypeLongName(s), DBTypeLongName(t));
			bad = TRUE;
			continue;
		    }
		    if (ExtCurStyle->exts_overlapCap[s][t] > (CapValue) 0)
		    {
			TechError("Only one of \"overlap %s %s\" or"
				" \"overlap %s %s\" allowed\n",
				DBTypeLongName(s), DBTypeLongName(t),
				DBTypeLongName(t), DBTypeLongName(s));
			bad = TRUE;
			continue;
		    }
		    ExtCurStyle->exts_overlapCap[s][t] = capVal;
		    ExtCurStyle->exts_overlapPlanes |= PlaneNumToMaskBit(p1);
		    ExtCurStyle->exts_overlapOtherPlanes[s]
			    |= PlaneNumToMaskBit(p2);
		    TTMaskSetType(&ExtCurStyle->exts_overlapTypes[p1], s);
		    TTMaskSetType(&ExtCurStyle->exts_overlapOtherTypes[s], t);
		    if (argc == 4) continue;

		    /* Shielding */
		    pshield = 0;
		    for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
		    {
			if (TTMaskHasType(&shield, r))
			{
			    /* Shielding types are determined from residues */
			    if (DBIsContact(r)) continue;

			    p3 = DBPlane(r);
			    if (p3 == p1 || p3 == p2)
			    {
				TechError("Shielding type (%s) must be on a"
					" different plane from shielded types.\n",
					DBTypeLongName(r));
				bad = TRUE;
				continue;
			    }
			    pshield |= PlaneNumToMaskBit(p3);
			}
		    }
		    ExtCurStyle->exts_overlapShieldPlanes[s][t] = pshield;
		    ExtCurStyle->exts_overlapShieldTypes[s][t] = shield;
		}
	    }
	    if (bad)
		return (TRUE);
	    break;
	case SIDEOVERLAP:
	    bad = FALSE;
	    DBTechNoisyNameMask(argv[2], &types2);
	    pov = DBTechNoisyNameMask(argv[3], &ov);
	    capVal = aToCap(argv[4]);
	    shield = DBZeroTypeBits;
	    if (argc == 6) DBTechNoisyNameMask(argv[5], &shield);
	    if (TTMaskHasType(&types1, TT_SPACE))
		TechError("Can't have space on inside of edge [ignored]\n");
	    /* It's ok to have the overlap be to space as long as a plane is */
	    /* specified.						     */
	    if (TTMaskHasType(&ov, TT_SPACE))
	    {
		if ((cp = strchr(argv[3],'/')) == NULL)
		{
		    TechError("Must specify plane for sideoverlap to space\n");
		}
		cp++;
		p3 = (spointertype) dbTechNameLookup(cp, &dbPlaneNameLists);
		if (p3 < 0)
		    TechError("Unknown overlap plane %s\n",argv[3]);
		else
		    pov = PlaneNumToMaskBit(p3);
	    }
	    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	    {
		if (!TTMaskHasType(&types1, s))
		    continue;

		/* Side overlap computed from residues */
		if (DBIsContact(s)) continue;

		p1 = DBPlane(s);
		if (PlaneMaskHasPlane(pov, p1))
		    goto diffplane;
		ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(p1);
		TTMaskSetType(&ExtCurStyle->exts_sideTypes[p1], s);
		TTMaskSetMask(&ExtCurStyle->exts_sideEdges[s], &types2);
		for (t = 0; t < DBNumTypes; t++)
		{
		    if (!TTMaskHasType(&types2, t))
			continue;

		    /* Side overlap computed from residues */
		    if (DBIsContact(t)) continue;

		    p2 = DBPlane(t);
		    if (t != TT_SPACE && PlaneMaskHasPlane(pov, p2))
			goto diffplane;
		    TTMaskSetMask(&ExtCurStyle->exts_sideOverlapOtherTypes[s][t], &ov);
		    ExtCurStyle->exts_sideOverlapOtherPlanes[s][t] |= pov;
		    cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		    cnew->ec_cap = capVal;
		    cnew->ec_far = shield; /* Really types that shield */
		    cnew->ec_near = ov;  /* Really types we create cap with */
		    cnew->ec_pmask = pov;
		    cnew->ec_next = ExtCurStyle->exts_sideOverlapCap[s][t];
		    ExtCurStyle->exts_sideOverlapCap[s][t] = cnew;

		    /* Shielding */
		    pshield = 0;
		    for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
		    {
			if (TTMaskHasType(&shield, r))
			{
			    /* Side overlap shielding computed from residues */
			    if (DBIsContact(r)) continue;

			    p3 = DBPlane(r);
			    if (p3 == p1 || p3 == p2)
			    {
				TechError("Shielding type (%s) must be on"
					" a different plane from shielded types.\n",
					DBTypeLongName(r));
				bad = TRUE;
				continue;
			    }
			    pshield |= PlaneNumToMaskBit(p3);
			}
		    }
		    for (o = TT_TECHDEPBASE; o < DBNumTypes; o++)
		    {
			if (TTMaskHasType(&ov, o))
			{
			    ExtCurStyle->exts_sideOverlapShieldPlanes[s][o] |= pshield;
			}
		    }
		}
	    }
	    if (bad)
		return (TRUE);
	    break;
	case SIDEWALL:
	    DBTechNoisyNameMask(argv[2], &types2);
	    DBTechNoisyNameMask(argv[3], &near);
	    DBTechNoisyNameMask(argv[4], &far);
	    if (TTMaskHasType(&types1, TT_SPACE))
		TechError("Can't have space on inside of edge [ignored]\n");
	    capVal = aToCap(argv[5]);
	    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	    {
		if (!TTMaskHasType(&types1, s))
		    continue;
		ExtCurStyle->exts_sidePlanes |= PlaneNumToMaskBit(DBPlane(s));
		TTMaskSetType(&ExtCurStyle->exts_sideTypes[DBPlane(s)], s);
		TTMaskSetMask(&ExtCurStyle->exts_sideEdges[s], &types2);
		for (t = 0; t < DBNumTypes; t++)
		{
		    if (!TTMaskHasType(&types2, t))
			continue;
		    TTMaskSetMask(&ExtCurStyle->exts_sideCoupleOtherEdges[s][t], &far);
		    cnew = (EdgeCap *) mallocMagic((unsigned) (sizeof (EdgeCap)));
		    cnew->ec_cap = capVal;
		    cnew->ec_near = near;
		    cnew->ec_far = far;
		    cnew->ec_next = ExtCurStyle->exts_sideCoupleCap[s][t];
		    cnew->ec_pmask = 0;
		    ExtCurStyle->exts_sideCoupleCap[s][t] = cnew;
		}
	    }
	    break;
	case SIDEHALO:
	    /* Allow floating-point and increase by factor of 1000      */
	    /* to accommodate "units microns".                          */

	    /* Warning:  Due to some gcc bug with an i686 FPU, using a	*/
	    /* result from atof() with a static value like 1000		*/
	    /* produces a NaN result!  sscanf() seems to be safe. . .	*/

	    sscanf(argv[1], "%lg", &dhalo);
	    dhalo *= (double)1000.0;
	    ExtCurStyle->exts_sideCoupleHalo = (int)dhalo;
	    break;
	case PERIMC:
	    DBTechNoisyNameMask(argv[2], &types2);
	    capVal = aToCap(argv[3]);
	    if (capVal == (CapValue) 0)
		break;
	    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
		for (t = 0; t < DBNumTypes; t++)
		    if (TTMaskHasType(&types1, s) && TTMaskHasType(&types2, t))
		    {
			ExtCurStyle->exts_perimCap[s][t] = capVal;
			TTMaskSetType(&ExtCurStyle->exts_perimCapMask[s], t);
		    }
	    break;
	case RESIST: {
	    float chop = 1.0;

	    if (!StrIsInt(argv[2]))
	    {
		if (!strcmp(argv[2], "None"))
		{
		    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
			if (TTMaskHasType(&types1, t))
			    TTMaskClearType(&ExtCurStyle->exts_activeTypes, t);
		    break;
		}
		else
		{
		    TxError("Resist argument must be integer or \"None\".\n");
		    break;
		}
	    }
	    else
		val = atoi(argv[2]);

	    if (argc == 4)
		chop = atof(argv[3]);
	    class = ExtCurStyle->exts_numResistClasses++;
	    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
		if (TTMaskHasType(&types1, t))
		{
		    ExtCurStyle->exts_sheetResist[t] = val;
		    ExtCurStyle->exts_cornerChop[t] = chop;
		    ExtCurStyle->exts_typeToResistClass[t] = class;
		}
	    ExtCurStyle->exts_resistByResistClass[class] = val;
	    ExtCurStyle->exts_typesByResistClass[class] = types1;
	    }
	    break;
	case RSCALE:
	    ExtCurStyle->exts_resistScale = atoi(argv[1]);
	    break;
	case STEP:
	    val = (int)atof(argv[1]);
	    if (val <= 0)
	    {
		TechError("Hierarchical interaction step size must be > 0\n");
		return (FALSE);
	    }
	    ExtCurStyle->exts_stepSize = val;
	    break;
	case SUBSTRATE:
	    TTMaskZero(&ExtCurStyle->exts_globSubstrateTypes);
	    TTMaskSetMask(&ExtCurStyle->exts_globSubstrateTypes, &types1);
	    ExtCurStyle->exts_globSubstratePlane = DBTechNoisyNamePlane(argv[2]);
	    break;
	case NOPLANEORDER: {
	     if ( ExtCurStyle->exts_planeOrderStatus == seenPlaneOrder ) 
		TechError("\"noplaneordering\" specified after \"planeorder\".\n");
	     else 
		ExtCurStyle->exts_planeOrderStatus = noPlaneOrder ;
	    }
	    break ;
	case PLANEORDER: {
	    int pnum = (spointertype) dbTechNameLookup(argv[1], &dbPlaneNameLists);
	    int pos = atoi(argv[2]);

	    if ( ExtCurStyle->exts_planeOrderStatus == noPlaneOrder ) {
		TechError("\"planeorder\" specified after \"noplaneordering\".\n");
	    }
	    ExtCurStyle->exts_planeOrderStatus = seenPlaneOrder ;
	    if (pnum < 0) 
		TechError("Unknown planeorder plane %s\n", argv[1]);
	    else if (pos < 0 || pos >= DBNumPlanes-PL_TECHDEPBASE)
		TechError("Planeorder index must be [0..%d]\n", 
		    DBNumPlanes-PL_TECHDEPBASE-1);
	    else
		ExtCurStyle->exts_planeOrder[pnum] = pos;
	    }
	    break;
    }
    return (TRUE);

usage:
    TechError("Malformed line for keyword %s.  Correct usage:\n\t%s %s\n",
		    kp->k_name, kp->k_name, kp->k_usage);
    return (TRUE);

diffplane:
    TechError("Overlapped types in \"sideoverlap\" rule must be on a\n"
		"\tdifferent plane from intypes and outtypes.\n");
    return (TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * ExtTechFinal --
 *
 * Postprocess the technology specific information for extraction.
 * Builds the connectivity tables exts_nodeConn[], exts_resistConn[],
 * and exts_transConn[].
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the tables mentioned above.
 *	Leaves ExtCurStyle pointing to the first style in the list
 *	ExtAllStyles.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtTechFinal()
{
    ExtStyle *es;

    /* Create a "default" style if there isn't one */
    if (ExtAllStyles == NULL)
    {
	ExtAllStyles = (ExtKeep *)mallocMagic(sizeof(ExtKeep));
	ExtAllStyles->exts_next = NULL;
	ExtAllStyles->exts_name = StrDup((char **) NULL, "default");

	ExtCurStyle = extTechStyleNew();
	ExtCurStyle->exts_name = ExtAllStyles->exts_name;
	ExtCurStyle->exts_status = TECH_LOADED;
    }
    extTechFinalStyle(ExtCurStyle);
}


void
extTechFinalStyle(style)
    ExtStyle *style;
{
    TileTypeBitMask maskBits;
    TileType r, s, t;
    int p, p1, missing, conflict;
    int indicis[NP];

    for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
    {
	maskBits = style->exts_nodeConn[r] = DBConnectTbl[r];
	if (!TTMaskHasType(&style->exts_transMask, r))
	{
	     TTMaskZero(&style->exts_transConn[r]);
	}
	for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	{
	    if (TTMaskHasType(&maskBits, s))
		if (style->exts_typeToResistClass[s]
			!= style->exts_typeToResistClass[r])
		    TTMaskClearType(&maskBits, s);
	}
	style->exts_resistConn[r] = maskBits;
    }

    /* r ranges over types, s over resistance entries */
    for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
    {
	s = style->exts_typeToResistClass[r];
	if (s >= 0)
	    TTMaskClearMask(&style->exts_typesResistChanged[r],
			&style->exts_typesByResistClass[s]);
    }

    /*
     * Residue check:
     * We have ignored all contact types when parsing parasitic
     * capacitances.  Now we need to add them.  For each contact
     * type, add the contact type to the types lists accordingly.
     * Note that we don't have to record any cap values, since the
     * extraction routine dissolves contacts into their residue
     * types when computing the parasitics.  But, the type must be
     * in the type lists or contact tiles will be passed over during
     * searches.
     */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	TileTypeBitMask rmask;
	PlaneMask pMask;
	TileType q;

	if (!DBIsContact(s)) continue;

	pMask = DBTypePlaneMaskTbl[s];
	for (p = 0; p < DBNumPlanes; p++)
	{
	    if (PlaneMaskHasPlane(pMask, p))
	    {
		TTMaskSetType(&style->exts_overlapTypes[p], s);
		TTMaskSetType(&style->exts_sideTypes[p], s);
	    }
	}
	DBFullResidueMask(s, &rmask);
	for (r = TT_TECHDEPBASE; r < DBNumUserLayers; r++)
	{
	    if (!TTMaskHasType(&rmask, r)) continue;

	    TTMaskSetMask(&style->exts_sideEdges[s], &style->exts_sideEdges[r]);

	    for (q = TT_TECHDEPBASE; q < DBNumUserLayers; q++)
	    {
		if (TTMaskHasType(&style->exts_overlapOtherTypes[q], r)) 
		    TTMaskSetType(&style->exts_overlapOtherTypes[q], s);

	        for (t = TT_TECHDEPBASE; t < DBNumUserLayers; t++)
		    if (TTMaskHasType(&style->exts_overlapShieldTypes[q][t], r)
				&& !TTMaskHasType(&rmask, q)
				&& !TTMaskHasType(&rmask, t))
			TTMaskSetType(&style->exts_overlapShieldTypes[q][t], s);

		/* For sideOverlap, t is "outtypes" and includes space, so we	*/
		/* must count from TT_SPACE, not TT_TECHDEPBASE.		*/

	        for (t = TT_SPACE; t < DBNumUserLayers; t++)
		    if (TTMaskHasType(&style->exts_sideOverlapOtherTypes[q][t], r)) 
			TTMaskSetType(&style->exts_sideOverlapOtherTypes[q][t], s);
	    }
	}
    }

    /*
     * Consistency check:
     * If a type R shields S from T, make sure that R is listed as
     * being in the list of overlapped types for S, even if there
     * was no overlap capacitance explicitly specified for this
     * pair of types in an "overlap" line.  This guarantees that
     * R will shield S from substrate even if there is no capacitance
     * associated with the overlap.
     */
    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
	for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	{
	    if (style->exts_overlapShieldPlanes[s][t] == 0)
		continue;
	    for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
	    {
		if (!TTMaskHasType(&style->exts_overlapShieldTypes[s][t], r))
		    continue;
		p1 = DBPlane(s);
		style->exts_overlapPlanes |= PlaneNumToMaskBit(p1);
		style->exts_overlapOtherPlanes[s]
			|= PlaneNumToMaskBit(DBPlane(r));
		TTMaskSetType(&style->exts_overlapTypes[p1], s);
		TTMaskSetType(&style->exts_overlapOtherTypes[s], r);
	    }
	}

    /* Finally, for all coupling type masks, remove those types	 */
    /* that have been declared not to participate in extraction. */

    for (s = TT_TECHDEPBASE; s < DBNumTypes; s++)
    {
	TTMaskAndMask(&style->exts_overlapOtherTypes[s], &style->exts_activeTypes);
	TTMaskAndMask(&style->exts_perimCapMask[s], &style->exts_activeTypes);

	for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
	{
	    TTMaskAndMask(&style->exts_overlapShieldTypes[s][t], 
			&style->exts_activeTypes);
	    TTMaskAndMask(&style->exts_sideOverlapOtherTypes[s][t], 
			&style->exts_activeTypes);
	    TTMaskAndMask(&style->exts_sideCoupleOtherEdges[s][t],
			&style->exts_activeTypes);
	}
    }

    for (p = 0; p < DBNumPlanes; p++)
    {
	TTMaskAndMask(&style->exts_overlapTypes[p], &style->exts_activeTypes);
	TTMaskAndMask(&style->exts_sideTypes[p], &style->exts_activeTypes);
    }

    if ( style->exts_planeOrderStatus == noPlaneOrder ) 
    	return /* no need to check */ ;
    /* Else Check to make sure the plane order is a permutation of the
       numbers 0..DBNumPlanes-DBNumPlanes-1 */
    for (p1 = PL_TECHDEPBASE; p1 < DBNumPlanes; p1++) {
	indicis[p1] = 0;
    }
    for (p1 = PL_TECHDEPBASE; p1 < DBNumPlanes; p1++) {
	int pn = style->exts_planeOrder[p1]+PL_TECHDEPBASE;
	if (pn >= PL_TECHDEPBASE && pn < DBNumPlanes)
	    indicis[pn]++;
    }
    conflict = FALSE;
    missing = FALSE;
    for (p1 = PL_TECHDEPBASE; p1 < DBNumPlanes; p1++) {
	if (indicis[p1] > 1) conflict = TRUE ;
	if (indicis[p1] < 1) missing = TRUE ;
    }
    if (!conflict && !missing)		/* Everything was ok */
	goto zinit; 	

    TxError ("\nWarning: Extraction Style %s\n", style -> exts_name);
    if (conflict) {
	TxError ("  Conflicting planeorder for plane(s):\n   ");
	for (p1 = PL_TECHDEPBASE; p1 < DBNumPlanes; p1++) {
	    if (indicis[p1] > 1)
		TxError (" %s,", DBPlaneLongNameTbl[p1]);
	}
    	TxError("\n");
    }
    if (missing) {
	TxError ("  Missing planeorder for plane(s):\n   ");
	for (p1 = PL_TECHDEPBASE; p1 < DBNumPlanes; p1++) {
	    if (indicis[p1] < 1)
		TxError (" %s,", DBPlaneLongNameTbl[p1]);
	}
    	TxError("\n");
    }
    TxError("  Magic will use the default planeorder for this style:\n   ");
    for (p1 = PL_TECHDEPBASE; p1 < DBNumPlanes; p1++) {
    	style->exts_planeOrder[p1] = p1 - PL_TECHDEPBASE ;
	TxError(" %s=%d,",DBPlaneLongNameTbl[p1], style->exts_planeOrder[p1]);
    }
    TxError("\n");

    /* Now that we have a plane ordering, we can apply default height	*/
    /* and thickness values for those layers.				*/

zinit:
    for (r = TT_TECHDEPBASE; r < DBNumTypes; r++)
    {
	if (style->exts_thick[r] == 0)
	    style->exts_thick[r] = 0.05;
	if (style->exts_height[r] == 0)
	    style->exts_height[r] = 0.1 * style->exts_planeOrder[DBPlane(r)];
    }

    /* If global variable "doConvert" is TRUE, then we convert from	*/
    /* microns to lambda and microns^2 to lambda^2.			*/

    if (doConvert)
    {
	/* exts_unitsPerLambda is in centimicrons, so divide by		*/
	/* 100 to get microns.						*/

	CapValue scalefac = (CapValue)style->exts_unitsPerLambda / 100.0;
	CapValue sqfac = scalefac * scalefac;

	for (r = 0; r < DBNumTypes; r++)
	{
	    style->exts_areaCap[r] *= sqfac;
	    style->exts_transSDCap[r] *= sqfac;
	    style->exts_transGateCap[r] *= sqfac;

	    for (s = 0; s < DBNumTypes; s++)
	    {
		EdgeCap *ec;

		style->exts_perimCap[r][s] *= scalefac;
		style->exts_overlapCap[r][s] *= sqfac;
		for (ec = style->exts_sideOverlapCap[r][s]; ec != NULL;
				ec = ec->ec_next)
		    ec->ec_cap *= scalefac;

		// Note that because sidewall caps are referred to
		// a specific distance, the value (run / separation)
		// is unscaled, so the capacitance does not get
		// modified by the scalefactor.  However, the lambda
		// reference for sidewall cap is 2 lambda, so if 
		// the reference is to be interpreted as 1 micron,
		// the value needs to be divided by 2 (the factor of
		// 2 is made up by the fact that the sidewall is
		// independently accumulated on each plate of the
		// capacitor)

		for (ec = style->exts_sideCoupleCap[r][s]; ec != NULL;
				ec = ec->ec_next)
		    ec->ec_cap *= 0.5;
	    }
	}

	/* side halo and step size are also in microns */

	style->exts_sideCoupleHalo = (int)(((CapValue)style->exts_sideCoupleHalo
		/ scalefac) + 0.5);
	style->exts_stepSize = (int)(((CapValue)style->exts_stepSize
		/ scalefac) + 0.5);
    }

    /* Avoid setting stepSize to zero, or extraction will hang! */
    if (style->exts_stepSize <= 0)
    {
	TxError("Warning:  zero step size!  Resetting to default.\n");
	style->exts_stepSize = 100;		/* Revert to default */
    }

    /* We had multiplied sideCoupleHalo by 1000 to accommodate a 	*/
    /* floating-point value in microns, whether or not doConvert was	*/
    /* needed, so normalize it back to lambda units.			*/

    style->exts_sideCoupleHalo /= 1000;
}

/*
 * ----------------------------------------------------------------------------
 * ExtTechScale --
 *
 *	Scale all extraction values appropriately when rescaling the grid.
 * ----------------------------------------------------------------------------
 */

void
ExtTechScale(scalen, scaled)
    int scalen;			/* Scale numerator */
    int scaled;			/* Scale denominator */
{
    ExtStyle *style = ExtCurStyle;
    EdgeCap *ec;
    int i, j;
    float sqn, sqd;

    if (style == NULL) return;

    sqn = (float)(scalen * scalen);
    sqd = (float)(scaled * scaled);

    style->exts_unitsPerLambda = style->exts_unitsPerLambda * (float)scalen
		/ (float)scaled;
    DBScaleValue(&style->exts_sideCoupleHalo, scaled, scalen);
    DBScaleValue(&style->exts_stepSize, scaled, scalen);

    for (i = 0; i < DBNumTypes; i++)
    {
	style->exts_areaCap[i] *= sqn;
	style->exts_areaCap[i] /= sqd;

	style->exts_transSDCap[i] *= sqn;
	style->exts_transSDCap[i] /= sqd;
	style->exts_transGateCap[i] *= sqn;
	style->exts_transGateCap[i] /= sqd;

	style->exts_height[i] *= scaled;
	style->exts_height[i] /= scalen;
	style->exts_thick[i] *= scaled;
	style->exts_thick[i] /= scalen;

	for (j = 0; j < DBNumTypes; j++)
	{
	    style->exts_perimCap[i][j] *= scalen;
	    style->exts_perimCap[i][j] /= scaled;
	    style->exts_overlapCap[i][j] *= sqn;
	    style->exts_overlapCap[i][j] /= sqd;    /* Typo fixed in 7.2.57 */

	    // Do not scale sidewall cap, for while the value is
	    // per distance, the distance is referred to a separation
	    // distance in the same units, so the cap never scales.

	    // for (ec = style->exts_sideCoupleCap[i][j]; ec != NULL;
	    //			ec = ec->ec_next)
	    // {
	    //	ec->ec_cap *= scalen;
	    //	ec->ec_cap /= scaled;
	    // }
	    for (ec = style->exts_sideOverlapCap[i][j]; ec != NULL;
				ec = ec->ec_next)
	    {
		ec->ec_cap *= scalen;
		ec->ec_cap /= scaled;
	    }
	}
    }

    return;
}

