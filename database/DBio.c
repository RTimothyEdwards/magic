/*
 * DBio.c --
 *
 * Reading and writing of cells
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBio.c,v 1.5 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#include <unistd.h>
#define direct dirent
#else
#include <sys/dir.h>
#endif

#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "database/fonts.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/tech.h"
#include "textio/textio.h"
#include "drc/drc.h"
#include "utils/undo.h"
#include "utils/malloc.h"
#include "utils/signals.h"

#ifndef _PATH_TMP
#define _PATH_TMP "/tmp"
#endif

extern char *Path;

/* Suffix for all Magic files */
char *DBSuffix = ".mag";

/* Magic units per lambda (2 integers, representing (n / d) */
int DBLambda[2] = {1, 1};

/* If set to FALSE, don't print warning messages. */
bool DBVerbose = TRUE;

/* Global name of backup file for this session */
static char *DBbackupFile = (char *)NULL;

/* Forward declarations */
char *dbFgets();
FILE *dbReadOpen();
int DBFileOffset;
bool dbReadLabels();
bool dbReadElements();
bool dbReadProperties();
bool dbReadUse();

/*
 * ----------------------------------------------------------------------------
 *
 *   file_is_not_writeable -- 
 *
 *  Check to see if file is not writeable. (wen-king@cs.caltech.edu)
 *  Modified to deal with changed semantics of access() (rajit@cs.caltech.edu)
 *
 * ---------------------------------------------------------------------------
 */

static int
file_is_not_writeable(name)
    char *name;
{
    struct stat sbuf;

    /* attempt to read stat buffer for file */

    if (lstat(name,&sbuf) < 0) return(-1);

    /* regular file? if not, error */
    if (!S_ISREG(sbuf.st_mode)) { errno = EACCES; return(-1); }

    /* can we write to it? */
    if (access(name, W_OK) < 0) return(-1);

    /* the OS thinks we can write to the file;
       but does the file think so?
    */
    if (geteuid() == sbuf.st_uid) {
      if (sbuf.st_mode & S_IWUSR) return (0);
      /* I own the file, but I don't have write permission */
      errno = EACCES;
      return (-1);
    }

    if (!(sbuf.st_mode & (S_IWOTH|S_IWGRP))) {
      errno = EACCES;
      return (-1);
    }

    return(0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbCellReadDef --
 *
 *
 * Read in the paint for a cell from its associated disk file.
 * If a filename for the cell is specified, we try to open it
 * somewhere in the search path.  Otherwise, we try the filename
 * already associated with the cell, or the name of the cell itself
 * as the name of the file containing the definition of the cell.
 *
 * Mark the cell definition as "read in" (CDAVAILABLE), and
 * recompute the bounding box.
 *
 *				WARNING:
 *
 * It is the responsibility of the caller to call DBReComputeBbox(cellDef)
 * at a convenient place, as we do not set the bounding box of the cell def.
 * If we were to update the bounding box here, this CellDef would first have
 * to be ripped out of each parent subcell tile plane in which it appears, and
 * then relinked in after its bounding box had changed.  Since DBCellRead() may
 * be called from in the middle of a database search, however, the database
 * modification resulting from this could ruin the search context and crash
 * the system.
 *
 * Results:
 *	TRUE if the cell could be read successfully, FALSE
 *	otherwise.
 *
 * Side effects:
 	Clears the cell's MODIFIED bit.
 *	Updates the tile planes for the cell definition.
 *	In the event of an error while reading in the cell,
 *	the external integer errno is set to the UNIX error
 *	encountered.
 *
 *	If newTechOk is TRUE and the cell's technology is different
 *	from the current one, the current technology is changed.
 *
 * Errors:
 *	If incomplete specs are given either for a rectangle or for
 *	a cell use, then we immediately stop reading the file.
 *
 * File Format:
 *
 *	1. The first line of the file contains the string "magic".
 *
 *	2. Next comes an optional technology line, with the format
 *	"tech <tech>".  <tech> is the technology of the cell.
 *
 *	3. Next comes an optional scale line, with the format
 *	"magscale <n> <d>".  <n> and <d> give the number of magic
 *	internal units per lambda as the ratio <n>/<d>.  If this
 *	line is absent, the scale ratio is assumed to be 1-to-1.
 *
 *	4. Next comes an optional line giving the cell's timestamp
 *	(the last time it or any of its children changed, as far as
 *	we know).  The syntax is "timestamp <value>", where <value>
 *	is an integer as returned by the library function time().
 *
 *	5. Next come groups of lines describing rectangles of the
 *	Magic tile types.  Each group is headed with a line of the
 *	form "<< layer >>".	The layer name is matched against the
 *	current technology.  Each line after the header has the
 *	format "rect <xbot> <ybot> <xtop> <ytop>".
 *	Nonmanhattan geometry is covered by the entry
 *	"try <xbot> <ybot> <xtop> <ytop> <dir>" with <dir> indicating
 *	the direction of the corner made by the right triangle.
 *	If the split tile contains more than one type, separate entries
 *	are output for each.
 *	
 *	6. Zero or more groups of lines describing cell uses.  Each group
 *	is of the form
 *		use <filename> <id>
 *		array <xlo> <xhi> <xsep> <ylo> <yhi> <ysep>
 *		timestamp <int>
 *		transform <a> <b> <c> <d> <e> <f>
 *		box <xbot> <ybot> <xtop> <ytop>
 *	Each group may be preceded by one or more separator lines.  Note
 *	that <id> is optional and is omitted if there is to be no
 *	instance id for the cell use.  If it is omitted, an instance
 *	identifier is generated internally.  The "array" line may be
 *	omitted	if the cell use is not an array.  The "timestamp" line is
 *	optional;  if present, it gives the last time the parent
 *	was aware that the child changed.
 *
 *	7. If the cell contains labels, then the labels are preceded
 *	by the line "<< labels >>".  Each label is one line of the form
 *		rlabel <layer> [s] <xbot> <ybot> <xtop> <ytop> <position> <text>
 *		 or
 *		flabel <layer> [s] <xbot> <ybot> <xtop> <ytop> <position> <fontname>
 *			<size> <rotation> <offsetx> <offsety> <text>
 *		 or
 *		label <layer> <x> <y> <position> <text>
 *	(the last form is obsolete and is for point labels only).
 *
 *	Ports are declared in the label line after the label they refer
 *	to.  The syntax is
 *		port <index> <dirs>
 *	where <dir> is a string with one to four characters "nsew",
 *	denoting the position of allowed connections to the port.  The
 *	index must be unique to each port in a cell.
 *
 *	9. Elements are saved in a section "<< elements >>".
 *
 *     10. The file is terminated by the line "<< end >>".
 *
 *	Note: be careful about any changes to this format:  there are
 *	several previous file formats still in use in old files, and
 *	the current one is backwardly compatible with all of them.
 *
 *
 * ----------------------------------------------------------------------------
 */

bool
dbCellReadDef(f, cellDef, name, ignoreTech)
    FILE *f;		/* The file, already opened by the caller */
    CellDef *cellDef;	/* Pointer to definition of cell to be read in */
    char *name;		/* Name of file from which to read definition.
			 * If NULL, then use cellDef->cd_file; if that
			 * is NULL try the name of the cell.
			 */
    bool ignoreTech;	/* If FALSE then the technology of the file MUST
			 * match the current technology, or else the
			 * subroutine will return an error condition
			 * without reading anything.  If TRUE, a
			 * warning will be printed if the technology
			 * names do not match, but an attempt will be
			 * made to read the file anyway.
			 */
{
    int cellStamp = 0, rectCount = 0, rectReport = 10000;
    char line[2048], tech[50], layername[50];
    PaintResultType *ptable;
    bool result = TRUE, scaleLimit = FALSE;
    Rect *rp;
    int c;
    TileType type, rtype, loctype;
    TileTypeBitMask *rmask, typemask;
    Plane *plane;
    Rect r;
    int n = 1, d = 1;

    /*
     * It's very important to disable interrupts during the body of
     * this routine.  Otherwise, if the user types the interrupt key
     * only part of the file will be read in, and if he then writes
     * the cell out, the disk copy will get trashed.
     */
    SigDisableInterrupts();

    /*
     * Process the header of the Magic file:
     * make sure that this is a Magic-format file and that it
     * has the right technology.  Magic files have a first line
     * of "magic".
     */
    if (dbFgets(line, sizeof line, f) == NULL)
	goto badfile;

    if (strncmp(line, "magic", 5) != 0)
    {
	TxError("First line in file must be \"magic\"; instead saw: %s", line);
	goto badfile;
    }
    if (dbFgets(line, sizeof line, f) == NULL)
	goto badfile;

    if ((line[0] != '<') && (line[0] != '\0'))
    {
	if (sscanf(line, "tech %49s", tech) != 1)
	{
	    TxError("Malformed \"tech\" line: %s", line);
	    goto badfile;
	}
	if (strcmp(DBTechName, tech) != 0)
	{
	    TxError("Cell %s has technology \"%s\", but current "
			"technology is \"%s\"\n", cellDef->cd_name,
			tech, DBTechName);
	    if (ignoreTech)
		TxPrintf("Will attempt to read cell anyway.\n");
	    else
	    {
		TxError("Use command \"tech load\" if you want to switch"
			" technologies, or use\n");
		TxError("\"cellname delete %s\" and \"load %s -force\" to"
			" force the cell to load as technology %s\n",
			cellDef->cd_name, cellDef->cd_name, DBTechName);
		SigEnableInterrupts();
		return (FALSE);
	    }
	}
	if (dbFgets(line, sizeof line, f) == NULL)
	    goto badfile;
	
	if (line[0] == 'm')
	{
	    if (!strncmp(line, "magscale", 8))
	    {
		if (sscanf(line, "magscale %d %d", &n, &d) != 2)
		{
		    TxError("Expected two arguments to magscale;  ignoring\n");
		    n = d = 1;
		}
	    }
		
	    /* For backward compatibility, accept (and throw away) lines
	     * whose first word is "maxlabscale".
	     */
	    else if (!strncmp(line, "maxlabscale", 11))
		TxError("Deprecated keyword \"maxlabscale\" in input file.\n");
	    else 
		TxError("Expected magscale but got: %s", line);
	    if (dbFgets(line, sizeof line, f) == NULL)
		goto badfile;
	}
	if (line[0] == 't')
	{
	    if (sscanf(line, "timestamp %d", &cellStamp) != 1)
		TxError("Expected timestamp but got: %s", line);
	    if (dbFgets(line, sizeof line, f) == NULL)
		goto badfile;
	}
    }

    /*
     * Determine scalefactor between file and database.  Adjust scale of the
     * file and/or the database accordingly.
     */

    n *= DBLambda[1];
    d *= DBLambda[0];
    ReduceFraction(&n, &d);
    scaleLimit = CIFTechLimitScale(n, d);

    if (!scaleLimit && (d > 1))
    {
	CIFTechInputScale(1, d, TRUE);
	CIFTechOutputScale(1, d);
	DRCTechScale(1, d);
	PlowAfterTech();
	ExtTechScale(1, d);
	WireTechScale(1, d);
#ifdef LEF_MODULE
	LefTechScale(1, d);
#endif
#ifdef ROUTE_MODULE
	RtrTechScale(1, d);
	MZAfterTech();
	IRAfterTech();
#endif
	DBScaleEverything(d, 1);
	DBLambda[1] *= d;
	TxPrintf("Input cell %s scales magic internal geometry by factor of %d\n",
			cellDef->cd_name, d);
	d = 1;
    }
    if (n > 1)
    {
	TxPrintf("Scaled magic input cell %s geometry by factor of %d", 
		cellDef->cd_name, n);
	if (d > 1) 
	{
	    TxPrintf("/ %d\n", d);
	    TxError("Warning:  Geometry may be lost because internal grid"
			" cannot be reduced.\n");
	}
	else
	    TxPrintf("\n");
    }

    /*
     * Next, get the paint, subcells, and labels for this cell.
     * While we are generating paints to the database, we want
     * to disable the undo package.
     */
    rp = &r;
    UndoDisable();
    while (TRUE)
    {
	/*
	 * Read the header line to get the layer name, then read as
	 * many rectangles as are specified on consecutive lines.
	 * If not a layer header line, then it should be a cell
	 * use header line.
	 */
	if (sscanf(line, "<< %s >>", layername) != 1)
	{
	    if (!dbReadUse(cellDef, line, sizeof line, f, n, d))
		goto badfile;
	    continue;
	}

	TTMaskZero(&typemask);
	rmask = &typemask;
	type = DBTechNameType(layername);
	if (type < 0)
	{
	    /*
	     * Look for special layer names:
	     *		labels	   -- begins a list of labels and ports
	     *		elements   -- begins a list of elements
	     *		properties -- begins a list of properties
	     *		end	   -- marks the end of this file
	     */
	    if (!strcmp(layername, "labels"))
	    {
		if (!dbReadLabels(cellDef, line, sizeof line, f, n, d)) goto badfile;
		continue;
	    }
	    else if (!strcmp(layername, "elements"))
	    {
		if (!dbReadElements(cellDef, line, sizeof line, f, n, d)) goto badfile;
		continue;
	    }
	    else if (!strcmp(layername, "properties"))
	    {
		if (!dbReadProperties(cellDef, line, sizeof line, f, n, d)) goto badfile;
		continue;
	    }
	    else if (!strcmp(layername, "end")) goto done;
	    else
		DBTechNoisyNameMask(layername, rmask);

	    //	TxError("Unknown layer %s ignored in %s\n", layername,
	    //		cellDef->cd_name);
	}

	/*
	 * Record presence of material in cell.
	 */
	    
	if (DBPlane(type) > 0)
	{
	    if (type < DBNumUserLayers)
	    {
		TTMaskSetType(&cellDef->cd_types, type);
		TTMaskSetType(rmask, type);
	    }
	    else
	    {
		/* Separate stacked contact types into their components */
	        rmask = DBResidueMask(type);
		for (rtype = TT_SPACE + 1; rtype < DBNumUserLayers; rtype++)
		    if (TTMaskHasType(rmask, rtype))
			TTMaskSetType(&cellDef->cd_types, type);
	    }
	}

	/*
	 * The following loop is executed once for each line
	 * in the file beginning with 'r'.
	 */
nextrect:
	while (((c = getc(f)) == 'r') || (c == 't'))
	{
	    TileType dinfo;
	    int dir;
	    /*
	     * GetRect actually reads the rest of the line up to
	     * a trailing newline or EOF.
	     */
	    if (c == 't')
	    {
		if ((dir = GetRect(f, 3, rp, n, d)) == 0) goto badfile;
		dir >>= 1;
		dinfo = TT_DIAGONAL | ((dir & 0x2) ? TT_SIDE : 0) |
			((((dir & 0x2) >> 1) ^ (dir & 0x1)) ?
			TT_DIRECTION : 0);
	    }
            else  
	    {
		dinfo = 0;
		if (!GetRect(f, 4, rp, n, d)) goto badfile;
	    }

	    if ((++rectCount % rectReport == 0) && DBVerbose)
	    {
		TxPrintf("%s: %d rects\n", cellDef->cd_name, rectCount);
		fflush(stdout);
	    }

	    /*
	     * Only add a new rectangle if it is non-null, and if the
	     * layer is reasonable.
	     */
	    if (!GEO_RECTNULL(rp))
	    {
		/*------------------------------------------------------*/
		/* The complicated use of DBPaintPlane() has been	*/
		/* replaced with a simpler call to DBPaint().  HOWEVER	*/
		/* there are instances where this can cause unexpected	*/
		/* behavior.  Namely, magic-7.2 allows, e.g., m456c	*/
		/* painted over m3c;  this apparently works although	*/
		/* image on the metal4 plane cannot represent both	*/
		/* contact types.  magic-7.3 disallows this, so for the	*/
		/* same technology file (no stackable types), m3c gets	*/
		/* eliminated by the paint rules!  This condition is	*/
		/* left AS-IS.  Caveat end-user.			*/
		/*				   	Tim 7/8/04	*/
		/*------------------------------------------------------*/

		for (rtype = TT_SPACE + 1; rtype < DBNumUserLayers; rtype++)
		{
		    if (TTMaskHasType(rmask, rtype))
		    {
			loctype = rtype;
			if (dinfo & TT_SIDE) loctype <<= 14;
			loctype |= dinfo;
			DBPaint(cellDef, rp, loctype);
		    }
		}
	    }
	}

	/*
	 * Ignore comments.
	 * Note we use fgets() since we only want to discard this line.
	 */
	if (c == '#')
	{
	    (void) fgets(line, sizeof line, f);
	    goto nextrect;
	}

	/*
	 * We reach here if the first character on a line is not
	 * 'r', meaning that we have reached the end of this
	 * section of rectangles.
	 */
	if (c == EOF) goto badfile;
	line[0] = c;
	if (dbFgets(&line[1], sizeof line - 1, f) == NULL) goto badfile;
    }

done:

    cellDef->cd_flags &= ~(CDMODIFIED|CDBOXESCHANGED|CDGETNEWSTAMP);

    /*
     * Assign instance-ids to cell uses that didn't contain
     * explicit use identifiers.  Warn about duplicate instance
     * ids as well, changing these to unique ones.  We do this
     * here instead of on-the-fly during cell read-in, to avoid
     * an N**2 algorithm that blows up for large #s of subcells.
     */
    DBGenerateUniqueIds(cellDef, TRUE);

    /*
     * If the timestamp in the cell didn't match expectations,
     * notify the timestamp manager.  Note:  it's possible that
     * this cell is used only as the root of windows.  If that
     * is the case, then don't trigger a timestamp mismatch (we
     * can tell this by whether or not there are any parent uses
     * with non-null parent defs.  If the cell on disk had a zero
     * timestamp, then force the cell to be written out with a
     * correct timestamp.
     */
    if ((cellDef->cd_timestamp != cellStamp) || (cellStamp == 0))
    {
	CellUse *cu;
	for (cu = cellDef->cd_parents; cu != NULL; cu = cu->cu_nextuse)
	{
	    if (cu->cu_parent != NULL)
	    {
		DBStampMismatch(cellDef, &cellDef->cd_bbox);
		break;
	    }
	}
    }
    cellDef->cd_timestamp = cellStamp;
    if (cellStamp == 0)
    {
	TxError("\"%s\" has a zero timestamp; it should be written out\n",
	    cellDef->cd_name);
	TxError("    to establish a correct timestamp.\n");
	cellDef->cd_flags |= CDSTAMPSCHANGED|CDGETNEWSTAMP;
    }

    UndoEnable();
    DRCCheckThis(cellDef, TT_CHECKPAINT, (Rect *) NULL);
    SigEnableInterrupts();
    return (result);

badfile:
    TxError("File %s contained format error\n", cellDef->cd_name);
    DRCCheckThis(cellDef, TT_CHECKPAINT, (Rect *) NULL);
    result = FALSE;
    goto done;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBRemoveBackup() --
 *
 * Remove any crash backup file.  This routine is normally called either on
 * normal program exit, or after the successful read-in of a crash backup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If DBbackupFile is non-NULL, then memory is freed, and the backup
 *	file is removed from the filesystem temp directory.  Otherwise,
 *	nothing happens.
 *	
 * ----------------------------------------------------------------------------
 */

void
DBRemoveBackup()
{
    if (DBbackupFile != (char *)NULL)
    {
	unlink(DBbackupFile);
	freeMagic(DBbackupFile);
	DBbackupFile = (char *)NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBFileRecovery() --
 *
 * Get the name of the first backup file found in the /tmp directory,
 * prompt for action, and if action is "read", then load it.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Prompts for action.
 *	Loads the contents of the backup file into the database.
 *
 * ----------------------------------------------------------------------------
 */

void
DBFileRecovery(filename)
    char *filename;
{
    DIR *cwd;
    struct direct *dp;
    struct stat sbuf;
    uid_t userid = getuid();
    time_t recent = 0;
    char *snptr, *tempdir, tempname[256];
    int pid;
    static char *actionNames[] = {"read", "cancel", 0 };
    char *prompt;
    int action;

    if (DBbackupFile != NULL)
    {
	TxError("Error:  Backup file in use for current session.\n");
	return;
    }

    if (filename == NULL)
    {

	tempdir = getenv("TMPDIR");
	if (tempdir == NULL) tempdir = _PATH_TMP;

	cwd = opendir(tempdir);
	if (cwd == NULL) return;

	/* Find the most recent crash file in the temp directory */

	while ((dp = readdir(cwd)) != NULL)
	{
	    char *doslash = (tempdir[strlen(tempdir) - 1] == '/') ? "" : "/";
	    sprintf(tempname, "%s%s%s", tempdir, doslash, dp->d_name);
	    snptr = tempname + strlen(tempdir);
	    if (!strncmp(snptr, "MAG", 3))
	    {
		char *dotptr = strchr(snptr, '.');
	        pid = -1;
		if (dotptr && dotptr > snptr + 3)
		{
		    *dotptr = '\0';
		    if (sscanf(snptr + 3, "%d", &pid) != 1)
			pid = -1;
		    *dotptr = '.';
		}
		if ((!stat(tempname, &sbuf)) && (sbuf.st_uid == userid))
		{
		    if ((recent == 0) || (sbuf.st_ctime > recent))
		    {
			/* If the PID encoded in the name belongs to an	*/
			/* active process, then we should not try to	*/
			/* open it.					*/

			if (pid != -1)
			    if (SigCheckProcess(pid) == TRUE)
				continue;

			recent = sbuf.st_ctime;
			DBbackupFile = StrDup(&DBbackupFile, tempname);
		    }
		}  
	    } 
	}
	closedir(cwd);
    }
    else
    {
	DBbackupFile = StrDup(&DBbackupFile, filename);
	recent = 1;
    }

    if (recent > 0)
    {			/* There exists at least one temporary file	*/
			/* belonging to this user.  Ask to recover	*/
			/* the most recent one.				*/

	prompt = TxPrintString("Recover from backup file %s?", DBbackupFile);
	action = TxDialog(prompt, actionNames, 0);

	switch(action)
	{
	    case 0:	/* Read */
		if (DBReadBackup(DBbackupFile) == TRUE)
		    DBRemoveBackup();
		break;
	    case 1:	/* Cancel */
		break;
	}
    }

    /* Make sure we've cleared out the backup filename */

    if (DBbackupFile != NULL)
    {
	freeMagic(DBbackupFile);
	DBbackupFile = (char *)NULL;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBReadBackup --
 *
 * This file reads a backup file containing multiple cell definitions.
 *
 * Results:
 *	TRUE if the backup file was read successfully, FALSE otherwise.
 *
 * Side Effects:
 *	Side effects are the side effects caused by dbCellReadDef()
 *	(see above).  As many cells as are listed in the backup file
 *	are created and added to the database.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBReadBackup(name)
    char *name;		/* Name of the backup file */
{
    FILE *f;
    char *filename, *rootname, *chrptr;
    char line[256];
    CellDef *cellDef;
    bool result = TRUE;

    if ((f = PaOpen(name, "r", NULL, "", NULL, NULL)) == NULL)
    {
	TxError("Cannot open backup file \"%s\"\n", name);
	return FALSE;
    }

    if (dbFgets(line, sizeof(line), f) == NULL)
    {
	TxError("Bad backup file %s; can't restore!\n", name);
	return FALSE;
    }

    while (strncmp(line, "end", 3) != 0)
    {
	if (strncmp(line, "file", 4) == 0)
	{
	    filename = line + 4;

	    /* Remove any trailing return character */
	    chrptr = strrchr(filename, '\n');
	    if (chrptr != NULL) *chrptr = '\0';

	    /* Remove any trailing file extension */
	    chrptr = strstr(filename, ".mag");
	    if (chrptr != NULL) *chrptr = '\0';
	    rootname = strrchr(filename, '/');
	    if (rootname == NULL)
		rootname = filename;
	    else
		rootname++;

	    /* Remove any leading whitespace */
	    while (isspace(*rootname) && *rootname != '\0') rootname++;
	    if (strlen(rootname) == 0) return FALSE;

 	    cellDef = DBCellLookDef(rootname);
	    if (cellDef == (CellDef *)NULL)
		cellDef = DBCellNewDef(rootname, (char *)NULL);

	    cellDef->cd_flags &= ~CDNOTFOUND;
	    cellDef->cd_flags |= CDAVAILABLE;

	    if (dbCellReadDef(f, cellDef, filename, TRUE) == FALSE)
		return FALSE;

	    if (dbFgets(line, sizeof(line), f) == NULL)
	    {
		TxError("Error in backup file %s; partial restore only!\n",
			name);
		return FALSE;
	    }
	}
	else
	{
	    TxError("Error in backup file %s; expected keyword"
			" \"file\", got \"%s\"!\n", name, line);
	    return FALSE;
	}
    }
    chrptr = strrchr(line, '\n');
    if (chrptr > line + 4)
    {
	/* Remove the trailing return character */
	*chrptr = '\0';
	DBWreload(line + 4);
    }
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellRead --
 *
 * This is the wrapper for DBCellReadDef.  The routine has been divided into
 * parts so that a single backup file can be made and recovered, and in
 * preparation for allowing certain cell definitions to be in-lined into the
 * output file (such as polygonXXXXX cells generated by the gds read-in).
 *
 * Results:
 *	TRUE if the cell could be read successfully, FALSE
 *	otherwise.  If the cell is already read in, TRUE is
 *	also returned.
 *
 * Side Effects:
 *	If the cell is already marked as available (CDAVAILABLE), this
 *	routine	does nothing (has no side effects).
 *
 *	Otherwise, side effects are the side effects caused by
 *	dbCellReadDef() (see above).  In addition, the cell
 *	definition is marked as available.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellRead(cellDef, name, ignoreTech, errptr)
    CellDef *cellDef;	/* Pointer to definition of cell to be read in */
    char *name;		/* Name of file from which to read definition.
			 * If NULL, then use cellDef->cd_file; if that
			 * is NULL try the name of the cell.
			 */
    bool ignoreTech;	/* If FALSE then the technology of the file MUST
			 * match the current technology, or else the
			 * subroutine will return an error condition
			 * without reading anything.  If TRUE, a
			 * warning will be printed if the technology
			 * names do not match, but an attempt will be
			 * made to read the file anyway.
			 */
    int *errptr;	/* Copy of errno set by file reading routine
			 * is placed here, unless NULL.
			 */
{
    FILE *f;
    bool result;

    if (errptr != NULL) *errptr = 0;

    if (cellDef->cd_flags & CDAVAILABLE)
	result = TRUE;

    else if ((f = dbReadOpen(cellDef, name, TRUE, errptr)) == NULL)
	result = FALSE;

    else
    {
	result = (dbCellReadDef(f, cellDef, name, ignoreTech));

#ifdef FILE_LOCKS
	/* Close files that were locked by another user */
	if (cellDef->cd_fd == -1) fclose(f);
#else
	/* When using fcntl() to enforce file locks, we can't	*/
	/* close the file descriptor without losing the lock.	*/
	fclose(f);
#endif
    }
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbReadOpen --
 *
 * Open the file containing the cell we are going to read.
 * If a filename for the cell is specified ('name' is non-NULL),
 * we try to open it somewhere in the search path.  Otherwise,
 * we try the filename already associated with the cell, or the
 * name of the cell itself as the name of the file containing
 * the definition of the cell.
 *
 * If 'setFileName' is TRUE, then cellDef->cd_file will be updated
 * to point to the name of the file from which the cell was loaded.
 *
 * Results:
 *	Returns an open FILE * if successful, or NULL on error.
 *
 * Side effects:
 *	Opens a FILE.  Leaves cellDef->cd_flags marked as
 *	CDAVAILABLE, with the CDNOTFOUND bit clear, if we
 *	were successful.
 *
 * ----------------------------------------------------------------------------
 */

FILE *
dbReadOpen(cellDef, name, setFileName, errptr)
    CellDef *cellDef;	/* Def being read */
    char *name;		/* Name if specified, or NULL */
    bool setFileName;	/* If TRUE then cellDef->cd_file should be updated
			 * to point to the name of the file from which the
			 * cell was loaded.
			 */
    int *errptr;	/* Pointer to int to hold error value */
{
    FILE *f = NULL;
    char *filename, *realname;
    bool is_locked;

#ifdef FILE_LOCKS
    if (cellDef->cd_fd != -1)
    {
	close(cellDef->cd_fd);
	cellDef->cd_fd = -1;
    }
#endif

    if (errptr != NULL) *errptr = 0;	// No error, by default

    if (name != (char *) NULL)
    {
	f = PaLockOpen(name, "r", DBSuffix, Path,
			CellLibPath, &filename, &is_locked);
	if (errptr != NULL) *errptr = errno;
    }
    else if (cellDef->cd_file != (char *) NULL)
    {
	/* Do not send a name with a file extension to PaLockOpen(),
	 * otherwise that routine must handle it and then cannot
	 * distinguish between, say, cell.mag and cell.mag.mag.
	 */
	char *pptr, *sptr;

	sptr = strrchr(cellDef->cd_file, '/');
	if (sptr == NULL)
	    sptr = cellDef->cd_file;
	else
	    sptr++;

	pptr = strrchr(sptr, '.');
	if (pptr != NULL)
	    if (strcmp(pptr, DBSuffix)) pptr = NULL;
	else
	    *pptr = '\0';

	f = PaLockOpen(cellDef->cd_file, "r", DBSuffix, ".",
			(char *) NULL, &filename, &is_locked);

	if (errptr != NULL) *errptr = errno;
	if (pptr != NULL) *pptr = '.';	// Put it back where you found it!
    }
    else
    {
	f = PaLockOpen(cellDef->cd_name, "r", DBSuffix, Path,
			CellLibPath, &filename, &is_locked);
	if (errptr != NULL) *errptr = errno;
    }

    if (f == NULL)
    {
	/* Don't print another message if we've already tried to read it */
	if (cellDef->cd_flags & CDNOTFOUND)
	    return ((FILE *) NULL);

	if (name != (char *) NULL)
	    TxError("File %s%s couldn't be read\n", name, DBSuffix);
	else if (cellDef->cd_file != (char *) NULL)
	    TxError("File %s couldn't be read\n", cellDef->cd_file);
	else {
	    TxError("Cell %s couldn't be read\n", cellDef->cd_name);
	    realname = (char *) mallocMagic((unsigned) (strlen(cellDef->cd_name)
			+ strlen(DBSuffix) + 1));
	    (void) sprintf(realname, "%s%s", cellDef->cd_name, DBSuffix);
	    cellDef->cd_file = StrDup(&cellDef->cd_file, realname);
	}
	if (errptr) TxError("%s\n", strerror(*errptr));

	cellDef->cd_flags |= CDNOTFOUND;
	return ((FILE *) NULL);
    }

#ifdef FILE_LOCKS
    else
    {
	if (file_is_not_writeable(filename) || (is_locked == TRUE))
	{
	    cellDef->cd_flags |= CDNOEDIT;
	    if ((is_locked == FALSE) && DBVerbose)
		TxPrintf("Warning: cell <%s> from file %s is not writeable\n",
			cellDef->cd_name, filename);
	}
	else
	    cellDef->cd_flags &= ~CDNOEDIT;

	if (is_locked == FALSE)
	    cellDef->cd_fd = fileno(f);
	cellDef->cd_flags &= ~CDNOTFOUND;
    }
#else
    if (file_is_not_writeable(filename) && DBVerbose)
	TxPrintf("Warning: cell <%s> from file %s is not writeable\n",
		cellDef->cd_name, filename);
    TxFlushOut();

    cellDef->cd_flags &= ~CDNOTFOUND;
#endif
    if (setFileName)
	(void) StrDup(&cellDef->cd_file, filename);
    cellDef->cd_flags |= CDAVAILABLE;
    return (f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTestOpen --
 *
 * Check whether or not a database file can be found on disk.
 *
 * Results:
 *	Returns TRUE if available, FALSE if not.
 *
 * Side effects:
 *	Full pathname is returned in fullPath (if non-NULL)
 *
 * ----------------------------------------------------------------------------
 */

bool
DBTestOpen(name, fullPath)
    char *name;
    char **fullPath;
{
    FILE *f;

    f = PaLockOpen(name, "r", DBSuffix, Path, CellLibPath,
		fullPath, (bool *)NULL);

    if (f != NULL)
    {
	fclose(f);
	return TRUE;
    }
    return FALSE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbReadUse --
 *
 * Read a single cell use specification.  Create a new cell
 * use that is a child of cellDef.  Create the def for this
 * child use if it doesn't already exist.
 *
 * On input, 'line' contains the "use" line; on exit, 'line'
 * contains the next line in the input after the "use".
 *
 * Results:
 *	Returns TRUE normally, or FALSE on error or EOF.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbReadUse(cellDef, line, len, f, scalen, scaled)
    CellDef *cellDef;	/* Cell whose cells are being read */
    char *line;		/* Line containing "use ..." */
    int len;		/* Size of buffer pointed to by line */
    FILE *f;		/* Input file */
    int scalen;		/* Multiply values in file by this */
    int scaled;		/* Divide values in file by this */
{
    int xlo, xhi, ylo, yhi, xsep, ysep, childStamp;
    int absa, absb, absd, abse;
    char cellname[1024], useid[1024];
    CellUse *subCellUse;
    CellDef *subCellDef;
    Transform t;
    Rect r;
    bool locked;

    if (strncmp(line, "use", 3) != 0)
    {
	TxError("Expected \"use\" line but saw: %s", line);
	return (FALSE);
    }

    useid[0] = '\0';
    if (sscanf(line, "use %1023s %1023s", cellname, useid) < 1)
    {
	TxError("Malformed \"use\" line: %s", line);
	return (FALSE);
    }

    locked = (useid[0] == CULOCKCHAR) ? TRUE : FALSE;

    if (dbFgets(line, len, f) == NULL)
	return (FALSE);

    if (strncmp(line, "array", 5) == 0)
    {
	if (sscanf(line, "array %d %d %d %d %d %d",
		&xlo, &xhi, &xsep, &ylo, &yhi, &ysep) != 6)
	{
	    TxError("Malformed \"array\" line: %s", line);
	    return (FALSE);
	}
	if (scalen > 1)
	{
	    xsep *= scalen;
	    ysep *= scalen;
	}
	if (scaled > 1)
	{
	    xsep /= scaled;
	    ysep /= scaled;
	}
	if (dbFgets(line, len, f) == NULL)
	    return (FALSE);
    }
    else
    {
	xlo = ylo = 0;
	xhi = yhi = 0;
	xsep = ysep = 0;
    }

    if (strncmp(line, "timestamp", 9) == 0)
    {
	if (sscanf(line, "timestamp %d", &childStamp) != 1)
	{
	    TxError("Malformed \"timestamp\" line: %s", line);
	    return (FALSE);
	}
	if (dbFgets(line, len, f) == NULL)
	    return (FALSE);
    } else childStamp = 0;

    if (sscanf(line, "transform %d %d %d %d %d %d",
	    &t.t_a, &t.t_b, &t.t_c, &t.t_d, &t.t_e, &t.t_f) != 6)
    {
badTransform:
	TxError("Malformed or illegal \"transform\" line: %s", line);
	return (FALSE);
    }

    /*
     * Sanity check for transform.
     * Either a == e == 0 and both abs(b) == abs(d) == 1,
     *     or b == d == 0 and both abs(a) == abs(e) == 1.
     */
    if (t.t_a == 0)
    {
	absb = t.t_b > 0 ? t.t_b : -t.t_b;
	absd = t.t_d > 0 ? t.t_d : -t.t_d;
	if (t.t_e != 0 || absb != 1 || absd != 1)
	    goto badTransform;
    }
    else
    {
	absa = t.t_a > 0 ? t.t_a : -t.t_a;
	abse = t.t_e > 0 ? t.t_e : -t.t_e;
	if (t.t_b != 0 || t.t_d != 0 || absa != 1 || abse != 1)
	    goto badTransform;
    }

    if (dbFgets(line, len, f) == NULL)
	return (FALSE);

    if (sscanf(line, "box %d %d %d %d",
	    &r.r_xbot, &r.r_ybot, &r.r_xtop, &r.r_ytop) != 4)
    {
	TxError("Malformed \"box\" line: %s", line);
	return (FALSE);
    }

    if (scalen > 1)
    {
	t.t_c *= scalen;
	t.t_f *= scalen;
	r.r_xbot *= scalen;
	r.r_ybot *= scalen;
	r.r_xtop *= scalen;
	r.r_ytop *= scalen;
    }
    if (scaled > 1)
    {
	t.t_c /= scaled;
	t.t_f /= scaled;
	r.r_xbot /= scaled;
	r.r_ybot /= scaled;
	r.r_xtop /= scaled;
	r.r_ytop /= scaled;
    }

    /*
     * Set up cell use.
     * If the definition for this use has not been read in,
     * make a dummy one that's marked not available.  For now,
     * don't change the bounding box if the cell already exists
     * (we'll fix it below when handling timestamp problems).
     */
    subCellDef = DBCellLookDef(cellname);
    if (subCellDef == (CellDef *) NULL)
    {
	subCellDef = DBCellNewDef(cellname, (char *)NULL);
	subCellDef->cd_timestamp = childStamp;

	/* Make sure rectangle is non-degenerate */
	if (GEO_RECTNULL(&r))
	{
	    TxPrintf("Subcell has degenerate bounding box: %d %d %d %d\n",
		    r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
	    TxPrintf("Adjusting bounding box of subcell %s of %s",
		    cellname, cellDef->cd_name);
	    if (r.r_xtop <= r.r_xbot) r.r_xtop = r.r_xbot + 1;
	    if (r.r_ytop <= r.r_ybot) r.r_ytop = r.r_ybot + 1;
	    TxPrintf(" to %d %d %d %d\n",
		    r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
	}
	subCellDef->cd_bbox = r;
	subCellDef->cd_extended = r;
    }
    else if (DBIsAncestor(subCellDef, cellDef))
    {
	/*
	 * Watchout for attempts to create circular structures.
	 * If this happens, disregard the subcell.
	 */
	TxPrintf("Subcells are used circularly!\n");
	TxPrintf("Ignoring subcell %s of %s.\n", cellname,
	    cellDef->cd_name);
	goto nextLine;
    }

    
    subCellUse = DBCellNewUse(subCellDef, (useid[0]) ?
		((locked) ? useid + 1 : useid) : (char *) NULL);

    if (locked) subCellUse->cu_flags |= CU_LOCKED;
    /*
     * Instead of calling DBLinkCell for each cell, DBGenerateUniqueIds()
     * gets called for the entire cell at the end.
     */

    DBMakeArray(subCellUse, &GeoIdentityTransform,
			    xlo, ylo, xhi, yhi, xsep, ysep);
    DBSetTrans(subCellUse, &t);

    /*
     * Link the subcell into the parent.
     * This should be the only place where a cell use
     * gets created as part of the database, and not recorded
     * on the undo list (because undo is disabled while the
     * cell is being read in).
     */
    DBPlaceCell(subCellUse, cellDef);

    /*
     * Things get real tricky if the our guess about the
     * timestamp doesn't match the existing timestamp in
     * subCellDef.  This can be because (1) subCellDef has
     * been read in, so we're just a confused parent, or
     * (2) the cell hasn't been read in yet, so two parents
     * disagree, and it's not clear which is correct.  In
     * either event, call the timestamp manager with our guess
     * area, since it seems to be wrong, and in case (2) also
     * call the timestamp manager with the existing area, since
     * that parent is probably confused too.  If the cell isn't
     * available, set the timestamp to zero to force mismatches
     * forever until the cell gets read from disk.
     */
    if ((childStamp != subCellDef->cd_timestamp) || (childStamp == 0))
    {
	DBStampMismatch(subCellDef, &r);
	if (!(subCellDef->cd_flags & CDAVAILABLE))
	    subCellDef->cd_timestamp = 0;
	else DBStampMismatch(subCellDef, &subCellDef->cd_bbox);
    }

nextLine:
    return (dbFgets(line, len, f) != NULL);
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbReadProperties --
 *
 * Starting with the line << properties >>, read properties for 'cellDef'
 * up to the end of the properties section.  On exit, 'line' contains
 * the line that terminated the properties section, which will be either
 * a line of the form "<< something >>" or one beginning with a
 * character other than 'r', 'l', or 't'.
 *
 * Results:
 *	Returns TRUE normally, or FALSE on error or EOF.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbReadProperties(cellDef, line, len, f, scalen, scaled)
    CellDef *cellDef;	/* Cell whose elements are being read */
    char *line;		/* Line containing << elements >> */
    int len;		/* Size of buffer pointed to by line */
    FILE *f;		/* Input file */
    int scalen;		/* Scale up by this factor */
    int scaled;		/* Scale down by this factor */
{
    char propertyname[128], propertyvalue[2048], *storedvalue;
    int ntok;
    unsigned int noeditflag;

    /* Save CDNOEDIT flag if set, and clear it */
    noeditflag = cellDef->cd_flags & CDNOEDIT;
    cellDef->cd_flags &= ~CDNOEDIT;

    /* Get first element line */
    if (dbFgets(line, len, f) == NULL) return (FALSE);

    while (TRUE)
    {
	/* Skip blank lines */
	while (line[0] == '\0')
	    if (dbFgets(line, len, f) == NULL)
	    {
		cellDef->cd_flags |= noeditflag;
		return (TRUE);
	    }

	/* Stop when at end of properties section (currently, only "string"
	 * is defined)
	 */
	if (line[0] != 's') break;

	/*
	 * Properties may only be "string", for now.  This may be the only
	 * property type ever needed.
	 */
	if (line[0] == 's')
	{
	    if ((ntok = sscanf(line, "string %127s %2047[^\n]",
		    propertyname, propertyvalue)) != 2)
	    {
		TxError("Skipping bad property line: %s", line);
		goto nextproperty;
	    }

	    /* Go ahead and process the vendor GDS property */
	    if (!strcmp(propertyname, "GDS_FILE"))
		cellDef->cd_flags |= CDVENDORGDS;

	    /* Also process FIXED_BBOX property, but do not keep	*/
	    /* the property, as it should be regenerated on cell	*/
	    /* output from the current scale.				*/

	    if (!strcmp(propertyname, "FIXED_BBOX"))
	    {
		if (sscanf(propertyvalue, "%d %d %d %d",
			&(cellDef->cd_bbox.r_xbot),
			&(cellDef->cd_bbox.r_ybot),
			&(cellDef->cd_bbox.r_xtop),
			&(cellDef->cd_bbox.r_ytop)) != 4)
		{
		    TxError("Cannot read bounding box values in %s property",
				propertyname);
		    storedvalue = StrDup((char **)NULL, propertyvalue);
		    (void) DBPropPut(cellDef, propertyname, storedvalue);
		}
		else
		{
		    if (scalen > 1)
		    {
			cellDef->cd_bbox.r_xbot *= scalen;
			cellDef->cd_bbox.r_ybot *= scalen;
			cellDef->cd_bbox.r_xtop *= scalen;
			cellDef->cd_bbox.r_ytop *= scalen;
		    }
		    if (scaled > 1)
		    {
			cellDef->cd_bbox.r_xbot /= scaled;
			cellDef->cd_bbox.r_ybot /= scaled;
			cellDef->cd_bbox.r_xtop /= scaled;
			cellDef->cd_bbox.r_ytop /= scaled;
		    }
		    cellDef->cd_flags |= CDFIXEDBBOX;
		}
	    }
	    else
	    {
		storedvalue = StrDup((char **)NULL, propertyvalue);
		(void) DBPropPut(cellDef, propertyname, storedvalue);
	    }
	}

nextproperty:
	if (dbFgets(line, len, f) == NULL)
	    break;
    }

    cellDef->cd_flags |= noeditflag;
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbReadElements --
 *
 * Starting with the line << elements >>, read elements for 'cellDef'
 * up to the end of the elements section.  On exit, 'line' contains
 * the line that terminated the elements section, which will be either
 * a line of the form "<< something >>" or one beginning with a
 * character other than 'r', 'l', or 't'.
 *
 * Results:
 *	Returns TRUE normally, or FALSE on error or EOF.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbReadElements(cellDef, line, len, f, scalen, scaled)
    CellDef *cellDef;	/* Cell whose elements are being read */
    char *line;		/* Line containing << elements >> */
    int len;		/* Size of buffer pointed to by line */
    FILE *f;		/* Input file */
    int scalen;		/* Scale up by this factor */
    int scaled;		/* Scale down by this factor */
{
    char elementname[128], styles[1024], *text, flags[100];
    int istyle, ntok;
    Rect r;
    char *tstr, *nstr;

    /* Get first element line */
    if (dbFgets(line, len, f) == NULL) return (FALSE);

    while (TRUE)
    {
	/* Skip blank lines */
	while (line[0] == '\0')
	    if (dbFgets(line, len, f) == NULL)
		return (TRUE);

	/* Stop when at end of elements section (either paint or cell use) */
	if (line[0] != 'r' && line[0] != 'l' && line[0] != 't') break;

	/*
	 * Elements may be either rectangles, lines, or text.
	 */
	if (line[0] == 'r')
	{
	    if ((ntok = sscanf(line, "rectangle %127s %1023s %d %d %d %d %99[^\n]",
		    elementname, styles, &r.r_xbot, &r.r_ybot,
				&r.r_xtop, &r.r_ytop, flags)) < 6)
	    {
		TxError("Skipping bad \"rectangle\" element line: %s", line);
		goto nextelement;
	    }
	    if (scalen > 1)
	    {
		r.r_xbot *= scalen;
		r.r_ybot *= scalen;
		r.r_xtop *= scalen;
		r.r_ytop *= scalen;
	    }
	    if (scaled > 1)
	    {
		r.r_xbot /= scaled;
		r.r_ybot /= scaled;
		r.r_xtop /= scaled;
		r.r_ytop /= scaled;
	    }
	    (void) DBWElementAddRect(NULL, elementname, &r, cellDef, 0);
	    ntok -= 6;
	}
	else if (line[0] == 'l')
	{
	    if ((ntok = sscanf(line, "line %127s %1023s %d %d %d %d %99[^\n]",
		    elementname, styles, &r.r_xbot, &r.r_ybot,
				&r.r_xtop, &r.r_ytop, flags)) < 6)
	    {
		TxError("Skipping bad \"line\" element line: %s", line);
		goto nextelement;
	    }
	    if (scalen > 1)
	    {
		r.r_xbot *= scalen;
		r.r_ybot *= scalen;
		r.r_xtop *= scalen;
		r.r_ytop *= scalen;
	    }
	    if (scaled > 1)
	    {
		r.r_xbot /= scaled;
		r.r_ybot /= scaled;
		r.r_xtop /= scaled;
		r.r_ytop /= scaled;
	    }
	    (void) DBWElementAddLine(NULL, elementname, &r, cellDef, 0);
	    ntok -= 6;
	}
	else
	{
	    char *textend;

	    if (((ntok = sscanf(line, "text %127s %1023s %d %d",
			elementname, styles, &r.r_xbot, &r.r_ybot)) < 4)
			|| ((text = strchr(line, '"')) == NULL)
			|| ((textend = strrchr(line, '"')) == text))
	    {
		TxError("Skipping bad \"text\" element line: %s", line);
		goto nextelement;
	    }
	    text++;
	    *textend = '\0';

	    if (scalen > 1)
	    {
		r.r_xbot *= scalen;
		r.r_ybot *= scalen;
	    }
	    if (scaled > 1)
	    {
		r.r_xbot /= scaled;
		r.r_ybot /= scaled;
	    }
	    (void) DBWElementAddText(NULL, elementname, r.r_xbot, r.r_ybot, text,
				cellDef, 0);
	    *textend = '"';
	    ntok += sscanf(textend + 1, "%99[^\n]", flags);
	    ntok -= 4;
	}
	DBWElementParseFlags(NULL, elementname, "persistent");

	/* Set the style(s) for this element */
	tstr = styles;
	while ((nstr = strchr(tstr, ',')) != NULL)
	{
	    *nstr = '\0';
	    istyle = GrGetStyleFromName(tstr);
	    DBWElementStyle(NULL, elementname, istyle, TRUE);
	    *nstr = ',';
	    tstr = nstr + 1;
	}
	istyle = GrGetStyleFromName(tstr);
	DBWElementStyle(NULL, elementname, istyle, TRUE);

        /* Remove initial style 0, which was temporary */
	DBWElementStyle(NULL, elementname, 0, FALSE);

	/* Any remaining text should be a comma-separated list of flags */
	if (ntok > 0)
	{
	    tstr = flags;
	    while (isspace(*tstr)) tstr++;
	    while ((nstr = strchr(tstr, ',')) != NULL)
	    {
		*nstr = '\0';
		DBWElementParseFlags(NULL, elementname, tstr);
		*nstr = ',';
		tstr = nstr + 1;
	    }
	    DBWElementParseFlags(NULL, elementname, tstr);
	}

nextelement:
	if (dbFgets(line, len, f) == NULL)
	    break;
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbReadLabels --
 *
 * Starting with the line << labels >>, read labels for 'cellDef'
 * up until the end of a label section.  On exit, 'line' contains
 * the line that terminated the label section, which will be either
 * a line of the form "<< something >>" or one beginning with a
 * character other than 'r', 'l', 'f', or 'p'.
 *
 * Results:
 *	Returns TRUE normally, or FALSE on error or EOF.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbReadLabels(cellDef, line, len, f, scalen, scaled)
    CellDef *cellDef;	/* Cell whose labels are being read */
    char *line;		/* Line containing << labels >> */
    int len;		/* Size of buffer pointed to by line */
    FILE *f;		/* Input file */
    int scalen;		/* Scale up by this factor */
    int scaled;		/* Scale down by this factor */
{
    char layername[50], text[1024], port_use[50], port_class[50];
    TileType type;
    int ntok, orient, size, rotate, font, flags;
    Point offset;
    Rect r;
    char stickyflag[2];

    /* Get first label line */
    if (dbFgets(line, len, f) == NULL) return (FALSE);

    while (TRUE)
    {
	/* Skip blank lines */
	while (line[0] == '\0')
	    if (dbFgets(line, len, f) == NULL)
		return (TRUE);

	/* Stop when at end of labels section (either paint or cell use) */
	if (line[0] != 'r' && line[0] != 'l' && line[0] != 'p' &&
		line[0] != 'f') break;

	/*
	 * Labels may be either point labels or rectangular ones.
	 * Since each label is associated with a particular
	 * tile, the type of tile is also stored.
	 */
	if (line[0] == 'r')
	{
	    if (sscanf(line, "rlabel %*49s %1s", stickyflag) == 1)
	    {
		font = -1;
		if (*stickyflag == 's')
		{
		    flags = LABEL_STICKY;
		    if (sscanf(line, "rlabel %49s %c %d %d %d %d %d %99[^\n]",
			layername, &stickyflag[0], &r.r_xbot, &r.r_ybot,
			&r.r_xtop, &r.r_ytop, &orient, text) != 8)
		    {
			TxError("Skipping bad \"rlabel\" line: %s", line);
			goto nextlabel;
		    }
		}
		else
		{
		    flags = 0;
		    if (sscanf(line, "rlabel %49s %d %d %d %d %d %99[^\n]",
			layername, &r.r_xbot, &r.r_ybot, &r.r_xtop, &r.r_ytop,
			&orient, text) != 7)
		    {
			TxError("Skipping bad \"rlabel\" line: %s", line);
			goto nextlabel;
		    }
		}
	    }
	    else
	    {
		TxError("Skipping bad \"flabel\" line: %s", line);
		goto nextlabel;
	    }
	}
	else if (line[0] == 'f')
	{
	    char fontname[256];
	    if (sscanf(line, "flabel %*49s %1s", stickyflag) == 1)
	    {
		if (*stickyflag == 's')
		{
		    flags = LABEL_STICKY;
		    if (sscanf(line,
			    "flabel %49s %c %d %d %d %d %d %255s %d %d %d %d %99[^\n]",
			    layername, &stickyflag[0], &r.r_xbot, &r.r_ybot, &r.r_xtop,
			    &r.r_ytop, &orient, fontname, &size, &rotate, &offset.p_x,
			    &offset.p_y, text) != 13)
		    {
			TxError("Skipping bad \"flabel\" line: %s", line);
			goto nextlabel;
	    	    }
		}
		else
		{
		    flags = 0;
		    if (sscanf(line,
			    "flabel %49s %d %d %d %d %d %255s %d %d %d %d %99[^\n]",
			    layername, &r.r_xbot, &r.r_ybot, &r.r_xtop, &r.r_ytop,
			    &orient, fontname, &size, &rotate, &offset.p_x,
			    &offset.p_y, text) != 12)
		    {
			TxError("Skipping bad \"flabel\" line: %s", line);
			goto nextlabel;
	    	    }
		}
	    }
	    else
	    {
		TxError("Skipping bad \"flabel\" line: %s", line);
		goto nextlabel;
	    }

	    font = DBNameToFont(fontname);
	    if (font < -1) font = -1;	/* Force default font if font is unknown */
	}
	else if (line[0] == 'p')
	{
	    char ppos[5], *pptr;
	    int idx;
	    Label *lab;

	    if (((lab = cellDef->cd_lastLabel) == NULL) ||
			(lab->lab_flags & PORT_DIR_MASK) ||
			(((ntok = sscanf(line, "port %d %4s %49s %49s",
				&idx, ppos, port_use, port_class)) != 2) &&
			(ntok != 4)))
	    {
		TxError("Skipping bad \"port\" line: %s", line);
		goto nextlabel;
	    }
	    lab->lab_flags &= ~LABEL_STICKY;
	    lab->lab_flags |= idx;
	    for (pptr = &ppos[0]; *pptr != '\0'; pptr++)
	    {
		switch(*pptr)
		{
		    case 'n':
			lab->lab_flags |= PORT_DIR_NORTH;
			break;
		    case 's':
			lab->lab_flags |= PORT_DIR_SOUTH;
			break;
		    case 'e':
			lab->lab_flags |= PORT_DIR_EAST;
			break;
		    case 'w':
			lab->lab_flags |= PORT_DIR_WEST;
			break;
		}
	    }
	    if (ntok == 4)
	    {
		switch(port_use[0])
		{
		    case 's':
			lab->lab_flags |= PORT_USE_SIGNAL;
			break;
		    case 'a':
			lab->lab_flags |= PORT_USE_ANALOG;
			break;
		    case 'p':
			lab->lab_flags |= PORT_USE_POWER;
			break;
		    case 'g':
			lab->lab_flags |= PORT_USE_GROUND;
			break;
		    case 'c':
			lab->lab_flags |= PORT_USE_CLOCK;
			break;
		    case 'd':
			lab->lab_flags |= PORT_USE_DEFAULT;
			break;
		    default:
			TxError("Ignoring unknown \"port\" use: %s", port_use);
			break;
		}

		switch(port_class[0])
		{
		    case 'i':
			lab->lab_flags |= PORT_CLASS_INPUT;
			break;
		    case 'o':
			lab->lab_flags |= PORT_CLASS_OUTPUT;
			break;
		    case 't':
			lab->lab_flags |= PORT_CLASS_TRISTATE;
			break;
		    case 'b':
			lab->lab_flags |= PORT_CLASS_BIDIRECTIONAL;
			break;
		    case 'f':
			lab->lab_flags |= PORT_CLASS_FEEDTHROUGH;
			break;
		    case 'd':
			lab->lab_flags |= PORT_CLASS_DEFAULT;
			break;
		    default:
			TxError("Ignoring unknown \"port\" use: %s", port_use);
			break;
		}

	    }
	    goto nextlabel;
	}
	else	/* deprecated, retained for backward compatibility */
	{
	    if (sscanf(line, "label %49s %d %d %d %99[^\n]",
		    layername, &r.r_xbot, &r.r_ybot, &orient, text) != 5)
	    {
		TxError("Skipping bad \"label\" line: %s", line);
		goto nextlabel;
	    }
	    r.r_xtop = r.r_xbot;
	    r.r_ytop = r.r_ybot;
	    font = -1;
	}

	if (scalen > 1)
	{
	    r.r_xbot *= scalen;
	    r.r_ybot *= scalen;
	    r.r_xtop *= scalen;
	    r.r_ytop *= scalen;
	    if (font >= 0)
	    {
		size *= scalen;
		offset.p_x *= scalen;
		offset.p_y *= scalen;
	    }
	}
	if (scaled > 1)
	{
	    r.r_xbot /= scaled;
	    r.r_ybot /= scaled;
	    r.r_xtop /= scaled;
	    r.r_ytop /= scaled;
	    if (font >= 0)
	    {
		size /= scaled;
		offset.p_x /= scaled;
		offset.p_y /= scaled;
	    }
	}
	type = DBTechNameType(layername);
	if (type < 0)
	{
	    TileTypeBitMask rmask;

	    /* Check against alias hash table.  Names that are	*/
	    /* aliases for multiple types return the first type	*/
	    /* encountered (lowest mask bit number).		*/

	    type = DBTechNameTypes(layername, &rmask);
	}
	if (type < 0)
	{
	    TxError("Warning: label \"%s\" attached to unknown "
			"type \"%s\"\n", text, layername);
	    type = TT_SPACE;
	}
	else if (type >= DBNumUserLayers)
	{
	    TileTypeBitMask *rmask;
	    TileType rtype;

	    /* Don't stick labels on stacked types; choose the	*/
	    /* topmost (last, usually) residue contact type.	*/

	    rmask = DBResidueMask(type);
	    for (rtype = TT_SPACE + 1; rtype < DBNumUserLayers; rtype++)
		if (TTMaskHasType(rmask, rtype))
		    type = rtype;
	}
	if (font < 0)
	    DBPutLabel(cellDef, &r, orient, text, type, flags);
	else
	    DBPutFontLabel(cellDef, &r, font, size, rotate, &offset,
			orient, text, type, flags);

nextlabel:
	if (dbFgets(line, len, f) == NULL)
	    break;
    }

    return (TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbFgets --
 *
 * Like fgets(), but ignore lines beginning with a pound-sign.
 *
 * Results:
 *	Returns a pointer to 'line', or NULL on EOF.
 *
 * Side effects:
 *	Stores characters into 'line', terminating it with a
 *	NULL byte.
 *
 * ----------------------------------------------------------------------------
 */

char *
dbFgets(line, len, f)
    char *line;
    int len;
    FILE *f;
{
    char *cs;
    int l;
    int c;

    do
    {
	cs = line, l = len;
	while (--l > 0 && (c = getc(f)) != EOF)
	{
	    if (c != '\r') *cs++ = c;
	    if (c == '\n')
		break;
	}

	if (c == EOF && cs == line)
	    return (NULL);

	*cs = '\0';
    } while (line[0] == '#');

    return (line);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellFindScale --
 *
 * Results:
 *	Returns the greatest common factor of all the geometry in the cellDef.
 *	This includes tiles, labels, and cell use positions (from the cell use
 *	transform), bounding boxes, and array spacing.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
DBCellFindScale(cellDef)
    CellDef *cellDef;
{
    int dbFindGCFFunc(), dbFindCellGCFFunc();
    TileType type;
    TileTypeBitMask typeMask;
    int pNum;
    int ggcf;
    Label *lab;

    /* We only care about preventing magic from making the grid spacing finer	*/
    /* and finer.  If the current scale is lambda = 1 magic unit or larger,	*/
    /* then we simply set the scale factor to 1 and return.  Otherwise, do the	*/
    /* search to see if we can write this cell out at a coarser scale.		*/

    if (DBLambda[1] <= DBLambda[0]) return 1;

    /* Find greatest common factor of all geometry.  If this becomes 1, stop.	*/

    ggcf = DBLambda[1];
    for (type = TT_PAINTBASE; type < DBNumUserLayers; type++)
    {
	if ((pNum = DBPlane(type)) < 0)
	    continue;
	TTMaskSetOnlyType(&typeMask, type);
	if (DBSrPaintArea((Tile *) NULL, cellDef->cd_planes[pNum],
		&TiPlaneRect, &typeMask, dbFindGCFFunc, (ClientData) &ggcf))
	    return 1;
    }

    /* Now labels */
    if (cellDef->cd_labels)
    {
	for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
	{
	    if (lab->lab_rect.r_xtop % ggcf != 0)
		ggcf = FindGCF(lab->lab_rect.r_xtop, ggcf);
	    if (lab->lab_rect.r_xbot % ggcf != 0)
		ggcf = FindGCF(lab->lab_rect.r_xbot, ggcf);
	    if (lab->lab_rect.r_ytop % ggcf != 0)
		ggcf = FindGCF(lab->lab_rect.r_ytop, ggcf);
	    if (lab->lab_rect.r_ybot % ggcf != 0)
		ggcf = FindGCF(lab->lab_rect.r_ybot, ggcf);
	    if (ggcf == 1) return 1;
	}
    }

    /* Finally, cell uses */
	
    if (DBCellEnum(cellDef, dbFindCellGCFFunc, (ClientData) &ggcf))
	return 1;

    return ggcf;
}

int
dbFindGCFFunc(tile, ggcf)
    Tile *tile;
    int *ggcf;
{
    Rect r;

    TiToRect(tile, &r);

    if (r.r_xtop % (*ggcf) != 0)
	*ggcf = FindGCF(r.r_xtop, *ggcf);
    if (r.r_xbot % (*ggcf) != 0)
	*ggcf = FindGCF(r.r_xbot, *ggcf);
    if (r.r_ytop % (*ggcf) != 0)
	*ggcf = FindGCF(r.r_ytop, *ggcf);
    if (r.r_ybot % (*ggcf) != 0)
	*ggcf = FindGCF(r.r_ybot, *ggcf);
   
    return (*ggcf == 1) ? 1 : 0;
}

int
dbFindCellGCFFunc(cellUse, ggcf)
    CellUse *cellUse;	/* Cell use whose "call" is to be written to a file	*/
    int *ggcf;		/* Greatest common denominator for all geometry		*/
{
    Transform *t;
    Rect *b;

    t = &(cellUse->cu_transform);
    b = &(cellUse->cu_def->cd_bbox);

    /* Check transform translation values */
    if (t->t_c % (*ggcf) != 0)
	*ggcf = FindGCF(t->t_c, *ggcf);
    if (t->t_f % (*ggcf) != 0)
	*ggcf = FindGCF(t->t_f, *ggcf);

    /* Check bounding box */
    if (b->r_xtop % (*ggcf) != 0)
	*ggcf = FindGCF(b->r_xtop, *ggcf);
    if (b->r_xbot % (*ggcf) != 0)
	*ggcf = FindGCF(b->r_xbot, *ggcf);
    if (b->r_ytop % (*ggcf) != 0)
	*ggcf = FindGCF(b->r_ytop, *ggcf);
    if (b->r_ybot % (*ggcf) != 0)
	*ggcf = FindGCF(b->r_ybot, *ggcf);

    /* Check array separation, if arrayed */
    if ((cellUse->cu_xlo != cellUse->cu_xhi)
	    || (cellUse->cu_ylo != cellUse->cu_yhi))
    {
	if (cellUse->cu_xsep % (*ggcf) != 0)
	    *ggcf = FindGCF(cellUse->cu_xsep, *ggcf);
	if (cellUse->cu_ysep % (*ggcf) != 0)
	    *ggcf = FindGCF(cellUse->cu_ysep, *ggcf);
    }

    return (*ggcf == 1) ? 1 : 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellWriteFile --
 *
 * NOTE: this routine is usually not want you want.  Use DBCellWrite().
 *
 * Write out the paint for a cell to the specified file.
 * Mark the cell as having been written out.  Before calling this
 * procedure, the caller should make sure that timestamps have been
 * updated where appropriate.
 *
 * Results:
 *	TRUE if the cell could be written successfully, FALSE otherwise.
 *
 * Side effects:
 *	Writes a file to disk.
 * 	Does NOT close the file 'f', but does fflush(f) before
 * 	returning.
 *
 *	If successful, clears the CDMODIFIED, CDBOXESCHANGED,
 *	and CDSTAMPSCHANGED bits in cellDef->cd_flags.
 *
 *	In the event of an error while writing out the cell,
 *	the external integer errno is set to the UNIX error
 *	encountered, and the above bits are not cleared in
 *	cellDef->cd_flags.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellWriteFile(cellDef, f)
    CellDef *cellDef;	/* Pointer to definition of cell to be written out */
    FILE *f;		/* The FILE to write to */
{
    int dbWritePaintFunc(), dbWriteCellFunc(), dbWritePropFunc();
    int dbClearCellFunc();
    Label *lab;
    struct writeArg arg;
    int pNum;
    TileType type, stype;
    TileTypeBitMask typeMask, *sMask;
    int reducer;
    char *estring;
    char lstring[256];

#define FPRINTF(f,s)\
{\
     if (fprintf(f,s) == EOF) goto ioerror;\
     DBFileOffset += strlen(s);\
}
#define FPRINTR(f,s)\
{\
     if (fprintf(f,s) == EOF) return 1;\
     DBFileOffset += strlen(s);\
}

    if (f == NULL) return FALSE;

    /* If interrupts are left enabled, a partial file could get written.
     * This is not good.
     */

    reducer = DBCellFindScale(cellDef);

    SigDisableInterrupts();
    DBFileOffset = 0;

    if (cellDef->cd_flags & CDGETNEWSTAMP)
	TxPrintf("Magic error: writing out-of-date timestamp for %s.\n",
	    cellDef->cd_name);

    {
    	 char headerstring[256];
	 if (DBLambda[0] == (DBLambda[1] / reducer))	/* Default scale */
	     sprintf(headerstring,"magic\ntech %s\ntimestamp %d\n",
	 		DBTechName,cellDef->cd_timestamp);
	 else
	     sprintf(headerstring,"magic\ntech %s\nmagscale %d %d\ntimestamp %d\n",
	 		DBTechName, DBLambda[0], DBLambda[1] / reducer,
			cellDef->cd_timestamp);
	 FPRINTF(f, headerstring);
    }

    /*
     * Output the paint of the cell.
     * Note that we only output up to the last layer appearing
     * in the technology file (DBNumUserLayers-1).  Automatically
     * generated stacked contact types are added to typeMask and
     * will be decomposed into the residue appropriate for the
     * plane being searched.
     */

    arg.wa_file = f;
    arg.wa_reducer = reducer;
    for (type = TT_PAINTBASE; type < DBNumUserLayers; type++)
    {
	if ((pNum = DBPlane(type)) < 0)
	    continue;
	arg.wa_found = FALSE;
	arg.wa_type = type;
	arg.wa_plane = pNum;
	TTMaskSetOnlyType(&typeMask, type);

	/* Add to the mask all generated (stacking) types which	*/
	/* have this type as a residue.				*/

	for (stype = DBNumUserLayers; stype < DBNumTypes; stype++)
	{
	    sMask = DBResidueMask(stype);
	    if (TTMaskHasType(sMask, type))
		TTMaskSetType(&typeMask, stype);
	}

	if (DBSrPaintArea((Tile *) NULL, cellDef->cd_planes[pNum],
		&TiPlaneRect, &typeMask, dbWritePaintFunc, (ClientData) &arg))
	    goto ioerror;
    }

    /* Now the cell uses */
    if (DBCellEnum(cellDef, dbWriteCellFunc, (ClientData) &arg))
	goto ioerror;

    /* Clear flags set in dbWriteCellFunc */
    DBCellEnum(cellDef, dbClearCellFunc, (ClientData)NULL);

    /* Now labels */
    if (cellDef->cd_labels)
    {
	FPRINTF(f, "<< labels >>\n");
	for (lab = cellDef->cd_labels; lab; lab = lab->lab_next)
	{
	    if (strlen(lab->lab_text) == 0) continue;	// Shouldn't happen
	    if (lab->lab_font < 0)
	    {
		sprintf(lstring, "rlabel %s %s%d %d %d %d %d %s\n",
			DBTypeLongName(lab->lab_type),
			((lab->lab_flags & LABEL_STICKY) ? "s " : ""),
			lab->lab_rect.r_xbot / reducer,
			lab->lab_rect.r_ybot / reducer,
			lab->lab_rect.r_xtop / reducer,
			lab->lab_rect.r_ytop / reducer,
			lab->lab_just, lab->lab_text);
	    }
	    else
	    {
		sprintf(lstring, "flabel %s %s%d %d %d %d %d %s %d %d %d %d %s\n",
			DBTypeLongName(lab->lab_type),
			((lab->lab_flags & LABEL_STICKY) ? "s " : ""),
			lab->lab_rect.r_xbot / reducer,
			lab->lab_rect.r_ybot / reducer,
			lab->lab_rect.r_xtop / reducer,
			lab->lab_rect.r_ytop / reducer,
			lab->lab_just, DBFontList[lab->lab_font]->mf_name,
			lab->lab_size / reducer, lab->lab_rotate,
			lab->lab_offset.p_x / reducer,
			lab->lab_offset.p_y / reducer, lab->lab_text);
	    }
	    FPRINTF(f, lstring);
	    if (lab->lab_flags & PORT_DIR_MASK)
	    {
		char ppos[5];

		ppos[0] = '\0';
		if (lab->lab_flags & PORT_DIR_NORTH) strcat(ppos, "n");
		if (lab->lab_flags & PORT_DIR_SOUTH) strcat(ppos, "s");
		if (lab->lab_flags & PORT_DIR_EAST) strcat(ppos, "e");
		if (lab->lab_flags & PORT_DIR_WEST) strcat(ppos, "w");
		sprintf(lstring, "port %d %s", lab->lab_flags & PORT_NUM_MASK,
			ppos);

		if (lab->lab_flags & (PORT_USE_MASK | PORT_CLASS_MASK))
		{
		    switch (lab->lab_flags & PORT_USE_MASK)
		    {
			case PORT_USE_SIGNAL:
			    strcat(lstring, " signal");
			    break;
			case PORT_USE_ANALOG:
			    strcat(lstring, " analog");
			    break;
			case PORT_USE_POWER:
			    strcat(lstring, " power");
			    break;
			case PORT_USE_GROUND:
			    strcat(lstring, " ground");
			    break;
			case PORT_USE_CLOCK:
			    strcat(lstring, " clock");
			    break;
			case PORT_USE_DEFAULT:
			    strcat(lstring, " default");
			    break;
		    }

		    switch (lab->lab_flags & PORT_CLASS_MASK)
		    {
			case PORT_CLASS_INPUT:
			    strcat(lstring, " input");
			    break;
			case PORT_CLASS_OUTPUT:
			    strcat(lstring, " output");
			    break;
			case PORT_CLASS_TRISTATE:
			    strcat(lstring, " tristate");
			    break;
			case PORT_CLASS_BIDIRECTIONAL:
			    strcat(lstring, " bidirectional");
			    break;
			case PORT_CLASS_FEEDTHROUGH:
			    strcat(lstring, " feedthrough");
			    break;
			case PORT_CLASS_DEFAULT:
			    strcat(lstring, " default");
			    break;
		    }
		}
		strcat(lstring, "\n");
		FPRINTF(f, lstring); 
	    }
	}
    }

    /* Now any persistent elements */
    estring = DBWPrintElements(cellDef, DBW_ELEMENT_PERSISTENT);
    if (estring != NULL)
    {
	FPRINTF(f, "<< elements >>\n");
	FPRINTF(f, estring);
	freeMagic(estring);
    }

    /* And any properties */
    if (cellDef->cd_props != (ClientData)NULL)
    {
	FPRINTF(f, "<< properties >>\n");
	DBPropEnum(cellDef, dbWritePropFunc, (ClientData)f);
    }

    /* Fixed bounding box goes into a special property in output file	*/
    /* This is not kept internally as a property, so that it can be	*/
    /* read and written in the correct units without regard to internal	*/
    /* changes in scaling.						*/

    if (cellDef->cd_flags & CDFIXEDBBOX)
    {
	// If there were no explicit properties, then we need to
	// write the header

	if (cellDef->cd_props == (ClientData)NULL)
	    FPRINTF(f, "<< properties >>\n");

	sprintf(lstring, "string FIXED_BBOX %d %d %d %d\n",
		cellDef->cd_bbox.r_xbot / reducer,
		cellDef->cd_bbox.r_ybot / reducer,
		cellDef->cd_bbox.r_xtop / reducer,
		cellDef->cd_bbox.r_ytop / reducer);
	FPRINTF(f, lstring);
    }

    FPRINTF(f, "<< end >>\n");

    if (fflush(f) == EOF || ferror(f))
    {
ioerror:
	TxError("Warning: I/O error in writing file\n");
	SigEnableInterrupts();
	return (FALSE);
    }
    cellDef->cd_flags &= ~(CDMODIFIED|CDBOXESCHANGED|CDSTAMPSCHANGED);
    SigEnableInterrupts();
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbWritePropFunc --
 *
 * Filter function used to write out a single cell property.
 *
 * Results:
 *	Normally returns 0; returns 1 on I/O error.
 *
 * Side effects:
 *	Writes to the disk file.
 *
 * Warnings:
 *	This function assumes that all property values are strings!
 *	This is currently true;  if it changes in the future, this
 *	function will have to check each property against a list of
 *	expected property strings and output the value based on the
 *	known format of what it points to.
 *
 * ----------------------------------------------------------------------------
 */

int
dbWritePropFunc(key, value, cdata)
    char *key;
    ClientData value;
    ClientData cdata;
{
    FILE *f = (FILE *)cdata;
    char lstring[256];

    sprintf(lstring, "string %s %s\n", key, (char *)value);
    FPRINTR(f, lstring);

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBCellWrite --
 *
 * Write out the paint for a cell to its associated disk file.
 * Mark the cell as having been written out.  Before calling this
 * procedure, the caller should make sure that timestamps have been
 * updated where appropriate.
 * 
 * This code is fairly tricky to ensure that we never destroy the
 * original contents of a cell in the event of an I/O error.  We
 * try the following approaches in order.
 *
 *  1.	If we can create a temporary file in the same directory as the
 *	target cell, do so.  Then write to the temporary file and rename
 *	it to the target cell name.
 *
 *  2.	If we can't create the above temporary file, open the target
 *	cell for APPENDING, then write the new contents to the END of
 *	the file.  If successful, rewind the now-expanded file and
 *	overwrite the beginning of the file, then truncate it.
 *
 *
 * Results:
 *	TRUE if the cell could be written successfully, FALSE otherwise.
 *
 * Side effects:
 *	Writes a file to disk.
 *	If successful, clears the CDMODIFIED, CDBOXESCHANGED,
 *	and CDSTAMPSCHANGED bits in cellDef->cd_flags.
 *
 *	In the event of an error while writing out the cell,
 *	the external integer errno is set to the UNIX error
 *	encountered, and the above bits are not cleared in
 *	cellDef->cd_flags.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellWrite(cellDef, fileName)
    CellDef *cellDef;	/* Pointer to definition of cell to be written out */
    char *fileName;	/* If not NULL, name of file to write.  If NULL,
			 * the name associated with the CellDef is used
			 */
{
#define NAME_SIZE	1000
    char *template = ".XXXXXXX";
    char *realname, *tmpname, *expandname;
    char *cp1, *cp2;
    char expandbuf[NAME_SIZE];
    FILE *realf, *tmpf;
    int tmpres;
    struct stat statb;
    bool result, exists;

    result = FALSE;

    /*
     * Figure out the name of the file we will eventually write.
     */
    if (fileName)
    {
	realname = (char *) mallocMagic(strlen(fileName) + strlen(DBSuffix) + 1);
	(void) sprintf(realname, "%s%s", fileName, DBSuffix);

	/* Bug fix: 7/17/99, Michael D. Godfrey:  Forces		*/
	/* cd_name and cd_file to ALWAYS be the same, otherwise ugly	*/
	/* surprises can occur after saving a file as a different	*/
	/* filename.							*/

	cellDef->cd_file = StrDup(&cellDef->cd_file, realname);
    }
    else if (cellDef->cd_file)
    {
	realname = StrDup((char **) NULL, cellDef->cd_file);
    }
    else if (cellDef->cd_name)
    {
	realname = (char *) mallocMagic((unsigned) (strlen(cellDef->cd_name)
		+ strlen(DBSuffix) + 1));
	(void) sprintf(realname, "%s%s", cellDef->cd_name, DBSuffix);
    }
    else return (FALSE);

    /*
     * Expand the filename, removing the leading ~, if any.
     */
    expandname = expandbuf;
    cp1 = realname;
    cp2 = expandname;
    if (PaExpand(&cp1, &cp2, NAME_SIZE) == -1)
	expandname = realname;

    /*
     * If the locking logic works, this should not happen.  Files which
     * are not editable should never be set to MODIFIED.  But it is
     * better to be extra safe.  If it does happen, it would be good to
     * figure out why.
     */

    if(cellDef->cd_flags & CDNOEDIT)
    {
#ifdef FILE_LOCKS
	TxPrintf("File %s is locked by another user or "
		"is read_only and cannot be written\n", realname);
#else
	TxPrintf("File %s is read_only and cannot be written\n", realname);
#endif
	freeMagic(realname);
	return(FALSE);
    }

    /* Check if the .mag file exists.  If not, we don't need to deal	*/
    /* with temporary file names.					*/
    exists = (access(expandname, F_OK) == 0) ? TRUE : FALSE;

    if (exists)
    {
	/*
	 * Determine unique name for a temp file to write.
	 */
	tmpname = (char *) mallocMagic((unsigned) (strlen(expandname)
		+ strlen(template) + 1));
	(void) sprintf(tmpname, "%s%s", expandname, template);
	tmpres = mkstemp(tmpname);
	if (tmpres != -1)
	{
	    /* Assert the file permissions of the original file */
	    /* This will prevent the overwriting of a file that	*/
	    /* has write permissions blocked.			*/

	    if (stat(expandname, &statb) == 0)
		fchmod(tmpres, statb.st_mode & 0777);
	    close(tmpres);	/* We'll re-open it as a stream, below */
	}

	/* Critical: disable interrupts while we do our work */
	SigDisableInterrupts();

	/* Don't allow a write if the file isn't writeable, or if	*/
	/* mkstemp() returned an error condition.			*/

	if (file_is_not_writeable(expandname))
	{
	    if (tmpres != -1) unlink(tmpname);
	    perror(expandname);
	    goto cleanup;
	}
    }
    else
    {
	tmpname = StrDup((char **)NULL, expandname);
    }

    /*
     * See if we can create a temporary file in this directory.
     * If so, write to the temp file and then rename it after
     * we're done.
     */
    if (tmpf = fopen(tmpname, "w"))
    {
	result = DBCellWriteFile(cellDef, tmpf);
	(void) fclose(tmpf);
	tmpf = NULL;
	if (!result)
	{
	    /*
	     * Total loss -- just can't write the file.
	     * The error message is printed elsewhere.
	     */
	    (void) unlink(tmpname);
	    goto cleanup;
	}

#ifdef FILE_LOCKS
	if (cellDef->cd_fd != -1)
	{
	    close(cellDef->cd_fd);
	    cellDef->cd_fd = -1;
	}
#endif  

	/*
	 * The temp file is in good shape -- rename it to the real name,
	 * thereby completing the write.  The error below should NEVER
	 * normally happen.
	 */
	if (exists && (rename(tmpname, expandname) < 0))
	{
	    result = FALSE;
	    perror("rename");
	    TxError("ATTENTION: Magic was unable to rename file %s to %s.\n"
		    "If the file %s exists, it is the old copy of the cell %s.\n"
		    "The new copy is in the file %s.  Please copy this file\n"
		    "to a safe place before executing any more Magic commands.\n",
		    tmpname, expandname, expandname, cellDef->cd_name, tmpname);
	    goto cleanup;
	}

#ifdef FILE_LOCKS
	else
	    /* Re-aquire the lock on the new file by opening it. */
	    DBCellRead(cellDef, NULL, TRUE, NULL);
#endif  

    }
    else if (exists)
    {
	/*
	 * Couldn't create a temp file in this directory.  Instead, open
	 * the original file for APPENDING, write this cell (just to make
	 * sure the file is big enough), then rewind and write this cell
	 * again, and finally truncate the file.  The idea here is that
	 * by appending to realf, we don't trash the existing data, but
	 * do guarantee that there's enough space left to rewrite the
	 * file (in effect, we're pre-reserving space for it).
	 */
	realf = fopen(expandname, "a");
	if (realf == (FILE *) NULL)
	{
	    perror(expandname);
	    result = FALSE;
	    goto cleanup;
	}

	/* Remember the original length of the file for later truncation */
	(void) fstat(fileno(realf), &statb);

	/* Try to write by appending to the end of realf */
	if (!(result = DBCellWriteFile(cellDef, realf)))
	{
	    /* Total loss -- just can't write the file */
	    (void) fclose(realf);
	    realf = NULL;
	    (void) truncate(expandname, (long) statb.st_size);
	    goto cleanup;
	}

	/*
	 * Only try rewriting if the file wasn't zero-size to begin with.
	 * (If the file were zero-size, we're already done).
	 */
	if (statb.st_size > 0)
	{
	    rewind(realf);
	    result = DBCellWriteFile(cellDef, realf);
	    if (!result)
	    {
		/* Should NEVER happen */
		if (errno) perror(expandname);
		TxError("Something went wrong and the file %s was truncated\n",
			expandname);
		TxError("Try saving it in another file that is on a \n");
		TxError("filesystem where there is enough space!\n");
		(void) fclose(realf);
		realf = NULL;
		goto cleanup;
	    }

	    /* Successful writing the second time around */
	    statb.st_size = ftell(realf);
	    (void) fclose(realf);
	    realf = NULL;
	    (void) truncate(expandname, (long) statb.st_size);
	}
    }

    /* Everything worked so far. */

    (void) StrDup(&cellDef->cd_file, expandname);
    result = TRUE;
    {
	struct stat thestat;
	realf = fopen(expandname,"r");
	if (realf == NULL)
	{
	    cellDef->cd_flags |= CDMODIFIED;
	    TxError("Warning: Cannot open file for writing!\n");
	}
	else
	{
	    fstat(fileno(realf),&thestat);
	    if (thestat.st_size != DBFileOffset)
	    {
		cellDef->cd_flags |= CDMODIFIED;
		TxError("Warning: I/O error in writing file\n");
	    }
	    fclose(realf);
	}
	realf = NULL;
    }

cleanup:
    SigEnableInterrupts();
    freeMagic(realname);
    freeMagic(tmpname);
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbWritePaintFunc --
 *
 * Filter function used to write out a single paint tile.
 * Only writes out tiles of type arg->wa_type.
 * If the tile is the first encountered of its type, the header
 *	<< typename >>
 * is output.
 *
 * Results:
 *	Normally returns 0; returns 1 on I/O error.
 *
 * Side effects:
 *	Writes to the disk file.
 *
 * ----------------------------------------------------------------------------
 */

int
dbWritePaintFunc(tile, cdarg)
    Tile *tile;
    ClientData cdarg;
{
    char pstring[256];
    struct writeArg *arg = (struct writeArg *) cdarg;
    TileType type = TiGetType(tile);
    TileTypeBitMask *lMask, *rMask;

    int dir;

    if (IsSplit(tile))      
    { 
	lMask = DBResidueMask(SplitLeftType(tile));
	rMask = DBResidueMask(SplitRightType(tile));

	if ((SplitLeftType(tile) == arg->wa_type) ||
		((SplitLeftType(tile) >= DBNumUserLayers) &&
		TTMaskHasType(lMask, arg->wa_type)))
	{
	    type = arg->wa_type;
	    dir = 0x0;
	}
	else if ((SplitRightType(tile) == arg->wa_type) ||
		((SplitRightType(tile) >= DBNumUserLayers) &&
		TTMaskHasType(rMask, arg->wa_type)))
	{
	    type = arg->wa_type;
	    dir = 0x2;
	}
	else
	    return 0;
    }
    else if (type != arg->wa_type)
    {
	rMask = DBResidueMask(type);
	if ((type < DBNumUserLayers) ||
	    	(!TTMaskHasType(rMask, arg->wa_type)))
	    return 0;

	type = arg->wa_type;
    }

    if (!arg->wa_found)
    {
	sprintf(pstring, "<< %s >>\n", DBTypeLongName(type));
	FPRINTR(arg->wa_file,pstring);
	arg->wa_found = TRUE;
    }

    if (IsSplit(tile))
    {
	static char *pos_diag[] = {"nw", "sw", "se", "ne"};
	dir |= SplitDirection(tile);
	sprintf(pstring, "tri %d %d %d %d %s\n",
	    	LEFT(tile) / arg->wa_reducer, BOTTOM(tile) / arg->wa_reducer,
		RIGHT(tile) / arg->wa_reducer, TOP(tile) / arg->wa_reducer,
		pos_diag[dir]);
    }
    else
	sprintf(pstring, "rect %d %d %d %d\n",
	    LEFT(tile) / arg->wa_reducer, BOTTOM(tile) / arg->wa_reducer,
	    RIGHT(tile) / arg->wa_reducer, TOP(tile) / arg->wa_reducer);
    FPRINTR(arg->wa_file,pstring);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbClearCellFunc --
 *
 * Filter function that clears flags set by dbWriteCellFunc.
 *
 * Results:
 *	Always returns 0
 *
 * Side effects:
 *	Cell use flags changed.
 *
 * ----------------------------------------------------------------------------
 */

int
dbClearCellFunc(cellUse, cdarg)
    CellUse *cellUse;	/* Cell use */
    ClientData cdarg;	/* Not used */
{
    cellUse->cu_def->cd_flags &= ~CDVISITED;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbWriteCellFunc --
 *
 * Filter function used to write out a single cell use in the
 * subcell tile plane for a cell.
 *
 * Results:
 *	Normally returns 0; return 1 on I/O error
 *
 * Side effects:
 *	Writes to the disk file.
 *
 * ----------------------------------------------------------------------------
 */

int
dbWriteCellFunc(cellUse, cdarg)
    CellUse *cellUse;	/* Cell use whose "call" is to be written to a file */
    ClientData cdarg;
{
    struct writeArg *arg = (struct writeArg *) cdarg;
    Transform *t;
    Rect *b;
    char     cstring[256], *pathend;

    t = &(cellUse->cu_transform);
    b = &(cellUse->cu_def->cd_bbox);

    if (cellUse->cu_def->cd_file == NULL)
	pathend = NULL;
    else
    {
	pathend = strrchr(cellUse->cu_def->cd_file, '/');
	if (pathend != NULL) *pathend = '\0';
    }

    if ((cellUse->cu_def->cd_flags & CDVISITED) || (pathend == NULL))
    {
	sprintf(cstring, "use %s %c%s\n", cellUse->cu_def->cd_name,
		(cellUse->cu_flags & CU_LOCKED) ? CULOCKCHAR : ' ',
		cellUse->cu_id);
    }
    else
    {
	sprintf(cstring, "use %s %c%s %s\n", cellUse->cu_def->cd_name,
		(cellUse->cu_flags & CU_LOCKED) ? CULOCKCHAR : ' ',
		cellUse->cu_id, cellUse->cu_def->cd_file);
    }
    FPRINTR(arg->wa_file, cstring);

    cellUse->cu_def->cd_flags |= CDVISITED;
    if (pathend != NULL) *pathend = '/';

    if ((cellUse->cu_xlo != cellUse->cu_xhi)
	    || (cellUse->cu_ylo != cellUse->cu_yhi))
    {
	sprintf(cstring, "array %d %d %d %d %d %d\n",
		cellUse->cu_xlo, cellUse->cu_xhi, cellUse->cu_xsep / arg->wa_reducer,
		cellUse->cu_ylo, cellUse->cu_yhi, cellUse->cu_ysep / arg->wa_reducer);
	FPRINTR(arg->wa_file,cstring);
    }

    sprintf(cstring, "timestamp %d\n", cellUse->cu_def->cd_timestamp);
    FPRINTR(arg->wa_file,cstring)
    sprintf(cstring, "transform %d %d %d %d %d %d\n",
	    t->t_a, t->t_b, t->t_c / arg->wa_reducer,
	    t->t_d, t->t_e, t->t_f / arg->wa_reducer);
    FPRINTR(arg->wa_file,cstring)
    sprintf(cstring, "box %d %d %d %d\n",
	    b->r_xbot / arg->wa_reducer, b->r_ybot / arg->wa_reducer,
	    b->r_xtop / arg->wa_reducer, b->r_ytop / arg->wa_reducer);
    FPRINTR(arg->wa_file,cstring)
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBGetTech --
 *
 * 	Reads the first few lines of a file to find out what technology
 *	it is.
 *
 * Results:
 *	The return value is a pointer to a string containing the name
 *	of the technology of the file containing cell cellName.  NULL
 *	is returned if the file couldn't be read or isn't in Magic
 *	format.  The string is stored locally to this procedure and
 *	will be overwritten on the next call to this procedure.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
DBGetTech(cellName)
    char *cellName;			/* Name of cell whose technology
					 * is desired.
					 */
{
    FILE *f;
    static char line[512];
    char *p;

    f = PaOpen(cellName, "r", DBSuffix, Path, CellLibPath, (char **) NULL);
    if (f == NULL) return NULL;

    p = (char *) NULL;
    if (dbFgets(line, sizeof line - 1, f) == NULL) goto ret;
    if (strcmp(line, "magic\n") != 0) goto ret;
    if (dbFgets(line, sizeof line - 1, f) == NULL) goto ret;
    if (strncmp(line, "tech ", 5) != 0) goto ret;
    for (p = &line[5]; (*p != '\n') && (*p != 0); p++)
	/* Find the newline */;
    *p = 0;
    for (p = &line[5]; isspace(*p); p++)
	/* Find the tech name */;

ret:
    (void) fclose(f);
    f = NULL;
    return (p);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWriteBackup --
 *
 * Save all modified cells to "filename", if specified, or the current
 * setting of DBbackupFile, which is the current name of the crash
 * backup file.  If "filename" is NULL and no name has been set for
 * DBbackupFile, then DBbackupFile is generated as a unique filename
 * in the temp directory.  If "filename" is non-null, then DBbackupFile
 * is set to this name, erasing any previous value.  If "filename" is
 * an empty string, then the DBbackupFile reverts to NULL.
 *
 * Results:
 *	TRUE if the backup file was created, FALSE if an error was
 *	encountered.
 *
 * Side effects:
 *	Writes cells to disk (in a single file).
 *	Allocates and sets global variable DBbackupFile.
 *	Does NOT clear the modified bits.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBWriteBackup(filename)
    char *filename;
{
    FILE *f;
    int fd, pid;
    char *tempdir;
    MagWindow *mw;

    int dbWriteBackupFunc(), dbCheckModifiedCellsFunc();
    int flags = CDMODIFIED;
    int result;

    /* First check if there are any modified cells that need to be written */

    result = DBCellSrDefs(flags, dbCheckModifiedCellsFunc, (ClientData)NULL);
    if (result == 0) return TRUE;		/* Nothing to write */

    if (filename == NULL)
    {
	if (DBbackupFile == (char *)NULL)
	{
	    char *doslash, *template;

	    tempdir = getenv("TMPDIR");
	    if (tempdir == NULL) tempdir = _PATH_TMP;
	    template = (char *)mallocMagic(20 + strlen(tempdir));
	    pid = (int)getpid();

	    doslash = (tempdir[strlen(tempdir) - 1] == '/') ? "" : "/";
	    sprintf(template, "%s/MAG%d.XXXXXX", tempdir, pid);

	    fd = mkstemp(template);
	    if (fd == -1)
	    {
		TxError("Error generating backup file\n");
		freeMagic(template);
		return FALSE;
	    }
	    close(fd);
	    DBbackupFile = StrDup(&DBbackupFile, template);
	    freeMagic(template);
	    TxPrintf("Created database crash recovery file %s\n", DBbackupFile);
	}
	filename = DBbackupFile;
    }
    else
    {
	if (strlen(filename) == 0)
	{
	    DBbackupFile = StrDup(&DBbackupFile, (char *)NULL);
	    return TRUE;
	}
	DBbackupFile = StrDup(&DBbackupFile, filename);
	TxPrintf("Created database crash recovery file %s\n", DBbackupFile);
    }

    f = fopen(filename, "w");
    if (f == NULL)
    {
	TxError("Backup file %s cannot be opened for writing.\n", filename);
	return FALSE;
    }

    result = DBCellSrDefs(flags, dbWriteBackupFunc, (ClientData)f);

    /* End by printing the keyword "end" followed by the cell to load	*/
    /* into the first available window, so that we don't have a default	*/
    /* blank display after crash recovery.				*/

    mw = WindSearchWid(0);
    if (mw != NULL)
	fprintf(f, "end %s\n", ((CellUse *)mw->w_surfaceID)->cu_def->cd_name);
    else
	fprintf(f, "end\n");
    fclose(f);
    return TRUE;
}

/*
 * Filter function used by DBWriteBackup() above.
 * This function writes a single cell definition to the crash backup
 * file.  Only editable cells whose paint, labels, or subcells have
 * changed are considered.
 */

int
dbWriteBackupFunc(def, f)
    CellDef *def;	/* Pointer to CellDef to be saved */
    FILE *f;		/* File to append to */
{
    char *name = def->cd_file;
    int result, save_flags;

    if (def->cd_flags & (CDINTERNAL | CDNOEDIT | CDNOTFOUND)) return 0;
    else if (!(def->cd_flags & CDAVAILABLE)) return 0;

    if (name == NULL) name = def->cd_name;

    fprintf(f, "file %s\n", name);
    
    /* Save/restore flags such that the crash recovery file write does	*/
    /* *not* clear the CDMODIFIED, et al., bits				*/

    save_flags = def->cd_flags;
    def->cd_flags &= ~(CDGETNEWSTAMP);
    result = DBCellWriteFile(def, f);
    def->cd_flags = save_flags;
    return (result == TRUE) ? FALSE : TRUE;
}

/*
 * Filter function used by DBWriteBackup() above.
 * This function checks if at least one cell needs to be written to the
 * crash backup file.  Only editable cells whose paint, labels, or
 * subcells have changed are considered.
 */

int
dbCheckModifiedCellsFunc(def, cdata)
    CellDef *def;	/* Pointer to CellDef to be saved */
    ClientData cdata;	/* Unused */
{
    if (def->cd_flags & (CDINTERNAL | CDNOEDIT | CDNOTFOUND)) return 0;
    else if (!(def->cd_flags & CDAVAILABLE)) return 0;
    return 1;
}
