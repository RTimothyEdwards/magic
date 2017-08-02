/*
 * CmdFI.c --
 *
 * Commands with names beginning with the letters F through I.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdFI.c,v 1.4 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_READLINE
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#include "readline/readline/readline.h"
#include "readline/readline/history.h"
#endif
#endif

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "utils/undo.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "utils/macros.h"
#include "drc/drc.h"
#include "textio/txcommands.h"
#include "utils/styles.h"
#include "graphics/graphics.h"
#include "extract/extract.h"
#include "utils/malloc.h"
#include "select/select.h"
#include "sim/sim.h"
#include "gcr/gcr.h"

/* The following structure is used by CmdFill to keep track of
 * areas to be filled.
 */

struct cmdFillArea
{
    Rect cfa_area;			/* Area to fill. */
    TileType cfa_type;			/* Type of material. */
    struct cmdFillArea *cfa_next;	/* Next in list of areas to fill. */
};

/* The following structure passes arguments needed by the	  */
/* DBWFeedbackAdd command to the feedPolyFunc() callback routine. */

struct cmdFPArg
{
   CellDef *def;
   int style;
   char *text;
};

#define FEEDMAGNIFY 20	/* Allow feedback to be drawn to 1/20 of an 	*/
			/* internal unit.				*/

/*
 * ----------------------------------------------------------------------------
 *
 * feedPolyFunc --
 *
 *	This procedure generates feedback entries in a polygonal area
 *	by calling DBWFeedbackArea per tile on simple 1-bit types in
 *	a plane.  The use of the temporary plane allows us to use the
 *	same path decomposition routine used for CIF input, polygon
 *	drawing, and wire segments.
 *
 * ----------------------------------------------------------------------------
 */

int
feedPolyFunc(tile, arg)
    Tile *tile;
    struct cmdFPArg *arg;
{
    Rect area;
    TiToRect(tile, &area);

    DBWFeedbackAdd(&area, arg->text, arg->def, FEEDMAGNIFY,
        arg->style |
                (TiGetTypeExact(tile) & (TT_DIAGONAL | TT_DIRECTION | TT_SIDE)));
        /* (preserve information about the geometry of a diagonal tile) */
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdFeedback --
 *
 * 	Implement the "feedback" command, which provides facilities
 *	for querying and manipulating feedback information provided
 *	by other commands when they have troubles or want to highlight
 *	certain things.
 *
 * Usage:
 *	feedback option [additional_args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the option.
 *
 * ----------------------------------------------------------------------------
 */

#undef	CLEAR
#define ADD		0
#define CLEAR		1
#define COUNT		2
#define FIND		3
#define FEED_HELP	4
#define SAVE		5
#define WHY		6

void
CmdFeedback(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    static char *cmdFeedbackOptions[] =
    {
	"add text [style] [points...]	create new feedback area over box",
	"clear [substring]		clear all or selected feedback info",
	"count				count # feedback entries",
	"find [nth]			put box over next [or nth] entry",
	"help				print this message",
	"save file			save feedback areas in file",
	"why				print all feedback messages under box",
	NULL
    };
    static char *cmdFeedbackStyleNames[] =
    {
	"dotted", "medium", "outline", "pale", "solid", NULL
    };
    static int cmdFeedbackStyles[] =
    {
	STYLE_DOTTEDHIGHLIGHTS, STYLE_MEDIUMHIGHLIGHTS,
	STYLE_OUTLINEHIGHLIGHTS, STYLE_PALEHIGHLIGHTS,
	STYLE_SOLIDHIGHLIGHTS, -1
    };
    static int nth = 0;			/* Last entry displayed in
					 * "feedback find".
					 */
    int option, i, style, pstart;
    Rect box, r;
    char *text, **msg;
    CellDef *rootDef;
    HashTable table;
    HashEntry *h;
    FILE *f;

    if (cmd->tx_argc < 2)
    {
	badusage:
	TxPrintf("Wrong number of arguments for \"feedback\" command.\n");
	TxPrintf("Type \":feedback help\" for help.\n");
	return;
    }
    option = Lookup(cmd->tx_argv[1], cmdFeedbackOptions);
    if (option < 0)
    {
	TxError("%s isn't a valid feedback option.  Try one of:\n",
	    cmd->tx_argv[1]);
	TxError("    add        find\n");
	TxError("    clear      help\n");
	TxError("    count      save\n");
	TxError("    save\n");
	return;
    }
    pstart = 4;		/* argument # where point list starts, if any */
    switch (option)
    {
	case ADD:
	    style = STYLE_PALEHIGHLIGHTS;
	    if (cmd->tx_argc > 3)
	    {
		i = Lookup(cmd->tx_argv[3], cmdFeedbackStyleNames);
		if (i < 0)
		{
		    if (StrIsNumeric(cmd->tx_argv[3]))
			pstart = 3;
		    else
		    {
		        style = GrGetStyleFromName(cmd->tx_argv[3]);
			if (style < 0)
			{
			    TxError("%s isn't a valid display style.  Try one of:\n",
					cmd->tx_argv[3]);
			    TxError("    dotted, pale, medium, solid, outline,\n");
			    TxError("    or a long name from the .dstyle file\n");
			    break;
			}
			if (style < 1 || style >= TECHBEGINSTYLES)
	 		{
			    TxError("Numbered styles must be between 1 and %d\n",
					TECHBEGINSTYLES - 1);
			    break;
			}
		    }
		}
		else style = cmdFeedbackStyles[i];
	    }
	    if (cmd->tx_argc - pstart > 0)
	    {
		/* points must be in X Y pairs */
		if ((cmd->tx_argc - pstart) & 1) goto badusage;

	        if (w == NULL) return;
		rootDef = ((CellUse *) w->w_surfaceID)->cu_def;

		/* Read coordinates in FEEDMAGNIFY x internal units,	*/
		/* then scale the highlight accordingly.		*/

		if ((cmd->tx_argc - pstart) == 2)
		{
		    /* Single point highlight.  Style MUST be outlined	*/
		    /* or else the point won't be visible.		*/

		    if (GrStyleTable[style].outline == 0)
			style = STYLE_OUTLINEHIGHLIGHTS;

		    box.r_xbot = box.r_xtop = cmdScaleCoord(w, cmd->tx_argv[pstart++],
				FALSE, TRUE, FEEDMAGNIFY);
		    box.r_ybot = box.r_ytop = cmdScaleCoord(w, cmd->tx_argv[pstart++],
				FALSE, FALSE, FEEDMAGNIFY);

		    DBWFeedbackAdd(&box, cmd->tx_argv[2], rootDef, FEEDMAGNIFY, style);
		}
		else if ((cmd->tx_argc - pstart) == 4)
		{
		    /* Single line highlight.  Style MUST be outlined,	*/
		    /* or else the line won't be visible.		*/

		    if (GrStyleTable[style].outline == 0)
			style = STYLE_OUTLINEHIGHLIGHTS;
		    r.r_xbot = cmdScaleCoord(w, cmd->tx_argv[pstart++],
				FALSE, TRUE, FEEDMAGNIFY);
		    r.r_ybot = cmdScaleCoord(w, cmd->tx_argv[pstart++],
				FALSE, FALSE, FEEDMAGNIFY);
		    r.r_xtop = cmdScaleCoord(w, cmd->tx_argv[pstart++],
				FALSE, TRUE, FEEDMAGNIFY);
		    r.r_ytop = cmdScaleCoord(w, cmd->tx_argv[pstart++],
				FALSE, FALSE, FEEDMAGNIFY);
		    if (r.r_xbot != r.r_xtop && r.r_ybot != r.r_ytop)
		    {
			/* Line at an angle.  The hack of setting TT_SIDE */
			/* without setting TT_DIAGONAL allows the drawing */
			/* routine to treat this case as a line, not a	  */
			/* triangle.					  */

			style |= TT_SIDE;
			if ((r.r_xbot > r.r_xtop && r.r_ybot < r.r_ytop) ||
				(r.r_xbot < r.r_xtop && r.r_ybot > r.r_ytop))
			    style |= TT_DIRECTION;
		    }
		    GeoCanonicalRect(&r, &box);
		    DBWFeedbackAdd(&box, cmd->tx_argv[2], rootDef, FEEDMAGNIFY, style);
		}
		else
		{
		    Plane *plane;
		    PaintResultType ptable[2] = {1, 1}; /* simple 1 bit table */
		    TileTypeBitMask feedSimpleMask;
		    Point *plist;
		    int points, j;
		    struct cmdFPArg fpargs;

		    points = (cmd->tx_argc - pstart) >> 1;
		    plist = (Point *)mallocMagic(points * sizeof(Point));
		    fpargs.def = rootDef;
		    fpargs.style = style;
		    fpargs.text = cmd->tx_argv[2];

		    for (i = 0, j = pstart; i < points; i++) 
		    {
			plist[i].p_x = cmdScaleCoord(w, cmd->tx_argv[j++],
				FALSE, TRUE, FEEDMAGNIFY);
			plist[i].p_y = cmdScaleCoord(w, cmd->tx_argv[j++],
				FALSE, FALSE, FEEDMAGNIFY);
		    }
		    plane = DBNewPlane((ClientData)0);
		    TTMaskZero(&feedSimpleMask);
		    TTMaskSetType(&feedSimpleMask, 1);
		    PaintPolygon(plist, points, plane, ptable,
				(PaintUndoInfo *)NULL, FALSE);
		    i = DBWFeedbackCount;
		    DBSrPaintArea((Tile *)NULL, plane, &TiPlaneRect, &feedSimpleMask,
			feedPolyFunc, (ClientData)&fpargs);
		    TiFreePlane(plane);

		    if (i == DBWFeedbackCount)
		    {
			/* Pathological condition---feedback area is so thin	*/
			/* that the decomposition to rectangles and triangles	*/
			/* was degenerate.  If so, then we can just treat the	*/
			/* plist as several lines and/or points.		*/

			if (GrStyleTable[style].outline == 0)
			    style = STYLE_OUTLINEHIGHLIGHTS;

			for (i = 0; i < points - 1; i++)
			{
			    r.r_xbot = plist[i].p_x;
			    r.r_ybot = plist[i].p_y;
			    r.r_xtop = plist[i + 1].p_x;
			    r.r_ytop = plist[i + 1].p_y;
			    if (r.r_xbot != r.r_xtop && r.r_ybot != r.r_ytop)
		    	    {
				style |= TT_SIDE;
				if ((r.r_xbot > r.r_xtop && r.r_ybot < r.r_ytop) ||
					(r.r_xbot < r.r_xtop && r.r_ybot > r.r_ytop))
			    	style |= TT_DIRECTION;
			    }
			    GeoCanonicalRect(&r, &box);
			    DBWFeedbackAdd(&box, cmd->tx_argv[2], rootDef,
						FEEDMAGNIFY, style);
			}
		    }
		    freeMagic(plist);
		}	
	    }
	    else
	    {
		w = ToolGetBoxWindow(&box, (int *) NULL);
	        if (w == NULL) return;
		rootDef = ((CellUse *) w->w_surfaceID)->cu_def;
		DBWFeedbackAdd(&box, cmd->tx_argv[2], rootDef, 1, style);
	    }
	    break;
	
	case CLEAR:
	    if (cmd->tx_argc == 3)
	        DBWFeedbackClear(cmd->tx_argv[2]);
	    else if (cmd->tx_argc == 2)
	        DBWFeedbackClear(NULL);
	    else
		goto badusage;
	    nth = 0;
	    break;
	
	case COUNT:
	    if (cmd->tx_argc != 2) goto badusage;
	    TxPrintf("There are %d feedback areas.\n", DBWFeedbackCount);
	    break;
	
	case FIND:
	    if (cmd->tx_argc > 3) goto badusage;
	    if (DBWFeedbackCount == 0)
	    {
		TxPrintf("There are no feedback areas right now.\n");
		break;
	    }
	    if (cmd->tx_argc == 3)
	    {
		nth = atoi(cmd->tx_argv[2]);
		if ((nth > DBWFeedbackCount) || (nth <= 0))
		{
		    TxError("Sorry, but only feedback areas 1-%d exist.\n",
			DBWFeedbackCount);
		    nth = 1;
		}
	    }
	    else
	    {
		nth += 1;
		if (nth > DBWFeedbackCount) nth = 1;
	    }
	    text = DBWFeedbackNth(nth-1, &box, &rootDef, (int *) NULL);
	    ToolMoveBox(TOOL_BL, &box.r_ll, FALSE, rootDef);
	    ToolMoveCorner(TOOL_TR, &box.r_ur, FALSE, rootDef);
	    TxPrintf("Feedback #%d: %s\n", nth, text);
	    break;
	
	case FEED_HELP:
	    if (cmd->tx_argc > 2) goto badusage;
	    TxPrintf("Feedback commands have the form \"feedback option\",\n");
	    TxPrintf("where option is one of:\n");
	    for (msg = cmdFeedbackOptions; *msg != NULL; msg++)
		TxPrintf("%s\n", *msg);
	    break;
	
	case SAVE:
	    if (cmd->tx_argc != 3) goto badusage;
	    f = PaOpen(cmd->tx_argv[2], "w", (char *) NULL, ".",
	        (char *) NULL, (char **) NULL);
	    if (f == NULL)
	    {
		TxError("Can't open file %s.\n", cmd->tx_argv[2]);
		break;
	    }
	    for (i = 0; i < DBWFeedbackCount; i++)
	    {
		int j, style;
		text = DBWFeedbackNth(i, &box, (CellDef **) NULL, &style);
		fprintf(f, "box %d %d %d %d\n", box.r_xbot, box.r_ybot,
		    box.r_xtop, box.r_ytop);
		fprintf(f, "feedback add \"");

		/* Be careful to backslash any quotes in the text! */

		for ( ; *text != 0; text += 1)
		{
		    if (*text == '"') fputc('\\', f);
		    fputc(*text, f);
		}
		fputc('"', f);
		for (j = 0; cmdFeedbackStyles[j] >= 0; j++)
		{
		    if (cmdFeedbackStyles[j] == style)
		    {
			fprintf(f, " %s", cmdFeedbackStyleNames[j]);
			break;
		    }
		}
		fprintf(f, "\n");
	    }
	    (void) fclose(f);
	    break;
	
	case WHY:
	    if (cmd->tx_argc > 2) goto badusage;
	    w = ToolGetBoxWindow(&box, (int *) NULL);
	    if (w == NULL) return;
	    rootDef = ((CellUse *) w->w_surfaceID)->cu_def;
	    HashInit(&table, 16, 0);
	    for (i = 0; i < DBWFeedbackCount; i++)
	    {
		Rect area;
		CellDef *fbRootDef;

		text = DBWFeedbackNth(i, &area, &fbRootDef, (int *) NULL);
		if (rootDef != fbRootDef) continue;
		if (!GEO_OVERLAP(&box, &area)) continue;
		h = HashFind(&table, text);
		if (HashGetValue(h) == 0) TxPrintf("%s\n", text);
		HashSetValue(h, 1);
	    }
	    HashKill(&table);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdFill --
 *
 * Implement the "fill" command.  Find all paint touching one side
 * of the box, and paint it across to the other side of the box.  Can
 * operate in any of four directions.
 *
 * Usage: 
 *	fill direction [layers]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the edit cell definition.
 *
 * ----------------------------------------------------------------------------
 */

/* Data passed between CmdFill and cmdFillFunc: */

int cmdFillDir;				/* Direction in which to fill. */
Rect cmdFillRootBox;			/* Root coords of box. */
struct cmdFillArea *cmdFillList;	/* List of areas to fill. */

void
CmdFill(w, cmd)
    MagWindow *w;		/* Window in which command was invoked. */
    TxCommand *cmd;	/* Describes the command that was invoked. */
{
    TileTypeBitMask maskBits;
    Rect editBox;
    SearchContext scx;
    extern int cmdFillFunc();

    if (cmd->tx_argc < 2 || cmd->tx_argc > 3)
    {
	TxError("Usage: %s direction [layers]\n", cmd->tx_argv[0]);
	return;
    }

    windCheckOnlyWindow(&w, DBWclientID);
    if ( w == (MagWindow *) NULL )
    {
	TxError("Point to a window\n");
	return;
    }

    /* Find and check validity of position argument. */

    cmdFillDir = GeoNameToPos(cmd->tx_argv[1], TRUE, TRUE);
    if (cmdFillDir < 0)
	return;

    /* Figure out which layers to fill. */

    if (cmd->tx_argc < 3)
	maskBits = DBAllButSpaceAndDRCBits;
    else
    {
	if (!CmdParseLayers(cmd->tx_argv[2], &maskBits))
	    return;
    }

    /* Figure out which material to search for and invoke a search
     * procedure to find it.
     */

    if (!ToolGetEditBox(&editBox)) return;
    GeoTransRect(&EditToRootTransform, &editBox, &cmdFillRootBox);
    scx.scx_area = cmdFillRootBox;
    switch (cmdFillDir)
    {
	case GEO_NORTH:
	    scx.scx_area.r_ytop = scx.scx_area.r_ybot + 1;
	    scx.scx_area.r_ybot -= 1;
	    break;
	case GEO_SOUTH:
	    scx.scx_area.r_ybot = scx.scx_area.r_ytop - 1;
	    scx.scx_area.r_ytop += 1;
	    break;
	case GEO_EAST:
	    scx.scx_area.r_xtop = scx.scx_area.r_xbot + 1;
	    scx.scx_area.r_xbot -= 1;
	    break;
	case GEO_WEST:
	    scx.scx_area.r_xbot = scx.scx_area.r_xtop - 1;
	    scx.scx_area.r_xtop += 1;
	    break;
    }
    scx.scx_use = (CellUse *) w->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    cmdFillList = (struct cmdFillArea *) NULL;

    (void) DBTreeSrTiles(&scx, &maskBits,
	    ((DBWclientRec *) w->w_clientData)->dbw_bitmask,
	    cmdFillFunc, (ClientData) NULL);

    /* Now that we've got all the material, scan over the list
     * painting the material and freeing up the entries on the list.
     */
    while (cmdFillList != NULL)
    {
	DBPaint(EditCellUse->cu_def, &cmdFillList->cfa_area,
		cmdFillList->cfa_type);
	freeMagic((char *) cmdFillList);
	cmdFillList = cmdFillList->cfa_next;
    }

    SelectClear();
    DBAdjustLabels(EditCellUse->cu_def, &editBox);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKPAINT, &editBox);
    DBWAreaChanged(EditCellUse->cu_def, &editBox, DBW_ALLWINDOWS, &maskBits);
    DBReComputeBbox(EditCellUse->cu_def);
}

/* Important note:  these procedures can't paint the tiles directly,
 * because a search is in progress over the same planes and if we
 * paint here it may mess up the search.  Instead, the procedures
 * save areas on a list.  The list is post-processed to paint the
 * areas once the search is finished.
 */

int
cmdFillFunc(tile, cxp)
    Tile *tile;			/* Tile to fill with. */
    TreeContext *cxp;		/* Describes state of search. */
{
    Rect r1, r2;
    struct cmdFillArea *cfa;

    TiToRect(tile, &r1);
    GeoTransRect(&cxp->tc_scx->scx_trans, &r1, &r2);
    GeoClip(&r2, &cmdFillRootBox);
    switch (cmdFillDir)
    {
	case GEO_NORTH:
	    r2.r_ytop = cmdFillRootBox.r_ytop;
	    break;
	case GEO_SOUTH:
	    r2.r_ybot = cmdFillRootBox.r_ybot;
	    break;
	case GEO_EAST:
	    r2.r_xtop = cmdFillRootBox.r_xtop;
	    break;
	case GEO_WEST:
	    r2.r_xbot = cmdFillRootBox.r_xbot;
	    break;
    }
    GeoTransRect(&RootToEditTransform, &r2, &r1);
    cfa = (struct cmdFillArea *) mallocMagic(sizeof(struct cmdFillArea));
    cfa->cfa_area = r1;
    cfa->cfa_type = TiGetType(tile);
    cfa->cfa_next = cmdFillList;
    cmdFillList = cfa;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdFindBox --
 *
 * Center the display on a corner of the box.  If 'zoom', then make the box
 * fill the window.
 *
 * Usage:
 *	findbox [zoom]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window underneath the cursor is moved.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdFindBox(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellDef *boxDef;
    Rect box;

    if (w == NULL)
    {
	TxError("Point to a window first.\n");
	return;
    };

    if (!ToolGetBox(&boxDef, &box))
    {
	TxError("Put the box in a window first.\n");
	return;
    };

    if (boxDef != (((CellUse *) w->w_surfaceID)->cu_def))
    {
	TxError("The box is not in the same coordinate %s", 
		"system as the window.\n");
	return;
    };

    if (cmd->tx_argc == 1) 
    {
	/* center view on box */
	Point rootPoint;
	Rect newArea, oldArea;

	rootPoint.p_x = (box.r_xbot + box.r_xtop)/2;
	rootPoint.p_y = (box.r_ybot + box.r_ytop)/2;

	oldArea = w->w_surfaceArea;
	newArea.r_xbot = rootPoint.p_x - (oldArea.r_xtop - oldArea.r_xbot)/2;
	newArea.r_xtop = newArea.r_xbot - oldArea.r_xbot + oldArea.r_xtop;
	newArea.r_ybot = rootPoint.p_y - (oldArea.r_ytop - oldArea.r_ybot)/2;
	newArea.r_ytop = newArea.r_ybot - oldArea.r_ybot + oldArea.r_ytop;

	WindMove(w, &newArea);
	return;
    }
    else if (cmd->tx_argc == 2)
    {
	int expand;

	/* zoom in to box */

	if (strcmp(cmd->tx_argv[1], "zoom") != 0) goto usage;

	/* Allow a 5% ring around the box on each side. */

	expand = (box.r_xtop - box.r_xbot)/20;
	if (expand < 2) expand = 2;
	box.r_xtop += expand;
	box.r_xbot -= expand;
	expand = (box.r_ytop - box.r_ybot)/20;
	if (expand < 2) expand = 2;
	box.r_ytop += expand;
	box.r_ybot -= expand;

	WindMove(w, &box);
	return;
    };

usage:
    TxError("Usage: findbox [zoom]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdFindLabel --
 *
 * Find a label and set the box to it.
 *
 * Usage:
 *	findlabel [-glob] name
 *
 * Results:
 *	None.  In TCL, the -glob option will generate a list of matching
 *	nodes returned in the interpreter result.
 *
 * Side effects:
 *	The box may be moved.  If "-glob" is specified, in the non-Tcl
 *	version, matching label names are printed.
 *
 * ----------------------------------------------------------------------------
 */

int cmdFindLabelFunc(rect, name, label, cdarg) 
    Rect *rect;
    char *name;
    Label *label;
    Rect *cdarg;
{
    *cdarg = *rect;
    return 1;
}

void
CmdFindLabel(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellDef *boxDef;
    CellUse *labUse;
    Rect box, cmdFindLabelRect;
    char *labname;
    int found;
    bool doglob = FALSE;   /* csh-style glob matching (see utils/match.c) */
    int dbListLabels();	   /* forward declaration */

    if ((cmd->tx_argc == 3) && !strncmp(cmd->tx_argv[1], "-glob", 5))
	doglob = TRUE;
    else if (cmd->tx_argc != 2)
	goto usage;

    if (w == NULL)
    {
	TxError("Point to a window first.\n");
	return;
    };

    if (!ToolGetBox(&boxDef, &box))
    {
	TxError("Put the box in a window first.\n");
	return;
    };

    if (boxDef != (((CellUse *) w->w_surfaceID)->cu_def))
    {
	TxError("The box is not in the same coordinate %s", 
		"system as the window.\n");
	return;
    };

    labname = (cmd->tx_argc == 3) ? cmd->tx_argv[2] : cmd->tx_argv[1];
    labUse = EditCellUse;
    if (labUse == NULL) labUse = (CellUse *)w->w_surfaceID;

    if (doglob)
    {
	/* Pattern-matching label search */

	SearchContext scx;

	scx.scx_use = labUse;
	scx.scx_area = labUse->cu_def->cd_bbox;
	scx.scx_trans = GeoIdentityTransform;

	DBSearchLabel(&scx, &DBAllButSpaceAndDRCBits, 0, labname,
		dbListLabels, (ClientData) 0);
    }
    else
    {
	/* Exact-match label search (corrected by Nishit, 10/14/04) */

	found = DBSrLabelLoc(labUse, labname, cmdFindLabelFunc,
		(ClientData) &cmdFindLabelRect);
	if (found) {
	    if (cmdFindLabelRect.r_xbot == cmdFindLabelRect.r_xtop) 
		cmdFindLabelRect.r_xtop++;
	    if (cmdFindLabelRect.r_ybot == cmdFindLabelRect.r_ytop) 
		cmdFindLabelRect.r_ytop++;
	    ToolMoveBox(TOOL_BL,&cmdFindLabelRect.r_ll,FALSE,labUse->cu_def);
	    ToolMoveCorner(TOOL_TR,&cmdFindLabelRect.r_ur,FALSE,labUse->cu_def);
	} else {
	    TxError("Couldn't find label %s\n", labname);
	}
    }

    return;


usage:
    TxError("Usage: findlabel [-glob] label_name\n");
}

/*
 * Callback routine for listing pattern-matched labels.
 * Always return zero to keep the search going.
 */

int
dbListLabels(scx, label, tpath, cdarg)
    SearchContext *scx;
    Label *label;			/* Pointer to label structure	*/
    TerminalPath *tpath;		/* Full pathname of terminal	*/
    ClientData cdarg;			/* (unused)			*/
{
    char *n = tpath->tp_next;
    char c = *n;
    strcpy(n, label->lab_text);
#ifdef MAGIC_WRAPPER
    Tcl_AppendElement(magicinterp, tpath->tp_first);
#else
    TxPrintf("%s\n", tpath->tp_first);
#endif
    *n = c;
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdFlush --
 *
 * Implement the "flush" command.
 * Throw away all changes made within magic to the specified cell,
 * and re-read it from disk.  If no cell is specified, the default
 * is the current edit cell.
 *
 * Usage:
 *	flush [cellname]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	THIS IS NOT UNDO-ABLE!
 *	Modifies the specified CellDef.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdFlush(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    CellDef *def;
    int action;
    static char *actionNames[] = { "no", "yes", 0 };
    char *prompt;

    if (cmd->tx_argc > 2)
    {
	TxError("Usage: flush [cellname]\n");
	return;
    }

    if (cmd->tx_argc == 1)
    {
	if (EditCellUse != NULL)
	    def = EditCellUse->cu_def;
	else
	    def = ((CellUse *)w->w_surfaceID)->cu_def;
    }
    else
    {
	def = DBCellLookDef(cmd->tx_argv[1]);
	if (def == (CellDef *) NULL)
	{
	    /* an error message has already been printed by the database */
	    return;
	}
    }

    if (def->cd_flags & (CDMODIFIED|CDSTAMPSCHANGED|CDBOXESCHANGED))
    {
	prompt = TxPrintString("Really throw away all changes made"
			" to cell %s? ", def->cd_name);
	action = TxDialog(prompt, actionNames, 0);
	if (action == 0)	/* No */
	    return;
    }

    cmdFlushCell(def);
    SelectClear();
    TxPrintf("[Flushed]\n");
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdGetcell --
 *
 *	Implement the ":getcell" command.
 *
 * Usage:
 *	getcell cellName [child refPointChild] [parent refPointParent]
 *
 * where the refPoints are either a label name, e.g., SOCKET_A, or an x-y
 * pair of integers, e.g., 100 200.  The words "child" and "parent" are
 * keywords, and may be abbreviated.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Makes cellName a subcell of the edit cell, positioned so
 *	that refPointChild in the child cell (or the lower-left
 *	corner of its bounding box) ends up at location refPointParent
 *	in the edit cell (or the location of the box tool's lower-left).
 *
 * ----------------------------------------------------------------------------
 */

void
CmdGetcell(w, cmd)
    MagWindow *w;			/* Window in which command was invoked. */
    TxCommand *cmd;		/* Describes command arguments. */
{
    CellUse dummy, *newUse;
    Transform editTrans;
    SearchContext scx;
    CellDef *def;
    Rect newBox;

    /* Leaves scx.scx_trans set to the transform from the child to root */
    if (!cmdDumpParseArgs("getcell", w, cmd, &dummy, &scx))
	return;
    def = dummy.cu_def;

    /* Create the new use. */
    newUse = DBCellNewUse(def, (char *) NULL);
    if (!DBLinkCell(newUse, EditCellUse->cu_def))
    {
	(void) DBCellDeleteUse(newUse);
	TxError("Could not link in new cell\n");
	return;
    }

    GeoTransTrans(&scx.scx_trans, &RootToEditTransform, &editTrans);
    DBSetTrans(newUse, &editTrans);
    if (DBCellFindDup(newUse, EditCellUse->cu_def) != NULL)
    {
	DBCellDeleteUse(newUse);
	TxError("Can't place a cell on an exact copy of itself.\n");
	return;
    }
    DBPlaceCell(newUse, EditCellUse->cu_def);

    /*
     * Reposition the box tool to around the gotten cell to show
     * that it has become the current cell.
     */
    GeoTransRect(&EditToRootTransform, &newUse->cu_bbox, &newBox);
    DBWSetBox(EditRootDef, &newBox);

    /* Select the new use */
    SelectClear();
    SelectCell(newUse, EditRootDef, &scx.scx_trans, FALSE);

    /* Redisplay and mark for design-rule checking */
    DBReComputeBbox(EditCellUse->cu_def);
    DBWAreaChanged(EditCellUse->cu_def, &newUse->cu_bbox,
	DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DRCCheckThis(EditCellUse->cu_def, TT_CHECKSUBCELL, &newUse->cu_bbox);

#ifdef MAGIC_WRAPPER
    /* If using the TCL wrapper, set the TCL return value to the	*/
    /* name of the new use.						*/
    if (newUse->cu_id)
	Tcl_SetResult(magicinterp, newUse->cu_id, TCL_VOLATILE);
#endif

}
#ifndef NO_SIM_MODULE

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGetnode --
 *
 * Implement the "getnode" command.
 * Returns the name of the node pointed by the mouse
 *
 * Usage:
 *  	getnode
 *	getnode abort [string]
 *      getnode alias [on | off]
 *      getnode globals [on | off]
 *      getnode fast
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The GetNode hash tables may be modified.
 * ----------------------------------------------------------------------------
 */

void
CmdGetnode(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
#define TBLSIZE 50
#define STRINGS 0

    bool  is_fast = FALSE;

    /* check arguments to command */

    switch (cmd->tx_argc) {
	case 1 : 
	    break;

	case 2 : 
	    if (strcmp("abort", cmd->tx_argv[1]) == 0) {
		if (!SimInitGetnode) {
		    HashKill(&SimGetnodeTbl);
		    SimInitGetnode = TRUE;
   		    SimRecomputeSel = TRUE;
		}
		return;
	    }
	    else if (strcmp("fast", cmd->tx_argv[1]) == 0) {
		is_fast = TRUE;
	    }
	    else {
		if (!strcmp("alias", cmd->tx_argv[1])) {
		    TxPrintf("Aliases %s\n", (SimGetnodeAlias) ?  "on" : "off");
		    return;
		}
		else if (strncmp("global", cmd->tx_argv[1], 6) == 0) {
		    TxPrintf("Node names ending in ! are %s\n",
				(SimIgnoreGlobals) ? "local (off)" : "global (on)");
		    return;
		}
		else
		    goto badusage;
	    }
	    break;

	case 3 : 
	    if (strcmp("alias", cmd->tx_argv[1]) == 0) {
		if (strcmp("on", cmd->tx_argv[2]) == 0) {
		    if (!SimGetnodeAlias) {
			HashInit(&SimGNAliasTbl, 120, STRINGS);
		    }
		    SimGetnodeAlias = TRUE;
		    return;
		}
		else if (strcmp("off", cmd->tx_argv[2]) == 0) {
		    if (SimGetnodeAlias) {
			HashKill(&SimGNAliasTbl);
		    }
		    SimGetnodeAlias = FALSE;
		    return;
		}
		else
		    goto badusage;
	    }
	    else if (strncmp("global", cmd->tx_argv[1], 6) == 0) {
		if (strcmp("off", cmd->tx_argv[2]) == 0) {
		    SimIgnoreGlobals = TRUE;
		    return;
		}
		else if (strcmp("on", cmd->tx_argv[2]) == 0) {
		    SimIgnoreGlobals = FALSE;
		    return;
		}
		else
		    goto badusage;
	    }
	    else if (strcmp("abort", cmd->tx_argv[1]) == 0) {
		if (SimInitGetnode) {
		    HashInit(&SimGetnodeTbl, TBLSIZE, STRINGS);
		    SimInitGetnode = FALSE;
		}
		SimRecomputeSel = TRUE;
		HashFind(&SimGetnodeTbl, cmd->tx_argv[2]);
		return;
	    }
	    else {
		goto badusage;
	    }
	    break;

	default :
	    goto badusage;
    }

    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }
    if( is_fast == TRUE )
    {
	SimRecomputeSel = TRUE;
	SimGetsnode();
    }
    else
	SimGetnode();

    if (SimGetnodeAlias) {			/* "erase" the hash table */
	HashKill(&SimGNAliasTbl);
	HashInit(&SimGNAliasTbl, 120, STRINGS);
    }
    return;


badusage:
    TxError("Usage: getnode [abort [str]]\n");
    TxError("   or: getnode alias [on | off]\n");
    TxError("   or: getnode globals [on | off]\n");
    TxError("   or: getnode fast\n");
}
#endif

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGrid --
 *
 * Implement the "gridspace" command.
 * Toggle the grid on or off in the selected window.
 *
 * Usage:
 *	gridspace [spacing [spacing [xorig yorig]]]
 *	gridspace on|off|box|state|help|multiple
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None, except to enable or disable grid display.
 *
 * ----------------------------------------------------------------------------
 */

#define GRID_BOX	0
#define GRID_HELP	1
#define GRID_MULTIPLE	2
#define GRID_OFF	3
#define GRID_ON		4
#define GRID_STATE	5
#define GRID_TOGGLE	6
#define GRID_WHAT	7

void
CmdGrid(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int option, locargc;
    int xSpacing, ySpacing, xOrig, yOrig, multiple;
    DBWclientRec *crec;
    char *boxvalues;
    static char *cmdGridOptions[] =
    {
	"box [values]	report the box representing the user grid",
	"help		print this message",
	"multiple [m]	set the grid multiple for drawing grids at large scales",
	"off		turn off the user grid",
	"on		turn on the user grid",
	"state		report the state of the user grid",
	"toggle		toggle the state (on/off) of the user grid",
	"what		(equivalent to option \"box\")",
	NULL
    };

    windCheckOnlyWindow(&w, DBWclientID);
    if (w == (MagWindow *) NULL) return;
    crec = (DBWclientRec *) w->w_clientData;
    locargc = cmd->tx_argc;

    if (locargc == 1)
	option = GRID_TOGGLE;
    else if ((locargc == 2) && !strcmp(cmd->tx_argv[1], "0"))
	option = GRID_OFF;
    else
	option = Lookup(cmd->tx_argv[1], cmdGridOptions);

    /* Process various options with two arguments */
    switch (option)
    {
	case GRID_BOX:
	    if (locargc > 2)
	    {
		locargc--;
		break;
	    }

	case GRID_WHAT:

#ifdef MAGIC_WRAPPER
	    boxvalues = (char *)Tcl_Alloc(50);
	    sprintf(boxvalues, "%d %d %d %d",
		crec->dbw_gridRect.r_xbot, crec->dbw_gridRect.r_ybot,
		crec->dbw_gridRect.r_xtop, crec->dbw_gridRect.r_ytop);
	    Tcl_SetResult(magicinterp, boxvalues, TCL_DYNAMIC);
	
#else
	    TxPrintf("Grid unit box is (%d, %d) to (%d, %d)\n", 
		crec->dbw_gridRect.r_xbot, crec->dbw_gridRect.r_ybot,
		crec->dbw_gridRect.r_xtop, crec->dbw_gridRect.r_ytop);
#endif
	    return;

	case GRID_STATE:

#ifdef MAGIC_WRAPPER
	    Tcl_SetObjResult(magicinterp,
			Tcl_NewBooleanObj(crec->dbw_flags & DBW_GRID));
#else
	    TxPrintf("Grid is %s\n", 
			(crec->dbw_flags & DBW_GRID) ? "on" : "off");
#endif
	    return;

	case GRID_HELP:

	    TxPrintf("Usage: grid [xSpacing [ySpacing [xOrig yOrig]]]]\n");
	    TxPrintf("or     grid <option>\n");
	    TxPrintf("where <option> is one of: "
			"on, off, state, box, what, help, or multiple.\n");
	    return;

	case GRID_OFF:

	    if (crec->dbw_flags & DBW_GRID)
	    {
		crec->dbw_flags &= ~DBW_GRID;
		WindAreaChanged(w, (Rect *) NULL);
	    }
	    return;

	case GRID_ON:
	    if (!(crec->dbw_flags & DBW_GRID))
	    {
		crec->dbw_flags |= DBW_GRID;
		WindAreaChanged(w, (Rect *) NULL);
	    }
	    return;

	case GRID_MULTIPLE:
	    if (locargc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetObjResult(magicinterp,
			Tcl_NewIntObj(GrGridMultiple));
#endif
	    }
	    else if (StrIsInt(cmd->tx_argv[2]))
	    {
		multiple = atoi(cmd->tx_argv[2]);
		if (multiple < 1 || multiple > 255)
		    TxError("Usage: grid multiple <integer value 1-255>\n");
		GrGridMultiple = (unsigned char)multiple;
	    }
	    else if (!strcmp(cmd->tx_argv[2], "off"))
		GrGridMultiple = (unsigned char)1;
	    else
		TxError("Usage: grid multiple <integer value 1-255>\n");
	    return;

	case GRID_TOGGLE:
 	    crec->dbw_flags ^= DBW_GRID;
	    break;
    }

    if ((option == GRID_BOX) || (option < 0))
    {
	int argstart = (option == GRID_BOX) ? 2 : 1;

	if ((locargc == 4) || (locargc > 5))
	{
	    TxError("Usage: %s [xSpacing [ySpacing [xOrig yOrig]]]]\n",
			cmd->tx_argv[0]);
	    return;
	}

	xSpacing = cmdParseCoord(w, cmd->tx_argv[argstart], TRUE, TRUE);
	if (xSpacing <= 0)
	{
	    TxError("Grid spacing must be greater than zero.\n");
	    return;
	}
	ySpacing = xSpacing;
	xOrig = yOrig = 0;

	if (locargc >= 3)
	{
	    ySpacing = cmdParseCoord(w, cmd->tx_argv[argstart + 1], TRUE, FALSE);
	    if (ySpacing <= 0)
	    {
		TxError("Grid spacing must be greater than zero.\n");
		return;
	    }

	    if (locargc == 5)
	    {
		xOrig = cmdParseCoord(w, cmd->tx_argv[argstart + 2], FALSE, TRUE);
		yOrig = cmdParseCoord(w, cmd->tx_argv[argstart + 3], FALSE, FALSE);
	    }
	}

	crec->dbw_gridRect.r_xbot = xOrig;
	crec->dbw_gridRect.r_ybot = yOrig;
	crec->dbw_gridRect.r_xtop = xOrig + xSpacing;
	crec->dbw_gridRect.r_ytop = yOrig + ySpacing;
	crec->dbw_flags |= DBW_GRID;
    }
    WindAreaChanged(w, (Rect *) NULL);
}

#ifdef USE_READLINE
void
CmdHistory(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
  int i;
  HIST_ENTRY *he;
  int list_reverse = 0;
  int list_numbers = 1;
  int print_help = 0;
  int num_to_list = history_length;

  if( cmd->tx_argc > 1 ) {
    for(i=1; i<cmd->tx_argc; i++) {
      if( strcmp(cmd->tx_argv[i], "help") == 0 ) {
        print_help = 1;
        break;
      } else if( strcmp(cmd->tx_argv[i], "n") == 0 ) {
        list_numbers = 0;
      } else if( strcmp(cmd->tx_argv[i], "r") == 0 ) {
        list_reverse = 1;
      } else {
        num_to_list = atoi(cmd->tx_argv[i]);
        if( num_to_list == 0 ) {
          TxError("Bad arg to history: '%s'\n", cmd->tx_argv[i]);
          print_help = 1;
          break;
        }
      }
    }
  }

  if( print_help ) {
    TxError("Usage: history [n] [r] [<num>]\n"
            "         'n'     suppresses printing numbers\n"
            "         'r'     prints the list in reverse\n"
            "         '<num>' is the number of entries to print\n"
            "         'help'  prints this message\n"
           );
  } else {
    int begin = MAX(history_length - num_to_list, 0);
    int end   = history_length;
    int j;

    for(i=begin,j=end-1; i<end; i++,j--) {
      int idx = (list_reverse) ? j : i;
      HIST_ENTRY *he = history_get(idx);
      if( he != (HIST_ENTRY *)NULL && he->line != (char *)NULL ) {
        if( list_numbers ) {
          TxPrintf("\t%4d %s\n", idx,  he->line);
        } else {
          TxPrintf("%s\n", he->line);
        }
      }
    }
  }
}
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * CmdIdentify --
 *
 * Implement the "identify" command.
 * Sets the instance identifier for the currently selected cell.
 *
 * Usage:
 *	identify use_id
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the instance identifier for the selected cell (the
 *	first selected cell, if there are many).
 *
 * ----------------------------------------------------------------------------
 */

void
CmdIdentify(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    extern int cmdIdFunc();		/* Forward reference. */

    if (cmd->tx_argc != 2)
    {
	TxError("Usage: identify use_id\n");
	return;
    }

    if (CmdIllegalChars(cmd->tx_argv[1], "[],/", "Cell use id"))
	return;

    if (SelEnumCells(FALSE, (int *) NULL, (SearchContext *) NULL,
	    cmdIdFunc, (ClientData) cmd->tx_argv[1]) == 0)
    {
	TxError("There isn't a selected subcell;  can't change ids.\n");
	return;
    }
}

    /* ARGSUSED */
int
cmdIdFunc(selUse, use, transform, newId)
    CellUse *selUse;		/* Use from selection cell. */
    CellUse *use;		/* Use from layout that corresponds to
				 * selUse.
				 */
    Transform *transform;	/* Not used. */
    char *newId;		/* New id for cell use. */
{
    if (EditCellUse == NULL)
    {
	TxError("Top-level cell is not editable---cannot change identifier"
		" of child cell %s.\n", use->cu_id);
	return 1;
    }
    if (!DBIsChild(use, EditCellUse))
    {
	TxError("Cell %s (%s) isn't a child of the edit cell.\n",
	    use->cu_id, use->cu_def->cd_name);
	TxError("    Cell identifier not changed.\n");
	return 1;
    }
    if (use->cu_parent == NULL)
    {
	TxError("Cell instance is a window top-level and cannot be changed.\n");
	return 1;
    }

    if (!DBReLinkCell(use, newId))
    {
	TxError("New name isn't unique within its parent definition.\n");
	TxError("    Cell identifier not changed.\n");
	return 1;
    }

    /* Change the id of the cell in the selection too, so that they
     * stay in sync.
     */

    (void) DBReLinkCell(selUse, newId);

    DBWAreaChanged(use->cu_parent, &use->cu_bbox,
	(int) ~(use->cu_expandMask), &DBAllButSpaceBits);
    DBWHLRedraw(EditRootDef, &selUse->cu_bbox, TRUE);
    return 1;
}

TileType
CmdFindNetProc(nodename, use, rect, warn_not_found)
    char *nodename;
    CellUse *use;
    Rect *rect;
    bool warn_not_found;
{
    char		*s,*s2;
    SearchContext	scx, scx2;
    Transform		trans, newtrans, tmp;
    Label		*label;
    Rect		localrect;
    int			pnum, xpos, ypos;
    char 		*xstr, *ystr;
    bool		locvalid;
    TileType		ttype;

    scx.scx_use = use;
    scx.scx_trans = GeoIdentityTransform;
    s = nodename;
    trans = GeoIdentityTransform;
    while (s2 = strchr(s, '/'))
    {
     	*s2 = '\0';
	DBTreeFindUse(s, scx.scx_use, &scx2);
	use = scx2.scx_use;
	if (use == NULL)
	{
	    if (warn_not_found)
		TxError("Couldn't find use %s\n", s);
	    return TT_SPACE; 
	}
	GeoTransTrans(DBGetArrayTransform(use, scx2.scx_x, scx2.scx_y),
	  		&use->cu_transform, &tmp);
	GeoTransTrans(&tmp, &trans, &newtrans);
	trans = newtrans;
	scx = scx2;
        *s2 = '/';
	s = s2 + 1;
    }

    /* If this node name is in the format of automatically-generated */
    /* node names, then parse the node string for X and Y values and */
    /* go to that point (transformed to the top level of the design  */
    /* hierarchy).						     */

    /* see extract/extractInt.h for the format of the node, found in */
    /* extMakeNodeNumPrint(), which is a macro, not a subroutine.    */

    locvalid = FALSE;
    if ((xstr = strchr(s, '_')) != NULL)
    {
	bool isNeg = FALSE;

        /* The characters up to the leading '_' should match one of the	*/
	/* "short names" for a plane in this technology.		*/ 

	*xstr = '\0';
	for (pnum = PL_TECHDEPBASE; pnum < DBNumPlanes; pnum++)
	    if (!strcmp(s, DBPlaneShortName(pnum)))
		break;
	*xstr = '_';
	if (pnum != DBNumPlanes)
	{
	    xstr++;
	    if (*xstr == 'n')
	    {
	        isNeg = TRUE;
	        xstr++;
	    }
	    if (sscanf(xstr, "%d", &xpos) == 1)
	    {
	        if (isNeg) xpos = -xpos;
	        if ((ystr = strchr(xstr, '_')) != NULL)
	        {
		    isNeg = FALSE;
		    ystr++;
		    if (*ystr == 'n')
		    {
		        isNeg = TRUE;
		        ystr++;
		    }
		    if (sscanf(ystr, "%d", &ypos) == 1)
		    {
		        if (isNeg) ypos = -ypos;
		        localrect.r_xbot = xpos;
		        localrect.r_ybot = ypos;
		        localrect.r_xtop = xpos + 1;
		        localrect.r_ytop = ypos + 1;
			/* TxPrintf("Node is on the plane \"%s\"\n", 
					DBPlaneLongNameTbl[pnum]); */
		        locvalid = TRUE;
		    }
	        }
	    }
	}
    }

    /* This is the original version, and assumes a format for node	*/
    /* coordinates that is no longer generated by magic.  It is kept	*/
    /* here for backward compatibility.					*/

    if ((locvalid == FALSE) && (sscanf(s,"%d_%d_%d",&pnum,&xpos,&ypos) == 3))
    {
	xpos = ((xpos & 0x1)?-1:1)*xpos/2;
	ypos = ((ypos & 0x1)?-1:1)*ypos/2;
	localrect.r_xbot = xpos;
	localrect.r_ybot = ypos;
	localrect.r_xtop = xpos + 1;
	localrect.r_ytop = ypos + 1;
	locvalid = TRUE;
    }

    if (locvalid == TRUE)
    {
	int findTile();
	CellDef	 *targetdef = use->cu_def;
	Plane *plane = targetdef->cd_planes[pnum];

	ttype = TT_SPACE;	/* revert to space in case of failure */
	
	/* Find the tile type of the tile at the specified point which	*/
	/* exists on the plane pnum.					*/

	(void) TiSrArea(NULL, plane, &localrect, findTile, (ClientData) &ttype);
    }
    else
    {
	for (label = scx.scx_use->cu_def->cd_labels; label;
			label = label->lab_next)
	{
	    if (!strcmp(label->lab_text, s)) break;
	}
	if (label)
	{
	    localrect = label->lab_rect;
	    ttype = label->lab_type;
	}
	else
	{
	    if (warn_not_found)
		TxError("Couldn't find label %s\n", s);
	    return TT_SPACE;
	}
    }
    GeoTransRect(&trans, &localrect, rect);
    return ttype;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGoto --
 *
 * Implements goto command
 *
 * Usage:
 *	goto nodename [-nocomplain]
 *
 * Results:
 *	None.  In TCL version, returns the material type
 *	of the node, so that the node can be uniquely determined
 *	by an automated script.   If the node cannot be found, a
 *	null list is returned.
 *
 * Side Effects:
 *	changes box location.
 *
 * Notes:
 *	Due to the way global node names are handled, "getnode" and
 *	"goto" are not necessarily reversible if "getnode globals on"
 *	is set!  In this case, use the glob option for findlabel
 *	to determine any valid local label belonging to the global
 *	node (a node cannot be global unless it is labeled).  This
 *	method, although awkward, can be much faster for large layouts
 *	due to the time required for "getnode" to uniquely determine
 *	the name of a large network such as power or ground.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdGoto(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char	*s, *nodename = cmd->tx_argv[1];
    Rect	rect;
    CellUse	*use;
    int		locargc;
    bool	nocomplain = FALSE;
    TileType	ttype;
     
    windCheckOnlyWindow(&w, DBWclientID);
    if ((w == (MagWindow *) NULL) || (w->w_client != DBWclientID))
    {
	TxError("Put the cursor in a layout window\n");
	return;
    }

    locargc = cmd->tx_argc;
    if (locargc == 3)
    {
	if (!strncmp(cmd->tx_argv[2], "-nocom", 5))
	{
	    nocomplain = TRUE;
	    locargc--;
	}
    }
    if (locargc != 2)
    {
     	TxError("usage: goto nodename [-nocomplain]\n");
	return;
    }

    /* CmdFindNetProc() does all the work */
    use = (CellUse *)w->w_surfaceID;
    ttype = CmdFindNetProc(nodename, use, &rect, !nocomplain);
    if (ttype == TT_SPACE) return;

    ToolMoveBox(TOOL_BL, &rect.r_ll, FALSE, use->cu_def);
    ToolMoveCorner(TOOL_TR, &rect.r_ur, FALSE, use->cu_def);

    /* Return the tile type so we know what we're looking at if there	*/
    /* are multiple layers drawn at the indicated point.		*/ 

#ifdef MAGIC_WRAPPER
    Tcl_SetResult(magicinterp, DBTypeLongName(ttype), NULL);
#else
    TxPrintf("node %s is type %s\n", s, DBTypeLongName(ttype));
#endif
}

/*
 * ----------------------------------------------------------------------------
 * Filter function for finding the tile type of the specified node
 * ----------------------------------------------------------------------------
 */

int
findTile(tile, rtype)
    Tile *tile;
    TileType *rtype;
{
    TileType ttype;

    if (IsSplit(tile))  
    {   
	if (SplitSide(tile))
	    ttype = SplitRightType(tile);
	else
	    ttype = SplitLeftType(tile);
    }
    else
	ttype = TiGetTypeExact(tile);
    *rtype = ttype;
    return 1;			/* stop search */
}

/*
 * The following are from DBcellcopy.c; slightly modified for present 
 * purposes.
 */

#define FLATTERMSIZE	1024

void
FlatCopyAllLabels(scx, mask, xMask, targetUse)
    SearchContext *scx;
    TileTypeBitMask *mask;
    int xMask;		
    CellUse *targetUse;
{
    int flatCopyAllLabels();
    char pathstring[FLATTERMSIZE];
    TerminalPath	tpath;
    
    tpath.tp_first = tpath.tp_next = pathstring;
    tpath.tp_last = pathstring + FLATTERMSIZE;

    DBTreeSrLabels(scx, mask, xMask, &tpath, TF_LABEL_ATTACH,
			flatCopyAllLabels, (ClientData) targetUse);
}

int
flatCopyAllLabels(scx, lab, tpath, targetUse)
    SearchContext *scx;
    Label *lab;
    TerminalPath *tpath;
    CellUse	*targetUse;
{
    Rect labTargetRect;
    int targetPos;
    CellDef *def;
    char	labelname[1024];
    char *n, *f, c;

    def = targetUse->cu_def;
    if (!GEO_LABEL_IN_AREA(&lab->lab_rect, &(scx->scx_area))) return 0;
    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &labTargetRect);
    targetPos = GeoTransPos(&scx->scx_trans, lab->lab_just);

    /* Eliminate duplicate labels.  Don't pay any attention to layers
     * in deciding on duplicates:  if text and position match, it's a
     * duplicate.
     */
    /* 9/12/05---The utility of eliminating duplicate labels has been
     * put into question by observing the O(n^2) behavior causing
     * significant flattening times for layouts with many labels.
     * Label text will not be duplicated anyway, because each label
     * contains the heirarchical names.
     */

    /* (void) DBEraseLabelsByContent(def, &labTargetRect, -1, lab->lab_text); */

    /* (Added 9/10/04) Make sure that the target flattened label is
     * not a port; possibly we should retain ports taken from the
     * top level cell, but certainly not any others.
     */

    /* To-do Feb. 2008:  Translate target rotation and offset */

    n = tpath->tp_next;
    f = tpath->tp_first;
    c = *n;
    strcpy(n, lab->lab_text);
    DBPutFontLabel(def, &labTargetRect, lab->lab_font, lab->lab_size,
		lab->lab_rotate, &lab->lab_offset, targetPos,
		f, lab->lab_type, 0);
    *n = c;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdFlatten --
 *
 * Implements flatten command
 *
 * Usage:
 *	flatten [-<option>] destname
 *
 * Results:
 *	creates new cell def with all the desired info.
 *
 *
 * ----------------------------------------------------------------------------
 */

void
CmdFlatten(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
     int		rval, xMask;
     bool		dolabels, toplabels, invert;
     char		*destname;
     CellDef		*newdef;
     CellUse		*newuse;
     SearchContext	scx;
     CellUse		*flatDestUse;
     
    destname = cmd->tx_argv[cmd->tx_argc - 1];
    xMask = CU_DESCEND_ALL;
    dolabels = TRUE;
    toplabels = FALSE;

    rval = 0;
    if (cmd->tx_argc > 2)
    {
	int i;
	for (i = 1; i < (cmd->tx_argc - 1); i++)
	{
	    if (!strncmp(cmd->tx_argv[i], "-no", 3))
	    {
		invert = TRUE;
	    }
	    else if (!strncmp(cmd->tx_argv[i], "-do", 3))
	    {
		invert = FALSE;
	    }
	    else
	    {
	        rval = -1;
		break;
	    }

	    if (strlen(cmd->tx_argv[i]) > 3)
	    {
		switch(cmd->tx_argv[i][3])
		{
		    case 'l':
			dolabels = (invert) ? FALSE : TRUE;
			break;
		    case 't':
			toplabels = (invert) ? FALSE : TRUE;
			break;
		    case 's':
			xMask = (invert) ? CU_DESCEND_NO_SUBCKT : CU_DESCEND_ALL;
			break;
		    case 'v':
			xMask = (invert) ? CU_DESCEND_NO_VENDOR : CU_DESCEND_ALL;
			break;
		    default:
			TxError("options are: -nolabels, -nosubcircuits "
				"-novendor, -dotoplabels\n");
			break;
		}
	    }
	}
    }
    else if (cmd->tx_argc != 2)
	rval = -1;

    if (rval != 0)
    {
     	TxError("usage: flatten [-<option>...] destcell\n");
	return;
    }
    /* create the new def */
    if (newdef = DBCellLookDef(destname))
    {
    	 TxError("%s already exists\n",destname);
	 return;
    }
    newdef = DBCellNewDef(destname, (char *) NULL);
    ASSERT(newdef, "CmdFlatten");
    DBCellSetAvail(newdef);
    newuse = DBCellNewUse(newdef, (char *) NULL);
    (void) StrDup(&(newuse->cu_id), "Flattened cell");
    DBSetTrans(newuse, &GeoIdentityTransform);
    newuse->cu_expandMask = CU_DESCEND_SPECIAL;
    UndoDisable();
    flatDestUse = newuse;
    
    if (EditCellUse)
	scx.scx_use  = EditCellUse;
    else
	scx.scx_use = (CellUse *)w->w_surfaceID;

    scx.scx_area = scx.scx_use->cu_def->cd_bbox;
    scx.scx_trans = GeoIdentityTransform;

    DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, xMask, flatDestUse);
    if (dolabels)
	FlatCopyAllLabels(&scx, &DBAllTypeBits, xMask, flatDestUse);
    else if (toplabels)
    {
	int savemask = scx.scx_use->cu_expandMask;
	scx.scx_use->cu_expandMask = CU_DESCEND_SPECIAL;
	DBCellCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_SPECIAL, flatDestUse);
	scx.scx_use->cu_expandMask = savemask;
    }

    if (xMask != CU_DESCEND_ALL)
	DBCellCopyAllCells(&scx, xMask, flatDestUse, (Rect *)NULL);
    
    UndoEnable();
}


