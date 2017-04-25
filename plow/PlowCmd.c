/*
 * PlowCmd.c --
 *
 * Commands for the plow module only.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plow/PlowCmd.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "textio/txcommands.h"
#include "plow/plow.h"
#include "select/select.h"

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPlow --
 *
 * Implement the "plow" command:  snowplow.
 * One side of the box forms the plow, which is swept through the layout
 * until it coincides with the opposite side of the box.  The direction
 * depends on that specified by the user.
 *
 * Usage:
 *	plow [options]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

    /*
     * The order of the following must be identical to that in
     * the table cmdPlowOption[] below.
     */
#define	PLOWBOUND		0
#define	PLOWHELP		1
#define	PLOWHORIZON		2
#define PLOWJOGS		3
#define	PLOWSELECTION		4
#define	PLOWSTRAIGHTEN		5
#define	PLOWNOBOUND		6
#define	PLOWNOJOGS		7
#define	PLOWNOSTRAIGHTEN	8
#define	PLOWPLOW		9	/* Implicit when direction specified */

void
CmdPlow(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int xdelta, ydelta, absX, absY;
    int option, dir, distance;
    char *layers, **msg;
    TileTypeBitMask mask;
    Rect rootBox, editBox, newBox;
    CellDef *rootDef, *editDef;
    Point rootPoint;
    MagWindow *window;
    Transform t;
    static char *cmdPlowOption[] =
    {	
	"boundary	set boundary around area plowing may affect",
	"help		print this help information",
	"horizon n	set the horizon for jog introduction to n lambda",
	"jogs		reenable jog insertion (set horizon to 0)",
	"selection [direction [amount]]\n\
		plow the selection",
	"straighten	automatically straighten jogs after each plow",
	"noboundary	remove boundary around area plowing may affect",
	"nojogs		disable jog insertion (infinite jog horizon)",
	"nostraighten	don't automatically straighten jogs after each plow",
	NULL
    };

    if (cmd->tx_argc < 2)
	goto usage2;

    option = Lookup(cmd->tx_argv[1], cmdPlowOption);
    if (option == -1)
    {
	TxError("Ambiguous plowing option: \"%s\"\n", cmd->tx_argv[1]);
	goto usage2;
    }
    if (option < 0)
    {
	dir = GeoNameToPos(cmd->tx_argv[1], TRUE, FALSE);
	if (dir < 0)
	    goto usage;
	dir = GeoTransPos(&RootToEditTransform, dir);
	option = PLOWPLOW;
    }

    switch (option)
    {
	case PLOWBOUND:
	case PLOWSELECTION:
	case PLOWNOBOUND:
	case PLOWPLOW:
    	    windCheckOnlyWindow(&w, DBWclientID);
	    if (w == (MagWindow *) NULL)
	    {
		TxError("Point to a window first\n");
		return;
	    }
	    if (EditCellUse == (CellUse *) NULL)
	    {
		TxError("There is no edit cell!\n");
		return;
	    }
	    if (!ToolGetEditBox(&editBox) || !ToolGetBox(&rootDef, &rootBox))
		return;
	    editDef = EditCellUse->cu_def;
	    break;
    }

    switch (option)
    {
	case PLOWHELP:
	    TxPrintf("Plow commands have the form \"plow option\",\n");
	    TxPrintf("where option is one of:\n\n");
	    for (msg = &(cmdPlowOption[0]); *msg != NULL; msg++)
	    {
		if (**msg == '*') continue;
		TxPrintf("  %s\n", *msg);
	    }
	    TxPrintf("\n");
	    TxPrintf("Option may also be any Manhattan direction, which\n");
	    TxPrintf("causes the plow to be moved in that direction.\n");
	    return;
	case PLOWBOUND:
	    if (cmd->tx_argc != 2)
	    {
wrongNumArgs:
		TxError("Wrong number of arguments to %s option.\n",
			cmd->tx_argv[1]);
		TxError("Type \":plow help\" for help.\n");
		return;
	    }
	    PlowSetBound(editDef, &editBox, rootDef, &rootBox);
	    break;
	case PLOWHORIZON:
	    if (cmd->tx_argc == 3) PlowJogHorizon = cmdParseCoord(w,
			cmd->tx_argv[2], TRUE, TRUE);
	    else if (cmd->tx_argc != 2) goto wrongNumArgs;

	    if (PlowJogHorizon == INFINITY)
		TxPrintf("Jog horizon set to infinity.\n");
	    else TxPrintf("Jog horizon set to %d units.\n", PlowJogHorizon);
	    break;
	case PLOWSTRAIGHTEN:
	    PlowDoStraighten = TRUE;
	    TxPrintf("Jogs will be straightened after each plow.\n");
	    break;
	case PLOWNOBOUND:
	    if (cmd->tx_argc != 2) goto wrongNumArgs;
	    PlowClearBound();
	    break;
	case PLOWNOJOGS:
	    if (cmd->tx_argc != 2) goto wrongNumArgs;
	    PlowJogHorizon = INFINITY;
	    TxPrintf("Jog insertion disabled.\n");
	    break;
	case PLOWJOGS:
	    if (cmd->tx_argc != 2) goto wrongNumArgs;
	    PlowJogHorizon = 0;
	    TxPrintf("Jog insertion re-enabled (horizon 0).\n");
	    break;
	case PLOWNOSTRAIGHTEN:
	    PlowDoStraighten = FALSE;
	    TxPrintf("Jogs will not be straightened automatically.\n");
	    break;
	case PLOWPLOW:
	    if (cmd->tx_argc > 3) goto wrongNumArgs;
	    layers = cmd->tx_argc == 2 ? "*,l,subcell,space" : cmd->tx_argv[2];
	    if (!CmdParseLayers(layers, &mask))
		break;
	    if (Plow(editDef, &editBox, mask, dir))
		break;

	    TxPrintf("Reduced plow size to stay within the boundary.\n");
	    GeoTransRect(&EditToRootTransform, &editBox, &rootBox);
	    ToolMoveBox(TOOL_BL, &rootBox.r_ll, FALSE, rootDef);
	    ToolMoveCorner(TOOL_TR, &rootBox.r_ur, FALSE, rootDef);
	    break;
	case PLOWSELECTION:
	    if (cmd->tx_argc > 2)
	    {
		dir = GeoNameToPos(cmd->tx_argv[2], TRUE, TRUE);
		if (dir < 0)
		    return;
		if (cmd->tx_argc == 4)
		{
		    switch (dir)
		    {
			case GEO_EAST: case GEO_WEST:
			    distance = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
			    break;
			case GEO_NORTH: case GEO_SOUTH:
			    distance = cmdParseCoord(w, cmd->tx_argv[3], TRUE, FALSE);
			    break;
		    }
		}
		else distance = 1;

		switch (dir)
		{
		    case GEO_NORTH: xdelta = 0; ydelta = distance; break;
		    case GEO_SOUTH: xdelta = 0; ydelta = -distance; break;
		    case GEO_EAST:  xdelta = distance; ydelta = 0; break;
		    case GEO_WEST:  xdelta = -distance; ydelta = 0; break;
		    default:
			ASSERT(FALSE, "Bad direction in CmdPlow");
			return;
		}
	    }
	    else
	    {
		/*
		 * Use the displacement between the box lower-left corner
		 * and the point as the transform.
		 */
		if (rootDef != SelectRootDef)
		{
		    TxError("\"plow selection\" uses the box lower-left\n");
		    TxError("corner as a place to pick up the selection\n");
		    TxError("for plowing, but the box isn't in a window\n");
		    TxError("containing the selection\n");
		    return;
		}
		window = ToolGetPoint(&rootPoint, (Rect *) NULL);
		if ((window == NULL) ||
		    (EditRootDef != ((CellUse *) window->w_surfaceID)->cu_def))
		{
		    TxError("\"plow selection\" uses the point as the\n");
		    TxError("place to plow the selection, but the point\n");
		    TxError("doesn't point to the edit cell.\n");
		    return;
		}
		xdelta = rootPoint.p_x - rootBox.r_xbot;
		ydelta = rootPoint.p_y - rootBox.r_ybot;
		if (xdelta < 0) absX = -xdelta; else absX = xdelta;
		if (ydelta < 0) absY = -ydelta; else absY = ydelta;
		if (absY <= absX)
		{
		    ydelta = 0;
		    distance = absX;
		    dir = xdelta > 0 ? GEO_EAST : GEO_WEST;
		}
		else
		{
		    xdelta = 0;
		    distance = absY;
		    dir = ydelta > 0 ? GEO_NORTH : GEO_SOUTH;
		}
	    }
	    if (!PlowSelection(editDef, &distance, dir))
	    {
		TxPrintf("Reduced distance to stay in the boundary.\n");
		switch (dir)
		{
		    case GEO_EAST:	xdelta =  distance; break;
		    case GEO_NORTH:	ydelta =  distance; break;
		    case GEO_WEST:	xdelta = -distance; break;
		    case GEO_SOUTH:	ydelta = -distance; break;
		}
	    }
	    GeoTransTranslate(xdelta, ydelta, &GeoIdentityTransform, &t);
	    GeoTransRect(&t, &rootBox, &newBox);
	    DBWSetBox(rootDef, &newBox);
	    SelectClear();
	    break;
    }
    return;

usage:
    TxError("\"%s\" isn't a valid plow option.", cmd->tx_argv[1]);

usage2:
    TxError("  Type \"plow help\" for help.\n");
    return;
}
/*
 * ----------------------------------------------------------------------------
 *
 * CmdStraighten --
 *
 * Straighten jogs in an area by pulling in a particular direction.
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
CmdStraighten(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect editBox;
    int dir;

    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first\n");
	return;
    }

    if (cmd->tx_argc != 2)
	goto usage;

    dir = GeoNameToPos(cmd->tx_argv[1], TRUE, FALSE);
    if (dir < 0)
	goto usage;
    dir = GeoTransPos(&RootToEditTransform, dir);

    if (EditCellUse == (CellUse *) NULL)
    {
	TxError("There is no edit cell!\n");
	return;
    }
    if (!ToolGetEditBox(&editBox))
    {
	TxError("The box is not in a window over the edit cell.\n");
	return;
    }

    PlowStraighten(EditCellUse->cu_def, &editBox, dir);
    return;

usage:
    TxError("Usage: straighten manhattan-direction\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPlowTest --
 *
 * Debugging of plowing.
 *
 * Usage:
 *	*plow cmd [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See comments in PlowTest() in plow/PlowTest.c for details.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdPlowTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    PlowTest(w, cmd);
}

