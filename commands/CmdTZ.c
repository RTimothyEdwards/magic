/*
 * CmdTZ.c --
 *
 * Commands with names beginning with the letters T through Z.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdTZ.c,v 1.8 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/signals.h"
#include "utils/undo.h"
#include "select/select.h"
#include "utils/styles.h"
#include "wiring/wiring.h"
#include "utils/netlist.h"
#include "netmenu/netmenu.h"
#include "utils/tech.h"
#include "drc/drc.h"

#ifdef	LLNL
#include "yacr.h"
#endif	/* LLNL */

extern void DisplayWindow();

/* Trivial function that returns 1 if called */

int
existFunc(tile)
    Tile *tile;
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * checkForPaintFunc---
 *
 * Simple routine that checks to see if a cell has any paint in it.
 *
 * Results:
 *	Returns 1 on the first non-space tile found.
 *
 * ----------------------------------------------------------------------------
 */

int
checkForPaintFunc(cellDef, arg)
    CellDef *cellDef;
    ClientData arg;
{
    int numPlanes = *((int *)arg);
    int pNum, result;
   
    if (cellDef->cd_flags & CDINTERNAL) return 0;

    for (pNum = PL_SELECTBASE; pNum < numPlanes; pNum++)
    {
	if (DBSrPaintArea((Tile *)NULL, cellDef->cd_planes[pNum],
		&TiPlaneRect, &DBAllButSpaceAndDRCBits,
		existFunc, NULL))
	    return 1;
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * CmdCheckForPaintFunc --
 *
 *   This routine searches the database for any paint belonging to a
 *   cell other than an internally-used one.  A FALSE result means that
 *   no technology-dependent paint exists in the layout, and therefore
 *   it is safe to change technologies.
 *
 * Results:
 *	TRUE if technology-dependent paint exists in the database, FALSE
 *	if not.
 *
 * Side Effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

bool
CmdCheckForPaintFunc()
{
    if (DBCellSrDefs(0, checkForPaintFunc, (ClientData)&DBNumPlanes))
	return TRUE;
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdTech --
 *
 * 	Implement the "tech" command.  This largely replaces the old wizard
 *	"*showtech" command, and is meant to facilitate technology file
 *	writing and debugging, as well as providing information about the
 *	current technology.  The "tech layers" command replaces the "layers"
 *	command.
 *
 * Usage:
 *	tech [name|filename|version|lambda|load|help|planes|layers]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output.
 *
 * ----------------------------------------------------------------------------
 */

#define TECH_LOAD	0
#define TECH_HELP	1
#define TECH_NAME	2
#define TECH_FILE	3
#define TECH_VERSION	4
#define TECH_LAMBDA	5
#define TECH_PLANES	6
#define TECH_LAYERS	7
#define TECH_DRC	8
#define TECH_LOCK	9
#define TECH_UNLOCK	10	
#define TECH_REVERT	11

void
CmdTech(w, cmd)
    MagWindow *w;		/* Window in which command was invoked. */
    TxCommand *cmd;		/* Info about command options. */
{
    int	option, action, i, locargc;
    char **msg;
#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif
    bool noprompt = FALSE;

    static char *actionNames[] =
	{ "no", "yes", 0 };

    static char *cmdTechOption[] =
    {	
	"load filename [-noprompt][-override]\n\
				Load a new technology",
	"help			Display techinfo command options",
	"name			Show current technology name",
	"filename		Show current technology filename",
	"version		Show current technology version",
	"lambda			Show internal units per lambda", 
	"planes			Show defined planes",
	"layers	[<layer...>]	Show defined layers",
	"drc <rule> <layer...>	Query DRC ruleset",
	"locked [<layer...>]	Lock (make uneditable) layer <layer>",
	"unlocked [<layer...>]	Unlock (make editable) layer <layer>",
	"revert [<layer...>]	Revert lock state of layer <layer> to default",
	NULL
    };

    if (cmd->tx_argc == 1)
	option = TECH_HELP;
    else
	option = Lookup(cmd->tx_argv[1], cmdTechOption);

    if (option == -1)
    {
	TxError("Ambiguous techinfo option: \"%s\"\n", cmd->tx_argv[1]);
	goto usage2;
    }
    if (option < 0)
	goto usage;

    switch (option)
    {

#ifdef MAGIC_WRAPPER
	case TECH_NAME:
	    Tcl_SetResult(magicinterp, DBTechName, NULL);
	    break;
	case TECH_FILE:
	    Tcl_SetResult(magicinterp, TechFileName, NULL);
	    break;
	case TECH_VERSION:
	    Tcl_SetResult(magicinterp, DBTechVersion, NULL);
	    Tcl_AppendElement(magicinterp, DBTechDescription);
	    break;
	case TECH_LAMBDA:
	    if ((cmd->tx_argc > 2) && (StrIsInt(cmd->tx_argv[2])))
	    {
		DBLambda[1] = atoi(cmd->tx_argv[2]);
		if (cmd->tx_argc == 3)
		    DBLambda[0] = 1;
		else if ((cmd->tx_argc > 3) && (StrIsInt(cmd->tx_argv[3])))
		DBLambda[0] = atoi(cmd->tx_argv[3]);
		ReduceFraction(&DBLambda[0], &DBLambda[1]);
	    }
	    lobj = Tcl_NewListObj(0, NULL);
 	    Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewIntObj(DBLambda[0]));
 	    Tcl_ListObjAppendElement(magicinterp, lobj,
			Tcl_NewIntObj(DBLambda[1]));
 	    Tcl_SetObjResult(magicinterp, lobj);
	    break;
	case TECH_PLANES:
	    for (i = 0; i < DBNumPlanes; i++)
		Tcl_AppendElement(magicinterp, DBPlaneLongName(i));
	    break;
#else
	case TECH_NAME:
	    TxPrintf("Technology name is \"%s\"\n", DBTechName);
	    break;
	case TECH_FILE:
	    TxPrintf("Technology filename is \"%s\"\n", TechFileName);
	    break;
	case TECH_VERSION:
	    TxPrintf("Technology version is: \"%s\"\n", DBTechVersion);
	    TxPrintf("Description: %s\n", DBTechDescription);
	    break;
	case TECH_LAMBDA:
	    if ((cmd->tx_argc > 2) && (StrIsInt(cmd->tx_argv[2])))
	    {
		DBLambda[1] = atoi(cmd->tx_argv[2]);
		if (cmd->tx_argc == 3)
		    DBLambda[0] = 1;
		else if ((cmd->tx_argc > 3) && (StrIsInt(cmd->tx_argv[3])))
		DBLambda[0] = atoi(cmd->tx_argv[3]);
		ReduceFraction(&DBLambda[0], &DBLambda[1]);
	    }
	    TxPrintf("Internal units per Lambda = %d / %d\n",
		DBLambda[0], DBLambda[1]);
	    break;
	case TECH_PLANES:
	    TxPrintf("Defined planes (%d) are:\n", DBNumPlanes);
	    for (i = 0; i < DBNumPlanes; i++)
		TxPrintf("   %s\n", DBPlaneLongName(i));
	    break;
#endif

	case TECH_LAYERS:
	    if (cmd->tx_argc == 3)
	    {
		if (!strcmp(cmd->tx_argv[2], "*"))
		    DBTechPrintTypes(&DBAllButSpaceAndDRCBits, TRUE);
		else
		{
		    TileTypeBitMask layermask;
		    DBTechNoisyNameMask(cmd->tx_argv[2], &layermask);
		    DBTechPrintTypes(&layermask, TRUE);
		}
	    }
	    else if (cmd->tx_argc == 2)
		DBTechPrintTypes(&DBAllButSpaceAndDRCBits, FALSE);
	    else
		goto wrongNumArgs;
	    break;

	case TECH_LOCK:
	    if (cmd->tx_argc == 3)
	    {
		TileTypeBitMask lockmask, *rMask;
		TileType ctype;

		TTMaskZero(&lockmask);
		if (!strcmp(cmd->tx_argv[2], "*"))
		    TTMaskSetMask(&lockmask, &DBUserLayerBits);
		else
		    DBTechNoisyNameMask(cmd->tx_argv[2], &lockmask);

		TTMaskClearMask(&DBActiveLayerBits, &lockmask);

		for (ctype = TT_TECHDEPBASE; ctype < DBNumUserLayers; ctype++)
		    if (TTMaskHasType(&lockmask, ctype))
			if (DBIsContact(ctype))
			    DBLockContact(ctype);

		for (ctype = DBNumUserLayers; ctype < DBNumTypes; ctype++)
		{
		    rMask = DBResidueMask(ctype);
		    if (TTMaskIntersect(&lockmask, rMask))
		    {
			TTMaskClearType(&DBActiveLayerBits, ctype);
			DBLockContact(ctype);
		    }
		}
	    }
	    else if (cmd->tx_argc == 2)
	    {
		/* List all locked layers */
		TileTypeBitMask lockedLayers;
		TTMaskCom2(&lockedLayers, &DBActiveLayerBits);
		TTMaskAndMask(&lockedLayers, &DBAllButSpaceAndDRCBits);
		DBTechPrintTypes(&lockedLayers, TRUE);
	    }
	    else
		goto wrongNumArgs;
	    break;

	case TECH_UNLOCK:
	    if (cmd->tx_argc == 3)
	    {
		TileTypeBitMask lockmask, *rMask;
		TileType ctype;

		TTMaskZero(&lockmask);
		if (!strcmp(cmd->tx_argv[2], "*"))
		    TTMaskSetMask(&lockmask, &DBUserLayerBits);
		else
		    DBTechNoisyNameMask(cmd->tx_argv[2], &lockmask);

		TTMaskSetMask(&DBActiveLayerBits, &lockmask);
		for (ctype = TT_TECHDEPBASE; ctype < DBNumUserLayers; ctype++)
		    if (TTMaskHasType(&lockmask, ctype))
			if (DBIsContact(ctype))
			    DBUnlockContact(ctype);

		for (ctype = DBNumUserLayers; ctype < DBNumTypes; ctype++)
		{
		    TileTypeBitMask testmask;
		    rMask = DBResidueMask(ctype);
		    TTMaskAndMask3(&testmask, &DBActiveLayerBits, rMask);
		    if (TTMaskEqual(&testmask, rMask))
		    {
			TTMaskSetType(&DBActiveLayerBits, ctype);
			DBUnlockContact(ctype);
		    }
		}
		TTMaskAndMask(&DBActiveLayerBits, &DBAllButSpaceAndDRCBits);
	    }
	    else if (cmd->tx_argc == 2)
	    {
		/* List all unlocked layers */

		TileTypeBitMask unlockedLayers;
		TTMaskZero(&unlockedLayers);
		TTMaskSetMask(&unlockedLayers, &DBAllButSpaceAndDRCBits);
		TTMaskCom2(&unlockedLayers, &DBActiveLayerBits);
		TTMaskCom(&unlockedLayers);
		DBTechPrintTypes(&unlockedLayers, TRUE);
	    }
	    else
		goto wrongNumArgs;
	    break;

	case TECH_REVERT:
	    {
	    TileTypeBitMask lockmask, *rMask;
	    TileType ctype;

	    if (cmd->tx_argc == 3)
	    {

		TTMaskZero(&lockmask);
		if (!strcmp(cmd->tx_argv[2], "*"))
		    TTMaskSetMask(&lockmask, &DBTechActiveLayerBits);
		else
		    DBTechNoisyNameMask(cmd->tx_argv[2], &lockmask);

		TTMaskClearMask(&DBActiveLayerBits, &lockmask);
		TTMaskAndMask(&lockmask, &DBTechActiveLayerBits);
		TTMaskSetMask(&DBActiveLayerBits, &lockmask);

	    }
	    else if (cmd->tx_argc == 2)
	    {
		// Copy DBTechActiveLayerBits back to DBActiveLayerBits
		TTMaskZero(&DBActiveLayerBits);
		TTMaskSetMask(&DBActiveLayerBits, &DBTechActiveLayerBits);
	    }
	    else
		goto wrongNumArgs;

	    // Handle contact types, which involve a bit more than
	    // just setting the active layer mask, as paint/erase
	    // tables need to be manipulated

	    for (ctype = TT_TECHDEPBASE; ctype < DBNumUserLayers; ctype++)
		if (DBIsContact(ctype))
		    if (TTMaskHasType(&DBActiveLayerBits, ctype))
			DBUnlockContact(ctype);
		    else
			DBLockContact(ctype);

	    for (ctype = DBNumUserLayers; ctype < DBNumTypes; ctype++)
	    {
		TileTypeBitMask testmask;
		rMask = DBResidueMask(ctype);
		TTMaskAndMask3(&testmask, &DBActiveLayerBits, rMask);
		if (TTMaskEqual(&testmask, rMask))
		{
		    TTMaskSetType(&DBActiveLayerBits, ctype);
		    DBUnlockContact(ctype);
		}
		else if (TTMaskIntersect(&DBActiveLayerBits, rMask))
		{
		    TTMaskClearType(&DBActiveLayerBits, ctype);
		    DBLockContact(ctype);
		}
	    }
	    }
	    break;

	case TECH_DRC:
	    /* Query the DRC database */
	    if (cmd->tx_argc >= 4)
	    {
		TileType t1, t2;
		int tresult;

		t1 = DBTechNoisyNameType(cmd->tx_argv[3]);
		if (t1 < 0) {
		    TxError("No such layer %s\n", cmd->tx_argv[3]);
		    break;
		}
		if (!strncmp(cmd->tx_argv[2], "width", 5))
		{
		    tresult = DRCGetDefaultLayerWidth(t1);
#ifdef MAGIC_WRAPPER		
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(tresult));
#else
		    TxPrintf("Minimum width is %d\n", tresult);
#endif
		}
		else if (!strncmp(cmd->tx_argv[2], "spac", 4))
		{
		    if (cmd->tx_argc >= 5)
		    {
			t2 = DBTechNoisyNameType(cmd->tx_argv[4]);
			if (t2 < 0) {
			    TxError("No such layer %s\n", cmd->tx_argv[4]);
			    break;
		 	}
		    }
		    else
			t2 = t1;
		    tresult = DRCGetDefaultLayerSpacing(t1, t2);
#ifdef MAGIC_WRAPPER		
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(tresult));
#else
		    TxPrintf("Minimum spacing is %d\n", tresult);
#endif
		}
		else if (!strncmp(cmd->tx_argv[2], "surr", 4))
		{
		    if (cmd->tx_argc >= 5)
		    {
			t2 = DBTechNoisyNameType(cmd->tx_argv[4]);
			if (t2 < 0) {
			    TxError("No such layer %s\n", cmd->tx_argv[4]);
			    break;
		 	}
		    }
		    else
		    {
			TxError("Requires two layer types.\n");
			break;
		    }

		    tresult = DRCGetDefaultLayerSurround(t1, t2);
#ifdef MAGIC_WRAPPER		
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(tresult));
#else
		    TxPrintf("Minimum surround is %d\n", tresult);
#endif
		}
	    }
	    else
		goto wrongNumArgs;
	    break;

	case TECH_LOAD:
	    locargc = cmd->tx_argc;
	    while ((*cmd->tx_argv[locargc - 1] == '-') && (locargc > 3))
	    {
		if (!strcmp(cmd->tx_argv[locargc - 1], "-override"))
		{
		    /* Allow the "tech load" command to override 	*/
		    /* a technology specified on the command line.	*/
		    /* Possibly this behavior should not be allowed,	*/
		    /* but you never know.  Use with utmost caution.	*/

		    TechOverridesDefault = FALSE;
		}
		else if (!strcmp(cmd->tx_argv[locargc - 1], "-noprompt"))
		    noprompt = TRUE;

		// "-nooverride" is kept for backwards compatibility but
		// has no effect.

		else if (strcmp(cmd->tx_argv[locargc - 1], "-nooverride"))
		    break;
		locargc--;
	    }

	    if (locargc != 3)
	    {
		TxError("Usage: tech load <filename> [-noprompt] [-override]\n");
		break;
	    }

	    /* If the "-T" command line option is given to magic, then	*/
	    /* the techfile given on the command line prevents any	*/
	    /* other techfile from being loaded until after startup is	*/
	    /* done.							*/

	    if (TechOverridesDefault) return;

	    /* Here:  We need to do a test to see if any structures	*/
	    /* exist in memory and delete them, or else we need to have	*/
	    /* all tech stuff set up such that multiple technologies	*/
	    /* can be present at the same time.				*/

	    /* For now: check and warn, but do nothing to the layout */

	    if (!noprompt)
	    {
		if (DBCellSrDefs(0, checkForPaintFunc, (ClientData)&DBNumPlanes))
		{
		    action = TxDialog("Technology file (re)loading may invalidate"
				" the existing layout.  Continue? ", actionNames, 0);
		    if (action == 0) return;
		}
	    }

	    /* Call TechLoad with initmask = -2 to check only that the	*/
	    /* techfile exists and is readable before calling the init	*/
	    /* functions on various sections, which is not reversible.	*/

	    if (!TechLoad(cmd->tx_argv[2], -2))
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, "Technology file does not exist"
				" or is not readable\n", NULL);
#else
		TxError("Technology file does not exist or is not readable.\n");
#endif
		break;
	    }
	
	    if (!TechLoad(cmd->tx_argv[2], 0))
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, "Error in loading technology file\n", NULL);
#else
		TxError("Error in loading technology file\n");
#endif
		break;
	    }
	    break;

	case TECH_HELP:
	    TxPrintf("Tech commands have the form \"tech option\",\n");
	    TxPrintf("where option is one of:\n");
	    for (msg = &(cmdTechOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    break;
    }
    return;

wrongNumArgs:
    TxPrintf("wrong number of arguments to command \"%s\"\n", cmd->tx_argv[0]);
    goto usage2;

usage:
    TxError("\"%s\" isn't a valid %s option.", cmd->tx_argv[1], cmd->tx_argv[0]);

usage2:
    TxError("  Type \":%s help\" for help.\n", cmd->tx_argv[0]);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdTool --
 *
 * 	Implement the "tool" command.
 *
 * Usage:
 *	tool [name|info]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current tool that's active in layout windows may be changed.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdTool(w, cmd)
    MagWindow *w;		/* Window in which command was invoked. */
    TxCommand *cmd;		/* Info about command options. */
{
    if (cmd->tx_argc == 1)
    {
	(void) DBWChangeButtonHandler((char *) NULL);
	return;
    }

    if (cmd->tx_argc > 2)
    {
	TxError("Usage: %s [name|info]\n", cmd->tx_argv[0]);
	return;
    }

    if (strcmp(cmd->tx_argv[1], "info") == 0)
	DBWPrintButtonDoc();
    else (void) DBWChangeButtonHandler(cmd->tx_argv[1]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdUnexpand --
 *
 * Implement the "unexpand" command.
 *
 * Usage:
 *	unexpand
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unexpands all cells under the box that don't completely
 *	contain the box.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdUnexpand(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int windowMask, boxMask;
    Rect rootRect;
    int cmdUnexpandFunc();		/* Forward reference. */

    if (cmd->tx_argc != 1)
    {
	TxError("Usage: %s\n", cmd->tx_argv[0]);
	return;
    }
    
    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }
    windowMask = ((DBWclientRec *) w->w_clientData)->dbw_bitmask;

    (void) ToolGetBoxWindow(&rootRect, &boxMask);
    if ((boxMask & windowMask) != windowMask)
    {
	TxError("The box isn't in the same window as the cursor.\n");
	return;
    }
    DBExpandAll(((CellUse *) w->w_surfaceID), &rootRect, windowMask,
	    FALSE, cmdUnexpandFunc, (ClientData)(pointertype) windowMask);
}

/* This function is called for each cell whose expansion status changed.
 * It forces the cells area to be redisplayed, then returns 0 to keep
 * looking for more cells to unexpand.
 */

int
cmdUnexpandFunc(use, windowMask)
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
 *
 * CmdUpsidedown --
 *
 * Implement the "upsidedown" command.
 *
 * Usage:
 *	upsidedown
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The box and verything in the selection are flipped upside down
 *	using the point as the axis around which to flip.
 *
 * ----------------------------------------------------------------------------
 */
    
void
CmdUpsidedown(w, cmd)
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
    
    /* To flip the selection upside down, first flip it around the
     * x-axis, then move it back so its lower-left corner is in
     * the same place that it used to be.
     */
    
    GeoTransRect(&GeoUpsideDownTransform, &SelectDef->cd_bbox, &bbox);
    GeoTranslateTrans(&GeoUpsideDownTransform,
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
 * cmdWhatPrintCell --
 *
 * This command is used to print the cell id of the types encountered in the
 * what command. For each type it only prints the name once.
 *
 * Results:
 *	Returns 0 to keep the search going.
 *
 * Side Effects:
 *	Prints cell names to the terminal.  Adds to a maintained list of
 *	unique cell IDs, which needs to be free'd by the calling routine.
 * ----------------------------------------------------------------------------
 */

struct linked_id {
   char *lid_name;
   struct linked_id *lid_next;
};


int cmdWhatPrintCell(tile, cxp)
   Tile *tile;
   TreeContext *cxp;
{
    struct linked_id **lid = (struct linked_id **)cxp->tc_filter->tf_arg;
    struct linked_id *curlid = *lid;
    char *CurrCellName;
    char *whatCell;

    // Use the id of the cell or its name if top cell

    CurrCellName = cxp->tc_scx->scx_use->cu_id;
    if ((CurrCellName == NULL) || (CurrCellName[0] == '\0'))
	CurrCellName = cxp->tc_scx->scx_use->cu_def->cd_name;

    for (curlid = *lid; curlid != NULL; curlid = curlid->lid_next)
    {
	whatCell = curlid->lid_name;
	if (whatCell == CurrCellName)
	    break;
    }
    if (curlid == NULL)
    {
	TxPrintf(" %s ", CurrCellName);
	curlid = (struct linked_id *)mallocMagic(sizeof(struct linked_id));
	curlid->lid_name = CurrCellName;
	curlid->lid_next = *lid;
	*lid = curlid;
    }
    return 0;
}

typedef struct labelstore
{
    TileType lab_type;
    char *lab_text;
    char *cell_name;
} LabelStore;

static int moreLabelEntries, labelEntryCount;
static LabelStore *labelBlockTop, *labelEntry;

/*
 * ----------------------------------------------------------------------------
 *
 * CmdWhat --
 *
 * 	Print out information about what's selected.
 *
 * Usage:
 *	what [-list]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets printed to identify the kinds of paint, plus
 *	labels and subcells, that are selected.
 *	In the TCL version, the "-list" option puts the result in a
 *	nested TCL list.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdWhat(w, cmd)
    MagWindow *w;		/* Window in which command was invoked. */
    TxCommand *cmd;		/* Information about the command. */
{
    int i, locargc;
    bool foundAny;
    bool doList = FALSE;
    TileTypeBitMask layers, maskBits, *rMask;
    SearchContext scx;
    CellUse *CheckUse;
    struct linked_id *lid;

#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj, *paintobj, *labelobj, *cellobj;
    extern int cmdWhatCellListFunc();
#endif

    extern int cmdWhatPaintFunc(), cmdWhatLabelFunc(), cmdWhatCellFunc();
    extern int cmdWhatLabelPreFunc(), orderLabelFunc();

    locargc = cmd->tx_argc;
    
#ifdef MAGIC_WRAPPER
    if ((locargc == 2) && !strncmp(cmd->tx_argv[locargc - 1], "-list", 5))
    {
	doList = TRUE;
	locargc--;
	lobj = Tcl_NewListObj(0, NULL);
	paintobj = Tcl_NewListObj(0, NULL);
	labelobj = Tcl_NewListObj(0, NULL);
	cellobj = Tcl_NewListObj(0, NULL);
    }	
    if (locargc > 1)
    {
	TxError("Usage: what [-list]\n");
	return;
    }
#else
    if (locargc > 1)
    {
	TxError("Usage: what\n");
	return;
    }
#endif

    /* Find all the selected paint and print out the layer names. */

    TTMaskZero(&layers);
    (void) SelEnumPaint(&DBAllButSpaceAndDRCBits, FALSE, (bool *) NULL,
	    cmdWhatPaintFunc, (ClientData) &layers);

    if (!TTMaskIsZero(&layers))
    {
	/* If there are any stacked types in the list, decompose them	*/
	/* into their residues (which are the two contact types that	*/
	/* are stacked).						*/

	for (i = DBNumUserLayers; i < DBNumTypes; i++)
	{
	    if (TTMaskHasType(&layers, i))
	    {
		rMask = DBResidueMask(i);
		TTMaskSetMask(&layers, rMask);
	    }
	    TTMaskClearType(&layers, i);
	}
    }

    if (!TTMaskIsZero(&layers))
    {
#ifdef MAGIC_WRAPPER
	if (doList)
	{
	    for (i = TT_SELECTBASE; i < DBNumUserLayers; i++)
		if (TTMaskHasType(&layers, i))
		    Tcl_ListObjAppendElement(magicinterp, paintobj,
				Tcl_NewStringObj(DBTypeLongName(i), -1));
	}
	else
	{
#endif
	    CheckUse = NULL;
	    if (EditRootDef == SelectRootDef)
		CheckUse = EditCellUse;
	    if (CheckUse == NULL)
	    {
		if (w == (MagWindow *)NULL)
		    windCheckOnlyWindow(&w, DBWclientID);
		if (w) CheckUse = (CellUse *)w->w_surfaceID;
	    }
	    if ((CheckUse != NULL) && (CheckUse->cu_def == SelectRootDef))
	    {
		scx.scx_use = CheckUse;
		scx.scx_area = SelectUse->cu_bbox;	// BSI
		scx.scx_trans = GeoIdentityTransform;	// BSI

		TxPrintf("Selected mask layers:\n");
		for (i = TT_SELECTBASE; i < DBNumUserLayers; i++)
		{
		    if (TTMaskHasType(&layers, i))
		    {
			lid = NULL;
			TxPrintf("    %-8s (", DBTypeLongName(i));
			TTMaskSetOnlyType(&maskBits, i);
			if (DBIsContact(i)) DBMaskAddStacking(&maskBits);
			DBTreeSrTiles(&scx, &maskBits, 0, cmdWhatPrintCell,
				(ClientData)&lid);
			TxPrintf(")\n");
			while (lid != NULL)
			{
			    freeMagic(lid);
			    lid = lid->lid_next;
			}
		    }
		}
	    }
	    else
	    {
		TxPrintf("Selected mask layers:\n");
		for (i = TT_SELECTBASE; i < DBNumUserLayers; i++)
		    if (TTMaskHasType(&layers, i))
			TxPrintf("    %s\n", DBTypeLongName(i));
	    }
#ifdef MAGIC_WRAPPER
	}
#endif
    }

    /* Enumerate all of the selected labels. */

    moreLabelEntries = 0;
    labelEntryCount = 0;
    labelBlockTop = NULL; 
    (void) SelEnumLabels(&DBAllTypeBits, FALSE, (bool *) NULL,
	    cmdWhatLabelPreFunc, (ClientData) &foundAny);
    foundAny = FALSE;
    if (labelBlockTop)
    {
	qsort(labelBlockTop, labelEntryCount, sizeof(LabelStore), orderLabelFunc);

#ifdef MAGIC_WRAPPER
	if (doList)
	{
	    Tcl_Obj *newtriple;
	    for (labelEntry = labelBlockTop; labelEntryCount-- > 0; labelEntry++)
	    {
		newtriple = Tcl_NewListObj(0, NULL);
		Tcl_ListObjAppendElement(magicinterp, newtriple,
			Tcl_NewStringObj(labelEntry->lab_text, -1));
		Tcl_ListObjAppendElement(magicinterp, newtriple,
			Tcl_NewStringObj(DBTypeLongName(labelEntry->lab_type), -1));
		if (labelEntry->cell_name != NULL)
		{
		    Tcl_ListObjAppendElement(magicinterp, newtriple,
				Tcl_NewStringObj(labelEntry->cell_name, -1));
		}
		else
		{
		    /* Label in top-level def---append a NULL list */
		    Tcl_ListObjAppendElement(magicinterp, newtriple,
				Tcl_NewListObj(0, NULL));
		}
		Tcl_ListObjAppendElement(magicinterp, labelobj, newtriple);
	    }
	}
	else
	{
#endif
    	    /* now print them out */
	    for (labelEntry = labelBlockTop; labelEntryCount-- > 0; labelEntry++)
        	i = cmdWhatLabelFunc(labelEntry, &foundAny);
	    if (i > 1)
		TxPrintf(" (%i instances)", i);
	    TxPrintf("\n");

#ifdef MAGIC_WRAPPER
	}
#endif
	freeMagic(labelBlockTop);
    }

    /* Enumerate all of the selected subcells. */

    foundAny = FALSE;
#ifdef MAGIC_WRAPPER
    if (doList)
	SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL,
		cmdWhatCellListFunc, (ClientData) cellobj);
    else
#endif
	SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL,
		cmdWhatCellFunc, (ClientData) &foundAny);

#ifdef MAGIC_WRAPPER
    if (doList)
    {
	Tcl_ListObjAppendElement(magicinterp, lobj, paintobj);
	Tcl_ListObjAppendElement(magicinterp, lobj, labelobj);
	Tcl_ListObjAppendElement(magicinterp, lobj, cellobj);
 	Tcl_SetObjResult(magicinterp, lobj);
    }
#endif
}

/* Search function invoked for each paint tile in the selection:
 * just set a bit in a tile type mask.
 */

    /*ARGSUSED*/
int
cmdWhatPaintFunc(rect, type, mask)
    Rect *rect;			/* Not used. */
    TileType type;		/* Type of this piece of paint. */
    TileTypeBitMask *mask;	/* Place to OR in type's bit. */
{
    if (type & TT_DIAGONAL)
	type = (type & TT_SIDE) ? (type & TT_RIGHTMASK) >> 14 :
		(type & TT_LEFTMASK);
    TTMaskSetType(mask, type);
    return 0;
}

/* Search function invoked for each label in the selection:  print
 * out information about the label.
 */

    /*ARGSUSED*/
int
cmdWhatLabelPreFunc(label, cellUse, transform, foundAny)
    Label *label;		/* Label that's selected. */
    CellUse *cellUse;		/* Cell use containing label. */
    Transform *transform;	/* Not used. */
    bool *foundAny;		/* Use to print extra stuff for the first
				 * label found.
				 */
{
    LabelStore	*newPtr;
    CellDef *cellDef = cellUse->cu_def;	/* Cell definition containing label. */

    if (moreLabelEntries == 0)
    {
	newPtr = (LabelStore *)mallocMagic((labelEntryCount + 100)
		* sizeof(LabelStore));
	if (!newPtr)
		return 1;	/* no space stop the search */
	if (labelBlockTop)
	{
	   memcpy(newPtr, labelBlockTop, labelEntryCount * sizeof(LabelStore));
	   freeMagic(labelBlockTop);
	}
	labelBlockTop = newPtr;
        labelEntry = &labelBlockTop[labelEntryCount];
	moreLabelEntries = 100;
    }
	/* store the pointers for sorting later */	
    labelEntry->lab_type = label->lab_type;
    labelEntry->lab_text = label->lab_text;
    if (!cellUse->cu_id)
	labelEntry->cell_name = NULL;
    else if (EditRootDef && (!strcmp(cellDef->cd_name, EditRootDef->cd_name)))
	/* This is a hack to get around an apparently bogus cellUse entry. . .*/
	labelEntry->cell_name = NULL;
    else
	labelEntry->cell_name = cellUse->cu_id;
    labelEntry++;
    moreLabelEntries--;
    labelEntryCount++;
    return 0;
}
	

int
cmdWhatLabelFunc(entry, foundAny)
    LabelStore *entry;		/* stored pointers to label info*/
    bool *foundAny;		/* Use to print extra stuff for the first
				 * label found.
				 */
{
    static TileType last_type;
    static char *last_name, *last_cell;
    static int counts;
    bool isDef = FALSE;

    if (!*foundAny)
    {
	TxPrintf("Selected label(s):");
	*foundAny = TRUE;
        last_name = NULL;
	counts = 0;
    }

    if (entry->cell_name == NULL)
    {
	isDef = TRUE;
	if (SelectRootDef)
	    entry->cell_name = SelectRootDef->cd_name;
	else if (EditRootDef)
	    entry->cell_name = EditRootDef->cd_name;
	else
	    entry->cell_name = "(unknown)";
    }

    if ((last_name && (strcmp(entry->lab_text, last_name) !=0 ||
                       strcmp(entry->cell_name, last_cell) != 0)) ||
       (entry->lab_type != last_type) || (last_name == NULL))
    {
	if (counts > 1)
		TxPrintf(" (%i instances)", counts);
        TxPrintf("\n    \"%s\" is attached to %s in cell %s %s", entry->lab_text,
		DBTypeLongName(entry->lab_type), 
		(isDef) ? "def" : "use", entry->cell_name);
        last_type = entry->lab_type;
        last_cell = entry->cell_name;
        last_name = entry->lab_text;
	counts = 1;
    } else 
	counts++;
    return counts;
}

/* orderLabelFunc is called by qsort to compare the labels */
/* they are sorted by label name, then cell name, then attached material */
/* that way all of identical names are grouped together */
int
orderLabelFunc(one, two)
    LabelStore *one;		/* one of the labels being compared */
    LabelStore *two;		/* the other label to compare with */
{
    int i;

    if ((i = strcmp(one->lab_text, two->lab_text)) != 0)
        return i;
    if (one->cell_name && two->cell_name && (i = strcmp(one->cell_name,
		two->cell_name)) != 0)
        return i;
    if (one->lab_type != two->lab_type)
	return (one->lab_type < two->lab_type) ? 1 : -1;
    return 0;
}

/* Search function invoked for each selected subcell.  Just print out
 * its name and use id.
 */

    /*ARGSUSED*/
int
cmdWhatCellFunc(selUse, realUse, transform, foundAny)
    CellUse *selUse;		/* Not used. */
    CellUse *realUse;		/* Selected cell use. */
    Transform *transform;	/* Not used. */
    bool *foundAny;		/* Used to print extra stuff for the first
				 * use found.
				 */
{
    /* Forward reference */
    char *dbGetUseName();

    if (!*foundAny)
    {
	TxPrintf("Selected subcell(s):\n");
	*foundAny = TRUE;
    }
    TxPrintf("    Instance \"%s\" of cell \"%s\"\n", dbGetUseName(realUse),
	    realUse->cu_def->cd_name);
    return 0;
}

#ifdef MAGIC_WRAPPER

/* Same search function as above, but appends use names to a Tcl list */

int
cmdWhatCellListFunc(selUse, realUse, transform, newobj)
    CellUse *selUse;		/* Not used. */
    CellUse *realUse;		/* Selected cell use. */
    Transform *transform;	/* Not used. */
    Tcl_Obj *newobj;		/* Tcl list object holding use names */
{
    Tcl_Obj *tuple;
    /* Forward reference */
    char *dbGetUseName();

    tuple = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(magicinterp, tuple,
		Tcl_NewStringObj(dbGetUseName(realUse), -1));
    Tcl_ListObjAppendElement(magicinterp, tuple,
		Tcl_NewStringObj(realUse->cu_def->cd_name, -1));
    Tcl_ListObjAppendElement(magicinterp, newobj, tuple);
    return 0;
}

#endif

/*
 * ----------------------------------------------------------------------------
 *
 * CmdWire --
 *
 * Implement the "wire" command, which provides a wiring-style interface
 * for painting.
 *
 * Usage:
 *	wire option args
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The edit cell is modified to contain additional paint.
 *
 * ----------------------------------------------------------------------------
 */

#define DECREMENT	0
#define HELP		1
#define HORIZONTAL	2
#define INCREMENT	3
#define LEG		4
#define SHOW		5
#define SWITCH		6
#define TYPE		7
#define VALUES		8
#define VERTICAL	9
#define WIDTH		10
#define SEGMENT		11

void
CmdWire(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int option, locargc;
    char **msg, *lastargv;
    TileType type;
    int width;

#ifdef MAGIC_WRAPPER
    Tcl_Obj *lobj;
#endif

    static char *cmdWireOption[] =
    {	
	"decrement layer|width 	decrement the wire layer or width",
	"help                   print this help information",
	"horizontal             add a new horizontal wire leg",
	"increment layer|width 	increment the wire layer or width",
	"leg                    add a new horizontal or vertical leg",
	"show			show next wire segment as a selection",
	"switch [layer width]   place contact and switch layers",
	"type [layer [width]]   select the type and size of wires",
	"values			query current wire type and width",
	"vertical               add a new vertical wire leg",
	"width [value]		change the width of the wire",
	"segment layer width x1 y1 x2 y2... [-noendcap]  (or)\n"
	"segment layer width filename [-noendcap]\n"
	"			paint one or more wire segments",
	NULL
    };

    locargc = cmd->tx_argc;
    if (locargc < 2)
    {
	option = HELP;
	locargc = 2;
    }
    else
    {
	option = Lookup(cmd->tx_argv[1], cmdWireOption);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid wire option.\n", cmd->tx_argv[1]);
	    option = HELP;
	    locargc = 2;
	}
    }

    switch (option)
    {
	case DECREMENT:
	    if (cmd->tx_argc != 3 && cmd->tx_argc != 4)
		goto badargs;
	    if (!strcmp(cmd->tx_argv[2], "type"))
	    {
		Contact *contact;
		type = TT_SPACE;
		for (contact = WireContacts; contact != NULL;
			contact = contact->con_next)
		{
		    if (contact->con_layer2 == WireType)
		    {
			if (type != TT_SPACE)
			{
			    TxError("Ambiguous directive---multiple routing types\n");
			    break;
			}
			else
			    type = contact->con_layer1;
		    }
		}
		if (type == TT_SPACE)
		    TxError("No routing layer defined\n");
		else
		{
		    width = DRCGetDefaultLayerWidth(type);
		    WireAddContact(type, width);
		}
	    }
	    else if (!strcmp(cmd->tx_argv[2], "width"))
	    {
		int value = 1;
		if (cmd->tx_argc == 4)
		    value = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
		WirePickType(WireType, WireWidth - value);
	    }
	    else
		goto badargs;
	    return;

	case INCREMENT:
	    if (cmd->tx_argc != 3 && cmd->tx_argc != 4)
		goto badargs;
	    if (!strcmp(cmd->tx_argv[2], "type") || !strcmp(cmd->tx_argv[2], "layer"))
	    {
		Contact *contact;
		type = TT_SPACE;
		for (contact = WireContacts; contact != NULL;
			contact = contact->con_next)
		{
		    if (contact->con_layer1 == WireType)
		    {
			if (type != TT_SPACE)
			{
			    TxError("Ambiguous directive---multiple routing types\n");
			    break;
			}
			else
			    type = contact->con_layer2;
		    }
		}
		if (type == TT_SPACE)
		    TxError("No routing layer defined\n");
		else
		{
		    width = DRCGetDefaultLayerWidth(type);
		    WireAddContact(type, width);
		}
	    }
	    else if (!strcmp(cmd->tx_argv[2], "width"))
	    {
		int value = 1;
		if (cmd->tx_argc == 4)
		    value = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
		WirePickType(WireType, WireWidth + value);
	    }
	    else
		goto badargs;
	    return;


	case HELP:
	    TxPrintf("Wiring commands have the form \":wire option\",");
	    TxPrintf(" where option is one of:\n");
	    for (msg = &(cmdWireOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    return;
	
	case HORIZONTAL:
	    WireAddLeg((Rect *) NULL, (Point *) NULL, WIRE_HORIZONTAL);
	    return;
	
	case LEG:
	    WireAddLeg((Rect *) NULL, (Point *) NULL, WIRE_CHOOSE);
	    return;

	case SHOW:
	    WireShowLeg();
	    return;

	case SWITCH:
	    if (locargc == 2)
		WireAddContact(-1, 0);
	    else if (locargc != 4)
		goto badargs;
	    else
	    {
		type = DBTechNameType(cmd->tx_argv[2]);
		if (type == -2)
		{
		    TxError("Layer name \"%s\" doesn't exist.\n",
			    cmd->tx_argv[2]);
		    return;
		}
		else if (type == -1)
		{
		    TxError("Layer name \"%s\" is ambiguous.\n",
			    cmd->tx_argv[2]);
		    return;
		}
		width = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
		WireAddContact(type, width);
		return;
	    }
	    break;

	case TYPE:
	    if (locargc == 2)
		WirePickType(-1, 0);
	    else if (locargc != 3 && locargc != 4)
	    {
		badargs:
		TxError("Wrong arguments.  The correct syntax is\n");
		TxError("    \"wire %s\"\n", cmdWireOption[option]);
		return;
	    }
	    else
	    {
		type = DBTechNameType(cmd->tx_argv[2]);
		if (type == -2)
		{
		    TxError("Layer name \"%s\" doesn't exist.\n",
			    cmd->tx_argv[2]);
		    return;
		}
		else if (type == -1)
		{
		    TxError("Layer name \"%s\" is ambiguous.\n",
			    cmd->tx_argv[2]);
		    return;
		}
		if (locargc == 3)
		{
		    int minwidth = DRCGetDefaultLayerWidth(type);
		    width = WireGetWidth();
		    if (width < minwidth) width = minwidth;
		}
		else
		    width = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
		WirePickType(type, width);
		return;
	    }
	    break;

	case VALUES:
	    if (locargc == 2)
	    {
		width = WireGetWidth();
		type = WireGetType();
#ifdef MAGIC_WRAPPER
		lobj = Tcl_NewListObj(0, NULL);
		Tcl_ListObjAppendElement(magicinterp, lobj,
				Tcl_NewIntObj(width));
		Tcl_ListObjAppendElement(magicinterp, lobj,
				Tcl_NewStringObj(DBTypeLongNameTbl[type], -1));
		Tcl_SetObjResult(magicinterp, lobj);
#else
		TxPrintf("Wire layer %s, width %d\n",
			DBTypeLongNameTbl[type], width);
#endif
	    }
	    return;

	case VERTICAL:
	    WireAddLeg((Rect *) NULL, (Point *) NULL, WIRE_VERTICAL);
	    return;

	case WIDTH:
	    if (locargc == 2)
	    {
		width = WireGetWidth();
#ifdef MAGIC_WRAPPER
		lobj = Tcl_NewIntObj(width);
		Tcl_SetObjResult(magicinterp, lobj);
#else
		TxPrintf("Wire width is %d\n", width);
#endif
	    }
	    else if (locargc != 3)
		goto badargs;
	    else
	    {
		width = cmdParseCoord(w, cmd->tx_argv[2], TRUE, TRUE);
		type =  WireGetType();
		WirePickType(type, width);
		return;
	    }
	    break;

	case SEGMENT:
	    {
		Point *plist;
		Rect r;
		int pNum, i, j, points, wexpand;
	 	CellDef *def = (CellDef *)NULL;
		PaintUndoInfo ui;
		bool endcap = TRUE;

		if (EditCellUse != NULL)
		    def = EditCellUse->cu_def;

		if (def == NULL)
		{
		    TxError("No cell being edited\n");
		    return;
		}

		lastargv = cmd->tx_argv[locargc - 1];
		if (lastargv[0] == '-')
		{
		    if (!strcmp(lastargv, "-noendcap"))
		    {
			endcap = FALSE;
			locargc--;
		    }
		}
		if (((locargc < 8) || (locargc & 0x1)) && (locargc != 5))
		{
		    TxError("Usage: wire segment layer width x1 x2 y1 y2\n");
		    TxError(" (or)  wire segment layer width filename\n");
		    return;
		}
		type = DBTechNoisyNameType(cmd->tx_argv[2]);
		if (type < 0)
		    return;

		if (!StrIsNumeric(cmd->tx_argv[3]))
		{
		    TxError("Route segment width must be a numeric value\n");
		    return;
		}
		else
		    width = cmdParseCoord(w, cmd->tx_argv[3], FALSE, TRUE);

		/* Get the coordinates in 2x internal units, which is	*/
		/* what is needed by routine PaintWireList.		*/

		if (locargc == 5)
		{
		    FILE *pfile;
		    char line[128];
		    char *pptr, *xptr, *yptr;

		    if ((pfile = PaOpen(cmd->tx_argv[4], "r", NULL, Path, CellLibPath,
				NULL)) == NULL)
		    {
			TxError("No such file or error opening %s\n", cmd->tx_argv[4]);
			return;
		    }
		    else
		    {
			points = 0;
			while (fgets(line, 128, pfile) != NULL) points++;
			rewind(pfile);
			plist = (Point *)mallocMagic(points * sizeof(Point));
			i = 0;
			while (fgets(line, 128, pfile) != NULL)
			{
			    /* Parse two coordinates (x and y) out of each line */
			    for (pptr = line; isspace(*pptr) && *pptr != '\0'; pptr++);
			    xptr = pptr;
			    for (; !isspace(*pptr) && *pptr != '\0'; pptr++);
			    *pptr++ = '\0';
			    for (; isspace(*pptr) && *pptr != '\0'; pptr++);
			    yptr = pptr;
			    for (; !isspace(*pptr) && *pptr != '\0'
					&& *pptr != '\n'; pptr++);
			    *pptr++ = '\0';

			    if (*xptr == '\0' || *yptr == '\0')
			    {
				TxError("Bad coordinate pair at %s line %d\n",
					cmd->tx_argv[4], i + 1); 
				freeMagic(plist);
				return;
			    }

			    plist[i].p_x = cmdScaleCoord(w, xptr, FALSE, TRUE, 2);
			    plist[i].p_y = cmdScaleCoord(w, yptr, FALSE, FALSE, 2);
			    i++;
			}
			fclose(pfile);
		    }
		}
		else
		{
		    points = (locargc - 4) / 2;
		    plist = (Point *)mallocMagic(points * sizeof(Point));
		    for (i = 0, j = 4; i < points; i++)
		    {
			plist[i].p_x = cmdScaleCoord(w, cmd->tx_argv[j++],
					FALSE, TRUE, 2);
			plist[i].p_y = cmdScaleCoord(w, cmd->tx_argv[j++],
					FALSE, FALSE, 2);
		    }
		}

		def->cd_flags |= CDMODIFIED | CDGETNEWSTAMP;
		ui.pu_def = def;
		for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
		{
		    if (DBPaintOnPlane(type, pNum))
		    {
			ui.pu_pNum = pNum;
			PaintWireList(plist, points, width, endcap,
				def->cd_planes[pNum],
				DBStdPaintTbl(type, pNum), &ui);
		    }
		}

		/* Determine the bounding box of the segment just drawn */
		r.r_xbot = r.r_xtop = plist[0].p_x;
		r.r_ybot = r.r_ytop = plist[0].p_y;
		for (i = 1; i < points; i++)
		    GeoIncludePoint(plist + i, &r);

		wexpand = (int)(0.5 + (float)width * 1.414214);
		r.r_xbot -= wexpand;
		r.r_ybot -= wexpand;
		r.r_xtop += wexpand;
		r.r_ytop += wexpand;
		r.r_xbot /= 2;
		r.r_ybot /= 2;
		r.r_xtop /= 2;
		r.r_ytop /= 2;

		DBWAreaChanged(def, &r, DBW_ALLWINDOWS, &DBAllButSpaceBits);
		DBReComputeBbox(def);
		DRCCheckThis (def, TT_CHECKPAINT, &r);
		freeMagic(plist);
	    }
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdWriteall --
 *
 * Implement the "writeall" command.
 * Write out all modified cells to disk.
 *
 * Usage:
 *	writeall [force [cellname...]]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	For each cell that has been modified since it was last written,
 *	the user is asked whether he wants to write it, flush it,
 *	skip it, or abort the "writeall" command.  If the decision
 *	is made to write, the cell is written out to disk and the
 *	modified bit in its definition's flags is cleared.  If the
 *	decision is made to flush, all paint and subcell uses are
 *	removed from the cell, and it is re-read from disk.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdWriteall(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int cmdWriteallFunc();
    static char *force[] = { "force", 0 };
    int argc;

    if ((cmd->tx_argc >= 2) && (Lookup(cmd->tx_argv[1], force) < 0))
    {
	TxError("Usage: %s [force [cellname ...]]\n", cmd->tx_argv[0]);
	return;
    }

    DBUpdateStamps();
    argc = cmd->tx_argc;
    (void) DBCellSrDefs(CDMODIFIED|CDBOXESCHANGED|CDSTAMPSCHANGED,
		cmdWriteallFunc, (ClientData)cmd);
    cmd->tx_argc = argc;
}

/*
 * Filter function used by CmdWriteall() above.
 * This function is called for each known CellDef whose modified bit
 * is set.
 */

    /*ARGSUSED*/
int
cmdWriteallFunc(def, cmd)
    CellDef *def;	/* Pointer to CellDef to be saved.  This def might
			 * be an internal buffer; if so, we ignore it.
			 */
    TxCommand *cmd;	/* Client data passed to DBCellSrDefs, a pointer
			 * to the command structure.  If cmd->tx_argc == 1,
			 * then prompt for each action.  If cmd->tx_argc
			 * == 2, then write all cells without asking.  If
			 * cmd->tx_argc > 2, then the arguments from 3 to
			 * argc is a list of cells to write.
			 */
{
    char *prompt, *argv;
    int i, action, cidx = 0;
    static char *actionNames[] =
        { "write", "flush", "skip", "abort", "autowrite", 0 };
    static char *explain[] =
	{ "", "(bboxes)", "(timestamps)", "(bboxes/timestamps)", 0 };

    if (def->cd_flags & CDINTERNAL) return 0;
    if (SigInterruptPending) return 1;

    if (cmd->tx_argc == 2)
    {
	action = 4;
    }
    else if (cmd->tx_argc > 2)
    {
	action = 2;		/* skip */
	for (i = 2; i < cmd->tx_argc; i++)
	    if (!strcmp(cmd->tx_argv[i], def->cd_name))
	    {
		action = 0;	/* write */
		break;
	    }
    }
    else	/* cmd->tx_argc == 1 */
    {
	if (!(def->cd_flags & CDMODIFIED))
	{
	    if (!(def->cd_flags & CDSTAMPSCHANGED))
		cidx = 1;
	    else if (!(def->cd_flags & CDBOXESCHANGED))
		cidx = 2;
	    else cidx = 3;
	}
	prompt = TxPrintString("%s %s: write, autowrite, flush, skip, "
		"or abort command? ", def->cd_name, explain[cidx]);
	action = TxDialog(prompt, actionNames, 0);
    }

    switch (action)
    {
	case 0:		/* Write */
	    cmdSaveCell(def, (char *) NULL, FALSE, TRUE);
	    break;
	case 1:		/* Flush */
	    cmdFlushCell(def);
	    break;
	case 2:		/* Skip */
	    break;
	case 3:		/* Abort command */
	    return 1;
	case 4:		/* Automatically write everything */
	    cmd->tx_argc = 2;
	    TxPrintf("Writing '%s'\n", def->cd_name);
	    cmdSaveCell(def, (char *) NULL, TRUE, TRUE);
	    break;
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdXload --
 *
 * Implement the "xload" command.
 *
 * Usage:
 *	xload [name]
 *
 * If name is supplied, then the window containing the point tool is
 * remapped so as to edit the cell with the given name.
 *
 * If no name is supplied, then a new cell with the name "(UNNAMED)"
 * is created in the selected window.  If there is already a cell by
 * that name in existence (eg, in another window), that cell gets loaded
 * rather than a new cell being created.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets EditCellUse.
 *
 * Notes:
 *	This does not yet implement the "-force" option.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdXload(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }

    if (cmd->tx_argc > 2)
    {
	TxError("Usage: %s [name]\n", cmd->tx_argv[0]);
	return;
    }

    if (cmd->tx_argc == 2)
    {
	if (CmdIllegalChars(cmd->tx_argv[1], "[],", "Cell name"))
	    return;
	DBWloadWindow(w, cmd->tx_argv[1], FALSE, TRUE);
    }
    else DBWloadWindow(w, (char *) NULL, FALSE, TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdXor --
 *
 * Implements xor command
 *
 * Usage:
 *	xor [-<option>] destname
 *
 * Results:
 *	rewrites paint tables with xor functions.
 *	Cell "destname" is assumed to exist.
 *	Existing top level cell is flattened into destname on top of
 *	what's already there, using an XOR paint function.  Whatever
 *	paint remains after the command is a difference between the
 *	two layouts.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdXor(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int			rval, xMask;
    bool		dolabels;
    char		*destname;
    CellDef		*newdef;
    CellUse		*newuse;
    SearchContext	scx;
    CellUse		*flatDestUse;

    PaintResultType	DBXORResultTbl[NP][NT][NT];
    PaintResultType	(*curPaintSave)[NT][NT];
    void		(*curPlaneSave)();

    int p, t, h;
     
    destname = cmd->tx_argv[cmd->tx_argc - 1];
    xMask = CU_DESCEND_ALL;
    dolabels = TRUE;

    rval = 0;
    if (cmd->tx_argc > 2)
    {
	int i;
	for (i = 1; i < (cmd->tx_argc - 1); i++)
	{
	    if (strncmp(cmd->tx_argv[i], "-no", 3))
	    {
	        rval = -1;
		break;
	    }
	    else if (strlen(cmd->tx_argv[i]) > 3)
	    {
		switch(cmd->tx_argv[1][3])
		{
		    case 'l':
			dolabels = FALSE;
			break;
		    case 's':
			xMask = CU_DESCEND_NO_SUBCKT;
			break;
		    case 'v':
			xMask = CU_DESCEND_NO_VENDOR;
			break;
		    default:
			TxError("options are: -nolabels, -nosubcircuits -novendor\n");
			break;
		}
	    }
	}
    }
    else if (cmd->tx_argc != 2)
	rval = -1;

    if (rval != 0)
    {
     	TxError("usage: xor [-<option>...] destcell\n");
	return;
    }
    /* create the new def */
    if ((newdef = DBCellLookDef(destname)) == NULL)
    {
    	 TxError("%s does not exist\n", destname);
	 return;
    }

    UndoDisable();
    newuse = DBCellNewUse(newdef, (char *) NULL);
    (void) StrDup(&(newuse->cu_id), "Flattened cell");
    DBSetTrans(newuse, &GeoIdentityTransform);
    newuse->cu_expandMask = CU_DESCEND_SPECIAL;
    flatDestUse = newuse;
    
    if (EditCellUse)
	scx.scx_use  = EditCellUse;
    else
	scx.scx_use = (CellUse *)w->w_surfaceID;

    scx.scx_area = scx.scx_use->cu_def->cd_bbox;
    scx.scx_trans = GeoIdentityTransform;

    // Prepare the XOR result table

    for (p = 0; p < DBNumPlanes; p++)
    {
	// During the copy, space tiles will not be painted over
	// anything.  However, due to non-manhattan geometry
	// handling, TT_SPACE is occasionally painted over a
	// split tile that is being erased and replaced by a
	// rectangular tile, so painting TT_SPACE should always
	// result in TT_SPACE.

	for (h = 0; h < DBNumTypes; h++)
	    DBXORResultTbl[p][0][h] = TT_SPACE;

	for (t = 1; t < DBNumTypes; t++)
	    for (h = 0; h < DBNumTypes; h++)
	    {
		if (t == h)
		    DBXORResultTbl[p][t][h] = TT_SPACE;
		else
		    DBXORResultTbl[p][t][h] = t;
	    }
    }

    curPaintSave = DBNewPaintTable(DBXORResultTbl);
    curPlaneSave = DBNewPaintPlane(DBPaintPlaneXor);

    DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, xMask, flatDestUse);
    if (dolabels)
	FlatCopyAllLabels(&scx, &DBAllTypeBits, xMask, flatDestUse);

    if (xMask != CU_DESCEND_ALL)
	DBCellCopyAllCells(&scx, xMask, flatDestUse, (Rect *)NULL);
    
    DBNewPaintTable(curPaintSave);
    DBNewPaintPlane(curPlaneSave);

    // Remove new use
    DBCellDeleteUse(newuse);

    UndoEnable();
}

