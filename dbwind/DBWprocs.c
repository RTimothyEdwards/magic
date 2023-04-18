/*
 * DBWprocs.c --
 *
 *	Procedures to interface the database with the window package
 *	for the purposes of window creation, deletion, and modification.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWprocs.c,v 1.3 2008/06/01 18:37:39 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "utils/main.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "dbwind/dbwind.h"
#include "graphics/graphics.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/utils.h"
#include "utils/undo.h"
#include "graphics/glyphs.h"
#include "utils/malloc.h"
#include "utils/styles.h"

global WindClient DBWclientID;
static int dbwBitMask = 0;
#define MAX_BITMASK INT_MAX
#define MAX_BITS_IN_MASK (sizeof(unsigned int) * 8 - 1)

extern void DBWredisplay();		/* Defined in DBWdisplay.c */

/* The following variable is used to allow somebody besides ourselves
 * to take over button handling for database windows.  If it is NULL
 * (which it usually is) we handle buttons.  If it isn't NULL, it gives
 * the address of a procedure to handle buttons.
 */

static int (*dbwButtonHandler)() = NULL;

/*
 * ----------------------------------------------------------------------------
 *
 * DBWcreate
 *
 *	A new window has been created, create and initialize the needed
 *	structures.
 *
 * Results:
 *	FALSE if we have too many windows, TRUE otherwise.
 *
 * Side effects:
 *	Load the given cell into the window.  If no cell is given, then
 *	if the box exists, load a zoomed-in view of the box.  Otherwise
 *	load a new blank cell.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBWcreate(window, argc, argv)
    MagWindow *window;
    int argc;
    char *argv[];
{
    int bitMask, newBitMask, expand;
    DBWclientRec *crec;
    CellDef *boxDef;
    Rect box;

    /*
     * See if we can create a new window without running out
     * of bits in our bitMask.
     */

    newBitMask = (dbwBitMask + 1) | dbwBitMask;
    if (newBitMask > MAX_BITMASK)
	return FALSE;
    bitMask = newBitMask ^ dbwBitMask;
    dbwBitMask = newBitMask;

    crec = (DBWclientRec *) mallocMagic(sizeof(DBWclientRec));
    crec->dbw_flags = DBW_SEELABELS | DBW_SEECELLS;
    crec->dbw_watchPlane = -1;
    crec->dbw_watchDef = (CellDef *) NULL;
    crec->dbw_bitmask = bitMask;
    crec->dbw_expandAmounts.r_xbot = 0;
    crec->dbw_expandAmounts.r_ybot = 0;
    crec->dbw_expandAmounts.r_xtop = 0;
    crec->dbw_expandAmounts.r_ytop = 0;
    crec->dbw_gridRect.r_xbot = 0;
    crec->dbw_gridRect.r_ybot = 0;
    crec->dbw_gridRect.r_xtop = 1;
    crec->dbw_gridRect.r_ytop = 1;
    crec->dbw_visibleLayers = DBAllTypeBits;
    crec->dbw_hlErase = DBNewPlane((ClientData) TT_SPACE);
    crec->dbw_hlRedraw = DBNewPlane((ClientData) TT_SPACE);
    crec->dbw_labelSize = 0;
    crec->dbw_scale = -1;
    crec->dbw_surfaceArea.r_xbot = 0;
    crec->dbw_surfaceArea.r_ybot = 0;
    crec->dbw_surfaceArea.r_xtop = -1;
    crec->dbw_surfaceArea.r_ytop = -1;
    crec->dbw_origin.p_x = 0;
    crec->dbw_origin.p_y = 0;

    window->w_clientData = (ClientData) crec;
    if (argc > 0)
	DBWloadWindow(window, argv[0], DBW_LOAD_IGNORE_TECH);
    else if (ToolGetBox(&boxDef, &box))
    {
	DBWloadWindow(window, boxDef->cd_name, DBW_LOAD_IGNORE_TECH);

	/* Zoom in on the box, leaving a 10% border or at least 2 units
	 * on each side.
	 */

	expand = (box.r_xtop - box.r_xbot)/20;
	if (expand < 2) expand = 2;
	box.r_xtop += expand;
	box.r_xbot -= expand;
	expand = (box.r_ytop - box.r_ybot)/20;
	if (expand < 2) expand = 2;
	box.r_ytop += expand;
	box.r_ybot -= expand;
	WindMove(window, &box);
    }
    else
    {
	DBWloadWindow(window, (char *) NULL, DBW_LOAD_IGNORE_TECH);
    }
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 * DBWdelete --
 *
 *	Clean up the data structures before deleting a window.
 *
 * Results:
 *	TRUE if we really want to delete the window, FALSE otherwise.
 *
 * Side effects:
 *	A DBWclientRec is freed.
 * ----------------------------------------------------------------------------
 */

bool
DBWdelete(window)
    MagWindow *window;
{
    DBWclientRec *cr;

    cr = (DBWclientRec *) window->w_clientData;
    dbwBitMask &= ~(cr->dbw_bitmask);
    DBFreePaintPlane(cr->dbw_hlErase);
    DBFreePaintPlane(cr->dbw_hlRedraw);
    TiFreePlane(cr->dbw_hlErase);
    TiFreePlane(cr->dbw_hlRedraw);
    freeMagic( (char *) cr);
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * dbwLoadFunc --
 *
 * 	This is a utility function passed to WindSearch by
 *	DBWloadWindow.  Its job is to see if the edit cell is
 *	present in any window except a given one.
 *
 * Results:
 *	1 is returned if the window doesn't match the ClientData
 *	but contains the Edit Cell.  0 is returned otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
dbwLoadFunc(w, clientData)
    MagWindow *w;		/* A window found in the search. */
    MagWindow *clientData;	/* Window to ignore (passed as ClientData). */
{
    if (w == clientData) return 0;
    if (((CellUse *) w->w_surfaceID)->cu_def == EditRootDef)
	return 1;
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBWreload --
 *
 * Re-load all windows to contain the named cell as a root.
 * This is intended to be called during startup or when restarting a saved
 * image of Magic.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Loads windows with the named cell.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWreload(name)
    char *name;
{
    int dbwReloadFunc();

    (void) WindSearch(DBWclientID, (ClientData) NULL, (Rect *) NULL,
		dbwReloadFunc, (ClientData) name);
}

int
dbwReloadFunc(w, name)
    MagWindow *w;
    char *name;
{
    DBWloadWindow(w, name, DBW_LOAD_IGNORE_TECH);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWloadWindow
 *
 *	Replace the root cell of a window by the specified cell.
 *
 *	A cell name of NULL causes the cell with name "(UNNAMED)" to be
 *	created if it does not already exist, or used if it does.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If there is currently no edit use, or if this is the only
 *	window containing the edit cell, then set the edit cell
 *	to the topmost cell in the new window.  Otherwise, the edit
 *	cell doesn't change.
 *
 * Flags:
 *	If "expand" is true, unexpands all subcells of the root cell.
 *	If "dereference" is true, ignore path reference in the input file.
 *	If "fail" is true, do not create a new cell if no file is found.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWloadWindow(window, name, flags)
    MagWindow *window;		/* Identifies window to which cell is to be bound */
    char *name;			/* Name of new cell to be bound to this window */
    unsigned char flags;	/* See flags below */
{
    CellDef *newEditDef, *deleteDef;
    CellUse *newEditUse;
    void DisplayWindow();
    int res, newEdit, error_val;
    int xadd, yadd;
    Rect loadBox;
    char *rootname;
    bool isUnnamed;
    int UnexpandFunc();		/* forward declaration */

    bool ignoreTech;	/* If FALSE, indicates that the technology of
			 * the layout must match the current technology.
			 */
    bool expand;	/* Indicates whether or not to expand the cell */
    bool dereference;	/* If TRUE, ignore path references in the input */
    bool dofail;	/* If TRUE, do not create a cell if file not found */
    bool beQuiet;	/* If TRUE, do not print messages during load */

    ignoreTech = ((flags & DBW_LOAD_IGNORE_TECH) == 0) ? FALSE : TRUE;
    expand = ((flags & DBW_LOAD_EXPAND) == 0) ? FALSE : TRUE;
    dereference = ((flags & DBW_LOAD_DEREFERENCE) == 0) ? FALSE : TRUE;
    dofail = ((flags & DBW_LOAD_FAIL) == 0) ? FALSE : TRUE;
    beQuiet = ((flags & DBW_LOAD_QUIET) == 0) ? FALSE : TRUE;

    loadBox.r_xbot = loadBox.r_ybot = 0;
    loadBox.r_xtop = loadBox.r_ytop = 1;

    /* See if we're to change the edit cell */
    newEdit = !WindSearch((WindClient) DBWclientID, (ClientData) NULL,
			(Rect *) NULL, dbwLoadFunc, (ClientData) window);

    /* The (UNNAMED) cell generally gets in the way, so delete it if	*/
    /* any new cell is loaded and (UNNAMED) has no contents.		*/

    if (window->w_surfaceID == (ClientData)NULL)
	deleteDef = NULL;
    else
    {
	deleteDef = ((CellUse *)window->w_surfaceID)->cu_def;
	if (strcmp(deleteDef->cd_name, "(UNNAMED)") ||
		(GrDisplayStatus == DISPLAY_SUSPEND) ||
		deleteDef->cd_flags & (CDMODIFIED|CDBOXESCHANGED|CDSTAMPSCHANGED))
	    deleteDef = NULL;
    }

    if ((name == (char *) NULL) || (name[0] == '\0'))
    {
	/*
	 * If there is an existing unnamed cell, we use it.
	 * Otherwise, we create one afresh.
	 */
	newEditDef = DBCellLookDef(UNNAMED);
	if (newEditDef == (CellDef *) NULL)
	{
	    newEditDef = DBCellNewDef(UNNAMED);
	    DBCellSetAvail(newEditDef);
	}
    }
    else
    {
	/*
	 * Name specified.
	 * First try to find it in main memory, then try to
	 * read it from disk.
	 */

	char *dotptr;

	/* Strip any leading "./" from the name */
	if (!strncmp(name, "./", 2)) name += 2;

	rootname = strrchr(name, '/');
	if (rootname == NULL)
	    rootname = name;
	else
	    rootname++;

	/* Strip off any ".gz" extension from the name */
	dotptr = strrchr(rootname, '.');

	if (dotptr != NULL)
	    if (!strcmp(dotptr, ".gz"))
		*dotptr = '\0';

	/* Strip off any ".mag" extension from the name */
	dotptr = strrchr(rootname, '.');

	if (dotptr != NULL)
	    if (!strcmp(dotptr, ".mag"))
		*dotptr = '\0';

	newEditDef = DBCellLookDef(rootname);

	/* If "rootname" is the same as "name", then a cell was requested
	 * by name and was found in memory, and all is good.  If not, then
	 * "name" implies some specific location and needs to be checked
	 * against the existing cell's file path.
	 */
	if ((newEditDef != (CellDef *)NULL) && (newEditDef->cd_file != NULL) &&
		strcmp(name, rootname))
 	{
	    /* If the cellname exists already, check if we are really	*/
	    /* looking at the same file.  If not, and two files in two	*/
	    /* different paths have the same root cellname, then fail.	*/

	    char *fullpath;
	    bool badFile = FALSE;
	    struct stat statbuf;
	    ino_t inode;

	    if (DBTestOpen(name, &fullpath))
	    {
		if (stat(fullpath, &statbuf) == 0)
		{
		    inode = statbuf.st_ino;
		    char *full_cdfile;

		    full_cdfile = (char *)mallocMagic(strlen(newEditDef->cd_file) +
				strlen(DBSuffix) + 1);

		    /* cd_file is not supposed to have the file extension,	*/
		    /* but check just in case.					*/

		    if (strstr(newEditDef->cd_file, DBSuffix) != NULL)
			sprintf(full_cdfile, "%s", newEditDef->cd_file);
		    else
			sprintf(full_cdfile, "%s%s", newEditDef->cd_file, DBSuffix);

		    if (stat(full_cdfile, &statbuf) == 0)
		    {
			if (inode != statbuf.st_ino)
			    badFile = TRUE;
		    }
		    else if ((dofail == FALSE) &&
				(!(newEditDef->cd_flags & CDAVAILABLE)))
		    {
			/* Exception:  If the cell can be found and the		*/
			/* existing file has not been loaded yet, then keep	*/
			/* the same behavior of "expand" and rename the	cell	*/
			/* and flag a warning.					*/

			TxError("Warning:  Existing cell %s points to invalid path "
				"\"%s\".  Cell was found at location \"%s\" and "
				"this location will be used.\n",
				rootname, newEditDef->cd_file, fullpath);
			freeMagic(newEditDef->cd_file);
			newEditDef->cd_file = NULL;
		    }
		    else
			badFile = TRUE;

		    freeMagic(full_cdfile);
		}
		else
		    badFile = TRUE;

	    }
	    else
		badFile = TRUE;

	    /* If the cells have the same name but different 	*/
	    /* paths, then fail.				*/

	    if (badFile == TRUE)
	    {
		TxError("File \"%s\":  The cell was already read and points "
				"to conflicting location \"%s\".\n", name,
				newEditDef->cd_file);
		return;
	    }
	}
	if (newEditDef == (CellDef *) NULL)
	{
	    /* "-fail" option:  If no file is readable, then do not	*/
	    /* create a new cell.					*/

	    if (dofail && !DBTestOpen(name, NULL))
	    {
		if (!beQuiet)
		    TxError("No file \"%s\" found or readable.\n", name);
		return;
	    }
	    newEditDef = DBCellNewDef(rootname);
	}

	/* If dereferencing, set the appropriate flag in newEditDef. */

	if (dereference)
	    newEditDef->cd_flags |= CDDEREFERENCE;

	/* If the name differs from the root name, then set the file	*/
	/* path to be the same as "name".				*/

	if ((newEditDef->cd_file == NULL) && strcmp(name, rootname))
	    newEditDef->cd_file = StrDup((char **)NULL, name);

	/* A cell name passed on the "load" command line is never
	 * dereferenced itself;  "dereference" applies only to its
	 * (unloaded) descendents.
	 */
	if (!DBCellRead(newEditDef, ignoreTech, FALSE, &error_val))
	{
	    if (error_val == ENOENT)
	    {
		if (!beQuiet)
		    TxPrintf("Creating new cell\n");
		DBCellSetAvail(newEditDef);
	    }
	    else
	    {
		/* File exists but some error has occurred like
		 * file is unreadable or max file descriptors
		 * was reached, in which case we don't want to
		 * create a new cell, so delete the new celldef
		 * and return.
		 */
		UndoDisable();
		DBCellDeleteDef(newEditDef);
		UndoEnable();

		/*
		 * Go back to loading cell (UNNAMED) if
		 * there is no EditRootDef or EditCellUse.
		 * Otherwise, on error, keep whatever was already
		 * the root def & use.
		 */
		if (EditRootDef == NULL || EditCellUse == NULL)
		{
		    newEditDef = DBCellLookDef(UNNAMED);
		    if (newEditDef == (CellDef *) NULL)
		    {
			newEditDef = DBCellNewDef(UNNAMED);
			DBCellSetAvail(newEditDef);
		    }
		}
		else
		    return;
	    }
	}
	else
	{
	    /* DBCellRead doesn't dare to change bounding boxes, so
	     * we have to call DBReComputeBbox here (we know that it's
	     * safe).
	     */
	    DBReComputeBbox(newEditDef);
	    loadBox = newEditDef->cd_bbox;
	}
    }

    /*
     * Attach the new cell to the selected window.
     */

    if (window != NULL)
    {
	newEditUse = DBCellNewUse(newEditDef, (char *) NULL);
	(void) StrDup(&(newEditUse->cu_id), "Topmost cell in the window");
	DBExpand(newEditUse,
		((DBWclientRec *)window->w_clientData)->dbw_bitmask, TRUE);

	if (expand)
	    DBExpandAll(newEditUse, &(newEditUse->cu_bbox),
			((DBWclientRec *)window->w_clientData)->dbw_bitmask,
			FALSE, UnexpandFunc, (ClientData)
			(((DBWclientRec *)window->w_clientData)->dbw_bitmask));

	if (newEdit)
	{
	    if ((EditCellUse && EditRootDef) && (deleteDef == NULL))
	    {
		DBWUndoOldEdit(EditCellUse, EditRootDef,
			&EditToRootTransform, &RootToEditTransform);
		DBWUndoNewEdit(newEditUse, newEditDef,
			&GeoIdentityTransform, &GeoIdentityTransform);
	    }
	    if (newEditUse->cu_def->cd_flags & CDNOEDIT)
	    {
		newEdit = FALSE;
		EditCellUse = NULL;
		EditRootDef = NULL;
	    }
	    else
	    {
		EditCellUse = newEditUse;
		EditRootDef = newEditDef;
	    }

	    EditToRootTransform = GeoIdentityTransform;
	    RootToEditTransform = GeoIdentityTransform;
	}

	/* enforce a minimum size of 60 and a border of 10% around the sides */
	xadd = MAX(0, (60 - (loadBox.r_xtop - loadBox.r_xbot)) / 2) +
		(loadBox.r_xtop - loadBox.r_xbot + 1) / 10;
	yadd = MAX(0, (60 - (loadBox.r_ytop - loadBox.r_ybot)) / 2) +
		(loadBox.r_ytop - loadBox.r_ybot + 1) / 10;
	loadBox.r_xbot -= xadd;  loadBox.r_xtop += xadd;
	loadBox.r_ybot -= yadd;  loadBox.r_ytop += yadd;

	window->w_bbox = &(newEditUse->cu_def->cd_bbox);
	res = WindLoad(window, DBWclientID, (ClientData) newEditUse, &loadBox);
	ASSERT(res, "DBWcreate");

	/* Update the captions in all windows to reflect the new
	 * edit cell.  Also, if we've got a new edit cell, we need
	 * to explicitly ask for redisplay, because there could be
	 * another window on this cell somewhere else, and it needs
	 * to be redisplayed too (if it's just the new window, that
	 * is taken care of during WindLoad).
	 */
	CmdSetWindCaption(EditCellUse, EditRootDef);
    }

    if (newEdit)
	DBWAreaChanged(newEditDef, &newEditDef->cd_bbox, DBW_ALLWINDOWS,
	    &DBAllButSpaceBits);

    /* If the cell before loading was (UNNAMED) and it was	*/
    /* never modified, then delete it now.			*/

    /* Caveat: The (UNNAMED) cell could be on a push stack;	*/
    /* that is not fatal but should be avoided.  Since the most	*/
    /* common use is from the toolkit scripts, then make sure	*/
    /* this doesn't happen within suspendall ... resumeall.	*/

    if (deleteDef != NULL)
	DBCellDelete(deleteDef->cd_name, TRUE);
}

/* This function is called for each cell whose expansion status changed.
 * It forces the cells area to be redisplayed, then returns 0 to keep
 * looking for more cells to unexpand.
 */

int
UnexpandFunc(use, windowMask)
    CellUse *use;		/* Use that was just unexpanded. */
    int windowMask;		/* Window where it was unexpanded. */
{
    if (use->cu_parent == NULL) return 0;
    DBWAreaChanged(use->cu_parent, &use->cu_bbox, windowMask,
	    (TileTypeBitMask *) NULL);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 * DBWexit --
 *
 *	Magic is about to exit.  Check to see if any cells need to be written.
 *
 * Results:
 *	TRUE if it is OK to exit.
 *	FALSE otherwise.
 *
 * Side effects:
 *	The user is asked if he wants to write out any cells that are left.
 * ----------------------------------------------------------------------------
 */

bool
DBWexit()
{
    return (CmdWarnWrite() == 1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWcommands --
 *
 * 	This procedure is called by the window package whenever a
 *	button is pushed or command is typed while the cursor is over
 *	a database window.  This procedure dispatches to the handler
 *	for the command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the command procedure does.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWcommands(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int cmdNum;

    /* If it was a keyboard command, just dispatch to the proper
     * command routine.
     */

    if (cmd->tx_button == TX_NO_BUTTON)
    {
	WindExecute(w, DBWclientID, cmd);
    }
    else
    {
	/* It's a button. */

	(*DBWButtonCurrentProc)(w, cmd);
    }

    UndoNext();

    /* Update timestamps now that it's safe */

    DBFixMismatch();
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWNewButtonHandler --
 *
 * 	This procedure permits anyone else in Magic to take over
 *	handling of button pushes in database windows.  One example
 *	of this is the netlist editor.
 *
 * Results:
 *	The return value is the old button handler.  NULL means the
 *	default handler (moving the box and painting) was the old
 *	handler.  The caller must restore the old handler when it is
 *	finished.
 *
 * Side effects:
 *	From now on, all button pushes within database windows are
 *	passed to buttonProc, in the following form:
 *
 *	    int
 *	    buttonProc(w, cmd)
 *		MagWindow *w;
 *		TxCommand *cmd;
 *	    {
 *	    }
 *
 * ----------------------------------------------------------------------------
 */

int (*(DBWNewButtonHandler(buttonProc)))()
    int (*buttonProc)();		/* New button handler. */
{
    int (*result)();

    result = dbwButtonHandler;
    dbwButtonHandler = buttonProc;
    return result;
}

/*
 * ----------------------------------------------------------------------------
 * DBWupdate--
 *
 *	This procedure is called once during each WindUpdate call.  It
 *	takes care of redisplay stuff that's not already handled by
 *	DBWAreaChanged (e.g. highlight redisplay).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff gets redisplayed.
 * ----------------------------------------------------------------------------
 */

void
DBWupdate()
{
    DBWFeedbackShow();
    DBWHLUpdate();
}

/*
 * ----------------------------------------------------------------------------
 * DBWinit --
 *
 *	Initialize this module and open an initial window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A client is added, and an initial empty window is made
 * ----------------------------------------------------------------------------
 */

void
DBWinit()
{
    MagWindow *initialWindow;
    int i;
    static char *doc =
	"You are currently using the \"box\" tool.  The button actions are:\n"
	"   left    - move the box so its lower-left corner is at cursor position\n"
	"   right   - resize box by moving upper-right corner to cursor position\n"
	"   middle  - paint box area with material underneath cursor\n"
	"You can move or resize the box by different corners by pressing left\n"
	"    or right, holding it down, moving the cursor near a different corner\n"
	"    and clicking the other (left or right) button down then up without\n"
	"    releasing the initial button.\n";

    /* Initialize */
    DBWclientID = WindAddClient("layout", DBWcreate, DBWdelete,
	    DBWredisplay, DBWcommands, DBWupdate, DBWexit,
	    (void (*)()) NULL,
	    (GrGlyph *) NULL);

    /* Add all of the commands for the DBWind interface.  These	*/
    /* are defined in DBWCommands.c.  Commands added by other	*/
    /* modules are registered with the client by each module.	*/

    DBWInitCommands();

    DBWHLAddClient(DBWDrawBox);
    DBWAddButtonHandler("box", DBWBoxHandler, STYLE_CURS_NORMAL, doc);
    (void) DBWChangeButtonHandler("box");

    UndoDisable();
    DBCellInit();
    DBUndoInit();
    dbwUndoInit();

    /* Create initial window, but don't load anything into it. */
    WIND_MAX_WINDOWS(MAX_BITS_IN_MASK);

#ifdef MAGIC_WRAPPER
    if (MakeMainWindow)
#endif

    initialWindow = WindCreate(DBWclientID, (Rect *) NULL, TRUE,
	0, (char **) NULL);

#ifndef MAGIC_WRAPPER
    ASSERT(initialWindow != (MagWindow *) NULL, "DBWinit");
#endif

    /* Other initialization routines in the DBW package */
    dbwFeedbackInit();
    dbwElementInit();
    dbwCrosshairInit();

    UndoEnable();
}
