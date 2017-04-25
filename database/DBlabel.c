/*
 * DBlabel.c --
 *
 * Label manipulation primitives.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBlabel.c,v 1.9 2010/08/25 17:33:55 tim Exp $";
#endif  /* not lint */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>	/* For sin(), cos(), and round() functions */

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "database/database.h"
#include "database/fonts.h"
#include "database/databaseInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "commands/commands.h"
#include "textio/textio.h"

static TileType DBPickLabelLayer(/* CellDef *def, Label *lab, int noreconnect */);

/* Globally-accessible font information */

MagicFont **DBFontList = NULL;
int DBNumFonts = 0;

/*
 * ----------------------------------------------------------------------------
 *
 * DBIsSubcircuit --
 *
 * Check if any labels in a CellDef declare port attributes, indicating
 * that the CellDef should be treated as a subcircuit.
 *
 * Results:
 *	TRUE if CellDef contains ports, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBIsSubcircuit(cellDef)
    CellDef *cellDef;
{
    Label *lab;

    for (lab = cellDef->cd_labels; lab != NULL; lab = lab->lab_next)
	if (lab->lab_flags & PORT_DIR_MASK)
	    return TRUE;
 
    return FALSE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBPutLabel --
 *
 * Place a rectangular label in the database, in a particular cell.
 * This is a wrapper function to the more complete DBPutFontLabel()
 * (see below).
 *
 * ----------------------------------------------------------------------------
 */

Label *
DBPutLabel(cellDef, rect, align, text, type, flags)
    CellDef *cellDef;
    Rect *rect;
    int align;
    char *text;
    TileType type;
    int flags;
{
    /* Draw text in a standard X11 font */
    return DBPutFontLabel(cellDef, rect, -1, 0, 0, &GeoOrigin,
		align, text, type, flags);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBPutFontLabel --
 *
 * Place a rectangular label in the database, in a particular cell.
 *
 * It is the responsibility of higher-level routines to insure that
 * the material to which the label is being attached really exists at
 * this point in the cell, and that TT_SPACE is used if there is
 * no single material covering the label's entire area.  The routine
 * DBAdjustLabels is useful for this.
 *
 * Results:
 *	The return value is the actual alignment position used for
 *	the label.  This may be different from align, if align is
 *	defaulted.
 *
 * Side effects:
 *	Updates the label list in the CellDef to contain the label.
 *
 * ----------------------------------------------------------------------------
 */
Label *
DBPutFontLabel(cellDef, rect, font, size, rot, offset, align, text, type, flags)
    CellDef *cellDef;	/* Cell in which label is placed */
    Rect *rect;		/* Location of label; see above for description */
    int font;		/* A vector outline font to use, or -1 for X11 font */
    int size;		/* Scale of vector font relative to the database (x8) */
    int rot;		/* Rotate of the vector font in degrees */
    Point *offset;	/* Offset of the font from the point of origin */
    int align;		/* Orientation/alignment of text.  If this is < 0,
			 * an orientation will be picked to keep the text
			 * inside the cell boundary.
			 */
    char *text;		/* Pointer to actual text of label */
    TileType type;	/* Type of tile to be labelled */
    int flags;		/* Label flags */
{
    Label *lab;
    int len, x1, x2, y1, y2, tmp, labx, laby;

    len = strlen(text) + sizeof (Label) - sizeof lab->lab_text + 1;
    lab = (Label *) mallocMagic ((unsigned) len);
    strcpy(lab->lab_text, text);

    /* Pick a nice alignment if the caller didn't give one.  If the
     * label is more than BORDER units from an edge of the cell,
     * use GEO_NORTH.  Otherwise, put the label on the opposite side
     * from the boundary, so it won't stick out past the edge of
     * the cell boundary.
     */
    
#define BORDER 5
    if (align < 0)
    {
	tmp = (cellDef->cd_bbox.r_xtop - cellDef->cd_bbox.r_xbot)/3;
	if (tmp > BORDER) tmp = BORDER;
	x1 = cellDef->cd_bbox.r_xbot + tmp;
	x2 = cellDef->cd_bbox.r_xtop - tmp;
	tmp = (cellDef->cd_bbox.r_ytop - cellDef->cd_bbox.r_ybot)/3;
	if (tmp > BORDER) tmp = BORDER;
	y1 = cellDef->cd_bbox.r_ybot + tmp;
	y2 = cellDef->cd_bbox.r_ytop - tmp;
	labx = (rect->r_xtop + rect->r_xbot)/2;
	laby = (rect->r_ytop + rect->r_ybot)/2;

	if (labx <= x1)
	{
	    if (laby <= y1) align = GEO_NORTHEAST;
	    else if (laby >= y2) align = GEO_SOUTHEAST;
	    else align = GEO_EAST;
	}
	else if (labx >= x2)
	{
	    if (laby <= y1) align = GEO_NORTHWEST;
	    else if (laby >= y2) align = GEO_SOUTHWEST;
	    else align = GEO_WEST;
	}
	else
	{
	    if (laby <= y1) align = GEO_NORTH;
	    else if (laby >= y2) align = GEO_SOUTH;
	    else align = GEO_NORTH;
	}
    }

    lab->lab_just = align;
    if (font >= 0 && font < DBNumFonts)
    {
	lab->lab_font = font;
	lab->lab_size = size;
	lab->lab_rotate = (short)rot;
	lab->lab_offset = *offset;
    }
    else
    {
	lab->lab_font = -1;
	lab->lab_size = 0;
	lab->lab_rotate = (short)0;
	lab->lab_offset = GeoOrigin;
    }
    lab->lab_type = type;
    lab->lab_flags = flags;
    lab->lab_rect = *rect;
    lab->lab_next = NULL;
    if (cellDef->cd_labels == NULL)
	cellDef->cd_labels = lab;
    else
    {
	ASSERT(cellDef->cd_lastLabel->lab_next == NULL, "DBPutLabel");
	cellDef->cd_lastLabel->lab_next = lab;
    }
    cellDef->cd_lastLabel = lab;

    DBFontLabelSetBBox(lab);
    DBUndoPutLabel(cellDef, lab);
    cellDef->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    return lab;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBEraseLabel --
 *
 * Delete labels attached to tiles of the indicated types that
 * are in the given area (as determined by the macro GEO_LABEL_IN_AREA).  
 * If this procedure is called as part of a command that also modifies paint, 
 * then the paint modifications should be done BEFORE calling here.
 *
 * Results:
 *	TRUE if any labels were deleted, FALSE otherwise.
 *
 * Side effects:
 *	This procedure tries to be clever in order to avoid deleting
 *	labels whenever possible.  If there's enough material on the
 *	label's attached layer so that the label can stay on its
 *	current layer, or if the label can be migrated to a layer that
 *	connects to its current layer, then the label is not deleted.
 *	Deleting up to the edge of a label won't cause the label
 *	to go away.  There's one final exception:  if the mask includes
 *	L_LABEL, then labels are deleted from all layers even if there's
 *	still enough material to keep them around.  The rect pointed to
 *	by areaReturn is filled with the area affected by removing the
 *	label, for purposes of redrawing the necessary portions of the
*	screen.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBEraseLabel(cellDef, area, mask, areaReturn)
    CellDef *cellDef;		/* Cell being modified */
    Rect *area;			/* Area from which labels are to be erased.
				 * This may be a point; any labels touching
				 * or overlapping it are erased.
				 */
    TileTypeBitMask *mask;	/* Mask of types from which labels are to
				 * be erased.
				 */
    Rect *areaReturn;		/* Expand this with label bounding box */
{
    Label *lab, *labPrev;
    bool erasedAny = FALSE;
    TileType newType;

    labPrev = NULL;
    lab = cellDef->cd_labels;
    while (lab != NULL)
    {
	if (!GEO_LABEL_IN_AREA(&lab->lab_rect, area)) goto nextLab;
	if (!TTMaskHasType(mask, L_LABEL))
	{
	    if (!TTMaskHasType(mask, lab->lab_type)) goto nextLab;

	    /* Labels on space always get deleted at this point, since
	     * there's no reasonable new layer to put them on.
	     */
	    if (!(lab->lab_type == TT_SPACE))
	    {
		newType = DBPickLabelLayer(cellDef, lab, 0);
		if (DBConnectsTo(newType, lab->lab_type)) goto nextLab;
	    }
	}

	DBWLabelChanged(cellDef, lab, DBW_ALLWINDOWS);
	if (labPrev == NULL)
	    cellDef->cd_labels = lab->lab_next;
	else labPrev->lab_next = lab->lab_next;
	if (cellDef->cd_lastLabel == lab)
	    cellDef->cd_lastLabel = labPrev;
	DBUndoEraseLabel(cellDef, lab);
	if ((lab->lab_font >= 0) && areaReturn)
	    GeoInclude(&lab->lab_bbox, areaReturn);

	freeMagic((char *) lab);
	lab = lab->lab_next;
	erasedAny = TRUE;
	continue;

	nextLab: labPrev = lab;
	lab = lab->lab_next;
    }

    if (erasedAny)
	cellDef->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
    return (erasedAny);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBEraseLabelsByContent --
 *
 * Erase any labels found on the label list for the given
 * CellDef that match the given specification.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the label list for the argument CellDef.  The
 *	DBWind module is notified about any labels that were
 *	deleted.
 *
 * ----------------------------------------------------------------------------
 */

void
DBEraseLabelsByContent(def, rect, type, text)
    CellDef *def;		/* Where to look for label to delete. */
    Rect *rect;			/* Coordinates of label.  If NULL, then
				 * labels are deleted regardless of coords.
				 */
    TileType type;		/* Layer label is attached to.  If < 0, then
				 * labels are deleted regardless of type.
				 */
    char *text;			/* Text associated with label.  If NULL, then
				 * labels are deleted regardless of text.
				 */
{
    Label *lab, *labPrev;

#define	RECTEQUAL(r1, r2)	  ((r1)->r_xbot == (r2)->r_xbot \
				&& (r1)->r_ybot == (r2)->r_ybot \
				&& (r1)->r_xtop == (r2)->r_xtop \
				&& (r1)->r_ytop == (r2)->r_ytop)

    for (labPrev = NULL, lab = def->cd_labels;
	    lab != NULL;
	    labPrev = lab, lab = lab->lab_next)
    {
	nextCheck:
	if ((rect != NULL) && !(RECTEQUAL(&lab->lab_rect, rect))) continue;
	if ((type >= 0) && (type != lab->lab_type)) continue;
	if ((text != NULL) && (strcmp(text, lab->lab_text) != 0)) continue;
	DBUndoEraseLabel(def, lab);
	DBWLabelChanged(def, lab, DBW_ALLWINDOWS);
	if (labPrev == NULL)
	    def->cd_labels = lab->lab_next;
	else labPrev->lab_next = lab->lab_next;
	if (def->cd_lastLabel == lab)
	    def->cd_lastLabel = labPrev;
	freeMagic((char *) lab);

	/* Don't iterate through loop, since this will skip a label:
	 * just go back to top.  This is tricky!
	 */

	lab = lab->lab_next;
	if (lab == NULL) break;
	else goto nextCheck;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBEraseLabelsByFunction --
 *
 * Erase any labels found on the label list for which the function returns
 * TRUE.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the label list for the argument CellDef.  The
 *	DBWind module is notified about any labels that were
 *	deleted.
 *
 * ----------------------------------------------------------------------------
 */

void
DBEraseLabelsByFunction(def, func)
    CellDef *def;		/* Where to look for label to delete. */
    bool (*func)();		/* Function to call for each label.  If it
				 * returns TRUE, we delete the label.
				 *
				 * Function should be of the form:
				 *
				 *	bool func(lab)
				 *	    Label *lab;
				 *	{
				 *	    return XXX;
				 *	}
				 */
{
    Label *lab, *labPrev;

    for (labPrev = NULL, lab = def->cd_labels;
	    lab != NULL;
	    labPrev = lab, lab = lab->lab_next)
    {
	nextCheck:
	if (!(*func)(lab)) continue;
	DBUndoEraseLabel(def, lab);
	DBWLabelChanged(def, lab, DBW_ALLWINDOWS);
	if (labPrev == NULL)
	    def->cd_labels = lab->lab_next;
	else labPrev->lab_next = lab->lab_next;
	if (def->cd_lastLabel == lab)
	    def->cd_lastLabel = labPrev;
	freeMagic((char *) lab);

	/* Don't iterate through loop, since this will skip a label:
	 * just go back to top.  This is tricky!
	 */

	lab = lab->lab_next;
	if (lab == NULL) break;
	else goto nextCheck;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBReOrientLabel --
 *
 * 	Change the text positions of all labels underneath a given
 *	area in a given cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
DBReOrientLabel(cellDef, area, newPos)
    CellDef *cellDef;		/* Cell whose labels are to be modified. */
    Rect *area;			/* All labels touching this area have their
				 * text positions changed.
				 */
    int newPos;			/* New text positions for all labels in
				 * the area, for example, GEO_NORTH.
				 */
{
    Label *lab;

    for (lab = cellDef->cd_labels; lab != NULL; lab = lab->lab_next)
    {
	if (GEO_TOUCH(area, &lab->lab_rect))
	{
	    DBUndoEraseLabel(cellDef, lab);
	    DBWLabelChanged(cellDef, lab, DBW_ALLWINDOWS);
	    lab->lab_just = newPos;
	    DBUndoPutLabel(cellDef, lab);
	    DBWLabelChanged(cellDef, lab, DBW_ALLWINDOWS);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBAdjustLabels --
 *
 * 	This procedure is called after paint has been modified
 *	in an area.  It finds all labels overlapping that area,
 *	and adjusts the layers they are attached to to reflect
 *	the changes in paint.  Thus, a layer will automatically
 *	migrate from poly to poly-metal-contact and back to
 *	poly if the contact layer is painted and then erased.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The layer attachments of labels may change.  For each
 *	such change, a message is output.
 *
 * ----------------------------------------------------------------------------
 */

void
DBAdjustLabels(def, area)
    CellDef *def;		/* Cell whose paint was changed. */
    Rect *area;			/* Area where paint was modified. */
{
    Label *lab;
    TileType newType;
    bool modified = FALSE;

    /* First, find each label that crosses the area we're
     * interested in.
     */
    
    for (lab = def->cd_labels; lab != NULL; lab = lab->lab_next)
    {
	if (!GEO_TOUCH(&lab->lab_rect, area)) continue;
	newType = DBPickLabelLayer(def, lab, 0);
	if (newType == lab->lab_type) continue;
	if (lab->lab_flags & LABEL_STICKY) continue;
	if (DBVerbose && ((def->cd_flags & CDINTERNAL) == 0)) {
	    TxPrintf("Moving label \"%s\" from %s to %s in cell %s.\n",
		    lab->lab_text, DBTypeLongName(lab->lab_type),
		    DBTypeLongName(newType), def->cd_name);
	};
	DBUndoEraseLabel(def, lab);
	lab->lab_type = newType;
	DBUndoPutLabel(def, lab);
	modified = TRUE;
    }

    if (modified) DBCellSetModified(def, TRUE);
}


/*
 * Extended version of DBAdjustLabels.  If noreconnect==0, 
 * this is supposed to be the same as DBAdjustlabels() above.
 */
void
DBAdjustLabelsNew(def, area, noreconnect)
    CellDef *def;	/* Cell whose paint was changed. */
    Rect *area;		/* Area where paint was modified. */
    int noreconnect; 	/* if 1, don't move label to a type that doesn't
			 * connect to the original type, delete instead
			 */
{
    Label *lab, *labPrev;
    TileType newType;
    bool modified = FALSE;

    /* First, find each label that crosses the area we're
     * interested in.
     */
    
    labPrev = NULL;
    lab = def->cd_labels;
    while (lab != NULL)
    {
	    if (!GEO_TOUCH(&lab->lab_rect, area)) {
		    goto nextLab;
	    }
	    newType = DBPickLabelLayer(def, lab, noreconnect);
	    if (newType == lab->lab_type) {
		    goto nextLab;
	    } 
	    if(newType < 0 && !(lab->lab_flags & LABEL_STICKY)) {
		    TxPrintf("Deleting ambiguous-layer label \"%s\" from %s in cell %s.\n",
			     lab->lab_text, DBTypeLongName(lab->lab_type),
			     def->cd_name);
	    
		    if (labPrev == NULL)
			    def->cd_labels = lab->lab_next;
		    else 
			    labPrev->lab_next = lab->lab_next;
		    if (def->cd_lastLabel == lab)
			    def->cd_lastLabel = labPrev;
		    DBUndoEraseLabel(def, lab);
		    DBWLabelChanged(def, lab, DBW_ALLWINDOWS);
		    freeMagic((char *) lab);
		    lab = lab->lab_next;
		    modified = TRUE;
		    continue;
	    } else if (!(lab->lab_flags & LABEL_STICKY)) {
		    if (DBVerbose && ((def->cd_flags & CDINTERNAL) == 0)) {
			    TxPrintf("Moving label \"%s\" from %s to %s in cell %s.\n",
				     lab->lab_text, DBTypeLongName(lab->lab_type),
				     DBTypeLongName(newType), def->cd_name);
		    }
		    DBUndoEraseLabel(def, lab);
		    lab->lab_type = newType;
		    DBUndoPutLabel(def, lab);
		    modified = TRUE;
	    }
    nextLab: 
	    labPrev = lab;
	    lab = lab->lab_next;
    }

    if (modified) DBCellSetModified(def, TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBPickLabelLayer --
 *
 * 	This procedure looks at the material around a label and
 *	picks a new layer for the label to be attached to.
 *
 * Results:
 *	Returns a tile type, which is a layer that completely
 *	covers the label's area.  If possible, the label's current
 *	layer is chosen, or if that's not possible, then a layer
 *	that connects to the label's current layer, or else any
 *	other mask layer.  If everything fails, TT_SPACE is returned.
 *
 * Side effects:
 *	None.  The label's layer is not changed by this procedure.
 *
 * ----------------------------------------------------------------------------
 */

/* The following variable(s) are shared between DBPickLabelLayer and
 * its search function dbPickFunc2.
 */

TileTypeBitMask *dbAdjustPlaneTypes;	/* Mask of all types in current
					 * plane being searched.
					 */
TileType
DBPickLabelLayer(def, lab, noreconnect)
    CellDef *def;		/* Cell definition containing label. */
    Label *lab;			/* Label for which a home must be found. */
    int noreconnect;        /* if 1, return -1 if rule 5 or 6 would succeed */
{
    TileTypeBitMask types[3], types2[3];
    Rect check1, check2;
    int i, plane;
    TileType choice1, choice2, choice3, choice4, choice5, choice6;
    extern int dbPickFunc1(), dbPickFunc2();

    /* Compute an array of three tile type masks.  The first is for
     * all of the types that are present everywhere underneath the
     * label. The second is for all types that are components of
     * tiles that completely cover the label's area.  The third
     * is for tile types that touch the label anywhere.  To make this
     * work correctly we have to consider four cases separately:
     * point labels, horizontal line labels, vertical line labels,
     * and rectangular labels.
     */

    if ((lab->lab_rect.r_xbot == lab->lab_rect.r_xtop)
	    && (lab->lab_rect.r_ybot == lab->lab_rect.r_ytop))
    {
	/* Point label.  Find out what layers touch the label and
	 * use this for all three masks.
	 */

	GEO_EXPAND(&lab->lab_rect, 1, &check1);
	types[0] = DBZeroTypeBits;
	for (i = PL_SELECTBASE; i < DBNumPlanes; i += 1)
	{
	    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[i],
		    &check1, &DBAllTypeBits, dbPickFunc1,
		    (ClientData) &types[0]);
	}
	types[1] = types[0];
	types[2] = types[0];
    }
    else if (lab->lab_rect.r_xbot == lab->lab_rect.r_xtop)
    {
	/* Vertical line label.  Search two areas, one on the
	 * left and one on the right.  For each side, compute
	 * the type arrays separately.  Then merge them together.
	 */
	
	check1 = lab->lab_rect;
	check2 = lab->lab_rect;
	check1.r_xbot -= 1;
	check2.r_xtop += 1;

	twoAreas:
	types[0] = DBAllButSpaceAndDRCBits;
	types[1] = DBAllButSpaceAndDRCBits;
	TTMaskZero(&types[2]);
	types2[0] = DBAllButSpaceAndDRCBits;
	types2[1] = DBAllButSpaceAndDRCBits;
	TTMaskZero(&types2[2]);
	for (i = PL_SELECTBASE; i < DBNumPlanes; i += 1)
	{
	    dbAdjustPlaneTypes = &DBPlaneTypes[i];
	    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[i],
		    &check1, &DBAllTypeBits, dbPickFunc2,
		    (ClientData) types);
	    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[i],
		    &check2, &DBAllTypeBits, dbPickFunc2,
		    (ClientData) types2);
	}
	TTMaskSetMask(&types[0], &types2[0]);
	TTMaskSetMask(&types[1], &types2[1]);
	TTMaskSetMask(&types[2], &types2[2]);
    }
    else if (lab->lab_rect.r_ybot == lab->lab_rect.r_ytop)
    {
	/* Horizontal line label.  Search two areas, one on the
	 * top and one on the bottom.  Use the code from above
	 * to handle.
	 */
	
	check1 = lab->lab_rect;
	check2 = lab->lab_rect;
	check1.r_ybot -= 1;
	check2.r_ytop += 1;
	goto twoAreas;
    }
    else
    {
	/* This is a rectangular label.  Same thing as for line labels,
	 * except there's only one area to search.
	 */
	
	types[0] = DBAllButSpaceAndDRCBits;
	types[1] = DBAllButSpaceAndDRCBits;
	TTMaskZero(&types[2]);
	for (i = PL_SELECTBASE; i < DBNumPlanes; i += 1)
	{
	    dbAdjustPlaneTypes = &DBPlaneTypes[i];
	    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[i],
		    &lab->lab_rect, &DBAllTypeBits, dbPickFunc2,
		    (ClientData) types);
	}
    }

    /* If the label's layer covers the label's area, use it.
     * Otherwise, look for a layer in the following order:
     * 1. A layer on the same plane as the original layer and that
     *    covers the label and connects to its original layer.
     * 2. A layer on the same plane as the original layer and that
     *    is a component of material that covers the label and
     *    connects to its original layer.
     * 3. A layer that covers the label and connects to the
     *    old layer.
     * 4. A layer that is a component of material that covers
     *    the label and connects to the old layer.
     * 5. A layer that covers the label.
     * 6. A layer that is a component of material that covers the label.
     * 7. Space.
     */
    
    if (TTMaskHasType(&types[0], lab->lab_type)) return lab->lab_type;
    plane = DBPlane(lab->lab_type);
    choice1 = choice2 = choice3 = choice4 = choice5 = choice6 = TT_SPACE;
    for (i = TT_SELECTBASE; i < DBNumUserLayers; i += 1)
    {
	if (!TTMaskHasType(&types[2], i)) continue;
	if (DBConnectsTo(i, lab->lab_type))
	{
	    if (DBPlane(i) == plane)
	    {
		if (TTMaskHasType(&types[0], i))
		{
		    choice1 = i;
		    continue;
		}
		else if (TTMaskHasType(&types[1], i))
		{
		    choice2 = i;
		    continue;
		}
	    }
	    if (TTMaskHasType(&types[0], i))
	    {
		choice3 = i;
		continue;
	    }
	    else if (TTMaskHasType(&types[1], i))
	    {
		choice4 = i;
		continue;
	    }
	}
	if (TTMaskHasType(&types[0], i))
	{
	    /* A type that connects to more than itself is preferred */
	    if (choice5 == TT_SPACE)
		choice5 = i;
	    else
	    {
		TileTypeBitMask ctest;
		TTMaskZero(&ctest);
		TTMaskSetMask(&ctest, &DBConnectTbl[i]);
		TTMaskClearType(&ctest, i);
		if (!TTMaskIsZero(&ctest))
		    choice5 = i;
		else if (TTMaskHasType(&types[1], i))
		    choice6 = i;
	    }
	    continue;
	}
	else if (TTMaskHasType(&types[1], i))
	{
	    choice6 = i;
	    continue;
	}
    }

    if (choice1 != TT_SPACE) return choice1;
    else if (choice2 != TT_SPACE) return choice2;
    else if (choice3 != TT_SPACE) return choice3;
    else if (choice4 != TT_SPACE) return choice4;
    else if (noreconnect) {
#ifdef notdef
	TxPrintf("DBPickLabelLayer \"%s\" (on %s at %d,%d) choice4=%s choice5=%s choice6=%s.\n",
		     lab->lab_text, 
		     DBTypeLongName(lab->lab_type),
		     lab->lab_rect.r_xbot,
		     lab->lab_rect.r_ytop,
		     DBTypeLongName(choice4),
		     DBTypeLongName(choice5),
		     DBTypeLongName(choice6));
#endif
	/* If the flag is set, don't cause a netlist change by moving a
	   the label.  So unless there's only space here, delete the label */
	if(choice5 == TT_SPACE && choice6 == TT_SPACE)
	    return TT_SPACE;
	else
 	    return -1;
    }
    else if (choice5 != TT_SPACE) return choice5;
    else return choice6;
}

/* Search function for DBPickLabelLayer:  just OR in the type of
 * any tiles (except space) to the mask passed as clientdata.
 * Always return 0 to keep the search alive.
 */

int
dbPickFunc1(tile, mask)
    Tile *tile;			/* Tile found. */
    TileTypeBitMask *mask;	/* Mask to be modified. */
{
    TileType type;

    if (IsSplit(tile)) 
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    else
	type = TiGetType(tile);

    if (type == TT_SPACE) return 0;
    TTMaskSetType(mask, type);
    return 0;
}

/* Another search function for DBPickLabelLayer.  For the first element
 * in the mask array, AND off all types on the current plane except the
 * given type.  For the second element, AND off all types on the current
 * plane except the ones that are components of this tile's type.  For
 * the third element of the array, just OR in the type of the current
 * tile.  A space tile ruins the whole plane so return 1 to abort the
 * search.  Otherwise return 0.
 */

int
dbPickFunc2(tile, mask)
    Tile *tile;			/* Tile found. */
    TileTypeBitMask *mask;	/* Mask to be modified. */
{
    TileType type;
    TileTypeBitMask tmp, *rMask;

    if (IsSplit(tile)) 
	type = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    else
	type = TiGetType(tile);

    if (type == TT_SPACE)
    {
	/* Space means can't have any tile types on this plane. */

	TTMaskClearMask(&mask[0], dbAdjustPlaneTypes);
	TTMaskClearMask(&mask[1], dbAdjustPlaneTypes);
	return 1;
    }

    tmp = *dbAdjustPlaneTypes;
    TTMaskClearType(&tmp, type);
    TTMaskClearMask(&mask[0], &tmp);
    rMask = DBResidueMask(type);
    TTMaskClearMask(&tmp, rMask);
    TTMaskClearMask(&mask[1], &tmp);
    TTMaskSetType(&mask[2], type);
    return 0;
}

/*
 *  Linked point list, used as temporary space when generating
 *  font character paths.
 */

typedef struct fontpath *FontPathPtr;
typedef struct fontpath
{
    Point       fp_point;
    FontPathPtr fp_next;
} FontPath;

#define INTSEGS 5	/* Font outline Bezier curves are deconstructed
			 * into (INTSEGS + 1) line segments
			 */
float par[INTSEGS];
float parsq[INTSEGS];
float parcb[INTSEGS];

/*
 * ----------------------------------------------------------------------------
 *
 * Initialize Bezier parametric settings
 *
 * ----------------------------------------------------------------------------
 */

void
DBFontInitCurves()
{
    float t;
    short idx;

    for (idx = 0; idx < INTSEGS; idx++)
    {
	t = (float)(idx + 1) / (INTSEGS + 1);
	par[idx] = t;
	parsq[idx] = t * t;
	parcb[idx] = parsq[idx] * t;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 *  Compute line segment points from control points of a Cubic Bezier curve.
 *
 *  Note:  This routine does not take into account the curvature, and therefore
 *  does not optimize for the minimum number of line segments needed to
 *  represent the curve within a fixed margin of error. 
 *
 * ----------------------------------------------------------------------------
 */

void
CalcBezierPoints(fp, bp)
    FontPath *fp;		/* Pointer to last point of closed path */
    FontPath *bp;		/* Pointer to 1st of 3 bezier control points */
{
    FontPath *newPath, *curPath;
    Point *beginPath, *ctrl1, *ctrl2, *endPath;
    float ax, bx, cx, ay, by, cy;
    int idx, tmpx, tmpy;

    beginPath = &fp->fp_point;
    ctrl1 = &fp->fp_next->fp_point;
    ctrl2 = &fp->fp_next->fp_next->fp_point;
    endPath = &fp->fp_next->fp_next->fp_next->fp_point;

    cx = 3.0 * (float)(ctrl1->p_x - beginPath->p_x);
    bx = 3.0 * (float)(ctrl2->p_x - ctrl1->p_x) - cx;
    ax = (float)(endPath->p_x - beginPath->p_x) - cx - bx;

    cy = 3.0 * (float)(ctrl1->p_y - beginPath->p_y);
    by = 3.0 * (float)(ctrl2->p_y - ctrl1->p_y) - cy;
    ay = (float)(endPath->p_y - beginPath->p_y) - cy - by;

    curPath = fp;
    for (idx = 0; idx < INTSEGS; idx++)
    {
	tmpx = (int)(ax * parcb[idx] + bx * parsq[idx] +
		cx * par[idx] + beginPath->p_x);
	tmpy = (int)(ay * parcb[idx] + by * parsq[idx] +
		cy * par[idx] + beginPath->p_y);
	if ((tmpx != curPath->fp_point.p_x) || (tmpy != curPath->fp_point.p_y))
	{
	    newPath = (FontPath *)mallocMagic(sizeof(FontPath));
	    newPath->fp_point.p_x = tmpx;
	    newPath->fp_point.p_y = tmpy;
	    curPath->fp_next = newPath;
	    curPath = newPath; 
	}
    }

    /* Link the last point to the end and free the two control points */
    curPath->fp_next = bp->fp_next->fp_next;
    freeMagic(bp->fp_next);
    freeMagic(bp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbGetToken ---
 *
 *	Tokenize PostScript input from a Type3 font definition file.
 *
 * ----------------------------------------------------------------------------
 */

char *
dbGetToken(ff)
    FILE *ff;
{
    static char line[512];
    static char *lineptr = NULL;
    char *rptr;

    while (lineptr == NULL)
    {
	if (fgets(line, 511, ff) == NULL)
	    return NULL;
	else
	{
	    lineptr = line;
	    while (isspace(*lineptr)) lineptr++;
	}
	if (*lineptr == '%') lineptr = NULL;	 /* Skip comment lines */
	else if (*lineptr == '\n') lineptr = NULL;   /* Skip blank lines */
    }

    rptr = lineptr;

    while (!isspace(*lineptr) && (*lineptr != '\n')) lineptr++;
    if (*lineptr == '\n')
    {
	*lineptr = '\0';
	lineptr = NULL;
    }
    else
    {
	*lineptr = '\0';
	lineptr++;
	while (isspace(*lineptr)) lineptr++;
    }
    return rptr;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBFontLabelSetBBox --
 *
 *	Given a font label with corresponding size, rotation, offset, etc.,
 *	(1) compute the four corners of the label, and (2) compute the
 *	Manhattan bounding box of the label.  The corners are computed in
 *	units of (database / 8) for a finer resolution, and measured
 *	relative to the label rectangle's origin so we don't run out of
 *	bits in computing the corner positions.
 *	
 *	This routine needs to be run whenever a font label changes
 *	properties (including when the font label is created).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The label's lab_bbox record is modified to contain the bounding
 *	box, in database coordinates, of the given label.  The label's
 *	lab_corners[] points are modified to contain the corner positions
 *	of the label, in units as described above.
 *
 * ----------------------------------------------------------------------------
 */

void
DBFontLabelSetBBox(label)
    Label *label;
{
    char *tptr;
    Point *coffset, rcenter;
    Rect *cbbox, locbbox, *frect;
    Transform labTransform = GeoIdentityTransform;
    int i, ysave, size;
    double rrad, cr, sr, tmpx, tmpy;
    bool expand;

    if (label->lab_font < 0) return;	/* No action for default font */

    /* Label size is computed such that size = 1 means that the font	*/
    /* height extent is equal to 1 database unit.  The font height	*/
    /* extent is measured from the baseline up.				*/

    frect = &DBFontList[label->lab_font]->mf_extents;

    /* Get label size from outline fonts */

    locbbox = GeoNullRect;
    for (tptr = label->lab_text; *tptr != '\0'; tptr++)
    {
	DBFontChar(label->lab_font, *tptr, NULL, &coffset, &cbbox);
	if (*(tptr + 1) == '\0')
	    locbbox.r_xtop += cbbox->r_xtop;
	else
	    locbbox.r_xtop += coffset->p_x;
	locbbox.r_ytop = MAX(locbbox.r_ytop, cbbox->r_ytop);
	locbbox.r_ybot = MIN(locbbox.r_ybot, cbbox->r_ybot);
    }

    /* In case the font did not declare its extents, use the actual text height */
    locbbox.r_ytop = MAX(locbbox.r_ytop, frect->r_ytop);

    /* Scale to font size, in database units / 8 (upshift 3 bits)  */

    ysave = locbbox.r_ytop;

    locbbox.r_xbot *= label->lab_size;
    locbbox.r_xtop *= label->lab_size;
    locbbox.r_ybot *= label->lab_size;
    locbbox.r_ytop *= label->lab_size;

    locbbox.r_xbot /= ysave;
    locbbox.r_xtop /= ysave;
    locbbox.r_ybot /= ysave;
    locbbox.r_ytop /= ysave;

    /* Apply justification */

    switch (label->lab_just)
    {
	case GEO_SOUTH:
	case GEO_SOUTHWEST:
	case GEO_SOUTHEAST:
	    locbbox.r_ybot -= locbbox.r_ytop;
	    locbbox.r_ytop = 0;
	    break;
	case GEO_WEST:
	case GEO_EAST:
	case GEO_CENTER:
	    locbbox.r_ytop >>= 1;
	    locbbox.r_ybot -= locbbox.r_ytop;
	    break;
    }
    switch (label->lab_just)
    {
	case GEO_WEST:
	case GEO_NORTHWEST:
	case GEO_SOUTHWEST:
	    locbbox.r_xbot -= locbbox.r_xtop;
	    locbbox.r_xtop = 0;
	    break;
	case GEO_NORTH:
	case GEO_SOUTH:
	case GEO_CENTER:
	    locbbox.r_xtop >>= 1;
	    locbbox.r_xbot -= locbbox.r_xtop;
	    break;
    }

    /* Apply offset (already in units / 8) */

    locbbox.r_xbot += label->lab_offset.p_x;
    locbbox.r_xtop += label->lab_offset.p_x;
    locbbox.r_ybot += label->lab_offset.p_y;
    locbbox.r_ytop += label->lab_offset.p_y;

    /* Apply rotation */

    if (label->lab_rotate < 0) label->lab_rotate += 360;
    if (label->lab_rotate >= 360) label->lab_rotate -= 360;

    label->lab_corners[0] = locbbox.r_ll;
    label->lab_corners[1].p_x = locbbox.r_ur.p_x;
    label->lab_corners[1].p_y = locbbox.r_ll.p_y;
    label->lab_corners[2] = locbbox.r_ur;
    label->lab_corners[3].p_x = locbbox.r_ll.p_x;
    label->lab_corners[3].p_y = locbbox.r_ur.p_y;

    rrad = (double)label->lab_rotate * 0.0174532925;
    cr = cos(rrad);
    sr = sin(rrad);

    for (i = 0; i < 4; i++)
    {
	tmpx = label->lab_corners[i].p_x * cr - label->lab_corners[i].p_y * sr;
	tmpy = label->lab_corners[i].p_x * sr + label->lab_corners[i].p_y * cr;
	label->lab_corners[i].p_x = (int)round(tmpx);
	label->lab_corners[i].p_y = (int)round(tmpy);

	/* Initialize bounding box */
	if (i == 0)
	    label->lab_bbox.r_ll = label->lab_bbox.r_ur = label->lab_corners[0];
	else
	    GeoIncludePoint(&label->lab_corners[i], &label->lab_bbox);
    }

    /* Compute the bounding box, in (rounded) database coordinates */

    rcenter.p_x = (label->lab_rect.r_xtop + label->lab_rect.r_xbot) << 2;
    rcenter.p_y = (label->lab_rect.r_ytop + label->lab_rect.r_ybot) << 2;
    label->lab_bbox.r_xbot += rcenter.p_x;
    label->lab_bbox.r_xtop += rcenter.p_x;
    label->lab_bbox.r_ybot += rcenter.p_y;
    label->lab_bbox.r_ytop += rcenter.p_y;

    expand = (label->lab_bbox.r_xbot & 0x7f) ? TRUE : FALSE;
    label->lab_bbox.r_xbot >>= 3;
    if (expand) label->lab_bbox.r_xbot--;
    expand = (label->lab_bbox.r_xtop & 0x7f) ? TRUE : FALSE;
    label->lab_bbox.r_xtop >>= 3;
    if (expand) label->lab_bbox.r_xtop++;
    expand = (label->lab_bbox.r_ybot & 0x7f) ? TRUE : FALSE;
    label->lab_bbox.r_ybot >>= 3;
    if (expand) label->lab_bbox.r_ybot--;
    expand = (label->lab_bbox.r_ytop & 0x7f) ? TRUE : FALSE;
    label->lab_bbox.r_ytop >>= 3;
    if (expand) label->lab_bbox.r_ytop++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBFontChar ---
 *
 *	Return information about a single character of a font
 *
 * Results:
 *	Return 0 on success, -1 on failure.
 *
 * Side effects:
 *	Fills in clist, coffset, and cbbox if any are non-NULL.
 *
 * ----------------------------------------------------------------------------
 */

int
DBFontChar(font, ccode, clist, coffset, cbbox)
    int font;		/* Index of font */
    char ccode;		/* ASCII character code */
    FontChar **clist;	/* Return vector list here */
    Point    **coffset; /* Return position offset here */
    Rect     **cbbox;	/* Return bounding box here */
{

    if (font < 0 || font >= DBNumFonts) return - 1;
    if (DBFontList[font] == NULL) return -1;
    if (ccode < 32) ccode = 127;	 /* out-of-bounds code */

    if (clist)   *clist = DBFontList[font]->mf_vectors[ccode - 32];
    if (coffset) *coffset = &DBFontList[font]->mf_offset[ccode - 32];
    if (cbbox)   *cbbox = &DBFontList[font]->mf_bbox[ccode - 32];

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNameToFont ---
 *
 *	Given a font name, return the index into DBFontList.
 *
 * Results:
 *	-1 if the name is "default", and -2 if the name is not found.
 *	Otherwise, return the index into the DBFontList.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
DBNameToFont(name)
    char *name;
{
    int i;
    for (i = 0; i < DBNumFonts; i++)
	if (!strcasecmp(name, DBFontList[i]->mf_name))
	    return i;
    if (!strcasecmp(name, "default"))
	return -1;
    return -2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBLoadFont ---
 *
 *	Read information from a Type3 font file, and save each character
 *	as a set of vectors forming closed paths.
 *	This routines assumes a *very* limited syntax for Type3 PostScript
 *	fonts, using only moveto, lineto, curveto, and closepath for each
 *	character.  The font dictionary is assumed to be called CharProcs.
 *	These assumptions are valid for Type3 font output generated by
 *	FontForge.
 *
 * Results:
 *	0 on success, -1 on failure.
 *
 * Side effects:
 *	Fills font data structures.
 *
 * ----------------------------------------------------------------------------
 */

#define NUMBUF 16

int
DBLoadFont(fontfile, scale)
    char *fontfile;
    float scale;
{
    FILE *ff;
    char *ascii_names[] = {
	"space", "exclam", "quotedbl", "numbersign", "dollar",
	"percent", "ampersand", "quoteright", "parenleft", "parenright",
	"asterisk", "plus", "comma", "hyphen", "period", "slash", "zero",
	"one", "two", "three", "four", "five", "six", "seven", "eight",
	"nine", "colon", "semicolon", "less", "equal", "greater", "question",
	"at", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
	"M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y",
	"Z", "bracketleft", "backslash", "bracketright", "asciicircum",
	"underscore", "quoteleft", "a", "b", "c", "d", "e", "f", "g", "h",
	"i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u",
	"v", "w", "x", "y", "z", "braceleft", "bar", "braceright",
	"asciitilde", ".notdef", NULL
    };
    int i, j, x, y, asciiidx, chardef, numidx;
    int numtok[NUMBUF];
    char *token, *psname = NULL;
    MagicFont *newFont = NULL, **newDBFontList;
    FontChar *newChar;
    FontPath *newPath, *pathStart = NULL, *curPath, *lastPath;
    Rect extents = GeoNullRect;

    ff = PaOpen(fontfile, "r", (char *)NULL, ".", SysLibPath, (char **)NULL);
    if (ff == NULL) return -1;

    chardef = 0;
    while ((token = dbGetToken(ff)) != NULL)
    {
	if (token[0] == '/') psname = &token[1];  /* Hopefully this doesn't go away */

	if (chardef == 0)
	{
	    if (!strcmp(token, "/FontBBox"))
	    {
		token = dbGetToken(ff);
		if (*token == '{') token++;
		while (isspace(*token)) token++;
		if (StrIsNumeric(token)) extents.r_xbot = (int)round(atof(token));
		token = dbGetToken(ff);
		if (StrIsNumeric(token)) extents.r_ybot = (int)round(atof(token));
		token = dbGetToken(ff);
		if (StrIsNumeric(token)) extents.r_xtop = (int)round(atof(token));
		token = dbGetToken(ff);
		if (StrIsNumeric(token)) extents.r_ytop = (int)round(atof(token));
	    }
	    else if (!strcmp(token, "CharProcs"))
	    {
		token = dbGetToken(ff);
		if (!strcmp(token, "begin"))
		{
		    chardef = 1;
		    newDBFontList = (MagicFont **)mallocMagic((DBNumFonts + 2)
				* sizeof(MagicFont *));
		    newDBFontList[DBNumFonts + 1] = NULL;
		    for (i = 0; i < DBNumFonts; i++)
			newDBFontList[i] = DBFontList[i];
		    if (DBNumFonts > 0) freeMagic((char *)DBFontList);
		    DBFontList = newDBFontList;

		    newFont = (MagicFont *)mallocMagic(sizeof(MagicFont));
		    newDBFontList[DBNumFonts] = newFont;

		    DBNumFonts++;
		    for (asciiidx = 0; asciiidx < 96; asciiidx++)
			newFont->mf_vectors[asciiidx] = NULL;

		    newFont->mf_extents = extents;

		    /* This is a hack, but all these fonts declare	*/
		    /* their "height" to be the Y-value of the tallest	*/
		    /* character, from which one cannot get any useful	*/
		    /* measure of the height of a standard capital	*/
		    /* letter.  I am using an ad-hoc scale passed as an	*/
		    /* argument to the load command (usually ~0.6).	*/

		    newFont->mf_extents.r_ytop =
				(int)((float)newFont->mf_extents.r_ytop
				* scale);
		}
	    }
	    else if (!strcmp(token, "definefont"))
	    {
		if (newFont && psname)
		{
		    char *dotptr = strrchr(psname, '.');
		    if (dotptr != NULL) *dotptr = '\0';
		    newFont->mf_name = StrDup((char **)NULL, psname);
		}
		break;	/* Presumably this is the end of input */
	    }
	}
	else if (chardef == 1)
	{
	    if (!strcmp(token, "currentdict"))
	    {
		token = dbGetToken(ff);
		if (!strcmp(token, "end"))
		    chardef = 0;
	    }

	    /* Look for a character def start */
	    else if (!strcmp(token, "{"))
	    {
		if (psname != NULL)
		{
		    asciiidx = LookupStructFull(psname, ascii_names, sizeof(char *));
		    if (asciiidx >= 0)
			chardef = 2;
		}
		numidx = 0;
	    }
	}
	else if (chardef == 2)
	{
	    /* Look for character def end */
	    if (!strcmp(token, "}"))
		chardef = 1;

	    /* Pick up values for setcachedevice */
	    if (StrIsNumeric(token))
	    {
		numtok[numidx] = (int)round(atof(token));
		numidx = (numidx + 1) % NUMBUF;
	    }

	    /* Look for path start */
	    else if (!strcmp(token, "setcachedevice"))
	    {
		newChar = NULL;
		chardef = 3;

		/* Fill in bounding box and offset */
		newFont->mf_offset[asciiidx].p_x = numtok[(numidx + NUMBUF - 6)
			% NUMBUF];
		newFont->mf_offset[asciiidx].p_y = numtok[(numidx + NUMBUF - 5)
			% NUMBUF];
		newFont->mf_bbox[asciiidx].r_xbot = numtok[(numidx + NUMBUF - 4)
			% NUMBUF];
		newFont->mf_bbox[asciiidx].r_ybot = numtok[(numidx + NUMBUF - 3)
			% NUMBUF];
		newFont->mf_bbox[asciiidx].r_xtop = numtok[(numidx + NUMBUF - 2)
			% NUMBUF];
		newFont->mf_bbox[asciiidx].r_ytop = numtok[(numidx + NUMBUF - 1)
			% NUMBUF];
	    }
	}
	else if (chardef == 3)
	{
	    if (StrIsNumeric(token))
	    {
		numtok[numidx] = (int)round(atof(token));
		numidx = (numidx + 1) % NUMBUF;
	    }
	    else if (!strcmp(token, "}"))	/* End def */
		chardef = 1;
	    else if (!strcmp(token, "closepath"))
	    {
		if (newChar == NULL)
		{
		    newChar = (FontChar *)mallocMagic(sizeof(FontChar));
		    newFont->mf_vectors[asciiidx] = newChar;
		}
		else
		{
		    newChar->fc_next = (FontChar *)mallocMagic(sizeof(FontChar));
		    newChar = newChar->fc_next;
		}

		/* translate pathStart */
		for (i = 0, curPath = pathStart; curPath->fp_next != NULL;
			curPath = curPath->fp_next)
		    if (!GEO_SAMEPOINT(curPath->fp_next->fp_point, curPath->fp_point))
			i++;

		newChar->fc_numpoints = i;

		/* If the first and last points are the same, remove the last one */
		if (!GEO_SAMEPOINT(curPath->fp_point, pathStart->fp_point))
		    newChar->fc_numpoints++;
		
		newChar->fc_points = (Point *)mallocMagic(i * sizeof(Point));
		newChar->fc_next = NULL;
		for (i = 0, curPath = pathStart; i < newChar->fc_numpoints;
			curPath = curPath->fp_next)
		{
		    if (curPath->fp_next != NULL)
			if (GEO_SAMEPOINT(curPath->fp_next->fp_point, curPath->fp_point))
			    continue;

		    newChar->fc_points[i].p_x = curPath->fp_point.p_x;
		    newChar->fc_points[i].p_y = curPath->fp_point.p_y;
		    i++;
		}

		/* Remove the pointlist */
		for (newPath = pathStart; newPath != NULL; newPath = newPath->fp_next)
		    freeMagic(newPath);
		pathStart = NULL;
	    }
	    else
	    {
		j = (!strcmp(token, "curveto")) ? 6 : 2;
		lastPath = curPath;
		for (i = j; i > 0; i -= 2)
		{
		    newPath = (FontPath *)mallocMagic(sizeof(FontPath));
		    newPath->fp_next = NULL;
		    newPath->fp_point.p_x = numtok[(numidx + NUMBUF - i) % NUMBUF];
		    newPath->fp_point.p_y = numtok[(numidx + NUMBUF - i + 1) % NUMBUF];
		    if (pathStart == NULL)
			pathStart = newPath;
		    else
			curPath->fp_next = newPath;

		    curPath = newPath;
		}

		/* "moveto" and "lineto" require no further action.  "curveto"	*/
		/* requires that we convert the bezier curve defined by		*/
		/* the last 4 points to a set of line segments.			*/

		if (j == 6)
		{
		    /* Retain the 3 curve points that will be replaced */
		    FontPath *curvePath = lastPath->fp_next;
		    CalcBezierPoints(lastPath, curvePath);
		}
	    }
	}
    }
    fclose(ff);
    return (newFont == NULL) ? -1 : 0;
}
