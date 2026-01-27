/*
 * extparse.h
 *
 * Definitions for the .ext file parser.  Relocated from EFread.c.
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

/* These must be in the order of "known devices" in extract/extract.h.	*/
/* This table is also used in extract/ExtBasic.c			*/

/* Note: "fet" refers to the original fet type; "mosfet" refers to the	*/
/* new type.  The main difference is that "fet" records area/perimeter	*/
/* while "mosfet" records length/width.					*/

#ifndef MAGIC_WRAPPER
const char * const extDevTable[] = {"fet", "mosfet", "asymmetric", "bjt", "devres",
		"devcap", "devcaprev", "vsource", "diode", "pdiode",
		"ndiode", "subckt", "rsubckt", "msubckt", "csubckt",
		"dsubckt", "veriloga", NULL};
#endif

/*
 * The following table describes the kinds of lines
 * that may be read in a .ext file.
 */
typedef enum
{
    ABSTRACT, ADJUST, ATTR, CAP, DEVICE, DIST, EQUIV, FET, KILLNODE, MERGE,
    NODE, PARAMETERS, PORT, PRIMITIVE, RESISTOR, RESISTCLASS, RNODE, SCALE,
    SUBCAP, SUBSTRATE, TECH, TIMESTAMP, USE, VERSION, EXT_STYLE
} Key;

static const struct
{
    const char	*k_name;	/* Name of first token on line */
    Key 	 k_key;		/* Internal name for token of this type */
    int		 k_mintokens;	/* Min total # of tokens on line of this type */
}
keyTable[] =
{
    {"abstract",	ABSTRACT,	0},	/* defines a LEF-like view */
    {"adjust",		ADJUST,		4},
    {"attr",		ATTR,		8},
    {"cap",		CAP,		4},
    {"device",		DEVICE,		11},	/* effectively replaces "fet" */
    {"distance",	DIST,		4},
    {"equiv",		EQUIV,		3},
    {"fet",		FET,		12},	/* for backwards compatibility */
    {"killnode",	KILLNODE,	2},
    {"merge",		MERGE,		3},
    {"node",		NODE,		7},
    {"parameters",	PARAMETERS,	3},
    {"port",		PORT,		8},
    {"primitive",	PRIMITIVE,	0},	/* defines a primitive device */
    {"resist",		RESISTOR,	4},
    {"resistclasses",	RESISTCLASS,	1},
    {"rnode",		RNODE,		5},
    {"scale",		SCALE,		4},
    {"subcap",		SUBCAP,		3},
    {"substrate",	SUBSTRATE,	3},
    {"tech",		TECH,		2},
    {"timestamp",	TIMESTAMP,	2},
    {"use",		USE,		9},
    {"version",		VERSION,	2},
    {"style",		EXT_STYLE,	2},
    {0}
};

/* atoCap - convert a string to a EFCapValue */
#define	atoCap(s)	((EFCapValue)atof(s))

extern int efReadLineNum;      /* Current line number in the .ext file */

