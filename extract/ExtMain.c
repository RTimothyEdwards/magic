/*
 * ExtMain.c --
 *
 * Circuit extraction.
 * Command interface.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtMain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "utils/stack.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "utils/undo.h"

/* Imports from elsewhere in this module */
extern FILE *extFileOpen();

/* ------------------------ Exported variables ------------------------ */

    /*
     * See extract.h for the bit flags that may be set in the following.
     * If any are set, the corresponding warnings get generated, leaving
     * feedback messages.  If this word is zero, only fatal errors are
     * reported.
     */
int ExtDoWarn = EXTWARN_DUP|EXTWARN_FETS;
int ExtOptions = EXT_DOALL;

/* --------------------------- Global data ---------------------------- */

    /* Cumulative yank buffer for hierarchical circuit extraction */
CellUse *extYuseCum = NULL;
CellDef *extYdefCum = NULL;

    /* Identifier returned by the debug module for circuit extraction */
ClientData extDebugID;

    /* Number of errors encountered during extraction */
int extNumFatal;
int extNumWarnings;

    /* Dummy use pointing to def being extracted */
CellUse *extParentUse;

/* ------------------------ Data local to this file ------------------- */

    /* Stack of defs pending extraction */
Stack *extDefStack;

    /* Forward declarations */
int extDefInitFunc(), extDefPushFunc();
void extParents();
void extDefParentFunc();
void extDefParentAreaFunc();
void extExtractStack();

bool extContainsGeometry();
bool extContainsCellFunc();
bool extTimestampMisMatch();

/*
 * ----------------------------------------------------------------------------
 *
 * ExtInit --
 *
 * Initialize the technology-independent part of the extraction module.
 * This procedure should be called once, after the database module has
 * been initialized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the local variables of the extraction module.
 *	Registers the extractor with the debugging module.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtInit()
{
    int n;
    static struct
    {
	char	*di_name;
	int	*di_id;
    } debugFlags[] = {
	"areaenum",	&extDebAreaEnum,
	"array",	&extDebArray,
	"hardway",	&extDebHardWay,
	"hiercap",	&extDebHierCap,
        "hierareacap",	&extDebHierAreaCap,
	"label",	&extDebLabel,
	"length",	&extDebLength,
	"neighbor",	&extDebNeighbor,
	"noarray",	&extDebNoArray,
	"nofeedback",	&extDebNoFeedback,
	"nohard",	&extDebNoHard,
	"nosubcell",	&extDebNoSubcell,
	"perimeter",	&extDebPerim,
	"resist",	&extDebResist,
	"visonly",	&extDebVisOnly,
	"yank",		&extDebYank,
	0
    };

    /* Register ourselves with the debugging module */
    extDebugID =
	    DebugAddClient("extract", sizeof debugFlags/sizeof debugFlags[0]);
    for (n = 0; debugFlags[n].di_name; n++)
	*(debugFlags[n].di_id) =
		DebugAddFlag(extDebugID, debugFlags[n].di_name);

    /* Create the yank buffer used for hierarchical extraction */
    DBNewYank("__ext_cumulative", &extYuseCum, &extYdefCum);

    /* Create the dummy use also used in hierarchical extraction */
    extParentUse = DBCellNewUse(extYdefCum, (char *) NULL);
    DBSetTrans(extParentUse, &GeoIdentityTransform);

    /* Initialize the hash tables used in ExtLength.c */
    extLengthInit();
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtAll --
 *
 * Extract the subtree rooted at the CellDef 'rootUse->cu_def'.
 * Each cell is extracted to a file in the current directory
 * whose name consists of the last part of the cell's path,
 * with a .ext suffix.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a number of .ext files and writes to them.
 *	Adds feedback information where errors have occurred.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtAll(rootUse)
    CellUse *rootUse;
{
    /* Make sure the entire subtree is read in */
    DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox);

    /* Fix up bounding boxes if they've changed */
    DBFixMismatch();

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and push on stack */
    extDefStack = StackNew(100);
    (void) extDefPushFunc(rootUse);

    /* Now extract all the cells we just found */
    extExtractStack(extDefStack, TRUE, rootUse->cu_def);
    StackFree(extDefStack);
}

/*
 * Function to initialize the client data field of all
 * cell defs, in preparation for extracting a subtree
 * rooted at a particular def.
 */
int
extDefInitFunc(def)
    CellDef *def;
{
    def->cd_client = (ClientData) 0;
    return (0);
}

/*
 * Function to push each cell def on extDefStack
 * if it hasn't already been pushed, and then recurse
 * on all that def's children.
 */
int
extDefPushFunc(use)
    CellUse *use;
{
    CellDef *def = use->cu_def;

    if (def->cd_client || (def->cd_flags&CDINTERNAL))
	return (0);

    def->cd_client = (ClientData) 1;
    StackPush((ClientData) def, extDefStack);
    (void) DBCellEnum(def, extDefPushFunc, (ClientData) 0);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtUnique --
 *
 * For each cell in the subtree rooted at rootUse->cu_def, make
 * sure that there are not two different nodes with the same label.
 * If there are, we generate unique names by appending a numeric
 * suffix to all but one of the offending labels.
 * If "option" is 1 (tagged mode), then only labels ending in the
 * character "#" are forced to be unique.  If "option" is 2 (noports
 * mode), then port labels are not forced to be unique.  Finally,
 * if the label has been changed and doesn't end in a '!', we leave
 * feedback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May modify the label lists of some of the cells rooted
 *	at rootUse->cu_def, and mark the cells as CDMODIFIED.
 *	May also leave feedback.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtUnique(rootUse, option)
    CellUse *rootUse;
    int option;
{
    CellDef *def;
    int nwarn;

    /* Make sure the entire subtree is read in */
    DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox);

    /* Fix up bounding boxes if they've changed */
    DBFixMismatch();

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and push on stack */
    extDefStack = StackNew(100);
    (void) extDefPushFunc(rootUse);

    /* Now process all the cells we just found */
    nwarn = 0;
    while (def = (CellDef *) StackPop(extDefStack))
    {
	def->cd_client = (ClientData) 0;
	if (!SigInterruptPending)
	    nwarn += extUniqueCell(def, option);
    }
    StackFree(extDefStack);
    if (nwarn)
	TxError("%d uncorrected errors (see the feedback info)\n", nwarn);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtParents --
 * ExtShowParents --
 *
 * ExtParents extracts the cell use->cu_def and all its parents.
 * ExtShowParents merely finds and prints all the parents without
 * extracting them.
 *
 * As in ExtAll, each cell is extracted to a file in the current
 * directory whose name consists of the last part of the cell's path,
 * with a .ext suffix.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a number of .ext files and writes to them.
 *	Adds feedback information where errors have occurred.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtParents(use)
    CellUse *use;
{
    extParents(use, TRUE);
}

void
ExtShowParents(use)
    CellUse *use;
{
    extParents(use, FALSE);
}

void
extParents(use, doExtract)
    CellUse *use;
    bool doExtract;	/* If TRUE, we extract; if FALSE, we print */
{
    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and push on stack */
    extDefStack = StackNew(100);
    extDefParentFunc(use->cu_def);

    /* Now extract all the cells we just found */
    extExtractStack(extDefStack, doExtract, (CellDef *) NULL);
    StackFree(extDefStack);
}

/*
 * Function to visit all the parents of 'def' and push them on
 * extDefStack.  We only push a def if it is unmarked, ie, its
 * cd_client field is 0.  After pushing a def, we mark it by
 * setting its cd_client field to 1.
 */

void
extDefParentFunc(def)
    CellDef *def;
{
    CellUse *parent;

    if (def->cd_client || (def->cd_flags&CDINTERNAL))
	return;

    def->cd_client = (ClientData) 1;
    StackPush((ClientData) def, extDefStack);
    for (parent = def->cd_parents; parent; parent = parent->cu_nextuse)
	if (parent->cu_parent)
	    extDefParentFunc(parent->cu_parent);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtParentArea --
 *
 * ExtParentArea extracts the cell use->cu_def and each of its
 * parents that contain geometry touching or overlapping the area
 * of use->cu_def.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates one or more .ext files and writes to them.
 *	Adds feedback information where errors have occurred.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtParentArea(use, changedArea, doExtract)
    CellUse *use;	/* Use->cu_def changed; extract its affected parents */
    Rect *changedArea;	/* Area changed in use->cu_def coordinates */
    bool doExtract;	/* If TRUE, we extract; if FALSE, we just print names
			 * of the cells we would extract.
			 */
{
    Rect area;

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /*
     * Recursively visit all defs in the tree
     * and push on stack if they contain any geometry
     * overlapping or touching the area 'changedArea'.
     */
    area = *changedArea;
    area.r_xbot--, area.r_ybot--;
    area.r_xtop++, area.r_ytop++;
    extDefStack = StackNew(100);
    extDefParentAreaFunc(use->cu_def, use->cu_def, (CellUse *) NULL, &area);

    /* Now extract all the cells we just found */
    extExtractStack(extDefStack, doExtract, (CellDef *) NULL);
    StackFree(extDefStack);
}

/*
 * Function to visit all the parents of 'def' and push them on
 * extDefStack.  We only push a def if it is unmarked, ie, its
 * cd_client field is 0, and if it is either 'baseDef' or it
 * contains geometry or other subcells in the area 'area'.
 * We mark each def visited by setting cd_client to 1.
 */

void
extDefParentAreaFunc(def, baseDef, allButUse, area)
    CellDef *def;
    CellDef *baseDef;
    CellUse *allButUse;
    Rect *area;
{
    int x, y, xoff, yoff;
    CellUse *parent;
    Transform t, t2;
    Rect parArea;

    if (def->cd_client || (def->cd_flags&CDINTERNAL))
	return;

    if (def == baseDef || extContainsGeometry(def, allButUse, area))
    {
	def->cd_client = (ClientData) 1;
	StackPush((ClientData) def, extDefStack);
    }

    for (parent = def->cd_parents; parent; parent = parent->cu_nextuse)
    {
	if (parent->cu_parent)
	{
	    for (x = parent->cu_xlo; x <= parent->cu_xhi; x++)
	    {
		for (y = parent->cu_ylo; y <= parent->cu_yhi; y++)
		{
		    xoff = (x - parent->cu_xlo) * parent->cu_xsep;
		    yoff = (y - parent->cu_ylo) * parent->cu_ysep;
		    GeoTranslateTrans(&GeoIdentityTransform, xoff, yoff, &t);
		    GeoTransTrans(&t, &parent->cu_transform, &t2);
		    GeoTransRect(&t2, area, &parArea);
		    extDefParentAreaFunc(parent->cu_parent, baseDef,
				parent, &parArea);
		}
	    }
	}
    }
}

bool
extContainsGeometry(def, allButUse, area)
    CellDef *def;
    CellUse *allButUse;
    Rect *area;
{
    int extContainsPaintFunc();
    bool extContainsCellFunc();
    int pNum;

    if (TiSrArea((Tile *) NULL, def->cd_planes[PL_CELL], area,
			extContainsCellFunc, (ClientData) allButUse))
	return (TRUE);

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
			area, &DBAllButSpaceBits,
			extContainsPaintFunc, (ClientData) NULL))
	    return (TRUE);

    return (FALSE);
}

bool
extContainsCellFunc(tile, allButUse)
    Tile *tile;
    CellUse *allButUse;
{
    CellTileBody *ctb;

    for (ctb = (CellTileBody *) TiGetBody(tile); ctb; ctb = ctb->ctb_next)
	if (ctb->ctb_use != allButUse)
	    return (TRUE);

    return (FALSE);
}

int
extContainsPaintFunc()
{
    return (1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ExtIncremental --
 *
 * Starting at 'rootUse', extract all cell defs that have changed.
 * Right now, we forcibly read in the entire tree before doing the
 * extraction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a number of .ext files and writes to them.
 *	Adds feedback information where errors have occurred.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtIncremental(rootUse)
    CellUse *rootUse;
{
    /* Make sure the entire subtree is read in */
    DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox);

    /* Fix up bounding boxes if they've changed */
    DBFixMismatch();

    /* Update all timestamps */
    DBUpdateStamps();

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /*
     * Recursively visit all defs in the tree
     * and push on stack if they need extraction.
     */
    extDefStack = StackNew(100);
    (void) extDefIncrementalFunc(rootUse);

    /* Now extract all the cells we just found */
    extExtractStack(extDefStack, TRUE, rootUse->cu_def);
    StackFree(extDefStack);
}

/*
 * Function to push each cell def on extDefStack if it hasn't
 * already been pushed and if it needs re-extraction, and then
 * recurse on all that def's children.
 */

int
extDefIncrementalFunc(use)
    CellUse *use;
{
    CellDef *def = use->cu_def;

    if (def->cd_client || (def->cd_flags&CDINTERNAL))
	return (0);

    def->cd_client = (ClientData) 1;
    if (extTimestampMisMatch(def))
	StackPush((ClientData) def, extDefStack);
    (void) DBCellEnum(def, extDefIncrementalFunc, (ClientData) 0);
    return (0);
}

/*
 * Function returning TRUE if 'def' needs re-extraction.
 * This will be the case if either the .ext file for 'def'
 * does not exist, or if its timestamp fails to match that
 * recorded in 'def'.
 */

bool
extTimestampMisMatch(def)
    CellDef *def;
{
    char line[256];
    FILE *extFile;
    bool ret = TRUE;
    int stamp;

    extFile = extFileOpen(def, (char *) NULL, "r", (char **) NULL);
    if (extFile == NULL)
	return (TRUE);

    if (fgets(line, sizeof line, extFile) == NULL) goto closeit;
    if (sscanf(line, "timestamp %d", &stamp) != 1) goto closeit;
    if (def->cd_timestamp != stamp) goto closeit;
    ret = FALSE;

closeit:
    (void) fclose(extFile);
    return (ret);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extExtractStack --
 *
 * If 'doExtract' is TRUE, call ExtCell for each def on the stack 'stack';
 * otherwise, print the name of each def on the stack 'stack'.
 * The root cell of the tree being processed is 'rootDef'; we only
 * extract pathlength information for this cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Leaves 'stack' empty.
 *	Calls ExtCell on each def on the stack if 'doExtract' is TRUE.
 *	Resets cd_client to 0 for each def on the stack.
 *	Prints the total number of errors and warnings.
 *
 * ----------------------------------------------------------------------------
 */

void
extExtractStack(stack, doExtract, rootDef)
    Stack *stack;
    bool doExtract;
    CellDef *rootDef;
{
    int fatal = 0, warnings = 0;
    bool first = TRUE;
    CellDef *def;

    while (def = (CellDef *) StackPop(stack))
    {
	def->cd_client = (ClientData) 0;
	if (!SigInterruptPending)
	{
	    if (doExtract)
	    {
		ExtCell(def, (char *) NULL, (def == rootDef));
		fatal += extNumFatal;
		warnings += extNumWarnings;
	    }
	    else
	    {
		if (!first) TxPrintf(", ");
		TxPrintf("%s", def->cd_name);
		TxFlush();
		first = FALSE;
	    }
	}
    }

    if (!doExtract)
    {
	TxPrintf("\n");
    }
    else
    {
	if (fatal > 0)
	    TxError("Total of %d fatal error%s.\n",
		    fatal, fatal != 1 ? "s" : "");
	if (warnings > 0)
	    TxError("Total of %d warning%s.\n",
		    warnings, warnings != 1 ? "s" : "");
    }
}
