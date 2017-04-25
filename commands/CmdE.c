/*
 * CmdE.c --
 *
 * Commands with names beginning with the letter E.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdE.c,v 1.4 2010/06/17 14:38:33 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/styles.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "utils/macros.h"
#include "drc/drc.h"
#include "textio/txcommands.h"
#include "extract/extract.h"
#include "select/select.h"


/*
 * ----------------------------------------------------------------------------
 *
 * CmdEdit --
 *
 * Implement the "edit" command.
 * Use the cell that is currently selected as the edit cell.  If more than
 * one cell is selected, use the point to choose between them.
 *
 * Usage:
 *	edit
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets EditCellUse.
 *
 * ----------------------------------------------------------------------------
 */

/* The following variable is set by cmdEditEnumFunc to signal that there
 * was at least one cell in the selection.
 */

static bool cmdFoundNewEdit;

void
CmdEdit(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect area, pointArea;
    CellUse *usave;
    CellDef *csave;
    int cmdEditRedisplayFunc();		/* Forward declaration. */
    int cmdEditEnumFunc();		/* Forward declaration. */
    bool noCurrentUse = FALSE;

    if (cmd->tx_argc > 1)
    {
	TxError("Usage: edit\nMaybe you want the \"load\" command\n");
	return;
    }
    usave = EditCellUse;
    if (usave != (CellUse *)NULL)
    {

        /* Record the current edit cell's area for redisplay (now that it's
         * not the edit cell, it will be displayed differently).  Do this
         * only in windows where the edit cell is displayed differently from
         * other cells.
         */
    
        GeoTransRect(&EditToRootTransform, &(usave->cu_def->cd_bbox), &area);
        (void) WindSearch(DBWclientID, (ClientData) NULL,
	    	(Rect *) NULL, cmdEditRedisplayFunc, (ClientData) &area);

	DBWUndoOldEdit(EditCellUse, EditRootDef, &EditToRootTransform,
			&RootToEditTransform);
    }
	
    /* Use the position of the point to select one of the currently-selected
     * cells (if there are more than one).  If worst comes to worst, just
     * select any selected cell.
     */
    
    (void) ToolGetPoint((Point *) NULL, &pointArea);

    cmdFoundNewEdit = FALSE;
    csave = EditRootDef;
    usave = EditCellUse;
    EditCellUse = NULL;

    (void) SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL,
	    cmdEditEnumFunc, (ClientData) &pointArea);

    if (EditCellUse == (CellUse *)NULL)
    {
	TxError("No cell selected to edit.\n");
	EditCellUse = usave;
	EditRootDef = csave;
	return;
    }
    else if (!(EditCellUse->cu_def->cd_flags & CDAVAILABLE))
	DBCellRead(EditCellUse->cu_def, (char *)NULL, TRUE, NULL);

    if (EditCellUse->cu_def->cd_flags & CDNOEDIT)
    {
	TxError("File %s is not writeable.  Edit not set.\n",
		EditCellUse->cu_def->cd_file);
	cmdFoundNewEdit = FALSE;
	EditCellUse = usave;
	EditRootDef = csave;
	return;
    }

    if (!cmdFoundNewEdit)
	TxError("You haven't selected a new cell to edit.\n");

    CmdSetWindCaption(EditCellUse, EditRootDef);
    DBWUndoNewEdit(EditCellUse, EditRootDef,
		&EditToRootTransform, &RootToEditTransform);

    /* Now record the new edit cell's area for redisplay. */

    GeoTransRect(&EditToRootTransform, &(EditCellUse->cu_def->cd_bbox), &area);
    (void) WindSearch(DBWclientID, (ClientData) NULL,
	    (Rect *) NULL, cmdEditRedisplayFunc, (ClientData) &area);
}

/* Search function to handle redisplays for CmdEdit:  it checks to
 * be sure that this is a window on the edit cell, then if edit cells
 * are displayed differently from other cells in the window, the area
 * of the edit cell is redisplayed.  Also, if the grid is on in this
 * window, the origin area is redisplayed.
 */

int
cmdEditRedisplayFunc(w, area)
    MagWindow *w;			/* Window containing edit cell. */
    Rect *area;			/* Area to be redisplayed. */
{
    static Rect origin = {-1, -1, 1, 1};
    Rect tmp;
    DBWclientRec *crec = (DBWclientRec *) w->w_clientData;

    if (((CellUse *) w->w_surfaceID)->cu_def != EditRootDef) return 0;
    if (!(crec->dbw_flags & DBW_ALLSAME))
	DBWAreaChanged(EditRootDef, area, crec->dbw_bitmask,
	    &DBAllButSpaceBits);
    if (crec->dbw_flags & DBW_GRID)
    {
	GeoTransRect(&EditToRootTransform, &origin, &tmp);
	DBWAreaChanged(EditRootDef, &tmp, crec->dbw_bitmask,
	    &DBAllButSpaceBits);
    }
    return 0;
}

/* Search function to find the new edit cell:  look for a cell use
 * that contains the rectangle passed as argument.  If we find such
 * a use, return 1 to abort the search.  Otherwise, save information
 * about this use anyway:  it'll become the edit cell if nothing
 * better is found.
 */

int
cmdEditEnumFunc(selUse, use, transform, area)
    CellUse *selUse;		/* Use from selection (not used). */
    CellUse *use;		/* Use from layout that corresponds to
				 * selUse (could be an array!).
				 */
    Transform *transform;	/* Transform from use->cu_def to root coords. */
    Rect *area;			/* We're looking for a use containing this
				 * area, in root coords.
				 */
{
    Rect defArea, useArea;
    int xhi, xlo, yhi, ylo;

    /* Save this use as the default next edit cell, regardless of whether
     * or not it overlaps the area we're interested in.
     */

    EditToRootTransform = *transform;
    GeoInvertTrans(transform, &RootToEditTransform);
    EditCellUse = use;
    EditRootDef = SelectRootDef;
    cmdFoundNewEdit = TRUE;

    /* See if the bounding box of this use overlaps the area we're
     * interested in.
     */

    GeoTransRect(&RootToEditTransform, area, &defArea);
    GeoTransRect(&use->cu_transform, &defArea, &useArea);
    if (!GEO_OVERLAP(&useArea, &use->cu_bbox)) return 0;

    /* It overlaps.  Now find out which array element it points to,
     * and adjust the transforms accordingly.
     */
    
    DBArrayOverlap(use, &useArea, &xlo, &xhi, &ylo, &yhi);
    GeoTransTrans(DBGetArrayTransform(use, xlo, ylo), transform,
	    &EditToRootTransform);
    GeoInvertTrans(&EditToRootTransform, &RootToEditTransform);
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdElement --
 *
 * Implement the "element" command.
 * Handle general-purpose elements which are drawn on top of the layout.
 *
 * Usage:
 *	element name <add|delete|configure|cget> <line|rectangle|text> ...
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify the hash table of elements.
 *
 * ----------------------------------------------------------------------------
 */

#define ELEMENT_ADD	0
#define ELEMENT_DELETE	1
#define ELEMENT_CONFIG	2
#define ELEMENT_NAMES	3
#define ELEMENT_INBOX	4
#define ELEMENT_HELP	5

#define ELEMENT_LINE	0
#define ELEMENT_RECT	1
#define ELEMENT_TEXT	2

#define OPTION_TEXT	0
#define OPTION_STYLE	1
#define OPTION_POSITION	2
#define OPTION_FLAGS	3

void
CmdElement(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{

    int option, type;
    char **msg;
    Rect area;
    int style;
    CellDef *def;
    CellUse *use;
    bool getopt;

    static char *cmdElementOption[] = {
	"add			create a new element",
	"delete		delete an existing element",
	"configure		configure or query an existing element",
	"names		print names of all elements",
	"inbox		print name of element nearest the box",
	"help		print help information",
	NULL
    };

    static char *cmdElementType[] = {
	"line		name style x1 y1 x2 y2",
	"rectangle		name style llx lly urx ury",
	"text		name style cx cy label",
	NULL
    };

    static char *cmdConfigureType[] = {
	"text		get (or) replace <string>",
	"style		get (or) add <style> (or) remove <style>",
	"position		get (or) <point> (or) <rect>",
	"flags		set element flags",
	NULL
    };

    option = ELEMENT_HELP;
    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL) return;

    use = (CellUse *)w->w_surfaceID;
    if (use == NULL) return;
    def = use->cu_def;
    if (def == NULL) return;

    if (cmd->tx_argc > 1)
    {
	option = Lookup(cmd->tx_argv[1], cmdElementOption);
	if (option < 0) option = ELEMENT_HELP;
    }

    switch (option)
    {
	case ELEMENT_ADD:
	    if (cmd->tx_argc < 3)
	    {
		TxError("Usage:  element add <type> <name> <values...>\n");
		return;
	    }
	    type = Lookup(cmd->tx_argv[2], cmdElementType);
	    if (type < 0)
	    {
		TxError("Usage:  element add <type>..., where type is one of:\n");
		for (msg = &(cmdElementType[0]); *msg != NULL; msg++)
		    TxError("    %s\n", *msg);

		return;
	    }
	    else if (cmd->tx_argc >= 7)
	    {
		if (StrIsInt(cmd->tx_argv[4]))
		    style = atoi(cmd->tx_argv[4]);
		else
		{
		    style = GrGetStyleFromName(cmd->tx_argv[4]);
		    if (style < 0)
		    {
			TxError("Unknown style \"%s\".", cmd->tx_argv[4]);
			TxError("Use a number or one of the long names in the"
				" .dstyle file\n");
			return;
		    }
		}
		if (!StrIsInt(cmd->tx_argv[5])) goto badusage;
		area.r_xbot = atoi(cmd->tx_argv[5]);
		if (!StrIsInt(cmd->tx_argv[6])) goto badusage;
		area.r_ybot = atoi(cmd->tx_argv[6]);

		switch (type)
		{
		    case ELEMENT_LINE:
			if (cmd->tx_argc != 9)
			{
			    TxError("Usage:  element add %s\n",
					cmdElementType[ELEMENT_LINE]);
			    return;
			}
			if (!StrIsInt(cmd->tx_argv[7])) goto badusage;
			area.r_xtop = atoi(cmd->tx_argv[7]);
			if (!StrIsInt(cmd->tx_argv[8])) goto badusage;
			area.r_ytop = atoi(cmd->tx_argv[8]);
			DBWElementAddLine(w, cmd->tx_argv[3], &area, def, style);
			break;
		    case ELEMENT_RECT:
			if (cmd->tx_argc != 9)
			{
			    TxError("Usage:  element add %s\n",
					cmdElementType[ELEMENT_RECT]);
			    return;
			}
			if (!StrIsInt(cmd->tx_argv[7])) goto badusage;
			area.r_xtop = atoi(cmd->tx_argv[7]);
			if (!StrIsInt(cmd->tx_argv[8])) goto badusage;
			area.r_ytop = atoi(cmd->tx_argv[8]);
			DBWElementAddRect(w, cmd->tx_argv[3], &area, def, style);
			break;
		    case ELEMENT_TEXT:
			if (cmd->tx_argc != 8)
			{
			    TxError("Usage:  element add %s\n",
					cmdElementType[ELEMENT_TEXT]);
			    return;
			}
			DBWElementAddText(w, cmd->tx_argv[3], area.r_xbot,
				area.r_ybot, cmd->tx_argv[7], def, style);
			break;
		}
	    }
	    else
	    {
		TxError("Usage:  element add %s\n", cmdElementType[type]);
		return;
	    }
	    break;
	case ELEMENT_DELETE:
	    if (cmd->tx_argc != 3)
	    {
		TxPrintf("Usage:  element delete <name>\n");
		return;
	    }
	    DBWElementDelete(w, cmd->tx_argv[2]);
	    break;
	case ELEMENT_CONFIG:
	    if (cmd->tx_argc < 4)
	    {
		TxError("Usage:  element configure <name> <option...>\n");
		return;
	    }
	    type = Lookup(cmd->tx_argv[3], cmdConfigureType);
	    if ((type < 0) || (cmd->tx_argc < 4))
	    {
		TxError("Usage:  element configure <name> <option...>, "
				"where option is one of:\n");
		for (msg = &(cmdConfigureType[0]); *msg != NULL; msg++)
		{
		    TxError("    %s\n", *msg);
		}
		return;
	    }

	    if (cmd->tx_argc == 4)
		getopt = TRUE;
	    else
		getopt = !strncmp(cmd->tx_argv[4], "get", 3);

	    switch (type)
	    {
		case OPTION_TEXT:
		    if (getopt)
			DBWElementText(w, cmd->tx_argv[2], NULL);
		    else
		    {
			if (cmd->tx_argc == 5)
			    DBWElementText(w, cmd->tx_argv[2], cmd->tx_argv[4]);
			else if (cmd->tx_argc != 6)
			{
			    TxError("Usage:  element configure <name> text "
					"replace <string>\n");
			    return;
			}
			else
			{
			    DBWElementText(w, cmd->tx_argv[2], cmd->tx_argv[4]);
			}
		    }
		    break;
		case OPTION_STYLE:
		    if (getopt)
			DBWElementStyle(w, cmd->tx_argv[2], -1, 0);
		    else if (cmd->tx_argc != 6)
		    {
			TxError("Usage: element configure <name> style add|remove "
					" <style>\n");
			return;
		    }
		    else
		    {
			if (StrIsInt(cmd->tx_argv[5]))
			    style = atoi(cmd->tx_argv[5]);
			else
			{
			    style = GrGetStyleFromName(cmd->tx_argv[5]);
		    	    if (style < 0)
		    	    {
				TxError("Unknown style \"%s\".", cmd->tx_argv[5]);
				TxError("Use a number or one of the long names "
						"in the .dstyle file\n");
				return;
			    }
			}
			if (!strncmp(cmd->tx_argv[4], "add", 3))
			    DBWElementStyle(w, cmd->tx_argv[2], style, TRUE);
			else
			    DBWElementStyle(w, cmd->tx_argv[2], style, FALSE);
		    }
		    break;
		case OPTION_POSITION:
		    if (getopt)
		    {
			DBWElementPos(w, cmd->tx_argv[2], NULL);
		    }
		    else
		    {
			Rect crect;

			if (cmd->tx_argc >= 6)
			{
			    if (!StrIsInt(cmd->tx_argv[4]) ||
			    		!StrIsInt(cmd->tx_argv[5]))
				goto badrect;
			    crect.r_xbot = atoi(cmd->tx_argv[4]);
			    crect.r_ybot = atoi(cmd->tx_argv[5]);
			    crect.r_xtop = crect.r_xbot; /* placeholder */
			    crect.r_ytop = crect.r_ybot; /* placeholder */
			}
			if (cmd->tx_argc == 8)
			{
			    if (!StrIsInt(cmd->tx_argv[6]) ||
			    		!StrIsInt(cmd->tx_argv[7]))
				goto badrect;
			    crect.r_xtop = atoi(cmd->tx_argv[6]);
			    crect.r_ytop = atoi(cmd->tx_argv[7]);
			}

			if (cmd->tx_argc == 6 || cmd->tx_argc == 8)
			    DBWElementPos(w, cmd->tx_argv[2], &crect);
			else
			{
badrect:
			    TxError("Usage: element configure <name> position "
					"<x> <y> [<x2> <y2>]\n");
			    return;
			}
		    }
		    break;
		case OPTION_FLAGS:
		    if (getopt)
		    {
			TxError("(unimplemented function)\n");
		    }
		    else
		    {
			if (cmd->tx_argc >= 5)
			    DBWElementParseFlags(w, cmd->tx_argv[2], cmd->tx_argv[4]);
			else
			{
			    TxError("Usage: element configure <name> flags "
					"(flag)\n");
			    return;
			}
		    }
		    break;
	    }
	    break;
	case ELEMENT_NAMES:
	    DBWElementNames();
	    break;
	case ELEMENT_INBOX:
	    if (!ToolGetBox((CellDef **)NULL, &area))
	    {
		TxError("Box tool must be present\n");
		return;
	    }
	    DBWElementInbox(&area);
	    break;
	case ELEMENT_HELP:
badusage:
            for (msg = &(cmdElementOption[0]); *msg != NULL; msg++)
            {
                TxPrintf("    %s\n", *msg);
            }
	    break;
    }
}



/*
 * ----------------------------------------------------------------------------
 *
 * CmdErase --
 *
 * Implement the "erase" command.
 * Erase paint in the specified layers from underneath the box in
 * EditCellUse->cu_def.
 *
 * Usage:
 *	erase [layers | cursor]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modified EditCellUse->cu_def.
 *
 * ----------------------------------------------------------------------------
 */

/* The following information is used to keep track of cells to be
 * erased.  It is needed because we can't delete cells while searching
 * for cells, or the database gets screwed up.
 */

#define MAXCELLS 100
static CellUse *cmdEraseCells[MAXCELLS];
static int cmdEraseCount;

void
CmdErase(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect editRect, areaReturn;
    TileTypeBitMask mask;
    extern int cmdEraseCellsFunc();

    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL) return;

    if (cmd->tx_argc > 2)
    {
	TxError("Usage: %s [<layers> | cursor]\n", cmd->tx_argv[0]);
	return;
    }

    if (!ToolGetEditBox(&editRect)) return;

    /*
     * Erase with no arguments is the same as erasing
     * everything underneath the box tool (ie, painting space)
     * Labels are automatically affected; subcells are not.
     */

    if (cmd->tx_argc == 1)
	(void) CmdParseLayers("*,label", &mask);
    else if (!strncmp(cmd->tx_argv[1], "cursor", 6))
    {
	CmdPaintEraseButton(w, &cmd->tx_p, FALSE);
	return;
    }
    else if (!CmdParseLayers(cmd->tx_argv[1], &mask))
	return;

    if (TTMaskEqual(&mask, &DBSpaceBits))
	(void) CmdParseLayers("*,label", &mask);
    TTMaskClearType(&mask, TT_SPACE);
    if (TTMaskIsZero(&mask))
	return;

    TTMaskAndMask(&mask, &DBActiveLayerBits);

    /* Erase paint. */
    DBEraseValid(EditCellUse->cu_def, &editRect, &mask, 0);

    /* Erase labels. */
    areaReturn = editRect;
    DBEraseLabel(EditCellUse->cu_def, &editRect, &mask, &areaReturn);

    /* Erase subcells. */
    if (TTMaskHasType(&mask, L_CELL))
    {
	SearchContext scx;
	int i;

	/* To erase cells, we make a series of passes.  In each
	 * pass, collect a whole bunch of cells that are in the
	 * area of interest, then erase all those cells.  Continue
	 * this until all cells have been erased.
	 */
	
	scx.scx_use = EditCellUse;
	scx.scx_x = scx.scx_y = 0;
	scx.scx_area = editRect;
	scx.scx_trans = GeoIdentityTransform;
	while (TRUE)
	{
	    cmdEraseCount = 0;
	    (void) DBCellSrArea(&scx, cmdEraseCellsFunc, (ClientData) NULL);
	    for (i=0; i<cmdEraseCount; i++)
	    {
		DRCCheckThis(EditCellUse->cu_def, TT_CHECKSUBCELL,
		    &(cmdEraseCells[i]->cu_bbox));
		DBWAreaChanged(EditCellUse->cu_def,
		    &(cmdEraseCells[i]->cu_bbox), DBW_ALLWINDOWS,
		    (TileTypeBitMask *) NULL);
		DBUnLinkCell(cmdEraseCells[i], EditCellUse->cu_def);
		DBDeleteCell(cmdEraseCells[i]);
		(void) DBCellDeleteUse(cmdEraseCells[i]);
	    }
	    if (cmdEraseCount < MAXCELLS) break;
	}
    }
    DBAdjustLabels(EditCellUse->cu_def, &editRect);

    /* Don't run expensive DRC when only labels were erased */
    TTMaskClearType(&mask, L_LABEL);
    if (!TTMaskIsZero(&mask))
	DRCCheckThis (EditCellUse->cu_def, TT_CHECKPAINT, &editRect);
    TTMaskClearType(&mask, L_CELL);
    SelectClear();
    DBWAreaChanged(EditCellUse->cu_def, &areaReturn, DBW_ALLWINDOWS, &mask);
    DBReComputeBbox(EditCellUse->cu_def);
}

int
cmdEraseCellsFunc(scx, cdarg)
SearchContext *scx;		/* Indicates cell found. */
ClientData cdarg;		/* Not used. */
{
    /* All this procedure does is to remember cells that are
     * found, up to MAXCELLS of them.
     */
    
    if (cmdEraseCount >= MAXCELLS) return 1;
    cmdEraseCells[cmdEraseCount] = scx->scx_use;
    cmdEraseCount += 1;
    return 2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdExpand --
 *
 * Implement the "expand" command.
 *
 * Usage:
 *	expand
 *	expand toggle
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If "toggle" is specified, flips the expanded/unexpanded status
 *	of all selected cells.  Otherwise, aren't any unexpanded cells
 *	left under the box.  May read cells in from disk, and updates
 *	bounding boxes that have changed.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdExpand(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int windowMask, boxMask, d;
    Rect rootRect;
    CellUse *rootBoxUse;
    CellDef *rootBoxDef;
    int cmdExpandFunc();		/* Forward reference. */

    if (cmd->tx_argc > 2 || (cmd->tx_argc == 2 
	&& (strncmp(cmd->tx_argv[1], "toggle", strlen(cmd->tx_argv[1])) != 0)))
    {
	TxError("Usage: %s or %s toggle\n", cmd->tx_argv[0], cmd->tx_argv[0]);
	return;
    }

    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL)
    {
	TxError("Point to a window first.\n");
	return;
    }
    windowMask = ((DBWclientRec *) w->w_clientData)->dbw_bitmask;
    d = DBLambda[1];
    rootBoxUse = (CellUse *)w->w_surfaceID;
    rootBoxDef = rootBoxUse->cu_def;

    /* Check for possible rescaling during the expansion, and adjust    */
    /* the cursor box and window to restore the original view.          */

    d = DBLambda[1];
    do 
    {
	if (d != DBLambda[1])
	{
            d = DBLambda[1] / d;
            DBScalePoint(&rootRect.r_ll, d, 1);
            DBScalePoint(&rootRect.r_ur, d, 1);
            ToolMoveBox(TOOL_BL, &rootRect.r_ll, FALSE, rootBoxDef);
            ToolMoveCorner(TOOL_TR, &rootRect.r_ur, FALSE, rootBoxDef);

            /* Adjust all window viewing scales and positions and redraw */

            WindScale(d, 1);
	    TxPrintf("expand: rescaled by %d\n", d);
	    d = DBLambda[1];
	    if (cmd->tx_argc == 2) break;	/* Don't toggle twice */
	}
	(void) ToolGetBoxWindow(&rootRect, &boxMask);

	if (cmd->tx_argc == 2)
	    SelectExpand(windowMask);
	else
	{
	    if ((boxMask & windowMask) != windowMask)
	    {
		TxError("The box isn't in the same window as the cursor.\n");
		return;
	    }
	    DBExpandAll(rootBoxUse, &rootRect, windowMask,
			TRUE, cmdExpandFunc, (ClientData)(pointertype) windowMask);
	}
    } while (d != DBLambda[1]);
}

/* This function is called for each cell whose expansion status changed.
 * It forces the cells area to be redisplayed, then returns 0 to keep
 * looking for more cells to expand.
 */

int
cmdExpandFunc(use, windowMask)
    CellUse *use;		/* Use that was just expanded. */
    int windowMask;		/* Window where it was expanded. */
{
    if (use->cu_parent == NULL) return 0;
    DBWAreaChanged(use->cu_parent, &use->cu_bbox, windowMask,
	    &DBAllButSpaceBits);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdExtract --
 *
 * Implement the "extract" command.
 *
 * Usage:
 *	extract option args
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	There are no side effects on the circuit.  Various options
 *	may produce .ext files or change extraction parameters.
 *
 * ----------------------------------------------------------------------------
 */
#ifndef NO_EXT

#define EXTINCREMENTAL -1
#define	EXTALL		0
#define EXTCELL		1
#define	EXTDO		2
#define EXTHELP		3
#define	EXTLENGTH	4
#define	EXTNO		5
#define	EXTPARENTS	6
#define	EXTSHOWPARENTS	7
#define	EXTSTYLE	8
#define	EXTUNIQUE	9
#define	EXTWARN		10

#define	WARNALL		0
#define WARNDUP		1
#define	WARNFETS	2
#define WARNLABELS	3

#define	DOADJUST	0
#define	DOALL		1
#define	DOCAPACITANCE	2
#define	DOCOUPLING	3
#define	DOLENGTH	4
#define	DORESISTANCE	5

#define	LENCLEAR	0
#define	LENDRIVER	1
#define	LENRECEIVER	2

#define UNIQALL		0
#define UNIQTAGGED	1
#define UNIQNOPORTS	2

void
CmdExtract(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char **msg, *namep, *arg;
    int option, warn, len, n, all;
    bool no;
    CellUse *selectedUse;
    CellDef	*selectedDef;
    bool dolist = FALSE;
    bool doforall = FALSE;
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv;

    static char *cmdExtWarn[] =
    {
	"all			enable all warnings",
	"dup			warn when different nodes have the same name",
	"fets			warn about badly constructed fets",
	"labels		warn when subcell nodes are unlabelled",
	NULL
    };
    static char *cmdExtOption[] =
    {
	"adjust			compensate R and C hierarchically",
	"all			all options",
	"capacitance		extract substrate capacitance",
	"coupling		extract coupling capacitance",
	"length			compute driver-receiver pathlengths",
	"resistance		estimate resistance",
	NULL
    };
    static char *cmdExtLength[] =
    {
	"clear			clear the driver and receiver tables",
	"driver termName(s)	identify a driving (output) terminal",
	"receiver termName(s)	identify a receiving (input) terminal",
	NULL
    };
    static char *cmdExtUniq[] =
    {
	"all			extract matching labels as unique nodes",
	"#			extract tagged labels as unique nodes",
	"noports		ignore ports when making labels unique",
	NULL
    };
    static char *cmdExtCmd[] =
    {	
	"all			extract root cell and all its children",
	"cell name		extract selected cell into file \"name\"",
	"do [option]		enable extractor option",
	"help			print this help information",
	"length [option]	control pathlength extraction information",
	"no [option]		disable extractor option",
	"parents		extract selected cell and all its parents",
	"showparents		show all parents of selected cell",
	"style [stylename]	set current extraction parameter style",
	"unique [option]	generate unique names when different nodes\n\
			have the same name",
	"warn [ [no] option]	enable/disable reporting of non-fatal errors",
	NULL
    };

    if (argc > 1)
    {
	if (!strncmp(argv[1], "list", 4))
	{
	    dolist = TRUE;
	    if (!strncmp(argv[1], "listall", 7))
	    {
		doforall = TRUE;
	    }
	    argv++;
	    argc--;
	}
	option = Lookup(argv[1], cmdExtCmd);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid extract option.", argv[1]);
	    TxError("  Type \"extract help\" for a list of valid options.\n");
	    return;
        }
    }
    else option = EXTINCREMENTAL;

    /* Only check for a window on options requiring one */

    if ((option != EXTSTYLE) && (option != EXTHELP))
    {
	windCheckOnlyWindow(&w, DBWclientID);
	if (w == (MagWindow *) NULL)
	{
	    if (ToolGetBox(&selectedDef,NULL) == FALSE)
	    {
		TxError("Point to a window first\n");
		return;
	    }
	    selectedUse = selectedDef->cd_parents;
	}
	else
	{
    	    selectedUse = (CellUse *)w->w_surfaceID;
	}

	if (argc == 1)
	{
	    ExtIncremental(selectedUse);
	    return;
	}
    }

    switch (option)
    {
	case EXTUNIQUE:
	    if (argc < 3)
		all = UNIQALL;
	    else
	    {
		arg = argv[2];
		all = Lookup(arg, cmdExtUniq);
	    }
	    if (all < 0)
	    {
		TxError("Usage: extract unique [option]\n");
		TxPrintf("where option is one of:\n");
		for (msg = &(cmdExtUniq[0]); *msg != NULL; msg++)
		{
		    if (**msg == '*') continue;
		    TxPrintf("  %s\n", *msg);
		}
		TxPrintf("No option is equivalent to option \"all\"\n");
		return;
	    }
	    ExtUnique(selectedUse, all);
	    break;

	case EXTALL:
	    ExtAll(selectedUse);
	    return;

	case EXTCELL:
	    if (argc != 3) goto wrongNumArgs;
	    namep = argv[2];
	    selectedUse = CmdGetSelectedCell((Transform *) NULL);
	    if (selectedUse == NULL)
	    {
		TxError("No cell selected\n");
		return;
	    }
	    ExtCell(selectedUse->cu_def, namep, FALSE);
	    return;

	case EXTPARENTS:
	    selectedUse = CmdGetSelectedCell((Transform *) NULL);
	    if (selectedUse == NULL)
	    {
		TxError("No cell selected\n");
		return;
	    }
	    ExtParents(selectedUse);
	    return;

	case EXTSHOWPARENTS:
	    selectedUse = CmdGetSelectedCell((Transform *) NULL);
	    if (selectedUse == NULL)
	    {
		TxError("No cell selected\n");
		return;
	    }
	    ExtShowParents(selectedUse);
	    return;

	case EXTSTYLE:
	    if (argc == 2)
		ExtPrintStyle(dolist, doforall, !doforall);
	    else
		ExtSetStyle(argv[2]);
	    return;

	case EXTHELP:
	    if (argc != 2)
	    {
		wrongNumArgs:
		TxError("Wrong number of arguments in \"extract\" command.");
		TxError("  Try \"extract help\" for help.\n");
		return;
	    }
	    TxPrintf("Extract commands have the form \"extract option\",\n");
	    TxPrintf("where option is one of:\n");
	    for (msg = &(cmdExtCmd[0]); *msg != NULL; msg++)
	    {
		if (**msg == '*') continue;
		TxPrintf("  %s\n", *msg);
	    }
	    TxPrintf("If no option is given, the root cell and all its\n");
	    TxPrintf("children are extracted incrementally.\n");
	    return;

#define	WARNSET(f)	((ExtDoWarn & (f)) ? "do" : "no")
	case EXTWARN:
	    if (argc < 3)
	    {
		TxPrintf("The following extractor warnings are enabled:\n");
		TxPrintf("    %s dup\n", WARNSET(EXTWARN_DUP));
		TxPrintf("    %s fets\n", WARNSET(EXTWARN_FETS));
		TxPrintf("    %s labels\n", WARNSET(EXTWARN_LABELS));
		return;
	    }
#undef	WARNSET

	    no = FALSE;
	    arg = argv[2];
	    if (argc > 3 && strcmp(arg, "no") == 0)
		no = TRUE, arg = argv[3];

	    option = Lookup(arg, cmdExtWarn);
	    if (option < 0)
	    {
		TxError("Usage: extract warnings [no] option\n");
		TxPrintf("where option is one of:\n");
		for (msg = &(cmdExtWarn[0]); *msg != NULL; msg++)
		{
		    if (**msg == '*') continue;
		    TxPrintf("  %s\n", *msg);
		}
		return;
	    }
	    switch (option)
	    {
		case WARNALL:		warn = EXTWARN_ALL; break;
		case WARNDUP:		warn = EXTWARN_DUP; break;
		case WARNFETS:		warn = EXTWARN_FETS; break;
		case WARNLABELS:	warn = EXTWARN_LABELS; break;
	    }
	    if (no) ExtDoWarn &= ~warn;
	    else ExtDoWarn |= warn;
	    return;
	case EXTDO:
	case EXTNO:
#define	OPTSET(f)	((ExtOptions & (f)) ? "do" : "no")
	    if (argc < 3)
	    {
		TxPrintf("The following are the extractor option settings:\n");
		TxPrintf("%s adjust\n", OPTSET(EXT_DOADJUST));
		TxPrintf("%s capacitance\n", OPTSET(EXT_DOCAPACITANCE));
		TxPrintf("%s coupling\n", OPTSET(EXT_DOCOUPLING));
		TxPrintf("%s length\n", OPTSET(EXT_DOLENGTH));
		TxPrintf("%s resistance\n", OPTSET(EXT_DORESISTANCE));
		return;
#undef	OPTSET
	    }

	    no = (option == EXTNO);
	    arg = argv[2];
	    option = Lookup(arg, cmdExtOption);
	    if (option < 0)
	    {
		TxError("Usage: extract do option\n");
		TxError("   or  extract no option\n");
		TxPrintf("where option is one of:\n");
		for (msg = &(cmdExtOption[0]); *msg != NULL; msg++)
		{
		    if (**msg == '*') continue;
		    TxPrintf("  %s\n", *msg);
		}
		return;
	    }
	    switch (option)
	    {
		case DOADJUST:		option = EXT_DOADJUST; break;
		case DOALL:		option = EXT_DOALL; break;
		case DOCAPACITANCE:	option = EXT_DOCAPACITANCE; break;
		case DOCOUPLING:	option = EXT_DOCOUPLING; break;
		case DOLENGTH:		option = EXT_DOLENGTH; break;
		case DORESISTANCE:	option = EXT_DORESISTANCE; break;
	    }
	    if (no) ExtOptions &= ~option;
	    else ExtOptions |= option;
	    return;
	case EXTLENGTH:
	    if (argc < 3)
		goto lenUsage;
	    arg = argv[2];
	    len = Lookup(arg, cmdExtLength);
	    if (len < 0)
	    {
lenUsage:
		TxError("Usage: extract length option [args]\n");
		TxPrintf("where option is one of:\n");
		for (msg = &(cmdExtLength[0]); *msg != NULL; msg++)
		{
		    if (**msg == '*') continue;
		    TxPrintf("  %s\n", *msg);
		}
		return;
	    }
	    switch (len)
	    {
		case LENCLEAR:
		    ExtLengthClear();
		    break;
		case LENDRIVER:
		    if (argc < 4)
		    {
driverUsage:
			TxError("Must specify one or more terminal names\n");
			return;
		    }
		    for (n = 3; n < argc; n++)
			ExtSetDriver(argv[n]);
		    break;
		case LENRECEIVER:
		    if (argc < 4)
			goto driverUsage;
		    for (n = 3; n < argc; n++)
			ExtSetReceiver(argv[n]);
		    break;
	    }
	    return;
    }

    /*NOTREACHED*/
}
#endif
