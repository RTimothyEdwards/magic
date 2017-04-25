/*
 * CmdRS.c --
 *
 * Commands with names beginning with the letters R through S.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdRS.c,v 1.13 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/stack.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/styles.h"
#include "database/database.h"
#include "database/fonts.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "graphics/graphics.h"
#include "utils/tech.h"
#include "drc/drc.h"
#include "textio/txcommands.h"
#include "utils/malloc.h"
#include "utils/netlist.h"
#include "netmenu/netmenu.h"
#include "select/select.h"
#include "utils/signals.h"
#include "sim/sim.h"

extern void DisplayWindow();


#if !defined(NO_SIM_MODULE) && defined(RSIM_MODULE)
/*
 * ----------------------------------------------------------------------------
 *
 * CmdRsim
 *
 * 	Starts Rsim under Magic.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Rsim is forked.
 *
 * ----------------------------------------------------------------------------
 */
  
void
CmdRsim(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{

    if ((cmd->tx_argc == 1) && (!SimRsimRunning)) {
	TxPrintf("usage: rsim [options] file\n");
	return;
    }
    if ((cmd->tx_argc != 1) && (SimRsimRunning)) {
	TxPrintf("Simulator already running.  You cannot start another.\n");
	return;
    }
    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID)) {
	TxError("Put the cursor in a layout window.\n");
	return;
    }
    if (cmd->tx_argc != 1) {
	cmd->tx_argv[cmd->tx_argc] = (char *) 0;
	SimStartRsim(cmd->tx_argv);
    }
    SimConnectRsim(FALSE);

}
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * CmdSave --
 *
 * Implement the "save" command.
 * Writes the EditCell out to a disk file.
 *
 * Usage:
 *	save [file]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the cell out to file, if specified, or the file
 *	associated with the cell otherwise.
 *	Updates the caption in the window if the name of the edit
 *	cell has changed.
 *	Clears the modified bit in the cd_flags.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdSave(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellDef *locDef;

    if (cmd->tx_argc > 2)
    {
	TxError("Usage: %s [file]\n", cmd->tx_argv[0]);
	return;
    }

    /* The "save" command can turn a read-only cell into a writeable	*/
    /* cell, or vice versa.  We should have more checks on the status	*/
    /* of the resulting file. . . .  For now, we patch things up so	*/
    /* that doing "save" on a read-only cell doesn't crash magic.	*/

    if (EditCellUse == NULL)
    {
	locDef = ((CellUse *)w->w_surfaceID)->cu_def;
	locDef->cd_flags &= ~CDNOEDIT;
    }
    else
	locDef = EditCellUse->cu_def;

    DBUpdateStamps();
    if (cmd->tx_argc == 2)
    {
	if (CmdIllegalChars(cmd->tx_argv[1], "[],", "Cell name"))
	    return;
	cmdSaveCell(locDef, cmd->tx_argv[1], FALSE, TRUE);
    }
    else cmdSaveCell(locDef, (char *) NULL, FALSE, TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdScaleGrid --
 *
 *	This procedure scales magic units with respect to lambda.  A scaling
 *	of 2 to 1, for example, makes the magic units half their former size,
 *	allowing magic to read CIF files with components on the half-lambda
 *	grid without the necessity of altering the technology file for the
 *	process.
 *
 * Usage:
 *	scalegrid a b	(or)	scalegrid a/b	(or) scalegrid a:b
 *
 *	"a" and "b" are integers
 *
 * Results:
 *	magic internal units are scaled by factor a/b.
 *
 * Side Effects:
 *	cifinput and cifoutput scale factors are multiplied by a/b.
 *	All drc widths and distances are multiplied by b/a.
 *	All layout tile coordinates are multiplied by b/a.
 *	The current drawing scale and position are altered to maintain
 *	   the current view.
 *	All windows are redrawn in case scaling has caused round-off
 *	   truncation of tile coordinates.
 *	Geometry alterations are not undoable!
 *
 * ----------------------------------------------------------------------------
 */

void
CmdScaleGrid(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    extern void DBScalePoint();
    int scalen, scaled;
    char *argsep;
    Rect rootBox;
    CellDef *rootBoxDef;

    if ((cmd->tx_argc == 2) || (cmd->tx_argc == 3))
    {
	if (cmd->tx_argc == 2)
	{
	    if (((argsep = strchr(cmd->tx_argv[1], ':')) != NULL) ||
			((argsep = strchr(cmd->tx_argv[1], '/')) != NULL))
	    {
		*argsep++ = '\0';
	        if (!StrIsInt(argsep))
		    goto scalegridusage;
		else
		    scaled = atoi(argsep);
	    }
	    else
		goto scalegridusage;
	}
	else	
	{
	    if (!StrIsInt(cmd->tx_argv[2]))
		goto scalegridusage;
	    else
		scaled = atoi(cmd->tx_argv[2]);
	}

	if (!StrIsInt(cmd->tx_argv[1]))
	    goto scalegridusage;
	else
	    scalen = atoi(cmd->tx_argv[1]);

	if (scalen <= 0 || scaled <= 0) goto scalegridusage;
	else if (scalen == scaled) goto scalegridreport;

	/* Reduce fraction by the greatest common factor */

	ReduceFraction(&scalen, &scaled);

	/* Check against CIF output style to see if we're violating a	*/
	/* minimum grid resolution limit.				*/

	if (CIFTechLimitScale(scalen, scaled) != 0)
	{
	    TxError("Grid scaling is finer than limit set by the process!\n");
	    return;
	}

	/* Scale cifinput and cifoutput */

	CIFTechInputScale(scalen, scaled, TRUE);
	CIFTechOutputScale(scalen, scaled);

	/* Scale drc rules */

	DRCTechScale(scalen, scaled);

	/* Scale plow rules (must come after DRCTechScale) */

	PlowAfterTech();

	/* Scale extract parameters */

	ExtTechScale(scalen, scaled);

	/* Scale wiring parameters */

	WireTechScale(scalen, scaled);

	/* Scale LEF parameters */

#ifdef LEF_MODULE
	LefTechScale(scalen, scaled);
#endif

#ifdef ROUTE_MODULE
	/* Scale core router parameters */

	RtrTechScale(scalen, scaled);

	/* Scale maze router parameters (must come after DRCTechScale) */

	MZAfterTech();
	IRAfterTech();

#endif
	/* Scale all tiles */

	DBScaleEverything(scaled, scalen);

	/* Save the current scale factor */

	DBLambda[0] *= scalen;
	DBLambda[1] *= scaled;
	ReduceFraction(&DBLambda[0], &DBLambda[1]);

	/* Rescale cursor box */

	if (ToolGetBox(&rootBoxDef, &rootBox))
	{
	    DBScalePoint(&rootBox.r_ll, scaled, scalen);
	    DBScalePoint(&rootBox.r_ur, scaled, scalen);
	    ToolMoveBox(TOOL_BL, &rootBox.r_ll, FALSE, rootBoxDef);
	    ToolMoveCorner(TOOL_TR, &rootBox.r_ur, FALSE, rootBoxDef);
	}

	/* Adjust all window viewing scales and positions and redraw */

	WindScale(scaled, scalen);

	/* This is harsh.  Might work to scale all distance measures in */
	/* the undo record, but this is simple and direct.		*/

	UndoFlush();

	/* TxPrintf("Magic internal unit scaled by %.3f\n",
		(float)scalen / (float)scaled);	*/

scalegridreport:
	TxPrintf("%d Magic internal unit%s = %d Lambda\n",
		DBLambda[1], (DBLambda[1] == 1) ? "" : "s", DBLambda[0]);

	return;
    }

scalegridusage:
    TxError("Usage:  scalegrid a b, where a and b are strictly positive integers\n");
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSee --
 *
 * 	This procedure is used to enable or disable display of certain
 *	things on the screen.
 *
 * Usage:
 *	see [no] stuff
 *
 *	Stuff consists of mask layers or the keyword "allSame"
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The indicated mask layers are enabled or disabled from being
 *	displayed in the current window.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdSee(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int flags;
    bool off;
    char *arg;
    TileType i, j;
    TileTypeBitMask mask, *rmask;
    DBWclientRec *crec;

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == NULL) || (w->w_client != DBWclientID))
    {
	TxError("Point to a layout window first.\n");
	return;
    }
    crec = (DBWclientRec *) w->w_clientData;

    arg = (char *) NULL;
    off = FALSE;
    flags = 0;
    if (cmd->tx_argc > 1)
    {
	if (strcmp(cmd->tx_argv[1], "no") == 0)
	{
	    off = TRUE;
	    if (cmd->tx_argc > 2) arg = cmd->tx_argv[2];
	}
	else arg = cmd->tx_argv[1];
	if ((cmd->tx_argc > 3) || ((cmd->tx_argc == 3) && !off))
	{
	    TxError("Usage: see [no] layers|allSame\n");
	    return;
	}
    }

    /* Figure out which things to set or clear.  Don't ever make space
     * invisible:  that doesn't make any sense.
     */

    if (arg != NULL)
    {
	if (strcmp(arg, "allSame") == 0)
	{
	    mask = DBZeroTypeBits;
	    flags = DBW_ALLSAME;
	}
	else
	{
	    if (!CmdParseLayers(arg, &mask))
		return;
	}
    }
    else mask = DBAllTypeBits;

    if (TTMaskHasType(&mask, L_LABEL))
	flags |= DBW_SEELABELS;
    if (TTMaskHasType(&mask, L_CELL))
	flags |= DBW_SEECELLS;
    TTMaskClearType(&mask, L_LABEL);
    TTMaskClearType(&mask, L_CELL);
    TTMaskClearType(&mask, TT_SPACE);

    if (off)
    {
	for (i = 0; i < DBNumUserLayers; i++)
	{
	    if (TTMaskHasType(&mask, i))
		TTMaskClearMask(&crec->dbw_visibleLayers,
			&DBLayerTypeMaskTbl[i]);
	}
	for (; i < DBNumTypes; i++)
	{
	    /* This part handles stacked contact types.  The display	*/
	    /* routine calls DBTreeSrUniqueTiles(), which displays	*/
	    /* stacked contact types only on their home plane.  Thus	*/
	    /* if we select a contact type, we should also select all	*/
	    /* stacking types for that contact that are on the same	*/
	    /* plane.							*/

	    rmask = DBResidueMask(i);
	    for (j = 0; j < DBNumUserLayers; j++)
		if (TTMaskHasType(rmask, j))
		    if (TTMaskHasType(&mask, j))
			if (DBPlane(i) == DBPlane(j))
			    TTMaskClearMask(&crec->dbw_visibleLayers,
					&DBLayerTypeMaskTbl[i]);
	}
	crec->dbw_flags &= ~flags;
    }
    else
    {
	for (i = 0; i < DBNumUserLayers; i++)
	{
	    if (TTMaskHasType(&mask, i))
		TTMaskSetMask(&crec->dbw_visibleLayers,
			&DBLayerTypeMaskTbl[i]);
	}
	for (; i < DBNumTypes; i++)
	{
	    rmask = DBResidueMask(i);
	    for (j = 0; j < DBNumUserLayers; j++)
		if (TTMaskHasType(rmask, j))
		    if (TTMaskHasType(&mask, j))
			if (DBPlane(i) == DBPlane(j))
			    TTMaskSetMask(&crec->dbw_visibleLayers,
					&DBLayerTypeMaskTbl[i]);
	}
	crec->dbw_flags |= flags;
    }
    WindAreaChanged(w, &w->w_screenArea);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * cmdSelectArea --
 *
 * 	This is a utility procedure used by CmdSelect to do area
 *	selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is augmented to contain all the information on
 *	layers that is visible under the box, including paint, labels,
 *	and unexpanded subcells.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
cmdSelectArea(layers, less)
    char *layers;			/* Which layers are to be selected. */
    bool less;
{
    SearchContext scx;
    TileTypeBitMask mask;
    int windowMask, xMask;
    DBWclientRec *crec;
    MagWindow *window;

    bzero(&scx, sizeof(SearchContext));
    window = ToolGetBoxWindow(&scx.scx_area, &windowMask);
    if (window == NULL)
    {
	TxPrintf("The box isn't in a window.\n");
	return;
    }

    /* Since the box may actually be in multiple windows, we have to
     * be a bit careful.  If the box is only in one window, then there's
     * no problem.  If it's in more than window, the cursor must
     * disambiguate the windows.
     */
    
    xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
    if ((windowMask & ~xMask) != 0)
    {
	window = CmdGetRootPoint((Point *) NULL, (Rect *) NULL);
        xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
	if ((windowMask & xMask) == 0)
	{
	    TxPrintf("The box is in more than one window;  use the cursor\n");
	    TxPrintf("to select the one you want to select from.\n");
	    return;
	}
    }
    if (CmdParseLayers(layers, &mask))
    {
	if (TTMaskEqual(&mask, &DBSpaceBits))
	    (void) CmdParseLayers("*,label", &mask);
	TTMaskClearType(&mask, TT_SPACE);
    }
    else return;
    
    if (less)
      {
	(void) SelRemoveArea(&scx.scx_area, &mask);
	return;
      }

    scx.scx_use = (CellUse *) window->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    crec = (DBWclientRec *) window->w_clientData;
    SelectArea(&scx, &mask, crec->dbw_bitmask);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdSelectVisible --
 *
 * 	This is a utility procedure used by CmdSelect to do area
 *	selection of visible paint.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection is augmented to contain all the information on
 *	layers that is visible under the box, including paint, labels,
 *	and expanded subcells.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
cmdSelectVisible(layers, less)
    char *layers;			/* Which layers are to be selected. */
    bool less;
{
    SearchContext scx;
    TileTypeBitMask mask;
    int windowMask, xMask;
    DBWclientRec *crec;
    MagWindow *window;

    bzero(&scx, sizeof(SearchContext));
    window = ToolGetBoxWindow(&scx.scx_area, &windowMask);
    if (window == NULL)
    {
	TxPrintf("The box isn't in a window.\n");
	return;
    }

    /* Since the box may actually be in multiple windows, we have to
     * be a bit careful.  If the box is only in one window, then there's
     * no problem.  If it's in more than window, the cursor must
     * disambiguate the windows.
     */
    
    xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
    if ((windowMask & ~xMask) != 0)
    {
	window = CmdGetRootPoint((Point *) NULL, (Rect *) NULL);
        xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
	if ((windowMask & xMask) == 0)
	{
	    TxPrintf("The box is in more than one window;  use the cursor\n");
	    TxPrintf("to select the one you want to select from.\n");
	    return;
	}
    }
    if (CmdParseLayers(layers, &mask))
    {
	if (TTMaskEqual(&mask, &DBSpaceBits))
	    (void) CmdParseLayers("*,label", &mask);
	TTMaskClearType(&mask, TT_SPACE);
    }
    else return;
    
    if (less)
      {
	(void) SelRemoveArea(&scx.scx_area, &mask);
	return;
      }

    scx.scx_use = (CellUse *) window->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    crec = (DBWclientRec *) window->w_clientData;
    {
	int i;
	for (i = 0; i < DBNumUserLayers; i++)
	{
	    if((TTMaskHasType(&mask, i)) && !(TTMaskHasType(&crec->dbw_visibleLayers, i)))
		TTMaskClearType(&mask, i);
	}
    }
    SelectArea(&scx, &mask, crec->dbw_bitmask);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSelect --
 *
 * Implement the "select" command.
 *
 * Usage:
 *	select [option args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current selection is modified.  See the user documentation
 *	for all the possible things this command can do.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdSelect(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileTypeBitMask mask;
    SearchContext scx;
    DBWclientRec *crec;
    MagWindow *window;

    /* The two tables below define the allowable selection options, and
     * also the message printed out by ":select help" to describe the
     * options (can't use the same table for both because of the presence
     * of the "more" option).  Note that there's one more entry in the
     * second table, due to a help message for ":select" with no arguments.
     */

#define SEL_AREA	 0
#define SEL_VISIBLE	 1
#define SEL_CELL	 2
#define SEL_CLEAR	 3
#define SEL_FLAT	 4
#define SEL_HELP	 5
#define SEL_KEEP	 6
#define SEL_MOVE	 7
#define SEL_PICK	 8
#define SEL_SAVE	 9
#define SEL_FEEDBACK	10
#define SEL_BOX		11
#define SEL_CHUNK	12
#define SEL_REGION	13
#define SEL_NET		14
#define SEL_SHORT	15
#define SEL_DEFAULT	16

    static char *cmdSelectOption[] =
    {
	"area",
	"visible",
	"cell",
	"clear",
	"flat",
	"help",
	"keep",
	"move",
	"pick",
	"save",
	"feedback",
	"box",
	"chunk",
	"region",
	"net",
	"short",
	NULL
    };
    static char *cmdSelectMsg[] =
    {
	"[more | less | nocycle] [layers]\n"
        "                                [de]select paint chunk/region/net under\n"
 	"                                cursor, or [de]select subcell if cursor\n"
	"	                         over space",
	"[more | less] area [layers]     [de]select all info under box in layers",
	"[more | less] visible [layers]  [de]select all visible info under box in layers",
	"[more | less | top] cell [name] [de]select cell under cursor, or \"name\"",
	"clear                           clear selection",
	"flat				 flatten the contents of the selection",
	"help                            print this message",
	"keep                            copy the selection to the layout",
	"move                            move selection to a location in the layout",
	"pick                            delete selection from layout",
	"save file                       save selection on disk in file.mag",
	"feedback [style]		 copy selection to feedback",
	"[more | less] box | chunk | region | net [layers]\n"
	"				 [de]select chunk/region/net specified by\n"
	"				 the lower left corner of the current box",
	"short name1 name2		 find shorting path between two labels",
	NULL
    };

    static TileType type = TT_SELECTBASE-1;
				/* Type of material being pointed at.
				 * Remembered across commands so that when
				 * multiples types are pointed to, consecutive
				 * selections will cycle through them.
				 */
    static Rect lastArea = {-100, -100, -200, -200};
				/* Used to remember region around what was
				 * pointed at in the last select command:  a
				 * new selection in this area causes the next
				 * bigger thing to be selected.
				 */
    static int lastCommand;	/* Serial number of last command:  the next
				 * bigger thing is only selected when there
				 * are several select commands in a row.
				 */
    static Rect chunkSelection;	/* Used to remember the size of the last chunk
				 * selected.
				 */
    static int level = 0;	/* How big a piece to select.  See definitions
				 * below.
				 */
    static CellUse *lastUse;	/* The last cellUse selected.  Used to step
				 * through multiple uses underneath the cursor.
				 */
    static Point lastIndices;	/* The array indices of the last cell selected.
				 * also used to step through multiple uses.
				 */
    static bool lessCycle = FALSE, lessCellCycle = FALSE;
    char path[200], *printPath, **msg, **optionArgs, *feedtext;
    TerminalPath tpath;
    CellUse *use;
    CellDef *rootBoxDef;
    Transform trans, rootTrans, tmp1;
    Point p, rootPoint;
    Rect r, selarea;
    ExtRectList *rlist;
    int option;
    int feedstyle;
    bool layerspec;
    bool degenerate;
    bool more = FALSE, less = FALSE, samePlace = TRUE;
#ifdef MAGIC_WRAPPER
    char *tclstr;
#endif
    
/* How close two clicks must be to be considered the same point: */

#define MARGIN 2

    bzero(&scx, sizeof(SearchContext));
    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    /* See if "more" was given.  If so, just strip off the "more" from
     * the argument list and set the "more" flag.  Similarly for options
     * "less", "nocycle", "top", and "cell".
     */
    
    if (cmd->tx_argc >= 2)
    {
	int arg1len = strlen(cmd->tx_argv[1]);

	optionArgs = &cmd->tx_argv[1];
	if (!strncmp(cmd->tx_argv[1], "more", arg1len))
	{
	    more = TRUE;
	    less = FALSE;
	    optionArgs = &cmd->tx_argv[2];
	    cmd->tx_argc--;
	}
	else if (!strncmp(cmd->tx_argv[1], "less", arg1len))
	{
	    more = FALSE;
	    less = TRUE;
	    optionArgs = &cmd->tx_argv[2];
	    cmd->tx_argc--;
	}
	else if (!strncmp(cmd->tx_argv[1], "nocycle", arg1len))
	{
	    samePlace = FALSE;
	    more = FALSE;
	    less = FALSE;
	    type = TT_SELECTBASE - 1;	   /* avoid cycling between types */
	    optionArgs = &cmd->tx_argv[2];
	    cmd->tx_argc--;
	}
	else if (!strncmp(cmd->tx_argv[1], "same", arg1len))
	{
	    /* Force this to be the same as the last selection command,	*/
	    /* even if there were other commands in between.		*/
	    lastCommand = TxCommandNumber - 1;
	    optionArgs = &cmd->tx_argv[2];
	    cmd->tx_argc--;
	}

	else if (!strncmp(cmd->tx_argv[1], "top", arg1len)) 
	{
	    if ((cmd->tx_argc >= 3) && !strncmp(cmd->tx_argv[2],
			"cell", strlen(cmd->tx_argv[2])))
		optionArgs = &cmd->tx_argv[2];
	}
    }

    /* Check the option for validity. */

    if (cmd->tx_argc == 1)
	option = SEL_DEFAULT;
    else
    {
	option = Lookup(optionArgs[0], cmdSelectOption);
	if (option < 0 && cmd->tx_argc != 2)
	{
	    TxError("\"%s\" isn't a valid select option.\n", cmd->tx_argv[1]);
	    option = SEL_HELP;
	    cmd->tx_argc = 2;
	}
	else if (option < 0)
	{
	    option = SEL_DEFAULT;
	    if (more || less)
		optionArgs = &cmd->tx_argv[1];
	    else
		optionArgs = &cmd->tx_argv[0];
	}

	/* options other than SEL_DEFAULT and the ones that cycle
	 * through (SEL_BOX/CHUNK/REGION/NET) force "level" back
	 * to 0.
	 */
	if (option != SEL_BOX && option != SEL_CHUNK && option !=
		SEL_REGION && option != SEL_NET)
	    level = 0;
    }

#ifndef NO_SIM_MODULE
    SimRecomputeSel = TRUE;
#endif

    switch (option)
    {
	/*--------------------------------------------------------------------
	 * Select everything under the box, perhaps looking only at
	 * particular layers.
	 *--------------------------------------------------------------------
	 */

	case SEL_AREA:
	    if (cmd->tx_argc > 3)
	    {
		usageError:
		TxError("Bad arguments:\n    select %s\n",
			cmdSelectMsg[option+1]);
		return;
	    }
	    if (!(more || less)) SelectClear();
	    if (cmd->tx_argc == 3)
		cmdSelectArea(optionArgs[1], less);
	    else cmdSelectArea("*,label,subcell", less);
	    return;
	
	/*--------------------------------------------------------------------
	 * Select everything under the box, perhaps looking only at
	 * particular layers, but only if its visible.
	 *--------------------------------------------------------------------
	 */

	case SEL_VISIBLE:
	    if (cmd->tx_argc > 3) goto usageError;
	    if (!(more || less)) SelectClear();
	    if (cmd->tx_argc == 3)
		cmdSelectVisible(optionArgs[1], less);
	    else cmdSelectVisible("*,label,subcell", less);
	    return;
	
	/*--------------------------------------------------------------------
	 * Clear out all of the material in the selection.
	 *--------------------------------------------------------------------
	 */

	case SEL_CLEAR:
	    if ((more) || (less) || (cmd->tx_argc > 2)) goto usageError;
	    SelectClear();
	    return;
	
	/*--------------------------------------------------------------------
	 * Print out help information.
	 *--------------------------------------------------------------------
	 */

	case SEL_HELP:
	    TxPrintf("Selection commands are:\n");
	    for (msg = &(cmdSelectMsg[0]); *msg != NULL; msg++)
		TxPrintf("    select %s\n", *msg);
	    return;

	/*--------------------------------------------------------------------
	 * Make a copy of the selection at its present loction but do not
	 * clear the selection.
	 *--------------------------------------------------------------------
	 */

	 case SEL_KEEP:
	    if ((more) || (less) || (cmd->tx_argc > 2)) goto usageError;
	    SelectAndCopy1();
	    GeoTransRect(&SelectUse->cu_transform, &SelectDef->cd_bbox, &selarea);
	    DBWHLRedraw(SelectRootDef, &selarea, FALSE);
	    return;

	/*--------------------------------------------------------------------
	 * Move the selection relative to the cell def
	 *--------------------------------------------------------------------
	 */

	 case SEL_MOVE:
	    if ((more) || (less) || (cmd->tx_argc != 4)) goto usageError;

	    p.p_x = cmdParseCoord(w, cmd->tx_argv[2], FALSE, TRUE);
	    p.p_y = cmdParseCoord(w, cmd->tx_argv[3], FALSE, FALSE);

	    /* Erase first, then recompute the transform */
	    GeoTransRect(&SelectUse->cu_transform, &SelectDef->cd_bbox, &selarea);
	    DBWHLRedraw(SelectRootDef, &selarea, TRUE);

	    GeoTransTranslate(p.p_x, p.p_y,
			&GeoIdentityTransform, &SelectUse->cu_transform);

	    GeoTransRect(&SelectUse->cu_transform, &SelectDef->cd_bbox, &selarea);
	    DBWHLRedraw(SelectRootDef, &selarea, FALSE);
	    return;

	/*--------------------------------------------------------------------
	 * Delete the selection from the layout, but don't clear the selection
	 * cell.  This allows the selection to be dragged around independently
	 * of the layout.
	 *--------------------------------------------------------------------
	 */

	 case SEL_PICK:
	    if ((more) || (less) || (cmd->tx_argc > 2)) goto usageError;
	    SelectDelete("picked", FALSE);
	    DBWHLRedraw(SelectRootDef, &selarea, FALSE);
	    return;

	/*--------------------------------------------------------------------
	 * Flatten the contents of the selection cell, but keep the result
	 * within the selection cell.
	 *--------------------------------------------------------------------
	 */

	case SEL_FLAT:
	    if ((more) || (less) || (cmd->tx_argc > 2)) goto usageError;
	    SelectFlat();
	    return;

	/*--------------------------------------------------------------------
	 * Save the selection as a new Magic cell on disk.
	 *--------------------------------------------------------------------
	 */

	 case SEL_SAVE:
	    if (cmd->tx_argc != 3) goto usageError;

	    /* Be sure to paint DRC check information into the cell before
	     * saving it!  Otherwise DRC problems may not be detected.  Also
	     * be sure to adjust labels in the cell.
	     */

	    DBAdjustLabels(SelectDef, &TiPlaneRect);
	    DBPaintPlane(SelectDef->cd_planes[PL_DRC_CHECK],
		    &SelectDef->cd_bbox,
		    DBStdPaintTbl(TT_CHECKPAINT, PL_DRC_CHECK),
		    (PaintUndoInfo *) NULL);

	    DBUpdateStamps();
	    cmdSaveCell(SelectDef, cmd->tx_argv[2], FALSE, FALSE);
	    return;

	/*--------------------------------------------------------------------
	 * Copy the selection into a feedback area for permanent display
	 *--------------------------------------------------------------------
	 */
	 case SEL_FEEDBACK:
	    feedtext = NULL;
	    feedstyle = STYLE_ORANGE1;
	    if (cmd->tx_argc > 2)
	    {
		/* Get style (To do) */
		feedstyle = GrGetStyleFromName(cmd->tx_argv[2]);
		if (feedstyle == -1)
		{
		    TxError("Unknown style %s\n", cmd->tx_argv[2]);
		    TxError("Use a number or one of the long names in the"
					" .dstyle file\n");
		    return;
		}
		if (cmd->tx_argc > 3)
		    feedtext = cmd->tx_argv[3];
	    }
	    SelCopyToFeedback(SelectRootDef, SelectUse, feedstyle,
			(feedtext == NULL) ? "selection" : feedtext);
	    GeoTransRect(&SelectUse->cu_transform, &SelectDef->cd_bbox, &selarea);
	    DBWHLRedraw(SelectRootDef, &selarea, FALSE);
	    return;

	/*--------------------------------------------------------------------
	 * Given a net selection and two labels, determine the shortest
	 * connecting path between the two labels.
	 *--------------------------------------------------------------------
	 */
	case SEL_SHORT:
	    if (cmd->tx_argc != 4) goto usageError;
	    rlist = SelectShort(cmd->tx_argv[2], cmd->tx_argv[3]);

	    if (rlist == NULL)
	    {
		TxError("No shorting path found between source and destination!\n");
		return;
	    }

	    /* Delete selection and replace with contents of rlist */

	    /* (To do:  Alternately just return a list of the contents	*/
	    /* of rlist)						*/
	    SelectClear();

	    while (rlist != NULL)
	    {
		/* Paint rlist back into SelectDef */
		DBPaint(SelectDef, &rlist->r_r, rlist->r_type);

		/* cleanup as we go */
		freeMagic(rlist);
		rlist = rlist->r_next;
	    }

	    /* Force erase and redraw of the selection */
	    DBReComputeBbox(SelectDef);
	    DBWAreaChanged(SelectDef, &SelectDef->cd_extended, DBW_ALLWINDOWS,
			(TileTypeBitMask *)NULL);
	    GeoTransRect(&SelectUse->cu_transform, &SelectDef->cd_bbox, &selarea);
	    DBWHLRedraw(SelectRootDef, &selarea, FALSE);
	    break;
	
	case SEL_BOX: case SEL_CHUNK: case SEL_REGION: case SEL_NET:
	    if (cmd->tx_argc > 3) goto usageError;
	    if (cmd->tx_argc == 3)
		layerspec = TRUE;
	    else
		layerspec = FALSE;
	    goto Okay;

	/*--------------------------------------------------------------------
	 * The default case (no args):  see what's under the cursor.  Select
	 * paint if there is any, else select a cell.  In both cases,
	 * multiple clicks cycle through larger and larger selections.  The
	 * SEL_CELL option also comes here (to share initialization code) but
	 * quickly branches away.
	 *--------------------------------------------------------------------
	 */

	case SEL_DEFAULT:
	    if (cmd->tx_argc > 2) goto usageError;
	    if (cmd->tx_argc == 2)
		layerspec = TRUE;
	    else
		layerspec = FALSE;
	    goto Okay;
	case SEL_CELL:
	    layerspec = FALSE;
	    degenerate = FALSE;
Okay:
	    if (!(more || less)) SelectClear();
	    if (option == SEL_BOX || option == SEL_CHUNK || option == SEL_REGION
			|| option == SEL_NET)
	    {
		int windowMask, xMask;

		if (!ToolGetBox (&rootBoxDef, NULL)) {
		    TxError ("Box tool must be present\n");
		    return;
		}
		window = ToolGetBoxWindow (&scx.scx_area, &windowMask);
		if (!window)
		    TxError ("Box tool does not exist in any window\n");
		xMask = ((DBWclientRec *)window->w_clientData)->dbw_bitmask;
		if ((windowMask & ~xMask) != 0) {
		    window = CmdGetRootPoint ((Point *) NULL, (Rect *) NULL);
		    xMask = ((DBWclientRec *)window->w_clientData)->dbw_bitmask;
		    if (windowMask & xMask == 0) {
			TxError("Box present in multiple windows; use the"
				"cursor\nto select the one you want\n");
			return;
		    }
		}
		scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
		scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
		degenerate = TRUE;
	    }
	    else
	    {
	        window = CmdGetRootPoint((Point *) NULL, &scx.scx_area);
	    }
	    if (window == NULL) return;
	    scx.scx_use = (CellUse *) window->w_surfaceID;
	    scx.scx_trans = GeoIdentityTransform;
	    crec = (DBWclientRec *) window->w_clientData;
	    DBSeeTypesAll(scx.scx_use, &scx.scx_area, crec->dbw_bitmask, &mask);
	    TTMaskAndMask(&mask, &crec->dbw_visibleLayers);
	    TTMaskAndMask(&mask, &DBAllButSpaceAndDRCBits);

	    /* See if we're pointing at the same place as we were the last time
	     * this command was invoked, and if this command immediately follows
	     * another selection comand.  If not, it is important to set lastUse
	     * to NULL, otherwise trouble occurs if lastUse is an instance that
	     * was deleted (note that this is not foolproof:  deleting an
	     * instance followed by selecting an instance that was occupying the
	     * same space WILL cause a crash).
	     */
	
	    if (!GEO_ENCLOSE(&cmd->tx_p, &lastArea)
		    || ((lastCommand + 1) != TxCommandNumber))
	    {
		samePlace = FALSE;
		lastUse = NULL;
	    }

	    lastArea.r_xbot = cmd->tx_p.p_x - MARGIN;
	    lastArea.r_ybot = cmd->tx_p.p_y - MARGIN;
	    lastArea.r_xtop = cmd->tx_p.p_x + MARGIN;
	    lastArea.r_ytop = cmd->tx_p.p_y + MARGIN;
	    lastCommand = TxCommandNumber;

	    /* If there's material under the cursor, select some paint.
	     * Repeated selections at the same place result in first a
	     * chunk being selected, then a region of a particular type,
	     * then a whole net.
	     */

	    if (!TTMaskIsZero(&mask) && (option != SEL_CELL))
	    {
		if (layerspec == TRUE)
		{
		    /* User specified a layer.  Use the smallest type
		     * specified by the user and present under the
		     * box/cursor to begin the selection
		     */
		    TileTypeBitMask uMask;
	
		    if (CmdParseLayers (optionArgs[1], &uMask))
		    {
			if (TTMaskEqual (&uMask, &DBSpaceBits))
			    CmdParseLayers ("*,label", &uMask);
		    }
		    else
		    {
			TxError ("Invalid layer specification\n");
			return;
		    }

		    TTMaskAndMask (&mask, &uMask);

		    if (TTMaskIsZero(&mask))
		    {
			/* If the area was degenerate (point or line box) */
			/* try looking in the other direction before	  */
			/* giving up (added by Tim 3/18/07)		  */

			if (degenerate)
			{
			    scx.scx_area.r_xtop--;
			    scx.scx_area.r_ytop--;
			    scx.scx_area.r_xbot--;
			    scx.scx_area.r_ybot--;

			    DBSeeTypesAll(scx.scx_use, &scx.scx_area,
					crec->dbw_bitmask, &mask);
			    TTMaskAndMask(&mask, &crec->dbw_visibleLayers);
			    TTMaskAndMask(&mask, &DBAllButSpaceAndDRCBits);
			    TTMaskAndMask (&mask, &uMask);
			    if (TTMaskIsZero(&mask))
			    {
				TxError ("No paint of this type under "
					"or next to the cursor/box\n");
				return;
			    }
			}
			else
			{
			    TxError ("No paint of this type under the cursor/box\n");
			    return;
			}
		    }
		}

		/* Set connectivity searching level */

		if (option == SEL_CHUNK || option == SEL_REGION || option == SEL_NET)
		{
		    samePlace = TRUE;
		    level = option;

		    if (level != SEL_CHUNK)
		    {
			int count = 0;
			for (type++; count <= DBNumUserLayers; type++, count++)
			{
			    if (type >= DBNumUserLayers)
				type = TT_SELECTBASE;
			    if (TTMaskHasType(&mask, type)) break;
			}
		    }
		}
		else
		{
		    if (samePlace && lessCycle == less)
		    {
			level++;
			if (level > SEL_NET) level = SEL_CHUNK;
		    }
		    else level = SEL_CHUNK;

		    if ((level == 1) || (level != SEL_CHUNK &&
				!TTMaskHasType (&mask, type))) {
			/* User specified a new mask, and the current tile
			 * type being expanded is not in the set of types
			 * which the user wants us to use => reset level
			 */
			level = SEL_CHUNK;
			SelectClear();
		    }
		}

		lessCycle = less;

		if (level == SEL_CHUNK)
		{
		    /* Pick a tile type to use for selection.  If there are
		     * several different types under the cursor, pick one of
		     * them.  This code remembers which type was used to
		     * choose last time, so that consecutive selections will
		     * use different types.
		     */

		    int count = 0;
		    for (type++; count <= DBNumUserLayers; type++, count++)
		    {
			if (type >= DBNumUserLayers)	/* used to be DBNumTypes */
			    type = TT_SELECTBASE;
			if (TTMaskHasType(&mask, type)) break;
		    }

		    /* Check for selection between DBNumUserLayers & DBNumTypes */
		    /* Normally we don't want to use these types;  only if one  */
		    /* is the only bit set in the list.				*/

		    if (count == DBNumUserLayers)
			for (type = DBNumUserLayers; type < DBNumTypes; type++)
			    if (TTMaskHasType(&mask, type)) break;

		    /* Sanity check */
		    if (type == DBNumTypes) return;

		    SelectChunk(&scx, type, crec->dbw_bitmask, &chunkSelection, less);
		    if (!less)
		      DBWSetBox(scx.scx_use->cu_def, &chunkSelection);
		}
		if (level == SEL_REGION)
		{
		    /* If a region has the same size as the preceding chunk,
		     * then we haven't added anything to the selection, so
		     * go on immediately and select the whole net.
		     */

		    Rect area;

		    SelectRegion(&scx, type, crec->dbw_bitmask, &area, less);
		    if (GEO_SURROUND(&chunkSelection, &area))
			level = SEL_NET;
		}
		if (level == SEL_NET)
		{
		    SelectNet(&scx, type, crec->dbw_bitmask, (Rect *) NULL, less);
		}

		return;
	    }

	/*--------------------------------------------------------------------
	 * We get here either if the SEL_CELL option is requested, or under
	 * the SEL_DEFAULT case where there's no paint under the mouse.  In
	 * this case, select a subcell.
	 *--------------------------------------------------------------------
	 */

	    if (layerspec == TRUE)
	    {
		TileTypeBitMask uMask;

		if (CmdParseLayers (optionArgs[1], &uMask))
		{
		    TxError ("No paint of this type under the cursor/box\n");
		}
		else
		{
		    TxError ("Invalid layer specification\n");
		}
		return;
	    }

	    if (cmd->tx_argc > 3) goto usageError;

	    /* If an explicit cell use id is provided, look for that cell
	     * and select it.  In this case, defeat all of the "multiple
	     * click" code.
	     */
	    
	    if ((cmd->tx_argc == 3) && (optionArgs == &cmd->tx_argv[2]))
	    {
		use = lastUse = scx.scx_use;
		p.p_x = scx.scx_use->cu_xlo;
		p.p_y = scx.scx_use->cu_ylo;
		trans = GeoIdentityTransform;
		printPath = scx.scx_use->cu_id;
	    }
	    else if (cmd->tx_argc == 3)
	    {
		SearchContext scx2;

		bzero(&scx2, sizeof(SearchContext));
		DBTreeFindUse(optionArgs[1], scx.scx_use, &scx2);
		use = scx2.scx_use;
		if (use == NULL)
		{
		    TxError("Couldn't find a cell use named \"%s\"\n",
			    optionArgs[1]);
		    return;
		}
		trans = scx2.scx_trans;
		p.p_x = scx2.scx_x;
		p.p_y = scx2.scx_y;
		printPath = optionArgs[1];
		samePlace = FALSE;
		lastArea.r_xbot = lastArea.r_ybot = -1000;
		lastArea.r_xtop = lastArea.r_ytop = -1000;
	    }
	    else
	    {
		/* Find the cell underneath the cursor.  If this is a
		 * second or later click at the same position, select
		 * the "next" cell underneath the point (see comments
		 * in DBSelectCell() for what "next" means).
		 */

		tpath.tp_first = tpath.tp_next = path;
		tpath.tp_last = &path[(sizeof path) - 2];
		if ((lastUse == scx.scx_use) || !samePlace || (lessCellCycle != less))
		    lastUse = NULL;
		lessCellCycle = less;
		use = DBSelectCell(scx.scx_use, lastUse, &lastIndices,
			&scx.scx_area, crec->dbw_bitmask, &trans, &p, &tpath);
    
		/* Use the window's root cell if nothing else is found. */

		if (use == NULL)
		{
		    use = lastUse = scx.scx_use;
		    p.p_x = scx.scx_use->cu_xlo;
		    p.p_y = scx.scx_use->cu_ylo;
		    trans = GeoIdentityTransform;
		    printPath = scx.scx_use->cu_id;
		}
		else
		{
		    printPath = strchr(path, '/');
		    if (printPath == NULL)
			printPath = path;
		    else printPath++;
		}
	    }

	    lastUse = use;
	    lastIndices = p;

	    /* The translation stuff is funny, since we got one
	     * element of the array, but not necessarily the
	     * lower-left element.  To get the transform for the
	     * array as a whole, subtract off for the indx of
	     * the element.
	     */

	    GeoInvertTrans(DBGetArrayTransform(use, p.p_x, p.p_y), &tmp1);
	    GeoTransTrans(&tmp1, &trans, &rootTrans);

	    if (less)
	      SelectRemoveCellUse(use, &rootTrans);
	    else
	      SelectCell(use, scx.scx_use->cu_def, &rootTrans, samePlace);
	    GeoTransRect(&trans, &use->cu_def->cd_bbox, &r);
	    DBWSetBox(scx.scx_use->cu_def, &r);

#ifdef MAGIC_WRAPPER
	    tclstr = Tcl_escape(printPath);
	    Tcl_SetResult(magicinterp, tclstr, TCL_DYNAMIC);
#else
	    TxPrintf("Selected cell is %s (%s)\n", use->cu_def->cd_name,
		    printPath);
#endif
	    return;
    }
}

/*
 *-----------------------------------------------------------------------
 * Callback functions used with CmdSetLabel to change individual labels
 *-----------------------------------------------------------------------
 */

int
cmdLabelTextFunc(label, cellUse, transform, text)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    char *text;
{
    Label *newlab;
    CellDef *cellDef = cellUse->cu_def;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (text == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewStringObj(label->lab_text, -1));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%s\n", label->lab_text);
#endif
    }
    else if (cellDef == EditRootDef)
    {
	if (strcmp(text, label->lab_text))
	{
	    newlab = DBPutFontLabel(cellDef, &label->lab_rect, label->lab_font,
			label->lab_size, label->lab_rotate, &label->lab_offset,
			label->lab_just, text, label->lab_type, label->lab_flags);
	    DBEraseLabelsByContent(cellDef, &label->lab_rect, -1, label->lab_text);
	    DBWLabelChanged(cellDef, newlab, DBW_ALLWINDOWS);
	}
    }
    return 0;
}

int
cmdLabelRotateFunc(label, cellUse, transform, value)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    int *value;
{
    CellDef *cellDef = cellUse->cu_def;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (value == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewIntObj(label->lab_rotate));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%d\n", label->lab_rotate);
#endif
    }
    else if (cellDef == EditRootDef)
    {
	DBUndoEraseLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
	label->lab_rotate = *value;
	DBFontLabelSetBBox(label);
	DBUndoPutLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
    }
    return 0;
}

int
cmdLabelSizeFunc(label, cellUse, transform, value)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    int *value;
{
    CellDef *cellDef = cellUse->cu_def;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (value == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewDoubleObj((double)label->lab_size / 8.0));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%g\n", (double)label->lab_size / 8.0);
#endif
    }
    else if (cellDef == EditRootDef)
    {
	DBUndoEraseLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
	label->lab_size = *value;
	DBFontLabelSetBBox(label);
	DBUndoPutLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
    }
    return 0;
}

int
cmdLabelJustFunc(label, cellUse, transform, value)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    int *value;
{
    CellDef *cellDef = cellUse->cu_def;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (value == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewStringObj(GeoPosToName(label->lab_just), -1));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%s\n", GeoPosToName(label->lab_just));
#endif
    }
    else if (cellDef == EditRootDef)
    {
	DBUndoEraseLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
	label->lab_just = *value;
	DBFontLabelSetBBox(label);
	DBUndoPutLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
    }
    return 0;
}

int
cmdLabelLayerFunc(label, cellUse, transform, value)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    int *value;
{
    CellDef *cellDef = cellUse->cu_def;
    TileType ttype;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (value == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewStringObj(DBTypeLongNameTbl[label->lab_type], -1));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%s\n", DBTypeLongNameTbl[label->lab_type]);
#endif
    }
    else if (cellDef == EditRootDef)
    {
	ttype = (TileType)(*value);
	if (label->lab_type != ttype)
	{
	    DBUndoEraseLabel(cellDef, label);
	    label->lab_type = ttype;
	    DBUndoPutLabel(cellDef, label);
	    DBCellSetModified(cellDef, TRUE);
	}
    }
    return 0;
}

int
cmdLabelStickyFunc(label, cellUse, transform, value)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    int *value;
{
    CellDef *cellDef = cellUse->cu_def;
    int newvalue;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (value == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewBooleanObj((label->lab_flags & LABEL_STICKY) ? 1 : 0));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%s\n", (label->lab_flags & LABEL_STICKY) ? "true" : "false");
#endif
    }
    else if (cellDef == EditRootDef)
    {
	newvalue = label->lab_flags;
	newvalue &= ~LABEL_STICKY;
	newvalue |= *value;
	if (newvalue != label->lab_flags)
	{
	    /* Label does not change appearance, just need to record the change */
	    DBUndoEraseLabel(cellDef, label);
	    label->lab_flags = newvalue;
	    DBUndoPutLabel(cellDef, label);
	}
    }
    return 0;
}

int
cmdLabelOffsetFunc(label, cellUse, transform, point)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    Point *point;
{
    CellDef *cellDef = cellUse->cu_def;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj, *pobj;
#endif

    if (point == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	pobj = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(magicinterp, lobj, pobj);
	Tcl_ListObjAppendElement(magicinterp, pobj,
			Tcl_NewDoubleObj((double)label->lab_offset.p_x / 8.0));
	Tcl_ListObjAppendElement(magicinterp, pobj,
			Tcl_NewDoubleObj((double)label->lab_offset.p_y / 8.0));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	TxPrintf("%g %g\n", (double)(label->lab_offset.p_x) / 8.0,
		(double)(label->lab_offset.p_y) / 8.0);
#endif
    }
    else if (cellDef == EditRootDef)
    {
	DBUndoEraseLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
	label->lab_offset = *point;
	DBFontLabelSetBBox(label);
	DBUndoPutLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
    }
    return 0;
}

int
cmdLabelFontFunc(label, cellUse, transform, font)
    Label *label;
    CellUse *cellUse;
    Transform *transform;
    int *font;
{
    CellDef *cellDef = cellUse->cu_def;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    if (font == NULL)
    {
#ifdef MAGIC_WRAPPER
	lobj = Tcl_GetObjResult(magicinterp);
	if (label->lab_font == -1)
	    Tcl_ListObjAppendElement(magicinterp, lobj, Tcl_NewStringObj("default", 7));
	else
	    Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewStringObj(DBFontList[label->lab_font]->mf_name, -1));
	Tcl_SetObjResult(magicinterp, lobj);
#else
	if (label->lab_font == -1)
	    TxPrintf("default\n");
	else
	    TxPrintf("%s\n", DBFontList[label->lab_font]->mf_name);
#endif
    }
    else if (cellDef == EditRootDef)
    {
	DBUndoEraseLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
	label->lab_font = *font;
	if ((*font > -1) && (label->lab_size == 0)) label->lab_size = DBLambda[1];
	DBFontLabelSetBBox(label);
	DBUndoPutLabel(cellDef, label);
	DBWLabelChanged(cellDef, label, DBW_ALLWINDOWS);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSetLabel --
 *
 * Implement the "setlabel" command.
 * Query or change properties of a (selected) label in the edit cell
 *
 * Usage:
 *	setlabel option [name]
 *
 *
 * Option may be one of:
 *	text
 *	font
 *	justify
 *	size
 *	offset
 *	rotate
 *	layer
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified label entry.  The "setlabel font" command may load font
 *	information if the requested font is not already in the font list.
 *	"setlabel font <name>" can be used without any select to load fonts
 *	from a startup script.
 *
 * ----------------------------------------------------------------------------
 */

#define SETLABEL_TEXT		0
#define SETLABEL_FONT		1
#define SETLABEL_FONTLIST	2
#define SETLABEL_JUSTIFY	3
#define SETLABEL_SIZE		4
#define SETLABEL_OFFSET		5
#define SETLABEL_ROTATE		6
#define SETLABEL_STICKY		7
#define SETLABEL_LAYER		8
#define SETLABEL_HELP		9

void
CmdSetLabel(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int pos = -1, font = -1, size = 0, rotate = 0, flags = 0;
    char **msg;
    Point offset;
    TileType ttype;
    int option;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    static char *cmdLabelYesNo[] = { "no", "false", "off", "0",
		"yes", "true", "on", "1", 0 };

    static char *cmdLabelSetOption[] =
    {
	"text <text>		change/get label text",
	"font <name>		change/get label font",
	"fontlist		list available fonts",
	"justify <position>	change/get label justification",
	"size <value>		change/get label size",
	"offset <x> <y>		change/get label offset",
	"rotate <degrees>	change/get label rotation",
	"sticky [true|false]	change/get sticky property",
	"layer <type>		change/get layer type",
	"help			print this help info",
	NULL
    };

    if (cmd->tx_argc < 2 || cmd->tx_argc > 4)
	option = SETLABEL_HELP;
    else
	option = Lookup(cmd->tx_argv[1], cmdLabelSetOption);

    switch (option)
    {
	case SETLABEL_FONTLIST:
#ifdef MAGIC_WRAPPER
	    lobj = Tcl_NewListObj(0, NULL);
	    for (font = 0; font < DBNumFonts; font++)
		Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewStringObj(DBFontList[font]->mf_name, -1));
	    Tcl_SetObjResult(magicinterp, lobj);
#else
	    for (font = 0; font < DBNumFonts; font++)
		TxPrintf("%s ", DBFontList[font]->mf_name);
	    TxPrintf("\n");
#endif
	    break;
	
	case SETLABEL_TEXT:
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelTextFunc, (cmd->tx_argc == 3) ?
			(ClientData)cmd->tx_argv[2] : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_FONT:
	    if (cmd->tx_argc >= 2 && cmd->tx_argc <= 4)
	    {
		if ((cmd->tx_argc == 3) && StrIsInt(cmd->tx_argv[2]))
		{
		    int font = atoi(cmd->tx_argv[2]);
		    if (font < -1 || font >= DBNumFonts)
		    {
			if (DBNumFonts == 0)
			    TxError("No vector outline fonts are loaded.\n");
			else
			    TxError("Font index out of range (0 to %d)\n",
					DBNumFonts - 1);
		    }
		    else if (font == -1)
			TxPrintf("default\n");
		    else
			TxPrintf("%s\n", DBFontList[font]->mf_name);
		}
		else if ((cmd->tx_argc == 3 || cmd->tx_argc == 4) &&
				!StrIsInt(cmd->tx_argv[2]))
		{
		    font = DBNameToFont(cmd->tx_argv[2]);
		    if (font < -1)
		    {
			float scale = 1.0;
			if ((cmd->tx_argc == 4) && StrIsNumeric(cmd->tx_argv[3]))
			    scale = (float)atof(cmd->tx_argv[3]);
			if (DBLoadFont(cmd->tx_argv[2], scale) != 0)
			    TxError("Error loading font \"%s\"\n", cmd->tx_argv[2]);
			font = DBNameToFont(cmd->tx_argv[2]);
			if (font < -1) break;
		    }
		}
	    
		if (EditCellUse)
		{
		    SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
				cmdLabelFontFunc, (cmd->tx_argc == 3) ?
				(ClientData)&font : (ClientData)NULL);
		}
	    }
	    break;

	case SETLABEL_JUSTIFY:
	    if (cmd->tx_argc == 3)
	    {
		pos = GeoNameToPos(cmd->tx_argv[2], FALSE, TRUE);
		if (pos < 0) break;
	    }
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelJustFunc, (cmd->tx_argc == 3) ?
			(ClientData)&pos : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_SIZE:
	    if (cmd->tx_argc == 3)
	    {
		if (StrIsNumeric(cmd->tx_argv[2]))
		    size = cmdScaleCoord(w, cmd->tx_argv[2], TRUE, TRUE, 8);
		if (size <= 0) break;
	    }
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelSizeFunc, (cmd->tx_argc == 3) ?
			(ClientData)&size : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_OFFSET:
	    if (cmd->tx_argc == 3)
	    {
		char *yp;
		if ((yp = strchr(cmd->tx_argv[2], ' ')) != NULL)
		{
		    offset.p_x = cmdScaleCoord(w, cmd->tx_argv[2], TRUE, TRUE, 8);
		    offset.p_y = cmdScaleCoord(w, yp, TRUE, FALSE, 8);
		}
		else
		{
		    TxError("Usage:  setlabel offset <x> <y>\n");
		    return;
		}
	    }
	    else if (cmd->tx_argc == 4)
	    {
		offset.p_x = cmdScaleCoord(w, cmd->tx_argv[2], TRUE, TRUE, 8);
		offset.p_y = cmdScaleCoord(w, cmd->tx_argv[3], TRUE, FALSE, 8);
	    }	
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelOffsetFunc, (cmd->tx_argc != 2) ?
			(ClientData)&offset : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_ROTATE:
	    if (cmd->tx_argc == 3)
	    {
		if (StrIsInt(cmd->tx_argv[2]))
		    rotate = atoi(cmd->tx_argv[2]);
	    }
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelRotateFunc, (cmd->tx_argc == 3) ?
			(ClientData)&rotate : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_STICKY:
	    if (cmd->tx_argc == 3)
	    {
		option = Lookup(cmd->tx_argv[2], cmdLabelYesNo);
		if (option < 0)
		{
		    TxError("Unknown sticky option \"%s\"\n", cmd->tx_argv[2]);
		    break;
		}
		flags = (option <= 3) ? 0 : LABEL_STICKY;
	    }
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelStickyFunc, (cmd->tx_argc == 3) ?
			(ClientData)&flags : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_LAYER:
	    if (cmd->tx_argc == 3)
	    {
		if (!strcasecmp(cmd->tx_argv[2], "default"))
		    ttype = -1;
		else
		{
		    ttype = DBTechNoisyNameType(cmd->tx_argv[2]);
		    if (ttype < 0) break;
		}
	    }
	    if (EditCellUse)
	    {
		SelEnumLabels(&DBAllTypeBits, TRUE, (bool *)NULL,
			cmdLabelLayerFunc, (cmd->tx_argc == 3) ?
			(ClientData)&ttype : (ClientData)NULL);
	    }
	    break;

	case SETLABEL_HELP:
	    TxError("Usage:  setlabel [option], where [option] is one of:\n");
	    for (msg = &(cmdLabelSetOption[0]); *msg != NULL; msg++)
	    {
	        TxError("    %s\n", *msg);
	    }
	    break;

	default:
	    TxError("Unknown setlabel option \"%s\"\n", cmd->tx_argv[1]);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSideways --
 *
 * Implement the "sideways" command.
 *
 * Usage:
 *	sideways
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection and box are flipped left-to-right, using the
 *	center of the selection as the axis for flipping.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdSideways(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Transform trans;
    Rect rootBox, bbox;
    CellDef *rootDef;

    if (cmd->tx_argc != 1)
    {
	TxError("Usage: %s\n", cmd->tx_argv[0]);
	return;
    }
    if (!ToolGetEditBox((Rect *)NULL)) return;

    /* To flip the selection sideways, first flip it around the
     * y-axis, then move it back so its lower-left corner is in
     * the same place that it used to be.
     */
    
    GeoTransRect(&GeoSidewaysTransform, &SelectDef->cd_bbox, &bbox);
    GeoTranslateTrans(&GeoSidewaysTransform,
	SelectDef->cd_bbox.r_xbot - bbox.r_xbot,
	SelectDef->cd_bbox.r_ybot - bbox.r_ybot, &trans);

    SelectTransform(&trans);

    /* Flip the box, if it exists and is in the same window as the
     * selection.
     */
    
    if (ToolGetBox(&rootDef, &rootBox) && (rootDef == SelectRootDef))
    {
	Rect newBox;

	GeoTransRect(&trans, &rootBox, &newBox);
	DBWSetBox(rootDef, &newBox);
    }

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdShell
 *
 * Implement the "shell" or "!" command.
 *
 * Usage:
 *	shell [command]
 *
 * Results:
 *	Executes the command in a unix shell
 *
 * Side effects:
 *	May alter unix files
 *
 * ----------------------------------------------------------------------------
 */

void
CmdShell(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int i, cmdlength;
    char *command;

    if (cmd->tx_argc != 1) {
	cmdlength = 1;
	for (i = 1; i < cmd->tx_argc; i++) {
	    cmdlength = cmdlength + strlen(cmd->tx_argv[i]) + 1;
	}
	command = mallocMagic((unsigned) (cmdlength));
	(void) strcpy(command, cmd->tx_argv[1]);
	for (i = 2; i < cmd->tx_argc; i++) {
	    strcat(command, " ");
	    strcat(command, cmd->tx_argv[i]);
	}
	system(command);
	freeMagic(command);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSgraph
 *
 * Implement the "sgraph" command.
 *
 * Usage:
 *	sgraph [off|add|delete|debug]
 *	sgraph [show|auto] [vertical|horizontal]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the 'stretchgraph' module, if present.
 *
 * ----------------------------------------------------------------------------
 */

#ifdef	LLNL
int (*CmdStretchCmd)() = NULL;
    /* ARGSUSED */

void
CmdSgraph(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    if (CmdStretchCmd != NULL)
    {
	(*CmdStretchCmd)(w, cmd);
    }
    else
    {
	TxError("Sorry, the sgraph command doesn't work in this version.\n");
	TxError("(Magic was not linked with stretchgraph module.)\n");
    }
}
#endif	/* LLNL */

#if !defined(NO_SIM_MODULE) && defined(RSIM_MODULE)
/*
 * ----------------------------------------------------------------------------
 *
 * CmdStartRsim
 *
 * 	This command starts Rsim under Magic, escapes Rsim, and returns
 *	back to Magic.
 *
 * Results:
 *	Rsim is forked from Magic.
 * 
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdStartRsim(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    static char rsimstr[] = "rsim";

    if ((cmd->tx_argc == 1) && (!SimRsimRunning)) {
	TxPrintf("usage: startrsim [options] file\n");
	return;
    }
    if ((cmd->tx_argc != 1) && (SimRsimRunning)) {
	TxPrintf("Simulator already running.  You cannont start another.\n");
	return;
    }
    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID)) {
	TxError("Put the cursor in a layout window.\n");
	return;
    }

    /* change argv[0] to be "rsim" and send it to Rsim_start */

    cmd->tx_argv[0] = rsimstr;
    if (cmd->tx_argc != 1) {
	cmd->tx_argv[cmd->tx_argc] = (char *) 0;
	SimStartRsim(cmd->tx_argv);
    }
    SimConnectRsim(TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSimCmd
 *
 *	Applies the given rsim command to the currently selected nodes.
 *
 * Results:
 *	Whatever rsim replys to the commands input.
 *
 * Side effects:
 *	None.
 *
 * ---------------------------------------------------------------------------- 
 */

void
CmdSimCmd(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    static char cmdbuf[200];
    char 	*strptr;
    char 	*nodeCmd;
    int 	i;

    if (!SimRsimRunning) {
	TxPrintf("You must first start the simulator by using the rsim command.\n");
	return;
    }
    if (cmd->tx_argc == 1) {
	TxPrintf("usage: simcmd command [options]\n");
	return;
    }
    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID)) {
	TxError("Put the cursor in a layout window.\n");
	return;
    }

    /* check to see whether to apply the command to each node selected,
     * or whether to just ship the command to rsim without any node
     * names.
     */
    nodeCmd = SimGetNodeCommand( cmd->tx_argv[1] );

    strcpy( cmdbuf, (nodeCmd != NULL) ? nodeCmd : cmd->tx_argv[1] );
    strptr = cmdbuf + strlen(cmdbuf);
    *strptr++ = ' ';
    *strptr = '\0';

    for (i = 2; i <= cmd->tx_argc - 1; i++) {
	strcpy(strptr, cmd->tx_argv[i]);
	strcat(strptr, " ");
	strptr += strlen(strptr) + 1;
    }

    if (nodeCmd != NULL) {
	SimSelection(cmdbuf);
    }
    else {
	SimRsimIt(cmdbuf, "");

        while (TRUE) {
	    if (!SimGetReplyLine(&strptr)) {
		break;
	    }
	    if (!strptr) {
		break;
	    }
	    TxPrintf("%s\n", strptr);
	}
    }
}
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * CmdSnap --
 *
 * Set the box snapping to align either with the nearest user-defined grid,
 * the nearest integer lambda value, or turn snapping off (aligns to internal
 * units).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

#define SNAP_OFF		0
#define SNAP_INTERNAL		1
#define SNAP_LAMBDA		2
#define SNAP_GRID		3
#define SNAP_USER		4
#define SNAP_ON			5
#define SNAP_LIST		6

void
CmdSnap(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    static char *names[] = { "off", "internal", "lambda", "grid", "user", "on",
		"list", 0 };
    int n = SNAP_LIST;
    DBWclientRec *crec;

    if (cmd->tx_argc < 2) goto printit;

    n = Lookup(cmd->tx_argv[1], names);
    if (n < 0)
    {
	TxPrintf("Usage: snap [internal | lambda | user]\n");
	return;
    }
    switch (n)
    {
	case SNAP_OFF: case SNAP_INTERNAL:
	    DBWSnapToGrid = DBW_SNAP_INTERNAL;
	    return;
	case SNAP_LAMBDA:
	    DBWSnapToGrid = DBW_SNAP_LAMBDA;
	    return;
	case SNAP_GRID: case SNAP_USER: case SNAP_ON:
	    DBWSnapToGrid = DBW_SNAP_USER;
	    return;
    }

printit:
    if (n == SNAP_LIST)  /* list */
#ifdef MAGIC_WRAPPER
	Tcl_SetResult(magicinterp, 
		(DBWSnapToGrid == DBW_SNAP_INTERNAL) ? "internal" :
		((DBWSnapToGrid == DBW_SNAP_LAMBDA) ? "lambda" : "user"),
		TCL_VOLATILE);
#else
	TxPrintf("%s\n", (DBWSnapToGrid == DBW_SNAP_INTERNAL) ? "internal" :
		((DBWSnapToGrid == DBW_SNAP_LAMBDA) ? "lambda" : "user"));
#endif
    else
	TxPrintf("Box is aligned to %s grid\n",
		(DBWSnapToGrid == DBW_SNAP_INTERNAL) ? "internal" :
		((DBWSnapToGrid == DBW_SNAP_LAMBDA) ? "lambda" : "user"));
}



/*
 * ----------------------------------------------------------------------------
 *
 * CmdSplit --
 *
 * Split a tile with a diagonal (nonmanhattan geometry)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the geometry of the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdSplit(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect editRect, expRect;
    TileTypeBitMask mask1, mask2, *cmask;
    TileType t, dir, side, diag;
    int pNum, direction;
    PaintUndoInfo ui;

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    if (cmd->tx_argc != 3 && cmd->tx_argc != 4)
    {
	TxError("Usage: %s dir layer [layer2]\n", cmd->tx_argv[0]);
	return;
    }

    if (!ToolGetEditBox(&editRect)) return;

    if (!CmdParseLayers(cmd->tx_argv[2], &mask1))
        return;

    if ((direction = GeoNameToPos(cmd->tx_argv[1], FALSE, TRUE)) < 0)
	return;

    if (cmd->tx_argc == 4)
    {
	if (!CmdParseLayers(cmd->tx_argv[3], &mask2))
	    return;
	TTMaskClearType(&mask2, TT_SPACE);
    }
    else
	TTMaskZero(&mask2);

    TTMaskClearType(&mask1, TT_SPACE);

    direction = (direction >> 1) - 1;
    dir = (direction & 0x1) ? 0 : TT_DIRECTION;

    for (t = TT_SPACE + 1; t < DBNumTypes; t++)
    {
	side = (direction & 0x2) ? 0 : TT_SIDE;
	for (cmask = &mask1; cmask != NULL; cmask = ((cmask == &mask1) ? &mask2 : NULL))
	{
	    if (cmask == &mask2) side = (side) ? 0 : TT_SIDE;
	    diag = DBTransformDiagonal(TT_DIAGONAL | dir | side, &RootToEditTransform);

	    if (TTMaskHasType(cmask, t))
	    {
		EditCellUse->cu_def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
		ui.pu_def = EditCellUse->cu_def;
		for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
		    if (DBPaintOnPlane(t, pNum))
		    {
			ui.pu_pNum = pNum;
			DBNMPaintPlane(EditCellUse->cu_def->cd_planes[pNum],
				diag, &editRect, DBStdPaintTbl(t, pNum), &ui);
			GEO_EXPAND(&editRect, 1, &expRect);
			DBMergeNMTiles(EditCellUse->cu_def->cd_planes[pNum],
				&expRect, &ui);
		    }
	    }
	}
    }

    SelectClear();
    DBWAreaChanged(EditCellUse->cu_def, &editRect, DBW_ALLWINDOWS, &mask1);
    DBWAreaChanged(EditCellUse->cu_def, &editRect, DBW_ALLWINDOWS, &mask2);
    DBReComputeBbox(EditCellUse->cu_def);
    DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &editRect);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSplitErase --
 *
 * Erase a diagonal section from a tile (nonmanhattan geometry)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the geometry of the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdSplitErase(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect editRect, expRect;
    TileTypeBitMask mask;
    TileType t, dir, side, diag;
    int pNum, direction;
    PaintUndoInfo ui;

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    if (cmd->tx_argc != 2 && cmd->tx_argc != 3)
    {
	TxError("Usage: %s dir [layer]\n", cmd->tx_argv[0]);
	return;
    }

    if (!ToolGetEditBox(&editRect)) return;

    if ((direction = GeoNameToPos(cmd->tx_argv[1], FALSE, TRUE)) < 0)
	return;

    if (cmd->tx_argc == 2)
	(void) CmdParseLayers("*", &mask);
    else if (!CmdParseLayers(cmd->tx_argv[2], &mask))
	return;

    if (TTMaskEqual(&mask, &DBSpaceBits))
	(void) CmdParseLayers("*,label", &mask);
    TTMaskClearType(&mask, TT_SPACE);
    if (TTMaskIsZero(&mask))
	return;

    direction = (direction >> 1) - 1;
    dir = (direction & 0x1) ? 0 : TT_DIRECTION;

    for (t = TT_SPACE + 1; t < DBNumTypes; t++)
    {
	side = (direction & 0x2) ? 0 : TT_SIDE;
	diag = DBTransformDiagonal(TT_DIAGONAL | dir | side, &RootToEditTransform);

	if (TTMaskHasType(&mask, t))
	{
	    EditCellUse->cu_def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
	    ui.pu_def = EditCellUse->cu_def;
	    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
		if (DBPaintOnPlane(t, pNum))
		{
		    ui.pu_pNum = pNum;
		    DBNMPaintPlane(EditCellUse->cu_def->cd_planes[pNum],
				diag, &editRect, DBStdEraseTbl(t, pNum), &ui);
		    GEO_EXPAND(&editRect, 1, &expRect);
		    DBMergeNMTiles(EditCellUse->cu_def->cd_planes[pNum],
				&expRect, &ui);
		}
	}
    }

    SelectClear();
    DBWAreaChanged(EditCellUse->cu_def, &editRect, DBW_ALLWINDOWS, &mask);
    DBReComputeBbox(EditCellUse->cu_def);
    DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &editRect);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdStretch --
 *
 * Implement the "stretch" command.
 *
 * Usage:
 *	stretch [direction [distance]]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Moves everything that's currently selected, erases material that
 *	the selection would sweep over, and fills in material behind the
 *	selection.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdStretch(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Transform t;
    Rect rootBox, newBox;
    CellDef *rootDef;
    int xdelta, ydelta;

    if (cmd->tx_argc > 3)
    {
	TxError("Usage: %s [direction [amount]]\n", cmd->tx_argv[0]);
	return;
    }

    if (cmd->tx_argc > 1)
    {
	int indx, amountx, amounty;

	if (!ToolGetEditBox((Rect *)NULL)) return;

	indx = GeoNameToPos(cmd->tx_argv[1], TRUE, TRUE);
	if (indx < 0) return;

	if (cmd->tx_argc >= 3)
	{
	    switch (indx)
	    {
		case GEO_EAST: case GEO_WEST:
		    amountx = cmdParseCoord(w, cmd->tx_argv[2], TRUE, TRUE);
		    amounty = 0;
		    break;
		case GEO_NORTH: case GEO_SOUTH:
		    amountx = 0;
		    amounty = cmdParseCoord(w, cmd->tx_argv[2], TRUE, FALSE);
		    break;
		default:
		    amountx = amounty = 0;	/* Should not happen */
		    break;
	    }
	}
	else
	{
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
	    default:
		ASSERT(FALSE, "Bad direction in CmdStretch");
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
	 * the point as the transform.  Round off to a Manhattan distance.
	 */
	
	Point rootPoint;
	MagWindow *window;
	int absX, absY;

	if (!ToolGetBox(&rootDef, &rootBox) || (rootDef != SelectRootDef))
	{
	    TxError("\"Stretch\" uses the box lower-left corner as a place\n");
	    TxError("    to pick up the selection for stretching, but the\n");
	    TxError("    box isn't in a window containing the selection.\n");
	    return;
	}
	window = ToolGetPoint(&rootPoint, (Rect *) NULL);
	if ((window == NULL) ||
	    (EditRootDef != ((CellUse *) window->w_surfaceID)->cu_def))
	{
	    TxError("\"Stretch\" uses the point as the place to put down a\n");
	    TxError("    the selection, but the point doesn't point to the\n");
	    TxError("    edit cell.\n");
	    return;
	}
	xdelta = rootPoint.p_x - rootBox.r_xbot;
	ydelta = rootPoint.p_y - rootBox.r_ybot;
	if (xdelta < 0) absX = -xdelta;
	else absX = xdelta;
	if (ydelta < 0) absY = -ydelta;
	else absY = ydelta;
	if (absY <= absX) ydelta = 0;
	else xdelta = 0;
	GeoTransTranslate(xdelta, ydelta, &GeoIdentityTransform, &t);
	GeoTransRect(&t, &rootBox, &newBox);
	DBWSetBox(rootDef, &newBox);
    }
    
    SelectStretch(xdelta, ydelta);
}
