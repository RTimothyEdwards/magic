/*
 * EFread.c -
 *
 * Procedures to read a .ext file and call the procedures
 * in EFbuild.c to build up a description of each def.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFread.c,v 1.4 2009/01/30 03:51:02 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "commands/commands.h"
#include "database/database.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"
#include "utils/paths.h"

#ifndef MAGIC_WRAPPER
/* This must match the definition for extDevTable in extract/ExtBasic.c */
char *extDevTable[] = {"fet", "mosfet", "asymmetric", "bjt", "devres",
		"devcap", "devcaprev", "diode", "pdiode", "ndiode",
		"subckt", "rsubckt", "msubckt", NULL};
#endif

/*
 * The following table describes the kinds of lines
 * that may be read in a .ext file.
 */
typedef enum
{
    ABSTRACT, ADJUST, ATTR, CAP, DEVICE, DIST, EQUIV, FET, KILLNODE, MERGE,
    NODE, PARAMETERS, PORT, RESISTOR, RESISTCLASS, RNODE, SCALE, SUBCAP,
    SUBSTRATE, TECH, TIMESTAMP, USE, VERSION, EXT_STYLE
} Key;

static struct
{
    char	*k_name;	/* Name of first token on line */
    Key 	 k_key;		/* Internal name for token of this type */
    int		 k_mintokens;	/* Min total # of tokens on line of this type */
}
keyTable[] =
{
    "abstract",		ABSTRACT,	0,	/* defines a LEF-like view */
    "adjust",		ADJUST,		4,
    "attr",		ATTR,		8,
    "cap",		CAP,		4,
    "device",		DEVICE,		11,	/* effectively replaces "fet" */
    "distance",		DIST,		4,
    "equiv",		EQUIV,		3,
    "fet",		FET,		12,	/* for backwards compatibility */
    "killnode",		KILLNODE,	2,
    "merge",		MERGE,		3,
    "node",		NODE,		7,
    "parameters",	PARAMETERS,	3,
    "port",		PORT,		8,
    "resist",		RESISTOR,	4,
    "resistclasses",	RESISTCLASS,	1,
    "rnode",		RNODE,		5,
    "scale",		SCALE,		4,
    "subcap",		SUBCAP,		3,
    "substrate",	SUBSTRATE,	3,
    "tech",		TECH,		2,
    "timestamp",	TIMESTAMP,	2,
    "use",		USE,		9,
    "version",		VERSION,	2,
    "style",		EXT_STYLE,	2,
    0
};

/* Data shared with EFerror.c */
char *efReadFileName;	/* Name of file currently being read */
int efReadLineNum;	/* Current line number in above file */

/* Data local to this file */
static bool efReadDef();

/* atoCap - convert a string to a EFCapValue */
#define	atoCap(s)	((EFCapValue)atof(s))


/*
 * ----------------------------------------------------------------------------
 *
 * EFReadFile --
 *
 * Main procedure to read a .ext file.  If there is no Def by the
 * name of 'name', allocates a new one.  Calls efReadDef to do the
 * work of reading the def itself.  If 'dosubckt' is true, then port
 * mappings are kept.  If 'resist' is true, read in the .res.ext file
 * (from extresist) if it exists, after reading the .ext file.
 *
 * Results:
 *	Passes on the return value of efReadDef (see below)
 *
 * Side effects:
 *	See above.
 *	Leaves EFTech set to the technology specified with the -T flag
 *	if there was one.  Leaves EFScale set to 1 if it changed while
 *	reading the .ext files.
 *
 * ----------------------------------------------------------------------------
 */

bool
EFReadFile(name, dosubckt, resist, noscale)
    char *name; /* Name of def to be read in */
    bool dosubckt, resist;
{
    Def *def;
    bool  rc;

    def = efDefLook(name);
    if (def == NULL)
	def = efDefNew(name);

    rc = efReadDef(def, dosubckt, resist, noscale, TRUE);
    if (EFArgTech) EFTech = StrDup((char **) NULL, EFArgTech);
    if (EFScale == 0.0) EFScale = 1.0;

    return rc;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efReadDef --
 *
 * Procedure to read in a Def.  Actually does the work of reading
 * the file 'def->def_name'.ext to build up the fields of the new
 * def, then recursively reads all uses of this def that haven't
 * yet been read.
 *
 * Results:
 *	Returns TRUE if successful, FALSE if the file for 'name'
 *	could not be found or we encountered errors.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
efReadDef(def, dosubckt, resist, noscale, toplevel)
   Def *def;
   bool dosubckt, resist, noscale, toplevel;
{
    int argc, ac, n;
    EFCapValue cap;
    char line[1024], *argv[64], *name, *attrs;
    int rscale = 1;	/* Multiply resistances by this */
    int cscale = 1;	/* Multiply capacitances by this */
    float lscale = 1.0;	/* Multiply lambda by this */
    FILE *inf;
    Use *use;
    Rect r;
    bool rc = TRUE;
    bool DoResist = resist;
    bool DoSubCircuit = dosubckt;

    /* Mark def as available */
    def->def_flags |= DEF_AVAILABLE;
    name = def->def_name;
    inf = PaOpen(name, "r", ".ext", EFSearchPath, EFLibPath, &efReadFileName);
    if (inf == NULL)
    {
	/* Complementary to .ext file write:  If file is in a read-only	*/
	/* directory, then .ext	file is written to CWD.			*/
	char *proot;
	proot = strrchr(name, '/');
	if (proot != NULL)
	    inf = PaOpen(proot + 1, "r", ".ext", ".", ".", &efReadFileName);
    }
    if (inf == NULL)
    {
#ifdef MAGIC_WRAPPER
	char *tclres = Tcl_Alloc(128);
	sprintf(tclres, "Cannot read extract file %s.ext\n", name);
	Tcl_SetResult(magicinterp, tclres, TCL_DYNAMIC);
#else
	perror(name);
#endif
	return FALSE;
    }

readfile:
    efReadLineNum = 0;
    while ((argc = efReadLine(line, sizeof line, inf, argv)) >= 0)
    {
	n = LookupStruct(argv[0], (LookupTable *) keyTable, sizeof keyTable[0]);
	if (n < 0)
	{
	    efReadError("Unrecognized token \"%s\" (ignored)\n", argv[0]);
	    continue;
	}
	if (argc < keyTable[n].k_mintokens)
	{
	    efReadError("Not enough tokens for %s line\n", argv[0]);
	    continue;
	}

	switch (keyTable[n].k_key)
	{
	    /* scale rscale cscale lscale */
	    case SCALE:
		rscale = atoi(argv[1]);
		if (rscale == 0)
		{
		    efReadError("Bad resistance scaling = 0; reset to 1.\n");
		    rscale = 1;
		}
		cscale = atoi(argv[2]);
		if (cscale == 0)
		{
		    efReadError("Bad capacitance scaling = 0; reset to 1.\n");
		    cscale = 1;
		}
		lscale = (float)atof(argv[3]);
		if (lscale == 0.0)
		{
		    efReadError("Bad linear scaling = 0; reset to 1.\n");
		    lscale = 1.0;
		}
		if (noscale == FALSE)
		{
		    def->def_scale = lscale;
		    if (EFScale != lscale)
		    {
			if (EFScale != 0) efScaleChanged = TRUE, EFScale = 1.0;
			else EFScale = lscale;
		    }
		}
		break;

	    /* attr node xlo ylo xhi yhi type text */
	    case ATTR:
		r.r_xbot = atoi(argv[2]);
		r.r_ybot = atoi(argv[3]);
		r.r_xtop = atoi(argv[4]);
		r.r_ytop = atoi(argv[5]),
		efBuildAttr(def, argv[1], &r, argv[6], argv[7]);
		break;

	    /* cap node1 node2 capacitance */
	    case CAP:
		cap = cscale*atoCap(argv[3]);
		efBuildCap(def, argv[1], argv[2], (double) cap);
		break;

	    /* subcap node capacitance */
	    case SUBCAP:
		cap = cscale*atoCap(argv[2]);
		efAdjustSubCap(def, argv[1], cap);
		break;

	    /* equiv node1 node2 */
	    case EQUIV:
		efBuildEquiv(def, argv[1], argv[2]);
		break;

	    /* replaces "fet" (below) */
	    /* device mosfet|bjt|subckt	type xlo ylo xhi yhi		*/
	    /*		area perim [substrate] GATE T1 T2 ...		*/
	    /* device res|cap|rsubckt type xlo ylo xhi yhi value	*/
	    /*		GATE T1 T2 ...					*/
	    case DEVICE:
		/* Parse device class */
		for (n = 0; extDevTable[n] != NULL; n++)
		    if (!strcmp(argv[1], extDevTable[n]))
			break;

		if (extDevTable[n] == NULL)
		{
		    efReadError("Unknown device class\n");
		    continue;
		}
		switch (n)
		{
		    case DEV_MOSFET:
		    case DEV_ASYMMETRIC:
		    case DEV_BJT:
			ac = 10;
			break;
		    case DEV_DIODE:
		    case DEV_NDIODE:
		    case DEV_PDIODE:
			ac = 7;
			break;
		    case DEV_CAP:
		    case DEV_CAPREV:
		    case DEV_RES:
			if (!strcmp(argv[2], "None"))	/* Has device value */
			   ac = 8;
			else
			   ac = 9;	/* Has device L and W */
			break;
		    case DEV_SUBCKT:
		    case DEV_MSUBCKT:
		    case DEV_RSUBCKT:
			ac = 7;	/* Actually can have many arguments, which */
			break;	/* we will deal with in efBuildDevice().   */
		}

		r.r_xbot = atoi(argv[3]);
		r.r_ybot = atoi(argv[4]);
		r.r_xtop = atoi(argv[5]);
		r.r_ytop = atoi(argv[6]);

		if (efBuildDevice(def, (char)n, argv[2], &r, argc - 7,
				&argv[7]) != 0)
		{
		    efReadError("Incomplete terminal description for device\n");
		    continue;
		}
		break;

	    /* for backwards compatibility */
	    /* fet type xlo ylo xhi yhi area perim substrate GATE T1 T2 ... */
	    case FET:
		r.r_xbot = atoi(argv[2]);
		r.r_ybot = atoi(argv[3]);
		r.r_xtop = atoi(argv[4]);
		r.r_ytop = atoi(argv[5]);
		if (efBuildDevice(def, DEV_FET, argv[1], &r, argc - 6, &argv[6]) != 0)
		{
		    efReadError("Incomplete terminal description for fet\n");
		    continue;
		}
		break;

	    /* merge node1 node2 C a1 p1 a2 p2 ... */
	    case MERGE:
		/* Redundant merge lines are purposely generated with	*/
		/* no area and perimeter values;  these should not be	*/
		/* flagged as errors.					*/

		/*
		if (argc > 4) && (argc - 4 < 2 * efNumResistClasses))
		{
		    efReadError("Too few area/perim values: "
				"assuming remainder are zero\n");
		}
		*/

		/* cap = (argc > 3) ? atoCap(argv[3]) * cscale : 0; */
		/* 3/1/2017:  Cap adjustments now handled under SUBCAP. */
		efBuildConnect(def, argv[1], argv[2], (double)0.0, &argv[4], argc - 4);
		break;

	    /* node name R C x y layer a1 p1 a2 p2 ... [ attrs ] */
	    case NODE:
	    case SUBSTRATE:
		attrs = NULL;
		ac = argc - 7;
		if (ac & 01)
		    attrs = argv[argc-1], ac--;
		if (ac < 2*efNumResistClasses)
		{
		    efReadError(
		    "Too few area/perim values: assuming remainder are zero\n");
		}
		/* Note: resistance is ignored; we use perim/area instead */
		cap = atoCap(argv[3])*cscale;
		efBuildNode(def,
			    (keyTable[n].k_key == SUBSTRATE) ? TRUE : FALSE,
			    argv[1], (double) cap,
			    atoi(argv[4]), atoi(argv[5]), argv[6],
			    &argv[7], ac);
		break;

	    /* parameters name <type=string ...> */
	    case PARAMETERS:
		efBuildDeviceParams(argv[1], argc - 2, &argv[2]);
		break;

	    /* port name num xl yl xh yh type */
	    case PORT:
		if (DoSubCircuit)
		{
		    DoResist = FALSE;
		    def->def_flags |= DEF_SUBCIRCUIT;
		}
		efBuildPortNode(def, argv[1], atoi(argv[2]), atoi(argv[3]),
					atoi(argv[4]), argv[7]);
		break;

	    /*
	     * rnode name R C x y layer
	     * These are nodes resulting from resistance extraction and
	     * so have no "intrinsic" resistance per se.
	     */
	    case RNODE:
		cap = atoCap(argv[3])*cscale;
		efBuildNode(def, FALSE, argv[1], (double) cap,
			    atoi(argv[4]), atoi(argv[5]), argv[6],
			    (char **) NULL, 0);
		break;

	    /* resist r1 r2 ... */
	    case RESISTCLASS:
		if (efNumResistClasses == 0)
		{
		    efNumResistClasses = argc-1;
		    for (n = 0; n < efNumResistClasses; n++)
			efResists[n] = atoi(argv[n + 1]);
		}
		else if (efNumResistClasses != argc-1)
		{
		    efReadError("Number of resistance classes doesn't match:\n");
resistChanged:
		    efReadError("Re-extract the entire tree with "
				"the same technology file\n");
		    efResistChanged = TRUE;
		    break;
		}
		for (n = 0; n < efNumResistClasses; n++)
		    if (efResists[n] != atoi(argv[n + 1]))
		    {
			efReadError("Resistance class values don't match:\n");
			goto resistChanged;
		    }
		break;

	    /* use def use-id T0 .. T5 */
	    case USE:
		efBuildUse(def, argv[1], argv[2],
			atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
			atoi(argv[6]), atoi(argv[7]), atoi(argv[8]));
		break;

	    /* tech techname */
	    case TECH:
#ifdef MAGIC_WRAPPER
		if (strcmp(argv[1], DBTechName))
		{
		    /* If we are running in batch mode and no layout is	*/
		    /* present, then load the new technology.		*/

		    if (CmdCheckForPaintFunc())
		    {
			TxError("Error: .ext file has different technology %s\n",
				argv[1]);
			TxError("Load this technology and repeat.\n");
			rc = FALSE;
			break;
		    }
		    else
		    {
			TxError("Loading technology %s\n", argv[1]);
			if (!TechLoad(argv[1], 0))
			{
			    TxError("Error in loading technology file\n");
			    rc = FALSE;
			    break;
			}
			else
			    EFTech = StrDup((char **) NULL, argv[1]);
		    }
		}
#else
		if (EFTech && EFTech[0])
		{
		    if (strcmp(EFTech, argv[1]) != 0)
		    {
			efReadError("Technology %s doesn't match initial "
				"technology %s\n", EFTech, argv[1]);
			rc = FALSE;
			break;
		    }
		}
#endif
		else EFTech = StrDup((char **) NULL, argv[1]);

		if (!EFLibPath[0])	/* Put in a path if there wasn't one */
		    (void) sprintf(EFLibPath, EXT_PATH, EFTech);
		break;

	    /* ext_style stylename */
	    case EXT_STYLE:
#ifdef MAGIC_WRAPPER
		if (ExtCompareStyle(argv[1]) == FALSE)
		{
		    TxError("Warning:  .ext file style %s is not known "
				"in this technology!\n", argv[1]);
		    if (EFStyle)
		    {
			freeMagic(EFStyle);
			EFStyle = NULL;
		    }
		}
#else
		if (EFStyle)
		{
		    if (strcmp(EFStyle, argv[1]) != 0)
		    {
			efReadError("Extraction style doesn't match: %s\n", argv[1]);
			rc = FALSE;
			break;
		    }
		}
#endif
		else
		    EFStyle = StrDup(&EFStyle, argv[1]);
		break;

	    /* version version-number */
	    case VERSION:
		if (strcmp(argv[1], EFVersion) != 0)
		{
		    efReadError(
	"Cell was extracted using version %s of the extractor.\n", argv[1]);
		    efReadError("   It should be re-extracted.\n");
		}
		break;

	    /* distance driver receiver min max */
	    case DIST:
		efBuildDist(def, argv[1], argv[2],
			(int)(lscale*atoi(argv[3])),
			(int)(lscale*atoi(argv[4])));
		break;

	    /* killnode nodename */
	    case KILLNODE:
		efBuildKill(def, argv[1]);
		break;

	    /* resistor node1 node2 resistance */
	    case RESISTOR:
		efBuildResistor(def, argv[1], argv[2], rscale*atoi(argv[3]));
		break;

	    /* abstract (no options/arguments) */
	    case ABSTRACT:
		def->def_flags |= DEF_ABSTRACT;
		break;

	    /* To-do: compare timestamp against the cell */
	    case TIMESTAMP:
		break;

	    /* Ignore the rest for now */
	    case ADJUST:	/* Unused */
	    default:
		break;
	}
    }
    (void) fclose(inf);

    /* Is there an "extresist" extract file? */
    if (DoResist)
    {
	DoResist = FALSE;	/* do this only once */
	inf = PaOpen(name, "r", ".res.ext", EFSearchPath, EFLibPath, &efReadFileName);
	if (inf != NULL)
	    goto readfile;
    }

    /* If we are considering standard cells, only the first level of	*/
    /* subcircuits is meaningful.					*/

    if ((def->def_flags & DEF_SUBCIRCUIT) && (toplevel != TRUE))
	DoSubCircuit = FALSE;

    /* Read in each def that has not yet been read in */
    for (use = def->def_uses; use; use = use->use_next)
	if ((use->use_def->def_flags & DEF_AVAILABLE) == 0)
	    if (efReadDef(use->use_def, DoSubCircuit, resist, noscale, FALSE)
			!= TRUE)
		rc = FALSE;

    return rc;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efReadLine --
 *
 * Read a line from a .ext file and split it up into tokens.
 * Blank lines are ignored.  Lines ending in backslash are joined
 * to their successor lines.  Lines beginning with '#' are considered
 * to be comments and are ignored.
 *
 * Results:
 *	Returns the number of tokens into which the line was split, or
 *	-1 on end of file.  Never returns 0.
 *
 * Side effects:
 *	Copies the line just read into 'line'.  The trailing newline
 *	is turned into a '\0'.  The line is broken into tokens which
 *	are then placed into argv.  Updates *plinenum to point to the
 *	current line number in 'file'.
 *
 * ----------------------------------------------------------------------------
 */

int
efReadLine(line, size, file, argv)
    char *line;			/* Character array into which line is read */
    int size;			/* Size of character array */
    FILE *file;	/* Open .ext file */
    char *argv[];		/* Vector of tokens built by efReadLine() */
{
    char *get, *put;
    bool inquote;
    int argc = 0;

    /* Read one line into the buffer, joining lines when they end in '\' */
start:
     get = line;
     while (size > 0)
     {
	efReadLineNum += 1;
	if (fgets(get, size, file) == NULL) return (-1);
	for (put = get; *put != '\n'; put++) size -= 1;
	if ((put != get) && (*(put-1) == '\\'))
	{
	    get = put-1;
	    continue;
	}
	*put= '\0';
	break;
    }
    if (size == 0) efReadError("long line truncated\n");

    get = put = line;

    if (*line == '#') goto start;	/* Ignore comments */

    while (*get != '\0')
    {
	/* Skip leading blanks */
	while (isspace(*get)) get++;

	/* Beginning of the token is here */
	argv[argc] = put = get;
	inquote = FALSE;

	/*
	 * Grab up characters to the end of the token.  Any character
	 * preceded by a backslash is taken literally.
	 */
	while (*get != '\0')
	{
	    if (inquote)
	    {
		if (*get == '"')
		{
		    get++;
		    inquote = FALSE;
		    continue;
		}
	    }
	    else
	    {
		if (isspace(*get))
		    break;
		if (*get == '"')
		{
		    get++;
		    inquote = TRUE;
		    continue;
		}
	    }

	    if (*get == '\\')	/* Process quoted characters literally */
	    {
		get++;
		if (*get == '\0') break;
	    }

	    /* Copy into token receiving area */
	    *put++ = *get++;
	}

	/*
	 * If we got no characters in the token, we must have been at
	 * the end of the line.
	 */
	if (get == argv[argc])
	    break;
	
	/* Terminate the token and advance over the terminating character. */

	if (*get != '\0') get++;	/* Careful!  could be at end of line! */
	*put++ = '\0';
	argc++;
    }

    if (argc == 0)
	goto start;

    return (argc);
}
