/*
 * CmdAB.c --
 *
 * Commands with names beginning with the letters A through B.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdAB.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "utils/utils.h"
#include "textio/textio.h"
#include "drc/drc.h"
#include "cif/cif.h"
#include "graphics/graphics.h"
#include "textio/txcommands.h"
#include "utils/malloc.h"
#include "utils/netlist.h"
#include "select/select.h"


/* ---------------------------------------------------------------------------
 *
 * CmdAddPath --
 *
 * Implement the "addpath" command:  append to the global cell search path.
 * (Usage superceded by extended "path" command; retained for compatibility)
 *
 * Usage:
 *	addpath path
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The search path used to find cells is appended to with the first
 *	command line argument.  See CmdLQ.CmdPath for more information.
 *
 * History:
 *	Contributed by Doug Pan and Prof. Mark Linton at Stanford.
 *
 * ---------------------------------------------------------------------------
 */
 /*ARGSUSED*/

void
CmdAddPath( w, cmd )
    MagWindow *w;
    TxCommand *cmd;
{
    if (cmd->tx_argc != 2) {
	TxError("Usage: %s appended_search_path\n", cmd->tx_argv[0]);
	return;
    }
    PaAppend(&Path, cmd->tx_argv[1]);
}


/* Linked-list structure for returning information about arrayed cells */

typedef struct LA1
{
    CellUse *cellUse;
    ArrayInfo arrayInfo;
    struct LA1 *ar_next;
} LinkedArray;

/*
 * ----------------------------------------------------------------------------
 *
 * CmdArray --
 *
 * Implement the "array" command.  Make everything in the selection
 * into an array.  For paint and labels, just copy.  For subcells,
 * make each use into an arrayed use.
 *
 * Usage:
 *	array xlo xhi ylo yhi | xsize ysize
 *
 *	array count [xlo xhi ylo yhi | xsize ysize]
 *	array width [value]
 *	array height [value]
 *	array pitch [x y]
 *	array position [x y]
 *	array help
 *
 *	array -list [count | width | height | pitch [x y] | position [x y]]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

#define ARRAY_COUNT	0
#define ARRAY_WIDTH	1
#define ARRAY_HEIGHT	2
#define ARRAY_PITCH	3
#define ARRAY_POSITION	4
#define ARRAY_HELP	5
#define ARRAY_DEFAULT	6

void
CmdArray(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    static char *cmdArrayOption[] = {
	"count 		[[xlo] xhi [ylo] yhi]	array subcells",
	"width 		[value]			set or return array x-spacing",
	"height 	[value]			set or return array y-spacing",
	"pitch 		[x y]			set or return array spacing",
	"position 	[x y]			set or return array origin",
	"help					print help information",
	NULL
    };

    char **msg;
    int option, locargc, argstart;
    bool doList = FALSE;
    ArrayInfo a;
    Rect toolRect;
    LinkedArray *lahead = NULL, *la;
    int xval, yval;

#ifdef MAGIC_WRAPPER
    Tcl_Obj *tobj;
#endif

    extern int selGetArrayFunc();

    locargc = cmd->tx_argc;
    argstart = 1;

    if (locargc <= 1)
	goto badusage;
    else
    {
	if (!strncmp(cmd->tx_argv[argstart], "-list", 5))
	{
	    doList = TRUE;
	    locargc--;
	    argstart++;
	}
        if (locargc <= 1)
	    goto badusage;	/* Prohibits "array -list" alone */
	
	option = Lookup(cmd->tx_argv[argstart], cmdArrayOption);
	if (option < 0) {
	    if (locargc == 3 || locargc == 5)
		option = ARRAY_DEFAULT;
	    else
		goto badusage;
	}
    }
    if (!ToolGetEditBox((Rect *)NULL)) return;

    /* Get all information about cell uses in the current selection */

    (void) SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL, 
		selGetArrayFunc, (ClientData) &lahead);

    /* Note:  All "unimplemented functions" below will require a routine
     * similar to "SelectArray", but which only operates on cells, and
     * only changes specific parameters; i.e., the arrayInfo structure
     * is generated separately for each selected cell, the values filled
     * in with the current values for that cell, and then the requested
     * parameter changed.
     */

    switch (option)
    {
	case ARRAY_COUNT:
	    if (locargc == 2)
	    {
		for (la = lahead; la != NULL; la = la->ar_next)
		{
#ifdef MAGIC_WRAPPER
		    if (doList)
		    {
			tobj = Tcl_NewListObj(0, NULL);
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_xlo));
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_xhi));
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_ylo));
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_yhi));
			Tcl_SetObjResult(magicinterp, tobj);
		    }
		    else
		    {
#endif
			if (la->cellUse->cu_id != NULL)
			    TxPrintf("Cell use \"%s\":", la->cellUse->cu_id);
			else
			    TxPrintf("Cell \"%s\":", la->cellUse->cu_def->cd_name);
			TxPrintf("x index %d to %d, y index %d to %d\n",
				la->arrayInfo.ar_xlo,
				la->arrayInfo.ar_xhi,
				la->arrayInfo.ar_ylo,
				la->arrayInfo.ar_yhi);
#ifdef MAGIC_WRAPPER
		    }
#endif
		}
		break;
	    }
	    else if ((locargc != 4) && (locargc != 6))
		goto badusage;

	    if (!StrIsInt(cmd->tx_argv[argstart + 1])
			|| !StrIsInt(cmd->tx_argv[argstart + 2])) 
		goto badusage;

	    if (locargc == 4)
	    {
		a.ar_xlo = 0;
		a.ar_ylo = 0;
		a.ar_xhi = atoi(cmd->tx_argv[argstart + 1]) - 1;
		a.ar_yhi = atoi(cmd->tx_argv[argstart + 2]) - 1;
		if ( (a.ar_xhi < 0) || (a.ar_yhi < 0) ) goto badusage;
	    }
	    else if (locargc == 6)
	    {
		if (!StrIsInt(cmd->tx_argv[argstart + 3]) || 
			!StrIsInt(cmd->tx_argv[argstart + 4])) goto badusage;
		a.ar_xlo = atoi(cmd->tx_argv[argstart + 1]);
		a.ar_xhi = atoi(cmd->tx_argv[argstart + 2]);
		a.ar_ylo = atoi(cmd->tx_argv[argstart + 3]);
		a.ar_yhi = atoi(cmd->tx_argv[argstart + 4]);
	    }

	    if (!ToolGetBox((CellDef **) NULL, &toolRect))
	    {
		TxError("Position the box to indicate the array spacing.\n");
		return;
	    }
	    a.ar_xsep = toolRect.r_xtop - toolRect.r_xbot;
	    a.ar_ysep = toolRect.r_ytop - toolRect.r_ybot;
	    SelectArray(&a);
	    break;

	case ARRAY_WIDTH:
	    if (locargc == 2)
	    {
		for (la = lahead; la != NULL; la = la->ar_next)
		{
#ifdef MAGIC_WRAPPER
		    if (doList)
		    {
			tobj = Tcl_NewListObj(0, NULL);
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_xsep));
			Tcl_SetObjResult(magicinterp, tobj);
		    }
		    else
		    {
#endif
			if (la->cellUse->cu_id != NULL)
			    TxPrintf("Cell use \"%s\":", la->cellUse->cu_id);
			else
			    TxPrintf("Cell \"%s\":", la->cellUse->cu_def->cd_name);
			TxPrintf("x separation %d\n", la->arrayInfo.ar_xsep);
#ifdef MAGIC_WRAPPER
		    }
#endif
		}
		break;
	    }
	    if ((locargc != 3) || (!StrIsInt(cmd->tx_argv[argstart + 1])))
		goto badusage;

	    xval = atoi(cmd->tx_argv[argstart + 1]);
	    yval = atoi(cmd->tx_argv[argstart + 2]);

	    TxPrintf("Unimplemented function.\n");
	    break;

	case ARRAY_HEIGHT:
	    if (locargc == 2)
	    {
		for (la = lahead; la != NULL; la = la->ar_next)
		{
#ifdef MAGIC_WRAPPER
		    if (doList)
		    {
			tobj = Tcl_NewListObj(0, NULL);
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_ysep));
			Tcl_SetObjResult(magicinterp, tobj);
		    }
		    else
		    {
#endif
			if (la->cellUse->cu_id != NULL)
			    TxPrintf("Cell use \"%s\":", la->cellUse->cu_id);
			else
			    TxPrintf("Cell \"%s\":", la->cellUse->cu_def->cd_name);
			TxPrintf("y separation %d\n", la->arrayInfo.ar_ysep);
#ifdef MAGIC_WRAPPER
		    }
#endif
		}
		break;
	    }
	    if ((locargc != 3) || (!StrIsInt(cmd->tx_argv[argstart + 1])))
		goto badusage;

	    xval = atoi(cmd->tx_argv[argstart + 1]);
	    yval = atoi(cmd->tx_argv[argstart + 2]);

	    TxPrintf("Unimplemented function.\n");
	    break;

	case ARRAY_PITCH:
	    if (locargc == 2)
	    {
		for (la = lahead; la != NULL; la = la->ar_next)
		{
#ifdef MAGIC_WRAPPER
		    if (doList)
		    {
			tobj = Tcl_NewListObj(0, NULL);
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_xsep));
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->arrayInfo.ar_ysep));
			Tcl_SetObjResult(magicinterp, tobj);
		    }
		    else
		    {
#endif
			if (la->cellUse->cu_id != NULL)
			    TxPrintf("Cell use \"%s\":", la->cellUse->cu_id);
			else
			    TxPrintf("Cell \"%s\":", la->cellUse->cu_def->cd_name);
			TxPrintf("x separation %d ", la->arrayInfo.ar_xsep);
			TxPrintf("y separation %d\n", la->arrayInfo.ar_ysep);
#ifdef MAGIC_WRAPPER
		    }
#endif
		}
		break;
	    }
	    if ((locargc != 4) || (!StrIsInt(cmd->tx_argv[argstart + 1])) ||
				(!StrIsInt(cmd->tx_argv[argstart + 2])))
		goto badusage;

	    xval = atoi(cmd->tx_argv[argstart + 1]);
	    yval = atoi(cmd->tx_argv[argstart + 2]);

	    TxPrintf("Unimplemented function.\n");
	    break;

	case ARRAY_POSITION:
	    if (locargc == 2)
	    {
		for (la = lahead; la != NULL; la = la->ar_next)
		{
#ifdef MAGIC_WRAPPER
		    if (doList)
		    {
			tobj = Tcl_NewListObj(0, NULL);
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->cellUse->cu_bbox.r_xbot));
			Tcl_ListObjAppendElement(magicinterp, tobj,
				Tcl_NewIntObj(la->cellUse->cu_bbox.r_ybot));
			Tcl_SetObjResult(magicinterp, tobj);
		    }
		    else
		    {
#endif
			if (la->cellUse->cu_id != NULL)
			    TxPrintf("Cell use \"%s\":", la->cellUse->cu_id);
			else
			    TxPrintf("Cell \"%s\":", la->cellUse->cu_def->cd_name);
			TxPrintf("x=%d ", la->cellUse->cu_bbox.r_xbot);
			TxPrintf("y=%d\n", la->cellUse->cu_bbox.r_ybot);
#ifdef MAGIC_WRAPPER
		    }
#endif
		}
		break;
	    }

	    if ((locargc != 4) || (!StrIsInt(cmd->tx_argv[argstart + 1])) ||
				(!StrIsInt(cmd->tx_argv[argstart + 2])))
		goto badusage;

	    xval = atoi(cmd->tx_argv[argstart + 1]);
	    yval = atoi(cmd->tx_argv[argstart + 2]);

	    TxPrintf("Unimplemented function.\n");
	    break;

	case ARRAY_DEFAULT:
	    if (!StrIsInt(cmd->tx_argv[argstart])
			|| !StrIsInt(cmd->tx_argv[argstart + 1])) 
		    goto badusage;
	    if (locargc == 3)
	    {
		a.ar_xlo = 0;
		a.ar_ylo = 0;
		a.ar_xhi = atoi(cmd->tx_argv[argstart]) - 1;
		a.ar_yhi = atoi(cmd->tx_argv[argstart + 1]) - 1;
		if ( (a.ar_xhi < 0) || (a.ar_yhi < 0) ) goto badusage;
	    }
	    else
	    {
		if (!StrIsInt(cmd->tx_argv[argstart + 2]) || 
			!StrIsInt(cmd->tx_argv[argstart + 3])) goto badusage;
		a.ar_xlo = atoi(cmd->tx_argv[argstart]);
		a.ar_xhi = atoi(cmd->tx_argv[argstart + 1]);
		a.ar_ylo = atoi(cmd->tx_argv[argstart + 2]);
		a.ar_yhi = atoi(cmd->tx_argv[argstart + 3]);
	    }

	    if (!ToolGetBox((CellDef **) NULL, &toolRect))
	    {
		TxError("Position the box to indicate the array spacing.\n");
		return;
	    }
	    a.ar_xsep = toolRect.r_xtop - toolRect.r_xbot;
	    a.ar_ysep = toolRect.r_ytop - toolRect.r_ybot;
	    SelectArray(&a);
	    break;

	case ARRAY_HELP:
badusage:
	    for (msg = &(cmdArrayOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    break;
    }

freelist:
    la = lahead;
    while (la != NULL)
    {
	freeMagic((char *)la);
	la = la->ar_next;
    }
    return;
}

/* ----------------------------------------------------------------------------
 * Procedure returning information on arrayed selection
 */

int
selGetArrayFunc(selUse, use, trans, arg)
   CellUse *selUse;
   CellUse *use;
   Transform *trans;
   LinkedArray **arg;
{
    /* Check "use" for array information and pass this to arrayInfo */
  
    LinkedArray *la;
    int xlo, xhi, ylo, yhi, xsep, ysep, t;

    la = (LinkedArray *)mallocMagic(sizeof(LinkedArray));

    xlo = use->cu_xlo;
    ylo = use->cu_ylo;
    xhi = use->cu_xhi;
    yhi = use->cu_yhi;

    /* preserve x and y relative to root */

    if (trans->t_a == 0)
    {
	t = xlo; xlo = ylo; ylo = t;
	t = xhi; xhi = yhi; yhi = t;
    }

    la->arrayInfo.ar_xlo = xlo;
    la->arrayInfo.ar_xhi = xhi;
    la->arrayInfo.ar_ylo = ylo;
    la->arrayInfo.ar_yhi = yhi;
    
    /* Reverse the transformation in DBMakeArray */

    ysep = (trans->t_d * use->cu_xsep - trans->t_a * use->cu_ysep);
    ysep /= (trans->t_d * trans->t_b - trans->t_a * trans->t_e);
 
    if (trans->t_a == 0)
	xsep = (use->cu_ysep - trans->t_e * ysep) / trans->t_d;
    else
	xsep = (use->cu_xsep - trans->t_b * ysep) / trans->t_a;
    
    la->arrayInfo.ar_xsep = xsep;
    la->arrayInfo.ar_ysep = ysep;

    la->cellUse = use;
    la->ar_next = (*arg);
    (*arg) = la;

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdBox --
 *
 * Box command.
 *
 * Usage:
 *	box width [num]
 *	box height [num]
 *	box size [width height]
 *	box position [llx lly] [-edit]
 *	box values [llx lly urx ury] [-edit]
 *
 *	box <direction> <distance> | cursor
 *
 *	box move <direction> <distance> | cursor
 *	box grow <direction> <distance>
 *	box shrink <direction> <distance>
 *
 *	box [llx lly urx ury] [-edit]
 *
 *	Coordinates and sizes may be specified in any of the following
 *	units:  none (internal), "i" (internal), "l" (lambda), or any
 *	metric units (e.g., "um").  Coordinates may be integer or
 *	floating-point (e.g., "20i" or "1.5mm").  There should be no
 *	space between the value and the unit specifier (i.e., "20l",
 *	not "20 l").
 *
 *	Coordinates are usually reported relative to the root cell of
 *	the window, unless the "-edit" switch is given.
 *
 * Results:
 *	Box values are printed to the console.
 *	Tcl version returns values for the first five cases above when
 *	the optional value or values are not present.
 *
 * Side effects:
 *	May modify the location of the box tool.
 *
 * ----------------------------------------------------------------------------
 */

#define BOX_WIDTH	0
#define BOX_HEIGHT	1
#define BOX_SIZE	2
#define BOX_POSITION	3
#define BOX_VALUES	4
#define BOX_MOVE	5
#define BOX_GROW	6
#define BOX_SHRINK	7
#define BOX_CORNER	8
#define BOX_EXISTS	9
#define BOX_HELP	10
#define BOX_DEFAULT	11

void
CmdBox(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    static char *cmdBoxOption[] = {
	"width		[value]			set or return box width",
	"height		[value]			set or return box height",
	"size		[width height]		set or return box size",
	"position		[llx lly] [-edit]	set or return box position",
	"values		[llx lly urx ury] [-edit]	set or return box coordinates",
	"move		<direction> <distance> 	move box position",
	"grow		<direction> <distance>	expand box size",
	"shrink		<direction> <distance>  shrink box size",
	"corner		<direction> <distance>	set box corner",
	"exists					is the cursor box present?",
	"help					print help information",
	NULL
    };

    CellDef *rootBoxDef;
    Rect rootBox, editbox, *boxptr;
    Point ll;
    int option, direction, distancex, distancey;
    int width, height;
    dlong area;
    int argc;
    int tcorner;
    float iscale, oscale;
    bool needBox = TRUE;		/* require that box be defined */
    bool refEdit = FALSE;		/* referenced to edit cell coordinates */
    bool cursorRef = FALSE;		/* reference position is the cursor */
    char **msg;

    argc = cmd->tx_argc;
    if (argc > 7) goto badusage;

    /*----------------------------------------------------------*/
    /* Check for the request to report in edit cell coordinates */
    /*----------------------------------------------------------*/

    if (!strncmp(cmd->tx_argv[argc - 1], "-edit", 5))
    {
	refEdit = TRUE;
	argc--;
    }

    /*----------------------------------------------------------*/
    /* Parse command for options				*/
    /*----------------------------------------------------------*/

    if (argc == 1)
	option = BOX_DEFAULT;
    else
    {
	option = Lookup(cmd->tx_argv[1], cmdBoxOption);

	/* This hack allows 'h' to be used as a synonym for "height"	*/
	/* 'w' already works for width because no other options	begin	*/
	/* with "w".  But the addition of option "help" presents an	*/
	/* ambiguity.							*/

	if (option == -1 && cmd->tx_argv[1][0] == 'h')
	    option = BOX_HEIGHT;
	else if (option < 0)
	    option = BOX_DEFAULT;
    }

    windCheckOnlyWindow(&w, DBWclientID);

    /*----------------------------------------------------------*/
    /* Check for the command options which do not require a box	*/
    /* to be present.						*/
    /*----------------------------------------------------------*/

    switch (option)
    {
	case BOX_MOVE:
	case BOX_GROW:
	case BOX_SHRINK:
	case BOX_CORNER:
	    if (!strncmp(cmd->tx_argv[argc - 1], "cursor", 6))
	    {
		needBox = FALSE;
		cursorRef = TRUE;
	    }
	    break;
	case BOX_DEFAULT:
	    if (argc == 5) needBox = FALSE;
	    break;
	case BOX_VALUES:
	    if (argc == 6) needBox = FALSE;
	    break;
	case BOX_EXISTS:
#ifdef MAGIC_WRAPPER
	    Tcl_SetResult(magicinterp, ToolGetBox(NULL, NULL) ?  "1" : "0",
			NULL);
#else
	    TxPrintf("%s\n", ToolGetBox(NULL, NULL) ? "True" : "False");
#endif
	    return;
    }
    
    if (needBox)
    {
	if (refEdit)
	    if (!ToolGetEditBox(&editbox))
		return;

	if (!ToolGetBox(&rootBoxDef, &rootBox))
	{
	    TxError("Box tool must be present\n");
	    return;
	}
    }
    else if (w == NULL)
    {
	TxError("Cursor not in a window.\n");
	return;
    }
    else
    {
	Rect r;

	if (argc >= 5)
	{
	    r.r_xbot = cmdParseCoord(w, cmd->tx_argv[argc - 4], FALSE, TRUE);
	    r.r_ybot = cmdParseCoord(w, cmd->tx_argv[argc - 3], FALSE, FALSE);
	    r.r_xtop = cmdParseCoord(w, cmd->tx_argv[argc - 2], FALSE, TRUE);
	    r.r_ytop = cmdParseCoord(w, cmd->tx_argv[argc - 1], FALSE, FALSE);
	}
	else
	{
	    /* If we got here, then we requested a box or corner move
	     * to the cursor position, but there was no box present.
	     * To keep users from becoming bewildered at this point,
	     * we generate a zero-size box at the origin and work from
	     * there.
	     */
	    r.r_xbot = r.r_ybot = r.r_xtop = r.r_ytop = 0;
	}

	rootBoxDef = ((CellUse *) w->w_surfaceID)->cu_def;
	if (refEdit)
	{
	    GeoTransRect(&EditToRootTransform, &r, &rootBox);
	    refEdit = FALSE;
	}
	else rootBox = r;
    }

    /*----------------------------------------------------------*/
    /* Edit cell or Root cell?					*/
    /*----------------------------------------------------------*/

    boxptr = (refEdit) ? &editbox : &rootBox;

    /*----------------------------------------------------------*/
    /* Parse arguments according to class			*/
    /*----------------------------------------------------------*/

    switch (option)
    {
	case BOX_MOVE:
	case BOX_GROW:
	case BOX_SHRINK:
	case BOX_CORNER:
	    if (argc != 4) goto badusage;
	    direction = GeoNameToPos(cmd->tx_argv[2], FALSE, TRUE);
	    if (direction < 0) return;
	    else if (cursorRef)
	    {
		switch (direction)
		{
		    case GEO_SOUTHWEST:
			tcorner = TOOL_BL;
			break;
		    case GEO_NORTHEAST:
			tcorner = TOOL_TR;
			break;
		    case GEO_NORTHWEST:
			tcorner = TOOL_TL;
			break;
		    case GEO_SOUTHEAST:
			tcorner = TOOL_BR;
			break;
		}
		switch(option)
		{
		    case BOX_MOVE:
			ToolMoveBox(tcorner, &cmd->tx_p, TRUE, rootBoxDef);
			break;
		    case BOX_CORNER:
			ToolMoveCorner(tcorner, &cmd->tx_p, TRUE, rootBoxDef);
			break;
		}
		return;
	    }
	    else if (DBWSnapToGrid != DBW_SNAP_USER)
	    {
		distancex = cmdParseCoord(w, cmd->tx_argv[3], TRUE, FALSE);
		distancey = distancex;
	    }
	    else
	    {
		switch (direction)
		{
		    case GEO_EAST: case GEO_WEST:
			distancex = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
			distancey = distancex;
			break;
		    case GEO_NORTH: case GEO_SOUTH:
			distancey = cmdParseCoord(w, cmd->tx_argv[3], TRUE, FALSE);
			distancex = distancey;
			break;
		    default:
			distancex = cmdParseCoord(w, cmd->tx_argv[3], TRUE, TRUE);
			distancey = cmdParseCoord(w, cmd->tx_argv[3], TRUE, FALSE);
			break;
		}
	    }
	    if ((distancex == 0) && (distancey == 0)) return;
	    break;
    }

    /*----------------------------------------------------------*/

    switch (option)
    {
	case BOX_WIDTH:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		char *boxvalues = (char *)Tcl_Alloc(50);
		sprintf(boxvalues, "%d",
			boxptr->r_xtop - boxptr->r_xbot);
		Tcl_SetResult(magicinterp, boxvalues, TCL_DYNAMIC);
#else
		TxPrintf("%s box width is %d\n",
			(refEdit) ? "Edit" : "Root",
			boxptr->r_xtop - boxptr->r_xbot);
#endif
		return;
	    }
	    else if (argc != 3) goto badusage;
	    width = cmdParseCoord(w, cmd->tx_argv[2], TRUE, TRUE);
	    boxptr->r_xtop = boxptr->r_xbot + width;
	    break;

	case BOX_HEIGHT:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		char *boxvalues = (char *)Tcl_Alloc(50);
		sprintf(boxvalues, "%d",
			boxptr->r_ytop - boxptr->r_ybot);
		Tcl_SetResult(magicinterp, boxvalues, TCL_DYNAMIC);
#else
		TxPrintf("%s box height is %d\n",
			(refEdit) ? "Edit" : "Root",
			boxptr->r_ytop - boxptr->r_ybot);
#endif
		return;
	    }
	    else if (argc != 3) goto badusage;
	    height = cmdParseCoord(w, cmd->tx_argv[2], TRUE, FALSE);
	    boxptr->r_ytop = boxptr->r_ybot + height;
	    break;

	case BOX_SIZE:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		char *boxvalues = (char *)Tcl_Alloc(50);
		sprintf(boxvalues, "%d %d",
			boxptr->r_xtop - boxptr->r_xbot,
			boxptr->r_ytop - boxptr->r_ybot);
		Tcl_SetResult(magicinterp, boxvalues, TCL_DYNAMIC);
#else
		TxPrintf("%s box size is %d x %d\n",
			(refEdit) ? "Edit" : "Root",
			boxptr->r_xtop - boxptr->r_xbot,
			boxptr->r_ytop - boxptr->r_ybot);
#endif
		return;
	    }
	    else if (argc != 4) goto badusage;
	    width = cmdParseCoord(w, cmd->tx_argv[2], TRUE, TRUE);
	    height = cmdParseCoord(w, cmd->tx_argv[3], TRUE, FALSE);
	    boxptr->r_xtop = boxptr->r_xbot + width;
	    boxptr->r_ytop = boxptr->r_ybot + height;
	    break;

	case BOX_POSITION:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		char *boxvalues = (char *)Tcl_Alloc(50);
		sprintf(boxvalues, "%d %d",
			boxptr->r_xbot, boxptr->r_ybot);
		Tcl_SetResult(magicinterp, boxvalues, TCL_DYNAMIC);
#else
		TxPrintf("%s box lower-left corner at (%d, %d)\n",
			(refEdit) ? "Edit" : "Root",
			boxptr->r_xbot, boxptr->r_ybot);
#endif
		return;
	    }
	    else if (argc != 4) goto badusage;
	    width = boxptr->r_xtop - boxptr->r_xbot;
	    height = boxptr->r_ytop - boxptr->r_ybot;
	    ll.p_x = cmdParseCoord(w, cmd->tx_argv[2], FALSE, TRUE);
	    ll.p_y = cmdParseCoord(w, cmd->tx_argv[3], FALSE, FALSE);
	    boxptr->r_xbot = ll.p_x;
	    boxptr->r_ybot = ll.p_y;
	    boxptr->r_xtop = boxptr->r_xbot + width;
	    boxptr->r_ytop = boxptr->r_ybot + height;
	    break;

	case BOX_VALUES:
	    if (argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		char *boxvalues = (char *)Tcl_Alloc(50);
		sprintf(boxvalues, "%d %d %d %d",
			boxptr->r_xbot, boxptr->r_ybot,
			boxptr->r_xtop, boxptr->r_ytop);
		Tcl_SetResult(magicinterp, boxvalues, TCL_DYNAMIC);
#else
		TxPrintf("%s box coordinates (%d, %d) to (%d, %d)\n",
			(refEdit) ? "Edit" : "Root",
			boxptr->r_xbot, boxptr->r_ybot,
			boxptr->r_xtop, boxptr->r_ytop);
#endif
		return;
	    }
	    else if (argc != 6) goto badusage;
	    break;

	case BOX_MOVE:
	    switch (direction)
	    {
		case GEO_NORTH:
		    boxptr->r_ybot += distancey;
		    boxptr->r_ytop += distancey;
		    break;
		case GEO_SOUTH:
		    boxptr->r_ybot -= distancey;
		    boxptr->r_ytop -= distancey;
		    break;
		case GEO_EAST:
		    boxptr->r_xbot += distancex;
		    boxptr->r_xtop += distancex;
		    break;
		case GEO_WEST:
		    boxptr->r_xbot -= distancex;
		    boxptr->r_xtop -= distancex;
		    break;
		case GEO_NORTHEAST:
		    boxptr->r_ybot += distancey;
		    boxptr->r_ytop += distancey;
		    boxptr->r_xbot += distancex;
		    boxptr->r_xtop += distancex;
		    break;
		case GEO_NORTHWEST:
		    boxptr->r_ybot += distancey;
		    boxptr->r_ytop += distancey;
		    boxptr->r_xbot -= distancex;
		    boxptr->r_xtop -= distancex;
		    break;
		case GEO_SOUTHEAST:
		    boxptr->r_ybot -= distancey;
		    boxptr->r_ytop -= distancey;
		    boxptr->r_xbot += distancex;
		    boxptr->r_xtop += distancex;
		    break;
		case GEO_SOUTHWEST:
		    boxptr->r_ybot -= distancey;
		    boxptr->r_ytop -= distancey;
		    boxptr->r_xbot -= distancex;
		    boxptr->r_xtop -= distancex;
		    break;
	    }
	    break;

	case BOX_GROW:
	    switch (direction)
	    {
		case GEO_NORTH:
		    boxptr->r_ytop += distancey;
		    break;
		case GEO_SOUTH:
		    boxptr->r_ybot -= distancey;
		    break;
		case GEO_EAST:
		    boxptr->r_xtop += distancex;
		    break;
		case GEO_WEST:
		    boxptr->r_xbot -= distancex;
		    break;
		case GEO_NORTHEAST:
		    boxptr->r_ytop += distancey;
		    boxptr->r_xtop += distancex;
		    break;
		case GEO_NORTHWEST:
		    boxptr->r_ytop += distancey;
		    boxptr->r_xbot -= distancex;
		    break;
		case GEO_SOUTHEAST:
		    boxptr->r_ybot -= distancey;
		    boxptr->r_xtop += distancex;
		    break;
		case GEO_SOUTHWEST:
		    boxptr->r_ybot -= distancey;
		    boxptr->r_xbot -= distancex;
		    break;
		case GEO_CENTER:
		    boxptr->r_ytop += distancey;
		    boxptr->r_ybot -= distancey;
		    boxptr->r_xtop += distancex;
		    boxptr->r_xbot -= distancex;
		    break;
	    }
	    break;

	case BOX_CORNER:
	    ll.p_x = cmdParseCoord(w, cmd->tx_argv[2], FALSE, TRUE);
	    ll.p_y = cmdParseCoord(w, cmd->tx_argv[3], FALSE, FALSE);
	    switch (direction)
	    {
		case GEO_SOUTHWEST:
		    tcorner = TOOL_BL;
		    break;
		case GEO_NORTHEAST:
		    tcorner = TOOL_TR;
		    break;
		case GEO_NORTHWEST:
		    tcorner = TOOL_TL;
		    break;
		case GEO_SOUTHEAST:
		    tcorner = TOOL_BR;
		    break;
	    }
	    ToolMoveCorner(tcorner, &ll, FALSE, rootBoxDef);
	    return;

	case BOX_SHRINK:
	    switch (direction)
	    {
		case GEO_NORTH:
		    boxptr->r_ytop -= distancey;
		    break;
		case GEO_SOUTH:
		    boxptr->r_ybot += distancey;
		    break;
		case GEO_EAST:
		    boxptr->r_xtop -= distancex;
		    break;
		case GEO_WEST:
		    boxptr->r_xbot += distancex;
		    break;
		case GEO_NORTHEAST:
		    boxptr->r_ytop -= distancey;
		    boxptr->r_xtop -= distancex;
		    break;
		case GEO_NORTHWEST:
		    boxptr->r_ytop -= distancey;
		    boxptr->r_xbot += distancex;
		    break;
		case GEO_SOUTHEAST:
		    boxptr->r_ybot += distancey;
		    boxptr->r_xtop -= distancex;
		    break;
		case GEO_SOUTHWEST:
		    boxptr->r_ybot += distancey;
		    boxptr->r_xbot += distancex;
		    break;
		case GEO_CENTER:
		    boxptr->r_ytop -= distancey;
		    boxptr->r_xtop -= distancex;
		    boxptr->r_ybot += distancey;
		    boxptr->r_xbot += distancex;
		    break;
	    }
	    break;

	case BOX_DEFAULT:

	    /*----------------------------------------------------------*/
	    /* Print box values to the screen ("box" w/no options only)	*/
	    /*----------------------------------------------------------*/

	    width = boxptr->r_xtop - boxptr->r_xbot;
	    height = boxptr->r_ytop - boxptr->r_ybot;
	    area = (dlong)width * (dlong)height;

	    TxPrintf("%s cell box:\n", (refEdit) ? "Edit" : "Root");

	    iscale = (float)DBLambda[0] / (float)DBLambda[1];
	    oscale = CIFGetOutputScale(1000);	/* 1000 for conversion to um */

	    TxPrintf("           width x height  (   llx,  lly  ), (   urx,  ury  )");
	    if (area > 0)
		TxPrintf("  area (units^2)");

	    TxPrintf("\n\nmicrons:  %6.2f x %-6.2f  (% 6.2f, % -6.2f), "
		"(% 6.2f, % -6.2f)",
		(float)width *oscale, (float)height * oscale,
		(float)boxptr->r_xbot * oscale, (float)boxptr->r_ybot * oscale,
		(float)boxptr->r_xtop * oscale, (float)boxptr->r_ytop * oscale);
	    if (area > 0)
		TxPrintf("  %-10.2f", (float)area * oscale * oscale);

	    TxPrintf("\nlambda:");
	    if (DBLambda[0] != DBLambda[1])
	    {
		TxPrintf("   %6.2f x %-6.2f  (% 6.2f, % -6.2f), (% 6.2f, % -6.2f)",
			(float)width * iscale, (float)height * iscale,
			(float)boxptr->r_xbot * iscale,
			(float)boxptr->r_ybot * iscale,
			(float)boxptr->r_xtop * iscale,
			(float)boxptr->r_ytop * iscale);
		if (area > 0)
		    TxPrintf("  %-10.2f", (float)area * iscale * iscale);
		TxPrintf("\ninternal:");
	    }
	    else
		TxPrintf("  ");

	    TxPrintf(" %6d x %-6d  (% 6d, % -6d), (% 6d, % -6d)",
			width, height,
			boxptr->r_xbot, boxptr->r_ybot,
			boxptr->r_xtop, boxptr->r_ytop);
	    if (area > 0)
		TxPrintf("  %-10lld", area);
	    TxPrintf("\n");
	    break;

	case BOX_HELP:
badusage:
	    for (msg = &(cmdBoxOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    return;
    }

    /*----------------------------------------------------------*/
    /* Return to root coordinates, if working in edit coords	*/
    /*----------------------------------------------------------*/

    if (refEdit)
	GeoTransRect(&EditToRootTransform, &editbox, &rootBox);

    /*----------------------------------------------------------*/
    /* Change the position of the box in the layout window	*/
    /*----------------------------------------------------------*/

    if (argc != 1)	/* Don't bother to move if only reporting */
    {
	ToolMoveBox(TOOL_BL, &rootBox.r_ll, FALSE, rootBoxDef);
	ToolMoveCorner(TOOL_TR, &rootBox.r_ur, FALSE, rootBoxDef);
    }
    return;
}

/*----------------------------------------------------------*/
