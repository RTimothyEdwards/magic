/*
 * CmdSubrs.c --
 *
 * The functions in this file are local to the commands module
 * and not intended to be used by its clients.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/commands/CmdSubrs.c,v 1.2 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>		/* For round() function */

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "database/database.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "cif/cif.h"
#include "drc/drc.h"
#include "textio/txcommands.h"
#include "utils/undo.h"
#include "utils/macros.h"
#include "sim/sim.h"
#include "select/select.h"
#ifdef SCHEME_INTERPRETER
#include "lisp/lisp.h"
#endif

/* Forward declarations */

extern char *cmdCheckNewName();
extern int cmdSaveWindSet();
extern void CmdSetWindCaption();

TileTypeBitMask CmdYMLabel;
TileTypeBitMask CmdYMCell;
TileTypeBitMask CmdYMAllButSpace;

/*
 * ----------------------------------------------------------------------------
 *
 * cmdScaleCoord --
 *
 * Replaces the use of atoi() in command parsing of coordinates.  Allows
 * coordinates to be declared in internal units, lambda, user units, or
 * microns, and translates to internal units accordingly.  It also allows
 * coordinates to be specified in floating-point.
 *    A suffix of "i" indicates internal units, a suffix of "l" indicates
 * lambda, a suffix of "g" indicates the user grid, and a suffix in metric
 * notation ("nm", "um", "mm", "cm") indicates natural units.  Other valid
 * units are "cu" or "centimicrons" for centimicrons, or "microns" for um.
 *    Units without any suffix are assumed to be in lambda if "snap"
 * (DBWSnapToGrid) is set to lambda, grid units if "snap" is set to the
 * user grid, and internal units otherwise.
 *
 *    MagWindow argument w is used only with grid-based snapping, to find
 * the value of the grid for the given window.  In this case, because the
 * grid specifies an offset, "is_relative" specifies whether the given
 * coordinate is a relative measurement (ignore offset) or an absolute
 * position.  Because the grid can be different in X and Y, "is_x"
 * specifies whether the given distance is in the X (TRUE) or Y (FALSE)
 * direction.
 *
 *    This is the "general-purpose" routine, taking a value "scale" so
 * that returned units can be a fraction of internal units, e.g., to
 * represent wire centerlines (scale = 2) or highlights (any scale).
 *
 * Results:
 *      Integer representing the given coordinate in internal units,
 *	multiplied up by "scale".
 *
 * Side effects:
 *	None
 *
 * ----------------------------------------------------------------------------
 */

int
cmdScaleCoord(w, arg, is_relative, is_x, scale)
    MagWindow *w;
    char *arg;
    bool is_relative, is_x;
    int scale;
{
    char *endptr;
    double dval = 0;
    int mscale = 1;
    DBWclientRec *crec;

    if (*arg == '{') arg++;
    while (isspace(*arg)) arg++;

    dval = strtod(arg, &endptr);
    dval *= (double)scale;

    if (endptr == arg)
    {
        /* strtod() error condition */
	TxError("Coordinate value cannot be parsed:  assuming 0\n");
	return 0;
    }

    else if ((*endptr == 'l')
		|| ((*endptr == '\0') && (DBWSnapToGrid == DBW_SNAP_LAMBDA)))
    {
	/* lambda or default units */
	dval *= (double)DBLambda[1];
	dval /= (double)DBLambda[0];
	return round(dval);
    }
    else if ((*endptr == 'i')
		|| ((*endptr == '\0') && (DBWSnapToGrid == DBW_SNAP_INTERNAL)))
    {
	/* internal units */
	return round(dval);
    }
    else if ((*endptr == 'g')
		|| ((*endptr == '\0') && (DBWSnapToGrid == DBW_SNAP_USER)))
    {
	/* grid units */
	if (w == (MagWindow *)NULL)
	{
	    windCheckOnlyWindow(&w, DBWclientID);
	    if (w == (MagWindow *)NULL)
		return round(dval);		/* Default, if window is unknown */
	}
	crec = (DBWclientRec *) w->w_clientData;
	if (is_x)
	{
	    dval *= (double)(crec->dbw_gridRect.r_xtop
			- crec->dbw_gridRect.r_xbot);
	    if (!is_relative)
		dval += (double)crec->dbw_gridRect.r_xbot;
	}
	else
	{
	    dval *= (double)(crec->dbw_gridRect.r_ytop
			- crec->dbw_gridRect.r_ybot);
	    if (!is_relative)
		dval += (double)crec->dbw_gridRect.r_ybot;
	}
	return round(dval);
    }
    else
    {
        /* natural units referred to the current cifoutput style */
	if (*(endptr + 1) == 'm')
	{
	    switch (*endptr)
	    {
		case 'n':
		    mscale = 1;
		    break;
		case 'u':
		    mscale = 1000;
		    break;
		case 'm':
		    mscale = 1000000;
		    break;
		case 'c':
		    mscale = 10000000;
		    break;
		default:
		    TxError("Unknown metric prefix \"%cm\"; assuming internal units\n",
				*endptr);
		    return round(dval);
	    }
	}
	else if (!strncmp(endptr, "micron", 6))
	    mscale = 1000;
	else if (!strncmp(endptr, "centimicron", 11) || !strcmp(endptr, "cu"))
	    mscale = 10;
	else if (!isspace(*endptr))
	{
	    TxError("Unknown coordinate type \"%s\"; assuming internal units\n",
			endptr);
	    return round(dval);
	}
    }
    dval /= CIFGetOutputScale(mscale);
    return round(dval);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdParseCoord ---
 *
 *	This is the "normal" usage, calling cmdScaleCoord at a scale of 1.
 *	This routine should be used in all circumstances where the result
 *	is expected in internal units.
 *
 * Results:
 *      Integer representing the given coordinate in internal units
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
cmdParseCoord(w, arg, is_relative, is_x)
    MagWindow *w;
    char *arg;
    bool is_relative, is_x;
{
    return cmdScaleCoord(w, arg, is_relative, is_x, 1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdInit --
 *
 * Initialization for the commands module.
 * All we do now is set up the TileTypeBitMasks CmdYMLabel, CmdYMCell
 * and CmdYMAllButSpace.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdInit()
{
    TTMaskZero(&CmdYMLabel);
    TTMaskSetType(&CmdYMLabel, L_LABEL);

    TTMaskZero(&CmdYMCell);
    TTMaskSetType(&CmdYMCell, L_CELL);

    CmdYMAllButSpace = DBAllButSpaceBits;
    TTMaskClearType(&CmdYMAllButSpace, L_CELL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdFlushCell --
 *
 * Throw away all changes made within Magic to the specified cell,
 * and re-read it from disk.  If no cell is specified, the default
 * is the current edit cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	THIS IS NOT UNDO-ABLE!
 *	Modifies the specified CellDef, but marks it as being unmodified.
 *	All parents of the CellDef are re-DRC'ed over both the old and
 *	new areas of the cell.
 *
 * ----------------------------------------------------------------------------
 */

void
cmdFlushCell(def)
    CellDef *def;
{
    CellUse *parentUse;

    /* Disallow flushing a cell that contains the edit cell as a child */
    if (EditCellUse && (EditCellUse->cu_parent == def))
    {
	TxError("Cannot flush cell whose subcell is being edited.\n");
	TxError("%s not flushed\n", def->cd_name);
	return;
    }

    UndoFlush();
    DBWAreaChanged(def, &def->cd_bbox, DBW_ALLWINDOWS,
	(TileTypeBitMask *) NULL);
    for (parentUse = def->cd_parents; parentUse != NULL;
	parentUse = parentUse->cu_nextuse)
    {
	if (parentUse->cu_parent == NULL) continue;
	DRCCheckThis(parentUse->cu_parent, TT_CHECKSUBCELL,
	    &parentUse->cu_bbox);
    }
    DBCellClearDef(def);
    DBCellClearAvail(def);
    (void) DBCellRead(def, (char *) NULL, TRUE, NULL);
    DBCellSetAvail(def);
    DBReComputeBbox(def);
    DBCellSetModified(def, FALSE);
    DBWAreaChanged(def, &def->cd_bbox, DBW_ALLWINDOWS,
	&DBAllButSpaceBits);
    for (parentUse = def->cd_parents; parentUse != NULL;
	parentUse = parentUse->cu_nextuse)
    {
	if (parentUse->cu_parent == NULL) continue;
	DRCCheckThis(parentUse->cu_parent, TT_CHECKSUBCELL,
	    &parentUse->cu_bbox);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdParseLayers --
 *
 * Convert a string specifying a collection of layers into a TileTypeBitMask
 * representing the layers specified.
 *
 * A special layer, '$', refers to all tile types underneath the point
 * tool, except for the DRC "CHECKxxx" types.
 *
 * The layer '*' refers to all tile types except for "check-this" and
 * the label and cell pseudo-types.
 *
 * Results:
 *	TRUE on success, FALSE if any layers are unrecognized.
 *
 * Side effects:
 *	Prints an error message if any layers are unrecognized.
 *	Sets bits in 'mask' according to layers in layer specification.
 *	Leaves 'mask' set to 0 if any layers are unrecognized.
 *
 *	Eventually, this routine should return a "minimal" TileTypeBitMask,
 *	ie, one with the minimum number of bits set consistent with the
 *	string supplied it.
 *
 * ----------------------------------------------------------------------------
 */

bool
CmdParseLayers(s, mask)
    char *s;
    TileTypeBitMask *mask;
{
    TileTypeBitMask newmask, tempmask;
    char *dp, c;
    char name[50];
    TileType type;
    Rect rootRect;
    MagWindow *window;
    DBWclientRec *crec;
    bool adding = TRUE;
    int which, i;
#define LN_CELL		0
#define LN_LABELS	1
#define LN_ALL		2
#define LN_DOLLAR	3
#define LN_ERRORS	4
#define LN_CONNECT	5
    static struct
    {
	char *layer_name;
	int layer_value;
    }
    special[] =
    {
	"$",		LN_DOLLAR,
	"*",		LN_ALL,
	"errors",	LN_ERRORS,
	"labels",	LN_LABELS,
	"subcell",	LN_CELL,
	"connect",	LN_CONNECT,
	0,
    };


    TTMaskZero(mask);
    while (c = *s++)
    {
	switch (c)
	{
	    case '-':
		adding = FALSE;
		continue;
	    case '+':
		adding = TRUE;
		continue;
	    case ',':
	    case ' ':
		continue;
	}

	dp = name; *dp++ = c;
	while (*s && *s != ',' && *s != '+' && *s != '-' && *s != ' ')
	    *dp++ = *s++;
	*dp = '\0';
	if (name[0] == '\0')
	    continue;

	TTMaskZero(&newmask);

	type = DBTechNameTypes(name, &newmask);
	if (type == -2)
	{
	    which = LookupStruct(name, (LookupTable *) special, sizeof special[0]);
	    if (which >= 0)
	    {
		switch (special[which].layer_value)
		{
		    case LN_LABELS:
			TTMaskSetType(&newmask, L_LABEL);
			break;
		    case LN_CELL:
			TTMaskSetType(&newmask, L_CELL);
			break;
		    /*
		     * All layers currently beneath the point tool.
		     * Currently, neither labels nor cells are ever included
		     * in this.
		     */
    		    case LN_DOLLAR:
			window = CmdGetRootPoint((Point *) NULL, &rootRect);
			if ((window == (MagWindow *) NULL)
			    	|| (window->w_client != DBWclientID))
			    return (FALSE);
			crec = (DBWclientRec *) window->w_clientData;
			DBSeeTypesAll(((CellUse *)window->w_surfaceID), 
				&rootRect, crec->dbw_bitmask, &newmask);
			TTMaskAndMask(&newmask, &crec->dbw_visibleLayers);
			tempmask = DBAllButSpaceAndDRCBits;
			TTMaskSetType(&tempmask, TT_SPACE);
			TTMaskAndMask(&newmask, &tempmask);
			break;
		    /*
		     * Everything but labels and subcells
		     */
		    case LN_ALL:
			newmask = DBAllButSpaceAndDRCBits;
			TTMaskClearType(&newmask, L_LABEL);
			TTMaskClearType(&newmask, L_CELL);
			break;
		    /*
		     * All DRC error layers.
		     */
		    case LN_ERRORS:
			TTMaskSetType(&newmask, TT_ERROR_P);
			TTMaskSetType(&newmask, TT_ERROR_S);
			TTMaskSetType(&newmask, TT_ERROR_PS);
			break;
		    /*
		     * Add in all layers connected to layers already parsed
		     */
		    case LN_CONNECT:
			for (type = TT_TECHDEPBASE; type < DBNumTypes; type++)
			    if (TTMaskHasType(mask, type))
			    {
				TileType ttype;
				for (ttype = TT_TECHDEPBASE; ttype < DBNumTypes; ttype++)
				    if (DBConnectsTo(type, ttype))
					TTMaskSetType(&newmask, ttype);
			    }
			break;
		}
	    }
	    else
	    {
		TxError("Unrecognized layer: %s\n", name);
printTypes:
		DBTechPrintTypes(&DBAllButSpaceAndDRCBits, FALSE);
		for (i = 0; ; i++)
		{
		    if (special[i].layer_name == NULL) break;
		    TxError("    %s\n", special[i].layer_name);
		}
		return (FALSE);
	    }
	}
	else if (type == -1)
	{
	    TxError("Ambiguous layer: %s\n", name);
	    goto printTypes;
	}

	if (adding)
	{
	    TTMaskSetMask(mask, &newmask);
	}
	else
	{
	    TTMaskClearMask(mask, &newmask);
	}
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdMaskToType --
 *
 * Convert a TileTypeBitMask into a TileType.
 *
 * Results:
 *	Returns -1 if more than one type bit is set in the TileTypeBitMask;
 *	otherwise, returns the TileType of the bit set.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

TileType
cmdMaskToType(mask)
    TileTypeBitMask *mask;
{
    TileType type, t;

    type = -1;
    for (t = TT_SELECTBASE; t < DBNumTypes; t++)
    {
	if (TTMaskHasType(mask, t))
	{
	    if (type >= 0)
		return (-1);
	    type = t;
	}
    }

    if (type < 0)
	return (TT_SPACE);
    return (type);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdSaveCell --
 *
 * Save a given cell out to disk.
 * If a filename is given, the cell is written out to that file;
 * otherwise, the cell is written out to the file stored with the
 * cellDef, or to a newly created file of the same name as the
 * cellDef.  If there is no name associated with the cell, the
 * save is disallowed.
 *
 * The name of the cell is set to the filename, if it is specified.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the cell out to a disk file.
 *	Clears the modified bit in the cd_flags.
 *
 * ----------------------------------------------------------------------------
 */

void
cmdSaveCell(cellDef, newName, noninteractive, tryRename)
    CellDef *cellDef;	/* Pointer to def of cell to be saved */
    char *newName;	/* Pointer to name of file in which cell is to be
			 * saved.  May be NULL, in which case the name from
			 * the CellDef is taken.
			 */
    bool noninteractive;/* If true, try hard but don't ask the user 
			 * questions.
			 */
    bool tryRename;	/* We should rename the cell to the name of the
			 * place where it was saved.
			 */
{
    /* Eliminate the phony labels added for use by rsim */
#ifndef NO_SIM_MODULE
    SimEraseLabels();
#endif

    /*
     * Whenever the "unnamed" cell is saved, the name of the
     * cell changes to the name of the file in which it was
     * saved.
     */
    
    if (strcmp(cellDef->cd_name, UNNAMED) == 0)
    {
	if (newName == NULL)
	    TxPrintf("Must specify name for cell %s.\n", UNNAMED);
	newName = cmdCheckNewName(cellDef, newName, TRUE, noninteractive);
	if (newName == NULL) return;
    }
    else if (newName != NULL)
    {
	newName = cmdCheckNewName(cellDef, newName, TRUE, noninteractive);
	if (newName == NULL) return;
    }
    else
    {
	if (cellDef->cd_file == NULL)
	{
	    newName = cmdCheckNewName(cellDef, cellDef->cd_name,
		TRUE, noninteractive);
	    if (newName == NULL) return;
	}
    }

    DBUpdateStamps();
    if (!DBCellWrite(cellDef, newName))
    {
	TxError("Could not write file.  Cell not written.\n");
	return;
    }

    if (!tryRename || (newName == NULL) || (strcmp(cellDef->cd_name, newName) == 0))
	return;

    /* Rename the cell */
    if (!DBCellRenameDef(cellDef, newName))
    {
	/* This should never happen */
	TxError("Magic error: there is already a cell named \"%s\"\n",
		    newName);
	return;
    }

    if (EditCellUse && (cellDef == EditCellUse->cu_def))
    {
	/*
	 * The cell is the edit cell.
	 * All windows with compatible roots should show
	 * a caption of "root EDITING edit"
	 */
	CmdSetWindCaption(EditCellUse, EditRootDef);
    }
    else
    {
	/*
	 * The cell is not the edit cell.
	 * We want to find all windows for which this is
	 * the root cell and update their captions.
	 */
	(void) WindSearch(DBWclientID, (ClientData) NULL, (Rect *) NULL, 
		cmdSaveWindSet, (ClientData) cellDef);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdCheckNewName --
 *
 * Get the name of the file in which the argument CellDef is to
 * be saved, if a name was not already provided.
 * If the name of the file is different from the name of the cell,
 * check to make sure that the file doesn't already exist.
 * If the CellDef is to be renamed after saving it, check to make
 * sure that no cell already exists by the new name.
 *
 * Results:
 *	Returns a pointer to a string holding the filename in which
 *	the cell is to be saved, or NULL if the save should be aborted.
 *
 * Side effects:
 *	May prompt the user for a new file name if one is required.
 *	If the filename returned was one typed by the user in response to
 *	this prompt, it overwrites the previous such filename returned,
 *	as it is stored in a static array.
 *
 * ----------------------------------------------------------------------------
 */

char *
cmdCheckNewName(def, newName, tryRename, noninteractive)
    CellDef *def;
    char *newName;
    bool tryRename;
    bool noninteractive;
{
    static char newname[256];
    static char *yesno[] = { "no", "yes", 0 };
    char *filename;
    char *prompt;
    int code;
    FILE *f;

again:
    if (newName == NULL)
    {
	if (noninteractive) {
	    TxError("Can't write file named '%s'\n", def->cd_name);
	    return NULL;
	};
	TxPrintf("File for cell %s: [hit return to abort save] ", def->cd_name);
	if (TxGetLine(newname, sizeof newname) == NULL || newname[0] == '\0')
	{
	    TxPrintf("Cell not saved.\n");
	    return ((char *) NULL);
	}
	if (CmdIllegalChars(newname, "[],", "Cell name"))
	    goto again;
	newName = newname;
    }

    /* Remove any ".mag" file extension from the name */
    else if (!strcmp(newName + strlen(newName) - 4, ".mag"))
	*(newName + strlen(newName) - 4) = '\0';

    if (strcmp(newName, def->cd_name) != 0)
    {
	if (f = PaOpen(newName, "r", DBSuffix, ".", (char *) NULL, &filename))
	{
	    (void) fclose(f);
	    if (noninteractive) {
		TxError("Overwriting file '%s' with cell '%s'\n", filename,
		    def->cd_name);
	    }
	    else {
		prompt = TxPrintString("File %s already exists.\n"
			"  Overwrite it with %s? ", filename, def->cd_name);
		code = TxDialog(prompt, yesno, 0);

		if (code == 0)
		{
		    /* No -- don't overwrite */
		    newName = NULL;
		    goto again;
		}
	    }
	}

	if (tryRename && DBCellLookDef(newName) != NULL)
	{
	    TxError("Can't rename cell '%s' to '%s' because that cell already exists.\n",
	    def->cd_name, newName);
	    if (noninteractive) return NULL;
	    newName = NULL;
	    goto again;
	}
    }

    return (newName);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdSaveWindSet --
 *
 * Filter function for cmdSaveCell() above.
 * Called by WindSearch() with each window.
 *
 * The idea is to change only those captions in windows whose root
 * uses are instances of the def 'def'.  Sets the caption of each such
 * window to:
 *
 *	def [NOT BEING EDITED]
 *
 * Results:
 *	Always 0 to keep the search going.
 *
 * Side effects:
 *	Modifies captions and clientData for the window
 *
 * ----------------------------------------------------------------------------
 */

int
cmdSaveWindSet(window, def)
    MagWindow *window;
    CellDef *def;
{
    char caption[200];
    CellDef *rootDef;

    rootDef = ((CellUse *) window->w_surfaceID)->cu_def;
    if (rootDef != def)
	return 0;

    (void) sprintf(caption, "%s [NOT BEING EDITED]", def->cd_name);
    (void) StrDup(&window->w_iconname, def->cd_name);
    WindCaption(window, caption);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdSetWindCaption --
 *
 * Update the captions of all windows to reflect a new EditCell.
 * The caption of each window having the same root cell def as the
 * one in which the Edit Cell was selected is set to show that the
 * window is subediting the new edit cell.  The captions in all
 * other windows show that these windows are not subediting the
 * edit cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies captions and clientData for each window.
 *
 * ----------------------------------------------------------------------------
 */

/*
 * The following are used to pass information down to the filter
 * function applied by WindSearch(), since it is not intended that
 * CmdSetWindCaption() be re-entrant.
 */

static CellDef *newEditDef;	/* Pointer to new edit cell def */
static CellDef *newRootDef;	/* Pointer to root def of window in which
				 * new edit cell was selected.  This is
				 * used to determine whether the edit cell
				 * is being edited in a window or not.
				 */

void
CmdSetWindCaption(newEditUse, rootDef)
    CellUse *newEditUse;	/* Pointer to new edit cell use */
    CellDef *rootDef;		/* Root cell def of the window in which the
				 * edit cell was selected.
				 */
{
    int cmdWindSet();

    newEditDef = (newEditUse) ? newEditUse->cu_def : NULL;
    newRootDef = rootDef;
    (void) WindSearch(DBWclientID, (ClientData) NULL, (Rect *) NULL, 
	    cmdWindSet, (ClientData) 0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * cmdWindSet --
 *
 * Filter function for CmdSetWindCaption() above.
 * Called by WindSearch() with each window.
 *
 * If the window is compatible with the new edit cell, sets the caption
 * for that window to be
 *
 *	RootDef EDITING EditDef
 *
 * Otherwise, sets the caption to be
 *
 *	RootDef [NOT BEING EDITED]
 *
 * Results:
 *	Always 0 to keep the function going.
 *
 * Side effects:
 *	Modifies captions and clientData for the window
 *
 * ----------------------------------------------------------------------------
 */

int
cmdWindSet(window)
    MagWindow *window;
{
    char caption[200];
    CellDef *wDef;

    wDef = ((CellUse *) window->w_surfaceID)->cu_def;
    if (wDef != newRootDef)
	(void) sprintf(caption, "%s [NOT BEING EDITED]", wDef->cd_name);
    else {
	(void) sprintf(caption, "%s EDITING %s", wDef->cd_name, 
		newEditDef->cd_name);
#ifdef SCHEME_INTERPRETER
        /* Add a binding to scheme variable "edit-cell" */
        LispSetEdit (newEditDef->cd_name);
#endif
    }

    (void) StrDup(&window->w_iconname, wDef->cd_name);
    WindCaption(window, caption);
    return 0;
}


/*
 * ----------------------------------------------------------------------------
 *
 * CmdGetRootPoint --
 *
 * Get the window containing the point tool, and return (in root cell
 * coordinates for that window) the coordinates of the point, and of
 * a minimum-grid-size rectangle enclosing the point.
 *
 * Results:
 *	Pointer to window containing the point tool, or NULL if the
 *	point tool is not present.
 *
 * Side effects:
 *	Sets *point to be the coordinates of the point tool in root
 *	coordinates, and *rect to be the minimum-grid-size enclosing
 *	rectangle.
 *
 *	Prints an error message if the point is not found.
 *
 * ----------------------------------------------------------------------------
 */

MagWindow *
CmdGetRootPoint(point, rect)
    Point *point;
    Rect *rect;
{
    MagWindow *window;

    window = ToolGetPoint(point, rect);
    if (window == (MagWindow *) NULL)
	TxError("Crosshair not in a valid window for this command\n");

    return (window);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGetEditPoint --
 *
 * Get the window containing the point tool, and return (in edit cell
 * coordinates for that window) the coordinates of the point, and of
 * a minimum-grid-size rectangle enclosing the point.
 *
 * Results:
 *	Pointer to window containing the point tool, or NULL if the
 *	point tool is not present.
 *
 * Side effects:
 *	Sets *point to be the coordinates of the point tool in edit
 *	coordinates, and *rect to be the minimum-grid-size enclosing
 *	rectangle.
 *
 * ----------------------------------------------------------------------------
 */

MagWindow *
CmdGetEditPoint(point, rect)
    Point *point;
    Rect *rect;
{
    MagWindow *window;
    Rect rootRect;
    Point rootPoint;

    window = CmdGetRootPoint(&rootPoint, &rootRect);
    if (window != (MagWindow *) NULL)
    {
	GeoTransRect(&RootToEditTransform, &rootRect, rect);
	GeoTransPoint(&RootToEditTransform, &rootPoint, point);
    }

    return (window);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdWarnWrite --
 *
 * Check to see if there are modified and unwritten cells and ask the
 * user whether he wants to stay in magic or lose all these cells.
 *
 * Results:
 *	TRUE if the user wishes to continue anyway without writing the
 *	modified cells out to disk, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
CmdWarnWrite()
{
    int count, code;
    int cmdWarnWriteFunc();
    static char *yesno[] = { "no", "yes", 0 };
    char *prompt;

    count = 0;
    (void) DBCellSrDefs(CDMODIFIED|CDBOXESCHANGED|CDSTAMPSCHANGED,
	cmdWarnWriteFunc, (ClientData) &count);
    if (count == 0)
	return TRUE;

    prompt = TxPrintString("%d Magic cell%s been modified.\n  Do you"
		" want to exit magic and lose %s? ", count,
		count == 1 ? " has" : "s have",
		count == 1 ? "it" : "them");		
    code = TxDialog(prompt, yesno, 0);
    return (code) ? TRUE : FALSE;
}

int
cmdWarnWriteFunc(cellDef, pcount)
    CellDef *cellDef;
    int *pcount;
{
    if ((cellDef->cd_flags & CDINTERNAL) == 0)
	(*pcount)++;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * cmdExpandOneLevel --
 *
 *	Expand (unexpand) a cell, and unexpand all of its children.  This is 
 *	called by commands such as getcell, expand current cell, and load.
 *	Don't bother to unexpand children if we are unexpanding this cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
cmdExpandOneLevel(cu, bitmask, expand)
    CellUse *cu;
    int bitmask;
    bool expand;
{
    extern int cmdExpand1func();

    /* first, expand this cell use */
    DBExpand(cu, bitmask, expand);

    /* now, unexpand its direct children (ONE LEVEL ONLY) */
    if (expand)
	(void) DBCellEnum(cu->cu_def, cmdExpand1func, (ClientData) bitmask);
}

int
cmdExpand1func(cu, bitmask)
    CellUse *cu;
    ClientData bitmask;
{
    DBExpand(cu, (int) bitmask, FALSE);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGetSelectedCell --
 *
 * 	This procedure returns a pointer to the selected cell.
 *
 * Results:
 *	The return value is a pointer to the selected cell.  If more
 *	than one cell is selected, the upper-leftmost cell is returned.
 *	If no cell is selected, NULL is returned.
 *
 * Side effects:
 *	If pTrans isn't NULL, the area it points to is modified to hold
 *	the transform from coords of the selected cell to root coords.
 *
 * ----------------------------------------------------------------------------
 */

Transform *cmdSelTrans;		/* Shared between CmdGetSelectedCell and
				 * cmdGetCellFunc.
				 */

CellUse *
CmdGetSelectedCell(pTrans)
    Transform *pTrans;		/* If non-NULL, transform from selected
				 * cell to root coords is stored here.
				 */
{
    CellUse *result = NULL;
    int cmdGetSelFunc();		/* Forward declaration. */

    cmdSelTrans = pTrans;
    (void) SelEnumCells(FALSE, (bool *) NULL, (SearchContext *) NULL,
	cmdGetSelFunc, (ClientData) &result);
    return result;
}

	/* ARGSUSED */
int
cmdGetSelFunc(selUse, realUse, transform, pResult)
    CellUse *selUse;		/* Not used. */
    CellUse *realUse;		/* The first selected use. */
    Transform *transform;	/* Transform from coords of realUse to root. */
    CellUse **pResult;		/* Store realUse here. */
{
    *pResult = realUse;
    if (cmdSelTrans != NULL)
	*cmdSelTrans = *transform;
    return 1;			/* Skip any other selected cells. */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdIllegalChars --
 *
 * 	Checks a string for any of a number of illegal characters.
 *	If any is found, it's printed in an error message.
 *
 * Results:
 *	TRUE is returned if any of the characters in "illegal" is
 *	also in "string", or if "string" contains any control or
 *	non-ASCII characters.	Otherwise, FALSE is returned.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
CmdIllegalChars(string, illegal, msg)
    char *string;		/* String to check for illegal chars. */
    char *illegal;		/* String containing illegal chars. */
    char *msg;			/* String identifying what string is
				 * supposed to represent, for ease in
				 * printing error messages.
				 */
{
    char *p, *bad;

    for (p = string; *p != 0; p++)
    {
	if (!isascii(*p)) goto error;
	if (iscntrl(*p)) goto error;
	for (bad = illegal; *bad != 0; bad++)
	{
	    if (*bad == *p) goto error;
	}
	continue;

	error:
	if (!isascii(*p) || iscntrl(*p))
	{
	    TxError("%s contains illegal control character 0x%x\n",
		   msg, *p);
	}
	else TxError("%s contains illegal character \"%c\"\n",
		msg, *p);
	return TRUE;
    }
    return FALSE;
}

