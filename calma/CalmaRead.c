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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <netinet/in.h>

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
#include "utils/undo.h"

/* Globals for Calma reading */
FILE *calmaInputFile = NULL;		/* Read from this stream */
FILE *calmaErrorFile = NULL;		/* Write error output here */
bool CalmaSubcellPolygons = FALSE;	/* Put non-Manhattan polygons
					 * in their own subcells.
					 */
int CalmaPolygonCount;
bool CalmaFlattenUses = FALSE;		/* If TRUE, small cells in the input
					 * stream are flattened when encountered
					 * as uses.  This improves magic's
					 * performance when handling contacts
					 * saved as subcell arrays.
					 */
bool CalmaReadOnly = FALSE;		/* Set files to read-only and
					 * retain file position information
					 * so cells can be written verbatim.
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
extern void calmaUnexpected();

bool calmaParseUnits();

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
    FILE *file;			/* File from which to read Calma */
    char *filename;		/* The real name of the file read */
{
    int k, version;
    char *libname = NULL;
    MagWindow *mw;
    static int hdrSkip[] = { CALMA_FORMAT, CALMA_MASK, CALMA_ENDMASKS,
			     CALMA_REFLIBS, CALMA_FONTS, CALMA_ATTRTABLE,
			     CALMA_STYPTABLE, CALMA_GENERATIONS, -1 };
    static int skipBeforeLib[] = { CALMA_LIBDIRSIZE, CALMA_SRFNAME, 
				   CALMA_LIBSECUR, -1 };

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
    if ((libname != NULL) && (libname[0] != '\0'))
    {
	/* Avoid generating a magic name with spaces in it. . .	*/
	/* (added by Mike Godfrey, 7/17/05)			*/

	for (k = 0; k < strlen(libname); k++)
	    if (libname[k] == ' ')
		libname[k] = '_';
	TxPrintf("Library name: %s\n", libname);
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

    if (libname != NULL)
    {
	mw = CmdGetRootPoint((Point *)NULL, (Rect *)NULL);
	if (mw == NULL)
	    windCheckOnlyWindow(&mw, DBWclientID);
	if (mw != NULL)
	{
	    if (calmaLookCell(libname, NULL) != (CellDef *)NULL)
		DBWloadWindow(mw, libname, FALSE);
	}
	freeMagic(libname);
    }

    CIFReadCellCleanup(1);
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
    int nbytes, rtype;
    double metersPerDBUnit;
    double userUnitsPerDBUnit;
    double cuPerDBUnit;

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
 * calmaReadError --
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

void
    /*VARARGS1*/
calmaReadError(format, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)
    char *format;
    char *a1, *a2, *a3, *a4, *a5, *a6, *a7, *a8, *a9, *a10;
{
    calmaTotalErrors++;
    if (CIFWarningLevel == CIF_WARN_NONE) return;

    if ((calmaTotalErrors < 100) || (CIFWarningLevel != CIF_WARN_LIMIT))
    {   

        if (CIFWarningLevel == CIF_WARN_REDIRECT)
        {
            if (calmaErrorFile != NULL)
            {
                fprintf(calmaErrorFile, "Error while reading cell \"%s\": ",
                                cifReadCellDef->cd_name);
                fprintf(calmaErrorFile, format, a1, a2, a3, a4, a5, a6, a7, 
                                a8, a9, a10);
            }
        }
        else
        {
            TxError("Error while reading cell \"%s\": ", cifReadCellDef->cd_name);
            TxError(format, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
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
    calmaReadError("Unexpected record type in input: \n");

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
}
