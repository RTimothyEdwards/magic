/*
 * CmdLQ.c --
 *
 * Commands with names beginning with the letters L through Q.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdLQ.c,v 1.9 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/fonts.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "graphics/graphics.h"
#include "drc/drc.h"
#include "textio/txcommands.h"
#include "utils/undo.h"
#include "select/select.h"
#include "netmenu/netmenu.h"

/* Forward declarations */

void CmdPaintEraseButton();


/*
 * ----------------------------------------------------------------------------
 *
 * CmdLabelProc --
 *
 * 	This procedure does all the work of putting a label except for
 *	parsing argments.  It is separated from CmdLabel so it can be
 *	used by the net-list menu system.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A label is added to the edit cell at the box location, with
 *	the given text position, on the given layer.  If type is -1,
 *	then there must be only one layer underneath the box, and the
 *	label is added to that layer.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdLabelProc(text, font, size, rotate, offx, offy, pos, sticky, type)
    char *text;			/* Text for label. */
    int font;			/* Known font to use, or -1 for X11 fonts */
    int size;			/* Fixed size of font (invalid with X11 fonts) */
    int rotate;			/* Rotation (invalid with X11 fonts) */
    int offx;			/* Position offset X (invalid with X11 fonts) */
    int offy;			/* Position offset Y (invalid with X11 fonts) */
    int pos;			/* Justification of text relative to text. -1
				 * means "pick a nice one for me."
				 */
    bool sticky;		/* 1 if label should not be moved off chosen layer */
    TileType type;		/* Type of material label is to be attached
				 * to.  -1 means "pick a reasonable layer".
				 */
{
    Rect editBox, tmpArea;
    Point offset;
    Label *lab;

    /* Make sure the box exists */
    if (!ToolGetEditBox(&editBox)) return;

    /* Make sure there's a valid string of text. */

    if ((text == NULL) || (*text == 0))
    {
	TxError("Can't have null label name.\n");
	return;
    }
    if (CmdIllegalChars(text, " /", "Label name"))
	return;

    if (type < 0) type = TT_SPACE;

    /* Eliminate any duplicate labels at the same position, regardless
     * of type.
     */

    DBEraseLabelsByContent(EditCellUse->cu_def, &editBox, -1, text);

    offset.p_x = offx;
    offset.p_y = offy;
    lab = DBPutFontLabel(EditCellUse->cu_def, &editBox, font, size,
		rotate, &offset, pos, text, type,
		((sticky) ? LABEL_STICKY : 0));
    DBAdjustLabels(EditCellUse->cu_def, &editBox);
    DBReComputeBbox(EditCellUse->cu_def);
    tmpArea = lab->lab_rect;
    lab->lab_rect = editBox;
    DBWLabelChanged(EditCellUse->cu_def, lab, DBW_ALLWINDOWS);
    lab->lab_rect = tmpArea;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdLabel --
 *
 * Implement the "label" command.
 * Place a label at a specific point on a specific type in EditCell
 *
 * Usage:
 *	label <text> [<direction> [<layer>]]
 *	label <text> <font> [<size> [<rotation>
 *			[<offsetx> <offsety> [<direction> [<layer>]]]]]
 *
 * Direction may be one of:
 *	right left top bottom
 *	east west north south
 *	ne nw se sw
 * or any unique abbreviation.  If not specified, it defaults to a value
 * chosen to keep the label text inside the cell.
 *
 * Layer defaults to the type of material beneath the degenerate box.
 * If the box is a rectangle, then use the lower left corner to determine
 * the material.
 *
 * If more than more than one tiletype other than space touches the box,
 * then the "layer" must be specified in the command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified EditCellUse->cu_def.
 *
 * ----------------------------------------------------------------------------
 */


void
CmdLabel(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileType type;
    int pos, font = -1, size = 0, rotate = 0, offx = 0, offy = 0;
    bool sticky = FALSE;
    int option;
    char *p;

    if (cmd->tx_argc < 2 || cmd->tx_argc > 9)
    {
	TxError("Usage: %s text [direction [layer]]\n", cmd->tx_argv[0]);
	TxError("or:    %s text font [size [rotation [offsetx offsety "
		"[direction [layer]]]]]\n", cmd->tx_argv[0]);
	return;
    }

    p = cmd->tx_argv[1];

    /*
     * Find and check validity of position.
     */

    if (cmd->tx_argc > 2)
    {
	pos = GeoNameToPos(cmd->tx_argv[2], FALSE, FALSE);
	if (pos < 0)
	{
	    if (StrIsInt(cmd->tx_argv[2]))
	    {
		font = atoi(cmd->tx_argv[2]);
		if (font < 0 || font >= DBNumFonts)
		{
		    if (DBNumFonts == 0)
			TxError("No vector outline fonts are loaded!\n");
		    else
			TxError("Font value out of range (0 to %d)\n", DBNumFonts - 1);
		}
	    }
	    else
	    {
		/* Assume that this is a font name */

		font = DBNameToFont(cmd->tx_argv[2]);
		if (font < -1)
		{
		    TxError("Unknown vector outline font \"%s\"\n", cmd->tx_argv[2]);
		    return;
		}
	    }
	}
	else
	    pos = GeoTransPos(&RootToEditTransform, pos);
    }
    else pos = -1;

    if (font >= 0)
    {
	char *yp = NULL;

	size = DBLambda[1];
	if (cmd->tx_argc > 3)
	    if (StrIsNumeric(cmd->tx_argv[3]))
		size = cmdScaleCoord(w, cmd->tx_argv[3], TRUE, TRUE, 8);

	if (cmd->tx_argc > 4)
	    if (StrIsInt(cmd->tx_argv[4]))
		rotate = atoi(cmd->tx_argv[4]);

	if (cmd->tx_argc > 6)
	{
	    if ((yp = strchr(cmd->tx_argv[5], ' ')) != NULL)
	    {
		*yp = '\0';
		yp++;
		if (StrIsNumeric(cmd->tx_argv[5]) && StrIsNumeric(yp))
		{
		    offx = cmdScaleCoord(w, cmd->tx_argv[5], TRUE, TRUE, 8);
		    offy = cmdScaleCoord(w, yp, TRUE, FALSE, 8);
		    *yp = ' ';
		}
		else
		{
		    TxError("Uninterpretable offset value \"%s %s\"\n",
				cmd->tx_argv[5], yp);
		    *yp = ' ';
		    return;
		}
	    }
	    else if (StrIsNumeric(cmd->tx_argv[5]) && StrIsNumeric(cmd->tx_argv[6]))
	    {
		/* offx and offy are entered in lambda units and	*/
		/* re-interpreted as eighth-internal units.		*/

		offx = cmdScaleCoord(w, cmd->tx_argv[5], TRUE, TRUE, 8);
		offy = cmdScaleCoord(w, cmd->tx_argv[6], TRUE, FALSE, 8);
	    }
	    else
	    {
		TxError("Uninterpretable offset value \"%s %s\"\n", cmd->tx_argv[5],
			cmd->tx_argv[6]);
		return;
	    }
	}

	if (((yp != NULL) && (cmd->tx_argc > 6))  || (cmd->tx_argc > 7))
	{
	    int pidx = (yp != NULL) ? 6 : 7;
	    pos = GeoNameToPos(cmd->tx_argv[pidx], FALSE, TRUE);
	    if (pos < 0)
		return;
	    else
		pos = GeoTransPos(&RootToEditTransform, pos);
	}
    }
    
    /*
     * Find and check validity of type parameter.  Accept prefix "-" on
     * layer as an indication of a "sticky" label (label is fixed to
     * indicated type and does not change).
     */

    if ((font < 0 && cmd->tx_argc > 3) || (font >= 0 && cmd->tx_argc >= 8))
    {
	char *typename;

	typename = cmd->tx_argv[cmd->tx_argc - 1];
	if (*typename == '-')
	{
	    sticky = TRUE;
	    typename++;
	}
	type = DBTechNameType(typename);
	if (type < 0)
	{
	    TxError("Unknown layer: %s\n", cmd->tx_argv[cmd->tx_argc - 1]);
	    return;
	}
    } else type = -1;

    CmdLabelProc(p, font, size, rotate, offx, offy, pos, sticky, type);
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdLoad --
 *
 * Implement the "load" command.
 *
 * Usage:
 *	load [name [scaled n [d]]] [-force]
 *
 * If name is supplied, then the window containing the point tool is
 * remapped so as to edit the cell with the given name.
 *
 * If no name is supplied, then a new cell with the name "(UNNAMED)"
 * is created in the selected window.  If there is already a cell by
 * that name in existence (eg, in another window), that cell gets loaded
 * rather than a new cell being created.
 *
 * An input file can be scaled by specifying the "scaled" option, for
 * which the geometry of the input file is multiplied by n/d.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets EditCellUse.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdLoad(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int n = 1;
    int d = 1;
    int locargc = cmd->tx_argc;
    bool ignoreTech = FALSE;
    bool noWindow = FALSE;
    int keepGoing();			/* forward declaration */

    if (locargc > 2)
    {
	if (!strncmp(cmd->tx_argv[locargc - 1], "-nowindow", 8))
	{
	    locargc--;
	    noWindow = TRUE;
	}
	if (!strncmp(cmd->tx_argv[locargc - 1], "-force", 6))
	{
	    locargc--;
	    ignoreTech = TRUE;
	}
	if (locargc >= 4 && !strncmp(cmd->tx_argv[2], "scale", 5) &&
		StrIsInt(cmd->tx_argv[3]))
	{
	    n = atoi(cmd->tx_argv[3]);
 	    if (cmd->tx_argc == 5 && StrIsInt(cmd->tx_argv[4]))
	        d = atoi(cmd->tx_argv[4]);
	    else if (locargc != 4)
	    {
		TxError("Usage: %s name scaled n [d]\n", cmd->tx_argv[0]);
		return;
	    }
	    DBLambda[0] *= d;
	    DBLambda[1] *= n;
	    ReduceFraction(&DBLambda[0], &DBLambda[1]);
	}
	else if (!ignoreTech && !noWindow)
	{
	    TxError("Usage: %s [name [scaled n [d]]]\n", cmd->tx_argv[0]);
	    return;
	}
    }

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) && (noWindow == FALSE))
    {
	TxError("Point to a window first.\n");
	return;
    }

    if (locargc > 1)
    {
	if (CmdIllegalChars(cmd->tx_argv[1], "[],", "Cell name"))
	    return;
#ifdef MAGIC_WRAPPER
	/* Names that have been list-braced in Tcl need to be modified */
	if (cmd->tx_argv[1][0] == '{')
	{
	    cmd->tx_argv[1]++;
	    *(cmd->tx_argv[1] + strlen(cmd->tx_argv[1]) - 1) = '\0';
	}
#endif
	DBWloadWindow((noWindow == TRUE) ? NULL : w, cmd->tx_argv[1],
			ignoreTech, FALSE);

	if ((n > 1) || (d > 1))
	{
            CellUse *topuse = (CellUse *)w->w_surfaceID;

	    /* To ensure that all subcells are also scaled, we need to	*/
	    /* expand the entire cell hierarchy to force a load of the	*/
	    /* cell.  After this, we return all the expansion flags	*/
	    /* back to the way they were originally.			*/

	    TxPrintf("Recursively reading all cells at new scale.\n");

	    DBExpandAll(topuse, &(topuse->cu_bbox),
			((DBWclientRec *)w->w_clientData)->dbw_bitmask,
			TRUE, keepGoing, NULL);

	    DBExpandAll(topuse, &(topuse->cu_bbox),
			((DBWclientRec *)w->w_clientData)->dbw_bitmask,
			FALSE, keepGoing, NULL);
	    DBExpand(topuse,
			((DBWclientRec *)w->w_clientData)->dbw_bitmask,
			TRUE);

	    /* We don't want to save and restore DBLambda, because      */
	    /* loading the file may change their values.  Instead, we   */
	    /* invert the scaling factor and reapply.                   */

	    DBLambda[0] *= n;
	    DBLambda[1] *= d;
	    ReduceFraction(&DBLambda[0], &DBLambda[1]);
	}
    }
    else DBWloadWindow(w, (char *) NULL, TRUE, FALSE);
}

/*
 * Function callback which continues the search through all cells for
 * expansion/unexpansion.
 */

int
keepGoing(use, clientdata)
    CellUse *use;
    ClientData clientdata;
{
    return 0;	/* keep the search going */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdMove --
 *
 * Implement the "move" command.
 *
 * Usage:
 *	move [direction [amount]]
 *	move to x y
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Moves everything that's currently selected.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdMove(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
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
	    goto moveToPoint;
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
	    case GEO_NORTHEAST:
	    case -2:
		xdelta = amountx;
		ydelta = amounty;
		break;
	    case GEO_NORTHWEST:
		xdelta = -amountx;
		ydelta = amounty;
		break;
	    case GEO_SOUTHEAST:
		xdelta = amountx;
		ydelta = -amounty;
		break;
	    case GEO_SOUTHWEST:
		xdelta = -amountx;
		ydelta = -amounty;
		break;
	    default:
		ASSERT(FALSE, "Bad direction in CmdMove");
		return;
	}
	GeoTransTranslate(xdelta, ydelta, &GeoIdentityTransform, &t);

	/* Move the box by the same amount as the selection, if the
	 * box exists.
         * If no selection exists, but the box does, then move the box
         * anyway (hace 10/8/97)
	 * The above method is superceded by "box move <dir> <dist>"
	 * but is retained for backward compatibility.
	 */

	if (ToolGetBox(&rootDef, &rootBox) && ((rootDef == SelectRootDef)
                                           || (SelectRootDef == NULL)))
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
	    TxError("\"Move\" uses the point as the place to put down a\n");
	    TxError("    the selection, but the point doesn't point to the\n");
	    TxError("    edit cell.\n");
	    return;
	}

moveToPoint:
	if (!ToolGetBox(&rootDef, &rootBox) || (rootDef != SelectRootDef))
	{
	    TxError("\"Move\" uses the box lower-left corner as a place\n");
	    TxError("    to pick up the selection for moving, but the box\n");
	    TxError("    isn't in a window containing the selection.\n");
	    return;
	}
	GeoTransTranslate(rootPoint.p_x - rootBox.r_xbot,
	    rootPoint.p_y - rootBox.r_ybot, &GeoIdentityTransform, &t);
	GeoTransRect(&t, &rootBox, &newBox);
	DBWSetBox(rootDef, &newBox);
    }
    
    SelectTransform(&t);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPaint --
 *
 * Implement the "paint" command.
 * Paint the specified layers underneath the box in EditCellUse->cu_def.
 *
 * Usage:
 *	paint <layers> | cursor
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified EditCellUse->cu_def.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdPaint(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect editRect;
    TileTypeBitMask mask;

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: %s <layers> | cursor\n", cmd->tx_argv[0]);
	return;
    }

    if (!strncmp(cmd->tx_argv[1], "cursor", 6))
    {
	CmdPaintEraseButton(w, &cmd->tx_p, TRUE);
	return;
    }
    else if (!CmdParseLayers(cmd->tx_argv[1], &mask))
	return;

    if (!ToolGetEditBox(&editRect)) return;

    if (TTMaskHasType(&mask, L_LABEL))
    {
	TxError("Label layer cannot be painted.  Use the \"label\" command\n");
	return;
    }
    if (TTMaskHasType(&mask, L_CELL))
    {
	TxError("Subcell layer cannot be painted.  Use \"getcell\".\n");
	return;
    }

    TTMaskClearType(&mask, TT_SPACE);
    DBPaintValid(EditCellUse->cu_def, &editRect, &mask, 0);
    DBAdjustLabels(EditCellUse->cu_def, &editRect);
    SelectClear();
    DBWAreaChanged(EditCellUse->cu_def, &editRect, DBW_ALLWINDOWS, &mask);
    DBReComputeBbox(EditCellUse->cu_def);
    DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &editRect);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPaintEraseButton --
 *
 * Paint the specified layers underneath the box in EditCellUse->cu_def.
 * Implements the traditional "box tool" Button2 function as a command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified EditCellUse->cu_def.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdPaintEraseButton(w, butPoint, isPaint)
    MagWindow *w;
    Point *butPoint;	/* Screen location at which button was raised */
    bool isPaint;	/* True for paint, False for erase.	*/
{
    Rect rootRect, editRect, areaReturn;
    TileTypeBitMask mask;
    DBWclientRec *crec;

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }
    crec = (DBWclientRec *) w->w_clientData;

    WindPointToSurface(w, butPoint, (Point *) NULL, &rootRect);

    DBSeeTypesAll(((CellUse *)w->w_surfaceID), &rootRect, 
	    crec->dbw_bitmask, &mask);
    TTMaskAndMask(&mask, &DBActiveLayerBits);
    TTMaskAndMask(&mask, &crec->dbw_visibleLayers);
    TTMaskClearType(&mask, TT_SPACE);

    if (!ToolGetEditBox(&editRect)) return;

    areaReturn = editRect;
    if (TTMaskEqual(&mask, &DBZeroTypeBits))
    {
	TileTypeBitMask eraseMask;

	TTMaskAndMask3(&mask, &CmdYMAllButSpace, &crec->dbw_visibleLayers);

	/* A little extra bit of cleverness:  if the box is zero size
	 * then delete all labels (this must be what the user intended
	 * since a zero size box won't delete any paint).  Otherwise,
	 * only delete labels whose paint has completely vanished.
	 */

	if (GEO_RECTNULL(&editRect))
	    TTMaskSetType(&mask, L_LABEL);

	TTMaskAndMask3(&eraseMask, &crec->dbw_visibleLayers, &DBActiveLayerBits);
	DBEraseValid(EditCellUse->cu_def, &editRect, &eraseMask, 0);
	DBEraseLabel(EditCellUse->cu_def, &editRect, &mask, &areaReturn);
    }
    else if (isPaint)
	DBPaintValid(EditCellUse->cu_def, &editRect, &mask, 0);
    else
    {
	DBEraseValid(EditCellUse->cu_def, &editRect, &mask, 0);
	DBEraseLabel(EditCellUse->cu_def, &editRect, &mask);
    }
    SelectClear();
    DBAdjustLabels(EditCellUse->cu_def, &editRect);

    DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &editRect);
    DBWAreaChanged(EditCellUse->cu_def, &areaReturn, DBW_ALLWINDOWS, &mask);
    DBReComputeBbox(EditCellUse->cu_def);
    UndoNext();
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPath --
 *
 * Implement the "path" command:  set a global cell search path.
 *
 * Usage:
 *	path [search|cell|sys] [path]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The indicated path is set to the first command line argument.
 *	A "+" in front of the path indicates append to the current path.
 *	If the middle argument is missing, the search path is assumed.
 *	If no argument is given, then the current paths are printed.
 *
 * ----------------------------------------------------------------------------
 */

#define PATHSEARCH	0
#define PATHCELL	1
#define PATHSYSTEM	2
#define PATHHELP	3

void
CmdPath(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char **pathptr;
    char *srcptr;
    int option;
    static char *cmdPathOption[] =
    {
	"search	[[+]path]	set [append to] search path",
	"cell	[[+]path]	set [append to] cell path",
	"sys    [[+]path]	set [append to] system path",
	"help			print this help information",
	NULL
    };

    if (cmd->tx_argc > 3)
	goto usage;

    else if (cmd->tx_argc == 1)
    {
	TxPrintf("Search path for cells is \"%s\"\n", Path);
	TxPrintf("Cell library search path is \"%s\"\n", CellLibPath);
	TxPrintf("System search path is \"%s\"\n", SysLibPath);
	return;
    }
    option = Lookup(cmd->tx_argv[1], cmdPathOption);
    if (option == -1)
    {
	TxError("Ambiguous path option: \"%s\"\n", cmd->tx_argv[1]);
	goto usage;
    }

    switch (option) {
	case PATHHELP:
	    goto usage;
	    break;
	case PATHSEARCH:
	    pathptr = &Path;
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, Path, NULL);
#else
		TxPrintf("Search path for cells is \"%s\"\n", Path);
#endif
		return;
	    }
	    else
	        srcptr = cmd->tx_argv[2];
	    break;
	case PATHCELL:
	    pathptr = &CellLibPath;
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, CellLibPath, NULL);
#else
		TxPrintf("Cell library search path is \"%s\"\n", CellLibPath);
#endif
		return;
	    }
	    else
	        srcptr = cmd->tx_argv[2];
	    break;
	case PATHSYSTEM:
	    pathptr = &SysLibPath;
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, SysLibPath, NULL);
#else
		TxPrintf("System search path is \"%s\"\n", SysLibPath);
#endif
		return;
	    }
	    else
	        srcptr = cmd->tx_argv[2];
	    break;
	default:
	    if (cmd->tx_argc == 2)
	    {
	        pathptr = &Path;
		srcptr = cmd->tx_argv[1];
	    }
	    else
		goto usage;
    }

    if (*srcptr == '+')
    {
	srcptr++;
	PaAppend(pathptr, srcptr);
    }
    else
    {
	(void) StrDup(pathptr, srcptr);
    }
    return;
	
usage:
    TxError("Usage: %s [search|cell|sys] [[+]path]\n", cmd->tx_argv[0]);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdPolygon --
 *
 * Implement the "polygon" command.  Draws a polygon based on the
 * point pairs supplied.
 *
 * Usage:
 *	polygon tiletype x1 y1 x2 y2 [x3 y3 ...] xn yn
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Paints into the database.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdPolygon(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    TileType type;
    int points, i, j, pNum;
    Point *plist;
    Rect r;
    CellDef *def = (CellDef *)NULL;
    PaintUndoInfo ui;

    if (EditCellUse != NULL)
	def = EditCellUse->cu_def;

    if (def == NULL)
    {
	TxError("No cell being edited\n");
	return;
    }

    if (cmd->tx_argc < 8)
    {
	TxError("Usage:  polygon tiletype x1 y1 x2 y2 [x3 y3 ...] xn yn\n");
	return;
    }

    type = DBTechNoisyNameType(cmd->tx_argv[1]);
    if (type < 0)
	return;

    if (cmd->tx_argc & 1)
    {
	TxError("Unpaired coordinate value\n");
	return;
    }
    points = (cmd->tx_argc - 2) >> 1;
    plist = (Point *)mallocMagic(points * sizeof(Point));
    for (i = 0, j = 2; i < points; i++)
    {
	plist[i].p_x = cmdParseCoord(w, cmd->tx_argv[j++], FALSE, TRUE);
	plist[i].p_y = cmdParseCoord(w, cmd->tx_argv[j++], FALSE, FALSE);
    }

    def->cd_flags |= CDMODIFIED | CDGETNEWSTAMP;
    ui.pu_def = def;
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
    {
	if (DBPaintOnPlane(type, pNum))
	{
	    ui.pu_pNum = pNum;
	    PaintPolygon(plist, points, def->cd_planes[pNum],
			DBStdPaintTbl(type, pNum), &ui, FALSE);
	}
    }

    /* Get the bounding box of the polygon and update the cell bbox */

    r.r_xbot = r.r_xtop = plist[0].p_x;
    r.r_ybot = r.r_ytop = plist[0].p_y;
    for (i = 1; i < points; i++)
	GeoIncludePoint(plist + i, &r);

    DBWAreaChanged(def, &r, DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DBReComputeBbox(def);
    DRCCheckThis(def, TT_CHECKPAINT, &r);
    freeMagic(plist);
}

/*----------------------------------------------------------------------*/
/* portFindLabel ---							*/
/*									*/
/* Find a label in the cell editDef.					*/
/*									*/
/* If "port" is true, then search only for labels that are ports.	*/
/* If "unique" is true, then return a label only if exactly one label	*/
/* is found in the edit box.						*/ 
/*----------------------------------------------------------------------*/

Label *
portFindLabel(editDef, port, unique)
    CellDef *editDef;
    bool unique;
    bool port;
{
    bool found;
    Label *lab, *sl;
    Rect editBox;

    /*
     * Check for unique label in box area
     */

    ToolGetEditBox(&editBox);
    found = FALSE;
    lab = NULL;
    for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
    {
	if (GEO_OVERLAP(&editBox, &sl->lab_rect))
	{
	    if (found == TRUE)
	    {
		/* Let's do this again with the GEO_SURROUND function	*/
		/* and see if we come up with only one label.		*/

		found = FALSE;
		for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
		{
		    if (GEO_SURROUND(&editBox, &sl->lab_rect))
		    {
			if (found == TRUE && unique == TRUE) return NULL;
			lab = sl;
			found = TRUE;
		    }
		}
		break;
	    }
	    lab = sl;
	    found = TRUE;
	}
    }
    return lab;
}
/*
 * ----------------------------------------------------------------------------
 *
 * CmdPort --
 *
 * Implement the "port" command.  Finds a label inside the cursor box.
 * If this label is unique (only one label found), then its flags are
 * set according to "num" and one or more "connect_directions" (see
 * usage, below).
 *
 * Usage:
 *	port make|makeall [num] [connect_direction(s)]
 * or
 *	port [name|num] class|use|index [value]
 *
 * num is the index of the port, usually beginning with 1.  This indicates
 *	the order in which ports should be written to a subcircuit record
 *	in extracted output.  The edit cell is searched for ports to make
 *	sure that "num" is unique.  If not specified, "num" will use the
 *	first available index starting at 1.
 *
 * connect_directions is one or more of the manhattan (n, s, e, w)
 *	directions.  This indicates to place and route tools where
 *	networks may connect to the subcircuit.  To extresis, the
 *	direction indicates the direction of current flow into the
 *	subcircuit.  If not specified, connect_directions will use
 *	the direction of the label text as the direction of connection.
 *
 * "value" is a value string representing one of the valid port classes
 *	or uses.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified label record.
 *
 * ----------------------------------------------------------------------------
 */

#define PORT_CLASS	0
#define PORT_USE	1
#define PORT_INDEX	2
#define PORT_EQUIV	3
#define PORT_EXISTS	4
#define PORT_CONNECT	5
#define PORT_LAST	6
#define PORT_MAKE	7
#define PORT_MAKEALL	8
#define PORT_NAME	9
#define PORT_REMOVE	10
#define PORT_HELP	11	

void
CmdPort(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char **msg;
    int argstart;
    int i, idx, pos, type, option, argc;
    unsigned short dirmask;
    bool found;
    Label *lab, *sl;
    Rect editBox, tmpArea;
    CellDef *editDef = EditCellUse->cu_def;

    static char *cmdPortOption[] =
    {
	"class	[type]		get [set] port class type",
	"use	[type]		get [set] port use type",
	"index	[number]	get [set] port number",
	"equivalent [number]	make port equivalent to another port",
	"exists			report if a label is a port or not",
	"connections [dir...]	get [set] port connection directions",
	"last			report the highest port number used",
	"make [index] [dir...]	turn a label into a port",
	"makeall [index] [dir]	turn all labels into ports",
	"name			report the port name",
	"remove			turn a port back into a label",
	"help			print this help information",
	NULL
    };

    static char *cmdPortClassTypes[] =
    {
	"default",
	"input",
	"output",
	"tristate",
	"bidirectional",
	"inout",
	"feedthrough",
	"feedthru",
	NULL
    };

    static int cmdClassToBitmask[] =
    {
	PORT_CLASS_DEFAULT,
	PORT_CLASS_INPUT,
	PORT_CLASS_OUTPUT,
	PORT_CLASS_TRISTATE,
	PORT_CLASS_BIDIRECTIONAL,
	PORT_CLASS_BIDIRECTIONAL,
	PORT_CLASS_FEEDTHROUGH,
	PORT_CLASS_FEEDTHROUGH
    };

    static char *cmdPortUseTypes[] =
    {
	"default",
	"analog",
	"signal",
	"digital",
	"power",
	"ground",
	"clock",
	NULL
    };

    static int cmdUseToBitmask[] =
    {
	PORT_USE_DEFAULT,
	PORT_USE_ANALOG,
	PORT_USE_SIGNAL,
	PORT_USE_SIGNAL,
	PORT_USE_POWER,
	PORT_USE_GROUND,
	PORT_USE_CLOCK
    };

    argstart = 1;
    argc = cmd->tx_argc;
    if (argc > 6 || argc == 1)
        goto portWrongNumArgs;
    else
    {
	/* Handle syntax "port <name>|<index> [option ...]"	*/
	/* Does not require a selection.			*/

	lab = NULL;
	if (argc > 2) {
	    option = Lookup(cmd->tx_argv[2], cmdPortOption);
	    if (option >= 0 && option != PORT_HELP)
	    {
		char *portname;

		if (StrIsInt(cmd->tx_argv[1]))
		{
		    int portidx = atoi(cmd->tx_argv[1]);
		    for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
			if (sl && ((sl->lab_flags & PORT_DIR_MASK) != 0))
			    if ((sl->lab_flags & PORT_NUM_MASK) == portidx)
			    {
				lab = sl;
				break;
			    }
		}
		else
		{

 		    portname = cmd->tx_argv[1];
		    for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
			if (!strcmp(portname, sl->lab_text))
			{
			    lab = sl;
			    break;
			}
		}
		if (lab == NULL)
		{
		    TxError("No label found with that name\n.");
		    return;
		}
		argstart = 2;
		argc--;
	    }
	    else
		option = Lookup(cmd->tx_argv[1], cmdPortOption);
	}
	else
	    option = Lookup(cmd->tx_argv[1], cmdPortOption);
	if (option < 0 || option == PORT_HELP) goto portWrongNumArgs;
    }

    if (option >= 0)
    {
	/* Check for options that require only one selected port */

	if ((option != PORT_REMOVE) && (option != PORT_LAST))
	{
	    if (lab == NULL)
		lab = portFindLabel(editDef, TRUE, TRUE);

	    if (option == PORT_EXISTS)
	    {
		if (lab && ((lab->lab_flags & PORT_DIR_MASK) != 0))
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(1));
#else
		    TxPrintf("true\n");
#endif
		else
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(0));
#else
		    TxPrintf("false\n");
#endif
		return;
	    }

	}
	if ((option != PORT_LAST) && lab == NULL)
	{
	    /* Let "port remove" work without complaining. */
	    if (option != PORT_REMOVE)
		TxError("Exactly one label may be present under the cursor box.\n");
	    return;
	}

	/* Check for options that require label to be a port */

	if ((option != PORT_MAKE) && (option != PORT_MAKEALL)
		&& (option != PORT_EXISTS) && (option != PORT_LAST))
	{
	    /* label "lab" must already be a port */
	    if (!(lab->lab_flags & PORT_DIR_MASK))
	    {
		if (option != PORT_REMOVE)
		    TxError("The selected label is not a port.\n");
		return;
	    }
	}

	/* Handle all command options */
	switch (option)
	{
	    case PORT_LAST:
		i = -1;
		for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
		{
		    idx = sl->lab_flags & PORT_NUM_MASK;
		    if (idx > i) i = idx;
		}
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(i));
#else
		TxPrintf("%d\n", i);
#endif
		break;

	    case PORT_EXISTS:
		if (!(lab->lab_flags & PORT_DIR_MASK))
		{
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(0));
#else
		    TxPrintf("false\n");
#endif
		}
		else
		{
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewBooleanObj(1));
#else
		    TxPrintf("true\n");
#endif
		}
		break;

	    case PORT_NAME:
		if (argc == 2)
		{
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewStringObj(lab->lab_text, -1));
#else
		    TxPrintf("Name = %s\n", lab->lab_text);
#endif
		}
		else if (argc == 3)
		{
		    /* This is just a duplication of "setlabel text" */
		    sl = DBPutFontLabel(editDef, &lab->lab_rect, lab->lab_font,
				lab->lab_size, lab->lab_rotate,
				&lab->lab_offset, lab->lab_just,
				cmd->tx_argv[argstart + 1],
				lab->lab_type, lab->lab_flags);
		    DBEraseLabelsByContent(editDef, &lab->lab_rect, -1,
				lab->lab_text);
		    DBWLabelChanged(editDef, sl, DBW_ALLWINDOWS);
		}
		break;
	    case PORT_CLASS:
		if (argc == 2)
		{
		    type = lab->lab_flags & PORT_CLASS_MASK;
		    for (idx = 0; cmdPortClassTypes[idx] != NULL; idx++)
			if (cmdClassToBitmask[idx] == type)
			{
#ifdef MAGIC_WRAPPER
			    Tcl_AppendResult(magicinterp, cmdPortClassTypes[idx],
					NULL);
#else
			    TxPrintf("Class = %s\n", cmdPortClassTypes[idx]);
#endif
			    break;
			}
		}
		else if (argc == 3)
		{
		    type = Lookup(cmd->tx_argv[argstart + 1], cmdPortClassTypes);
		    if (type < 0)
		    {
		        TxError("Usage:  port class <type>, where <type> is one of:\n");
			for (msg = &(cmdPortClassTypes[0]); *msg != NULL; msg++)
			{
                	    TxError("    %s\n", *msg);
			}
		    }
		    else
		    {
		        lab->lab_flags &= (~PORT_CLASS_MASK);
			lab->lab_flags |= (PORT_CLASS_MASK & cmdClassToBitmask[type]);
			editDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
		    }
		}
		else
		    goto portWrongNumArgs;
		break;
	    case PORT_USE:
		if (argc == 2)
		{
		    type = lab->lab_flags & PORT_USE_MASK;
		    for (idx = 0; cmdPortUseTypes[idx] != NULL; idx++)
			if (cmdUseToBitmask[idx] == type)
			{
#ifdef MAGIC_WRAPPER
			    Tcl_AppendResult(magicinterp, cmdPortUseTypes[idx],
					NULL);
#else
			    TxPrintf("Use = %s\n", cmdPortUseTypes[idx]);
#endif
			    break;
			}
		}
		else if (argc == 3)
		{
		    type = Lookup(cmd->tx_argv[argstart + 1], cmdPortUseTypes);
		    if (type < 0)
		    {
		        TxError("Usage:  port use <type>, where <type> is one of:\n");
			for (msg = &(cmdPortUseTypes[0]); *msg != NULL; msg++)
			{
                	    TxError("    %s\n", *msg);
			}
		    }
		    else
		    {
		        lab->lab_flags &= (~PORT_USE_MASK);
			lab->lab_flags |= (PORT_USE_MASK & cmdUseToBitmask[type]);
			editDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
		    }
		}
		else
		    goto portWrongNumArgs;
		break;

	    case PORT_INDEX:
		if (argc == 2)
		{
		    idx = lab->lab_flags & PORT_NUM_MASK;
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp, Tcl_NewIntObj(idx));
#else
		    TxPrintf("Index = %d\n", idx);
#endif
		}
		else if (argc == 3)
		{
		    if (!StrIsInt(cmd->tx_argv[argstart + 1]))
		        TxError("Usage:  port index <integer>\n");
		    else
		    {
		        lab->lab_flags &= (~PORT_NUM_MASK);
			lab->lab_flags |= atoi(cmd->tx_argv[argstart + 1]);
			editDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
		    }
		}
		else
		    goto portWrongNumArgs;
		break;

	    case PORT_CONNECT:
		if (argc == 2)
		{
		    char cdir[5];

		    cdir[0] = '\0';
		    pos = lab->lab_flags & PORT_DIR_MASK;
		    if (pos & PORT_DIR_NORTH) strcat(cdir, "n");
		    if (pos & PORT_DIR_EAST) strcat(cdir, "e");
		    if (pos & PORT_DIR_SOUTH) strcat(cdir, "s");
		    if (pos & PORT_DIR_WEST) strcat(cdir, "w");
#ifdef MAGIC_WRAPPER
		    Tcl_AppendResult(magicinterp, cdir, NULL);
#else
		    TxPrintf("Directions = %s\n", cdir);
#endif
		}
		else
		{
		    argstart++;
		    goto parsepositions;
		}
		break;

	    case PORT_REMOVE:
	        lab->lab_flags = 0;
		tmpArea = lab->lab_rect;
		lab->lab_rect = editBox;
		DBWLabelChanged(editDef, lab, DBW_ALLWINDOWS);
		lab->lab_rect = tmpArea;
		editDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
		break;

	    case PORT_MAKEALL:
		lab = editDef->cd_labels;
		// Fall through. . .

	    case PORT_MAKE:
		argstart++;
		goto parseindex;
		break;

	    case PORT_HELP:
portWrongNumArgs:
		TxError("Usage:  port [option], where [option] is one of:\n");
		for (msg = &(cmdPortOption[0]); *msg != NULL; msg++)
		{
                    TxError("    %s\n", *msg);
		}
		TxError("    <index> <directions>\n");
		break;
	}
	return;
    }
    
parseindex:

    while (1) {

	/* For this syntax, the label must not already be a port */
	if (lab->lab_flags & PORT_DIR_MASK)
	{
	    TxError("The selected label is already a port.\n");
	    TxError("Do \"port help\" to get a list of options.\n");
	    return;
	}

	if ((argc > argstart) && StrIsInt(cmd->tx_argv[argstart]))
	{
	    idx = atoi(cmd->tx_argv[argstart]);
	    for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
	    {
		if (sl == lab) continue;	/* don't consider self */
		if (sl->lab_flags & PORT_DIR_MASK)
		{
		    if ((sl->lab_flags & PORT_NUM_MASK) == idx)
		    {
			TxError("Port index %d is already used by port %s.\n"
				"Use command \"port index %d\" to force "
				"equivalence after defining the port.\n",
				idx, sl->lab_text, idx);
			return;
		    }
		}
	    }
	    argstart++;
	}
	else
	{
	    idx = 0;
	    for (sl = editDef->cd_labels; sl != NULL; sl = sl->lab_next)
	    {
		if (sl == lab) continue;	/* don't consider self */
		if (sl->lab_flags & PORT_DIR_MASK)
		{
		    // If there is another, identical label that is already
		    // declared a port, then use its index.
		    if (!strcmp(sl->lab_text, lab->lab_text))
		    {
			idx = (sl->lab_flags & PORT_NUM_MASK) - 1;
			break;
		    }
		    else if ((sl->lab_flags & PORT_NUM_MASK) > idx)
			idx = (sl->lab_flags & PORT_NUM_MASK); 
		}
	    }
	    idx++;
	}

	lab->lab_flags &= ~PORT_NUM_MASK;
	lab->lab_flags |= idx;

	/*
	 * Find positions.
	 */

parsepositions:

	dirmask = 0;
	if (argc == argstart)
	{
	    /* Get position from label */

	    pos = lab->lab_just;
            switch (pos)
	    {
		case GEO_NORTH:
		    dirmask = PORT_DIR_NORTH;
		    break;
		case GEO_SOUTH:
		    dirmask = PORT_DIR_SOUTH;
		    break;
		case GEO_EAST:
		    dirmask = PORT_DIR_EAST;
		    break;
		case GEO_WEST:
		    dirmask = PORT_DIR_WEST;
		    break;
		case GEO_NORTHEAST:
		    dirmask = PORT_DIR_NORTH | PORT_DIR_EAST;
		    break;
		case GEO_NORTHWEST:
		    dirmask = PORT_DIR_NORTH | PORT_DIR_WEST;
		    break;
		case GEO_SOUTHEAST:
		    dirmask = PORT_DIR_SOUTH | PORT_DIR_EAST;
		    break;
		case GEO_SOUTHWEST:
		    dirmask = PORT_DIR_SOUTH | PORT_DIR_WEST;
		    break;
		case GEO_CENTER:
		    dirmask = PORT_DIR_MASK;
		    break;
	    }
	}
	else
	{
	    /* Parse one or more positions */

	    for (i = argstart; i < argc; i++)
	    {
		pos = GeoNameToPos(cmd->tx_argv[i], TRUE, TRUE);
		if (pos < 0)
		    return;
        	pos = GeoTransPos(&RootToEditTransform, pos);
		switch (pos)
		{
		    case GEO_NORTH:
			dirmask |= PORT_DIR_NORTH;
			break;
		    case GEO_SOUTH:
			dirmask |= PORT_DIR_SOUTH;
			break;
		    case GEO_EAST:
			dirmask |= PORT_DIR_EAST;
			break;
		    case GEO_WEST:
			dirmask |= PORT_DIR_WEST;
			break;
		}
	    }
	}

	lab->lab_flags &= ~PORT_DIR_MASK;
	lab->lab_flags |= dirmask;

	tmpArea = lab->lab_rect;
	lab->lab_rect = editBox;
	DBWLabelChanged(editDef, lab, DBW_ALLWINDOWS);
	lab->lab_rect = tmpArea;

	if (option == PORT_MAKEALL)
	{
	    /* Get the next valid label, skipping any that are not	*/
	    /* inside the edit box or are already marked as ports.	*/

	    lab = lab->lab_next;
	    while ((lab != NULL) && (!GEO_OVERLAP(&editBox, &lab->lab_rect)
			|| (lab->lab_flags & PORT_DIR_MASK)))
		lab = lab->lab_next;

	    if (lab == NULL) break;
	}
	else
	    break;
    }
    editDef->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdDoProperty ---
 *
 * This is the core of the CmdProperty function, separated out so that
 * the function can operate on a specified CellDef in addition to operating
 * on the currently selected EditDef.  This allows us to add the option
 * "property" to the "cellname" command, to grab a property from a cell
 * without having to be actively editing it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified cd_prop record in the current edit cell's def.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdDoProperty(def, cmd, argstart)
    CellDef *def;
    TxCommand *cmd;
{
    int printPropertiesFunc();
    char *value;
    bool propfound;
    int locargc = cmd->tx_argc - argstart + 1;
    
    if (locargc == 1)
    {
	/* print all properties and their values */
	DBPropEnum(def, printPropertiesFunc);
    }
    else if (locargc == 2)
    {
	/* print the value of the indicated property */
	value = (char *)DBPropGet(def, cmd->tx_argv[argstart], &propfound);
	if (propfound)
#ifdef MAGIC_WRAPPER
	    Tcl_SetResult(magicinterp, value, NULL);
#else
	    TxPrintf("%s", value);
#endif
	else {
#ifdef MAGIC_WRAPPER
	    /* If the command was "cellname list property ...", then	*/
	    /* just return NULL if the property was not found.		*/
	    if (strcmp(cmd->tx_argv[1], "list"))
#endif
		TxError("Property name %s is not defined\n");
	}
    }
    else if (locargc == 3)
    {
	if (strlen(cmd->tx_argv[argstart + 1]) == 0)
	    DBPropPut(def, cmd->tx_argv[argstart], NULL);
	else
	{
	    value = StrDup((char **)NULL, cmd->tx_argv[argstart + 1]);
	    DBPropPut(def, cmd->tx_argv[argstart], value);
	}
	def->cd_flags |= (CDMODIFIED | CDGETNEWSTAMP);
    }    
    else
    {
	TxError("Usage: property [name] [value]\n");
	TxError("If value is more than one word, enclose in quotes\n");
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdProperty --
 *
 * Implement the "property" command.  Adds a property string to the
 * current edit cell, using the cd_props hash table and functions
 * defined in database/DBprop.c.
 *
 * Usage:
 *	property [name] [value]
 *
 * "name" is a unique string tag for the property, and "value" is its
 * string value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified cd_prop record in the current edit cell's def.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdProperty(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellDef *def;

    /* Get the root definition of the window	*/
    /* If window is NULL, pick up the edit def	*/

    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL)
    {
	def = EditCellUse->cu_def;
    }
    else
	def = ((CellUse *) w->w_surfaceID)->cu_def;

    CmdDoProperty(def, cmd, 1);
}

/*
 * ----------------------------------------------------------------------------
 * Callback function for printing a single property key:value pair
 * ----------------------------------------------------------------------------
 */

int
printPropertiesFunc(name, value)
    char *name;
    ClientData value;
{
#ifdef MAGIC_WRAPPER
    char *keyvalue;

    keyvalue = (char *)mallocMagic(strlen(name) + strlen((char *)value) + 2);
    sprintf(keyvalue, "%s %s", name, (char *)value);
    Tcl_AppendElement(magicinterp, keyvalue);
    freeMagic(keyvalue);
    
#else
    TxPrintf("%s = %s\n", name, value);
#endif

    return 0;	/* keep the search alive */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdNetlist --
 *
 * Usage:
 *	netlist option
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Performs the functions previously handled by the button
 *	handler mechanism, which has been replaced with a command-line
 *	command plus macro method.
 *
 * ----------------------------------------------------------------------------
 */

#define NLIST_HELP	0
#define NLIST_SELECT	1
#define NLIST_JOIN	2
#define NLIST_TERMINAL	3

void
CmdNetlist(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int option;
    char **msg, *lastargv;
    Point cursor;
    static char *cmdNetlistOption[] =
    {	
	"help           print this help information",
	"select		select the net nearest the cursor",
	"join           join current net and net containing terminal nearest the cursor",
	"terminal	toggle the terminal nearest the cursor into/out of current net",
	NULL
    };

    if (cmd->tx_argc < 2)
    {
	option = NLIST_HELP;
    }
    else
    {
	option = Lookup(cmd->tx_argv[1], cmdNetlistOption);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid netlist option.\n", cmd->tx_argv[1]);
	    option = NLIST_HELP;
	}
    }

    switch (option)
    {
	case NLIST_HELP:
	    TxPrintf("Netlist commands have the form \":netlist option\",");
	    TxPrintf(" where option is one of:\n");
	    for (msg = &(cmdNetlistOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    break;

	case NLIST_SELECT:
	    NMButtonLeft(w, cmd);
	    break;

	case NLIST_JOIN:
	    NMButtonMiddle(w, cmd);
	    break;

	case NLIST_TERMINAL:
	    NMButtonRight(w, cmd);
	    break;
    }
}
