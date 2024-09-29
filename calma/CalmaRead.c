/*
 * CalmaRead.c --
 *
 * Input of Calma GDS-II stream format.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/calma/CalmaRead.c,v 1.3 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <netinet/in.h>

/*
 * C99 compat
 * Mind: tcltk/tclmagic.h must be included prior to all the other headers
 */
#include "tcltk/tclmagic.h"

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"
#include "utils/tech.h"
#include "cif/cif.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "calma/calmaInt.h"
#include "commands/commands.h"		/* for CmdGetRootPoint */
#include "utils/main.h"			/* for EditCellUse */
#include "utils/undo.h"

/* C99 compat */
#include "calma/calma.h"

/* Globals for Calma reading */
FILETYPE calmaInputFile = NULL;		/* Read from this stream */
FILE *calmaErrorFile = NULL;		/* Write error output here */
unsigned char CalmaSubcellPolygons = CALMA_POLYGON_NONE;
					/* Read non-Manhattan polygons as-is */
int CalmaPolygonCount;
bool CalmaSubcellPaths = FALSE;		/* Put paths in their own subcells. */
int CalmaPathCount;
bool CalmaFlattenUses = FALSE;		/* If TRUE, small cells in the input
					 * stream are flattened when encountered
					 * as uses.  This improves magic's
					 * performance when handling contacts
					 * saved as subcell arrays.
					 */
char **CalmaFlattenUsesByName = NULL;	/* NULL-terminated list of strings
					 * to do glob-style pattern matching
					 * to determine what cells to flatten
					 * by cellname.
					 */
bool CalmaReadOnly = FALSE;		/* Set files to read-only and
					 * retain file position information
					 * so cells can be written verbatim.
					 */
float CalmaMagScale = 1.0;		/* Scale by which to interpret the MAG
					 * record in GDS text records.  The
					 * default is to treat the value as
					 * the text height in microns.  This
					 * value reinterprets the scale.
					 */
bool CalmaNoDRCCheck = FALSE;		/* If TRUE, don't mark cells as needing
					 * a DRC check;  they will be assumed
					 * DRC clean.
					 */
bool CalmaPostOrder = FALSE;		/* If TRUE, forces the GDS parser to
					 * read cells in post-order.  It is
					 * necessary, e.g., when we need to
					 * flatten cells that are contact cuts.
					 * Added by Nishit 8/16/2004
					 */
bool CalmaNoDuplicates = FALSE;		/* If TRUE, then if a cell exists in
					 * memory with the same name as a cell	
					 * in the GDS file, then the cell in
					 * the GDS file is skipped.
					 */
bool CalmaUnique = FALSE;		/* If TRUE, then if a cell exists in
					 * memory with the same name as a cell
					 * in the GDS file, then the cell in
					 * memory is renamed to a unique
					 * identifier with a _N suffix.
					 */
extern bool CalmaDoLibrary;		/* Also used by GDS write */

extern void calmaUnexpected();
extern int calmaWriteInitFunc();

/*
 * Scaling.
 * Multiply all coordinates by calmaReadScale1, then divide them
 * by calmaReadScale2 in order to get coordinates in centimicrons.
 */
int calmaReadScale1;
int calmaReadScale2;

int calmaTotalErrors;

/*
 * Lookahead: calmaLApresent is TRUE when calmaLAnbytes and calmaLArtype
 * are set to the record header of a record we just ungot.
 */
bool calmaLApresent;	/* TRUE if lookahead input waiting */
int calmaLAnbytes;	/* # bytes in record (from header)  */
int calmaLArtype;	/* Record type */

/*
 * Hash table for errors, indexed by (layer, datatype).
 * The corresponding entry in this table is created whenever
 * a (layer, datatype) is seen that we don't recognize, so
 * we don't output an error message more than once.
 */
HashTable calmaLayerHash;

/*
 * Hash table to keep track of all defs that have appeared
 * in this file.  Indexed by cell def name.
 */
HashTable calmaDefInitHash;

/* Common stuff to ignore */
int calmaElementIgnore[] = { CALMA_ELFLAGS, CALMA_PLEX, -1 };

/*
 * ----------------------------------------------------------------------------
 *
 * CalmaReadFile --
 *
 * Read an entire GDS-II stream format library from the open FILE 'file'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify the contents of cifReadCellDef by painting or adding
 *	new uses or labels.  May also create new CellDefs.
 *
 * ----------------------------------------------------------------------------
 */

void
CalmaReadFile(file, filename)
    FILETYPE file;			/* File from which to read Calma */
    char *filename;		/* The real name of the file read */
{
    int k, version;
    char *libname = NULL, *libnameptr;
    MagWindow *mw;
    static int hdrSkip[] = { CALMA_FORMAT, CALMA_MASK, CALMA_ENDMASKS,
			     CALMA_REFLIBS, CALMA_FONTS, CALMA_ATTRTABLE,
			     CALMA_STYPTABLE, CALMA_GENERATIONS, -1 };
    static int skipBeforeLib[] = { CALMA_LIBDIRSIZE, CALMA_SRFNAME,
				   CALMA_LIBSECUR, -1 };

    if (EditCellUse == (CellUse *)NULL)
    {
	TxError("Cannot read GDS:  There is no edit cell.\n");
	return;
    }

    /* We will use full cell names as keys in this hash table */
    CIFReadCellInit(0);

    if (CIFWarningLevel == CIF_WARN_REDIRECT)
    {
	if (CIFErrorFilename == NULL)
	    calmaErrorFile = NULL;
	else
	    calmaErrorFile = PaOpen(CIFErrorFilename, "w", (char *)NULL, ".",
			(char *)NULL, (char **)NULL);
    }

    if (cifCurReadStyle == NULL)
    {
	TxError("Don't know how to read GDS-II:\n");
	TxError("Nothing in \"cifinput\" section of tech file.\n");
	return;
    }
    TxPrintf("Warning: Calma reading is not undoable!  I hope that's OK.\n");
    UndoDisable();

    calmaTotalErrors = 0;
    CalmaPolygonCount = 0;
    CalmaPathCount = 0;

    /* Reset cd_client pointers (using init function from CalmaWrite.c) */
    /* This is in case a cell already in memory is being referenced;	*/
    /* it is probably better to avoid those kinds of naming collisions	*/
    /* though. . . 							*/
    (void) DBCellSrDefs(0, calmaWriteInitFunc, (ClientData) NULL);

    HashInit(&calmaDefInitHash, 32, 0);
    calmaLApresent = FALSE;
    calmaInputFile = file;

    /* Read the GDS-II header */
    if (!calmaReadI2Record(CALMA_HEADER, &version)) goto done;
    if (version < 600)
	TxPrintf("Library written using GDS-II Release %d.0\n", version);
    else
	TxPrintf("Library written using GDS-II Release %d.%d\n",
	    version / 100, version % 100);
    if (!calmaSkipExact(CALMA_BGNLIB)) goto done;
    calmaSkipSet(skipBeforeLib);
    if (!calmaReadStringRecord(CALMA_LIBNAME, &libname)) goto done;

    /* Use CalmaDoLibrary similarly for input as for output;  if set to	*/
    /* TRUE, the library name is considered meaningless and discarded;	*/
    /* the GDS file contents are read into memory but no view is loaded	*/

    if (CalmaDoLibrary)
	libnameptr = NULL;
    else
        libnameptr = libname;

    if ((libnameptr != NULL) && (libname[0] != '\0'))
    {
	bool modified = FALSE;
	char *sptr;

	/* Avoid generating a magic name with spaces in it. . .	*/
	/* (added by Mike Godfrey, 7/17/05)			*/

	for (k = 0; k < strlen(libname); k++)
	    if (libname[k] == ' ')
	    {
		libname[k] = '_';
		modified = TRUE;
	    }

	/* Avoid generating a magic name with slashes in it. . . */
	/* (added by Tim, 8/26/2022)				 */

	if ((sptr = strrchr(libname, '/')) != NULL)
	{
	    libnameptr = sptr + 1;
	    modified = TRUE;
	}

	if (modified)
	    TxPrintf("Library name modified to make legal cell name syntax.\n");
	TxPrintf("Library name: %s\n", libnameptr);
    }

    /* Skip the reflibs, fonts, etc. cruft */
    calmaSkipSet(hdrSkip);

    /* Set the scale factors */
    if (!calmaParseUnits()) goto done;

    /* Main body of GDS-II input */
    while (calmaParseStructure(filename))
	if (SigInterruptPending)
	    goto done;
    (void) calmaSkipExact(CALMA_ENDLIB);

done:

    /* Added by Nishit, Sept. 2004---Load cell read from GDS    */
    /* stream file to the magic layout window.  If this fails   */
    /* then we do the original action and don't do a load into  */
    /* the window.  Note that this follows the Magic GDS output	*/
    /* convention of giving the library the name of the		*/
    /* top-level cell, so magic-produced GDS can be read back	*/
    /* with the expected cell appearing in the layout window.	*/

    if (libnameptr != NULL)
    {
	mw = CmdGetRootPoint((Point *)NULL, (Rect *)NULL);
	if (mw == NULL)
	    windCheckOnlyWindow(&mw, DBWclientID);
	if (mw != NULL)
	{
	    if (calmaLookCell(libnameptr, NULL) != (CellDef *)NULL)
		DBWloadWindow(mw, libnameptr, 0);
	}
	freeMagic(libname);
    }

    CIFReadCellCleanup(FILE_CALMA);
    HashKill(&calmaDefInitHash);
    UndoEnable();

    if (calmaErrorFile != NULL) fclose(calmaErrorFile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaParseUnits --
 *
 * Process the CALMA_UNITS record that sets the relationship between
 * user units (stored in the stream file) and centimicrons.
 *
 * Results:
 *	TRUE if successful, FALSE if we encountered an error and
 *	the caller should abort.
 *
 * Side effects:
 *	Consumes input.
 *	Sets calmaReadScale1 to the number of centimicrons per user
 *	unit, and calmaReadScale2 to 1, unless calmaReadScale1 would be
 *	less than 1, in which case we set calmaReadScale1 to 1 and
 *	calmaReadScale2 to 1/calmaReadScale1.
 *
 * NOTE:
 *	We don't care about user units, only database units.  The
 *	GDS-II stream specifies the number of meters per database
 *	unit, which we use to compute the number of centimicrons
 *	per database unit.  Since database units are floating point,
 *	there is a possibility of roundoff unless the number of
 *	centimicrons per user unit is an integer value.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaParseUnits()
{
    int nbytes, rtype = 0;
    double metersPerDBUnit;
    double userUnitsPerDBUnit;
    double cuPerDBUnit;
    bool compatible;

    READRH(nbytes, rtype);
#ifdef	lint
    nbytes = nbytes;
#endif	/* lint */

    if (rtype != CALMA_UNITS)
    {
	calmaUnexpected(CALMA_UNITS, rtype);
	return (FALSE);
    }

    /* Skip user units per database unit */
    if (!calmaReadR8(&userUnitsPerDBUnit)) return (FALSE);

    /* Read meters per database unit */
    if (!calmaReadR8(&metersPerDBUnit)) return (FALSE);

    /* Important!  When CalmaReadOnly is TRUE, then this file will have its
     * contents output verbatim.  But if the database units don't match,
     * then it will get output at the wrong scale.  Setting a magnification
     * factor on the instance when generating output might (?) work.  For
     * now, prohibiting a GDS read in read-only mode when the database units
     * don't match.  This forces the user either to reconsider the read-only
     * status or to rewrite the GDS at a compatible scalefactor.
     */
    compatible = TRUE;
    if (CalmaReadOnly == TRUE)
    {
	if (CIFCurStyle->cs_flags & CWF_ANGSTROMS)
	{
	    if ((int)(0.5 + metersPerDBUnit * 1e12) != 100)
	    {
		CalmaReadError("Incompatible scale factor of %g, must be 1e-10.\n",
				metersPerDBUnit);
		TxError("Cannot read this file in read-only mode.\n");
		return FALSE;
	    }
	}
	else
	{
	    if ((int)(0.5 + metersPerDBUnit * 1e11) != 100)
	    {
		CalmaReadError("Incompatible scale factor of %g, must be 1e-9.\n",
				metersPerDBUnit);
		TxError("Cannot read this file in read-only mode.\n");
		return FALSE;
	    }
	}
    }

#ifdef	notdef
    TxPrintf("1 database unit equals %e user units\n", userUnitsPerDBUnit);
    TxPrintf("1 database unit equals %e meters\n", metersPerDBUnit);
    TxPrintf("1 user unit equals %e database units\n", 1.0/userUnitsPerDBUnit);
    TxPrintf("1 meter equals %e database units\n", 1.0/metersPerDBUnit);
#endif	/* notdef */

    /* Meters per database unit (1.0e8 corresponds to traditional centimicrons) */
    cuPerDBUnit = metersPerDBUnit * 1.0e8 * cifCurReadStyle->crs_multiplier;

    /*
     * Multiply database units by calmaReadScale1, then divide
     * by calmaReadScale2 to get CIF units.  The current scheme
     * relies entirely on calmaReadScale1 being an integer.
     */
    if (cuPerDBUnit >= 1.0)
    {
	calmaReadScale1 = (int)(cuPerDBUnit + 0.5);
	calmaReadScale2 = 1;
    }
    else
    {
	cuPerDBUnit = 1.0 / cuPerDBUnit;
	calmaReadScale1 = 1;
	calmaReadScale2 = (int)(cuPerDBUnit + 0.5);
    }
#ifdef	notdef
    TxPrintf("All units to be scaled by %d/%d\n", calmaReadScale1, calmaReadScale2);
#endif	/* notdef */

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CalmaReadError --
 *
 * This procedure is called to print out error messages during
 * Calma file reading.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      An error message is printed.
 *
 * Note:
 *      You can add more arguments if 10 turns out not to be enough.
 *
 * ----------------------------------------------------------------------------
 */

void CalmaReadError(char *format, ...)
{
    va_list args;
    OFFTYPE filepos;

    calmaTotalErrors++;
    if (CIFWarningLevel == CIF_WARN_NONE) return;

    if ((calmaTotalErrors < 100) || (CIFWarningLevel != CIF_WARN_LIMIT))
    {
	filepos = FTELL(calmaInputFile);

        if (CIFWarningLevel == CIF_WARN_REDIRECT)
        {
            if (calmaErrorFile != NULL)
            {
                fprintf(calmaErrorFile, "Error while reading cell \"%s\" ",
                                cifReadCellDef->cd_name);
		fprintf(calmaErrorFile, "(byte position %"DLONG_PREFIX"d): ",
				(dlong)filepos);
		va_start(args, format);
		Vfprintf(calmaErrorFile, format, args);
		va_end(args);
            }
        }
        else
        {
            TxError("Error while reading cell \"%s\" ", cifReadCellDef->cd_name);
	    TxError("(byte position %"DLONG_PREFIX"d): ", (dlong)filepos);
	    va_start(args, format);	    
	    TxErrorV(format, args);
	    va_end(args);	    
        }
    }
    else if ((calmaTotalErrors == 100) && (CIFWarningLevel == CIF_WARN_LIMIT))
    {
        TxError("Error limit set:  Remaining errors will not be reported.\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaUnexpected --
 *
 * Complain about a record where we expected one kind but got another.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints an error message.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaUnexpected(wanted, got)
    int wanted;	/* Type of record we wanted */
    int got;	/* Type of record we got */
{
    CalmaReadError("Unexpected record type in input: \n");

    if (CIFWarningLevel == CIF_WARN_NONE) return;
    if (calmaTotalErrors < 100 || (CIFWarningLevel != CIF_WARN_LIMIT))
    {
	if (CIFWarningLevel == CIF_WARN_REDIRECT)
	{
	    if (calmaErrorFile != NULL)
	    {
	        fprintf(calmaErrorFile,"    Expected %s record ",
			calmaRecordName(wanted));
		fprintf(calmaErrorFile, "but got %s.\n", calmaRecordName(got));
	    }
	}
	    else
	    {
	        TxError("    Expected %s record ", calmaRecordName(wanted));
	        TxError("but got %s.\n", calmaRecordName(got));
	    }
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaRecordName --
 *
 * Return a pointer to the printable name of a CALMA record type.
 *
 * Results:
 *	See above.
 *
 * Side effects:
 *	May overwrite the string we returned on the previous call.
 *
 * ----------------------------------------------------------------------------
 */

char *
calmaRecordName(rtype)
    int rtype;
{
    static char numeric[10];
    static char *calmaRecordNames[] =
    {
	"HEADER",	"BGNLIB",	"LIBNAME",	"UNITS",
	"ENDLIB",	"BGNSTR",	"STRNAME",	"ENDSTR",
	"BOUNDARY",	"PATH",		"SREF",		"AREF",
	"TEXT",		"LAYER",	"DATATYPE",	"WIDTH",
	"XY",		"ENDEL",	"SNAME",	"COLROW",
	"TEXTNODE",	"NODE",		"TEXTTYPE",	"PRESENTATION",
	"SPACING",	"STRING",	"STRANS",	"MAG",
	"ANGLE",	"UINTEGER",	"USTRING",	"REFLIBS",
	"FONTS",	"PATHTYPE",	"GENERATIONS",	"ATTRTABLE",
	"STYPTABLE",	"STRTYPE",	"ELFLAGS",	"ELKEY",
	"LINKTYPE",	"LINKKEYS",	"NODETYPE",	"PROPATTR",
	"PROPVALUE",	"BOX",		"BOXTYPE",	"PLEX",
	"BGNEXTN",	"ENDEXTN",	"TAPENUM",	"TAPECODE",
	"STRCLASS",	"RESERVED",	"FORMAT",	"MASK",
	"ENDMASKS"
    };

    if (rtype < 0 || rtype >= CALMA_NUMRECORDTYPES)
    {
	(void) sprintf(numeric, "%d", rtype);
	return (numeric);
    }

    return (calmaRecordNames[rtype]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CalmaTechInit --
 *
 * Prepare for a technology file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Error checking.
 *
 * ----------------------------------------------------------------------------
 */

void
CalmaTechInit()
{
    ASSERT(sizeof(FourByteInt)==4, "definition in calmaInt.h");
    ASSERT(sizeof(TwoByteInt)==2, "definition in calmaInt.h");

    /* NOTE:  Enable the code below when CalmaContactArrays	*/
    /* behaves like the non-arrayed function and can be enabled	*/
    /* by default.						*/
#if 0
    /* Initialize CalmaFlattenByName to have one entry for	*/
    /* "$$*$$" to match the name style used by the contact	*/
    /* array cell generation.  This can be overridden by the	*/
    /* "gds flatglob none" command option.			*/

    if (CalmaFlattenUsesByName == (char **)NULL)
    {
	CalmaFlattenUsesByName = (char **)mallocMagic(2 * sizeof(char *));
	*CalmaFlattenUsesByName = StrDup((char **)NULL, "$$*$$");
	*(CalmaFlattenUsesByName + 1) = NULL;
    }
#endif
}
