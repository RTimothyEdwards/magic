/*
 * DBcellcopy.c --
 *
 * Cell copying (yank and stuff)
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellcopy.c,v 1.13 2010/06/24 12:37:15 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>		/* For strlen() and strncmp() */
#include <ctype.h>		/* for isspace() */

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/malloc.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "commands/commands.h"

/* C99 compat */
#include "graphics/graphics.h"

/*
 * The following variable points to the tables currently used for
 * painting.  The paint tables are occasionally switched, by clients
 * like the design-rule checker, by calling DBNewPaintTable.  This
 * paint table applies only to the routine in this module.
 */
static PaintResultType (*dbCurPaintTbl)[NT][NT] = DBPaintResultTbl;

/*
 * The following variable points to the version of DBPaintPlane used
 * for painting during yanks.  This is occasionally switched by clients
 * such as the design-rule checker that need to use, for example,
 * DBPaintPlaneMark instead of the standard version.
 */
static int (*dbCurPaintPlane)() = DBPaintPlaneWrapper;

    /* Structure passed to DBTreeSrTiles() */
struct copyAllArg
{
    TileTypeBitMask	*caa_mask;	/* Mask of tile types to be copied */
    Rect		 caa_rect;	/* Clipping rect in target coords */
    CellUse		*caa_targetUse;	/* Use to which tiles are copied */
    void		(*caa_func)();	/* Callback function for off-grid points */
    Rect		*caa_bbox;	/* Bbox of material copied (in
					 * targetUse coords).  Used only when
					 * copying cells.
					 */
};

    /* Structure passed to DBSrPaintArea() */
struct copyArg
{
    TileTypeBitMask	*ca_mask;	/* Mask of tile types to be copied */
    Rect		 ca_rect;	/* Clipping rect in source coords */
    CellUse		*ca_targetUse;	/* Use to which tiles are copied */
    Transform		*ca_trans;	/* Transform to target */
};

    /* Structure passed to DBTreeSrLabels to hold information about
     * copying labels.
     */

struct copyLabelArg
{
    CellUse *cla_targetUse;		/* Use to which labels are copied. */
    Rect *cla_bbox;			/* If non-NULL, points to rectangle
					 * to be filled in with total area of
					 * all labels copied.
					 */
    char *cla_glob;			/* If non-NULL, used for glob-style
					 * matching of labels during copy.
					 */
};

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneWrapper --
 *
 *    Simple wrapper to DBPaintPlane.
 *    Note that this function is passed as a pointer on occasion, so
 *    it cannot be replaced with a macro!
 *
 * ----------------------------------------------------------------------------
 */

int
DBPaintPlaneWrapper(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;
    Rect expand;
    int result;

    undo->pu_pNum = pNum;
    result = DBNMPaintPlane(def->cd_planes[pNum], type, area,
		dbCurPaintTbl[pNum][loctype], undo);
    GEO_EXPAND(area, 1, &expand);
    DBMergeNMTiles(def->cd_planes[pNum], &expand, undo);
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneMark --
 *
 *    Another wrapper function to DBPaintPlane.  This one is used for
 *    hierarchical DRC, and ensures that tiles are never painted twice
 *    on the same pass, so as not to cause false overlap errors.
 *
 * ----------------------------------------------------------------------------
 */

int
DBPaintPlaneMark(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;

    undo->pu_pNum = pNum;
    return DBNMPaintPlane0(def->cd_planes[pNum], type, area,
		dbCurPaintTbl[pNum][loctype], undo, (unsigned char)PAINT_MARK);
}

/*
 * ----------------------------------------------------------------------------
 *
 * ----------------------------------------------------------------------------
 */

int
DBPaintPlaneXor(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;

    undo->pu_pNum = pNum;
    return DBNMPaintPlane0(def->cd_planes[pNum], type, area,
		dbCurPaintTbl[pNum][loctype], undo, (unsigned char)PAINT_XOR);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPaintPlaneActive ---
 *
 *    This function calls DBPaintPlane, but first checks if the type
 *    being painted is an active layer.  If the type is a contact,
 *    then the residues are checked to see if they are active layers.
 *    Painting proceeds accordingly.
 *
 * ----------------------------------------------------------------------------
 */

int
DBPaintPlaneActive(def, pNum, type, area, undo)
    CellDef *def;
    int pNum;
    TileType type;
    Rect *area;
    PaintUndoInfo *undo;
{
    TileType loctype = type & TT_LEFTMASK;
    TileType t;

    if (DBIsContact(loctype))
    {
	TileTypeBitMask tmask, *rMask;

	rMask = DBResidueMask(loctype);
	TTMaskAndMask3(&tmask, rMask, &DBActiveLayerBits);
	if (!TTMaskEqual(&tmask, rMask))
	{
	    if (!TTMaskIsZero(&tmask))
		for (t = TT_TECHDEPBASE; t < DBNumUserLayers; t++)
		    if (TTMaskHasType(&tmask, t))
			DBPaintPlaneWrapper(def, pNum, t | (type &
				(TT_SIDE | TT_DIRECTION | TT_DIAGONAL)),
				area, undo);
	    return 0;
	}
    }
    if (TTMaskHasType(&DBActiveLayerBits, loctype))
	return DBPaintPlaneWrapper(def, pNum, type, area, undo);
    else
	return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyManhattanPaint --
 *
 * Copy paint from the tree rooted at scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied, and only Manhattan
 * geometry is copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyManhattanPaint(scx, mask, xMask, targetUse)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
{
    struct copyAllArg arg;
    int dbCopyManhattanPaint();

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    arg.caa_func = NULL;
    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    (void) DBTreeSrTiles(scx, mask, xMask, dbCopyManhattanPaint, (ClientData) &arg);
}


/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyAllPaint --
 *
 * Copy paint from the tree rooted at scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyAllPaint(scx, mask, xMask, targetUse)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
{
    TileTypeBitMask locMask;
    struct copyAllArg arg;
    int dbCopyAllPaint();

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    arg.caa_func = NULL;
    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    /* Add any stacking types for the search (but not to mask passed as arg!) */
    locMask = *mask;
    DBMaskAddStacking(&locMask);

    DBTreeSrTiles(scx, &locMask, xMask, dbCopyAllPaint, (ClientData) &arg);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCheckCopyAllPaint --
 *
 * Copy paint from the tree rooted at scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCheckCopyAllPaint(scx, mask, xMask, targetUse, func)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
    void (*func)();		/* Function to call on tile split error */
{
    TileTypeBitMask locMask;
    struct copyAllArg arg;
    int dbCopyAllPaint();

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    arg.caa_func = func;
    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    /* Add any stacking types for the search (but not to mask passed as arg!) */
    locMask = *mask;
    DBMaskAddStacking(&locMask);

    DBTreeSrTiles(scx, &locMask, xMask, dbCopyAllPaint, (ClientData) &arg);
}

/* Data structure used by dbCopyMaskHintsFunc */

struct propUseDefStruct {
   CellDef *puds_source;
   CellDef *puds_dest;
   Transform *puds_trans;	/* Transform from source use to dest */
};

/*
 *-----------------------------------------------------------------------------
 *
 * dbCopyMaskHintsFunc --
 *
 * Callback function used by DBCellCopyMaskHints().  Does the work
 * of copying a "mask-hints" property from a child instance into its
 * parent def, modifying coordinates according to the child instance's
 * transform.
 *
 * Results:
 *	0 to keep the search going.
 *
 * Side effects:
 *	Creates properties in the parent cell.
 * 
 *-----------------------------------------------------------------------------
 */

int
dbCopyMaskHintsFunc(key, value, puds)
    char *key;
    ClientData value;
    struct propUseDefStruct *puds;
{
    CellDef *dest = puds->puds_dest;
    Transform *trans = puds->puds_trans;
    char *propstr = (char *)value;
    char *parentprop, *newvalue, *vptr;
    Rect r, rnew;
    bool propfound;

    if (!strncmp(key, "MASKHINTS_", 10))
    {
	char *vptr, *lastval;
	int lastlen;

	/* Append to existing mask hint (if any) */
	parentprop = (char *)DBPropGet(dest, key, &propfound);
	newvalue = (propfound) ? StrDup((char **)NULL, parentprop) : (char *)NULL;

	vptr = propstr;
	while (*vptr != '\0')
	{
	    if (sscanf(vptr, "%d %d %d %d", &r.r_xbot, &r.r_ybot,
			&r.r_xtop, &r.r_ytop) == 4)
	    {
		GeoTransRect(trans, &r, &rnew);

		lastval = newvalue;
		lastlen = (lastval) ? strlen(lastval) : 0;
		newvalue = mallocMagic(40 + lastlen);

		if (lastval)
		    strcpy(newvalue, lastval);
		else
		    *newvalue = '\0';

		sprintf(newvalue + lastlen, "%s%d %d %d %d", (lastval) ?  " " : "",
			rnew.r_xbot, rnew.r_ybot, rnew.r_xtop, rnew.r_ytop);
		if (lastval) freeMagic(lastval);

		while (*vptr && !isspace(*vptr)) vptr++;
		while (*vptr && isspace(*vptr)) vptr++;
		while (*vptr && !isspace(*vptr)) vptr++;
		while (*vptr && isspace(*vptr)) vptr++;
		while (*vptr && !isspace(*vptr)) vptr++;
		while (*vptr && isspace(*vptr)) vptr++;
		while (*vptr && !isspace(*vptr)) vptr++;
		while (*vptr && isspace(*vptr)) vptr++;
	    }
	    else break;
	}
	if (newvalue)
	    DBPropPut(dest, key, newvalue);
    }

    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyMaskHints --
 *
 * This function is used by the "flatten -inplace" command option to
 * transfer information from mask-hint properties from a flattened
 * child cell to the parent.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Properties copied from child to parent cell and modified.
 *
 *-----------------------------------------------------------------------------
 */
void
DBCellCopyMaskHints(child, parent, transform)
    CellUse *child;
    CellDef *parent;
    Transform *transform;
{
    struct propUseDefStruct puds;

    puds.puds_source = child->cu_def;
    puds.puds_dest = parent;
    puds.puds_trans = transform;
    DBPropEnum(child->cu_def, dbCopyMaskHintsFunc, (ClientData)&puds);
}

/*
 *-----------------------------------------------------------------------------
 *
 * dbFlatCopyMaskHintsFunc ---
 *
 * Callback function used by DBFlatCopyMaskHints() to copy mask hint
 * properties from a child cell into a flattened cell def.  This is
 * simply a variant of DBCellCopyMaskHints() above, with arguments
 * appropriate to being called from DBTreeSrCells(), and applying
 * the transform from the search context.
 *
 * Results:
 *	0 to keep the cell search going.
 * 
 * Side effects:
 *	Generates properties in the target def.
 *
 *-----------------------------------------------------------------------------
 */
int
dbFlatCopyMaskHintsFunc(scx, def)
    SearchContext *scx;
    CellDef *def;
{
    struct propUseDefStruct puds;
    CellUse *use = scx->scx_use;

    puds.puds_source = scx->scx_use->cu_def;
    puds.puds_dest = def;
    puds.puds_trans = &scx->scx_trans;

    DBPropEnum(use->cu_def, dbCopyMaskHintsFunc, (ClientData)&puds);

    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBFlatCopyMaskHints --
 *
 * This function is used by the "flatten" command option to copy information
 * in mask-hint properties from flattened children to a flattened cell def.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Properties copied from child to parent cell and modified.
 *
 *-----------------------------------------------------------------------------
 */
void
DBFlatCopyMaskHints(scx, xMask, targetUse)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which properties will be added */
{
    DBTreeSrCells(scx, xMask, dbFlatCopyMaskHintsFunc, (ClientData)targetUse->cu_def);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBFlattenInPlace --
 *
 * This function is used by the "flatten" command "-doinplace" option to
 * flatten a cell instance into its parent cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Indicated cell use is flattened into the edit cell def.
 *
 *-----------------------------------------------------------------------------
 */

void
DBFlattenInPlace(use, dest, xMask, dolabels, toplabels, doclear)
    CellUse *use;		/* Cell use to flatten */
    CellUse *dest;		/* Cell use to flatten into */
    int xMask;			/* Search mask for flattening */
    bool dolabels;		/* Option to flatten labels */
    bool toplabels;		/* Option to selectively flatten top-level labels */
    bool doclear;		/* Delete the original use if TRUE */
{
    Label *lab;
    SearchContext scx;
    int xsep, ysep, xbase, ybase;

    if (dest == NULL)
    {
	TxError("The target cell does not exist or is not editable.\n");
	return;
    }

    scx.scx_use = use;
    scx.scx_area = use->cu_def->cd_bbox;

    /* Mark labels in the subcell top level for later handling */
    for (lab = scx.scx_use->cu_def->cd_labels; lab; lab = lab->lab_next)
	lab->lab_flags |= LABEL_GENERATE;

    scx.scx_x = use->cu_xlo;
    scx.scx_y = use->cu_ylo;

    while (TRUE)
    {
	if ((use->cu_xlo == use->cu_xhi) && (use->cu_ylo == use->cu_yhi))
	    scx.scx_trans = use->cu_transform;
	else
	{
	    if (use->cu_xlo > use->cu_xhi) xsep = -use->cu_xsep;
	    else xsep = use->cu_xsep;
	    if (use->cu_ylo > use->cu_yhi) ysep = -use->cu_ysep;
	    else ysep = use->cu_ysep;
	    xbase = xsep * (scx.scx_x - use->cu_xlo);
	    ybase = ysep * (scx.scx_y - use->cu_ylo);
	    GeoTransTranslate(xbase, ybase, &use->cu_transform, &scx.scx_trans);
	}

	DBCellCopyAllPaint(&scx, &DBAllButSpaceAndDRCBits, xMask, dest);
	if (dolabels)
	     FlatCopyAllLabels(&scx, &DBAllTypeBits, xMask, dest);
	else if (toplabels)
	{
	    int savemask = scx.scx_use->cu_expandMask;
	    scx.scx_use->cu_expandMask = CU_DESCEND_SPECIAL;
	    DBCellCopyAllLabels(&scx, &DBAllTypeBits, CU_DESCEND_SPECIAL, dest, NULL);
	    scx.scx_use->cu_expandMask = savemask;
	}

	if (xMask != CU_DESCEND_ALL)
	    DBCellCopyAllCells(&scx, xMask, dest, (Rect *)NULL);

	/* Marked labels coming from the subcell top level must not be	*/ 
	/* ports, and text should be prefixed with the subcell name.	*/

	for (lab = dest->cu_def->cd_labels; lab; lab = lab->lab_next)
	{
	    Label *newlab;
	    char *newtext;

	    if (lab->lab_flags & LABEL_GENERATE)
	    {
		newtext = mallocMagic(strlen(lab->lab_text)
			+ strlen(scx.scx_use->cu_id) + 2);

		if ((use->cu_xlo != use->cu_xhi) && (use->cu_ylo != use->cu_yhi))
		    sprintf(newtext, "%s[%d][%d]/%s", scx.scx_use->cu_id,
				scx.scx_x, scx.scx_y, lab->lab_text);
		else if (use->cu_xlo != use->cu_xhi)
		    sprintf(newtext, "%s[%d]/%s", scx.scx_use->cu_id,
				scx.scx_x, lab->lab_text);
		else if (use->cu_ylo != use->cu_yhi)
		    sprintf(newtext, "%s[%d]/%s", scx.scx_use->cu_id,
				scx.scx_y, lab->lab_text);
		else
		    sprintf(newtext, "%s/%s", scx.scx_use->cu_id, lab->lab_text);

		DBPutFontLabel(dest->cu_def,
			&lab->lab_rect, lab->lab_font, lab->lab_size,
			lab->lab_rotate, &lab->lab_offset, lab->lab_just,
			newtext, lab->lab_type, 0, 0);
		DBEraseLabelsByContent(dest->cu_def, &lab->lab_rect,
			-1, lab->lab_text);

		freeMagic(newtext);
	    }
	}
	
	/* Copy and transform mask hints from child to parent */
	DBCellCopyMaskHints(scx.scx_use, dest->cu_def, &scx.scx_trans);

	/* Stop processing if the use is not arrayed. */
	if ((scx.scx_x == use->cu_xhi) && (scx.scx_y == use->cu_yhi))
	    break;

	if (use->cu_xlo < use->cu_xhi)
	    scx.scx_x++;
	else if (use->cu_xlo > use->cu_xhi)
	    scx.scx_x--;

	if (((use->cu_xlo < use->cu_xhi) && (scx.scx_x > use->cu_xhi)) ||
	    ((use->cu_xlo > use->cu_xhi) && (scx.scx_x < use->cu_xhi)))
	{
	    if (use->cu_ylo < use->cu_yhi)
	    {
		scx.scx_y++;
		scx.scx_x = use->cu_xlo;
	    }
	    else if (use->cu_yhi > use->cu_yhi)
	    {
		scx.scx_y--;
		scx.scx_x = use->cu_xlo;
	    }
	}
    }

    /* Unmark labels in the subcell top level */
    for (lab = scx.scx_use->cu_def->cd_labels; lab; lab = lab->lab_next)
	lab->lab_flags &= ~LABEL_GENERATE;

    /* Remove the use from the parent def */
    if (doclear)
	DBDeleteCell(scx.scx_use);

    /* Was: &scx.scx_use->cu_def->cd_bbox */
    DBWAreaChanged(dest->cu_def, &scx.scx_use->cu_bbox,
			DBW_ALLWINDOWS, &DBAllButSpaceAndDRCBits);
}

/* Client data structure used by DBCellFlattenAllCells() */

struct dbFlattenAllData {
    CellUse *fad_dest;		/* Cell use to flatten into */
    int fad_xmask;		/* Search mask for flattening */
    bool fad_dolabels;		/* Option to flatten labels */
    bool fad_toplabels;		/* Option to selectively flatten top-level labels */
};

/*
 *-----------------------------------------------------------------------------
 *
 * dbCellFlattenCellsFunc --
 *
 * Do the actual work of flattening cells for DBCellFlattenAllCells().
 *
 * Results:
 *	Always return 2.
 *
 * Side effects:
 *	Updates the paint planes of EditRootDef.
 *
 *-----------------------------------------------------------------------------
 */

int
dbCellFlattenCellsFunc(scx, clientData)
    SearchContext *scx;	/* Pointer to search context containing
			 * ptr to cell use to be copied,
			 * and transform to the target def.
			 */
    ClientData clientData;	/* Data passed to client function */
{
    CellUse *use, *dest;
    int xMask;
    bool dolabels;
    bool toplabels;
    struct dbFlattenAllData *fad = (struct dbFlattenAllData *)clientData;

    dest = fad->fad_dest;
    xMask = fad->fad_xmask;
    dolabels = fad->fad_dolabels;
    toplabels = fad->fad_toplabels;

    use = scx->scx_use;
    DBFlattenInPlace(use, dest, xMask, dolabels, toplabels, FALSE);
    return 2;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellFlattenAllCells --
 *
 * Flatten subcells from the tree rooted at scx->scx_use into the edit root
 * CellDef, transforming according to the transform in scx.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in EditRootDef.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellFlattenAllCells(scx, dest, xMask, dolabels, toplabels)
    SearchContext *scx;		/* Describes root cell to search and transform
				 * from root cell to coords of targetUse.
				 */
    CellUse *dest;		/* CellUse to flatten into (usually EditCellUse) */
    int xMask;			/* Expansion state mask to be passed to
				 * the flattening routine that determines
				 * whether to do a shallow or deep flattening.
				 */
    bool dolabels;		/* Option to flatten labels */
    bool toplabels;		/* Option to selectively flatten top-level labels */
{
    int dbCellFlattenCellsFunc();
    struct dbFlattenAllData fad;

    fad.fad_dest = dest;
    fad.fad_xmask = xMask;
    fad.fad_dolabels = dolabels;
    fad.fad_toplabels = toplabels;
    DBTreeSrCells(scx, CU_DESCEND_ALL, dbCellFlattenCellsFunc, (ClientData)&fad);
}

/* Client data structure used by DBCellGenerateSubstrate() */

struct dbCopySubData {
    Plane *csd_plane;
    TileType csd_subtype;
    int csd_pNum;
    bool csd_modified;
};

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellGenerateSubstrate --
 *
 * This function is used by the extraction code in ExtSubtree.c.
 * Paint substrate into the target use.  Similar to DBCellCopyAllPaint(),
 * but it finds space tiles on the substrate plane and converts them to
 * a substrate type in the target, clipped to the cell boundary.  This
 * allows the extraction to find and record all substrate regions, both
 * common (global substrate) and local (isolated substrate), without
 * requiring a physical substrate type to be drawn into all cells.
 *
 * Unlike normal paint copying, this can only be done by painting the
 * substrate type over the entire cell area and then erasing all areas
 * belonging to not-substrate types in the source.
 *
 * Returns:
 *	Nothing.
 *
 * Side Effects:
 *	Paints into the targetUse's CellDef.  This only happens if two
 *	conditions are met:
 *	(1) The techfile has defined "substrate"
 *	(2) The techfile defines a type corresponding to the substrate
 *
 * ----------------------------------------------------------------------------
 */

Plane *
DBCellGenerateSubstrate(scx, subType, notSubMask, subShieldMask, targetDef)
    SearchContext *scx;
    TileType subType;			/* Substrate paint type */
    TileTypeBitMask *notSubMask;	/* Mask of types that are not substrate */
    TileTypeBitMask *subShieldMask;	/* Mask of types that shield substrate */
    CellDef *targetDef;
{
    struct dbCopySubData csd;
    Plane *tempPlane;
    int plane;
    Rect rect;
    TileTypeBitMask allButSubMask;
    TileTypeBitMask defaultSubTypeMask;
    int dbEraseSubFunc();
    int dbPaintSubFunc();
    int dbEraseNonSub();
    int dbCopySubFunc();

    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &rect);

    /* Clip to bounding box of the top level cell */
    GEOCLIP(&rect, &scx->scx_use->cu_def->cd_bbox);

    plane = DBPlane(subType);

    tempPlane = DBNewPlane((ClientData) TT_SPACE);
    DBClearPaintPlane(tempPlane);

    csd.csd_subtype = subType;
    csd.csd_plane = tempPlane;
    csd.csd_pNum = plane;
    csd.csd_modified = FALSE;

    /* The substrate type will be redefined to denote only areas of	*/
    /* isolated substrate.  The first step is to erase the default	*/
    /* substrate everywhere so that it can be regenerated automatically	*/
    /* Note: xMask is always zero, as this is only called from extract routines */
    TTMaskSetOnlyType(&defaultSubTypeMask, subType);
    DBTreeSrTiles(scx, &defaultSubTypeMask, 0, dbEraseSubFunc, (ClientData)&csd);

    /* Next, paint the substrate type in the temporary plane over the	*/
    /* area of all substrate shield types.				*/
    DBTreeSrTiles(scx, subShieldMask, 0, dbPaintSubFunc, (ClientData)&csd);
    if (csd.csd_modified == FALSE) return NULL;

    /* Now erase all areas that are non-substrate types in the source */
    DBTreeSrTiles(scx, notSubMask, 0, dbEraseNonSub, (ClientData)&csd);

    /* Finally, copy the destination plane contents onto tempPlane,	*/
    /* ignoring the substrate type.					*/
    TTMaskZero(&allButSubMask);
    TTMaskSetMask(&allButSubMask, &DBAllButSpaceBits);
    TTMaskClearType(&allButSubMask, subType);
    DBSrPaintArea((Tile *)NULL, targetDef->cd_planes[plane], &TiPlaneRect,
		&allButSubMask, dbCopySubFunc, (ClientData)&csd);

    /* Now we have a plane where the substrate type has a strict 	*/
    /* definition that it always marks areas of isolated substrate but	*/
    /* never areas of default substrate.  Return this plane.		*/
    return tempPlane;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellGenerateSimpleSubstrate --
 *
 * This function is used by the extraction code in "extresist".
 * It is similar to DBCellGenerateSubstrate(), above.  It finds space
 * tiles on the substrate plane and converts them to a substrate type
 * in the target, clipped to the cell boundary.  This allows the
 * extraction to find and record all common (global) substrate regions,
 * without requiring a physical substrate type to be drawn into all cells.
 *
 * Unlike normal paint copying, this can only be done by painting the
 * substrate type over the entire cell area and then erasing all areas
 * belonging to not-substrate types in the source.
 *
 * Returns:
 *	Nothing.
 *
 * Side Effects:
 *	Paints into the targetUse's CellDef.  This only happens if two
 *	conditions are met:
 *	(1) The techfile has defined "substrate"
 *	(2) The techfile defines a type corresponding to the substrate
 *
 * ----------------------------------------------------------------------------
 */

Plane *
DBCellGenerateSimpleSubstrate(scx, subType, notSubMask, targetDef)
    SearchContext *scx;
    TileType subType;			/* Substrate paint type */
    TileTypeBitMask *notSubMask;	/* Mask of types that are not substrate */
    CellDef *targetDef;
{
    struct dbCopySubData csd;
    Plane *tempPlane;
    int plane;
    Rect rect;
    TileTypeBitMask allButSubMask;
    TileTypeBitMask defaultSubTypeMask;
    int dbEraseSubFunc();
    int dbPaintSubFunc();
    int dbEraseNonSub();
    int dbCopySubFunc();

    GEOTRANSRECT(&scx->scx_trans, &scx->scx_area, &rect);

    /* Clip to bounding box of the top level cell */
    GEOCLIP(&rect, &scx->scx_use->cu_def->cd_bbox);

    plane = DBPlane(subType);

    tempPlane = DBNewPlane((ClientData) TT_SPACE);
    DBClearPaintPlane(tempPlane);

    csd.csd_subtype = subType;
    csd.csd_plane = tempPlane;
    csd.csd_pNum = plane;
    csd.csd_modified = FALSE;

    /* Paint the substrate type in the temporary plane over the	area of	*/
    /* the entire cell.							*/
    DBPaintPlane(tempPlane, &rect, DBStdPaintTbl(subType, plane),
			(PaintUndoInfo *)NULL);

    /* Now erase all areas that are non-substrate types in the source */
    DBTreeSrTiles(scx, notSubMask, 0, dbEraseNonSub, (ClientData)&csd);

    /* Finally, copy the destination plane contents onto tempPlane,	*/
    /* ignoring the substrate type.					*/
    TTMaskZero(&allButSubMask);
    TTMaskSetMask(&allButSubMask, &DBAllButSpaceBits);
    TTMaskClearType(&allButSubMask, subType);
    DBSrPaintArea((Tile *)NULL, targetDef->cd_planes[plane], &TiPlaneRect,
		&allButSubMask, dbCopySubFunc, (ClientData)&csd);

    /* Now we have a plane where the substrate type occupies the whole	*/
    /* area of the cell except where there are conflicting types (e.g.,	*/
    /* nwell).  Return this plane.					*/
    return tempPlane;
}

/*
 * Callback function for DBCellGenerateSubstrate()
 * Finds tiles in the source def that belong to the type that represents
 * the substrate, and erases them.
 */

int
dbEraseSubFunc(tile, cxp)
    Tile *tile;			/* Pointer to source tile with shield type */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx;
    Rect sourceRect, targetRect;
    int pNum;
    TileType type, loctype, subType;
    Plane *plane;
    struct dbCopySubData *csd;	/* Client data */

    scx = cxp->tc_scx;
    csd = (struct dbCopySubData *)cxp->tc_filter->tf_arg;
    plane = csd->csd_plane;
    pNum = csd->csd_pNum;
    subType = csd->csd_subtype;
    type = TiGetTypeExact(tile);
    if (IsSplit(tile))
    {
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (loctype == TT_SPACE) return 0;
    }

    /* Construct the rect for the tile */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    csd->csd_modified = TRUE;

    return DBNMPaintPlane(plane, type, &targetRect, DBStdEraseTbl(subType, pNum),
		(PaintUndoInfo *)NULL);
}

/*
 * Callback function for DBCellGenerateSubstrate()
 * Finds tiles in the source def that belong to the list of types that
 * shield the substrate (e.g., deep nwell), and paint the substrate type
 * into the target plane over the same area.
 */

int
dbPaintSubFunc(tile, cxp)
    Tile *tile;			/* Pointer to source tile with shield type */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx;
    Rect sourceRect, targetRect;
    int pNum;
    TileType type, loctype, subType;
    Plane *plane;
    struct dbCopySubData *csd;	/* Client data */

    scx = cxp->tc_scx;
    csd = (struct dbCopySubData *)cxp->tc_filter->tf_arg;
    plane = csd->csd_plane;
    pNum = csd->csd_pNum;
    subType = csd->csd_subtype;
    type = TiGetTypeExact(tile);
    if (IsSplit(tile))
    {
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (loctype == TT_SPACE) return 0;
    }

    /* Construct the rect for the tile */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    csd->csd_modified = TRUE;

    return DBNMPaintPlane(plane, type, &targetRect, DBStdPaintTbl(subType, pNum),
		(PaintUndoInfo *)NULL);
}

/*
 * Callback function for DBCellGenerateSubstrate()
 * Finds tiles on the substrate plane in the source def that are not the
 * substrate type, and erases those areas from the target.  This reduces
 * the geometry in the target plane to areas that form isolated substrate
 * regions.  Regions belonging to the common global substrate are ignored.
 */

int
dbEraseNonSub(tile, cxp)
    Tile *tile;			/* Pointer to tile to erase from target */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx;
    Rect sourceRect, targetRect;
    Plane *plane;		/* Plane of target data */
    TileType type, loctype, subType;
    struct dbCopySubData *csd;
    int pNum;

    csd = (struct dbCopySubData *)cxp->tc_filter->tf_arg;
    plane = csd->csd_plane;
    subType = csd->csd_subtype;
    pNum = csd->csd_pNum;

    scx = cxp->tc_scx;

    type = TiGetTypeExact(tile);
    if (IsSplit(tile))
    {
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (loctype == TT_SPACE) return 0;
    }

    /* Construct the rect for the tile */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    /* Erase the substrate type from the area of this tile in the target plane. */
    return DBNMPaintPlane(plane, type, &targetRect, DBStdEraseTbl(subType, pNum),
		(PaintUndoInfo *)NULL);
}

/*
 * Callback function for DBCellGenerateSubstrate()
 * Simple paint function to copy all paint from the substrate plane of the
 * source def into the target plane containing the isolated substrate
 * regions.
 */

int
dbCopySubFunc(tile, csd)
    Tile *tile;			/* Pointer to tile to erase from target */
    struct dbCopySubData *csd;	/* Client data */
{
    Rect rect;
    int pNum;
    TileType type, loctype;
    Plane *plane;

    plane = csd->csd_plane;
    pNum = csd->csd_pNum;
    type = TiGetTypeExact(tile);
    if (IsSplit(tile))
    {
	loctype = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
	if (loctype == TT_SPACE) return 0;
    }
    else
	loctype = type;

    /* Construct the rect for the tile */
    TITORECT(tile, &rect);

    return DBNMPaintPlane(plane, type, &rect, DBStdPaintTbl(loctype, pNum),
		(PaintUndoInfo *)NULL);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyAllLabels --
 *
 * Copy labels from the tree rooted at scx->scx_use to targetUse,
 * transforming according to the transform in scx.  Only labels
 * attached to layers of the types specified by mask are copied.
 * The area to be copied is determined by GEO_LABEL_IN_AREA.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copies labels to targetUse, clipping against scx->scx_area.
 *	If pArea is given, store in it the bounding box of all the
 *	labels copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyAllLabels(scx, mask, xMask, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Only labels of these types are copied */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which labels are to be stuffed */
    Rect *pArea;		/* If non-NULL, points to a box that will be
				 * filled in with bbox (in targetUse coords)
				 * of all labels copied.  Will be degenerate
				 * if nothing was copied.
				 */
{
    int dbCopyAllLabels();
    struct copyLabelArg arg;

    /* DBTeeSrLabels finds all the labels that we want plus some more.
     * We'll filter out the ones that we don't need.
     */

    arg.cla_targetUse = targetUse;
    arg.cla_bbox = pArea;
    arg.cla_glob = NULL;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }
    (void) DBTreeSrLabels(scx, mask, xMask, (TerminalPath *) 0,
			TF_LABEL_ATTACH, dbCopyAllLabels,
			(ClientData) &arg);
}

    /*ARGSUSED*/
int
dbCopyAllLabels(scx, lab, tpath, arg)
    SearchContext *scx;
    Label *lab;
    TerminalPath *tpath;
    struct copyLabelArg *arg;
{
    Rect labTargetRect;
    Point labOffset;
    int targetPos, labRotate;
    CellDef *def;

    def = arg->cla_targetUse->cu_def;
    if (arg->cla_glob != NULL)
	if (!Match(arg->cla_glob, lab->lab_text))
	    return 0;
    if (!GEO_LABEL_IN_AREA(&lab->lab_rect, &(scx->scx_area))) return 0;
    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &labTargetRect);
    targetPos = GeoTransPos(&scx->scx_trans, lab->lab_just);
    GeoTransPointDelta(&scx->scx_trans, &lab->lab_offset, &labOffset);
    labRotate = GeoTransAngle(&scx->scx_trans, lab->lab_rotate);

    /* Eliminate duplicate labels.  Don't pay any attention to layers
     * in deciding on duplicates:  if text and position match, it's a
     * duplicate.
     */

    DBEraseLabelsByContent(def, &labTargetRect, -1, lab->lab_text);
    DBPutFontLabel(def, &labTargetRect, lab->lab_font,
		lab->lab_size, labRotate, &labOffset, targetPos,
		lab->lab_text, lab->lab_type, lab->lab_flags, lab->lab_port);
    if (arg->cla_bbox != NULL)
    {
	GeoIncludeAll(&labTargetRect, arg->cla_bbox);

	/* Rendered font labels include the bounding box of the text itself */
	if (lab->lab_font >= 0)
	{
	    GeoTransRect(&scx->scx_trans, &lab->lab_bbox, &labTargetRect);
	    GeoIncludeAll(&labTargetRect, arg->cla_bbox);
	}
    }
    return 0;
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyGlobLabels --
 *
 * Copy labels from the tree rooted at scx->scx_use to targetUse,
 * transforming according to the transform in scx.  Only labels
 * attached to layers of the types specified by mask and which
 * match the string "globmatch" by glob-style matching are copied.
 * The area to be copied is determined by GEO_LABEL_IN_AREA.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copies labels to targetUse, clipping against scx->scx_area.
 *	If pArea is given, store in it the bounding box of all the
 *	labels copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyGlobLabels(scx, mask, xMask, targetUse, pArea, globmatch)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Only labels of these types are copied */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which labels are to be stuffed */
    Rect *pArea;		/* If non-NULL, points to a box that will be
				 * filled in with bbox (in targetUse coords)
				 * of all labels copied.  Will be degenerate
				 * if nothing was copied.
				 */
    char *globmatch;		/* If non-NULL, only labels matching this
				 * string by glob-style matching are copied.
				 */
{
    int dbCopyAllLabels();
    struct copyLabelArg arg;

    /* DBTeeSrLabels finds all the labels that we want plus some more.
     * We'll filter out the ones that we don't need.
     */

    arg.cla_targetUse = targetUse;
    arg.cla_bbox = pArea;
    arg.cla_glob = globmatch;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }
    (void) DBTreeSrLabels(scx, mask, xMask, (TerminalPath *) 0,
			TF_LABEL_ATTACH, dbCopyAllLabels,
			(ClientData) &arg);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyPaint --
 *
 * Copy paint from the paint planes of scx->scx_use to the paint planes
 * of targetUse, transforming according to the transform in scx.
 * Only the types specified by typeMask are copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the paint planes in targetUse.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyPaint(scx, mask, xMask, targetUse)
    SearchContext *scx;		/* Describes cell to search, area to
				 * copy, transform from cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Types of tiles to be yanked/stuffed */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
{
    int pNum;
    PlaneMask planeMask;
    TreeContext cxp;
    TreeFilter filter;
    struct copyAllArg arg;
    int dbCopyAllPaint();

    if (!DBDescendSubcell(scx->scx_use, xMask))
	return;

    arg.caa_mask = mask;
    arg.caa_targetUse = targetUse;
    arg.caa_func = NULL;
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    /* Build dummy TreeContext */
    cxp.tc_scx = scx;
    cxp.tc_filter = &filter;
    filter.tf_arg = (ClientData) &arg;

    /* tf_func, tf_mask, tf_xmask, tf_planes, and tf_tpath are unneeded */

    planeMask = DBTechTypesToPlanes(mask);
    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (PlaneMaskHasPlane(planeMask, pNum))
	{
	    cxp.tc_plane = pNum;
	    (void) DBSrPaintArea((Tile *) NULL,
		scx->scx_use->cu_def->cd_planes[pNum], &scx->scx_area,
		mask, dbCopyAllPaint, (ClientData) &cxp);
	}
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyLabels --
 *
 * Copy labels from scx->scx_use to targetUse, transforming according to
 * the transform in scx.  Only labels attached to layers of the types
 * specified by mask are copied.  If mask contains the L_LABEL bit, then
 * all labels are copied regardless of their layer.  The area copied is
 * determined by GEO_LABEL_IN_AREA.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the labels in targetUse.  If pArea is given, it will
 *	be filled in with the bounding box of all labels copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyLabels(scx, mask, xMask, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    TileTypeBitMask *mask;	/* Only labels of these types are copied */
    int xMask;			/* Expansion state mask to be used in search */
    CellUse *targetUse;		/* Cell into which labels are to be stuffed */
    Rect *pArea;		/* If non-NULL, points to rectangle to be
				 * filled in with bbox (in targetUse coords)
				 * of all labels copied.  Will be degenerate
				 * if no labels are copied.
				 */
{
    Label *lab;
    CellDef *def = targetUse->cu_def;
    Rect labTargetRect;
    Rect *rect = &scx->scx_area;
    int targetPos, labRotate;
    Point labOffset;
    CellUse *sourceUse = scx->scx_use;

    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }

    if (!DBDescendSubcell(sourceUse, xMask))
	return;

    for (lab = sourceUse->cu_def->cd_labels; lab; lab = lab->lab_next)
	if (GEO_LABEL_IN_AREA(&lab->lab_rect, rect) &&
		(TTMaskHasType(mask, lab->lab_type)
		|| TTMaskHasType(mask, L_LABEL)))
	{
	    GeoTransRect(&scx->scx_trans, &lab->lab_rect, &labTargetRect);
	    targetPos = GeoTransPos(&scx->scx_trans, lab->lab_just);
	    GeoTransPointDelta(&scx->scx_trans, &lab->lab_offset, &labOffset);
	    labRotate = GeoTransAngle(&scx->scx_trans, lab->lab_rotate);


	    /* Eliminate duplicate labels.  Don't pay any attention to
	     * type when deciding on duplicates, since types can change
	     * later and then we'd have a duplicate.
	     */

	    DBEraseLabelsByContent(def, &labTargetRect, -1, lab->lab_text);
	    DBPutFontLabel(def, &labTargetRect, lab->lab_font,
			lab->lab_size, labRotate, &labOffset, targetPos,
			lab->lab_text, lab->lab_type, lab->lab_flags,
			lab->lab_port);
	    if (pArea != NULL)
		(void) GeoIncludeAll(&labTargetRect, pArea);
	}
}

/***
 *** Filter function for paint: Ignores diagonal (split) tiles for
 *** purposes of selection searches.
 ***/

int
dbCopyManhattanPaint(tile, cxp)
    Tile *tile;	/* Pointer to tile to copy */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx = cxp->tc_scx;
    struct copyAllArg *arg;
    Rect sourceRect, targetRect;
    PaintUndoInfo ui;
    CellDef *def;
    TileType type;
    int pNum = cxp->tc_plane;

    /*
     * Don't copy space tiles -- this copy is additive.
     * We should never get passed a space tile, though, because
     * the caller will be using DBSrPaintArea, so this is just
     * a sanity check.
     */

    type = TiGetTypeExact(tile);
    if (type == TT_SPACE || (type & TT_DIAGONAL))
	return 0;

    arg = (struct copyAllArg *) cxp->tc_filter->tf_arg;

    /* Construct the rect for the tile in source coordinates */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    ui.pu_def = def = arg->caa_targetUse->cu_def;
    def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;

    /* Clip against the target area */
    GEOCLIP(&targetRect, &arg->caa_rect);

    (*dbCurPaintPlane)(def, pNum, type, &targetRect, &ui);
    return (0);
}


/***
 *** Filter function for paint
 ***/

int
dbCopyAllPaint(tile, cxp)
    Tile *tile;	/* Pointer to tile to copy */
    TreeContext *cxp;		/* Context from DBTreeSrTiles */
{
    SearchContext *scx = cxp->tc_scx;
    struct copyAllArg *arg;
    Rect sourceRect, targetRect;
    PaintUndoInfo ui;
    CellDef *def;
    TileType type = TiGetTypeExact(tile);
    int pNum = cxp->tc_plane;
    int result;
    TileTypeBitMask *typeMask;

    /*
     * Don't copy space tiles -- this copy is additive.
     * We should never get passed a space tile, though, because
     * the caller will be using DBSrPaintArea, so this is just
     * a sanity check.
     */

    bool splittile = FALSE;
    TileType dinfo = 0;

    if (IsSplit(tile))
    {
	splittile = TRUE;
	dinfo = DBTransformDiagonal(type, &scx->scx_trans);
	type = (SplitSide(tile)) ? SplitRightType(tile) :
			SplitLeftType(tile);
    }

    if (type == TT_SPACE)
	return 0;

    arg = (struct copyAllArg *) cxp->tc_filter->tf_arg;
    typeMask = arg->caa_mask;

    /* Resolve what type we're going to paint, based on the type and mask */
    if (!TTMaskHasType(typeMask, type))
    {
	TileTypeBitMask rMask, *tmask;

	/* Simple case---typeMask has a residue of type on pNum */
	tmask = DBResidueMask(type);
	TTMaskAndMask3(&rMask, typeMask, tmask);
	TTMaskAndMask(&rMask, &DBPlaneTypes[pNum]);
	if (!TTMaskIsZero(&rMask))
	{
	    for (type = TT_TECHDEPBASE; type < DBNumUserLayers; type++)
		if (TTMaskHasType(&rMask, type))
		    break;
	    if (type == DBNumUserLayers) return 0;	/* shouldn't happen */

	    /* Hopefully there's always just one type here---sanity check */
	    TTMaskClearType(&rMask, type);
	    if (!TTMaskIsZero(&rMask))
	    {
		/* Diagnostic */
		TxError("Bad assumption:  Multiple types to paint!  Fix me!\n");
	    }
	}
	else
	{
	    type = DBPlaneToResidue(type, pNum);
	    if (!TTMaskHasType(typeMask, type)) return 0;
	}
    }

    /* Construct the rect for the tile in source coordinates */
    TITORECT(tile, &sourceRect);

    /* Transform to target coordinates */
    GEOTRANSRECT(&scx->scx_trans, &sourceRect, &targetRect);

    ui.pu_def = def = arg->caa_targetUse->cu_def;

    def->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;

    /* Nonmanhattan geometry requires slightly different handling. */
    /* Paint the whole tile and clip by erasing areas outside the  */
    /* clipping rectangle.					   */
    if (splittile)
    {
	Point points[5];
	Rect rrect, orect;
	int np, i, j;

	GrClipTriangle(&targetRect, &arg->caa_rect, TRUE, dinfo, points, &np);

	if (np == 0)
	   return(0);

	if (np >= 3)
	{
	    for (i = 0; i < np; i++)
	    {
		j = (i + 1) % np;
		if (points[i].p_x != points[j].p_x && points[i].p_y !=
				points[j].p_y)
		{
		    /* Break out the triangle */
		    rrect.r_xbot = points[i].p_x;
		    rrect.r_xtop = points[j].p_x;
		    rrect.r_ybot = points[i].p_y;
		    rrect.r_ytop = points[j].p_y;
		    GeoCanonicalRect(&rrect, &targetRect);
		    break;
		}
	    }
	    if (i == np)  /* Exactly one Manhattan rectangle */
	    {
		rrect.r_xbot = points[0].p_x;
		rrect.r_xtop = points[2].p_x;
		rrect.r_ybot = points[0].p_y;
		rrect.r_ytop = points[2].p_y;
		GeoCanonicalRect(&rrect, &targetRect);
		dinfo = 0;
	    }
	    else if (np >= 4) /* Process extra rectangles in the area */
	    {
		/* "orect" is the bounding box of the polygon returned	*/
		/* by ClipTriangle.					*/

		orect.r_xtop = orect.r_xbot = points[0].p_x;
		orect.r_ytop = orect.r_ybot = points[0].p_y;
		for (i = 0; i < np; i++)
		    GeoIncludePoint(&points[i], &orect);

		/* Rectangle to left or right */
		rrect.r_ybot = orect.r_ybot;
		rrect.r_ytop = orect.r_ytop;
		if (targetRect.r_xbot > orect.r_xbot)
		{
		    rrect.r_xbot = orect.r_xbot;
		    rrect.r_xtop = targetRect.r_xbot;
		}
		else if (targetRect.r_xtop < orect.r_xtop)
		{
		    rrect.r_xtop = orect.r_xtop;
		    rrect.r_xbot = targetRect.r_xtop;
		}
		else
		    goto topbottom;

		(*dbCurPaintPlane)(def, pNum, type, &rrect, &ui);

topbottom:
		/* Rectangle to top or bottom */
		rrect.r_xbot = targetRect.r_xbot;
		rrect.r_xtop = targetRect.r_xtop;
		if (targetRect.r_ybot > orect.r_ybot)
		{
		    rrect.r_ybot = orect.r_ybot;
		    rrect.r_ytop = targetRect.r_ybot;
		}
		else if (targetRect.r_ytop < orect.r_ytop)
		{
		    rrect.r_ytop = orect.r_ytop;
		    rrect.r_ybot = targetRect.r_ytop;
		}
		else
		    goto splitdone;

		(*dbCurPaintPlane)(def, pNum, type, &rrect, &ui);
	    }
	}
    }
    else
	/* Clip against the target area */
	GEOCLIP(&targetRect, &arg->caa_rect);

splitdone:

    result = (*dbCurPaintPlane)(def, pNum, dinfo | type, &targetRect, &ui);
    if ((result != 0) && (arg->caa_func != NULL))
    {
	/* result == 1 used exclusively for DRC off-grid error flagging */
	DRCOffGridError(&targetRect);
    }

    return (0);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyAllCells --
 *
 * Copy unexpanded subcells from the tree rooted at scx->scx_use
 * to the subcell plane of targetUse, transforming according to
 * the transform in scx.
 *
 * This effectively "flattens" a cell hierarchy in the sense that
 * all unexpanded subcells in a region (which would appear in the
 * display as bounding boxes) are copied into targetUse without
 * regard for their original location in the hierarchy of scx->scx_use.
 * If an array is unexpanded, it is copied as an array, not as a
 * collection of individual cells.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the cell plane in targetUse.  If pArea is given, it
 *	will be filled in with the total area of all cells copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyAllCells(scx, xMask, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from root cell to coords
				 * of targetUse.
				 */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
    int xMask;			/* Expansion state mask to be used in
				 * searching.  Cells not expanded according
				 * to this mask are copied.  To copy everything
				 * in the subtree under scx->scx_use without
				 * regard to expansion, pass a mask of 0.
				 */
    Rect *pArea;		/* If non-NULL, points to a rectangle to be
				 * filled in with bbox (in targetUse coords)
				 * of all cells copied.  Will be degenerate
				 * if nothing was copied.
				 */
{
    struct copyAllArg arg;
    int dbCellCopyCellsFunc();

    arg.caa_targetUse = targetUse;
    arg.caa_bbox = pArea;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;		/* Make bounding box empty initially. */
	pArea->r_xtop = -1;
    }
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    (void) DBTreeSrCells(scx, xMask, dbCellCopyCellsFunc, (ClientData) &arg);

    /* dbCellCopyCellsFunc() allows cells to be left with duplicate IDs */
    /* so generate unique IDs as needed now.				*/

    if (targetUse != NULL) DBGenerateUniqueIds(targetUse->cu_def, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 *
 * DBCellCopyCells --
 *
 * Copy all subcells that are immediate children of scx->scx_use->cu_def
 * into the subcell plane of targetUse, transforming according to
 * the transform in scx.  Arrays are copied as arrays, not as a
 * collection of individual cells.  If a cell is already present in
 * targetUse that would be exactly duplicated by a new cell, the new
 * cell isn't copied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the cell plane in targetUse.  If pArea is given, it will
 *	be filled in with the bounding box of all cells copied.
 *
 *-----------------------------------------------------------------------------
 */

void
DBCellCopyCells(scx, targetUse, pArea)
    SearchContext *scx;		/* Describes root cell to search, area to
				 * copy, transform from coords of
				 * scx->scx_use->cu_def to coords of targetUse.
				 */
    CellUse *targetUse;		/* Cell into which material is to be stuffed */
    Rect *pArea;		/* If non-NULL, points to rectangle to be
				 * filled in with bbox (in targetUse coords)
				 * of all cells copied.  Will be degenerate
				 * if nothing was copied.
				 */
{
    struct copyAllArg arg;
    int dbCellCopyCellsFunc();

    arg.caa_targetUse = targetUse;
    arg.caa_bbox = pArea;
    if (pArea != NULL)
    {
	pArea->r_xbot = 0;
	pArea->r_xtop = -1;
    }
    GeoTransRect(&scx->scx_trans, &scx->scx_area, &arg.caa_rect);

    (void) DBCellSrArea(scx, dbCellCopyCellsFunc, (ClientData) &arg);
}

/*
 *-----------------------------------------------------------------------------
 *
 * dbCellCopyCellsFunc --
 *
 * Do the actual work of yanking cells for DBCellCopyAllCells() and
 * DBCellCopyCells() above.
 *
 * Results:
 *	Always return 2.
 *
 * Side effects:
 *	Updates the cell plane in arg->caa_targetUse->cu_def.
 *
 *-----------------------------------------------------------------------------
 */

int
dbCellCopyCellsFunc(scx, arg)
    SearchContext *scx;	/* Pointer to search context containing
					 * ptr to cell use to be copied,
					 * and transform to the target def.
					 */
    struct copyAllArg *arg;	/* Client data from caller */
{
    CellUse *use, *newUse;
    CellDef *def;
    int xsep, ysep, xbase, ybase;
    Transform newTrans;

    use = scx->scx_use;
    def = use->cu_def;

    /* Don't allow circular structures! */

    if (DBIsAncestor(def, arg->caa_targetUse->cu_def))
    {
	TxPrintf("Copying %s would create a circularity in the",
	    def->cd_name);
	TxPrintf(" cell hierarchy \n(%s is already its ancestor)",
	    arg->caa_targetUse->cu_def->cd_name);
	TxPrintf(" so cell not copied.\n");
	return 2;
    }

    /* When creating a new use, re-use the id from the old one.		*/
    /* Do not attempt to run DBLinkCell() now and resolve unique IDs;	*/
    /* just create duplicate IDs and regenerate unique ones at the end.	*/

    newUse = DBCellNewUse(def, (char *) use->cu_id);

    newUse->cu_expandMask = use->cu_expandMask;
    newUse->cu_flags = use->cu_flags;

    /* The translation stuff is funny, since we got one element of
     * the array, but not necessarily the lower-left element.  To
     * get the transform for the array as a whole, subtract off fo
     * the index of the element.  The easiest way to see how this
     * works is to look at the code in dbCellSrFunc;  the stuff here
     * is the opposite.
     */

    if (use->cu_xlo > use->cu_xhi) xsep = -use->cu_xsep;
    else xsep = use->cu_xsep;
    if (use->cu_ylo > use->cu_yhi) ysep = -use->cu_ysep;
    else ysep = use->cu_ysep;
    xbase = xsep * (scx->scx_x - use->cu_xlo);
    ybase = ysep * (scx->scx_y - use->cu_ylo);
    GeoTransTranslate(-xbase, -ybase, &scx->scx_trans, &newTrans);
    DBSetArray(use, newUse);
    DBSetTrans(newUse, &newTrans);
    if (DBCellFindDup(newUse, arg->caa_targetUse->cu_def) != NULL)
    {
	if (!(arg->caa_targetUse->cu_def->cd_flags & CDINTERNAL))
	{
	    TxError("Cell \"%s\" would end up on top of an identical copy\n",
		newUse->cu_id);
	    TxError("    of itself.  I'm going to forget about the");
	    TxError(" new copy.\n");
	}
	DBUnLinkCell(newUse, arg->caa_targetUse->cu_def);
	(void) DBCellDeleteUse(newUse);
    }
    else
    {
	DBPlaceCell(newUse, arg->caa_targetUse->cu_def);
	if (arg->caa_bbox != NULL)
	    (void) GeoIncludeAll(&newUse->cu_bbox, arg->caa_bbox);
    }
    return 2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNewPaintTable --
 *
 * 	This procedure changes the paint table to be used by the
 *	DBCellCopyPaint and DBCellCopyAllPaint procedures.
 *
 * Results:
 *	The return value is the address of the paint table that used
 *	to be in effect.  It is up to the client to restore this
 *	value with another call to this procedure.
 *
 * Side effects:
 *	A new paint table takes effect.  However, if newTable is NULL,
 *	then the old paint table remains active.  This allows one to
 *	get a pointer to the active paint table without altering it.
 *
 * ----------------------------------------------------------------------------
 */

PaintResultType (*
DBNewPaintTable(newTable))[NT][NT]
    PaintResultType (*newTable)[NT][NT];  /* Address of new paint table. */
{
    PaintResultType (*oldTable)[NT][NT] = dbCurPaintTbl;
    if (newTable) dbCurPaintTbl = newTable;
    return oldTable;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNewPaintPlane --
 *
 * 	This procedure changes the painting procedure to be used by the
 *	DBCellCopyPaint and DBCellCopyAllPaint procedures.
 *
 * Results:
 *	The return value is the address of the paint procedure that
 *	used to be in effect.  It is up to the client to restore this
 *	value with another call to this procedure.
 *
 * Side effects:
 *	A new paint procedure takes effect.
 *
 * ----------------------------------------------------------------------------
 */

IntProc
DBNewPaintPlane(newProc)
    int (*newProc)();		/* Address of new procedure */
{
    int (*oldProc)() = dbCurPaintPlane;
    dbCurPaintPlane = newProc;
    return (oldProc);
}
