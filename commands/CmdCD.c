/*
 * CmdCD.c --
 *
 * Commands with names beginning with the letters C through D.
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
static const char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdCD.c,v 1.7 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "utils/utils.h"
#include "utils/magic_zlib.h"
#include "textio/textio.h"
#include "drc/drc.h"
#include "graphics/graphics.h"
#include "textio/txcommands.h"
#include "textio/textio.h"
#include "cif/cif.h"
#include "calma/calma.h"
#include "utils/styles.h"
#include "router/rtrDcmpose.h"
#include "select/select.h"
#include "utils/signals.h"
#include "utils/malloc.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "calma/calmaInt.h"

/* C99 compat */
#include "dbwind/dbwtech.h"

/* The following structure is used by CmdCorner to keep track of
 * areas to be filled.
 */

struct cmdCornerArea
{
    Rect cca_area;			/* Area to paint. */
    TileType cca_type;			/* Type of material. */
    struct cmdCornerArea *cca_next;	/* Next in list of areas to paint. */
};

/* Forward declarations */
int cmdDumpFunc(Rect *rect, char *name, Label *label, Point *point);
bool cmdDumpParseArgs(char *cmdName, MagWindow *w, TxCommand *cmd, CellUse *dummy, SearchContext *scx);
#ifdef CALMA_MODULE

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCalma --
 *
 * Implement the "gds" or "calma" command.
 *
 * Usage:
 *	gds option args
 *	calma option args
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	There are no side effects on the circuit.  Currently, there
 *	is only a single option, "write", to write a CALMA stream
 *	file.
 *
 * ----------------------------------------------------------------------------
 */

#define CALMA_HELP	0
#define CALMA_ABSTRACT	1
#define CALMA_ADDENDUM	2
#define CALMA_ARRAYS	3
#define CALMA_COMPRESS  4
#define CALMA_CONTACTS	5
#define CALMA_DATESTAMP	6
#define CALMA_DRCCHECK	7
#define	CALMA_FLATTEN	8
#define	CALMA_FLATGLOB  9
#define	CALMA_LABELS	10
#define	CALMA_LIBRARY	11
#define	CALMA_LOWER	12
#define CALMA_MAGSCALE  13
#define CALMA_MASKHINTS	14
#define CALMA_MERGE	15
#define CALMA_NO_STAMP	16
#define CALMA_NO_DUP	17
#define CALMA_ORDERING	18
#define CALMA_READ	19
#define CALMA_READONLY	20
#define CALMA_RESCALE	21
#define CALMA_WARNING	22
#define CALMA_WRITE	23
#define CALMA_POLYS	24
#define CALMA_PATHS	25
#define CALMA_UNDEFINED	26
#define CALMA_UNIQUE	27

#define CALMA_WARN_HELP CIF_WARN_END	/* undefined by CIF module */

void
CmdCalma(
    MagWindow *w,
    TxCommand *cmd)
{
    int option, ext, value;
    const char * const *msg;
    char *namep, *dotptr;
    char writeMode[3];
    CellDef *rootDef;
    FILETYPE f;
    FILE *fp;
#ifdef HAVE_ZLIB
    gzFile fz;
#endif

    static const char * const gdsExts[] = {".gds", ".gds.gz", ".gds2", ".strm", "", NULL};
    static const char * const cmdCalmaYesNo[] = {
		"no", "false", "off", "0", "yes", "true", "on", "1", 0 };
    static const char * const cmdCalmaAllowDisallow[] = {"disallow", "0", "allow", "1", 0};
    static const char * const cmdCalmaPolygonType[] = {"none", "temporary", "keep", 0};
    static const char * const cmdCalmaWarnOptions[] = { "default", "none", "align",
		"limit", "redirect", "help", 0 };
    static const char * const cmdCalmaOption[] =
    {
	"help		print this help information",
	"abstract [allow|disallow]\n"
	"		[dis]allow writing of GDS with calls to abstract cells",
	"addendum [yes|no]	output only cells that are not type \"readonly\"",
	"arrays [yes|no]	output arrays as individual subuses (like in CIF)",
	"compress [value]	compress output with zlib compression 0 to 6",
	"contacts [yes|no]	optimize output by arraying contacts as subcells",
	"datestamp [yes|value]	use current time or value as the creation date stamp",
	"drccheck [yes|no]	mark all cells as needing DRC checking",
	"flatten [yes|no|limit]	flatten simple cells (e.g., contacts) on input",
	"flatglob [<name>|none]	flatten cells by name with glob patterning",
	"labels [yes|no]	cause labels to be output when writing GDS-II",
	"library [yes|no]	do not output the top level, only subcells",
	"lower [yes|no]		allow both upper and lower case in labels",
	"magscale [value]	scale to interpret text magnification 1 in microns",
	"maskhints [yes|no]	generate mask hint properties on input",
	"merge [yes|no]		merge tiles into polygons in the output",
	"nodatestamp [yes|no]	write a zero value creation date stamp",
	"noduplicates [yes|no]	do not read cells that exist before reading GDS",
	"ordering [on|off]	cause cells to be read in post-order",
	"read file		read Calma GDS-II format from \"file\"\n"
	"		into edit cell",
	"readonly [yes|no]	set cell as read-only and generate output from GDS file",
	"rescale [yes|no]	allow or disallow internal grid subdivision",
	"warning [option]	set warning information level",
	"write file		output Calma GDS-II format to \"file\"\n"
	"		for the window's root cell",
	"polygon subcells [yes|no]\n"
	"		put non-Manhattan polygons into subcells",
	"path subcells [yes|no]\n"
	"		put wire paths into individual subcells",
	"undefined [allow|disallow]\n"
	"		[dis]allow writing of GDS with calls to undefined cells",
	"unique [yes|no]	rename any cells with names duplicated in the GDS",
	NULL
    };

    if (cmd->tx_argc == 1)
	option = CALMA_WRITE;
    else
    {
	option = Lookup(cmd->tx_argv[1], cmdCalmaOption);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid gds option.\n", cmd->tx_argv[1]);
	    option = CALMA_HELP;
	    cmd->tx_argc = 2;
	}
    }

    /* Only check for a window on options requiring one */

    switch (option)
    {
	case CALMA_READ: case CALMA_WRITE:
	    windCheckOnlyWindow(&w, DBWclientID);
	    if (w == (MagWindow *) NULL)
	    {
		TxError("Point to a window first\n");
		return;
	    }
	    rootDef = ((CellUse *) w->w_surfaceID)->cu_def;

	    if (cmd->tx_argc == 1)
	    {
		namep = strrchr(rootDef->cd_name, '/');
		if (namep == (char *) NULL)
		    namep = rootDef->cd_name;
		goto outputCalma;
	    }
	    break;
    }

    switch (option)
    {
	case CALMA_HELP:
	    TxPrintf("GDS commands have the form \":gds option\",");
	    TxPrintf(" where option is one of:\n");
	    for (msg = &(cmdCalmaOption[0]); *msg != NULL; msg++)
	    {
		if (**msg == '*') continue;
		TxPrintf("    %s\n", *msg);
	    }
	    TxPrintf("If no option is given, a CALMA GDS-II stream file is\n");
	    TxPrintf("    produced for the root cell.\n");
	    TxPrintf("The current CIF output style (\"cif ostyle\") is used\n");
	    TxPrintf("    to select the mask layers output by :gds write.\n");
	    TxPrintf("The current CIF input style (\"cif istyle\") is used\n");
	    TxPrintf("    to select the mask layers read by :gds read.\n");
	    return;

	case CALMA_LABELS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaDoLabels));
#else
		TxPrintf("Labels will %sbe output to the GDS file.\n",
			(CalmaDoLabels) ?  "" : "not ");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
	    {
		wrongNumArgs:
		TxError("Wrong number of arguments in \"%s\" command.",
			cmd->tx_argv[0]);
		TxError("  Try \":%s help\" for help.\n", cmd->tx_argv[0]);
		return;
	    }

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaDoLabels = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_LIBRARY:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaDoLibrary));
#else
		TxPrintf("The top-level cell will %sbe output to the GDS file.\n",
			(CalmaDoLibrary) ?  "not " : "");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaDoLibrary = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_ADDENDUM:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaAddendum));
#else
		TxPrintf("Read-only cell defs will %sbe output to the GDS file.\n",
			(CalmaAddendum) ?  "not " : "");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaAddendum = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_UNDEFINED:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaAllowUndefined));
#else
		TxPrintf("Writing of GDS file with undefined cells is %sallowed.\n",
			(CalmaAllowUndefined) ?  "" : "dis");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaAllowDisallow);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaAllowUndefined = (option < 2) ? FALSE : TRUE;
	    return;

	case CALMA_ABSTRACT:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaAllowAbstract));
#else
		TxPrintf("Writing of GDS file with abstract cells is %sallowed.\n",
			(CalmaAllowUndefined) ?  "" : "dis");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaAllowDisallow);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaAllowAbstract = (option < 2) ? FALSE : TRUE;
	    return;

	case CALMA_COMPRESS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef HAVE_ZLIB
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(CalmaCompression));
#else
		if (CalmaCompression == 0)
		    TxPrintf("Calma files are not compressed.\n");
		else
		    TxPrintf("Calma files are compressed with zlib compression level %d.\n", CalmaCompression);
#endif
#else	/* !HAVE_ZLIB */
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(0));
#else
		TxPrintf("No file compression has been enabled.\n");
#endif
#endif	/* !HAVE_ZLIB */
		return;
	    }
#ifdef HAVE_ZLIB
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    value = atoi(cmd->tx_argv[2]);
	    if ((value < 0) || (value > 9))
		TxError("Bad compression value %d.  Value must be in the range 0 to 9.\n", value);
	    else
		CalmaCompression = value;
#else
	    goto wrongNumArgs;
#endif
	    return;

	case CALMA_CONTACTS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaContactArrays));
#else
		if (CalmaContactArrays)
		    TxPrintf("Contact areas are arrayed as subcells.\n");
		else
		    TxPrintf("Contact areas are output as individual cuts.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaContactArrays = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_DRCCHECK:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(!CalmaNoDRCCheck));
#else
		TxPrintf("GDS cells read from input file are%s checked for DRC.\n",
			(CalmaNoDRCCheck) ?  " not" : " ");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaNoDRCCheck = (option < 4) ? TRUE : FALSE;
	    return;

	case CALMA_MAGSCALE:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewDoubleObj((double)CalmaMagScale));
#else
		TxPrintf("Text magnification 1.0 = %g microns.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    if (StrIsNumeric(cmd->tx_argv[2]))
		 CalmaMagScale = (float)atof(cmd->tx_argv[2]);
	    else if (!strcmp(cmd->tx_argv[2], "default"))
		 CalmaMagScale = 1.0;
	    else
		goto wrongNumArgs;
	    return;

	case CALMA_FLATTEN:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaFlattenUses));
#else
		if (CalmaFlattenUses)
		    TxPrintf("Small cells in input are flattened.\n");
		else
		    TxPrintf("Cells are never flattened on input.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
	    {
	        if (StrIsInt(cmd->tx_argv[2]))
		{
		    CalmaFlattenLimit = atoi(cmd->tx_argv[2]);
		    CalmaFlattenUses = (CalmaFlattenLimit > 0) ? TRUE : FALSE;
		}
		else
		    goto wrongNumArgs;
	    }
	    else
		CalmaFlattenUses = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_FLATGLOB:

	    if (cmd->tx_argc == 2)
	    {
		if (CalmaFlattenUsesByName != NULL)
		{
		    int i = 0;
		    char *pattern;
#ifdef MAGIC_WRAPPER
		    Tcl_Obj *lobj = Tcl_NewListObj(0, NULL);
		    while (TRUE)
		    {
			pattern = CalmaFlattenUsesByName[i];
			if (pattern == NULL) break;
			i++;
			Tcl_ListObjAppendElement(magicinterp, lobj,
				Tcl_NewStringObj(pattern, -1));
		    }
		    Tcl_SetObjResult(magicinterp, lobj);
#else
		    TxPrintf("Glob patterns for cells to flatten:\n");
		    while (TRUE)
		    {
			pattern = CalmaFlattenUsesByName[i];
			if (pattern == NULL) break;
			i++;
			TxPrintf("   \"%s\"\n", pattern);
		    }
#endif
		}
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    if (!strcasecmp(cmd->tx_argv[2], "none"))
	    {
		int i = 0;
		if (CalmaFlattenUsesByName == (char **)NULL) return;
		while (TRUE)
		{
		    char *pattern = CalmaFlattenUsesByName[i];
		    if (pattern == NULL) break;
		    freeMagic(pattern);
		    i++;
		}
		freeMagic(CalmaFlattenUsesByName);
		CalmaFlattenUsesByName = (char **)NULL;
	    }
	    else
	    {
		char **newpatterns;
		char *pattern;
		int i = 0;

		if (CalmaFlattenUsesByName == (char **)NULL)
		    i = 1;
		else
		{
		    while (TRUE)
		    {
			pattern = CalmaFlattenUsesByName[i++];
			if (pattern == NULL) break;
		    }
		}
		newpatterns = (char **)mallocMagic((i + 1) * sizeof(char *));
		i = 0;
		if (CalmaFlattenUsesByName != (char **)NULL)
		{
		    while (TRUE)
		    {
		    	pattern = CalmaFlattenUsesByName[i];
		    	if (pattern == NULL) break;
		    	newpatterns[i] = StrDup((char **)NULL, pattern);
		    	i++;
		    }
		}
		newpatterns[i++] = StrDup((char **)NULL, cmd->tx_argv[2]);
		newpatterns[i] = (char *)NULL;

		i = 0;
		if (CalmaFlattenUsesByName != (char **)NULL)
		{
		    while (TRUE)
		    {
			pattern = CalmaFlattenUsesByName[i];
			if (pattern == NULL) break;
			freeMagic(pattern);
			i++;
		    }
		    freeMagic(CalmaFlattenUsesByName);
		}
		CalmaFlattenUsesByName = newpatterns;
	    }
	    return;

	case CALMA_ORDERING:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaPostOrder));
#else
		if (CalmaPostOrder)
		    TxPrintf("GDS parser reads cells in post-order.\n");
		else
		    TxPrintf("GDS parser reads cells in the order "
				"encountered in the stream file.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaPostOrder = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_ARRAYS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaFlattenArrays));
#else
		TxPrintf("GDS are %s.\n",
			(CalmaFlattenArrays) ?  "flattened" : "hierarchical");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaFlattenArrays = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_LOWER:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaDoLower));
#else
		TxPrintf("GDS labels are %s.\n",
			(CalmaDoLower) ?  "lowercase only" : "mixed-case");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaDoLower = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_MASKHINTS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		if ((CalmaMaskHints != NULL) && !TTMaskIsZero(CalmaMaskHints))
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(TRUE));
		else
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(FALSE));
#else
		if ((CalmaMaskHints != NULL) && !TTMaskIsZero(CalmaMaskHints))
		    TxPrintf("Mask hints generated from GDS input.\n");
		else
		    TxPrintf("No mask hints generated from GDS input.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    if (CalmaMaskHints == NULL)
	    {
		CalmaMaskHints = (TileTypeBitMask *)mallocMagic(sizeof(TileTypeBitMask));
		TTMaskZero(CalmaMaskHints);
	    }
	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		CIFParseReadLayers(cmd->tx_argv[2], CalmaMaskHints, FALSE);
	    else
	    {
		if (option < 4)
		    TTMaskZero(CalmaMaskHints);
		else
		    TTMaskSetMask(CalmaMaskHints, &DBAllTypeBits);
	    }
	    return;

	case CALMA_MERGE:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaMergeTiles));
#else
		if (CalmaMergeTiles)
		    TxPrintf("Merge connected tiles into polygons on output\n");
		else
		    TxPrintf("Output individual tiles\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaMergeTiles = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_POLYS:
	    if (cmd->tx_argc == 3)
	    {
#ifdef MAGIC_WRAPPER
		switch (CalmaSubcellPolygons)
		{
		    case CALMA_POLYGON_NONE:
		        Tcl_SetObjResult(magicinterp, Tcl_NewStringObj("none", 0));
			break;
		    case CALMA_POLYGON_TEMP:
		        Tcl_SetObjResult(magicinterp, Tcl_NewStringObj("temporary", 0));
			break;
		    case CALMA_POLYGON_KEEP:
		        Tcl_SetObjResult(magicinterp, Tcl_NewStringObj("keep", 0));
			break;
		}
#else
		if (CalmaSubcellPolygons == CALMA_POLYGON_NONE)
		    TxPrintf("Non-manhattan polygons read as-is.\n");
		else if (CalmaSubcellPolygons == CALMA_POLYGON_TEMP)
		    TxPrintf("Non-manhattan polygons placed in temporary subcells.\n");
		else
		    TxPrintf("Non-manhattan polygons placed in subcells.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 4)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[3], cmdCalmaPolygonType);
	    if (option < 0)
	    {
		option = Lookup(cmd->tx_argv[3], cmdCalmaYesNo);
	    	if (option < 0)
		    goto wrongNumArgs;
		else if (option < 4)
		    CalmaSubcellPolygons = (unsigned char)CALMA_POLYGON_NONE;
		else
		    CalmaSubcellPolygons = (unsigned char)CALMA_POLYGON_TEMP;
	    }
	    else
		CalmaSubcellPolygons = (unsigned char)option;
	    return;

	case CALMA_NO_DUP:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaNoDuplicates));
#else
		TxPrintf("Cell defs that exist before reading GDS will not be paresd.\n",
			(CalmaNoDuplicates) ?  "not " : "");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaNoDuplicates = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_UNIQUE:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaUnique));
#else
		TxPrintf("Cell defs that exist before reading GDS will be renamed.\n",
			(CalmaUnique) ?  "not " : "");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaUnique = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_NO_STAMP:
	    /* CALMA_DATESTAMP is the current implementation.		*/
	    /* CALMA_NO_STAMP is retained for backwards-compatibility.	*/
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		if (CalmaDateStamp == NULL)
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(FALSE));
		else if (*CalmaDateStamp == 0)
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(TRUE));
		else
		    Tcl_SetObjResult(magicinterp, Tcl_NewStringObj("fixed", -1));
#else
		if (CalmaDateStamp != NULL)
		    TxPrintf("Structures will contain a header creation date "
			"stamp of %ld.\n", (long)(*CalmaDateStamp));
		else
		    TxPrintf("Structures will contain a default header creation date "
			"stamp.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;

	    /* yes|no:  If "yes", then set date stamp to zero.  If "no", then
	     * leave date stamp pointer NULL, and current date will be used for
	     * the timestamp.
	     */
	    if (option >= 4)
	    {
		if (CalmaDateStamp == NULL) 
		    CalmaDateStamp = (time_t *)mallocMagic(sizeof(time_t));
		*CalmaDateStamp = (time_t)0;
	    }
	    else
	    {
		if (CalmaDateStamp != NULL) 
		{
		    freeMagic((char *)CalmaDateStamp);
		    CalmaDateStamp = (time_t *)NULL;
		}
	    }
	    return;

	case CALMA_PATHS:
	    if (cmd->tx_argc == 3)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaSubcellPaths));
#else
		if (CalmaSubcellPaths)
		    TxPrintf("Wire paths placed in subcells.\n");
		else
		    TxPrintf("Wire paths read as-is.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 4)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[3], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaSubcellPaths = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_READONLY:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CalmaReadOnly));
#else
		TxPrintf("GDS cells read from input file are set read-%s.\n",
			(CalmaReadOnly) ?  "only" : "write");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CalmaReadOnly = (option < 4) ? FALSE : TRUE;
	    return;

	case CALMA_RESCALE:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CIFRescaleAllow));
#else
		TxPrintf("Internal grid rescaling %sallowed\n",
			(CIFRescaleAllow) ?  "" : "dis");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option < 0)
		goto wrongNumArgs;
	    CIFRescaleAllow = (option < 4) ? FALSE : TRUE;
	    if (!CIFRescaleAllow)
		CIFWarningLevel = CIF_WARN_LIMIT;
	    return;

	case CALMA_DATESTAMP:
	    /* CALMA_NO_STAMP is retained for backwards-compatibility. */
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		if (CalmaDateStamp != NULL)
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(*CalmaDateStamp));
		else
		    Tcl_SetObjResult(magicinterp, Tcl_NewStringObj("default", -1));
#else
		if (CalmaDateStamp != NULL)
		    TxPrintf("Structures will contain a header creation date "
			"stamp of %ld.\n", (long)(*CalmaDateStamp));
		else
		    TxPrintf("Structures will contain a default header creation date "
			"stamp.\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto wrongNumArgs;

	    option = Lookup(cmd->tx_argv[2], cmdCalmaYesNo);
	    if (option >= 0)
	    {
		/* option yes|no:  "yes" means set date stamp pointer to NULL
		 * and use current date as the timestamp.  "no" means set the
		 * date stamp to zero.
		 */
		if (option >= 4)
		{
		    if (CalmaDateStamp != NULL) 
		    {
			freeMagic((char *)CalmaDateStamp);
			CalmaDateStamp = (time_t *)NULL;
		    }
		}
		else
		{
		    if (CalmaDateStamp == NULL) 
			CalmaDateStamp = (time_t *)mallocMagic(sizeof(time_t));
		    *CalmaDateStamp = (time_t)0;
		}
	    }
	    else if (StrIsInt(cmd->tx_argv[2]))
	    {
		/* Otherwise, if value is an integer, use it for the date stamp */
		if (CalmaDateStamp == NULL) 
		    CalmaDateStamp = (time_t *)mallocMagic(sizeof(time_t));
		*CalmaDateStamp = atoi(cmd->tx_argv[2]);
	    }
	    else
	    {
	        TxError("Unrecognizable date stamp \"%s\".\n", cmd->tx_argv[2]);
		goto wrongNumArgs;
	    }
	    return;

	case CALMA_WARNING:
	    if (cmd->tx_argc == 2)
	    {
		TxPrintf("Warning display options: %s\n",
			cmdCalmaWarnOptions[CIFWarningLevel]);
	    }
	    else
	    {
		int suboption = Lookup(cmd->tx_argv[2], cmdCalmaWarnOptions);
		if (suboption < 0)
		{
		    TxError("\"%s\" isn't a valid gds warning option.\n",
				cmd->tx_argv[2]);
		    suboption = CALMA_WARN_HELP;
		}
		if (suboption == CALMA_WARN_HELP)
		    TxError("Valid options are: default, align, limit, "
			"redirect, and none.\n");
		else
		    CIFWarningLevel = suboption;

		if (suboption == CIF_WARN_REDIRECT)
		{
		    if (cmd->tx_argc == 4)
			StrDup(&CIFErrorFilename, cmd->tx_argv[3]);
		    else
			StrDup(&CIFErrorFilename, NULL);
		}
	    }
	    return;
	case CALMA_WRITE:
	    if (cmd->tx_argc != 3) goto wrongNumArgs;
	    namep = cmd->tx_argv[2];
	    goto outputCalma;

	case CALMA_READ:
	    if (cmd->tx_argc != 3) goto wrongNumArgs;

	    /* Check for various common file extensions, including	*/
	    /* no extension (as-is), ".gds", ".gds2", and ".strm".	*/

	    for (ext = 0; gdsExts[ext] != NULL; ext++)
		if ((f = PaZOpen(cmd->tx_argv[2], "r", gdsExts[ext], Path,
		    	(char *) NULL, &namep)) != (FILETYPE)NULL)
		    break;

	    if (f == (FILETYPE) NULL)
	    {
	        TxError("Cannot open %s.gds, %s.strm or %s to read "
			"GDS-II stream input.\n",
			cmd->tx_argv[2], cmd->tx_argv[2], cmd->tx_argv[2]);
	        return;
	    }

	    /* Ensure that there is a valid edit cell */
	    if (EditCellUse == NULL)
		DBWloadWindow(w, (char *)NULL, DBW_LOAD_IGNORE_TECH);

	    CalmaReadFile(f, namep);
	    (void) FCLOSE(f);
	    return;
    }

    /*
     * If control gets here, we're going to output GDS-II (stream)
     * into the file given by namep.
     */

outputCalma:
    dotptr = strrchr(namep, '.');

#ifdef HAVE_ZLIB
    /* Handle compression based on value of CalmaCompression */
    if (CalmaCompression > 0)
    {
	sprintf(writeMode, "w%d", CalmaCompression);
	fz = PaZOpen(namep, writeMode, (dotptr == NULL) ? ".gds.gz" : "", ".", (char *) NULL, (char **)NULL);
	if (fz == (gzFile)NULL)
	{
	    TxError("Cannot open %s%s to write compressed GDS-II stream output\n", namep,
			(dotptr == NULL) ? ".gds.gz" : "");
	    return;
	}
	if (!CalmaWriteZ(rootDef, fz))
	{
	    TxError("I/O error in writing compressed file %s.\n", namep);
	    TxError("File may be incompletely written.\n");
	}
	(void) gzclose(fz);
    }
    else
    {

#endif
	fp = PaOpen(namep, "w", (dotptr == NULL) ? ".gds" : "", ".", (char *) NULL, (char **)NULL);
	if (fp == (FILE *)NULL)
	{
	    TxError("Cannot open %s%s to write GDS-II stream output\n", namep,
			(dotptr == NULL) ? ".gds" : "");
	    return;
	}
	if (!CalmaWrite(rootDef, fp))
	{
	    TxError("I/O error in writing file %s.\n", namep);
	    TxError("File may be incompletely written.\n");
	}
	(void) fclose(fp);

#ifdef HAVE_ZLIB
    }
#endif

}
#endif /* CALMA_MODULE */

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCellname --
 *
 * Implement the "cellname" and "instance" commands. List the child(ren),
 * parent(s), or name of the selected or indicated cell or instance.
 *
 * Usage:
 *	cellname [list] children|parents|exists|instances|
 *			celldef|self|childinst [name]
 * or
 *	cellname [list] topcells|allcells|window
 * or
 *	cellname delete [name]
 * or
 *	cellname writeable [name] [true|false]
 * or
 *	cellname rename [name] [newname]
 * or
 *	cellname [list] filepath [path|"default"]
 * or
 *	cellname property [name] [property_key [property_value]]
 * or
 *	instance orientation [name] [-def] [orient]
 *	instance abutment [name]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Print out the names of all the cell currently loaded.
 *	Or (optionally) only the cell given.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */

void
CmdCellname(
    MagWindow *w,
    TxCommand *cmd)
{
    bool is_cellname;
    bool dolist = FALSE;
    bool dodef = FALSE;
    bool doforce = FALSE;
    int option;
    int locargc = cmd->tx_argc;
    char *cellname = NULL, *orient = NULL;
    void (*func)();
    CellDef *newDef, *cellDef;

    static const char * const cmdCellOption[] =
    {
	"children 	list children of selected or named cell",
	"parents 	list parents of selected or named cell",
	"exists 	true if loaded, false if not, or name of selected",
	"self		true if loaded, false if not, or name of selected",
	"instances	list instances of a cell definition",
	"childinst 	list children instances of a cell definition",
	"celldef	name the cell definition of an instance",
	"allcells	list all cells",
	"topcells	list top-level cells",
	"window		list top-level cell of a layout window",
	"create		create a new cell definition",
	"delete		delete the named cell definition",
	"dereference	reload the named cell from the search paths",
	"filepath	list the full path of the file for the cell",
	"flags		list option flags of the indicated cell definition",
	"timestamp 	list the cell timestamp",
	"lock		lock the named cell (prevent changes to cell use)",
	"unlock		unlock the named cell (allow changes to cell use)",
	"property	list or set cell definition properties",
	"abutment	list instance abutment coordinates",
	"orientation	list or set instance orientation",
	"rename		rename the indicated cell",
	"writeable	make the cell definition read-only or read-write",
	"modified	true if modified, false if not",
	NULL
    };
    typedef enum { IDX_CHILDREN, IDX_PARENTS, IDX_EXISTS, IDX_SELF,
		   IDX_INSTANCE, IDX_CHILDINST, IDX_CELLDEF, IDX_ALLCELLS,
		   IDX_TOPCELLS, IDX_IN_WINDOW, IDX_CREATE, IDX_DELETE,
		   IDX_DEREFERENCE, IDX_FILEPATH, IDX_FLAGS, IDX_TIMESTAMP,
		   IDX_LOCK, IDX_UNLOCK, IDX_PROPERTY, IDX_ABUTMENT,
		   IDX_ORIENTATION, IDX_RENAME, IDX_READWRITE,
		   IDX_MODIFIED } optionType;

    static const char * const cmdCellnameYesNo[] = {
		"no", "false", "off", "0", "yes", "true", "on", "1", 0 };

    if (strstr(cmd->tx_argv[0], "in"))
	is_cellname = FALSE;
    else
	is_cellname = TRUE;

    func = (is_cellname) ? DBCellPrint : DBUsePrint;

    if (locargc > 1)
    {
	if (!strcmp(cmd->tx_argv[1], "list")) {
	    dolist = TRUE;
	    locargc--;
	}
    }
    
    /* Check for option at end of option list */

    if (*cmd->tx_argv[cmd->tx_argc - 1] == '-') {
	char *option = cmd->tx_argv[cmd->tx_argc - 1] + 1;
	if (!strcmp(option, "list")) {
	    dolist = TRUE;
	    locargc--;
	}
	else if (!strcmp(option, "def")) {
	    dodef = TRUE;
	    locargc--;
	}
	else if (!strcmp(option, "force")) {
	    doforce = TRUE;
	    locargc--;
	}
    }

    if (locargc > 5 || locargc < 2) goto badusage;

    option = Lookup(cmd->tx_argv[1 + ((dolist) ? 1 : 0)], cmdCellOption);
    if (option < 0) goto badusage;

    if ((locargc > 3) && (option != IDX_RENAME) && (option != IDX_DELETE) &&
		(option != IDX_DEREFERENCE) && (option != IDX_READWRITE) &&
		(option != IDX_PROPERTY) && (option != IDX_FILEPATH) &&
		(option != IDX_ORIENTATION) && (option != IDX_TIMESTAMP))
	goto badusage;

    if ((locargc > 4) && (option != IDX_PROPERTY))
	goto badusage;

    if (locargc >= 3) {
	switch (option) {
	    case IDX_ALLCELLS:
	    case IDX_TOPCELLS:
	    case IDX_IN_WINDOW:
		goto badusage;
		break;
	    default:
		cellname = cmd->tx_argv[2 + ((dolist) ? 1 : 0)];
	}
    }

    if (is_cellname)
    {
	/* These functions only work with cell uses (instances) */
	switch (option) {
	    case IDX_LOCK:
	    case IDX_UNLOCK:
		TxError("Cell definitions cannot be locked.  Use \"instance\"?\n");
		TxError("  or do you mean \"cellname writeable\"?\n");
		return;
	    case IDX_ORIENTATION:
		TxError("Cell definitions do not have orientations."
			"  Use \"instance\"?\n");
		return;
	    case IDX_ABUTMENT:
		TxError("Use \"property get FIXED_BBOX\" to get the cell "
			"abutment box.\n");
		return;
	}
    }
    else
    {
	/* These functions only work with cell definitions */
	switch (option) {
	    case IDX_TOPCELLS:
		TxError("Instances do not have a top level.  Use \"cellname\"?\n");
		return;
	    case IDX_IN_WINDOW: case IDX_READWRITE: case IDX_FLAGS:
	    case IDX_PROPERTY: case IDX_FILEPATH: case IDX_MODIFIED:
	    case IDX_DEREFERENCE: case IDX_TIMESTAMP:
		TxError("Function unimplemented for instances.\n");
		return;
	    case IDX_DELETE:
		TxError("Function unimplemented for instances.  Use \"delete\"\n");
		return;
	    case IDX_CREATE:
		TxError("Function unimplemented for instances.  Use \"getcell\"\n");
		return;
	    case IDX_RENAME:
		TxError("Function unimplemented for instances.  Use \"identify\"\n");
		return;
	}
    }

    switch (option) {
	case IDX_ALLCELLS:
	    (*func)(NULL, ALLCELLS, dolist);
	    break;
	case IDX_TOPCELLS:
	    (*func)(NULL, TOPCELLS, dolist);
	    break;
	case IDX_IN_WINDOW:
	    DBTopPrint(w, dolist);
	    break;
	case IDX_SELF:
	case IDX_EXISTS:
	    (*func)(cellname, SELF, dolist);
	    break;
	case IDX_CELLDEF:
	    (*func)(cellname, ((is_cellname == FALSE) ? OTHER : SELF), dolist);
	    break;
	case IDX_INSTANCE:
	    (*func)(cellname, ((is_cellname == FALSE) ? SELF : OTHER), dolist);
	    break;
	case IDX_CHILDREN:
	    (*func)(cellname, CHILDREN, dolist);
	    break;
	case IDX_CHILDINST:
	    (*func)(cellname, ((is_cellname == FALSE) ? CHILDREN : CHILDINST), dolist);
	    break;
	case IDX_PARENTS:
	    (*func)(cellname, PARENTS, dolist);
	    break;
	case IDX_MODIFIED:
	    (*func)(cellname, MODIFIED, dolist);
	    break;
	case IDX_PROPERTY:
	    if (cellname == NULL)
		cellDef = EditRootDef;
	    else
		cellDef = DBCellLookDef(cellname);

	    if (cellDef == (CellDef *) NULL)
		TxError("Unknown cell %s\n", cellname);
	    else
		CmdDoProperty(cellDef, cmd, 3 + ((dolist) ? 1 : 0));
	    break;

	case IDX_DELETE:
	    /* Unload the cell definition and free memory */
	    /* Make sure selections are cleared or they may	*/
	    /* contain references to the deleted cell def.	*/
	    if ((locargc == 4) && !strcmp(cmd->tx_argv[3 + ((dolist) ? 1 : 0)],
			"-noprompt"))
	    {
		SelectClear();
	        DBCellDelete(cellname, TRUE);
	    }
	    else if (locargc == 3)
	    {
		SelectClear();
	        DBCellDelete(cellname, FALSE);
	    }
	    else
		TxError("Delete cell command missing cellname\n");
	    break;

	case IDX_DEREFERENCE:
	    /* Unload the cell definition and re-read with search paths */
	    if (locargc == 3)
	    {
		if (cellname == NULL)
		    cellDef = EditRootDef;
		else
		    cellDef = DBCellLookDef(cellname);

		/* Reload cell with dereferencing */
		if (cellDef == NULL)
		{
		    TxError("No such cell \"%s\"\n", cellname);
		    break;
		}
		cmdFlushCell(cellDef, TRUE);
		SelectClear();
	    }
	    else
		TxError("Dereference cell command missing cellname\n");
	    break;

	case IDX_READWRITE:
	    if (cellname == NULL)
		cellDef = EditRootDef;
	    else
		cellDef = DBCellLookDef(cellname);
	    if (cellDef == (CellDef *) NULL)
	    {
		TxError("Unknown cell %s\n", cellname);
		break;
	    }

	    if (locargc == 3)
	    {
		if (cellDef->cd_flags & CDNOEDIT)
#ifdef MAGIC_WRAPPER
		    if (dolist)
			Tcl_SetResult(magicinterp, "read-only", 0);
		    else
#endif
		    TxPrintf("read-only\n");
		else
#ifdef MAGIC_WRAPPER
		    if (dolist)
			Tcl_SetResult(magicinterp, "writeable", 0);
		    else
#endif
		    TxPrintf("writeable\n");
	    }
	    else if (locargc == 4)
	    {
	    	int subopt = Lookup(cmd->tx_argv[3 + ((dolist) ? 1 : 0)],
					cmdCellnameYesNo);
	    	if (subopt < 0) goto badusage;
		else if (subopt >= 4)
		{
		    /* Check if file is already read-write */
#ifdef FILE_LOCKS
		    if (!(cellDef->cd_flags & CDNOEDIT) && (cellDef->cd_fd != -2))
#else
		    if (!(cellDef->cd_flags & CDNOEDIT))
#endif
			break;

		    /* Make file read-write */
#ifdef FILE_LOCKS
		    if (cellDef->cd_fd < 0)
			DBOpenOnly(cellDef, NULL, TRUE, NULL);

		    if (cellDef->cd_fd == -2)
		    {
			TxError("An advisory lock is held on cell %s.  Cell can now"
				" be made editable but is not writeable.\n",
				cellDef->cd_name);
		    }
#endif
		    if (cellDef->cd_flags & CDNOEDIT)
		    {
			cellDef->cd_flags &= ~CDNOEDIT;
			WindAreaChanged(w, &w->w_screenArea);
			CmdSetWindCaption(EditCellUse, EditRootDef);
		    }
		}
		else	/* "cellname writeable false" */
		{
		    /* Check if file is already read-only */
		    if (cellDef->cd_flags & CDNOEDIT)
			break;

		    /* Make file read-only */

		    cellDef->cd_flags |= CDNOEDIT;

#ifdef FILE_LOCKS
		    /* Release any advisory lock held on this file */

		    if (cellDef->cd_fd >= 0)
		    {
			close(cellDef->cd_fd);
			cellDef->cd_fd = -1;	 /* Set to initial state */
		    }
#endif

		    if (EditCellUse && (EditCellUse->cu_def == cellDef))
			EditCellUse = (CellUse *)NULL;

		    if (EditRootDef == cellDef)
			EditRootDef = (CellDef *)NULL;

		    WindAreaChanged(w, &w->w_screenArea);
		    CmdSetWindCaption(EditCellUse, (CellDef *)NULL);
		}
	    }
	    break;

	case IDX_FILEPATH:
	    if (cellname == NULL)
		cellDef = EditRootDef;
	    else
		cellDef = DBCellLookDef(cellname);
	    if (cellDef == (CellDef *) NULL)
	    {
		TxError("Unknown cell %s\n", cellname);
		break;
	    }
	    if (locargc <= 3)
	    {
		if (cellDef->cd_file == NULL)
		{
#ifdef MAGIC_WRAPPER
		    if (dolist)
			Tcl_SetResult(magicinterp, "default", 0);
		    else
#endif
			TxPrintf("default\n");
		}
		else
		{
		    char *pathend;

		    pathend = strrchr(cellDef->cd_file, '/');
		    if (pathend) *pathend = '\0';
#ifdef MAGIC_WRAPPER
		    if (dolist)
			Tcl_SetResult(magicinterp, cellDef->cd_file, 0);
		    else
#endif
		 	TxPrintf("%s\n", cellDef->cd_file);

		    if (pathend) *pathend = '/';
		}
	    }
	    else if (locargc == 4)
	    {
		char *filepath;

		filepath = cmd->tx_argv[3 + ((dolist) ? 1 : 0)];
		if (!strcmp(filepath, "default"))
		{
		    if (cellDef->cd_file != NULL)
		    {
			freeMagic(cellDef->cd_file);
			cellDef->cd_file = NULL;
		    }
		}
		else
		{
		    char *fullpath;
		    fullpath = (char *)mallocMagic(strlen(filepath) +
			strlen(cellDef->cd_name) + 2);
		    sprintf(fullpath, "%s/%s", filepath, cellDef->cd_name);

		    if (cellDef->cd_file != NULL)
			freeMagic(cellDef->cd_file);

		    cellDef->cd_file = fullpath;
		}
	    }
	    break;

	case IDX_TIMESTAMP:
	    if (cellname == NULL)
		cellDef = EditRootDef;
	    else
		cellDef = DBCellLookDef(cellname);
	    if (cellDef == (CellDef *) NULL)
	    {
		TxError("Unknown cell %s\n", cellname);
		break;
	    }
	    if ((locargc == 3) || (locargc == 2 && cellname == NULL))
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(cellDef->cd_timestamp));
#else
	    	TxPrintf("Timestamp for cell %s = %d\n", cellname, cellDef->cd_timestamp);
#endif
	    }
	    else if (locargc == 4 || (locargc == 3 && cellname == NULL))
	    {
		int timestamp;
		if (StrIsInt(cmd->tx_argv[2 + ((cellname == NULL) ? 0 : 1)]))
		{
		    timestamp = atoi(cmd->tx_argv[2 + ((cellname == NULL) ? 0 : 1)]);
		    if (timestamp != cellDef->cd_timestamp)
		    {
			cellDef->cd_timestamp = timestamp;
			cellDef->cd_flags &= ~CDGETNEWSTAMP | CDFIXEDSTAMP;
		    }
		}
		else
		{
		    TxError("Invalid timestamp \"%s\".\n",
			cmd->tx_argv[2 + ((cellname == NULL) ? 0 : 1)]);
		}
	    }
	    break;

	case IDX_FLAGS:
	    if (cellname == NULL)
		cellDef = EditRootDef;
	    else
		cellDef = DBCellLookDef(cellname);
	    if (cellDef == (CellDef *) NULL)
	    {
		TxError("Unknown cell %s\n", cellname);
		break;
	    }
#ifdef MAGIC_WRAPPER
	    /* All of the flags are included for completeness.  A number
	     * of these flags are set in self-contained routines and will
	     * never appear as the result of the "cellname flags" command.
	     * Mainly for diagnostics (in case a routine is not cleaning
	     * up its own flag settings, for example).
	     */
	    if (cellDef->cd_flags & CDAVAILABLE)
		Tcl_AppendElement(magicinterp, "available");
	    if (cellDef->cd_flags & CDMODIFIED)
		Tcl_AppendElement(magicinterp, "modified");
	    if (cellDef->cd_flags & CDNOEDIT)
		Tcl_AppendElement(magicinterp, "readonly");
	    if (cellDef->cd_flags & CDINTERNAL)
		Tcl_AppendElement(magicinterp, "internal");
	    if (cellDef->cd_flags & CDFIXEDSTAMP)
		Tcl_AppendElement(magicinterp, "fixed-timestamp");
	    if (cellDef->cd_flags & CDGETNEWSTAMP)
		Tcl_AppendElement(magicinterp, "timestamp-pending");
	    if (cellDef->cd_flags & CDSTAMPSCHANGED)
		Tcl_AppendElement(magicinterp, "child-timestamp-changed");
	    if (cellDef->cd_flags & CDFIXEDBBOX)
		Tcl_AppendElement(magicinterp, "fixed-bbox");
	    if (cellDef->cd_flags & CDBOXESCHANGED)
		Tcl_AppendElement(magicinterp, "bbox-changed");
	    if (cellDef->cd_flags & CDPROCESSED)
		Tcl_AppendElement(magicinterp, "bbox-processed");
	    if (cellDef->cd_flags & CDFLATGDS)
		Tcl_AppendElement(magicinterp, "flat-gds");
	    if (cellDef->cd_flags & CDFLATTENED)
		Tcl_AppendElement(magicinterp, "flattened-gds");
	    if (cellDef->cd_flags & CDPROCESSEDGDS)
		Tcl_AppendElement(magicinterp, "processed-gds");
	    if (cellDef->cd_flags & CDVENDORGDS)
		Tcl_AppendElement(magicinterp, "vendor-gds");
	    if (cellDef->cd_flags & CDVISITED)
		Tcl_AppendElement(magicinterp, "visited");
	    if (cellDef->cd_flags & CDDEREFERENCE)
		Tcl_AppendElement(magicinterp, "dereference");
	    if (cellDef->cd_flags & CDNOTFOUND)
		Tcl_AppendElement(magicinterp, "not-found");
#else
	    TxPrintf("Flag settings for cell %s:\n", cellname);
	    TxPrintf("in database: %s\n", (cellDef->cd_flags & CDAVAILABLE) ?
			"true" : "false");
	    TxPrintf("modified: %s\n", (cellDef->cd_flags & CDMODIFIED) ?
			"true" : "false");
	    TxPrintf("readonly: %s\n", (cellDef->cd_flags & CDNOEDIT) ?
			"true" : "false");
	    TxPrintf("vendor: %s\n", (cellDef->cd_flags & CDVENDORGDS) ?
			"true" : "false");
	    TxPrintf("fixed-bbox: %s\n", (cellDef->cd_flags & CDFIXEDBBOX) ?
			"true" : "false");
	    TxPrintf("fixed-timestamp: %s\n", (cellDef->cd_flags & CDFIXEDSTAMP) ?
			"true" : "false");
#endif
	    break;
	case IDX_RENAME:
	    /* Rename the cell and mark as modified.  Do not write to disk. */
	    if ((locargc == 3) && (EditRootDef != NULL))
	    {
		/* Assume cellname is the target name, and the current	*/
		/* cell is the one to have its name changed.		*/
		cellDef = EditRootDef;
		DBCellRename(cellDef->cd_name, cellname, doforce);
		break;
	    }
	    else if (locargc != 4) goto badusage;
	    DBCellRename(cellname, cmd->tx_argv[3 + ((dolist) ? 1 : 0)], doforce);
	    break;
	case IDX_CREATE:
	    newDef = DBCellLookDef(cellname);
	    if (newDef == (CellDef *) NULL)
	    {
		newDef = DBCellNewDef(cellname);
		DBCellSetAvail(newDef);
	    }
	    break;
	case IDX_ORIENTATION:
	    orient = (locargc == 4) ? cmd->tx_argv[3 + ((dolist) ? 1 : 0)] : NULL;
	    if ((cellname != NULL) && (orient != NULL))
	    {
		TxError("Cannot set orientation by name.  Use selection.\n");
		break;
	    }
	    /* FIXME reading the function documentation above DBOrientUse() this method
	     *  is only used to report orientation.  The selection interface should be
	     *  used to change orientation.  Which is conveyed in the TxError() above.
	     * So the 3rd argument here does not do anything.
	     * Maybe the intention here is to display the current orientation, if so
	     *  please remove this comment and the extra argument 'orient' below.
	     */
	    DBOrientUse(cellname, dodef/*, orient*/);
	    break;
	case IDX_ABUTMENT:
	    DBAbutmentUse(cellname, dolist);
	    break;
	case IDX_LOCK:
	    DBLockUse(cellname, TRUE);
	    break;
	case IDX_UNLOCK:
	    DBLockUse(cellname, FALSE);
	    break;
    }
    return;

badusage:
    TxError("Usage: %s [list] children|parents|self|exists|"
		"instances|celldef|delete [name]\n", cmd->tx_argv[0]);
    TxError("or:    %s [list] allcells|topcells|window\n", cmd->tx_argv[0]);
    TxError("or:    %s create name\n", cmd->tx_argv[0]);
    TxError("or:    %s rename name newname [-force]\n", cmd->tx_argv[0]);
    TxError("or:    %s [un]lock [name]\n", cmd->tx_argv[0]);
    TxError("or:    %s writeable [name] [true|false]\n", cmd->tx_argv[0]);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCif --
 *
 * Implement the "cif" command.
 *
 * Usage:
 *	cif option args
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	There are no side effects on the circuit.  Various options
 *	may produce cif files, read cif, or display cif information
 *	on the screen.
 *
 * ----------------------------------------------------------------------------
 */

#ifdef CIF_MODULE

#define ARRAY		 0
#define HIER		 1
#define	AREALABELS	 2
#define COVERAGE	 3
#define CIF_DRC_CHECK	 4
#define HELP		 5
#define IDCELL		 6
#define ISTYLE		 7
#define CIF_LAMBDA	 8
#define CIF_LIMIT	 9
#define OSTYLE		10
#define CIF_PAINT	11
#define PREFIX		12
#define READ		13
#define RESCALE		14
#define CIF_SCALE	15
#define SEE		16
#define STATS		17
#define WARNING		18
#define CIF_WRITE	19
#define CIF_WRITE_FLAT	20
#define POLYGONS	21
#define UNFRACTURE	22

#define CIF_WARN_HELP  CIF_WARN_END	/* undefined by CIF module */

void
CmdCif(
    MagWindow *w,
    TxCommand *cmd)
{
    int option, yesno;
    const char * const *msg;
    char *namep;
    CellDef *rootDef, *paintDef;
    Rect box;
    TileType layer;
    TileTypeBitMask mask;
    FILE *f;
    bool wizardHelp;
    bool flatCif = FALSE;
    bool dolist = FALSE;
    bool doforall = FALSE;
    float curscale;
    int argc = cmd->tx_argc;
    int argshift;
    char **argv = cmd->tx_argv;

    static const char * const cmdCifWarnOptions[] = { "default", "none", "align",
		"limit", "redirect", "help", 0 };
    static const char * const cmdCifYesNo[] = { "no", "yes", 0 };
    static const char * const cmdCifInOut[] = { "input", "output", 0 };
    static const char * const cmdCifOption[] =
    {
	"*array layer		display CIF layer under box (array only)",
	"*hier layer		display CIF layer under box (hier only)",
	"arealabels yes|no	enable/disable us of area label extension",
	"coverage layer		print area coverage of indicated layer",
	"drccheck [yes|no]	mark all cells as needing DRC checking",
	"help		print this help information",
	"idcell yes|no	enable/disable use of cell ID extension",
	"istyle [style]	change style for reading CIF to style",
	"lambda in|out	show microns per lambda for the current style",
	"limit [value]	limit internal grid subdivision by scalefactor value",
	"ostyle [style]	change style for writing CIF to style",
	"paint ciflayer magiclayer [cellname]\n"
	"			generate CIF and paint into cell",
	"prefix [path]	prepend path to cell names in CIF output",
	"read file		read CIF from \"file\" into edit cell",
	"rescale [yes|no]	allow/disallow rescaling of internal grid",
	"scale in|out	show microns per internal units for the current style",
	"see layer		display CIF layer under box",
	"statistics		print out statistics for CIF generator",
	"warning [option]	set warning display options",
	"write file		output CIF for the window's root cell to \"file\"",
	"flat file		output flattened CIF for "
				"the window's root cell to \"file\"",
	"polygon subcells [yes|no]\n"
	"			put non-Manhattan polygons in subcells",
	"unfracture [yes|no]\n"
	"			optimize non-Manhattan geometry",
	NULL
    };

    if (argc == 1)
	option = CIF_WRITE;
    else
    {
	if (!strncmp(argv[1], "list", 4))
	{
	    dolist = TRUE;
	    if (!strncmp(argv[1], "listall", 7))
	    {
		doforall = TRUE;
	    }
	    argv++;
	    argc--;
	}
	option = Lookup(argv[1], cmdCifOption);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid cif option.\n", argv[1]);
	    option = HELP;
	    argc = 2;
	}
    }

    /* Only check for the presence of a window on options requiring one */

    switch (option)
    {
	case HELP: case ISTYLE: case OSTYLE: case PREFIX:
	case AREALABELS: case WARNING: case CIF_LIMIT:
	case POLYGONS:
	   break;
	default:
	    windCheckOnlyWindow(&w, DBWclientID);
	    if (w == (MagWindow *) NULL)
	    {
		TxError("Point to a window first\n");
		return;
	    }
	    rootDef = ((CellUse *) w->w_surfaceID)->cu_def;

	    if (argc == 1)
	    {
		namep = strrchr(rootDef->cd_name, '/');
		if (namep == (char *) NULL)
		    namep = rootDef->cd_name;
		goto outputCIF;
	    }
    }

    switch (option)
    {
	case ARRAY:
	    if (argc == 4) {
	        if (!strncmp(argv[2], "write", 5))
		{
		    if (!strncmp(argv[3], "dis", 3))
			CIFArrayWriteDisable = TRUE;
		    else if (!strncmp(argv[3], "en", 2))
			CIFArrayWriteDisable = FALSE;
		    else
			TxPrintf("Subcell interaction output %s\n",
				(CIFArrayWriteDisable) ? "disabled" : "enabled");
		    return;
		}
		else goto wrongNumArgs;
	    }
	    else if (argc != 3)
	    {
		wrongNumArgs:
		TxError("Wrong arguments in \"cif %s\" command:\n",
		    argv[1]);
		TxError("    :cif %s\n", cmdCifOption[option]);
		TxError("Try \":cif help\" for more help.\n");
		return;
	    }
	    if (!ToolGetBox(&rootDef, &box))
	    {
		TxError("Use the box to select the area in");
		TxError(" which you want to see CIF.\n");
		return;
	    }
	    CIFSeeHierLayer(rootDef, &box, argv[2], TRUE, FALSE);
	    return;

	case HIER:
	    if (argc == 4) {
	        if (!strncmp(argv[2], "write", 5))
		{
		    if (!strncmp(argv[3], "dis", 3))
			CIFHierWriteDisable = TRUE;
		    else if (!strncmp(argv[3], "en", 2))
			CIFHierWriteDisable = FALSE;
		    else
			TxPrintf("Subcell interaction output %s\n",
				(CIFHierWriteDisable) ? "disabled" : "enabled");
		    return;
		}
		else goto wrongNumArgs;
	    }
	    else if (argc != 3) goto wrongNumArgs;
	    if (!ToolGetBox(&rootDef, &box))
	    {
		TxError("Use the box to select the area in");
		TxError(" which you want to see CIF.\n");
		return;
	    }
	    CIFSeeHierLayer(rootDef, &box, argv[2], FALSE, TRUE);
	    return;

	case COVERAGE:
	    if ((argc == 4) && !strcmp(argv[3], "box"))
	    {
		if (!ToolGetBox(&rootDef, &box))
		{
		    TxError("Box requested but no cursor box exists\n");
		    return;
		}
		CIFCoverageLayer(rootDef, &box, argv[2], dolist);
	    }
	    else if (argc == 3)
		CIFCoverageLayer(rootDef, &rootDef->cd_bbox, argv[2], dolist);
	    else
		goto wrongNumArgs;

	    return;

	case CIF_LAMBDA:
	case CIF_SCALE:
	    if (argc != 3) goto wrongNumArgs;
	    yesno = Lookup(argv[2], cmdCifInOut); /* "input" or "output" */
	    if (yesno < 0)
		goto wrongNumArgs;
	    if (yesno == 0)		/* option "input" */
		curscale = CIFGetInputScale(1000);
	    else
		curscale = CIFGetOutputScale(1000);

	    if (option == CIF_LAMBDA)
	    {
		TxPrintf("Warning:  \"cif lambda\" is deprecated;  use "
			"\"cif scale\" instead.  Units are not in lambda.\n");
	    }
#ifdef MAGIC_WRAPPER
	    Tcl_SetObjResult(magicinterp, Tcl_NewDoubleObj((double)curscale));
#else
	    TxPrintf("One %s internal unit is %2.1f microns.\n", (yesno) ?
			"output" : "input", curscale);
#endif
	    return;

	case CIF_LIMIT:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(CIFRescaleLimit));
#else
		TxPrintf("Rescale limit is %d\n", CIFRescaleLimit);
#endif
		return;
	    }
	    else if (argc != 3)
		goto wrongNumArgs;
	    else if (!StrIsInt(argv[2]))
		goto wrongNumArgs;

	    CIFRescaleLimit = atoi(argv[2]);
	    if (CIFRescaleLimit > CIFMAXRESCALE)
	    {
		CIFRescaleLimit = CIFMAXRESCALE;
		TxError("Warning:  rescale limit set at maximum value of %d\n",
			CIFMAXRESCALE);
	    }
	    else if (CIFRescaleLimit < 1)
	    {
		CIFRescaleLimit = 1;
		TxError("Warning:  rescale limit set at minimum value of 1\n");
	    }
	    return;

	case AREALABELS:
	    if (argc > 3) goto wrongNumArgs;
	    if (argc == 3)
	    {
		yesno = Lookup(argv[2], cmdCifYesNo);
		if (yesno < 0)
		    goto wrongNumArgs;
		CIFDoAreaLabels = yesno;
	    }
	    TxPrintf("Output of CIF area labels is now %s\n",
		CIFDoAreaLabels ? "enabled" : "disabled");
	    return;

	case HELP:
	    if ((argc == 3)
		    && (strcmp(argv[2], "wizard") == 0))
		wizardHelp = TRUE;
	    else wizardHelp = FALSE;
	    TxPrintf("CIF commands have the form \":cif option\",");
	    TxPrintf(" where option is one of:\n");
	    for (msg = &(cmdCifOption[0]); *msg != NULL; msg++)
	    {
		if ((**msg == '*') && !wizardHelp) continue;
		TxPrintf("    %s\n", *msg);
	    }
	    TxPrintf("If no option is given, CIF is output for the");
	    TxPrintf(" root cell.\n");
	    return;

	case IDCELL:
	    if (argc > 3) goto wrongNumArgs;
	    if (argc == 3)
	    {
		yesno = Lookup(argv[2], cmdCifYesNo);
		if (yesno < 0)
		    goto wrongNumArgs;
		CIFDoCellIdLabels = yesno;
	    }
	    TxPrintf ("Output of CIF cell ID labels is now %s\n",
		CIFDoCellIdLabels ? "enabled" : "disabled");
	    return;

	case ISTYLE:
	    if (argc == 3)
		CIFSetReadStyle(argv[2]);
	    else if (argc == 2)
		CIFPrintReadStyle(dolist, doforall, !doforall);
	    else goto wrongNumArgs;
	    return;

	case POLYGONS:
	    if (argc == 3)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CIFSubcellPolygons));
#else
		if (CIFSubcellPolygons)
		    TxPrintf("Non-manhattan polygons placed in subcells.\n");
		else
		    TxPrintf("Non-manhattan polygons read as-is.\n");
#endif
		return;
	    }
	    else if (argc != 4)
		goto wrongNumArgs;

	    yesno = Lookup(argv[3], cmdCifYesNo);
	    if (yesno < 0)
		goto wrongNumArgs;
	    CIFSubcellPolygons = yesno;
	    return;

	case UNFRACTURE:
	    if (argc == 3)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CIFUnfracture));
#else
		if (CIFUnfracture)
		    TxPrintf("Non-manhattan geometry optimized.\n");
		else
		    TxPrintf("Non-manhattan geometry not optimized.\n");
#endif
		return;
	    }
	    else if (argc != 4)
		goto wrongNumArgs;

	    yesno = Lookup(argv[3], cmdCifYesNo);
	    if (yesno < 0)
		goto wrongNumArgs;
	    CIFUnfracture = yesno;
	    return;

	case PREFIX:
	    if (argc == 2)
		StrDup (&CIFPathPrefix, NULL);
	    else if (argc == 3)
		StrDup (&CIFPathPrefix, argv[2]);
	    else goto wrongNumArgs;
	    return;

	case OSTYLE:
	    if (argc == 3)
		CIFSetStyle(argv[2]);
	    else if (argc == 2)
		CIFPrintStyle(dolist, doforall, !doforall);
	    else goto wrongNumArgs;
	    return;

	case READ:
	    if (argc != 3) goto wrongNumArgs;
	    f = PaOpen(argv[2], "r", ".cif", Path,
		    (char *) NULL, (char **) NULL);
	    if (f == (FILE *) NULL)
	    {
		TxError("Cannot open %s.cif to read CIF.\n", argv[2]);
		return;
	    }
	    CIFReadFile(f);
	    (void) fclose(f);
	    return;

	case RESCALE:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(CIFRescaleAllow));
#else
		TxPrintf("Internal grid rescaling %sallowed\n",
			(CIFRescaleAllow) ?  "" : "dis");
#endif
		return;
	    }
	    else if (argc != 3)
		goto wrongNumArgs;

	    yesno = Lookup(argv[2], cmdCifYesNo);
	    if (yesno < 0)
		goto wrongNumArgs;
	    CIFRescaleAllow = yesno;
	    if (!CIFRescaleAllow)
		CIFWarningLevel = CIF_WARN_LIMIT;
	    return;

	case CIF_DRC_CHECK:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(!CIFNoDRCCheck));
#else
		TxPrintf("CIF cells are marked as%s requiring DRC checks.\n",
			(CIFNoDRCCheck) ?  " not" : " ");
#endif
		return;
	    }
	    else if (argc != 3)
		goto wrongNumArgs;

	    yesno = Lookup(argv[2], cmdCifYesNo);
	    if (yesno < 0)
		goto wrongNumArgs;
	    CIFNoDRCCheck = !yesno;
	    return;

	case SEE:
	    if (argc != 3) goto wrongNumArgs;
	    if (!ToolGetBox(&rootDef, &box))
	    {
		TxError("Use the box to select the area in");
		TxError(" which you want to see CIF.\n");
		return;
	    }
	    CIFSeeLayer(rootDef, &box, argv[2]);
	    return;

	case CIF_PAINT:
	    if ((argc != 4) && (argc != 5)) goto wrongNumArgs;
	    if (!ToolGetBox(&rootDef, &box))
	    {
		TxError("Use the box to select the area in");
		TxError(" which you want to paint CIF.\n");
		return;
	    }
	    if (argc == 5)
	    {
		paintDef = DBCellLookDef(argv[4]);
		if (paintDef == (CellDef *)NULL)
		{
		    paintDef = DBCellNewDef(argv[4]);
		    DBCellSetAvail(paintDef);
		}
	    }
	    else
		paintDef = rootDef;

	    layer = DBTechNoisyNameType(argv[3]);
	    if (layer >= TT_TECHDEPBASE)
	    {
		CIFPaintLayer(rootDef, &box, argv[2], layer, paintDef);

		/* Refresh the layout drawing */
		TTMaskSetOnlyType(&mask, layer);
		DBWAreaChanged(paintDef, &box, DBW_ALLWINDOWS, &mask);
		DRCCheckThis(paintDef, TT_CHECKPAINT, &box);
	    }
	    return;

	case STATS:
	    CIFPrintStats();
	    return;

	case WARNING:
	    if (argc == 2)
	    {
		TxPrintf("Warning display options: %s\n",
			cmdCifWarnOptions[CIFWarningLevel]);
	    }
	    else
	    {
		int suboption = Lookup(argv[2], cmdCifWarnOptions);
		if (suboption < 0)
		{
		    TxError("\"%s\" isn't a valid cif warning option.\n", argv[2]);
		    suboption = CIF_WARN_HELP;
		}
		if (suboption == CIF_WARN_HELP)
		    TxError("Valid options are: default, align, limit, "
			"redirect, and none\n");
		else
		    CIFWarningLevel = suboption;
		if (suboption == CIF_WARN_REDIRECT)
		{
		    if (argc == 4)
			StrDup(&CIFErrorFilename, argv[3]);
		    else
			StrDup(&CIFErrorFilename, NULL);
		}
	    }
	    return;

	case CIF_WRITE:
	    if (argc != 3) goto wrongNumArgs;
	    namep = argv[2];
	    goto outputCIF;

	case CIF_WRITE_FLAT:
	    if (argc != 3) goto wrongNumArgs;
	    namep = argv[2];
	    flatCif = TRUE;
	    goto outputCIF;
    }

    /* If control gets here, we're going to output CIF into the
     * file given by namep.
     */

    outputCIF:
    f = PaOpen(namep, "w", ".cif", ".", (char *) NULL, (char **) NULL);
    if (f == (FILE *) NULL)
    {
	TxError("Cannot open %s.cif to write CIF\n", namep);
	return;
    }
    if (flatCif == TRUE)
    {
	if (!CIFWriteFlat(rootDef, f))
	{
	    TxError("I/O error in writing file %s.\n", namep);
	    TxError("File may be incompletely written.\n");

	}
    }
    else
    {
	if (!CIFWrite(rootDef, f))
	{
	    TxError("I/O error in writing file %s.\n", namep);
	    TxError("File may be incompletely written.\n");

	}
    }
    (void) fclose(f);
}
#endif

/*
 * ----------------------------------------------------------------------------
 *
 * CmdClockwise --
 *
 * Implement the "clockwise" command.  Rotate the selection and the
 * box clockwise around the point.
 *
 * Usage:
 *	clockwise [degrees] [-origin]	or
 *	rotate [degrees] [-origin]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the edit cell.
 *
 * Notes:
 *	"rotate" has been added as an alias for "clockwise" because the
 *	usual shorthand "clock" conflicts with the built-in Tcl command
 *	in the Tcl version.  "clockwise" is still kept for backward
 *	compatibility but the command must contain at least "clockw" for
 *	the interpreter to uniquely identify it.
 *
 *	The "-origin" option has been added to allow rotating stuff
 *	relative to the origin, instead of the lower-left corner.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdClockwise(
    MagWindow *w,
    TxCommand *cmd)
{
    Transform trans, t2;
    int degrees, locargc;
    Rect rootBox,  bbox;
    CellDef *rootDef;
    bool noAdjust = FALSE;

    locargc = cmd->tx_argc;
    if (!strncmp(cmd->tx_argv[locargc - 1], "-orig", 5))
    {
	noAdjust = TRUE;
	locargc--;
    }

    if (locargc == 1)
	degrees = 90;
    else if (locargc == 2)
    {
	if (!StrIsInt(cmd->tx_argv[1])) goto badusage;
	degrees = atoi(cmd->tx_argv[1]);
    }
    else goto badusage;

    if (!ToolGetEditBox((Rect *)NULL)) return;
    if (degrees < 0) degrees += 360;

    switch (degrees)
    {
	case 90:
	    t2 = Geo90Transform;
	    break;
	case 180:
	    t2 = Geo180Transform;
	    break;
	case 270:
	    t2 = Geo270Transform;
	    break;
	default:
	    TxError("Rotation angle must be 90, 180, or 270 degrees\n");
	    return;
    }


    /* To rotate the selection, first rotate it around the origin
     * then move it so its lower-left corner is at the same place
     * that it used to be.
     */

    if (noAdjust)
    {
	GeoTransRect(&t2, &SelectDef->cd_bbox, &bbox);
	trans = t2;
    }
    else
    {
	GeoTransRect(&t2, &SelectDef->cd_bbox, &bbox);
	GeoTranslateTrans(&t2, SelectDef->cd_bbox.r_xbot - bbox.r_xbot,
		SelectDef->cd_bbox.r_ybot - bbox.r_ybot, &trans);
    }

    SelectTransform(&trans);

    /* Rotate the box, if it exists and is in the same window as the
     * selection.
     */

    if (ToolGetBox(&rootDef, &rootBox) && (rootDef == SelectRootDef))
    {
	Rect newBox;

	GeoTransRect(&trans, &rootBox, &newBox);
	DBWSetBox(rootDef, &newBox);
    }

    return;

    badusage:
    TxError("Usage: %s [degrees]\n", cmd->tx_argv[0]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdContact --
 *
 * Implement the "contact" command.
 *
 * Usage:
 *	contact <type>
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The area in the box is searched for the intersection of all residues
 *	of the contact type <type>.  The intersecting areas are replaced with
 *	the contact type.
 *
 * ----------------------------------------------------------------------------
 */

typedef struct CCS
{
    CellDef *rootDef;
    TileTypeBitMask *rmask;
    TileType tstart;
    Rect area;
    Rect clip;
    LinkedRect *lhead;
} CCStruct;

void
CmdContact(
    MagWindow *w,
    TxCommand *cmd)
{
    TileType type, rtype, ctype;
    TileTypeBitMask *rmask, smask;
    CCStruct ccs;
    Rect area;
    LinkedRect *lr = NULL;
    int cmdContactFunc(Tile *tile, CCStruct *ccs);	/* Forward declaration */
    int cmdContactEraseFunc(Tile *tile, LinkedRect **lr);	/* Forward declaration */

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    if ((cmd->tx_argc != 2) && (cmd->tx_argc != 3))
    {
	TxError("Usage: %s <contact_type>\n", cmd->tx_argv[0]);
	return;
    }
    if (!ToolGetEditBox(&area)) return;

    if (EditCellUse == NULL)
    {
	TxError("The cell in the window is not editable.\n");
	return;
    }

    if (cmd->tx_argc == 3)
    {
	if (!strcmp(cmd->tx_argv[1], "erase"))
	{
	    /* Erase a contact from the area of the box.  This acts	*/
	    /* differently from the "erase" command.  "erase" will 	*/
	    /* remove the contact and leave nothing behind.  "contact	*/
	    /* erase" will remove the contact and leave the residues	*/
	    /* behind.	The main difference is that this action cannot	*/
	    /* be done on a whole area;  the individual contacts need	*/
	    /* to be enumerated and the erase/paint done on each one.	*/

	    type = DBTechNoisyNameType(cmd->tx_argv[2]);

	    if (!DBIsContact(type))
	    {
	    	TxError("Error:  tile type \"%s\" is not a contact.\n",
			cmd->tx_argv[2]);
	    	return;
	    }
	    TTMaskSetOnlyType(&smask, type);

	    /* Add all stacked contact types containing "type" */
	    for (ctype = DBNumUserLayers; ctype < DBNumTypes; ctype++)
	    {
		rmask = DBResidueMask(ctype);
		if (TTMaskHasType(rmask, type))
		    TTMaskSetType(&smask, ctype);
	    }

	    /* Enumerate all tiles inside the box area containing contact "type" */
	    DBSrPaintArea((Tile *) NULL, EditCellUse->cu_def->cd_planes[DBPlane(type)],
			&area, &smask, cmdContactEraseFunc, (ClientData)&lr);

	    rmask = DBResidueMask(type);

	    while (lr != NULL)
	    {
    		GeoClip(&lr->r_r, &area);

		/* Erase contact type "type" and repaint with the residues */
		DBErase(EditCellUse->cu_def, &lr->r_r, type);
		for (rtype = 0; rtype < DBNumUserLayers; rtype++)
	    	    if (TTMaskHasType(rmask, rtype))
			DBPaint(EditCellUse->cu_def, &lr->r_r, rtype);

		freeMagic(lr);
		lr = lr->r_next;
	    }

	    /* Refresh the layout drawing */
	    DBWAreaChanged(EditCellUse->cu_def, &area, DBW_ALLWINDOWS, &smask);
	    DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &area);
	}
	else
	    TxError("Usage: %s erase <contact_type>\n", cmd->tx_argv[0]);
	return;
    }
    
    type = DBTechNoisyNameType(cmd->tx_argv[1]);
    if (type >= 0)
    {
	if (!DBIsContact(type))
	{
	    TxError("Error:  tile type \"%s\" is not a contact.\n",
			cmd->tx_argv[1]);
	    return;
	}

	/* Find the first residue type */
	rmask = DBResidueMask(type);
	for (rtype = 0; rtype < DBNumUserLayers; rtype++)
	    if (TTMaskHasType(rmask, rtype))
		break;

	/* Find all tiles of one residue type */
	ccs.lhead = (LinkedRect *)NULL;
	ccs.rmask = rmask;
	ccs.tstart = rtype;
	ccs.rootDef = EditCellUse->cu_def;
	ccs.clip = area;
	TTMaskSetOnlyType(&smask, rtype);
	DBSrPaintArea((Tile *) NULL, EditCellUse->cu_def->cd_planes[DBPlane(rtype)],
		&area, &smask, cmdContactFunc, (ClientData) &ccs);

	while (ccs.lhead != NULL)
	{
	    TTMaskSetOnlyType(&smask, type);
	    TTMaskAndMask(&smask, &DBActiveLayerBits);
	    DBPaintMask(EditCellUse->cu_def, &ccs.lhead->r_r, &smask);
	    freeMagic(ccs.lhead);
	    ccs.lhead = ccs.lhead->r_next;
	}

	/* Refresh the layout drawing */
	DBWAreaChanged(EditCellUse->cu_def, &area, DBW_ALLWINDOWS, &smask);
	DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &area);
    }
}

/* For each tile in the first residue type, search for tiles of the
 * next residue type, and add the intersection of areas to the linked
 * rectangle list.
 */

int
cmdContactFunc(
    Tile *tile,
    CCStruct *ccs)
{
    TileType stype;
    TileTypeBitMask smask;
    int cmdContactFunc2(Tile *tile, CCStruct *ccs);	/* Forward declaration */

    TiToRect(tile, &ccs->area);
    GeoClip(&ccs->area, &ccs->clip);

    for (stype = ccs->tstart + 1; stype < DBNumUserLayers; stype++)
	if (TTMaskHasType(ccs->rmask, stype))
	    break;

    TTMaskSetOnlyType(&smask, stype);
    DBSrPaintArea((Tile *) NULL, ccs->rootDef->cd_planes[DBPlane(stype)],
		&ccs->area, &smask, cmdContactFunc2, (ClientData)ccs);
    return 0;
}

int
cmdContactFunc2(
    Tile *tile,
    CCStruct *ccs)
{
    LinkedRect *newlr;
    Rect area;
    TiToRect(tile, &area);

    GeoClip(&area, &ccs->area);

    newlr = (LinkedRect *) mallocMagic(sizeof(LinkedRect));
    newlr->r_r = area;
    newlr->r_next = ccs->lhead;
    ccs->lhead = newlr;

    return 0;
}

/* For each contact tile found, attach the area to a LinkedRect structure
 */

int
cmdContactEraseFunc(
    Tile *tile,
    LinkedRect **lr)
{
    LinkedRect *newlr;
    Rect area;

    newlr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));

    newlr->r_type = TiGetType(tile);
    TiToRect(tile, &newlr->r_r);
    newlr->r_next = *lr;
    *lr = newlr;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCopy --
 *
 * Implement the "copy" command.
 *
 * Usage:
 *	copy [direction [amount]]
 *	copy to x y
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is copied.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdCopy(
    MagWindow *w,
    TxCommand *cmd)
{
    Transform t;
    Rect rootBox, newBox;
    Point rootPoint, editPoint;
    CellDef *rootDef;
    int argpos;

    if (cmd->tx_argc > 4)
    {
	badUsage:
	TxError("Usage: %s [direction [amount]]\n", cmd->tx_argv[0]);
	TxError("   or: %s to x y\n", cmd->tx_argv[0]);
	return;
    }

    if (cmd->tx_argc > 1)
    {
	int indx, amountx, amounty;
	int xdelta, ydelta;

	if (!ToolGetEditBox((Rect *)NULL)) return;

	if (strcmp(cmd->tx_argv[1], "to") == 0)
	{
	    if (cmd->tx_argc != 4)
		goto badUsage;
	    editPoint.p_x = cmdParseCoord(w, cmd->tx_argv[2], FALSE, TRUE);
	    editPoint.p_y = cmdParseCoord(w, cmd->tx_argv[3], FALSE, FALSE);
	    GeoTransPoint(&EditToRootTransform, &editPoint, &rootPoint);
	    goto copyToPoint;
	}

	indx = GeoNameToPos(cmd->tx_argv[1], FALSE, FALSE);
	argpos = (indx < 0) ? 1 : 2;

	if (cmd->tx_argc >= 3)
	{
	    switch (indx)
	    {
		case GEO_EAST: case GEO_WEST:
		    amountx = cmdParseCoord(w, cmd->tx_argv[argpos], TRUE, TRUE);
		    amounty = 0;
		    break;
		case GEO_NORTH: case GEO_SOUTH:
		    amountx = 0;
		    amounty = cmdParseCoord(w, cmd->tx_argv[argpos], TRUE, FALSE);
		    break;
		default:
		    amountx = cmdParseCoord(w, cmd->tx_argv[argpos], TRUE, TRUE);
		    amounty = cmdParseCoord(w, cmd->tx_argv[cmd->tx_argc - 1],
				TRUE, FALSE);
		    break;
	    }
	}
	else
	{
	    if (indx < 0) {
		TxError("Improperly defined copy. . . direction needed.\n");
		return;
	    }
	    amountx = cmdParseCoord(w, "1l", TRUE, TRUE);
	    amounty = cmdParseCoord(w, "1l", TRUE, FALSE);
	}

	switch (indx)
	{
	    case GEO_NORTH:
		xdelta = 0;
		ydelta = amounty;
		break;
	    case GEO_SOUTH:
		xdelta = 0;
		ydelta = -amounty;
		break;
	    case GEO_EAST:
		xdelta = amountx;
		ydelta = 0;
		break;
	    case GEO_WEST:
		xdelta = -amountx;
		ydelta = 0;
		break;
	    case GEO_NORTHWEST:
		xdelta = -amountx;
		ydelta = amounty;
		break;
	    case GEO_NORTHEAST:
	    case -2:
		xdelta = amountx;
		ydelta = amounty;
		break;
	    case GEO_SOUTHWEST:
		xdelta = -amountx;
		ydelta = -amounty;
		break;
	    case GEO_SOUTHEAST:
		xdelta = amountx;
		ydelta = -amounty;
		break;
	    case GEO_CENTER:
		xdelta = 0;
		ydelta = 0;
		break;
	    default:
		ASSERT(FALSE, "Bad direction in CmdCopy");
		return;
	}
	GeoTransTranslate(xdelta, ydelta, &GeoIdentityTransform, &t);

	/* Move the box by the same amount as the selection, if the
	 * box exists.
	 */

	if (ToolGetBox(&rootDef, &rootBox) && (rootDef == SelectRootDef))
	{
	    GeoTransRect(&t, &rootBox, &newBox);
	    DBWSetBox(rootDef, &newBox);
	}
    }
    else
    {
	/* Use the displacement between the box lower-left corner and
	 * the point as the transform.
	 */

	MagWindow *window;

	window = ToolGetPoint(&rootPoint, (Rect *) NULL);
	if ((window == NULL) ||
	    (EditRootDef != ((CellUse *) window->w_surfaceID)->cu_def))
	{
	    TxError("\"Copy\" uses the point as the place to put down a\n");
	    TxError("    copy of the selection, but the point doesn't\n");
	    TxError("    point to the edit cell.\n");
	    return;
	}

	/* Recast the command as "copy to x y" so that it no longer
	 * depends on the pointer position, for command logging.
	 */
	GeoTransPoint(&RootToEditTransform, &rootPoint, &editPoint);
	sprintf(cmd->tx_argstring, "copy to %di %di\n", editPoint.p_x,
			editPoint.p_y);
	TxRebuildCommand(cmd);

copyToPoint:
	if (!ToolGetBox(&rootDef, &rootBox) || (rootDef != SelectRootDef))
	{
	    TxError("\"Copy\" uses the box lower-left corner as a place\n");
	    TxError("    to pick up the selection for copying, but the box\n");
	    TxError("    isn't in a window containing the selection.\n");
	    return;
	}
	GeoTransTranslate(rootPoint.p_x - rootBox.r_xbot,
	    rootPoint.p_y - rootBox.r_ybot, &GeoIdentityTransform, &t);
	GeoTransRect(&t, &rootBox, &newBox);
	DBWSetBox(rootDef, &newBox);
    }
    SelectCopy(&t);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCorner --
 *
 * Implement the "corner" command.  Find all paint touching one side
 * of the box, and paint it around two edges of the box in an "L"
 * shape.
 *
 * Usage:
 *	corner firstDirection secondDirection [layers]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The edit cell is modified.
 *
 * ----------------------------------------------------------------------------
 */

/* Data passed between CmdCorner and cmdCornerFunc: */

int cmdCornerDir1;			/* First direction each wire must
					 * be extended.
					 */
int cmdCornerDir2;			/* Second direction each wire must
					 * be extended.
					 */
Rect cmdCornerRootBox;			/* Root coords of box. */
struct cmdCornerArea *cmdCornerList;	/* List of areas to fill. */

typedef struct cifpathlist
{
    TileType pathtype;
    CIFPath *pathhead;
    struct cifpathlist *cpl_next;
} CIFPathList;

typedef struct nmcornerpath
{
    bool hasErr;
    CIFPathList *pathlist;
} NMCornerPath;

void
CmdCorner(
    MagWindow *w,	/* Window in which command was invoked. */
    TxCommand *cmd)	/* Describes the command that was invoked. */
{
    TileTypeBitMask maskBits;
    Rect editBox;
    SearchContext scx;
    extern int cmdCornerFunc(Tile *tile, TreeContext *cxp);
    bool hasErr = FALSE;
    int locargc = cmd->tx_argc;

    extern int cmdBevelFunc(Tile *tile, TreeContext *cxp);
    bool dobevel = FALSE;
    NMCornerPath cmdPathList;

    if (cmd->tx_argc < 3 || cmd->tx_argc > 5)
    {
	TxError("Usage: %s direction1 direction2 [layers]\n",
			cmd->tx_argv[0]);
	return;
    }

    windCheckOnlyWindow(&w, DBWclientID);
    if ( w == (MagWindow *) NULL )
    {
	TxError("Point to a window\n");
	return;
    }

    /* Find and check validity of directions. */

    cmdCornerDir1 = GeoNameToPos(cmd->tx_argv[1], TRUE, TRUE);
    if (cmdCornerDir1 < 0)
	return;
    cmdCornerDir2 = GeoNameToPos(cmd->tx_argv[2], TRUE, TRUE);
    if (cmdCornerDir2 < 0)
	return;
    if ((cmdCornerDir1 == GEO_NORTH) || (cmdCornerDir1 == GEO_SOUTH))
    {
	if ((cmdCornerDir2 == GEO_NORTH) || (cmdCornerDir2 == GEO_SOUTH))
	{
	    TxPrintf("Can't corner-fill %s and then %s.\n",
		    cmd->tx_argv[1], cmd->tx_argv[2]);
	    return;
	}
    }
    else
    {
	if ((cmdCornerDir2 == GEO_EAST) || (cmdCornerDir2 == GEO_WEST))
	{
	    TxPrintf("Can't corner-fill %s and then %s.\n",
		    cmd->tx_argv[1], cmd->tx_argv[2]);
	    return;
	}
    }

    /* Check for "bevel" keyword */

    if (locargc > 3)
    {
	if (!strncmp(cmd->tx_argv[locargc - 1], "bevel", 5))
	{
	    locargc--;
	    dobevel = TRUE;
	}
    }

    /* Figure out which layers to fill. */

    if (locargc < 4)
	maskBits = DBAllButSpaceAndDRCBits;
    else
    {
	if (!CmdParseLayers(cmd->tx_argv[3], &maskBits))
	    return;
    }

    /* Figure out which material to search for and invoke a search
     * procedure to find it.
     */

    if (!ToolGetEditBox(&editBox)) return;
    GeoTransRect(&EditToRootTransform, &editBox, &cmdCornerRootBox);
    scx.scx_area = cmdCornerRootBox;
    switch (cmdCornerDir1)
    {
	case GEO_NORTH:
	    scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
	    scx.scx_area.r_ybot -= 1;
	    break;
	case GEO_SOUTH:
	    scx.scx_area.r_ybot = scx.scx_area.r_ytop - 1;
	    scx.scx_area.r_ytop += 1;
	    break;
	case GEO_EAST:
	    scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
	    scx.scx_area.r_xbot -= 1;
	    break;
	case GEO_WEST:
	    scx.scx_area.r_xbot = scx.scx_area.r_xtop - 1;
	    scx.scx_area.r_xtop += 1;
	    break;
    }
    scx.scx_use = (CellUse *) w->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;

    if (dobevel)
    {
	cmdPathList.hasErr = FALSE;
	cmdPathList.pathlist = NULL;

	(void) DBTreeSrTiles(&scx, &maskBits,
		((DBWclientRec *) w->w_clientData)->dbw_bitmask,
		cmdBevelFunc, (ClientData) &cmdPathList);

	if (cmdPathList.hasErr)
	    TxError("There's not enough room in the box for all the wires.\n");

	while (cmdPathList.pathlist != NULL)
	{
	    /* It is a bit of a hack to use CIFPolyToRects here, but it	*/
	    /* beats rewriting the entire path parsing routine.		*/

	    LinkedRect *rectp;
	    PaintUndoInfo ui;
	    int pNum =  DBPlane(cmdPathList.pathlist->pathtype);
	    PaintResultType  *resultTbl =
			DBStdPaintTbl(cmdPathList.pathlist->pathtype, pNum);
	    Plane *plane = EditRootDef->cd_planes[pNum];

	    ui.pu_def = EditRootDef;
	    ui.pu_pNum = pNum;

	    rectp = CIFPolyToRects(cmdPathList.pathlist->pathhead, plane,
			resultTbl, &ui, FALSE);
	    for (; rectp != NULL; rectp = rectp->r_next)
	    {
		DBPaintPlane(plane, &rectp->r_r, resultTbl, &ui);
		freeMagic((char *)rectp);
	    }
	    CIFFreePath(cmdPathList.pathlist->pathhead);
	    freeMagic((char *)cmdPathList.pathlist);
	    cmdPathList.pathlist = cmdPathList.pathlist->cpl_next;
	}
    }
    else
    {

	cmdCornerList = (struct cmdCornerArea *) NULL;

	(void) DBTreeSrTiles(&scx, &maskBits,
		((DBWclientRec *) w->w_clientData)->dbw_bitmask,
		cmdCornerFunc, (ClientData) &hasErr);

	if (hasErr)
	    TxError("There's not enough room in the box for all the wires.\n");

	/* Now that we've got all the material, scan over the list
	 * painting the material and freeing up the entries on the list.
	 */
	while (cmdCornerList != NULL)
	{
	    DBPaint(EditCellUse->cu_def, &cmdCornerList->cca_area,
			cmdCornerList->cca_type);
	    freeMagic((char *) cmdCornerList);
	    cmdCornerList = cmdCornerList->cca_next;
	}
    }

    SelectClear();
    DBAdjustLabels(EditCellUse->cu_def, &editBox);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editBox);
    DBWAreaChanged(EditCellUse->cu_def, &editBox, DBW_ALLWINDOWS, &maskBits);
    DBReComputeBbox(EditCellUse->cu_def);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdCornerFunc --
 *
 * 	Search procedure called by DBTreeSrTiles from CmdCorner.  Called once
 *	for each tile that crosses the appropriate boundary of the box.
 *	Makes an L-shaped 90 degree turn to extend a wire out of an
 *	adjacent side.
 *
 * Results:
 *	Returns 0 to keep the search alive.
 *
 * Side effects:
 *	Adds paint tiles to the display list.  If there are tiles found
 *	that can't be cornered correctly, the clientData value is set
 *	to TRUE.
 *
 * ----------------------------------------------------------------------------
 */
int
cmdCornerFunc(
    Tile *tile,			/* Tile to fill with. */
    TreeContext *cxp)		/* Describes state of search. */
{
    Rect r1, r2, r3;
    struct cmdCornerArea *cca;
    bool *errPtr = (bool *) cxp->tc_filter->tf_arg;

    /* Get the tile dimensions in root coordinates.  Clip to the box.
     */
    TiToRect(tile, &r1);
    GeoTransRect(&cxp->tc_scx->scx_trans, &r1, &r2);
    GeoClip(&r2, &cmdCornerRootBox);

    /* Generate r2 and r3, the first and second legs of the L-shaped
     * geometry to be painted for this tile.
     */

    r3 = r2;
    switch (cmdCornerDir1)
    {
	case GEO_NORTH:
	    if (cmdCornerDir2 == GEO_EAST)
	    {
		r2.r_ytop = r3.r_ytop = cmdCornerRootBox.r_ytop
			- (r2.r_xbot - cmdCornerRootBox.r_xbot);
		r3.r_xtop = cmdCornerRootBox.r_xtop;
	    }
	    else
	    {
		r2.r_ytop = r3.r_ytop = cmdCornerRootBox.r_ytop
			- (cmdCornerRootBox.r_xtop - r2.r_xtop);
		r3.r_xbot = cmdCornerRootBox.r_xbot;
	    }
	    r3.r_ybot = r3.r_ytop - (r2.r_xtop - r2.r_xbot);
	    if (r3.r_ybot < cmdCornerRootBox.r_ybot)
		*errPtr = TRUE;
	    break;

	case GEO_SOUTH:
	    if (cmdCornerDir2 == GEO_EAST)
	    {
		r2.r_ybot = r3.r_ybot = cmdCornerRootBox.r_ybot
			+ (r2.r_xbot - cmdCornerRootBox.r_xbot);
		r3.r_xtop = cmdCornerRootBox.r_xtop;
	    }
	    else
	    {
		r2.r_ybot = r3.r_ybot = cmdCornerRootBox.r_ybot
			+ (cmdCornerRootBox.r_xtop - r2.r_xtop);
		r3.r_xbot = cmdCornerRootBox.r_xbot;
	    }
	    r3.r_ytop = r3.r_ybot + (r2.r_xtop - r2.r_xbot);
	    if (r3.r_ytop > cmdCornerRootBox.r_ytop)
		*errPtr = TRUE;
	    break;

	case GEO_EAST:
	    if (cmdCornerDir2 == GEO_NORTH)
	    {
		r2.r_xtop = r3.r_xtop = cmdCornerRootBox.r_xtop
			- (r2.r_ybot - cmdCornerRootBox.r_ybot);
		r3.r_ytop = cmdCornerRootBox.r_ytop;
	    }
	    else
	    {
		r2.r_xtop = r3.r_xtop = cmdCornerRootBox.r_xtop
			- (cmdCornerRootBox.r_ytop - r2.r_ytop);
		r3.r_ybot = cmdCornerRootBox.r_ybot;
	    }
	    r3.r_xbot = r3.r_xtop - (r2.r_ytop - r2.r_ybot);
	    if (r3.r_xbot < cmdCornerRootBox.r_xbot)
		*errPtr = TRUE;
	    break;

	case GEO_WEST:
	    if (cmdCornerDir2 == GEO_NORTH)
	    {
		r2.r_xbot = r3.r_xbot = cmdCornerRootBox.r_xbot
			+ (r2.r_ybot - cmdCornerRootBox.r_ybot);
		r3.r_ytop = cmdCornerRootBox.r_ytop;
	    }
	    else
	    {
		r2.r_xbot = r3.r_xbot = cmdCornerRootBox.r_xbot
			+ (cmdCornerRootBox.r_ytop - r2.r_ytop);
		r3.r_ybot = cmdCornerRootBox.r_ybot;
	    }
	    r3.r_xtop = r2.r_xbot + (r2.r_ytop - r2.r_ybot);
	    if (r3.r_xtop > cmdCornerRootBox.r_xtop)
		*errPtr = TRUE;
	    break;
    }

    /* Clip the resulting geometry to the box, translate to edit cell
     * coords, and add to the paint list if non-NULL.
     */

    GeoClip(&r2, &cmdCornerRootBox);
    GeoTransRect(&RootToEditTransform, &r2, &r1);
    if (!GEO_RECTNULL(&r1))
    {
	/* Add this rectangle to the list. */

	cca = (struct cmdCornerArea *)
		mallocMagic(sizeof(struct cmdCornerArea));
	cca->cca_area = r1;
	cca->cca_type = TiGetType(tile);
	cca->cca_next = cmdCornerList;
	cmdCornerList = cca;
    }

    GeoClip(&r3, &cmdCornerRootBox);
    GeoTransRect(&RootToEditTransform, &r3, &r1);
    if (!GEO_RECTNULL(&r1))
    {
	cca = (struct cmdCornerArea *)
		mallocMagic(sizeof(struct cmdCornerArea));
	cca->cca_area = r1;
	cca->cca_type = TiGetType(tile);
	cca->cca_next = cmdCornerList;
	cmdCornerList = cca;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * Convenience function for cmdBevelFunc.  Allocates a CIFPath record.
 *
 * ----------------------------------------------------------------------------
 */

void
AddNewPoint(
    CIFPath **pathhead)
{
    CIFPath *newpoint;

    newpoint = (CIFPath *)mallocMagic(sizeof(CIFPath));
    newpoint->cifp_next = *pathhead;
    *pathhead = newpoint;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdBevelFunc --
 *
 * 	Search procedure called by DBTreeSrTiles from CmdCorner.  Called once
 *	for each tile that crosses the appropriate boundary of the box.
 *	Makes a beveled 90 degree turn to extend a wire out of an
 *	adjacent side.
 *
 * Results:
 *	Returns 0 to keep the search alive.
 *
 * Side effects:
 *	Adds paint tiles to the display list.  If there are tiles found
 *	that can't be cornered correctly, the clientData value is set
 *	to TRUE.
 *
 * ----------------------------------------------------------------------------
 */
int
cmdBevelFunc(
    Tile *tile,			/* Tile to fill with. */
    TreeContext *cxp)		/* Describes state of search. */
{
    Rect r1, r2, r3;
    CIFPathList *cpl;
    CIFPath *pptr, *pathhead = NULL;
    NMCornerPath *pathrecord = (NMCornerPath *) cxp->tc_filter->tf_arg;
    int wirewidth, boxwidth, boxheight;
    TileType pathtype;

    /* Get the tile dimensions in root coordinates.  Clip to the box.
     */
    TiToRect(tile, &r1);
    GeoTransRect(&cxp->tc_scx->scx_trans, &r1, &r2);
    GeoClip(&r2, &cmdCornerRootBox);

    /* Find the Manhattan rectangles like cmdCornerFunc.  Shrink
     * these to a minimum square or rectangle.  Finally, pick the
     * path points off of the two boxes to generate the nonManhattan
     * path.
     */

    r3 = r2;
    switch (cmdCornerDir1)
    {
	case GEO_NORTH:

	    pathtype = TiGetTopType(tile);
	    if (pathtype == TT_SPACE) return 0;
	    wirewidth = r2.r_xtop - r2.r_xbot;
	    if (cmdCornerDir2 == GEO_EAST)
	    {
		r3.r_ytop = cmdCornerRootBox.r_ytop
			- (r2.r_xbot - cmdCornerRootBox.r_xbot);
		r3.r_xtop = r3.r_xbot = cmdCornerRootBox.r_xtop;
		boxwidth = r3.r_xtop - r2.r_xbot;
		boxheight = r3.r_ytop - r3.r_ybot;

		if (boxwidth > boxheight)
		    r3.r_xbot = r2.r_xbot + boxheight;
		r3.r_xbot -= wirewidth;
	    }
	    else
	    {
		r3.r_ytop = cmdCornerRootBox.r_ytop
			- (cmdCornerRootBox.r_xtop - r2.r_xtop);
		r3.r_xbot = r3.r_xtop = cmdCornerRootBox.r_xbot;
		boxwidth = r2.r_xtop - r3.r_xbot;
		boxheight = r3.r_ytop - r3.r_ybot;

		if (boxwidth > boxheight)
		    r3.r_xtop = r2.r_xtop - boxheight;
		r3.r_xtop += wirewidth;
	    }
	    r3.r_ybot = r3.r_ytop - wirewidth;
	    if (r3.r_ybot < cmdCornerRootBox.r_ybot)
		 pathrecord->hasErr = TRUE;

	    if (boxheight > boxwidth)
		r2.r_ytop = r3.r_ytop - boxwidth;
	    else
		r2.r_ytop = r2.r_ybot;
	    r2.r_ytop += wirewidth;

	    /* Create nonManhattan path */
	    if (cmdCornerDir2 == GEO_EAST)
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ybot;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xtop;
		    pathhead->cifp_y = r2.r_ytop - wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xbot + wirewidth;
		    pathhead->cifp_y = r3.r_ybot;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ytop;
	    }
	    else
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ybot;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xbot;
		    pathhead->cifp_y = r2.r_ytop - wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xtop - wirewidth;
	    	    pathhead->cifp_y = r3.r_ybot;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ytop;
	    }
	    break;

	case GEO_SOUTH:
	    pathtype = TiGetBottomType(tile);
	    if (pathtype == TT_SPACE) return 0;
	    wirewidth = r2.r_xtop - r2.r_xbot;
	    if (cmdCornerDir2 == GEO_EAST)
	    {
		r3.r_ybot = cmdCornerRootBox.r_ybot
			+ (r2.r_xbot - cmdCornerRootBox.r_xbot);
		r3.r_xtop = r3.r_xbot = cmdCornerRootBox.r_xtop;
		boxwidth = r3.r_xtop - r2.r_xbot;
		boxheight = r3.r_ytop - r3.r_ybot;

		if (boxwidth > boxheight)
		    r3.r_xbot = r2.r_xbot + boxheight;
		r3.r_xbot -= wirewidth;
	    }
	    else
	    {
		r3.r_ybot = cmdCornerRootBox.r_ybot
			+ (cmdCornerRootBox.r_xtop - r2.r_xtop);
		r3.r_xbot = r3.r_xtop = cmdCornerRootBox.r_xbot;
		boxwidth = r2.r_xtop - r3.r_xbot;
		boxheight = r3.r_ytop - r3.r_ybot;

		if (boxwidth > boxheight)
		    r3.r_xtop = r2.r_xtop - boxheight;
		r3.r_xtop += wirewidth;
	    }
	    r3.r_ytop = r3.r_ybot + wirewidth;
	    if (r3.r_ytop > cmdCornerRootBox.r_ytop)
		pathrecord->hasErr = TRUE;

	    if (boxheight > boxwidth)
		r2.r_ybot = r3.r_ybot + boxwidth;
	    else
		r2.r_ybot = r2.r_ytop;
	    r2.r_ybot -= wirewidth;

	    /* Create nonManhattan path */
	    if (cmdCornerDir2 == GEO_EAST)
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ytop;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xtop;
		    pathhead->cifp_y = r2.r_ybot + wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xbot + wirewidth;
		    pathhead->cifp_y = r3.r_ytop;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ybot;
	    }
	    else
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ytop;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xbot;
		    pathhead->cifp_y = r2.r_ybot + wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xtop - wirewidth;
		    pathhead->cifp_y = r3.r_ytop;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ybot;
	    }
	    break;

	case GEO_EAST:
	    pathtype = TiGetRightType(tile);
	    if (pathtype == TT_SPACE) return 0;
	    wirewidth = r2.r_ytop - r2.r_ybot;
	    if (cmdCornerDir2 == GEO_NORTH)
	    {
		r3.r_xtop = cmdCornerRootBox.r_xtop
			- (r2.r_ybot - cmdCornerRootBox.r_ybot);
		r3.r_ytop = r3.r_ybot = cmdCornerRootBox.r_ytop;
		boxwidth = r3.r_xtop = r3.r_xbot;
		boxheight = r3.r_xtop = r2.r_ybot;

		if (boxheight > boxwidth)
		    r3.r_ybot = r2.r_ybot + boxwidth;
		r3.r_ybot -= wirewidth;
	    }
	    else
	    {
		r3.r_xtop = cmdCornerRootBox.r_xtop
			- (cmdCornerRootBox.r_ytop - r2.r_ytop);
		r3.r_ybot = r3.r_ytop = cmdCornerRootBox.r_ybot;
		boxwidth = r3.r_xtop = r3.r_xbot;
		boxheight = r2.r_xtop = r3.r_ybot;

		if (boxheight > boxwidth)
		    r3.r_ytop = r2.r_ytop - boxwidth;
		r3.r_ytop += wirewidth;
	    }
	    r3.r_xbot = r3.r_xtop - wirewidth;
	    if (r3.r_xbot < cmdCornerRootBox.r_xbot)
		pathrecord->hasErr = TRUE;

	    if (boxwidth > boxheight)
		r2.r_xtop = r3.r_xtop - boxheight;
	    else
		r2.r_xtop = r2.r_xbot;
	    r2.r_xtop += wirewidth;

	    /* Create nonManhattan path */
	    if (cmdCornerDir2 == GEO_NORTH)
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ytop;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xbot;
		    pathhead->cifp_y = r3.r_ybot + wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xtop - wirewidth;
		    pathhead->cifp_y = r2.r_ytop;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ybot;
	    }
	    else
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ybot;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xbot;
		    pathhead->cifp_y = r3.r_ytop - wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xtop - wirewidth;
		    pathhead->cifp_y = r2.r_ybot;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ytop;
	    }
	    break;

	case GEO_WEST:
	    pathtype = TiGetLeftType(tile);
	    if (pathtype == TT_SPACE) return 0;
	    wirewidth = r2.r_ytop - r2.r_ybot;
	    if (cmdCornerDir2 == GEO_NORTH)
	    {
		r3.r_xbot = cmdCornerRootBox.r_xbot
			+ (r2.r_ybot - cmdCornerRootBox.r_ybot);
		r3.r_ytop = r3.r_ybot = cmdCornerRootBox.r_ytop;
		boxwidth = r3.r_xtop = r3.r_xbot;
		boxheight = r3.r_xtop = r2.r_ybot;

		if (boxheight > boxwidth)
		    r3.r_ybot = r2.r_ybot + boxwidth;
		r3.r_ybot -= wirewidth;
	    }
	    else
	    {
		r3.r_xbot = cmdCornerRootBox.r_xbot
			+ (cmdCornerRootBox.r_ytop - r2.r_ytop);
		r3.r_ybot = r3.r_ytop = cmdCornerRootBox.r_ybot;
		boxwidth = r3.r_xtop = r3.r_xbot;
		boxheight = r2.r_xtop = r3.r_ybot;

		if (boxheight > boxwidth)
		    r3.r_ytop = r2.r_ytop - boxwidth;
		r3.r_ytop += wirewidth;
	    }
	    r3.r_xtop = r2.r_xbot + wirewidth;
	    if (r3.r_xtop > cmdCornerRootBox.r_xtop)
		pathrecord->hasErr = TRUE;

	    if (boxwidth > boxheight)
		r2.r_xbot = r3.r_xbot + boxheight;
	    else
		r2.r_xbot = r2.r_xtop;
	    r2.r_xbot -= wirewidth;

	    /* Create nonManhattan path */
	    if (cmdCornerDir2 == GEO_NORTH)
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ytop;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xtop;
		    pathhead->cifp_y = r3.r_ybot + wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xbot + wirewidth;
		    pathhead->cifp_y = r2.r_ytop;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ybot;
	    }
	    else
	    {
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xbot;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ytop;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r2.r_xtop;
		pathhead->cifp_y = r2.r_ybot;
		if (boxheight > boxwidth)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r3.r_xtop;
		    pathhead->cifp_y = r3.r_ytop - wirewidth;
		}
		else if (boxwidth > boxheight)
		{
		    AddNewPoint(&pathhead);
		    pathhead->cifp_x = r2.r_xbot + wirewidth;
		    pathhead->cifp_y = r2.r_ybot;
		}
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xtop;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ybot;
		AddNewPoint(&pathhead);
		pathhead->cifp_x = r3.r_xbot;
		pathhead->cifp_y = r3.r_ytop;
	    }
	    break;
    }

    /* If either rectangle is NULL after clipping, free the point */
    /* memory and return without adding the path.		  */

    GeoClip(&r2, &cmdCornerRootBox);
    GeoClip(&r3, &cmdCornerRootBox);
    if (GEO_RECTNULL(&r2) || GEO_RECTNULL(&r3))
    {
	for (pptr = pathhead; pptr != NULL; pptr = pptr->cifp_next)
	    freeMagic((char *)pptr);
	return 0;
    }

    /* Clip the point geometry to the box, and translate to edit cell
     * coords.
     */

    for (pptr = pathhead; pptr != NULL; pptr = pptr->cifp_next)
    {
	Point p = pptr->cifp_point;
	GeoClipPoint(&p, &cmdCornerRootBox);
	GeoTransPoint(&RootToEditTransform, &p, &pptr->cifp_point);
    }

    /* Create the path and add it to the list. */

    cpl = (CIFPathList *) mallocMagic(sizeof(CIFPathList));
    cpl->pathtype = pathtype;
    cpl->cpl_next = pathrecord->pathlist;
    cpl->pathhead = pathhead;
    pathrecord->pathlist = cpl;

    return 0;	/* Keep the search going */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCrash --
 *
 * Save cells to or recover cells from a crash backup file
 *
 * Usage:
 *	crash save|recover [file]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Loads the database with multiple cells from the crash recovery
 *	file, and removes the crash recovery file.
 *
 * ----------------------------------------------------------------------------
 */
void
CmdCrash(
    MagWindow *w,
    TxCommand *cmd)
{
    int option = -1;
    char *filename = NULL;
    static const char * const cmdCrashOpt[] = {"save", "recover", 0};

    if (cmd->tx_argc > 3)
	TxError("Usage: %s save|recover [filename]\n", cmd->tx_argv[0]);
    else if (cmd->tx_argc >= 2)
    {
	option = Lookup(cmd->tx_argv[1], cmdCrashOpt);
	if (option < 0)
	{
	    TxError("Usage: %s save|recover [filename]\n", cmd->tx_argv[0]);
	    return;
	}
    }
    if (cmd->tx_argc == 3) filename = cmd->tx_argv[2];

    switch(option) {
	case 0:			/* save */
	    DBWriteBackup(filename);
	    break;
	case 1:			/* recover */
	    DBFileRecovery(filename);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdCrosshair --
 *
 * Manipulate the crosshair highlight
 *
 * Usage:
 *	crosshair <x> <y>
 *	crosshair off
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The crosshair is enabled, moved, or disabled.
 * ----------------------------------------------------------------------------
 */
void
CmdCrosshair(
    MagWindow *w,
    TxCommand *cmd)
{
    Point pos;

    if (cmd->tx_argc == 2)
    {
	if (!strcmp(cmd->tx_argv[1], "off"))
	{
	    /* Erase and turn off */
	    pos.p_x = pos.p_y = MINFINITY;
	}
	else
	{
	    TxError("Usage: %s off|x y \n", cmd->tx_argv[0]);
	    return;
	}
    }
    else if (cmd->tx_argc == 3)
    {
	pos.p_x = cmdParseCoord(w, cmd->tx_argv[1], FALSE, TRUE);
	pos.p_y = cmdParseCoord(w, cmd->tx_argv[2], FALSE, FALSE);
    }
    else
    {
	TxError("Usage: %s off|x y \n", cmd->tx_argv[0]);
	return;
    }
    DBWSetCrosshair(w, &pos);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdDelete --
 *
 * Implement the "delete" command.
 *
 * Usage:
 *	delete
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is deleted.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdDelete(
    MagWindow *w,
    TxCommand *cmd)
{
    if (cmd->tx_argc != 1) goto badusage;
    if (!ToolGetEditBox((Rect *)NULL)) return;

    SelectDelete("deleted", TRUE);
    return;

    badusage:
    TxError("Usage: %s\n", cmd->tx_argv[0]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdDown --
 *
 * Implement the "down" command.
 * Use the cell that is currently selected as the edit cell.  If more than
 * one cell is selected, use the point to choose between them. Load the
 * new edit cell into the window containing the point tool.
 *
 * Usage:
 *	down [<instname>]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets EditCellUse.
 *
 * ----------------------------------------------------------------------------
 */

/* The following variable is set by cmdDownEnumFunc to signal that there
 * was at least one cell in the selection.
 */

static bool cmdFoundNewDown;

void
CmdDown(
    MagWindow *w,
    TxCommand *cmd)
{
    CellUse *use = NULL;
    Rect area, pointArea;
    int cmdEditRedisplayFunc(MagWindow *w, Rect *area);		/* External declaration. */
    int cmdDownEnumFunc(CellUse *selUse, CellUse *use, Transform *transform, Rect *area);		/* Forward declaration. */

    if ((w != NULL) && (cmd->tx_argc == 2))
    {
	CellUse *rootUse;
	SearchContext scx;

	rootUse = (CellUse *)w->w_surfaceID;
	bzero(&scx, sizeof(SearchContext));
	DBTreeFindUse(cmd->tx_argv[1], rootUse, &scx);
	use = scx.scx_use;
    }

    if ((use == NULL) && (cmd->tx_argc > 1))
    {
	TxError("Usage: edit\nMaybe you want the \"load\" command\n");
	return;
    }

    /* Record the current edit cell's area for redisplay (now that it's
     * not the edit cell, it will be displayed differently).  Do this
     * only in windows where the edit cell is displayed differently from
     * other cells.
     */

    GeoTransRect(&EditToRootTransform, &(EditCellUse->cu_def->cd_bbox), &area);
    (void) WindSearch(DBWclientID, (ClientData) NULL,
	    (Rect *) NULL, cmdEditRedisplayFunc, (ClientData) &area);

    /* Use the position of the point to select one of the currently-selected
     * cells (if there are more than one).  If worst comes to worst, just
     * select any selected cell.
     */

    (void) ToolGetPoint((Point *) NULL, &pointArea);
    cmdFoundNewDown = FALSE;

    if (use == NULL)
    {
	SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL,
		cmdDownEnumFunc, (ClientData) &pointArea);
    }
    else
    {
	EditCellUse = use;
	EditRootDef = use->cu_def;
	cmdFoundNewDown = TRUE;
    }
    if (!cmdFoundNewDown)
	TxError("You haven't selected a new cell to edit.\n");

    /* Now record the new edit cell's area for redisplay. */

    GeoTransRect(&EditToRootTransform, &(EditCellUse->cu_def->cd_bbox), &area);
    (void) WindSearch(DBWclientID, (ClientData) NULL,
	    (Rect *) NULL, cmdEditRedisplayFunc, (ClientData) &area);
    DBWloadWindow(w, EditCellUse->cu_def->cd_name, DBW_LOAD_IGNORE_TECH);

    if ((cmd->tx_argc == 1) && cmdFoundNewDown)
    {
	/* Recast the command with the instance name for logging */
	sprintf(cmd->tx_argstring, "down %s", EditCellUse->cu_id);
	TxRebuildCommand(cmd);
    }
}

/* Search function to find the new edit cell:  look for a cell use
 * that contains the rectangle passed as argument.  If we find such
 * a use, return 1 to abort the search.  Otherwise, save information
 * about this use anyway:  it'll become the edit cell if nothing
 * better is found.
 */

    /* ARGSUSED */
int
cmdDownEnumFunc(
    CellUse *selUse,		/* Use from selection (not used). */
    CellUse *use,		/* Use from layout that corresponds to
				 * selUse (could be an array!).
				 */
    Transform *transform,	/* Transform from use->cu_def to root coords. */
    Rect *area)			/* We're looking for a use containing this
				 * area, in root coords.
				 */
{
    Rect defArea, useArea;

    /* Save this use as the default next edit cell, regardless of whether
     * or not it overlaps the area we're interested in.
     */

    EditToRootTransform = *transform;
    GeoInvertTrans(transform, &RootToEditTransform);
    EditCellUse = use;
    EditRootDef = SelectRootDef;
    cmdFoundNewDown = TRUE;

    /* See if the bounding box of this use overlaps the area we're
     * interested in.
     */

    GeoTransRect(&RootToEditTransform, area, &defArea);
    GeoTransRect(&use->cu_transform, &defArea, &useArea);
    if (!GEO_OVERLAP(&useArea, &use->cu_bbox)) return 0;
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdDrc --
 *
 * Implement the "drc" command.
 *
 * Usage:
 *	drc option
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Most options have no side effects.  The only major side
 *	effects are to turn continuous DRC on or off, or recheck an
 *	area of a cell.
 *
 * ----------------------------------------------------------------------------
 */

#define FLATCHECK	0
#define DRC_HALO	1
#define SHOWINT		2
#define DRC_STEPSIZE	3
#define CATCHUP		4
#define CHECK		5
#define COUNT		6
#define EUCLIDEAN	7
#define FIND		8
#define DRC_HELP	9
#define DRC_IGNORE	10
#define DRC_OFF		11
#define DRC_ON		12
#define DRC_STATUS	13
#define DRC_STYLE	14
#define PRINTRULES	15
#define RULESTATS	16
#define STATISTICS	17
#define WHY		18

void
CmdDrc(
    MagWindow *w,
    TxCommand *cmd)
{
    static int	drc_nth = 1;
    int		  option, result, radius;
    Rect	  rootArea, area;
    CellUse	* rootUse, *use;
    CellDef	* rootDef;
    Transform	  trans;
    MagWindow	* window;
    const char 	* const *msg;
    bool	wizardHelp;
    bool	incremental;
    bool	doforall = FALSE;
    bool	dolist = FALSE;
    bool	findonly;
    int		drc_start;
    int		count_total;
    LinkedIndex *DRCSaveRules = NULL;
    DRCCountList *dcl;
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    static const char * const cmdDrcOption[] =
    {
	"*flatcheck             check box area by flattening",
	"*halo [d]		limit error checking to areas of d units",
	"*showint radius        show interaction area under box",
	"*stepsize [d]		change DRC step size to d units",
	"catchup                run checker and wait for it to complete",
	"check                  recheck area under box in all cells",
	"count [total]          count error tiles in each cell under box",
	"euclidean on|off	enable/disable Euclidean geometry checking",
	"find [nth]     	locate next (or nth) error in the layout",
	"help                   print this help information",
	"ignore	[<text>|none]   do not report on rules with this text",
	"off                    turn off background checker",
	"on                     reenable background checker",
	"status			report if the drc checker is on or off",
	"style			set the DRC style",
	"printrules [file]      print out design rules in file or on tty",
	"rulestats              print out stats about design rule database",
	"statistics             print out statistics gathered by checker",
	"why                    print out reasons for errors under box",
	NULL
    };

    if (argc < 2)
    {
	TxError("No option given in \":drc\" command.\n");
	option = DRC_HELP;
    }
    else
    {
	if (!strncmp(argv[1], "list", 4))
	{
	    dolist = TRUE;
	    if (!strncmp(argv[1], "listall", 7))
	    {
		doforall = TRUE;
	    }
	    argv++;
	    argc--;
	}

	/* Make sure "list" or "listall" is followed by additional arguments */
	if (argc < 2)
	{
	    TxError("No option given in \":drc\" command.\n");
	    option = DRC_HELP;
	}

	option = Lookup(argv[1], cmdDrcOption);
	if (option < 0)
	{
	    TxError("%s isn't a valid drc option.\n", argv[1]);
	    option = DRC_HELP;
	    argc = 2;
	}
	if ((argc > 2) && (option != PRINTRULES) && (option != FIND)
	    && (option != SHOWINT) && (option != DRC_HELP) && (option != EUCLIDEAN)
	    && (option != DRC_STEPSIZE) && (option != DRC_HALO) && (option != COUNT)
	    && (option != DRC_STYLE) && (option != DRC_IGNORE))
	{
	    badusage:
	    TxError("Wrong arguments in \"drc %s\" command:\n", argv[1]);
	    TxError("    drc %s\n", cmdDrcOption[option]);
	    TxError("Try \"drc help\" for more help.\n");
	    return;
	}
    }
    switch (option)
    {
	case FLATCHECK:
	    window = ToolGetBoxWindow(&rootArea, (int *) NULL);
	    if (window == NULL) return;
	    rootUse = (CellUse *) window->w_surfaceID;
	    DRCFlatCheck(rootUse, &rootArea);
	    break;

	case SHOWINT:
	    if (argc != 3) goto badusage;
	    radius = cmdParseCoord(w, argv[2], TRUE, FALSE);
	    if (radius < 0)
	    {
		TxPrintf("Radius must not be negative\n");
		return;
	    }
	    window = ToolGetBoxWindow(&rootArea, (int *) NULL);
	    if (window == NULL) return;
	    rootUse = (CellUse *) window->w_surfaceID;
	    if (DRCFindInteractions(rootUse->cu_def, &rootArea,
		radius, &area) <= 0)
	    {
		TxPrintf("No interactions in this area for that radius.\n");
		return;
	    }
	    ToolMoveBox(TOOL_BL, &area.r_ll, FALSE, rootUse->cu_def);
	    ToolMoveCorner(TOOL_TR, &area.r_ur, FALSE, rootUse->cu_def);
	    break;

	case DRC_STYLE:
	    if (argc == 2)
		DRCPrintStyle(dolist, doforall, !doforall);
	    else
		DRCSetStyle(argv[2]);
	    break;

	case CATCHUP:
	    DRCCatchUp();
	    break;

	case CHECK:
	    window = ToolGetBoxWindow(&rootArea, (int *) NULL);
	    if (window == NULL) return;
	    rootUse = (CellUse *) window->w_surfaceID;
	    DRCCheck(rootUse, &rootArea);
	    break;

	case COUNT:
	    count_total = -1;
	    if (argc == 3)
		if (!strncmp(argv[2], "total", 5))
		    count_total = 0;

#ifdef MAGIC_WRAPPER
	    if (count_total == -1) lobj = Tcl_NewListObj(0, NULL);
#endif
	    if ((window = w) == NULL)
	    {
		window = ToolGetBoxWindow(&rootArea, (int *) NULL);
		if (window == NULL) return;
	    }
	    else
		rootArea = w->w_surfaceArea;

	    rootUse = (CellUse *) window->w_surfaceID;
	    dcl = DRCCount(rootUse, &rootArea, doforall);
	    while (dcl != NULL)
	    {
		if (count_total >= 0)
		    count_total += dcl->dcl_count;
		else
		{
#ifdef MAGIC_WRAPPER
		    if (dolist)
		    {
			Tcl_Obj *pobj = Tcl_NewListObj(0, NULL);
			Tcl_ListObjAppendElement(magicinterp, pobj,
				Tcl_NewStringObj(dcl->dcl_def->cd_name, -1));
			Tcl_ListObjAppendElement(magicinterp, pobj,
				Tcl_NewIntObj(dcl->dcl_count));
			Tcl_ListObjAppendElement(magicinterp, lobj, pobj);
		    }
		    else
		    {
#endif
		    if (dcl->dcl_count > 1)
			TxPrintf("Cell %s has %d error tiles.\n",
				dcl->dcl_def->cd_name, dcl->dcl_count);
		    else if (dcl->dcl_count == 1)
			TxPrintf("Cell %s has just one error tile.\n",
				dcl->dcl_def->cd_name);
#ifdef MAGIC_WRAPPER
		    }
#endif
		}
		freeMagic((char *)dcl);
		dcl = dcl->dcl_next;
	    }

#ifdef MAGIC_WRAPPER
	    if ((count_total >= 0) || (!dolist))
	    {
		if (dolist)
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(count_total));
		else
		{
		    if ((DRCBackGround != DRC_SET_OFF) && (count_total == -1))
			count_total = 0;
		    if (count_total >= 0)
			TxPrintf("Total DRC errors found: %d\n", count_total);
		}
	    }
	    else if (dolist)
		Tcl_SetObjResult(magicinterp, lobj);

#else
	    if ((DRCBackGround != DRC_SET_OFF) && (count_total == -1))
		count_total = 0;
	    if (count_total >= 0)
		TxPrintf("Total DRC errors found: %d\n", count_total);
#endif
	    break;

	case EUCLIDEAN:
	    if (argc == 2)
	    {
		TxPrintf("DRC measurements are %s.\n", DRCEuclidean ?
			"Euclidean" : "Manhattan");
	    }
	    else
	    {
		if (!strcmp(argv[2], "on"))
		    DRCEuclidean = TRUE;
		else
		    DRCEuclidean = FALSE;
	    }
	    break;

	case FIND:
	    if ((window = w) == NULL)
	    {
		window = ToolGetBoxWindow(&rootArea, (int *) NULL);
		if (window == NULL) return;
	    }
	    else
		rootArea = w->w_surfaceArea;
	    if (argc > 3) goto badusage;

	    rootUse = (CellUse *) window->w_surfaceID;
	    rootDef = rootUse->cu_def;

	    incremental = FALSE;
	    findonly = FALSE;
	    if ((argc == 3) && StrIsInt(argv[2]))
	    {
		drc_nth = atoi(argv[2]);
		if (drc_nth <= 0) drc_nth = 1;
	    }
	    else
	    {
	        incremental = TRUE;
		drc_nth++;

		/* If 3rd argument is a string, then look only for
		 * rules containing the string text.
		 */
		if (argc == 3)
		{
		    int i;

		    findonly = TRUE;
		    DRCSaveRules = DRCIgnoreRules;
		    DRCIgnoreRules = NULL;
		    for (i = 1; i < DRCCurStyle->DRCWhySize; i++)
		    {
	 		if (strstr(DRCCurStyle->DRCWhyList[i], argv[2]) != NULL)
			{
			    LinkedIndex *newli;

			    newli = (LinkedIndex *)mallocMagic(sizeof(LinkedIndex));
			    newli->li_index = i;
			    newli->li_next = DRCIgnoreRules;
			    DRCIgnoreRules = newli;
			}
		    }
		}
	    }

	    drc_start = -1;
	    while (TRUE)
	    {
		/* May need to loop multiple times if some error types
		 * are being ignored.
		 */

		result = DRCFind(rootUse, &rootArea, &area, drc_nth);
		if (incremental && (result < 0))
		{
		    /* 2nd pass, if we exceeded the total number of errors */
		    drc_nth = 1;
		    result = DRCFind(rootUse, &rootArea, &area, drc_nth);
		}

		/* If looping on an incremental DRC error search, and errors
		 * are being ignored, and there are no non-ignored errors in
		 * the search area, then quit if we have looped back around
		 * to the starting point.
		 */
		if ((drc_start > 0) && (result == drc_start))
		    break;

		if (result > 0)
		{
		    ToolMoveBox(TOOL_BL, &area.r_ll, FALSE, rootDef);
		    ToolMoveCorner(TOOL_TR, &area.r_ur, FALSE, rootDef);
#ifdef MAGIC_WRAPPER
		    if (!dolist)
#endif
		    TxPrintf("Error area #%d:\n", result);
		    if (DRCWhy(dolist, rootUse, &area, findonly)) break;
		    drc_nth++;
		}
		else if (result < 0)
		{
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(-1));
		    if (!dolist)
#endif
		    TxPrintf("There aren't that many errors");
		    break;
		}
		else
		{
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(0));
		    if (!dolist)
#endif
		    TxPrintf("There are no errors in %s.\n", rootDef->cd_name);
		    break;
		}

		if (drc_start < 0)
		    drc_start = result;
	    }
	    if (findonly)
	    {
		/* Delete temporary rules */
		while (DRCIgnoreRules != NULL)
		{
		    freeMagic(DRCIgnoreRules);
		    DRCIgnoreRules = DRCIgnoreRules->li_next;
		}
		/* Replace temporary set of rules */
		DRCIgnoreRules = DRCSaveRules;
	    }
	    break;

	case DRC_HALO:
	    if (argc == 3)
	    {
		DRCTechHalo = cmdScaleCoord(w, argv[2], FALSE, TRUE, 1);
		if (DRCTechHalo > DRCCurStyle->DRCTechHalo)
		    DRCTechHalo = DRCCurStyle->DRCTechHalo;

		if (DRCTechHalo < DRCCurStyle->DRCTechHalo)
		    TxPrintf("Warning: rulechecking limited to halo of "
				"%d internal units (%d DRC units).\n",
				DRCTechHalo,
				DRCTechHalo * DRCCurStyle->DRCScaleFactorN
				/ DRCCurStyle->DRCScaleFactorD);
		else
		    TxPrintf("DRC checks all rules (halo of %d internal units, or"
				" %d DRC units)\n",
				DRCTechHalo,
				DRCTechHalo * DRCCurStyle->DRCScaleFactorN
				/ DRCCurStyle->DRCScaleFactorD);
	    }
	    else
	    {
		TxPrintf("DRC halo is %d internal units (%d DRC units)\n",
				DRCTechHalo,
				DRCTechHalo * DRCCurStyle->DRCScaleFactorN
				/ DRCCurStyle->DRCScaleFactorD);
		if (DRCTechHalo != DRCCurStyle->DRCTechHalo)
		    TxPrintf("Maximum rule distance is %d internal units "
				"(%d DRC units)\n",
				DRCCurStyle->DRCTechHalo,
				DRCCurStyle->DRCTechHalo * DRCCurStyle->DRCScaleFactorN
				/ DRCCurStyle->DRCScaleFactorD);
	    }
	    break;

	case DRC_HELP:
	    if ((argc == 3)
		    && (strcmp(argv[2], "wizard") == 0))
		wizardHelp = TRUE;
	    else wizardHelp = FALSE;
#ifdef MAGIC_WRAPPER
	    TxPrintf("DRC commands have the form \":drc [list|listall] option\",");
#else
	    TxPrintf("DRC commands have the form \":drc option\",");
#endif
	    TxPrintf(" where option is one of:\n");
	    for (msg = &(cmdDrcOption[0]); *msg != NULL; msg++)
	    {
		if ((**msg == '*') && !wizardHelp) continue;
		TxPrintf("    %s\n", *msg);
	    }
	    break;

	case DRC_IGNORE:
	    /* Ignore rules containing the given text, unless the text is "none",
	     * in which case clear the list of rules to ignore.
	     */
	    if (argc == 2)
	    {
		LinkedIndex *li;

		/* Print or list rules being ignored */

		li = DRCIgnoreRules;
		while (li != NULL)
		{
		    TxPrintf("%s\n", DRCCurStyle->DRCWhyList[li->li_index]);
		    li = li->li_next;
		}
		if (DRCIgnoreRules == NULL) TxPrintf("(none)\n");
		break;
	    }
	    if (argc != 3) goto badusage;
	    if (!strcasecmp(argv[2], "none"))
	    {
		while (DRCIgnoreRules != NULL)
		{
		    freeMagic(DRCIgnoreRules);
		    DRCIgnoreRules = DRCIgnoreRules->li_next;
		}
	    }
	    else
	    {
		int i;
		LinkedIndex *newli;
		for (i = 1; i < DRCCurStyle->DRCWhySize; i++)
		{
		    if (strstr(DRCCurStyle->DRCWhyList[i], argv[2]) != NULL)
		    {
			newli = (LinkedIndex *)mallocMagic(sizeof(LinkedIndex));
			newli->li_index = i;
			newli->li_next = DRCIgnoreRules;
			DRCIgnoreRules = newli;
		    }
		}
	    }
	    break;

	case DRC_OFF:
#ifdef MAGIC_WRAPPER
	    /* Turn on long enough to force a break, then turn off */
	    DRCBackGround = DRC_SET_ON;
	    DRCStatus = DRC_IN_PROGRESS;
	    DRCBreak();
#endif
	    DRCBackGround = DRC_SET_OFF;
#ifdef MAGIC_WRAPPER
	    if (TxInputRedirect != TX_INPUT_REDIRECTED)
#endif
		TxSetPrompt('%');		/* Return prompt to "normal" */
	    break;

	case DRC_ON:
	    /* Don't allow overriding of DRC_NOT_SET */
	    if (DRCBackGround == DRC_SET_OFF) DRCBackGround = DRC_SET_ON;
	    break;

	case DRC_STATUS:
#ifdef MAGIC_WRAPPER
	    Tcl_SetObjResult(magicinterp,
			Tcl_NewBooleanObj((DRCBackGround == DRC_SET_OFF) ?
			FALSE : TRUE));
#else
	    TxPrintf("DRC checker is %s.\n", (DRCBackGround == DRC_SET_OFF) ?
			"off" : "on");
#endif
	    break;

	case PRINTRULES:
	    if (argc > 3) goto badusage;
	    if (argc < 3)
	    {
		DRCPrintRulesTable (stdout);
	    }
	    else
	    {
	        FILE *fp = fopen (argv[2], "w");
	        if (fp == NULL)
		{
		    TxError("Cannot write file %s\n", argv[2]);
		    return;
		}
		DRCPrintRulesTable (fp);
		fclose(fp);
	    }
	    break;

	case RULESTATS:
	    DRCTechRuleStats();
	    break;

	case STATISTICS:
	    DRCPrintStats();
	    break;

	case DRC_STEPSIZE:
	    if (argc == 3)
	    {
		DRCStepSize = cmdScaleCoord(w, argv[2], FALSE, TRUE, 1);
		TxPrintf("DRC step size is now %d internal units (%d DRC units))\n",
				DRCStepSize,
				DRCStepSize * DRCCurStyle->DRCScaleFactorN
				/ DRCCurStyle->DRCScaleFactorD);
	    }
	    else
	    {
		TxPrintf("DRC step size is %d internal units (%d DRC units)\n",
			DRCStepSize,
			DRCStepSize * DRCCurStyle->DRCScaleFactorN
			/ DRCCurStyle->DRCScaleFactorD);
		if (DRCStepSize != (16 * DRCCurStyle->DRCTechHalo))
		    TxPrintf("Recommended step size is %d internal units"
				" (%d DRC units)\n",
				(16 * DRCCurStyle->DRCTechHalo),
				(16 * DRCCurStyle->DRCTechHalo) *
				DRCCurStyle->DRCScaleFactorN /
				DRCCurStyle->DRCScaleFactorD);
	    }
	    break;

	case WHY:
	    window = ToolGetBoxWindow(&rootArea, (int *) NULL);
	    if (window == NULL) return;
	    rootUse = (CellUse *) window->w_surfaceID;

#ifdef MAGIC_WRAPPER
	    if (doforall)
	       DRCWhyAll(rootUse, &rootArea, NULL);
	    else
#endif
	    if (!DRCWhy(dolist, rootUse, &rootArea, FALSE))
		TxPrintf("No errors found.\n");
	    break;
    }
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdDropPaintCell ---
 *
 *	Callback function used by cmdDropFunc.  Called for each tile found in
 *	the edit cell hierarchy that matches paint that was in the selection.
 *	Paints layers from lMask (clientData) into the subcell containing the
 *	tile, within the area of the tile.
 *
 * Returns:
 *	Always returns zero to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdDropPaintCell(
    Tile *tile,
    TreeContext *cxp)
{
    CellDef *cellDef = cxp->tc_scx->scx_use->cu_def;
    TileTypeBitMask *lMask = (TileTypeBitMask *)cxp->tc_filter->tf_arg;
    int pNum;
    TileType type;
    Rect area;

    if (SplitSide(tile))
        type = SplitRightType(tile);
    else
        type = SplitLeftType(tile);
    pNum = DBPlane(type);

    TiToRect(tile, &area);

    /* Clip to search area */
    GEOCLIP(&area, &cxp->tc_scx->scx_area);

    DBPaintMask(cellDef, &area, lMask);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdDropFunc ---
 *
 *	Callback function used by CmdDrop.  Called for each tile found in the
 *	selection.
 *
 * Returns:
 *	Always returns zero to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdDropFunc(
    Tile *tile,
    ClientData clientData)
{
    TileTypeBitMask tMask, *lMask = (TileTypeBitMask *)clientData;
    SearchContext scx;
    TileType type;

    TiToRect(tile, &scx.scx_area);
    scx.scx_use = EditCellUse;
    scx.scx_trans = GeoIdentityTransform;

    if (SplitSide(tile))
	type = SplitRightType(tile);
    else
	type = SplitLeftType(tile);
    TTMaskSetOnlyType(&tMask, type);

    DBTreeSrTiles(&scx, &tMask, 0, cmdDropPaintCell, (ClientData)lMask);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdDropPaintFunc --
 *
 *	Callback function to SelEnumPaint() from CmdDrop().  Sets a bit in
 *	the type mask passed as clientData for the type of the tile found
 *	in the selection.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdDropPaintFunc(
    Rect *rect,                 /* Not used. */
    TileType type,              /* Type of this piece of paint. */
    TileTypeBitMask *mask)      /* Place to OR in type's bit. */
{
    if (type & TT_DIAGONAL)
	type = (type & TT_SIDE) ? (type & TT_RIGHTMASK) >> 14 :
		(type & TT_LEFTMASK);
    TTMaskSetType(mask, type);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdDrop --
 *
 *	Implement the ":drop" command.
 *
 * Usage:
 *	drop <layers>
 *
 * where <layers> is a list of paint layers.  The command requires an
 * existing selection, with the expectation that the selection contains
 * paint material that exists in subcells of the current edit cell.  The
 * "drop" command will copy the types in <layers> into every subcell in
 * the hierarchy of the current edit cell that contains selected material.
 *
 * The purpose of this command is to deal with issues arising from layers
 * in a vendor GDS file that must be in the same cell as a device layer
 * in order for the device to be extracted properly, but instead is placed
 * in a cell further up in the hierarchy.  A typical example is the deep
 * nwell layer, which isolates the transistor bulk terminal from the
 * substrate.  Without the deep nwell layer in the same cell as the
 * transistor, the transistor will be extracted with the bulk terminal
 * connected to the substrate.
 *
 * Note that the act of copying material down into a subcell means that
 * material is then present in all instances of the subcell.  There is
 * no implementation as yet to handle the case where some instances of the
 * same subcell require <layers> and some don't, which would necessitate
 * splitting the subcell into (at least) two different subcells, one
 * containing <layers> and one not.  This needs to be implemented.  A
 * possible implementation would be:  1st pass:  Find all subcells that
 * will be modified.  Make a list of the instances that will be modified.
 * After the first pass, check if the list of instances contains all
 * instances of the cell def.  If so, then just modify the cell def.  If
 * not, then make a copy the cell def and split off the instances that
 * get the modification to point to that cell def, then modify that cell
 * def.  2nd pass:  Run the "drop" command as before.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Subcells are modified by adding paint.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdDrop(
    MagWindow *w,
    TxCommand *cmd)
{
    TileType i;
    TileTypeBitMask lMask, tMask;
    CellUse *checkUse;
    int pNum;
    Rect editBox;

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: %s layers\n", cmd->tx_argv[0]);
	return;
    }

    if (!ToolGetEditBox(&editBox)) return;
    if (!CmdParseLayers(cmd->tx_argv[1], &lMask))
	return;

    checkUse = NULL;
    if (EditRootDef == SelectRootDef)
	checkUse = EditCellUse;
    if (checkUse == NULL)
    {
	if (w == (MagWindow *)NULL)
	    windCheckOnlyWindow(&w, DBWclientID);
	if (w) checkUse = (CellUse *)w->w_surfaceID;
    }
    if ((checkUse == NULL) || (checkUse->cu_def != SelectRootDef))
    {
	TxError("The selection does not match the edit cell.\n");
	return;
    }

    TTMaskZero(&tMask);
    SelEnumPaint(&DBAllButSpaceAndDRCBits, FALSE, (bool *)NULL,
	    cmdDropPaintFunc, (ClientData)&tMask);

    if (TTMaskIsZero(&tMask)) return;	/* Nothing selected */
	
    for (i = TT_SELECTBASE; i < DBNumUserLayers; i++)
    {
	if (TTMaskHasType(&tMask, i))
	{
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		if (TTMaskHasType(&DBPlaneTypes[pNum], i))
		    DBSrPaintArea((Tile *)NULL, SelectDef->cd_planes[pNum],
			    &SelectUse->cu_bbox, &tMask,
			    cmdDropFunc, (ClientData)&lMask);
	}
    }

    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editBox);
    DBWAreaChanged(EditCellUse->cu_def, &editBox, DBW_ALLWINDOWS, &tMask);
    DBReComputeBbox(EditCellUse->cu_def);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdDump --
 *
 *	Implement the ":dump" command.
 *
 * Usage:
 *	dump cellName [child refPointChild] [parent refPointParent]
 *
 * where the refPoints are either a label name, e.g., SOCKET_A, or an x-y
 * pair of integers, e.g., 100 200.  The words "child" and "parent" are
 * keywords, and may be abbreviated.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Copies the contents of a given cell into the edit cell,
 *	so that refPointChild in the child cell (or the lower-left
 *	corner of its bounding box) ends up at location refPointParent
 *	in the edit cell (or the location of the box tool's lower-left).
 *
 * ----------------------------------------------------------------------------
 */

void
CmdDump(
    MagWindow *w,		/* Window in which command was invoked. */
    TxCommand *cmd)		/* Describes command arguments. */
{
    SearchContext scx;
    CellUse dummy;

    if (cmdDumpParseArgs("dump", w, cmd, &dummy, &scx))
	SelectDump(&scx);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdDumpParseArgs --
 *
 * Do the real work of the "dump" and "getcell" commands, by reading
 * in the child cell to be used and figuring out the transform implied
 * by the reference point arguments to the command.
 *
 * Results:
 *      TRUE on success; FALSE if arguments were missing or
 *	incorrect, or if the cell couldn't be found.
 *
 * Side effects:
 *	Fills in *dummy so that dummy->cu_def points to the cell
 *	specified by name in cmd->tx_argv[] (see CmdDump() for a
 *	description of the syntax of these args).  Also fills in
 *	*scx so scx_use is dummy, scx_trans is the desired transform
 *	from dummy->cu_def back to root coordinates, and scx_area
 *	is the bounding box of dummy->cu_def.  (Scx is set up
 *	directly for a call to SelectDump()).
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
bool
cmdDumpParseArgs(
    char *cmdName,	/* Either "dump" or "getcell" */
    MagWindow *w,	/* Window in which command was invoked (UNUSED) */
    TxCommand *cmd,	/* Arguments to command */
    CellUse *dummy,	/* Filled in to point to cell mentioned in command */
    SearchContext *scx)	/* Filled in with the transform from the child cell's
			 * def to ROOT coordinates, the bounding box of the
			 * child cell in child cell coordinates, and with
			 * scx_use = dummy, where dummy->cu_def is the child
			 * cell itself.
			 */
{
    Point childPoint, editPoint, rootPoint;
    CellDef *def, *rootDef, *editDef;
    bool hasChild, hasRoot, hasTrans;
    Rect rootBox, bbox;
    const Transform *tx_cell;
    Transform trans_cell;
    char **av;
    char *cellnameptr, *fullpathname;
    int ac, clen;

    if (cmd->tx_argc < 2)
    {
	TxError("Missing cell name in \"%s\" command.\n", cmdName);
	goto usage;
    }

    if (EditCellUse == NULL)
    {
	TxError("No cell def being edited; cannot place cell use!\n");
	return FALSE;
    }

    /* cellnameptr should not include any path components */
    if ((cellnameptr = strrchr(cmd->tx_argv[1], '/')) != NULL)
    {
	cellnameptr++;
	fullpathname = (char *)mallocMagic(strlen(cmd->tx_argv[1]) + 2);
	strcpy(fullpathname, cmd->tx_argv[1]);

    	/* If the name still has ".mag" attached, then strip it. */
    	clen = strlen(fullpathname);
    	if ((clen > 4) && !strcmp(fullpathname + clen - 4, ".mag"))
	    *(fullpathname + clen - 4) = '\0';
    }
    else
    {
	cellnameptr = cmd->tx_argv[1];
	fullpathname = NULL;
    }

    /* If the name still has ".mag" attached, then strip it. */
    clen = strlen(cellnameptr);
    if ((clen > 4) && !strcmp(cellnameptr + clen - 4, ".mag"))
	*(cellnameptr + clen - 4) = '\0';

    /* Check for illegal characters in the cellname */
    if (CmdIllegalChars(cellnameptr, "", "Cell name"))
    {
	if (fullpathname) freeMagic(fullpathname);
	return (FALSE);
    }

    def = DBCellLookDef(cellnameptr);
    if (def == (CellDef *) NULL)
	def = DBCellNewDef(cellnameptr);

    if (fullpathname != NULL)
    {
	/* Check if def already exists.  If it points to a	*/
	/* different file, then force a rename of the cell and	*/
	/* flag a warning.					*/

	if (def->cd_file != NULL)
	{
	    /* Note: may want processing to see if absolute paths match */
	    if (strcmp(def->cd_file, fullpathname))
	    {
		char uniqchar;
		char *newcellname = (char *)mallocMagic(strlen(cellnameptr) + 3);
		TxError("Warning:  Cell file path mismatch.  Existing cell has"
			" path \"%s\", while %s path is \"%s\".\n",
			def->cd_file, cmdName, fullpathname);
		uniqchar = 'a';
		while (def != NULL)
		{
		    sprintf(newcellname, "%s_%c", cellnameptr, uniqchar);
		    def = DBCellLookDef(newcellname);
		    uniqchar++;
		}
		TxError("Renaming cell to \"%s\" to avoid conflict.\n", newcellname);
		def = DBCellNewDef(newcellname);
		StrDup(&def->cd_file, fullpathname);
		freeMagic(newcellname);
	    }
	}
	else
	    StrDup(&def->cd_file, fullpathname);
	freeMagic(fullpathname);
    }

    editDef = EditCellUse->cu_def;

    /*
     * The following line of code is a bit of a hack.  It's needed to
     * force DBCellRead to print an error message if it can't find the
     * cell.  Otherwise, if the cell wasn't found the last time it was
     * looked for then no new error message will be printed.
     */
    def->cd_flags &= ~CDNOTFOUND;
    if (!DBCellRead(def, TRUE, TRUE, NULL))
	return (FALSE);
    DBReComputeBbox(def);
    dummy->cu_def = def;
    dummy->cu_transform = GeoIdentityTransform;
    dummy->cu_expandMask = CU_DESCEND_SPECIAL;
    if (DBIsAncestor(def, EditCellUse->cu_def))
    {
	TxError("The edit cell is already a descendant of \"%s\",\n",
	    cmd->tx_argv[1]);
	TxError("    which means that you're trying to create a circular\n");
	TxError("    structure.  This isn't legal.\n");
	return FALSE;
    }

    /*
     * Get def's bounding box.  If def is an abstract view with CDFIXEDBBOX
     * set, then used the property FIXED_BBOX to set the bounding box.
     */
    bbox = def->cd_bbox;
    if (def->cd_flags & CDFIXEDBBOX)
    {
	char *propvalue;
	bool found;

	propvalue = (char *)DBPropGet(def, "FIXED_BBOX", &found);
	if (found)
	{
	    if (sscanf(propvalue, "%d %d %d %d", &bbox.r_xbot, &bbox.r_ybot,
		    &bbox.r_xtop, &bbox.r_ytop) != 4)
		bbox = def->cd_bbox;
	}
    }

    /*
     * Parse the remainder of the arguments to find out the reference
     * points in the child cell and the edit cell.  Use the defaults
     * of the lower-left corner of the child cell's bounding box, and
     * the lower-left corner of the box tool, if the respective reference
     * points weren't provided.  (Lower-left of the box tool is interpreted
     * in root coordinates).
     */
	// getcell cellname child 0 0 parent ll v 0 0
    av = &cmd->tx_argv[2];
    ac = cmd->tx_argc - 2;
    hasChild = hasRoot = hasTrans = FALSE;
    while (ac > 0)
    {
	static const char * const kwdNames[] = { "child", "parent", "0", "90", "180", "270",
					    "v", "0v", "90v", "180v", "270v",
					    "h", "0h", "90h", "180h", "270h", 0 };
	static const char * const refPointNames[] = { "ll", "lr", "ul", "ur", 0 };
	Label *lab;
	int n,p;

	n = Lookup(av[0], kwdNames);
	if (n < 0)
	{
	    TxError("Unrecognized parent/child keyword: \"%s\"\n", av[0]);
	    goto usage;
	}
	switch (n)
	{
	    case  0:	/* Child */
		if (ac < 2)
		{
		    TxError("Keyword must be followed by a reference point\n");
		    goto usage;
	        }
		//else if (ac == 3) # error case: getcell cellname child 0 0 parent ll -> (ac > 3) -> read 0 as label 
		else if (ac >= 3 && StrIsInt(av[1]) && StrIsInt(av[2]))
		{
		    childPoint.p_x = cmdParseCoord(w, av[1], TRUE, TRUE);
		    childPoint.p_y = cmdParseCoord(w, av[2], TRUE, FALSE);
		    av += 3;
		    ac -= 3;
		}
		else
		{
		    p = Lookup(av[1], refPointNames);
		    if (p == 0) /* lower left */
		    {
			childPoint.p_x = bbox.r_ll.p_x;
			childPoint.p_y = bbox.r_ll.p_y;
		    }
		    else if (p == 1) /* lower right */
		    {
			childPoint.p_x = bbox.r_ur.p_x;
			childPoint.p_y = bbox.r_ll.p_y;
		    }
		    else if (p == 2) /* upper left */
		    {
			childPoint.p_x = bbox.r_ll.p_x;
			childPoint.p_y = bbox.r_ur.p_y;
		    }
		    else if (p == 3) /* upper right */
		    {
			childPoint.p_x = bbox.r_ur.p_x;
			childPoint.p_y = bbox.r_ur.p_y;
		    }
		    else
		    {
			childPoint = TiPlaneRect.r_ur;
			(void) DBSrLabelLoc(dummy, av[1], cmdDumpFunc,
					    &childPoint);
			if (childPoint.p_x == TiPlaneRect.r_xtop &&
			    childPoint.p_y == TiPlaneRect.r_ytop)
			{
			    TxError("Couldn't find label \"%s\" in cell \"%s\".\n",
				    av[1], cmd->tx_argv[1]);
			    return FALSE;
			}
		    }
		    av += 2;
		    ac -= 2;
		}
		hasChild = TRUE;
		break;
	    case  1:	/* Parent */
		if (ac < 2)
		{
		    TxError("Keyword must be followed by a reference point\n");
		    goto usage;
	    }
		//else if (ac == 3) # error case: getcell cellname child 0 0 parent ll v 0 0 -> (ac > 3) -> read 0 as label 
		else if (ac >= 3 && StrIsInt(av[1]) && StrIsInt(av[2]))
		{
		    editPoint.p_x = cmdParseCoord(w, av[1], TRUE, TRUE);
		    editPoint.p_y = cmdParseCoord(w, av[2], TRUE, FALSE);
		    av += 3;
		    ac -= 3;
		    GeoTransPoint(&EditToRootTransform, &editPoint,
				  &rootPoint);
		}
		else
		{
		    p = Lookup(av[1], refPointNames);
		    if (p == 0) /* lower left */
		    {
			if (!ToolGetBox(&rootDef, &rootBox) ||
			    (rootDef != EditRootDef)) goto box_error;
			rootPoint.p_x = rootBox.r_ll.p_x;
			rootPoint.p_y = rootBox.r_ll.p_y;
		    }
		    else if (p == 1) /* lower right */
		    {
			if (!ToolGetBox(&rootDef, &rootBox) ||
			    (rootDef != EditRootDef)) goto box_error;
			rootPoint.p_x = rootBox.r_ur.p_x;
			rootPoint.p_y = rootBox.r_ll.p_y;
		    }
		    else if (p == 2) /* upper left */
		    {
			if (!ToolGetBox(&rootDef, &rootBox) ||
			    (rootDef != EditRootDef)) goto box_error;
			rootPoint.p_x = rootBox.r_ll.p_x;
			rootPoint.p_y = rootBox.r_ur.p_y;
		    }
		    else if (p == 3) /* upper right */
		    {
			if (!ToolGetBox(&rootDef, &rootBox) ||
			    (rootDef != EditRootDef)) goto box_error;
			rootPoint.p_x = rootBox.r_ur.p_x;
			rootPoint.p_y = rootBox.r_ur.p_y;
		    }
		    else
		    {
			for (lab = editDef->cd_labels; lab; lab = lab->lab_next)
			    if (strcmp(lab->lab_text, av[1]) == 0)
				break;

			if (lab == NULL)
			{
			    TxError("Couldn't find label \"%s\" in edit cell.\n",
				    av[1]);
			    return FALSE;
			}
			editPoint = lab->lab_rect.r_ll;
			GeoTransPoint(&EditToRootTransform, &editPoint,
				      &rootPoint);
		    }
		    av += 2;
		    ac -= 2;
		}
		hasRoot = TRUE;
		break;
	    case  2:	/* 0 */
		tx_cell = &GeoIdentityTransform;
transform_cell:
		if (ac < 2 )
		{
default_action:
		    {
			Rect r;

			GeoTransRect(tx_cell, &bbox, &r);
			GeoTranslateTrans(tx_cell, bbox.r_xbot - r.r_xbot,
					  bbox.r_ybot - r.r_ybot,
					  &trans_cell);
		    }
		    av += 1;
		    ac -= 1;
	    }
		// error case: getcell cellname v 0 0 -> read 0 in kwdNames -> goto default transform
		// av[1] = "0", "90", "180", "270" case -> av[1] must mean editpoint coordinate
		else if (Lookup(av[1], kwdNames)>=0 && Lookup(av[1], kwdNames)!= 2 && strcmp(av[1],"90") && strcmp(av[1],"180") && strcmp(av[1],"270"))
		{
		    goto default_action;
		}
		else
		{
		    if (StrIsInt(av[1]))
		    {
			editPoint.p_x = atoi(av[1]);
			if (ac < 3 || !StrIsInt(av[2]))
			{
			    TxError("Must provide two coordinates\n");
			    goto usage;
			}
			editPoint.p_y = atoi(av[2]);
			av += 3;
			ac -= 3;
		    }
		    else
		    {
			p = Lookup(av[1], refPointNames);
			if (p == 0) /* lower left */
			{
			    editPoint.p_x = bbox.r_ll.p_x;
			    editPoint.p_y = bbox.r_ll.p_y;
			}
			else if (p == 1) /* lower right */
			{
			    editPoint.p_x = bbox.r_ur.p_x;
			    editPoint.p_y = bbox.r_ll.p_y;
			}
			else if (p == 2) /* upper left */
			{
			    editPoint.p_x = bbox.r_ll.p_x;
			    editPoint.p_y = bbox.r_ur.p_y;
			}
			else if (p == 3) /* upper right */
			{
			    editPoint.p_x = bbox.r_ur.p_x;
			    editPoint.p_y = bbox.r_ur.p_y;
			}
			else
			{
			    editPoint = TiPlaneRect.r_ur;
			    (void) DBSrLabelLoc(dummy, av[1], cmdDumpFunc,
						&editPoint);
			    if (editPoint.p_x == TiPlaneRect.r_xtop &&
				editPoint.p_y == TiPlaneRect.r_ytop)
			    {
				TxError("Couldn't find label \"%s\" in cell \"%s\".\n",
					av[1], cmd->tx_argv[1]);
				return FALSE;
			    }
			}
			av += 2;
			ac -= 2;
		    }
		    {
			Point p;

			GeoTransPoint(tx_cell, &editPoint, &p);
			GeoTranslateTrans(tx_cell, editPoint.p_x - p.p_x,
					  editPoint.p_y - p.p_y, &trans_cell);
		    }
		}
		hasTrans = TRUE;
		break;
	    case  3:	/* 90 */
		tx_cell = &Geo90Transform;
		goto transform_cell;
	    case  4:	/* 180 */
		tx_cell = &Geo180Transform;
		goto transform_cell;
	    case  5:	/* 270 */
		tx_cell = &Geo270Transform;
		goto transform_cell;
	    case  6:	/* v */
	    case  7:	/* 0v */
		tx_cell = &GeoUpsideDownTransform;
		goto transform_cell;
	    case  8:	/* 90v */
		tx_cell = &GeoRef45Transform;
		goto transform_cell;
	    case  9:	/* 180v */
		tx_cell = &GeoSidewaysTransform;
		goto transform_cell;
	    case  10:	/* 270v */
		tx_cell = &GeoRef135Transform;
		goto transform_cell;
	    case  11:	/* h */
	    case  12:	/* 0h */
		tx_cell = &GeoSidewaysTransform;
		goto transform_cell;
	    case 13:	/* 90h */
		tx_cell = &GeoRef135Transform;
		goto transform_cell;
	    case 14:	/* 180h */
		tx_cell = &GeoUpsideDownTransform;
		goto transform_cell;
	    case 15:	/* 270h */
		tx_cell = &GeoRef45Transform;
		goto transform_cell;
	}
    }

    /*
     * Use the default values if explicit reference points weren't
     * provided.
     */
    if (!hasChild)
	childPoint = bbox.r_ll;
    if (!hasRoot)
    {
	if (!ToolGetBox(&rootDef, &rootBox))
	{
box_error:
	    TxError("The box's lower-left corner must point to the place\n");
	    TxError("    in the edit cell where you'd like to put \"%s\".\n",
		cmd->tx_argv[1]);
	    return FALSE;
	}
	else if (rootDef != EditRootDef)
	{
	    TxError("The box is in cell \"%s\", not in the edit cell \"%s\"\n",
			rootDef->cd_name, EditRootDef->cd_name);
	    return FALSE;
	}
	rootPoint = rootBox.r_ll;
    }
    if (!hasTrans)
    {
	trans_cell = GeoIdentityTransform;
    }

    scx->scx_use = dummy;

    /* Transform childPoint by trans_cell */
    if (hasChild)
    {
	Point refpoint;

	GeoTransPoint(&trans_cell, &childPoint, &refpoint);
	GeoTranslateTrans(&trans_cell, rootPoint.p_x - refpoint.p_x,
		rootPoint.p_y - refpoint.p_y, &scx->scx_trans);
    }
    else
    {
	GeoTranslateTrans(&trans_cell, rootPoint.p_x - childPoint.p_x,
		rootPoint.p_y - childPoint.p_y, &scx->scx_trans);
    }

    scx->scx_area = bbox;
    return TRUE;

usage:
    TxError(
	"Usage: %s cellName [child refPointChild] [parent refPointParent]\n",
	cmdName);
    TxError("       [transform [refPointTrans]],\n");
    TxError("       where the refPoints are either a single label name,\n");
    TxError(
    "       or ll for lower left corner, or lr for lower right corner\n");
    TxError(
    "       or ul for upper left corner, or ur for upper right corner\n");
    TxError(
    "       or a pair of integer coordinates, and the transform is one of\n");
    TxError(
    "       90, 180, 270, v, 90v, 180v, 270v, h, 90h, 180h, 270h.\n");
    return FALSE;
}

/*
 * cmdDumpFunc --
 *
 * Search function used to locate positioning label.  It just computes
 * the lower-left corner of the label and aborts the search.
 *
 * Results:
 *	Always returns 1.
 *
 * Side effects:
 *	Sets *point to the lower-left corner of the label.
 */

    /* ARGSUSED */
int
cmdDumpFunc(
    Rect *rect,			/* Root coordinates of the label. */
    char *name,			/* Label name (not used). */
    Label *label,		/* Pointer to label (not used). */
    Point *point)		/* Place to store label's lower-left. */
{
    *point = rect->r_ll;
    return 1;
}

