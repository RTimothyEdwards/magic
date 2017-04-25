/*
 * ExtCell.c --
 *
 * Circuit extraction.
 * Extract a single cell.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtCell.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "utils/stack.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "utils/undo.h"

/* --------------------------- Global data ---------------------------- */

/*
 * Value normally present in ti_client to indicate tiles that have not
 * been marked with their associated region.
 */
ClientData extUnInit = (ClientData) CLIENTDEFAULT;


/* ------------------------ Data local to this file ------------------- */

/* Forward declarations */
int extOutputUsesFunc();
FILE *extFileOpen();

void extCellFile();
void extHeader();


/*
 * ----------------------------------------------------------------------------
 *
 * ExtCell --
 *
 * Extract the cell 'def', plus all its interactions with its subcells.
 * Place the result in the file named 'outName'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates the file 'outName'.ext and writes to it.
 *	May leave feedback information where errors were encountered.
 *	Upon return, extNumFatal contains the number of fatal errors
 *	encountered while extracting 'def', and extNumWarnings contains
 *	the number of warnings.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtCell(def, outName, doLength)
    CellDef *def;	/* Cell being extracted */
    char *outName;	/* Name of output file; if NULL, derive from def name */
    bool doLength;	/* If TRUE, extract pathlengths from drivers to
			 * receivers (the names are stored in ExtLength.c).
			 * Should only be TRUE for the root cell in a
			 * hierarchy.
			 */
{
    char *filename;
    FILE *f;

    f = extFileOpen(def, outName, "w", &filename);

    TxPrintf("Extracting %s into %s:\n", def->cd_name, filename);

    if (f == NULL)
    {
#ifdef MAGIC_WRAPPER
	TxError("Cannot open output file.\n");
#else
	TxError("Cannot open output file: ");
	perror(filename);
#endif
	return;
    }

    extNumFatal = extNumWarnings = 0;
    extCellFile(def, f, doLength);
    (void) fclose(f);

    if (extNumFatal > 0 || extNumWarnings > 0)
    {
	TxPrintf("%s:", def->cd_name);
	if (extNumFatal > 0)
	    TxPrintf(" %d fatal error%s",
		extNumFatal, extNumFatal != 1 ? "s" : "");
	if (extNumWarnings > 0)
	    TxPrintf(" %d warning%s",
		extNumWarnings, extNumWarnings != 1 ? "s" : "");
	TxPrintf("\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extFileOpen --
 *
 * Open the .ext file corresponding to a .mag file.
 * If def->cd_file is non-NULL, the .ext file is just def->cd_file with
 * the trailing .mag replaced by .ext.  Otherwise, the .ext file is just
 * def->cd_name followed by .ext.
 *
 * Results:
 *	Return a pointer to an open FILE, or NULL if the .ext
 *	file could not be opened in the specified mode.
 *
 * Side effects:
 *	Opens a file.
 *
 * ----------------------------------------------------------------------------
 */

FILE *
extFileOpen(def, file, mode, prealfile)
    CellDef *def;	/* Cell whose .ext file is to be written */
    char *file;		/* If non-NULL, open 'name'.ext; otherwise,
			 * derive filename from 'def' as described
			 * above.
			 */
    char *mode;		/* Either "r" or "w", the mode in which the .ext
			 * file is to be opened.
			 */
    char **prealfile;	/* If this is non-NULL, it gets set to point to
			 * a string holding the name of the .ext file.
			 */
{
    char namebuf[512], *name, *endp, *ends;
    int len;
    FILE *rfile, *testf;

    if (file) name = file;
    else if (def->cd_file)
    {
	name = def->cd_file;
	ends = strrchr(def->cd_file, '/');
	if (ends == NULL) ends = def->cd_file;
	if (endp = strrchr(ends + 1, '.'))
	{
	    name = namebuf;
	    len = endp - def->cd_file;
	    if (len > sizeof namebuf - 1) len = sizeof namebuf - 1;
	    (void) strncpy(namebuf, def->cd_file, len);
	    namebuf[len] = '\0';
	}
    }
    else name = def->cd_name;

    /* Try once as-is, and if this fails, try stripping any leading	*/
    /* path information in case cell is in a read-only directory.	*/

    if ((rfile = PaOpen(name, mode, ".ext", Path, CellLibPath, prealfile)) != NULL)
	return rfile;

    if (!strcmp(mode, "r")) return NULL;	/* Not even readable */

    /* Try writing to the cwd IF there is no .mag file by the	*/
    /* same name in the cwd that would conflict.		*/

    name = strrchr(def->cd_name, '/');
    if (name != NULL)
	name++;
    else
	name = def->cd_name;

    ends = strrchr(def->cd_file, '/');
    if (ends != NULL)
    {
	testf = PaOpen(ends + 1, "r", ".mag", ".", ".", NULL);
	if (testf)
	{
	    fclose(testf);
	    return NULL;
	}
    }
    return (PaOpen(name, mode, ".ext", ".", ".", prealfile));
}

/*
 * ----------------------------------------------------------------------------
 *
 * extCellFile --
 *
 * Internal interface for extracting a single cell.
 * Extracts it to the open FILE 'f'.  Doesn't print
 * any messages.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May leave feedback information where errors were encountered.
 *	Upon return, extNumFatal has been incremented by the number of
 *	fatal errors encountered while extracting 'def', and extNumWarnings
 *	by the number of warnings.
 *
 * ----------------------------------------------------------------------------
 */

void
extCellFile(def, f, doLength)
    CellDef *def;	/* Def to be extracted */
    FILE *f;		/* Output to this file */
    bool doLength;	/* TRUE if we should extract driver-receiver path
			 * length information for this cell (see ExtCell
			 * for more details).
			 */
{
    NodeRegion *reg;

    UndoDisable();

    /* Output the header: timestamp, technology, calls on cell uses */
    if (!SigInterruptPending) extHeader(def, f);

    /* Extract the mask information in this cell */
    reg = (NodeRegion *) NULL;
    if (!SigInterruptPending) reg = extBasic(def, f);

    /* Do hierarchical extraction */
    extParentUse->cu_def = def;
    if (!SigInterruptPending) extSubtree(extParentUse, reg, f);
    if (!SigInterruptPending) extArray(extParentUse, f);

    /* Clean up from basic extraction */
    if (reg) ExtFreeLabRegions((LabRegion *) reg);
    ExtResetTiles(def, extUnInit);

    /* Final pass: extract length information if desired */
    if (!SigInterruptPending && doLength && (ExtOptions & EXT_DOLENGTH))
	extLength(extParentUse, f);

    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHeader --
 *
 * Output header information to the .ext file for a cell.
 * This information consists of:
 *
 *	timestamp
 *	extractor version number
 *	technology
 *	scale factors for resistance, capacitance, and lambda
 *	calls on all subcells used by this cell (see extOutputUsesFunc)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to (FILE *) 'f'.
 *
 * ----------------------------------------------------------------------------
 */

void
extHeader(def, f)
    CellDef *def;	/* Cell being extracted */
    FILE *f;		/* Write to this file */
{
    int n;
    bool propfound;
    char *propvalue;

    /* Output a timestamp (should be first) */
    fprintf(f, "timestamp %d\n", def->cd_timestamp);

    /* Output our version number */
    fprintf(f, "version %s\n", MagicVersion);

    /* Output the technology */
    fprintf(f, "tech %s\n", DBTechName);

    /* Output the extract style name */
    fprintf(f, "style %s\n", ExtCurStyle->exts_name);

    /*
     * Output scaling factors: R C D
     *		R = amount to multiply all resistances in the file by
     *		C = amount to multiply all capacitances by
     *		D = amount to multiply all linear distances by (areas
     *		    should be multiplied by D**2).
     */
    fprintf(f, "scale %d %d %g\n",
		ExtCurStyle->exts_resistScale,
		ExtCurStyle->exts_capScale,
		ExtCurStyle->exts_unitsPerLambda);

    /* Output the sheet resistivity classes */
    fprintf(f, "resistclasses");
    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	fprintf(f, " %d", ExtCurStyle->exts_resistByResistClass[n]);
    fprintf(f, "\n");

    /* Output any parameters defined for this cell that	*/
    /* are to be passed to instances of the cell	*/
    /* (created by defining property "parameter")	*/

    propvalue = (char *)DBPropGet(def, "parameter", &propfound);
    if (propfound)
    {
	// Use device parameter table to store the cell def parameters,
	// but preface name with ":" to avoid any conflict with device
	// names.
	fprintf(f, "parameters :%s %s\n", def->cd_name, propvalue);
    }

    /* Output all calls on subcells */
    (void) DBCellEnum(def, extOutputUsesFunc, (ClientData) f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputUsesFunc --
 *
 * Filter function, called via DBCellEnum, that outputs all the
 * cell uses contained in the parent's cell tile planes.
 *
 * Results:
 *	Always returns 0, for DBCellEnum to keep going.
 *
 * Side effects:
 *	Writes a line for each use encountered to 'outf'.
 *	The line is of the following form:
 *
 *		use defname useid Ta ... Tf
 *
 *	where 'defname' is the name of the cell def referenced (cd_name),
 *	'useid' is its use identifier (cu_id), and Ta ... Tf are the six
 *	components of the transform from coordinates of this use up to
 *	its parent.  If the cell is an array, the use id may be followed by:
 *
 *		[xlo,xhi,xsep][ylo,yhi,ysep]
 *
 *	The indices are xlo through xhi inclusive, or ylo through yhi
 *	inclusive.  The separation between adjacent elements is xsep
 *	or ysep; this is used in computing the transform for a particular
 *	array element.  If arraying is not present in a given direction,
 *	the low and high indices are equal and the separation is ignored.
 *
 * ----------------------------------------------------------------------------
 */

int
extOutputUsesFunc(cu, outf)
    CellUse *cu;
    FILE *outf;
{
    Transform *t = &cu->cu_transform;

    fprintf(outf, "use %s %s", cu->cu_def->cd_name, cu->cu_id);
    if (cu->cu_xlo != cu->cu_xhi || cu->cu_ylo != cu->cu_yhi)
    {
	fprintf(outf, "[%d:%d:%d]",
			cu->cu_xlo, cu->cu_xhi, cu->cu_xsep);
	fprintf(outf, "[%d:%d:%d]",
			cu->cu_ylo, cu->cu_yhi, cu->cu_ysep);
    }

    /* Output transform to parent */
    fprintf(outf, " %d %d %d %d %d %d\n",
			t->t_a, t->t_b, t->t_c, t->t_d, t->t_e, t->t_f);

    return (0);
}
