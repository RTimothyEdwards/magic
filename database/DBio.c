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
#include <sys/stat.h>
#include <sys/wait.h>

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#include <unistd.h>
#define direct dirent
#else
#include <sys/dir.h>
#endif

#include <sys/stat.h>
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/magic_zlib.h"
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

/* C99 compat */
#include "dbwind/dbwtech.h"
#include "cif/cif.h"
#include "lef/lef.h"
#include "commands/commands.h"
#include "graphics/graphics.h"
#include "irouter/irouter.h"
#include "mzrouter/mzrouter.h"
#include "router/router.h"
#include "wiring/wiring.h"
#include "extract/extract.h"

#ifndef _PATH_TMP
#define _PATH_TMP "/tmp"
#endif

extern char *Path;

#ifdef FILE_LOCKS
extern bool FileLocking;
#endif

/* Suffix for all Magic files */
char *DBSuffix = ".mag";

/* Magic units per lambda (2 integers, representing (n / d) */
int DBLambda[2] = {1, 1};

/* See database.h for verbose levels */
unsigned char DBVerbose = DB_VERBOSE_ALL;

/* Global name of backup file for this session */
static char *DBbackupFile = (char *)NULL;

/* Forward declarations */
char *dbFgets();
FILETYPE dbReadOpen();
int DBFileOffset;
bool dbReadLabels();
bool dbReadElements();
bool dbReadProperties();
bool dbReadUse();

#ifdef MAGIC_WRAPPER
/* Used to make a tag callback after loading a techfile */
extern int TagCallback();
#endif /* MAGIC_WRAPPER */

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

static int
path_is_dir(const char *dirname, const char *filename)
{
    struct stat statbuf;
    char path[PATH_MAX];
    const char *sep = filename ? "/" : "";
    if (!filename)
        filename = "";
    size_t n = snprintf(path, sizeof(path), "%s%s%s", dirname, sep, filename);
    ASSERT(n < sizeof(path), "path");
    if (n >= sizeof(path))
        return -1;
    int err = stat(path, &statbuf);
    if (err != 0)
        return 0;
    return S_ISDIR(statbuf.st_mode) ? 1 : 0;
}

/* Linked string record used to hold directory contents */

typedef struct _linkedDirent {
    struct dirent *ld_dirent;
    struct _linkedDirent *ld_next;
} LinkedDirent;

/*
 * ----------------------------------------------------------------------------
 *
 * DBSearchForTech --
 *
 *	Helper function for automatically discovering a technology used in a
 *	.mag file when reading.  This function will recursively search all
 *	directories rooted at "pathroot" looking for a file "techname" which
 *	must include the ".tech" extension.  If found, the path name is returned.
 *
 * Results:
 *	Pointer to a string containing the path name.  This is allocated so as
 *	not to be lost, and must be freed by the caller.
 *
 * Side effects:
 *	None.
 *
 * Notes:
 *	Algorithm refined 9/12/2023.  A directory which has the exact name of
 *	the technology is preferred (put at the front of the list) to any other
 *	directory name.  e.g., for techname "sky130A.tech", directory "sky130A"
 *	would be preferred to "sky130A_orig".
 *
 * ----------------------------------------------------------------------------
 */

char *
DBSearchForTech(techname, techroot, pathroot, level)
    char *techname;
    char *techroot;	/* techname without the ".tech" suffix */
    char *pathroot;
    int level;
{
    char *newpath, *found, *dptr;
    struct dirent *tdent;
    DIR *tdir;
    LinkedDirent *dlist = NULL, *ld, *ldlast = NULL;
    int dlen;

    /* Avoid potential infinite looping.  Any tech file should not be very  */
    /* far down the path.  10 levels is already excessive.		    */
    if (level > 10) return NULL;

    tdir = opendir(pathroot);
    if (tdir) {

	/* Read the directory contents of tdir */
	/* If any entry of tdir is equal to techroot, put it at the front */
	/* of the list. */

	while ((tdent = readdir(tdir)) != NULL)
	{
	    ld = (LinkedDirent *)mallocMagic(sizeof(LinkedDirent));
	    ld->ld_dirent = tdent;

	    if (!strcmp(tdent->d_name, techroot))
	    {
		/* Put at front of list */
		ld->ld_next = dlist;
		dlist = ld;
		if (ldlast == NULL)
		    ldlast = ld;
	    }
	    else if (strcmp(tdent->d_name, ".") && strcmp(tdent->d_name, ".."))
	    {
		/* Put at end of list */
		ld->ld_next = NULL;
		if (ldlast == NULL)
		    dlist = ld;
		else
		    ldlast->ld_next = ld;
		ldlast = ld;
	    }
	}

	for (ld = dlist; ld; ld = ld->ld_next)
	{
	    tdent = ld->ld_dirent;
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
            int is_dir = tdent->d_type == DT_DIR;
#else
            int is_dir = path_is_dir(pathroot, tdent->d_name) > 0; /* treat error as false */
#endif
	    if (!is_dir)
	    {
		if (!strcmp(tdent->d_name, techname))
		{
		    closedir(tdir);
		    free_magic1_t mm1 = freeMagic1_init();
		    for (ld = dlist; ld; ld = ld->ld_next)
			freeMagic1(&mm1, ld);
		    freeMagic1_end(&mm1);
		    return pathroot;
		}
	    }
	    else
	    {
		newpath = mallocMagic(strlen(pathroot) + strlen(tdent->d_name) + 3);
		sprintf(newpath, "%s/%s", pathroot, tdent->d_name);
		found = DBSearchForTech(techname, techroot, newpath, level + 1);
		if (found != newpath) freeMagic(newpath);
		if (found)
		{
		    closedir(tdir);
		    free_magic1_t mm1 = freeMagic1_init();
		    for (ld = dlist; ld; ld = ld->ld_next)
			freeMagic1(&mm1, ld);
		    freeMagic1_end(&mm1);
		    return found;
		}
	    }
	}
	closedir(tdir);
    }

    {
	free_magic1_t mm1 = freeMagic1_init();
	for (ld = dlist; ld; ld = ld->ld_next)
	    freeMagic1(&mm1, ld);
	freeMagic1_end(&mm1);
    }

    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBAddStandardCellPaths --
 *
 *	Search for .mag files in any directory below "pathptr", for any
 *	directory found containing .mag files, add that path to the search
 *	path for cells.
 *
 * Results:
 *	Number of paths added to CellLibPath.
 *
 * Side effects:
 *	May add paths to the CellLibPath.
 *
 * ----------------------------------------------------------------------------
 */

int
DBAddStandardCellPaths(pathptr, level)
   char *pathptr;
   int level;
{
    int paths = 0;
    struct dirent *tdent;
    char *newpath;
    DIR *tdir;
    bool magfound = FALSE;

    /* Avoid potential infinite looping.  Any tech file should not be very  */
    /* far down the path.  10 levels is already excessive.		    */
    if (level > 10) return 0;

    tdir = opendir(pathptr);
    if (tdir) {

	while ((tdent = readdir(tdir)) != NULL)
	{
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
            int is_dir = tdent->d_type == DT_DIR;
#else
            int is_dir = path_is_dir(pathptr, tdent->d_name) > 0; /* treat error as false */
#endif
	    if (is_dir &&
		    (strcmp(tdent->d_name, ".") && strcmp(tdent->d_name, "..")))
	    {
		/* Scan the directory contents of tdir for more subdirectories */
		newpath = mallocMagic(strlen(pathptr) + strlen(tdent->d_name) + 3);
		sprintf(newpath, "%s/%s", pathptr, tdent->d_name);
		paths += DBAddStandardCellPaths(newpath, level + 1);
		freeMagic(newpath);
	    }
	    else if (!is_dir)
	    {
		/* Scan the directory contents of tdir for .mag files */
		if (!strcmp(tdent->d_name + strlen(tdent->d_name) - 4, ".mag"))
		{
		    if (magfound == FALSE)
		    {
			PaAppend(&CellLibPath, pathptr);
			paths++;
			magfound = TRUE;
		    }
		}
	    }
	}
	closedir(tdir);
    }
    return paths;
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
 *	"tri <xbot> <ybot> <xtop> <ytop> <dir>" with <dir> indicating
 *	the direction of the corner made by the right triangle.
 *	If the split tile contains more than one type, separate entries
 *	are output for each.
 *
 *	6. Zero or more groups of lines describing cell uses.  Each group
 *	is of the form
 *		use <filename> <id> [<path>]
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
 *	was aware that the child changed.  The <path> is a full path
 *	to the location of the cell <id>.  <path> will be interpreted
 *	relative to the parent cell (the .mag file being read) if it
 *	does not begin with "/" or "~/".  Only the first instance of a
 *	cell needs to declare <path>.  If omitted completely, the cell
 *	is searched for in the search paths declared using the "addpath"
 *	command, which is the original, backwardly-compatible behavior.
 *	The new behavior using <path>, introduced in magic-8.2, implies
 *	that (apart from backwardly-compatible use) the search path only
 *	pertains to cells imported using "getcell", while cells named in
 *	database files are version controlled by specifically naming the
 *	path to the file.  If no cell <id> exists at <path>, then the
 *	fall-back method is to use the search paths, which allows some
 *	portability of layouts from place to place without breaking.
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
dbCellReadDef(f, cellDef, ignoreTech, dereference)
    FILETYPE f;		/* The file, already opened by the caller */
    CellDef *cellDef;	/* Pointer to definition of cell to be read in */
    bool ignoreTech;	/* If FALSE then the technology of the file MUST
			 * match the current technology, or else the
			 * subroutine will return an error condition
			 * without reading anything.  If TRUE, a
			 * warning will be printed if the technology
			 * names do not match, but an attempt will be
			 * made to read the file anyway.
			 */
    bool dereference;	/* If TRUE, ignore path references in the input */
{
    int cellStamp = 0, rectCount = 0, rectReport = 10000;
    char line[2048], tech[50], layername[50];
    PaintResultType *ptable;
    bool result = TRUE, scaleLimit = FALSE, has_mismatch;
    Rect *rp;
    int c;
    TileType type, rtype, loctype;
    TileTypeBitMask *rmask, typemask;
    Plane *plane;
    Rect r;
    int n = 1, d = 1;
    HashTable dbUseTable;
    bool needcleanup = FALSE;
    bool hasrcfile = FALSE;

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
		/* If no cells are currently in memory, then make an
		 * attempt to find the technology associated with the
		 * layout and load it.
		 */

		if (!CmdCheckForPaintFunc())
		{
		    /* Places to check for a technology:  In the PDK_ROOT
		     * (PDKROOT) directory, PDK_PATH (PDKPATH) from environment
		     * variables, and CAD_ROOT from Tcl variables;  the open_pdks
		     * default install path /usr/local/share/pdk/, and magic's install
		     * path.  For CAD_ROOT the variable is expected to point to
		     * a path containing the techfile.  For PDK_PATH and PDK_ROOT,
		     * search the directory tree for any subdirectory called
		     * magic/ and look for a compatible techfile there.
		     */
		    char *found = NULL;
		    char *sptr, *string, *techfullname;
    
		    techfullname = mallocMagic(strlen(tech) + 6);
		    sprintf(techfullname, "%s.tech", tech);

		    string = getenv("PDK_PATH");
		    if (string)
			found = DBSearchForTech(techfullname, tech, string, 0);
		    if (!found)
		    {
			string = getenv("PDKPATH");
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		    if (!found)
		    {
			string = getenv("PDK_ROOT");
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		    if (!found)
		    {
			string = getenv("PDKROOT");
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		    if (!found)
		    {
			found = DBSearchForTech(techfullname, tech,
					"/usr/local/share/pdk", 0);
		    }
#ifdef MAGIC_WRAPPER
		    /* Additional checks for PDK_PATH, etc., as Tcl variables.	*/
		    /* This is unlikely, as they would have to be set in a	*/
		    /* startup file, and a startup file is likely to just load	*/
		    /* the technology itself.					*/
		    if (!found)
		    {
			string = (char *)Tcl_GetVar(magicinterp, "PDK_ROOT",
				    TCL_GLOBAL_ONLY);
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		    if (!found)
		    {
			string = (char *)Tcl_GetVar(magicinterp, "PDKROOT",
				    TCL_GLOBAL_ONLY);
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		    if (!found)
		    {
			string = (char *)Tcl_GetVar(magicinterp, "PDK_PATH",
				    TCL_GLOBAL_ONLY);
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		    if (!found)
		    {
			string = (char *)Tcl_GetVar(magicinterp, "PDKPATH",
				    TCL_GLOBAL_ONLY);
			if (string)
			    found = DBSearchForTech(techfullname, tech, string, 0);
		    }
		
		    /* Check for a ".magicrc" file in the same directory */
		    /* as ".tech" and source it	first.			 */

		    if (found)
		    {
			char *rcpath;
			FILE *tmpf;

			rcpath = (char *)mallocMagic(strlen(found) + strlen(tech)
					+ 10);
			sprintf(rcpath, "%s/%s.magicrc", found, tech);
			if ((tmpf = fopen(rcpath, "r")) != NULL)
			{
			    fclose(tmpf);
			    Tcl_EvalFile(magicinterp, rcpath);
			    hasrcfile = TRUE;
			}
			freeMagic(rcpath);
		    }
#endif

		    freeMagic(techfullname);
		    if (found)
			PaAppend(&SysLibPath, found);

		    TxError("Loading technology %s\n", tech);
		    result = TechLoad(tech, 0);
		    if (!result)
			TxError("Error in loading technology file\n");
		    else if (found)
		    {
			if ((sptr = strstr(found, "libs.tech")) != NULL)
			{
			    int paths = 0;

			    if (hasrcfile == FALSE)
			    {
				/* Additional automatic handling of open_pdks-  */
				/* style PDKs.  Append the libs.ref libraries   */
				/* to the cell search path.  Do this only if a	*/
				/* magicrc file was not associated with the PDK	*/
				/* as the magicrc file is expected to set the	*/
				/* search path.					*/
			    
				strcpy(sptr + 5, "ref");
				paths = DBAddStandardCellPaths(found, 0);
				if (paths > 0)
				    TxPrintf("Cell path is now \"%s\"\n", CellLibPath);
			    }
			}
			freeMagic(found);
		    }
#ifdef MAGIC_WRAPPER
		    if (result)
		    {
			/* Apply tag callbacks for "tech load" command */
			{
			    char *argv[2];
			    argv[0] = StrDup((char **)NULL, "tech");
			    argv[1] = StrDup((char **)NULL, "load");
			    TagCallback(magicinterp, NULL, 2, argv);
			    freeMagic(argv[1]);
			    freeMagic(argv[0]);
			}
		    }
#endif
		}
		if (strcmp(DBTechName, tech))
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
	if (DBVerbose >= DB_VERBOSE_ALL)
	    TxPrintf("Input cell %s scales magic internal geometry by factor of %d\n",
			cellDef->cd_name, d);
	d = 1;
    }
    if (n > 1)
    {
	if (DBVerbose >= DB_VERBOSE_ALL)
	    TxPrintf("Scaled magic input cell %s geometry by factor of %d",
			cellDef->cd_name, n);
	if (d > 1)
	{
	    if (DBVerbose >= DB_VERBOSE_ALL)
		TxPrintf("/ %d\n", d);
	    if (DBVerbose >= DB_VERBOSE_WARN)
		TxError("Warning:  Geometry may be lost because internal grid"
				" cannot be reduced.\n");
	}
	else if (DBVerbose >= DB_VERBOSE_ALL)
	    TxPrintf("\n");
    }

    /*
     * Next, get the paint, subcells, and labels for this cell.
     * While we are generating paints to the database, we want
     * to disable the undo package.
     */
    rp = &r;
    UndoDisable();
    HashInit(&dbUseTable, 32, HT_STRINGKEYS);
    needcleanup = TRUE;
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
	    if (!dbReadUse(cellDef, line, sizeof line, f, n, d,
			dereference, &dbUseTable))
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
	while (((c = FGETC(f)) == 'r') || (c == 't'))
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

	    if ((++rectCount % rectReport == 0) && (DBVerbose >= DB_VERBOSE_ALL))
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
	    (void) dbFgets(line, sizeof line, f);
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
    has_mismatch = FALSE;
    if ((cellDef->cd_timestamp != cellStamp) || (cellStamp == 0))
    {
	CellUse *cu;
	for (cu = cellDef->cd_parents; cu != NULL; cu = cu->cu_nextuse)
	{
	    if (cu->cu_parent != NULL)
	    {
		DBStampMismatch(cellDef, &cellDef->cd_bbox);
		has_mismatch = TRUE;
		break;
	    }
	}
    }
    /* Update timestamp flags */
    if (has_mismatch) DBFlagMismatches(cellDef);

    cellDef->cd_timestamp = cellStamp;
    if (cellStamp == 0)
    {
	TxError("\"%s\" has a zero timestamp; it should be written out\n",
	    cellDef->cd_name);
	TxError("    to establish a correct timestamp.\n");
	cellDef->cd_flags |= CDSTAMPSCHANGED|CDGETNEWSTAMP;
    }

    if (needcleanup) HashKill(&dbUseTable);
    UndoEnable();
    /* Disabled 3/16/2021.  Let <<checkpaint>> in file force a DRC check */
    /* DRCCheckThis(cellDef, TT_CHECKPAINT, (Rect *) NULL); */
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
    char *snptr, *tempdir, tempname[1024];
    int pid;
    static const char *actionNames[] = {"read", "cancel", 0 };
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
	    int n = snprintf(tempname, sizeof(tempname), "%s%s%s", tempdir, doslash, dp->d_name);
	    ASSERT(n < sizeof(tempname), "tempname");
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
			StrDup(&DBbackupFile, tempname);
		    }
		}
	    }
	}
	closedir(cwd);
    }
    else
    {
	StrDup(&DBbackupFile, filename);
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
    FILETYPE f;
    char *filename, *rootname, *chrptr;
    char line[256];
    CellDef *cellDef;
    bool result = TRUE;

    if ((f = PaZOpen(name, "r", NULL, "", NULL, NULL)) == NULL)
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
		cellDef = DBCellNewDef(rootname);

	    cellDef->cd_flags &= ~CDNOTFOUND;
	    cellDef->cd_flags |= CDAVAILABLE;

	    if (dbCellReadDef(f, cellDef, TRUE, FALSE) == FALSE)
		return FALSE;

	    if (dbFgets(line, sizeof(line), f) == NULL)
	    {
		TxError("Error in backup file %s; partial restore only!\n",
			name);
		return FALSE;
	    }
	    /* Update timestamp flags from dbCellReadDef() */
	    DBFlagMismatches(cellDef);
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
 * This is the wrapper for dbCellReadDef.  The routine has been divided into
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
DBCellRead(cellDef, ignoreTech, dereference, errptr)
    CellDef *cellDef;	/* Pointer to definition of cell to be read in */
    bool ignoreTech;	/* If FALSE then the technology of the file MUST
			 * match the current technology, or else the
			 * subroutine will return an error condition
			 * without reading anything.  If TRUE, a
			 * warning will be printed if the technology
			 * names do not match, but an attempt will be
			 * made to read the file anyway.
			 */
    bool dereference;	/* If TRUE then ignore path argument to cellDef */
    int *errptr;	/* Copy of errno set by file reading routine
			 * is placed here, unless NULL.
			 */
{
    FILETYPE f;
    bool result, usederef, locderef;

    if (errptr != NULL) *errptr = 0;

    /* NOTE: "dereference" indicates whether or not to dereference
     * the cellDef itself.  To determine if subcells of cellDef
     * should be dereferenced, use the CDDEREFERENCE flag in the
     * cellDef.
     */
    usederef = (cellDef->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
    /* "locderef" indicates whether or not to use the CDDEREFERENCE
     * for the cellDef itself.  If dereference is FALSE, then never
     * dereference.  Otherwise, follow the flag value.
     */
    locderef = (dereference == TRUE) ? usederef : FALSE;

    if (cellDef->cd_flags & CDAVAILABLE)
	result = TRUE;

    else if ((f = dbReadOpen(cellDef, TRUE, locderef, errptr)) == NULL)
	result = FALSE;

    else
    {
	result = (dbCellReadDef(f, cellDef, ignoreTech, usederef));

#ifdef FILE_LOCKS
	/* Close files that were locked by another user */
	if ((FileLocking == FALSE) || (cellDef->cd_fd == -2)) FCLOSE(f);
#else
	/* When using fcntl() to enforce file locks, we can't	*/
	/* close the file descriptor without losing the lock.	*/
	FCLOSE(f);
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
 * If a filename for the cell is being dereferenced,
 * we try to open it somewhere in the search path.  Otherwise,
 * we try the filename already associated with the cell, or the
 * name of the cell itself as the name of the file containing
 * the definition of the cell.
 *
 * If 'setFileName' is TRUE, then cellDef->cd_file will be updated
 * to point to the name of the file from which the cell was loaded.
 *
 * Results:
 *	Returns an open FILETYPE if successful, or NULL on error.
 *
 * Side effects:
 *	Opens a FILE.  Leaves cellDef->cd_flags marked as
 *	CDAVAILABLE, with the CDNOTFOUND bit clear, if we
 *	were successful.
 *
 * Notes:
 *	Global variable DBVerbose determines whether or not error
 *	messages are generated by this routine.  This can be controlled
 *	by "load -quiet" or "load -silent".
 *
 * ----------------------------------------------------------------------------
 */

FILETYPE
dbReadOpen(cellDef, setFileName, dereference, errptr)
    CellDef *cellDef;	/* Def being read */
    bool setFileName;	/* If TRUE then cellDef->cd_file should be updated
			 * to point to the name of the file from which the
			 * cell was loaded.
			 */
    bool dereference;	/* If dereferencing, try search paths first, and
			 * only if that fails, try the value in cd_file.
			 */
    int *errptr;	/* Pointer to int to hold error value */
{
    FILETYPE f = NULL;
    int fd;
    char *filename, *realname, *savename;
    bool is_locked;

#ifdef FILE_LOCKS
    if (cellDef->cd_fd >= 0)
    {
	close(cellDef->cd_fd);
	cellDef->cd_fd = -1;	/* Set to initial state */
    }
#endif

    if (errptr != NULL) *errptr = 0;	// No error, by default

    if (cellDef->cd_file != (char *) NULL)
    {
	/* Do not send a name with a file extension to PaLockZOpen(),
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
	{
	    if (strcmp(pptr, DBSuffix)) pptr = NULL;
	    else *pptr = '\0';
	}

	/* If dereferencing, then use search paths first */
	if (!dereference)
	    f = PaLockZOpen(cellDef->cd_file, "r", DBSuffix, ".",
			(char *) NULL, &filename, &is_locked, &fd);

	/* Fall back on the original method of using search paths. */

	if (f == NULL)
	{
	    f = PaLockZOpen(cellDef->cd_name, "r", DBSuffix, Path,
			CellLibPath, &filename, &is_locked, &fd);

	    if (f != NULL)
	    {
		/* NOTE:  May not want to present this as an error, as	*/
		/* it is a common technique to read files from, say, a	*/
		/* LEF file but not save them locally, and then expect	*/
		/* that the layout views will be picked up from		*/
		/* somewhere else in the search paths.			*/

		if (pptr != NULL) *pptr = '.';
		if (DBVerbose >= DB_VERBOSE_WARN)
		    if (!dereference)
			TxError("Warning:  Parent cell lists instance of \"%s\" at "
				"bad file path %s.\n",
				cellDef->cd_name, cellDef->cd_file);

		/* Write the new path to cd_file or else magic will	*/
		/* generate another error later.			*/
		StrDup(&cellDef->cd_file, filename);

		if (DBVerbose >= DB_VERBOSE_WARN)
		    if (!dereference)
		    {
			TxError("The cell exists in the search paths at %s.\n",
					filename);
			TxError("The discovered version will be used.\n");
		    }
	    }
	    else if (dereference)
	    {
		f = PaLockZOpen(cellDef->cd_file, "r", DBSuffix, ".",
			(char *) NULL, &filename, &is_locked, &fd);
		if (f != NULL)
		    if (DBVerbose)
			TxError("Warning:  Dereferenced cell \"%s\" not "
				"found in search paths;  using original "
				"location %s.\n",
				cellDef->cd_name,
				cellDef->cd_file);
	    }
	}

	if (errptr != NULL) *errptr = errno;
	if (pptr != NULL) *pptr = '.';	// Put it back where you found it!
    }
    else
    {
	f = PaLockZOpen(cellDef->cd_name, "r", DBSuffix, Path,
			CellLibPath, &filename, &is_locked, &fd);
	if (errptr != NULL) *errptr = errno;
    }

    if (f == NULL)
    {
	/* Don't print another message if we've already tried to read it */
	if (cellDef->cd_flags & CDNOTFOUND)
	    return ((FILETYPE) NULL);

	if (cellDef->cd_file != (char *) NULL)
	{
	    if (DBVerbose >= DB_VERBOSE_ERR)
	    	TxError("File %s couldn't be read\n", cellDef->cd_file);
	}
	else
	{
	    if (DBVerbose >= DB_VERBOSE_ERR)
		TxError("Cell %s couldn't be read\n", cellDef->cd_name);
	    realname = (char *) mallocMagic((unsigned) (strlen(cellDef->cd_name)
			+ strlen(DBSuffix) + 1));
	    (void) sprintf(realname, "%s%s", cellDef->cd_name, DBSuffix);
	    StrDup(&cellDef->cd_file, realname);
	}
	if (errptr && (DBVerbose >= DB_VERBOSE_ERR)) TxError("%s\n", strerror(*errptr));

	cellDef->cd_flags |= CDNOTFOUND;
	return ((FILETYPE) NULL);
    }

#ifdef FILE_LOCKS
    else
    {
	if (file_is_not_writeable(filename) || (is_locked == TRUE))
	{
	    cellDef->cd_flags |= CDNOEDIT;
	    if ((is_locked == FALSE) && (DBVerbose >= DB_VERBOSE_WARN))
		TxPrintf("Warning: cell <%s> from file %s is not writeable\n",
			cellDef->cd_name, filename);
	}
	else
	    cellDef->cd_flags &= ~CDNOEDIT;

	if (is_locked == TRUE)
	    cellDef->cd_fd = -2;	/* Indicates locked file */
	else
	    cellDef->cd_fd = fd;
	cellDef->cd_flags &= ~CDNOTFOUND;
    }
#else
    if (file_is_not_writeable(filename) && (DBVerbose >= DB_VERBOSE_WARN))
	TxPrintf("Warning: cell <%s> from file %s is not writeable\n",
		cellDef->cd_name, filename);
    TxFlushOut();

    cellDef->cd_flags &= ~CDNOTFOUND;
#endif
    if (setFileName)
    {
	/* Remove any ".mag" file extension */
	char *pptr = strrchr(filename, '.');
	if (pptr != NULL)
	    if (!strcmp(pptr, DBSuffix)) *pptr = '\0';

	(void) StrDup(&cellDef->cd_file, filename);
	if (DBVerbose >= DB_VERBOSE_ALL)
	{
	    char *sptr = strrchr(filename, '/');
	    if (sptr == NULL)
	        TxPrintf("Cell %s read from current working directory\n",
				cellDef->cd_name);
	    else
	    {
		*sptr = '\0';
	    	TxPrintf("Cell %s read from path %s\n", cellDef->cd_name, filename);
	    }
	}
    }
    else if (DBVerbose >= DB_VERBOSE_WARN)
    {
	TxPrintf("Warning:  Loaded cell %s but recorded file path is %s\n",
		filename, cellDef->cd_file);
    }
    cellDef->cd_flags |= CDAVAILABLE;
    return (f);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBOpenOnly --
 *
 * Form of dbReadOpen() used to check if a file is locked;  it does not
 * return a value.
 *
 * Return value:
 *	None.
 *
 * Side effects:
 *	See dbReadOpen() above.
 *
 * ----------------------------------------------------------------------------
 */

void
DBOpenOnly(cellDef, name, setFileName, errptr)
    CellDef *cellDef;	/* Def being read */
    char *name;		/* Name if specified, or NULL */
    bool setFileName;	/* If TRUE then cellDef->cd_file should be updated
			 * to point to the name of the file from which the
			 * cell was loaded.
			 */
    int *errptr;	/* Pointer to int to hold error value */
{
    dbReadOpen(cellDef, setFileName, FALSE, errptr);
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
    FILETYPE f;

    f = PaLockZOpen(name, "r", DBSuffix, Path, CellLibPath,
		fullPath, (bool *)NULL, (int *)NULL);

    if (f != NULL)
    {
	FCLOSE(f);
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
dbReadUse(cellDef, line, len, f, scalen, scaled, dereference, dbUseTable)
    CellDef *cellDef;	/* Cell whose cells are being read */
    char *line;		/* Line containing "use ..." */
    int len;		/* Size of buffer pointed to by line */
    FILETYPE f;		/* Input file */
    int scalen;		/* Multiply values in file by this */
    int scaled;		/* Divide values in file by this */
    bool dereference;	/* If TRUE, ignore path references */
    HashTable *dbUseTable;  /* Hash table of instances seen in this file */
{
    int xlo, xhi, ylo, yhi, xsep, ysep, childStamp;
    int absa, absb, absd, abse, nconv;
    char cellname[1024], useid[1024], path[1024];
    CellUse *subCellUse;
    CellDef *subCellDef;
    Transform t;
    Rect r;
    bool locked, firstUse;
    char *slashptr, *pathptr;

    if (strncmp(line, "use", 3) != 0)
    {
	TxError("Expected \"use\" line but saw: %s", line);
	return (FALSE);
    }

    useid[0] = '\0';
    nconv = sscanf(line, "use %1023s %1023s %1023s", cellname, useid, path);
    if (nconv < 1)
    {
	TxError("Malformed \"use\" line: %s", line);
	return (FALSE);
    }
    /* Make sure useid[0] is an empty string if no useid was provided */
    if (nconv == 1) useid[0] = '\0';
    if (nconv <= 2) path[0] = '\0';

    pathptr = &path[0];
    while (*pathptr == ' ' || *pathptr == '\t') pathptr++;

    /* NOTE:  Removed the truncating of the path when dereferencing,	*/
    /* 3/20/2023.  This allows the original location to be recovered if	*/
    /* dereferencing is used, but the cell does not exist in the search	*/
    /* paths and only exists in the original location.			*/

    if (*pathptr == '\n') *pathptr = '\0';

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

    /* Flag if this is the first time the cell is used in the file,
     * so that we can expect that additional instances will not have
     * path information given.
     */
    firstUse = (HashLookOnly(dbUseTable, cellname) == NULL) ? TRUE : FALSE;
    if (firstUse) HashFind(dbUseTable, cellname);

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
	subCellDef = DBCellNewDef(cellname);
	subCellDef->cd_timestamp = childStamp;

	/* Make sure rectangle is non-degenerate */
	if (GEO_RECTNULL(&r))
	{
	    if (firstUse == TRUE)
	    {
		TxPrintf("Subcell has degenerate bounding box: %d %d %d %d\n",
			r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
		TxPrintf("Adjusting bounding box of subcell %s of %s",
			cellname, cellDef->cd_name);
	    }
	    if (r.r_xtop <= r.r_xbot) r.r_xtop = r.r_xbot + 1;
	    if (r.r_ytop <= r.r_ybot) r.r_ytop = r.r_ybot + 1;
	    if (firstUse == TRUE)
	    {
		TxPrintf(" to %d %d %d %d\n",
			r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop);
	    }
	}
	subCellDef->cd_bbox = r;
	subCellDef->cd_extended = r;
    }
    else if (DBIsAncestor(subCellDef, cellDef))
    {
	/*
	 * Watch out for attempts to create circular structures.
	 * If this happens, disregard the subcell.
	 */
	if (firstUse == TRUE)
	{
	    TxPrintf("Subcells are used circularly!\n");
	    TxPrintf("Ignoring subcell %s of %s.\n", cellname,
		    cellDef->cd_name);
	}
	goto nextLine;
    }

#ifdef MAGIC_WRAPPER
    /* If path starts with '$' then check for a possible Tcl variable	*/
    /* replacement.							*/

    if (*pathptr == '$')
    {
	char *varstart, *varend, savechar, *tvar;

	varstart = pathptr + 1;
	if (*varstart == '{') varstart++;
	varend = varstart + 1;
	while (*varend != '\0' && *varend != '}' && *varend != '/'
			&& *varend != '\n' && *varend != ' ') varend++;
	savechar = *varend;
	*varend = '\0';

	tvar = (char *)Tcl_GetVar(magicinterp, varstart, TCL_GLOBAL_ONLY);
	*varend = savechar;
	if (savechar == '}') varend++;
	if (tvar)
	{
	    memmove(pathptr + strlen(tvar), varend, strlen(varend) + 1);
	    memmove(pathptr, tvar, strlen(tvar));
	}
    }
#endif

    /* Relative path handling:  If path does not have a leading "/"	*/
    /* or "~" and cellDef->cd_file has path components, then the path	*/
    /* should be interpreted relative to the path of the parent cell.	*/

    /* If there is no pathptr, then one of these three things are true:	*/
    /* (1) The instance is not the first time the cell was encountered	*/
    /* in the file, (2) "-dereference" has been selected on the current	*/
    /* file, but the subcell was already loaded, or (3) the cell is in	*/
    /* the same path as the parent.  Only case (3) needs to be handled.	*/

    if ((firstUse == TRUE) && ((*pathptr == '\0') ||
		((*pathptr != '/') && (*pathptr != '~'))))
	if ((cellDef->cd_file != NULL) &&
			(slashptr = strrchr(cellDef->cd_file, '/')) != NULL)
	{
	    *slashptr = '\0';
	    if (*pathptr == '\0')
		strcpy(path, cellDef->cd_file);
	    else
	    {
		char savepath[1024];
		strcpy(savepath, pathptr);
		int n = snprintf(path, sizeof(path), "%s/%s", cellDef->cd_file, savepath);
		ASSERT(n < sizeof(path), "path");
	    }
	    pathptr = &path[0];
	    *slashptr = '/';
	}

    /* If path has a leading '~/' and cellDef->cd_file has an absolute	*/
    /* path that does not match the user's home directory, but appears	*/
    /* to be a different home directory, then replace the "~" with the	*/
    /* home directory used by the parent cell.				*/

    if (*pathptr == '~' && *(pathptr + 1) == '/')
	if ((cellDef->cd_file != NULL) && (cellDef->cd_file[0] == '/'))
	{
	    char *homedir = getenv("HOME");
	    if (strncmp(cellDef->cd_file, homedir, strlen(homedir)) ||
			*(cellDef->cd_file + strlen(homedir)) != '/')
	    {
		char *homeroot = strrchr(homedir, '/');
		int rootlen = (int)(homeroot - homedir) + 1;
		if (!strncmp(cellDef->cd_file, homedir, rootlen))
		{
		    char savepath[1024];
		    char *userbrk = strchr(cellDef->cd_file + rootlen, '/');
		    if (userbrk != NULL)
		    {
			int userlen = (int)(userbrk - cellDef->cd_file);
			strcpy(savepath, pathptr + 1);
			strcpy(path, cellDef->cd_file);
			strcpy(path + userlen, savepath);
			pathptr = &path[0];
		    }
		}
	    }
	}

    /* If "use" line contains a path name, then set cd_file to this and	*/
    /* it will be the preferred path.  If cd_file is already set and	*/
    /* points to a different target, then flag an error, as there are	*/
    /* now two versions of the same cell name coming from different	*/
    /* sources, and this must be corrected.				*/

    if (*pathptr != '\0')
    {
	if (subCellDef->cd_file != NULL)
	{
	    slashptr = strrchr(subCellDef->cd_file, '/');
	    if (slashptr != NULL)
	    {
		bool pathOK = FALSE;
		char *cwddir = getenv("PWD");
		*slashptr = '\0';

		/* Avoid generating error message if pathptr starts with '~' */
		/* and the tilde-expanded name matches the subCellDef name   */

		if (*pathptr == '~')
		{
		    char *homedir = getenv("HOME");
		    if (!strncmp(subCellDef->cd_file, homedir, strlen(homedir))
			    && (!strcmp(subCellDef->cd_file + strlen(homedir),
			    pathptr + 1)))
			pathOK = TRUE;
		}
		else if (!strcmp(cwddir, pathptr)) pathOK = TRUE;

		/* Apply same check as done in DBWprocs, which is to check the
		 * inode of the two files and declare the path okay if they are
		 * the same.  This avoids conflicts in files that are referenced
		 * from two different places via different relative paths, or
		 * through symbolic links.
		 */

		if ((pathOK == FALSE) && strcmp(subCellDef->cd_file, pathptr)
			    && (dereference == FALSE) && (firstUse == TRUE))
		{
		    struct stat statbuf;
		    ino_t inode;

		    if (stat(subCellDef->cd_file, &statbuf) == 0)
		    {
			inode = statbuf.st_ino;

			if (stat(pathptr, &statbuf) == 0)
			{
			    if (inode == statbuf.st_ino)
				pathOK = TRUE;
			}
		    }
		}

		if ((pathOK == FALSE) && strcmp(subCellDef->cd_file, pathptr)
			    && (dereference == FALSE) && (firstUse == TRUE))
		{
		    /* See if both paths are inside a git repository, and both
		     * git repositories have the same commit hash.  Then the
		     * two layouts can be considered equivalent.  If the "git"
		     * command fails for any reason, then ignore the error and
		     * continue.
		     */
		    char *sl1ptr, *sl2ptr;
		    int link[2], nbytes, status;
		    pid_t pid;
		    char githash1[128];
		    char githash2[128];
		    char argstr[1024];

		    githash1[0] = '\0';
		    githash2[0] = '\0';

		    /* Remove the file component */
		    sl1ptr = strrchr(pathptr, '/');
		    if (sl1ptr != NULL) *sl1ptr = '\0';

		    /* Check first file for a git hash */
		    if (pipe(link) != -1)
		    {
			FORK(pid);
			if (pid == 0)
			{
			    dup2(link[1], STDOUT_FILENO);
			    close(link[0]);
			    close(link[1]);
			    int n = snprintf(argstr, sizeof(argstr), "-C %s", pathptr);
			    ASSERT(n < sizeof(argstr), "argstr");
			    execlp("git", argstr, "rev-parse", "HEAD", NULL);
			    _exit(122);  /* see vfork man page for reason for _exit() */
			}
			else
			{
			    close(link[1]);
			    nbytes = read(link[0], githash1, sizeof(githash1));
			    waitpid(pid, &status, 0);
			}
		    }

		    if (sl1ptr != NULL) *sl1ptr = '/';

		    if (githash1[0] != '\0')
		    {
			/* Check the second repository */

			/* Remove the file component */
			sl2ptr = strrchr(subCellDef->cd_file, '/');
			if (sl2ptr != NULL) *sl2ptr = '\0';

			/* Check first file for a git hash */
			if (pipe(link) != -1)
			{
			    FORK(pid);
			    if (pid == 0)
			    {
				dup2(link[1], STDOUT_FILENO);
				close(link[0]);
				close(link[1]);
				sprintf(argstr, "-C %s", subCellDef->cd_file);
				execlp("git", argstr, "rev-parse", "HEAD", NULL);
				_exit(123);  /* see vfork man page for reason for _exit() */
			    }
			    else
			    {
				close(link[1]);
				nbytes = read(link[0], githash2, sizeof(githash2));
				waitpid(pid, &status, 0);
			    }
			}

			if (sl2ptr != NULL) *sl2ptr = '/';

			if (githash2[0] != '\0')
			{
			    /* Check if the repositories have the same hash */
			    if (!strcmp(githash1, githash2))
			    {
				TxPrintf("Cells %s in %s and %s have matching git repository"
					" commits and can be considered equivalent.\n",
					slashptr + 1, subCellDef->cd_file, pathptr);
				pathOK = TRUE;
			    }
			}
		    }
		}

		if ((pathOK == FALSE) && strcmp(subCellDef->cd_file, pathptr)
			    && (dereference == FALSE) && (firstUse == TRUE))
		{
		    FILETYPE ftest;

		    TxError("Duplicate cell in %s:  Instance of cell %s is from "
				"path %s but cell was previously read from %s.\n",
				cellDef->cd_name, slashptr + 1, pathptr,
				subCellDef->cd_file);

		    /* Test file at path.  If path is invalid then ignore it	*/ 
		    /* (automatic dereferencing due to unavailability).		*/

		    ftest = PaZOpen(cellname, "r", DBSuffix, pathptr, (char *)NULL,
					(char **) NULL);
		    if (ftest == NULL)
		    {
			TxError("New path does not exist and will be ignored.\n");
		    }
		    else
		    {
			char *newname = (char *)mallocMagic(strlen(cellname) + 7);
			int i = 0;

			/* To do:  Run checksum on file (not yet implemented) */
			FCLOSE(ftest);

			while (TRUE)
			{
			    sprintf(newname, "%s__%d", cellname, i);
			    if (DBCellLookDef(newname) == NULL) break;
			    i++;
			}
			TxError("Cell name conflict:  Renaming original cell to %s.\n",
				newname);

			DBCellRename(cellname, newname, TRUE);
			subCellDef = DBCellNewDef(cellname);
			subCellDef->cd_timestamp = childStamp;
			subCellDef->cd_bbox = r;
			subCellDef->cd_extended = r;
			freeMagic(newname);

			/* Reconstruct file from path and cellname */

			strcat(path, "/");
			strcat(path, subCellDef->cd_name);
			strcat(path, DBSuffix);
			StrDup(&subCellDef->cd_file, path);
		    }
		}
		*slashptr = '/';
	    }

	    /* The same reasoning applies if the existing file has a	*/
	    /* default path but the new cell has a (different) path.	*/
	    /* The paths only match if pathptr is the CWD.		*/

	    else if (*pathptr != '\0')
	    {
		bool pathOK = FALSE;
		char *cwddir = getenv("PWD");

		if (cwddir != NULL)
		{
		    if (*pathptr == '~')
		    {
			/* Check if the path is the same as the current directory */

			char *homedir = getenv("HOME");
			if (!strncmp(cwddir, homedir, strlen(homedir))
				&& (!strcmp(cwddir + strlen(homedir),
				pathptr + 1)))
			    pathOK = TRUE;
		    }
		    else if (!strcmp(cwddir, pathptr)) pathOK = TRUE;

		    if ((pathOK == FALSE) && strcmp(cwddir, pathptr)
			    && (dereference == FALSE) && (firstUse == TRUE))
		    {
			FILETYPE ftest;

			TxError("Duplicate cell in %s:  Instance of cell %s is from "
				"path %s but cell was previously read from "
				"the current directory.\n",
				cellDef->cd_name, cellname, pathptr);

			/* Test file at path.  If path is invalid then ignore	*/ 
			/* it (automatic dereferencing due to unavailability).	*/

			ftest = PaZOpen(cellname, "r", DBSuffix, pathptr, (char *)NULL,
					(char **) NULL);
			if (ftest == NULL)
			{
			    TxError("New path does not exist and will be ignored.\n");
			}
			else
			{
			    char *newname = (char *)mallocMagic(strlen(cellname) + 7);
			    int i = 0;

			    /* To do:  Run checksum on file (not yet implemented) */
			    FCLOSE(ftest);

			    while (TRUE)
			    {
				sprintf(newname, "%s__%d", cellname, i);
				if (DBCellLookDef(newname) == NULL) break;
				i++;
			    }
			    TxError("Cell name conflict:  Renaming original "
					"cell to %s.\n", newname);

			    DBCellRename(cellname, newname, TRUE);
			    subCellDef = DBCellNewDef(cellname);
			    subCellDef->cd_timestamp = childStamp;
			    subCellDef->cd_bbox = r;
			    subCellDef->cd_extended = r;
			    freeMagic(newname);

			    /* Reconstruct file from path and cellname */

			    strcat(path, "/");
			    strcat(path, subCellDef->cd_name);
			    strcat(path, DBSuffix);
			    StrDup(&subCellDef->cd_file, path);
			}
		    }
		}
	    }
	}
	else
	{
	    /* Reconstruct file from path and cellname */

	    strcat(path, "/");
	    strcat(path, subCellDef->cd_name);
	    strcat(path, DBSuffix);
	    StrDup(&subCellDef->cd_file, path);
	}
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
    FILETYPE f;		/* Input file */
    int scalen;		/* Scale up by this factor */
    int scaled;		/* Scale down by this factor */
{
    char propertyname[128], propertyvalue[2049], *storedvalue;
    char *pvalueptr;
    int ntok;
    unsigned int noeditflag;

    /* Save CDNOEDIT flag if set, and clear it */
    noeditflag = cellDef->cd_flags & CDNOEDIT;
    cellDef->cd_flags &= ~CDNOEDIT;

    /* Get first element line */
    line[len - 1] = 'X';
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
	 * property type ever needed.  Handle possible string buffer
	 * overflows.
	 */
	if (line[0] == 's')
	{
	    pvalueptr = &propertyvalue[0];

	    if ((ntok = sscanf(line, "string %127s %2048[^\n]",
		    propertyname, propertyvalue)) != 2)
	    {
		TxError("Skipping bad property line: %s", line);
		goto nextproperty;
	    }

	    /* Handle string overflows in property values */
	    if (line[len - 1] == '\0')
	    {
		int pvlen = strlen(pvalueptr);
		*(pvalueptr + pvlen - 1) = '\0';

		while (*(pvalueptr + pvlen - 1) == '\0')
		{
		    char *newpvalue;

		    pvlen += 2048;
		    newpvalue = (char *)mallocMagic(pvlen);
		    strcpy(newpvalue, pvalueptr);
		    if (pvalueptr != &propertyvalue[0])
			freeMagic(pvalueptr);
		    pvalueptr = newpvalue;
		    *(pvalueptr + pvlen - 1) = 'X';
		    if (dbFgets(newpvalue + pvlen - 2048, 2048, f) == NULL)
		    {
			freeMagic(pvalueptr);
			cellDef->cd_flags |= noeditflag;
			return (TRUE);
		    }
		}
	    }

	    /* Go ahead and process the vendor GDS property */
	    if (!strcmp(propertyname, "GDS_FILE"))
		cellDef->cd_flags |= CDVENDORGDS;

	    /* Also process FIXED_BBOX property, as units must match,	*/
	    /* and ditto for MASKHINTS_*.				*/

	    if (!strcmp(propertyname, "FIXED_BBOX"))
	    {
		Rect locbbox;

		if (sscanf(pvalueptr, "%d %d %d %d",
			&(locbbox.r_xbot),
			&(locbbox.r_ybot),
			&(locbbox.r_xtop),
			&(locbbox.r_ytop)) != 4)
		{
		    TxError("Cannot read bounding box values in %s property",
				propertyname);
		    storedvalue = StrDup((char **)NULL, pvalueptr);
		    (void) DBPropPut(cellDef, propertyname, storedvalue);
		}
		else
		{
		    if (scalen > 1)
		    {
			locbbox.r_xbot *= scalen;
			locbbox.r_ybot *= scalen;
			locbbox.r_xtop *= scalen;
			locbbox.r_ytop *= scalen;
		    }
		    if (scaled > 1)
		    {
			locbbox.r_xbot /= scaled;
			locbbox.r_ybot /= scaled;
			locbbox.r_xtop /= scaled;
			locbbox.r_ytop /= scaled;
		    }
		    cellDef->cd_flags |= CDFIXEDBBOX;
		    storedvalue = (char *)mallocMagic(40);
		    sprintf(storedvalue, "%d %d %d %d",
			    locbbox.r_xbot, locbbox.r_ybot,
			    locbbox.r_xtop, locbbox.r_ytop);
		    (void) DBPropPut(cellDef, propertyname, storedvalue);
		}
	    }
	    else if (!strncmp(propertyname, "MASKHINTS_", 10))
	    {
		Rect locbbox;
		char *pptr = pvalueptr;
		int  numvals, numrects = 0, slen, n;

		while (*pptr != '\0')
		{
		    numvals = sscanf(pptr, "%d %d %d %d",
				&(locbbox.r_xbot),
				&(locbbox.r_ybot),
				&(locbbox.r_xtop),
				&(locbbox.r_ytop));
		    if (numvals <= 0)
			break;
		    else if (numvals != 4)
		    {
			TxError("Cannot read bounding box values in %s property",
				propertyname);
			break;
		    }
		    else
		    {
			if (numrects == 0)
			{
			    storedvalue = (char *)mallocMagic(40);
			    *storedvalue = '\0';
			    slen = -1;
			}
			else
			{
			    char *newvalue;
			    slen = strlen(storedvalue);
			    newvalue = (char *)mallocMagic(40 + slen);
			    sprintf(newvalue, "%s ", storedvalue);
			    freeMagic(storedvalue);
			    storedvalue = newvalue;
			}
			numrects++;

			if (scalen > 1)
			{
			    locbbox.r_xbot *= scalen;
			    locbbox.r_ybot *= scalen;
			    locbbox.r_xtop *= scalen;
			    locbbox.r_ytop *= scalen;
			}
			if (scaled > 1)
			{
			    locbbox.r_xbot /= scaled;
			    locbbox.r_ybot /= scaled;
			    locbbox.r_xtop /= scaled;
			    locbbox.r_ytop /= scaled;
			}
			sprintf(storedvalue + slen + 1, "%d %d %d %d",
				locbbox.r_xbot, locbbox.r_ybot,
				locbbox.r_xtop, locbbox.r_ytop);

			/* Skip forward four values in pvalueptr */
			for (n = 0; n < 4; n++)
			{
			    while ((*pptr != '\0') && !isspace(*pptr)) pptr++;
			    while ((*pptr != '\0') && isspace(*pptr)) pptr++;
			}
		    }
		}
		(void) DBPropPut(cellDef, propertyname, storedvalue);
	    }
	    else
	    {
		storedvalue = StrDup((char **)NULL, pvalueptr);
		(void) DBPropPut(cellDef, propertyname, storedvalue);
	    }
	    if (pvalueptr != &propertyvalue[0])
		freeMagic(pvalueptr);
	}

nextproperty:
	line[len - 1] = 'X';
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
    FILETYPE f;		/* Input file */
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
    FILETYPE f;		/* Input file */
    int scalen;		/* Scale up by this factor */
    int scaled;		/* Scale down by this factor */
{
    char layername[50], text[1024], port_use[50], port_class[50], port_shape[50];
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
	    int idx = 0;
	    Label *lab;

	    if (((lab = cellDef->cd_lastLabel) == NULL) ||
			(lab->lab_flags & PORT_DIR_MASK) ||
			(((ntok = sscanf(line, "port %d %4s %49s %49s %49s",
				&idx, ppos, port_use, port_class, port_shape)) != 2) &&
			(ntok != 4) && (ntok != 5)))
	    {
		TxError("Skipping bad \"port\" line: %s", line);
		goto nextlabel;
	    }
	    lab->lab_port = idx;
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
	    if (ntok >= 4)
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
		if (ntok == 5) {
		    switch(port_shape[0])
		    {
			case 'a':
			    lab->lab_flags |= PORT_SHAPE_ABUT;
			    break;
			case 'r':
			    lab->lab_flags |= PORT_SHAPE_RING;
			    break;
			case 'f':
			    lab->lab_flags |= PORT_SHAPE_THRU;
			    break;
			case 'd':
			    lab->lab_flags |= PORT_SHAPE_DEFAULT;
			    break;
			default:
			    TxError("Ignoring unknown \"port\" shape: %s", port_shape);
			    break;
		    }
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
	    DBPutLabel(cellDef, &r, orient, text, type, flags, 0);
	else
	    DBPutFontLabel(cellDef, &r, font, size, rotate, &offset,
			orient, text, type, flags, 0);

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
    FILETYPE f;
{
    char *cs;
    int l;
    int c = EOF;

    do
    {
	cs = line, l = len;
	while (--l > 0 && (c = FGETC(f)) != EOF)
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
 * cucompare ---
 *
 *	String comparison of two instance names, for the purpose of sorting
 *	the instances in a .mag file output in a repeatable way.
 *
 * ----------------------------------------------------------------------------
 */

int
cucompare(const void *one, const void *two)
{
    CellUse *use1, *use2;
    char *s1, *s2;

    use1 = *((CellUse **)one);
    use2 = *((CellUse **)two);

    s1 = use1->cu_id;
    s2 = use2->cu_id;

    return strcmpbynum(s1, s2);
}

/* Structure used by dbGetUseFunc().  Record a list of cell uses and	*/
/* an index into the list.						*/

struct cellUseList {
    int idx;
    CellUse **useList;
};

/*
 * ----------------------------------------------------------------------------
 *
 * dbGetUseFunc ---
 *
 *	Function to copy an enumerated cell use into a pre-allocated array
 *	for alphabetical sorting.
 *
 * Return value:
 *	Return 0 to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
dbGetUseFunc(cellUse, useRec)
    CellUse *cellUse;	/* Cell use whose "call" is to be written to a file */
    struct cellUseList *useRec;
{
    useRec->useList[useRec->idx] = cellUse;
    useRec->idx++;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbCountUseFunc ---
 *
 *	Function to count cell uses by incrementing a counter for each
 *	enumerated use.
 *
 * Return value:
 *	Return 0 to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
dbCountUseFunc(cellUse, count)
    CellUse *cellUse;	/* Cell use whose "call" is to be written to a file */
    int *count;
{
    (*count)++;
    return 0;
}

/* Structure used by keycompare to keep the key and value strings of a	*/
/* property record.							*/

struct keyValuePair {
    char *key;
    char *value;
};

/*
 * ----------------------------------------------------------------------------
 *
 * keycompare ---
 *
 *	String comparison of two property keys, for the purpose of sorting
 *	the properties in a .mag file output in a repeatable way.
 *
 * ----------------------------------------------------------------------------
 */

int
keycompare(const void *one, const void *two)
{
    int cval;
    struct keyValuePair *kv1 = *((struct keyValuePair **)one);
    struct keyValuePair *kv2 = *((struct keyValuePair **)two);
    char *s1 = kv1->key;
    char *s2 = kv2->key;

    cval = strcmpbynum(s1, s2);
    return cval;
}

/* Structures used by dbGetPropFunc().  Record a list of property keys	*/
/* and values and an index into the lists.				*/

struct cellPropList {
    int idx;
    struct keyValuePair **keyValueList;
};

/*
 * ----------------------------------------------------------------------------
 *
 * dbGetPropFunc ---
 *
 *	Function to copy the key and value of an enumerated property
 *	record into a pre-allocated array for alphabetical sorting.
 *
 * Return value:
 *	Return 0 to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
dbGetPropFunc(key, value, propRec)
    char *key;
    ClientData value;
    struct cellPropList *propRec;
{
    propRec->keyValueList[propRec->idx] =
		(struct keyValuePair *)mallocMagic(sizeof(struct keyValuePair));
    propRec->keyValueList[propRec->idx]->key = key;
    propRec->keyValueList[propRec->idx]->value = (char *)value;
    propRec->idx++;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbCountPropFunc ---
 *
 *	Function to count cell properties by incrementing a counter for each
 *	enumerated property.
 *
 * Return value:
 *	Return 0 to keep the search going.
 *
 * ----------------------------------------------------------------------------
 */

int
dbCountPropFunc(key, value, count)
    char *key;
    ClientData value;
    int *count;		/* Client data */
{
    (*count)++;
    return 0;
}

/* Structure used by dbPropWriteFunc.  Holds the FILE stream pointer of
 * the file being written to, and the scale reducer for dimensional
 * values in the output (magscale).
 */

typedef struct _pwfrec {
    FILE *pwf_file;
    int pwf_reducer;
} pwfrec;

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
    char lstring[2048];
    char *propvalue;
    bool propfound;
    CellUse **useList;
    int i, numUses = 0, numProps = 0;
    struct cellUseList cul;
    pwfrec pwf;

#define FPUTSF(f,s)\
{\
     if (fputs(s,f) == EOF) goto ioerror;\
     DBFileOffset += strlen(s);\
}
#define FPUTSR(f,s)\
{\
     if (fputs(s,f) == EOF) return 1;\
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
	 FPUTSF(f, headerstring);
    }

    /*
     * Output the paint of the cell.
     * Note that we only output up to the last layer appearing
     * in the technology file (DBNumUserLayers-1).  Automatically
     * generated stacked contact types are added to typeMask and
     * will be decomposed into the residue appropriate for the
     * plane being searched.
     */

    if (cellDef->cd_file)
	arg.wa_name = cellDef->cd_file;
    else
	arg.wa_name = cellDef->cd_name;
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

    /* Now the cell uses.  To make sure that output is repeatable each	*/
    /* time the CellDef is written, first collect all of the cells,	*/
    /* then sort them alphabetically, and then write them.		*/

    DBCellEnum(cellDef, dbCountUseFunc, (ClientData) &numUses);
    if (numUses > 0)
    {
    	cul.useList = (CellUse **)mallocMagic(numUses * sizeof(CellUse *));
    	cul.idx = 0;

    	if (DBCellEnum(cellDef, dbGetUseFunc, (ClientData)&cul))
	    goto ioerror;

    	qsort(cul.useList, numUses, sizeof(CellUse *), cucompare);

    	for (i = 0; i < numUses; i++)
	    dbWriteCellFunc(cul.useList[i], (ClientData) &arg);

        freeMagic((char *)cul.useList);
	
        /* Clear flags set in dbWriteCellFunc */
        DBCellEnum(cellDef, dbClearCellFunc, (ClientData)NULL);
    }

    /* Now labels */
    if (cellDef->cd_labels)
    {
	FPUTSF(f, "<< labels >>\n");
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
	    FPUTSF(f, lstring);
	    if (lab->lab_flags & PORT_DIR_MASK)
	    {
		char ppos[5];

		ppos[0] = '\0';
		if (lab->lab_flags & PORT_DIR_NORTH) strcat(ppos, "n");
		if (lab->lab_flags & PORT_DIR_SOUTH) strcat(ppos, "s");
		if (lab->lab_flags & PORT_DIR_EAST) strcat(ppos, "e");
		if (lab->lab_flags & PORT_DIR_WEST) strcat(ppos, "w");
		sprintf(lstring, "port %d %s", lab->lab_port, ppos);

		if (lab->lab_flags & (PORT_USE_MASK | PORT_CLASS_MASK | PORT_SHAPE_MASK))
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

		    switch (lab->lab_flags & PORT_SHAPE_MASK)
		    {
			case PORT_SHAPE_ABUT:
			    strcat(lstring, " abutment");
			    break;
			case PORT_SHAPE_RING:
			    strcat(lstring, " ring");
			    break;
			case PORT_SHAPE_THRU:
			    strcat(lstring, " feedthrough");
			    break;
		    }
		}
		strcat(lstring, "\n");
		FPUTSF(f, lstring);
	    }
	}
    }

    /* Now any persistent elements */
    estring = DBWPrintElements(cellDef, DBW_ELEMENT_PERSISTENT, reducer);
    if (estring != NULL)
    {
	FPUTSF(f, "<< elements >>\n");
	FPUTSF(f, estring);
	freeMagic(estring);
    }

    /* And any properties */

    DBPropEnum(cellDef, dbCountPropFunc, (ClientData)&numProps);
    if (numProps > 0)
    {
    	struct cellPropList propRec;

	propRec.idx = 0;
	propRec.keyValueList = (struct keyValuePair **)mallocMagic(numProps
			* sizeof(struct keyValuePair *));
	DBPropEnum(cellDef, dbGetPropFunc, (ClientData)&propRec);

    	qsort(propRec.keyValueList, numProps, sizeof(struct keyValuePair *),
			keycompare);

	FPUTSF(f, "<< properties >>\n");
	pwf.pwf_file = f;
	pwf.pwf_reducer = reducer;
	for (i = 0; i < numProps; i++)
	{
	    dbWritePropFunc(propRec.keyValueList[i]->key,
			propRec.keyValueList[i]->value,
			(ClientData)&pwf);
	    freeMagic ((char *)propRec.keyValueList[i]);
	}
	freeMagic((char *)propRec.keyValueList);
    }

    FPUTSF(f, "<< end >>\n");

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
 *	Also, this function assumes that properties FIXED_BBOX and
 *	MASKHINTS_* are in internal units, and converts them by
 *	dividing by the reducer value passed in cdata.  No other
 *	properties are altered.
 *
 * ----------------------------------------------------------------------------
 */

int
dbWritePropFunc(key, value, cdata)
    char *key;
    char *value;
    ClientData cdata;
{
    pwfrec *pwf = (pwfrec *)cdata;
    FILE *f = pwf->pwf_file;
    int reducer = pwf->pwf_reducer;
    char *newvalue = value;

    /* NOTE:  FIXED_BBOX is treated specially;  values are database */
    /* values and should be divided by reducer.  Easiest to do it   */
    /* here and revert values after.  Ditto for MASKHINTS_*.	    */

    if (!strcmp(key, "FIXED_BBOX"))
    {
	Rect scalebox, bbox;

	if (sscanf(value, "%d %d %d %d", &bbox.r_xbot, &bbox.r_ybot,
		    &bbox.r_xtop, &bbox.r_ytop) == 4)
	{
	    scalebox.r_xbot = bbox.r_xbot / reducer;
	    scalebox.r_xtop = bbox.r_xtop / reducer;
	    scalebox.r_ybot = bbox.r_ybot / reducer;
	    scalebox.r_ytop = bbox.r_ytop / reducer;

	    newvalue = mallocMagic(strlen(value) + 5);
	    sprintf(newvalue, "%d %d %d %d",
		    scalebox.r_xbot, scalebox.r_ybot,
		    scalebox.r_xtop, scalebox.r_ytop);
	}
	else
	    TxError("Error:  Cannot parse FIXED_BBOX property value!\n");
	
    }
    else if (!strncmp(key, "MASKHINTS_", 10))
    {
	Rect scalebox, bbox;
        char *vptr = value, *sptr;
	int numvals, numrects = 0, n;

	while (TRUE)
	{
	    numvals = sscanf(vptr, "%d %d %d %d", &bbox.r_xbot, &bbox.r_ybot,
		    &bbox.r_xtop, &bbox.r_ytop);
	    if (numvals <= 0)
		break;
	    else if (numvals != 4)
	    {
		TxError("Error:  Cannot parse %s property value!\n", key);
		/* Revert property value to original string */
		if (newvalue != value) freeMagic(newvalue);
		newvalue = value;
		break;
	    }
	    else
	    {
		scalebox.r_xbot = bbox.r_xbot / reducer;
		scalebox.r_xtop = bbox.r_xtop / reducer;
		scalebox.r_ybot = bbox.r_ybot / reducer;
		scalebox.r_ytop = bbox.r_ytop / reducer;
		if (numrects == 0)
		{
		    newvalue = mallocMagic(40);
		    sptr = newvalue;
		}
		else
		{
		    char *tempvalue;
		    tempvalue = mallocMagic(strlen(newvalue) + 40);
		    sprintf(tempvalue, "%s ", newvalue);
		    sptr = tempvalue + strlen(newvalue) + 1;
		    freeMagic(newvalue);
		    newvalue = tempvalue;
		}
		sprintf(sptr, "%d %d %d %d",
			scalebox.r_xbot, scalebox.r_ybot,
			scalebox.r_xtop, scalebox.r_ytop);
		numrects++;
	    }

	    /* Skip forward four values in value */
	    for (n = 0; n < 4; n++)
	    {
		while (!isspace(*vptr)) vptr++;
		while (isspace(*vptr) && (*vptr != '\0')) vptr++;
	    }
	}
    }

    FPUTSR(f, "string ");
    FPUTSR(f, key);
    FPUTSR(f, " ");
    FPUTSR(f, newvalue);
    FPUTSR(f, "\n");

    if (newvalue != value) freeMagic(newvalue);

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
    const char *cp1;
    char *cp2, *dotptr;
    char expandbuf[NAME_SIZE];
    FILE *realf, *tmpf;
    int tmpres;
    struct stat statb;
    bool result, exists;

    result = FALSE;

    /*
     * Figure out the name of the file we will eventually write.
     */
    if (!fileName)
    {
	if (cellDef->cd_file)
	    fileName = cellDef->cd_file;
	else if (cellDef->cd_name)
	    fileName = cellDef->cd_name;
	else
	    return FALSE;
    }

    /* Bug fix: 7/17/99, Michael D. Godfrey:  Forces		    */
    /* cd_name and cd_file to ALWAYS be the same, otherwise ugly    */
    /* surprises can occur after saving a file as a different	    */
    /* filename.						    */

    if (fileName != cellDef->cd_file)
	StrDup(&cellDef->cd_file, fileName);

    /* The cd_file should not have the .mag suffix, but make sure   */
    /* it doesn't before adding one.				    */

    if ((strlen(fileName) < 4) || (strcmp(fileName + strlen(fileName) - 4, DBSuffix)))
    {
	realname = (char *) mallocMagic(strlen(fileName) + strlen(DBSuffix) + 1);
	(void) sprintf(realname, "%s%s", fileName, DBSuffix);
    }
    else
	realname = StrDup((char **)NULL, fileName);

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

    if (cellDef->cd_flags & CDNOEDIT)
    {
	TxPrintf("File %s is read_only and cannot be written\n", realname);
	freeMagic(realname);
	return(FALSE);
    }

#ifdef FILE_LOCKS
    if (cellDef->cd_fd == -2)
    {
	TxPrintf("File %s is locked by another user and "
		"cannot be written\n", realname);
	freeMagic(realname);
	return(FALSE);
    }
#endif

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
    if ((tmpf = fopen(tmpname, "w")))
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
	if (cellDef->cd_fd >= 0)
	{
	    close(cellDef->cd_fd);
	    cellDef->cd_fd = -1;	/* Set to initial state */
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

    /* Copy expandname back to cellDef->cd_file, if the name was changed.   */
    /* The file extension does not get copied into cd_file.		    */

    dotptr = strrchr(expandname, '.');
    if (dotptr) *dotptr = '\0';
    if (strcmp(expandname, cellDef->cd_file))
	StrDup(&cellDef->cd_file, expandname);
    if (dotptr) *dotptr = '.';

    /* Everything worked so far. */

    result = TRUE;
    {
	struct stat thestat;
	bool is_locked;
	int fd;

#ifdef FILE_LOCKS
	if (FileLocking)
	    realf = flock_open(expandname, "r", &is_locked, NULL);
	else
#endif
	    realf = fopen(expandname, "r");

	if (realf == NULL)
	{
	    cellDef->cd_flags |= CDMODIFIED;
	    TxError("Warning: Cannot open file \"%s\" for writing!\n", expandname);
	}
	else
	{
	    fd = fileno(realf);
	    fstat(fd, &thestat);
	    if (thestat.st_size != DBFileOffset)
	    {
		cellDef->cd_flags |= CDMODIFIED;
		TxError("Warning: I/O error in writing file \"%s\"\n", expandname);
	    }

#ifdef FILE_LOCKS
	    cellDef->cd_fd = -1;
	    if (FileLocking && (is_locked == FALSE))
		cellDef->cd_fd = fd;
	    else if (FileLocking && (is_locked == TRUE))
		cellDef->cd_fd = -2;
	    else
#endif
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
	FPUTSR(arg->wa_file,pstring);
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
    FPUTSR(arg->wa_file,pstring);
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
 * DBPathSubstitute --
 *
 * Replace the leading part of a file path string according to the following
 * criteria:
 *
 *	1) If the filename starts with a string equal to the contents of
 *	   Tcl variables PDK_PATH, PDKPATH, PDK_ROOT, or PDKROOT, then
 *	   replace the string with the variable name.  The "PATH" names are
 *	   more specific than "ROOT" and so are checked first.
 *	2) If the filename starts with a string equal to the contents of
 *	   environment variable HOME, then replace the string with "~".
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Writes into the string "cstring".
 *
 * ----------------------------------------------------------------------------
 */

void
DBPathSubstitute(pathstart, cstring, cellDef)
    char *pathstart;
    char *cstring;
    CellDef *cellDef;
{
    bool subbed = FALSE;
#ifdef MAGIC_WRAPPER
    char *tvar;

    /* Check for the leading component of the file path being equal to	*/
    /* one of several common variable names for the PDK location, and	*/
    /* if there is a match, then substitute the variable name for the	*/
    /* matching leading path component.					*/

    if (subbed == FALSE)
    {
	tvar = (char *)Tcl_GetVar(magicinterp, "PDK_PATH", TCL_GLOBAL_ONLY);
	if (tvar)
	    if (!strncmp(pathstart, tvar, strlen(tvar)))
	    {
	    	sprintf(cstring, "$PDK_PATH%s", pathstart + strlen(tvar));
		subbed = TRUE;
	    }
    }
    if (subbed == FALSE)
    {
	tvar = (char *)Tcl_GetVar(magicinterp, "PDKPATH", TCL_GLOBAL_ONLY);
	if (tvar)
	    if (!strncmp(pathstart, tvar, strlen(tvar)))
	    {
	    	sprintf(cstring, "$PDKPATH%s", pathstart + strlen(tvar));
		subbed = TRUE;
	    }
    }
    if (subbed == FALSE)
    {
	tvar = (char *)Tcl_GetVar(magicinterp, "PDK_ROOT", TCL_GLOBAL_ONLY);
	if (tvar)
	    if (!strncmp(pathstart, tvar, strlen(tvar)))
	    {
	    	sprintf(cstring, "$PDK_ROOT%s", pathstart + strlen(tvar));
		subbed = TRUE;
	    }
    }
    if (subbed == FALSE)
    {
	tvar = (char *)Tcl_GetVar(magicinterp, "PDKROOT", TCL_GLOBAL_ONLY);
	if (tvar)
	    if (!strncmp(pathstart, tvar, strlen(tvar)))
	    {
	    	sprintf(cstring, "$PDKROOT%s", pathstart + strlen(tvar));
		subbed = TRUE;
	    }
    }
#endif

    if (subbed == FALSE)
    {
	/* If path starts with home path, then replace with "~"	*/
	/* to make IP semi-portable between home directories	*/
	/* with the same file structure.			*/

	char *homedir = getenv("HOME");

	if (cellDef->cd_file == NULL)
	    sprintf(cstring, "%s", pathstart);
	else if (!strncmp(cellDef->cd_file, homedir, strlen(homedir))
			&& (*(cellDef->cd_file + strlen(homedir)) == '/'))
	    sprintf(cstring, "~%s", cellDef->cd_file + strlen(homedir));
	else
	    sprintf(cstring, "%s", pathstart);
    }
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
    char     cstring[1024], *pathend, *pathstart, *parent;

    t = &(cellUse->cu_transform);
    b = &(cellUse->cu_def->cd_bbox);
    pathstart = cellUse->cu_def->cd_file;
    parent = arg->wa_name;

    if (pathstart == NULL)
	pathend = NULL;
    else
    {
	char *slashptr, *pathorigin;

	/* Get child path relative to the parent path */

	pathorigin = pathstart;
	pathend = strrchr(pathstart, '/');
	slashptr = strchr(pathstart, '/');
	while (slashptr)
	{
	    if (!strncmp(pathorigin, parent, (int)(slashptr - pathorigin + 1)))
	    {
		pathstart = slashptr + 1;
		slashptr = strchr(pathstart, '/');
	    }
	    else
		break;

	}

	/* If there are no common components, then restore the leading '/' */
	if ((*pathorigin == '/') && (pathstart == pathorigin + 1))
	    pathstart = pathorigin;

	if (pathend != NULL)
	{
	    *pathend = '\0';
	    if (pathstart >= pathend)
		pathstart = NULL;
	}
    }

    if ((cellUse->cu_def->cd_flags & CDVISITED) || (pathend == NULL) ||
		(pathstart == NULL) || (*pathstart == '\0'))
    {
	sprintf(cstring, "use %s %c%s\n", cellUse->cu_def->cd_name,
		(cellUse->cu_flags & CU_LOCKED) ? CULOCKCHAR : ' ',
		cellUse->cu_id);
    }
    else
    {
    	sprintf(cstring, "use %s %c%s ", cellUse->cu_def->cd_name,
		(cellUse->cu_flags & CU_LOCKED) ? CULOCKCHAR : ' ',
		cellUse->cu_id);
    	DBPathSubstitute(pathstart, cstring + strlen(cstring), cellUse->cu_def);
	strcat(cstring, "\n");
    }

    FPUTSR(arg->wa_file, cstring);

    cellUse->cu_def->cd_flags |= CDVISITED;
    if (pathend != NULL) *pathend = '/';

    if ((cellUse->cu_xlo != cellUse->cu_xhi)
	    || (cellUse->cu_ylo != cellUse->cu_yhi))
    {
	sprintf(cstring, "array %d %d %d %d %d %d\n",
		cellUse->cu_xlo, cellUse->cu_xhi, cellUse->cu_xsep / arg->wa_reducer,
		cellUse->cu_ylo, cellUse->cu_yhi, cellUse->cu_ysep / arg->wa_reducer);
	FPUTSR(arg->wa_file,cstring);
    }

    sprintf(cstring, "timestamp %d\n", cellUse->cu_def->cd_timestamp);
    FPUTSR(arg->wa_file,cstring)
    sprintf(cstring, "transform %d %d %d %d %d %d\n",
	    t->t_a, t->t_b, t->t_c / arg->wa_reducer,
	    t->t_d, t->t_e, t->t_f / arg->wa_reducer);
    FPUTSR(arg->wa_file,cstring)
    sprintf(cstring, "box %d %d %d %d\n",
	    b->r_xbot / arg->wa_reducer, b->r_ybot / arg->wa_reducer,
	    b->r_xtop / arg->wa_reducer, b->r_ytop / arg->wa_reducer);
    FPUTSR(arg->wa_file,cstring)
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
    FILETYPE f;
    static char line[512];
    char *p;

    f = PaZOpen(cellName, "r", DBSuffix, Path, CellLibPath, (char **) NULL);
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
    (void) FCLOSE(f);
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
	    StrDup(&DBbackupFile, template);
	    freeMagic(template);
	    TxPrintf("Created database crash recovery file %s\n", DBbackupFile);
	}
	filename = DBbackupFile;
    }
    else
    {
	if (strlen(filename) == 0)
	{
	    StrDup(&DBbackupFile, (char *)NULL);
	    return TRUE;
	}
	StrDup(&DBbackupFile, filename);
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
