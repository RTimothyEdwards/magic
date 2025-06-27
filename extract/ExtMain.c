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
     * feedback messages.  If this word is zero, only errors are
     * reported.  (Update:  "do labelcheck" and "do aliases" have been
     * added to the default options because they generally are required
     * and would only need to be disabled under exceptional circumstances.)
     */
int ExtDoWarn = EXTWARN_DUP|EXTWARN_FETS;
int ExtOptions = EXT_DOALL|EXT_DOLABELCHECK|EXT_DOALIASES;
char *ExtLocalPath = NULL;

/* --------------------------- Global data ---------------------------- */

    /* Cumulative yank buffer for hierarchical circuit extraction */
CellUse *extYuseCum = NULL;
CellDef *extYdefCum = NULL;

    /* Identifier returned by the debug module for circuit extraction */
ClientData extDebugID;

    /* Number of errors encountered during extraction */
int extNumErrors;
int extNumWarnings;

    /* Dummy use pointing to def being extracted */
CellUse *extParentUse;

/* ------------------------ Data local to this file ------------------- */

typedef struct _linkedDef {
    CellDef *ld_def;
    struct _linkedDef *ld_next;
} LinkedDef;

/* Linked list structure to use to store the substrate plane from each	*/ 
/* extracted CellDef so that they can be returned to the original after	*/
/* extraction.								*/

struct saveList {
    Plane *sl_plane;
    CellDef *sl_def;
    struct saveList *sl_next;
};

    /* Stack of defs pending extraction */
Stack *extDefStack;

    /* Forward declarations */
int extDefInitFunc();
void extDefPush();
void extDefIncremental();
void extParents();
void extDefParentFunc();
void extDefParentAreaFunc();
void extExtractStack();

bool extContainsGeometry();
int extContainsCellFunc(CellUse *use, ClientData cdata); /* cb_database_srcellplanearea_t (const CellUse *allButUse) */
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
	{"areaenum",	&extDebAreaEnum},
	{"array",	&extDebArray},
	{"hardway",	&extDebHardWay},
	{"hiercap",	&extDebHierCap},
	{"hierareacap",	&extDebHierAreaCap},
	{"label",	&extDebLabel},
	{"length",	&extDebLength},
	{"neighbor",	&extDebNeighbor},
	{"noarray",	&extDebNoArray},
	{"nofeedback",	&extDebNoFeedback},
	{"nohard",	&extDebNoHard},
	{"nosubcell",	&extDebNoSubcell},
	{"perimeter",	&extDebPerim},
	{"resist",	&extDebResist},
	{"visonly",	&extDebVisOnly},
	{"yank",	&extDebYank},
	{0}
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
 * Simple callback returns 1 if any cell is not marked with the CDDONTUSE
 * flag.  If DBCellEnum() returns 0 then all children of the CellDef are
 * marked CDDONTUSE.
 *
 * ----------------------------------------------------------------------------
 */

int
extIsUsedFunc(use, clientData)
    CellUse *use;
    ClientData clientData;	/* unused */
{
    CellDef *def = use->cu_def;

    /* If any cell is not marked CDDONTUSE then return 1 and stop the search. */
    if (!(def->cd_flags & CDDONTUSE)) return 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * Simple callback returns 1 if any non-space tile is found in a cell.
 * Used to determine if a cell does not need extracting because it is
 * an empty placeholder cell created when flattening part of a vendor
 * GDS, and exists only to reference the GDS file location for writing
 * GDS.
 *
 * ----------------------------------------------------------------------------
 */

int
extEnumFunc(tile, plane)
    Tile *tile;
    int *plane;
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ----------------------------------------------------------------------------
 */

int
extDefListFunc(use, defList)
    CellUse *use;
    LinkedDef **defList;
{
    CellDef *def = use->cu_def;
    LinkedDef *newLD;

    /* Ignore all internal cells and cells that are marked "don't use" */
    if (def->cd_flags & (CDINTERNAL | CDDONTUSE)) return 0;

    /* Recurse to the bottom first */
    (void) DBCellEnum(def, extDefListFunc, (ClientData)defList);

    /* Don't add cells that have already been visited */
    if (def->cd_client) return 0;

    /* Mark self as visited */
    def->cd_client = (ClientData) 1;

    /* Check if all descendents are marked as "don't use" */
    if (DBCellEnum(def, extIsUsedFunc, (ClientData)NULL) == 0)
    {
	int plane;

	/* Nothing below this cell had anything to extract.	*/
	/* Check this cell for paint.  If it has none, then	*/
	/* ignore it.						*/

	for (plane = PL_TECHDEPBASE; plane < DBNumPlanes; plane++)
	    if (DBSrPaintArea((Tile *)NULL, def->cd_planes[plane], &TiPlaneRect,
			&DBAllButSpaceAndDRCBits, extEnumFunc, (ClientData)NULL))
		break;

	if (plane == DBNumPlanes)
	{
	    /* Cell has no material.  Mark it and return. */
	    def->cd_flags |= CDDONTUSE;
	    return 0;
	}
    }

    /* When done with descendents, add self to the linked list */
    
    newLD = (LinkedDef *)mallocMagic(sizeof(LinkedDef));
    newLD->ld_def = def;
    newLD->ld_next = *defList;
    *defList = newLD;

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * ----------------------------------------------------------------------------
 */

int
extDefListFuncIncremental(use, defList)
    CellUse *use;
    LinkedDef **defList;
{
    CellDef *def = use->cu_def;
    LinkedDef *newLD;

    /* Ignore all internal cells and cells marked "don't use" */
    if (def->cd_flags & (CDINTERNAL | CDDONTUSE)) return 0;

    /* Mark cells that don't need updating */
    if (!extTimestampMisMatch(def))
	def->cd_flags |= CDNOEXTRACT;

    /* Recurse to the bottom first */
    (void) DBCellEnum(def, extDefListFuncIncremental, (ClientData)defList);

    /* Don't add cells that have already been visited */
    if (def->cd_client) return 0;

    /* Mark self as visited */
    def->cd_client = (ClientData) 1;

    /* Check if all descendents are marked as "don't use" */
    if (DBCellEnum(def, extIsUsedFunc, (ClientData)NULL) == 0)
    {
	int plane;

	/* Nothing below this cell had anything to extract.	*/
	/* Check this cell for paint.  If it has none, then	*/
	/* ignore it.						*/

	for (plane = PL_TECHDEPBASE; plane < DBNumPlanes; plane++)
	    if (DBSrPaintArea((Tile *)NULL, def->cd_planes[plane], &TiPlaneRect,
			&DBAllButSpaceAndDRCBits, extEnumFunc, (ClientData)NULL))
		break;

	if (plane == DBNumPlanes)
	{
	    /* Cell has no material.  Mark it and return. */
	    def->cd_flags |= CDDONTUSE;
	    return 0;
	}
    }

    /* When done with descendents, add self to the linked list */
    
    newLD = (LinkedDef *)mallocMagic(sizeof(LinkedDef));
    newLD->ld_def = def;
    newLD->ld_next = *defList;
    *defList = newLD;

    return 0;
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
    LinkedDef *defList = NULL;
    CellDef *err_def;

    /* Make sure the entire subtree is read in */
    err_def = DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox, TRUE);
    if (err_def != NULL)
    {
	TxError("Failure to read entire subtree of cell.\n");
	TxError("Failed on cell %s.\n", err_def->cd_name);
	return;
    }

    /* Fix up bounding boxes if they've changed */
    DBFixMismatch();

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and create a linked list	*/
    /* from bottommost back up to the top.				*/

    extDefListFunc(rootUse, &defList);

    /* Sanity check---print wrning if there is nothing to extract */
    if (defList == (LinkedDef *)NULL)
    {
	TxError("Warning:  There is nothing here to extract.\n");
	return;
    }

    /* Now reverse the list onto a stack such that the bottommost cell	*/
    /* is the first to be extracted, and so forth back up to the top.	*/

    extDefStack = StackNew(100);
    extDefPush(defList);

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
 * Function to reverse the linked list of CellDefs to extract by
 * pushing each cell def from the list onto extDefStack.
 */

void
extDefPush(defList)
    LinkedDef *defList;
{
    while (defList != NULL)
    {
    	StackPush((ClientData)defList->ld_def, extDefStack);
	free_magic1_t mm1 = freeMagic1_init();
	freeMagic1(&mm1, defList);
	defList = defList->ld_next;
	freeMagic1_end(&mm1);
    }
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
 * character "#" are forced to be unique.  If "option" is 2 ("noports"
 * mode), then port labels are not forced to be unique.  If "option"
 * is 3 ("notopports" mode), then port labels on the top level are
 * not forced to be unique.  Finally, if the label has been changed
 * and doesn't end in a '!', we leave feedback.
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
    CellDef *def, *err_def;
    LinkedDef *defList = NULL;
    int nwarn;
    int locoption;

    /* Make sure the entire subtree is read in */
    err_def = DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox, TRUE);
    if (err_def != NULL)
    {
	TxError("Failure to read entire subtree of cell.\n");
	TxError("Failed on cell %s.\n", err_def->cd_name);
	return;
    }

    /* Fix up bounding boxes if they've changed */
    DBFixMismatch();

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and create a linked list	*/
    /* from bottommost back up to the top.				*/

    extDefListFunc(rootUse, &defList);

    /* Now reverse the list onto a stack such that the bottommost cell	*/
    /* is the first to be extracted, and so forth back up to the top.	*/

    extDefStack = StackNew(100);
    extDefPush(defList);

    /* Now process all the cells we just found */
    nwarn = 0;
    while ((def = (CellDef *) StackPop(extDefStack)))
    {
	/* EXT_UNIQ_NOTOPPORTS:  Use EXT_UNIQ_ALL on all cells other than the top */
	if ((option == EXT_UNIQ_NOTOPPORTS) &&
		    (StackLook(extDefStack) != (ClientData)NULL))
	    locoption = EXT_UNIQ_ALL;
	else
	    locoption = option;
	    
	def->cd_client = (ClientData) 0;
	if (!SigInterruptPending)
	    nwarn += extUniqueCell(def, locoption);
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
    LinkedDef *defList = NULL;
    CellDef *def;
    Plane *saveSub;
    struct saveList *newsl, *sl = (struct saveList *)NULL;

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* All children of use->cu_def need substrate preparation */
    extDefListFunc(use, &defList);

    /* use->cu_def is on the top of the list, so remove it */ 
    {
	free_magic1_t mm1 = freeMagic1_init();
	freeMagic1(&mm1, defList);
	defList = defList->ld_next;
	freeMagic1_end(&mm1);
    }

    while (defList != NULL)
    {
	def = defList->ld_def;
	saveSub = extPrepSubstrate(def);
	if (saveSub != NULL)
	{
	    newsl = (struct saveList *)mallocMagic(sizeof(struct saveList));
	    newsl->sl_plane = saveSub;
	    newsl->sl_def = def;
	    newsl->sl_next = sl;
	    sl = newsl;
	}

	free_magic1_t mm1 = freeMagic1_init();
	freeMagic1(&mm1, defList);
	defList = defList->ld_next;
	freeMagic1_end(&mm1);
    }

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and push on stack */
    extDefStack = StackNew(100);
    extDefParentFunc(use->cu_def);

    /* Now extract all the cells we just found */
    extExtractStack(extDefStack, doExtract, (CellDef *) NULL);
    StackFree(extDefStack);

    /* Replace any modified substrate planes in use->cu_def's children */
    {
	free_magic1_t mm1 = freeMagic1_init();
	for (; sl; sl = sl->sl_next)
	{
	    ExtRevertSubstrate(sl->sl_def, sl->sl_plane);
	    freeMagic1(&mm1, sl);
	}
	freeMagic1_end(&mm1);
    }
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

/*
 * ----------------------------------------------------------------------------
 *
 * ExtractOneCell ---
 *
 * Extract a single cell by preparing the substrate plane of all of its
 * children, calling ExtCell(), then restoring the substrate planes of
 * all the cells.
 *
 * ----------------------------------------------------------------------------
 */

void
ExtractOneCell(def, outName, doLength)
    CellDef *def;       /* Cell being extracted */
    char *outName;      /* Name of output file; if NULL, derive from def name */
    bool doLength;      /* If TRUE, extract pathlengths from drivers to
                         * receivers (the names are stored in ExtLength.c).
                         * Should only be TRUE for the root cell in a
                         * hierarchy.
                         */
{
    LinkedDef *defList = NULL;
    CellUse dummyUse;
    CellDef *subDef;
    Plane *savePlane;
    struct saveList *newsl, *sl = (struct saveList *)NULL;

    dummyUse.cu_def = def;

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    extDefListFunc(&dummyUse, &defList);

    /* def is on top of the list, so remove it */
    {
        free_magic1_t mm1 = freeMagic1_init();
        freeMagic1(&mm1, defList);
        defList = defList->ld_next;
        freeMagic1_end(&mm1);
    }

    /* Prepare substrates of all children of def */
    while (defList != NULL)
    {
        subDef = defList->ld_def;
        savePlane = extPrepSubstrate(subDef);
        if (savePlane != NULL)
        {
            newsl = (struct saveList *)mallocMagic(sizeof(struct saveList));
            newsl->sl_plane = savePlane;
            newsl->sl_def = subDef;
            newsl->sl_next = sl;
            sl = newsl;
        }
        free_magic1_t mm1 = freeMagic1_init();
        freeMagic1(&mm1, defList);
        defList = defList->ld_next;
        freeMagic1_end(&mm1);
    }

    savePlane = ExtCell(def, outName, doLength);

    /* Restore all modified substrate planes */

    if (savePlane != NULL) ExtRevertSubstrate(def, savePlane);
    free_magic1_t mm1 = freeMagic1_init();
    for (; sl; sl = sl->sl_next)
    {
        ExtRevertSubstrate(sl->sl_def, sl->sl_plane);
        freeMagic1(&mm1, sl);
    }
    freeMagic1_end(&mm1);
}

/* ------------------------------------------------------------------------- */

bool
extContainsGeometry(def, allButUse, area)
    CellDef *def;
    CellUse *allButUse;
    Rect *area;
{
    int extContainsPaintFunc();
    int pNum;

    if (DBSrCellPlaneArea(def->cd_cellPlane, area,
			extContainsCellFunc, PTR2CD(allButUse)))
	return (TRUE);

    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	if (DBSrPaintArea((Tile *) NULL, def->cd_planes[pNum],
			area, &DBAllButSpaceBits,
			extContainsPaintFunc, (ClientData) NULL))
	    return (TRUE);

    return (FALSE);
}

/* ------------------------------------------------------------------------- */

/** @typedef cb_database_srcellplanearea_t */
int
extContainsCellFunc(
    CellUse *use,
    ClientData cdata)
{
    const CellUse *allButUse = (CellUse *)CD2PTR(cdata);
    return (use != allButUse) ? TRUE : FALSE;
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
    LinkedDef *defList = NULL;
    CellDef *err_def;

    /* Make sure the entire subtree is read in */
    err_def = DBCellReadArea(rootUse, &rootUse->cu_def->cd_bbox, TRUE);
    if (err_def != NULL)
    {
	TxError("Failure to read entire subtree of cell.\n");
	TxError("Failed on cell %s.\n", err_def->cd_name);
	return;
    }

    /* Fix up bounding boxes if they've changed */
    DBFixMismatch();

    /* Update all timestamps */
    DBUpdateStamps(NULL);

    /* Mark all defs as being unvisited */
    (void) DBCellSrDefs(0, extDefInitFunc, (ClientData) 0);

    /* Recursively visit all defs in the tree and create a linked list	*/
    /* from bottommost back up to the top.				*/

    extDefListFuncIncremental(rootUse, &defList);

    /* Now reverse the list onto a stack such that the bottommost cell	*/
    /* is the first to be extracted, and so forth back up to the top.	*/

    extDefStack = StackNew(100);
    extDefPush(defList);

    /* Now extract all the cells we just found */
    extExtractStack(extDefStack, TRUE, rootUse->cu_def);
    StackFree(extDefStack);
}

/*
 * ----------------------------------------------------------------------------
 * Function returning TRUE if 'def' needs re-extraction.
 * This will be the case if either the .ext file for 'def'
 * does not exist, or if its timestamp fails to match that
 * recorded in 'def'.
 * ----------------------------------------------------------------------------
 */

bool
extTimestampMisMatch(def)
    CellDef *def;
{
    char line[256];
    FILE *extFile;
    bool ret = TRUE;
    int stamp;
    bool doLocal;

    doLocal = (ExtLocalPath == NULL) ? FALSE : TRUE;

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
    int errorcnt = 0, warnings = 0;
    bool first = TRUE;
    Plane *savePlane;
    CellDef *def;
    struct saveList *newsl, *sl = (struct saveList *)NULL;

    while ((def = (CellDef *) StackPop(stack)))
    {
	def->cd_client = (ClientData) 0;
	if (!SigInterruptPending)
	{
	    if (doExtract)
	    {
		savePlane = ExtCell(def, (char *) NULL, (def == rootDef));
		if (savePlane != NULL)
		{
		    newsl = (struct saveList *)mallocMagic(sizeof(struct saveList));
		    newsl->sl_plane = savePlane;
		    newsl->sl_def = def;
		    newsl->sl_next = sl;
		    sl = newsl;
		}
		
		errorcnt += extNumErrors;
		warnings += extNumWarnings;
	    }
	    else if (!(def->cd_flags & CDNOEXTRACT))
	    {
		if (!first) TxPrintf(", ");
		TxPrintf("%s", def->cd_name);
		TxFlush();
		first = FALSE;
	    }
	}
    }

    /* Replace any modified substrate planes */
    free_magic1_t mm1 = freeMagic1_init();
    for (; sl; sl = sl->sl_next)
    {
	ExtRevertSubstrate(sl->sl_def, sl->sl_plane);
	sl->sl_def->cd_flags &= ~CDNOEXTRACT; 
	freeMagic1(&mm1, sl);
    }
    freeMagic1_end(&mm1);

    if (!doExtract)
    {
	TxPrintf("\n");
    }
    else
    {
	if (errorcnt > 0)
	    TxError("Total of %d error%s (check feedback entries).\n",
		    errorcnt, errorcnt != 1 ? "s" : "");
	if (warnings > 0)
	    TxError("Total of %d warning%s.\n",
		    warnings, warnings != 1 ? "s" : "");
    }
}
