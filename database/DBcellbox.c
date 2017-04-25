/*
 * DBcellbox.c --
 *
 * Procedures for calculating and changing cell bounding boxes,
 * and for manipulating arrays of cells.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellbox.c,v 1.3 2008/12/11 18:36:43 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/undo.h"


/*
 *-----------------------------------------------------------------------------
 *
 * DBPrintUseId --
 *
 * Generate the print name of the use identifier indicated by the supplied
 * SearchContext.
 *
 * Results:
 *	Returns a pointer to the NULL byte at the end of the string
 *	generated.
 *
 * Side effects:
 *	The character string pointed to by name is set to contain the use
 *	id of scx->scx_use followed by any array indices.  If scx->scx_use
 *	is a two dimensional array, the array indices are of the form [y,x],
 *	otherwise there is a single array index either of the form [y] or [x].
 *	The array indices are taken from scx->scx_x and scx->scx_y.  At most
 *	size characters are copied into the string pointed to by name.
 *
 *-----------------------------------------------------------------------------
 */

char *
DBPrintUseId(scx, name, size, display_only)
    SearchContext *scx;	/* Pointer to current search context, specifying a
			 * cell use and X,Y array indices.
			 */
    char *name;		/* Pointer to string into which we will copy the
			 * print name of this instance.
			 */
    int size;		/* Maximum number of characters to copy into string. */
    bool display_only;	/* TRUE if called for displaying only */
{
    CellUse *use = scx->scx_use;
    char *sp, *id, *ep;
    char indices[100];

    if ((id = use->cu_id) == (char *) NULL)
    {
	name[0] = '\0';
	return (name);
    }

    sp = name;
    if (display_only && (use->cu_flags & CU_LOCKED))
	*sp++ = CULOCKCHAR;

    for (ep = &name[size]; (sp < ep) && *id; *sp++ = *id++)
	/* Nothing */;

    if (use->cu_xlo != use->cu_xhi || use->cu_ylo != use->cu_yhi)
    {
	if (use->cu_xlo == use->cu_xhi)
	    (void) sprintf(indices, "[%d]", scx->scx_y);
	else if (use->cu_ylo == use->cu_yhi)
	    (void) sprintf(indices, "[%d]", scx->scx_x);
	else
	    (void) sprintf(indices, "[%d,%d]", scx->scx_y, scx->scx_x);

	for (id = indices; (sp < ep) && *id; *sp++ = *id++)
	    /* Nothing */;
    }

    if (sp == ep)
	sp--;
    *sp = '\0';

    return (sp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellSetAvail --
 * DBCellClearAvail --
 *
 * Mark a cell as available/unavailable.
 * These exist mainly to create a cell for the first time, and to
 * allow a cell to be 'flushed' via the "flush" command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies flags in cellDef.
 *
 * ----------------------------------------------------------------------------
 */

void
DBCellSetAvail(cellDef)
    CellDef *cellDef;		/* Pointer to definition of cell we wish to
				 * mark as available.
				 */
{
    cellDef->cd_flags &= ~CDNOTFOUND;
    cellDef->cd_flags |= CDAVAILABLE;
}

void
DBCellClearAvail(cellDef)
    CellDef *cellDef;		/* Pointer to definition of cell we wish to
				 * mark as available.
				 */
{
    cellDef->cd_flags &= ~(CDNOTFOUND|CDAVAILABLE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBCellGetModified --
 * DBCellSetModified --
 *
 * Get/set the CDMODIFIED status of a cell.
 * DBCellGetModified returns TRUE if the cell has been modified, FALSE
 * if not.  DBCellSetModified marks the cell as having been modified
 * if its argument is TRUE, and marks it as having been unmodified if
 * its argument is FALSE.
 *
 * Results:
 *	DBCellGetModified: TRUE or FALSE.
 *	DBCellSetModified: None.
 *
 * Side effects:
 *	DBCellGetModified: None.
 *	DBCellSetModified: modifies cellDef->cd_flags.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBCellGetModified(cellDef)
    CellDef *cellDef;	/* Pointer to definition of cell */
{
    return ((cellDef->cd_flags & CDMODIFIED) != 0);
}

void
DBCellSetModified(cellDef, ismod)
    CellDef *cellDef;
    bool ismod;		/* If TRUE, mark the cell as modified; if FALSE,
			 * mark it as unmodified.
			 */
{
    cellDef->cd_flags &= ~CDMODIFIED|CDGETNEWSTAMP;
    if (ismod)
	cellDef->cd_flags |= CDMODIFIED|CDGETNEWSTAMP;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBComputeUseBbox --
 *
 * Compute the bounding box for a CellUse in coordinates of its parent.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets cellUse->cu_bbox to be the bounding box for the indicated CellUse
 *	in coordinates of that CellUse's parent,  and cellUse->cu_extended
 *	to the bounding box of the cell use including all rendered text labels.
 *
 * ----------------------------------------------------------------------------
 */

void
DBComputeUseBbox(use)
    CellUse *use;
{
    Rect *box, *extended;
    Rect childRect, childExtend;
    int xdelta, ydelta;

    xdelta = use->cu_xsep * (use->cu_xhi - use->cu_xlo);
    ydelta = use->cu_ysep * (use->cu_yhi - use->cu_ylo);
    if (xdelta < 0) xdelta = (-xdelta);
    if (ydelta < 0) ydelta = (-ydelta);

    box = &(use->cu_def->cd_bbox);
    extended = &(use->cu_def->cd_extended);

    if (use->cu_xsep < 0)
    {
	childRect.r_xbot = box->r_xbot - xdelta;
	childRect.r_xtop = box->r_xtop;
	childExtend.r_xbot = extended->r_xbot - xdelta;
	childExtend.r_xtop = extended->r_xtop;
    }
    else
    {
	childRect.r_xbot = box->r_xbot;
	childRect.r_xtop = box->r_xtop + xdelta;
	childExtend.r_xbot = extended->r_xbot;
	childExtend.r_xtop = extended->r_xtop + xdelta;
    }

    if (use->cu_ysep < 0)
    {
	childRect.r_ybot = box->r_ybot - ydelta;
	childRect.r_ytop = box->r_ytop;
	childExtend.r_ybot = extended->r_ybot - ydelta;
	childExtend.r_ytop = extended->r_ytop;
    }
    else
    {
	childRect.r_ybot = box->r_ybot;
	childRect.r_ytop = box->r_ytop + ydelta;
	childExtend.r_ybot = extended->r_ybot;
	childExtend.r_ytop = extended->r_ytop + ydelta;
    }

    GeoTransRect(&use->cu_transform, &childRect, &use->cu_bbox);
    GeoTransRect(&use->cu_transform, &childExtend, &use->cu_extended);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBIsChild --
 *
 * Test to see if cu1 is a child of cu2.
 *
 * Results:
 *	TRUE if cu1 is a child of cu2, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBIsChild(cu1, cu2)
    CellUse *cu1, *cu2;
{
    return (cu1->cu_parent == cu2->cu_def);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBSetArray --
 *
 * Copy the array information from fromCellUse to toCellUse
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The array information if toCellUse is modified.
 *
 * ----------------------------------------------------------------------------
 */

void
DBSetArray(fromCellUse, toCellUse)
    CellUse *fromCellUse;
    CellUse *toCellUse;
{
    toCellUse->cu_xlo = fromCellUse->cu_xlo;
    toCellUse->cu_ylo = fromCellUse->cu_ylo;
    toCellUse->cu_xhi = fromCellUse->cu_xhi;
    toCellUse->cu_yhi = fromCellUse->cu_yhi;
    toCellUse->cu_xsep = fromCellUse->cu_xsep;
    toCellUse->cu_ysep = fromCellUse->cu_ysep;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBSetTrans --
 *
 * Change the transform for cellUse to that supplied.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Updates cellUse->cu_trans and cellUse->cu_bbox
 *
 * ----------------------------------------------------------------------------
 */

void
DBSetTrans(cellUse, trans)
    CellUse *cellUse;
    Transform *trans;
{
    cellUse->cu_transform = *trans;
    DBComputeUseBbox(cellUse);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBMakeArray --
 *
 * Turn cellUse into an array whose X indices run from xlo through xhi
 * and whose Y indices run from ylo through yhi.  The separation between
 * adjacent array elements is xsep in the X direction, and ysep in the
 * Y direction.
 *
 * The X and Y information is in coordinates of the root cell def.
 * It gets transformed down to the def of cellUse according to the
 * transform supplied.  What we do guarantee is that the array
 * indices will appear, in root coordinates, to run from xlo to xhi
 * left-to-right, and from ylo to yhi bottom-to-top.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The array information if toCellUse is modified.
 *
 * ----------------------------------------------------------------------------
 */

void
DBMakeArray(cellUse, rootToCell, xlo, ylo, xhi, yhi, xsep, ysep)
    CellUse *cellUse;
    Transform *rootToCell;
    int xlo, ylo;
    int xhi, yhi;
    int xsep, ysep;
{
    int t;

    cellUse->cu_xsep = rootToCell->t_a * xsep + rootToCell->t_b * ysep;
    cellUse->cu_ysep = rootToCell->t_d * xsep + rootToCell->t_e * ysep;

    /*
     * Now transform the indices.
     * We should preserve the property that indices in root coordinates
     * should appear to run from xlo through xhi, left-to-right, and
     * from ylo through yhi, bottom-to-top.
     */

    if (rootToCell->t_a == 0)
    {
	t = xlo; xlo = ylo; ylo = t;
	t = xhi; xhi = yhi; yhi = t;
    }

    cellUse->cu_xlo = xlo;
    cellUse->cu_ylo = ylo;
    cellUse->cu_xhi = xhi;
    cellUse->cu_yhi = yhi;

    DBComputeUseBbox(cellUse);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBArrayOverlap --
 *
 * Determine which elements of an array overlap the supplied clipping
 * rectangle.  Assumes that the clipping rectangle overlaps at least
 * some part of the array area.
 *
 * Results:
 *	None.
 *
 * WARNING:
 *	This code is very sensitive to being changed.  Make sure you
 *	understand it before you change it.
 *
 * Side Effects:
 *	Sets *pxlo, *pxhi, *pylo, *pyhi to be the inclusive range of array
 *	indices which overlay the given clipping rectangle.
 *
 *	If there is any overlap in X, *pxlo <= *pxhi; similarly, if there
 *	is any overlap in Y, *pylo <= *pyhi.
 *
 * ----------------------------------------------------------------------------
 */

void
DBArrayOverlap(cu, parentRect, pxlo, pxhi, pylo, pyhi)
    CellUse *cu;	/* Pointer to cell use which may be an array */
    Rect *parentRect;		/* Clipping rectangle cu->cu_parent coords */
    int *pxlo, *pxhi;
    int *pylo, *pyhi;
{
    int outxlo, outxhi, outylo, outyhi, t;
    int xlo, ylo, xhi, yhi, xsep, ysep;
    Transform parentToCell;
    Rect box, childR;

    /* For a non-arrayed element, return the indices of the only element */
    if (cu->cu_xlo == cu->cu_xhi && cu->cu_ylo == cu->cu_yhi)
    {
	*pxlo = *pxhi = cu->cu_xlo;
	*pylo = *pyhi = cu->cu_ylo;
	return;
    }

    box = cu->cu_def->cd_bbox;
    GEOINVERTTRANS(&cu->cu_transform, &parentToCell);
    GEOTRANSRECT(&parentToCell, parentRect, &childR);
    xsep = cu->cu_xsep;
    ysep = cu->cu_ysep;

    /*
     * Canonicalize the array indices so that the base element
     * of the array has the minimum x and y coordinate, ie,
     * so that xlo <= xhi and ylo <= yhi.
     */
    if (cu->cu_xlo > cu->cu_xhi) xlo = cu->cu_xhi, xhi = cu->cu_xlo;
    else			 xlo = cu->cu_xlo, xhi = cu->cu_xhi;

    if (cu->cu_ylo > cu->cu_yhi) ylo = cu->cu_yhi, yhi = cu->cu_ylo;
    else			 ylo = cu->cu_ylo, yhi = cu->cu_yhi;


    /*
     * If the separation along one of the coordinate axes is negative,
     * flip everything about that axis.
     */
    if (xsep < 0)
    {
	xsep = (-xsep);
	t = childR.r_xbot; childR.r_xbot = -childR.r_xtop; childR.r_xtop = -t;
	t = box.r_xbot; box.r_xbot = -box.r_xtop; box.r_xtop = -t;
    }

    if (ysep < 0)
    {
	ysep = (-ysep);
	t = childR.r_ybot; childR.r_ybot = -childR.r_ytop; childR.r_ytop = -t;
	t = box.r_ybot; box.r_ybot = -box.r_ytop; box.r_ytop = -t;
    }

    /*
     * The following inequalities are used to derive the equations
     * computed below.  "Blo" is the lower coordinate of the incident
     * box, and "Bhi" is the upper coordinate.
     *
     *	  min outlo : (outlo - lo) * sep + top >= Blo
     *	  max outhi : (outhi - lo) * sep + bot <= Bhi
     *
     * The intent is that "outlo" will be the smaller of the two
     * coordinates, and "outhi" the larger.
     */
    
    /* Even though it should never happen, handle zero spacings
     * gracefully.
     */

    if (xsep != 0)
    {
	outxlo = xlo + (childR.r_xbot - box.r_xtop + xsep - 1) / xsep;
	outxhi = xlo + (childR.r_xtop - box.r_xbot) / xsep;
    }
    else
    {
	outxlo = xlo;
	outxhi = xhi;
    }
    if (ysep != 0)
    {
	outylo = ylo + (childR.r_ybot - box.r_ytop + ysep - 1) / ysep;
	outyhi = ylo + (childR.r_ytop - box.r_ybot) / ysep;
    }
    else
    {
	outylo = ylo;
	outyhi = yhi;
    }

    /*
     * Clip against the canonicalized array indices.
     * Note that this may result in rxlo > rxhi or rylo > ryhi, in which
     * case the rectangle doesn't intersect the array at all.
     */
    if (outxlo < xlo) outxlo = xlo;
    if (outxhi > xhi) outxhi = xhi;
    if (outylo < ylo) outylo = ylo;
    if (outyhi > yhi) outyhi = yhi;

    /*
     * Convert canonicalized array indices back into actual
     * array indices for output.
     */
    if (cu->cu_xlo > cu->cu_xhi)
    {
	*pxhi = cu->cu_xhi + cu->cu_xlo - outxlo;
	*pxlo = cu->cu_xhi + cu->cu_xlo - outxhi;
    }
    else
    {
	*pxlo = outxlo;
	*pxhi = outxhi;
    }

    if (cu->cu_ylo > cu->cu_yhi)
    {
	*pyhi = cu->cu_yhi + cu->cu_ylo - outylo;
	*pylo = cu->cu_yhi + cu->cu_ylo - outyhi;
    }
    else
    {
	*pylo = outylo;
	*pyhi = outyhi;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBReComputeBbox --
 * DBReComputeBboxVert --
 *
 * Propagate changes to bounding boxes.
 * The former is the default procedure; the latter should be used
 * when the tile planes of a cell are organized into maximal vertical
 * strips instead of maximal horizontal.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	These procedures re-examine cellDef to see if its bounding box
 *	has gotten larger or smaller.  If so, the bounding box is
 *	modified, and the change is reflected upwards in the
 *	hierarchy to parents of cellDef.  This occurs recursively
 *	until the top of the hierarchy is reached.
 *
 *	Also modifies the CellUse bounding boxes of parents as
 *	appropriate (cu_bbox).
 *
 *				WARNING
 *
 *	In order to preserve consistency, if it is discovered that a
 *	cell contains no material of any kind, its bounding box is
 *	set to a default of (0,0) :: (1,1).  If the cell contains
 *	labels but still has a zero-size bounding box, then the bounding
 *	box is enlarged by one unit to give it non-zero area.
 *
 * ----------------------------------------------------------------------------
 */

void dbReComputeBboxFunc();

void
DBReComputeBbox(cellDef)
    CellDef *cellDef;	/* Cell def whose bounding box may have changed */
{
    extern bool DBBoundPlane();

    dbReComputeBboxFunc(cellDef, DBBoundPlane, DBReComputeBbox);
}

void
DBReComputeBboxVert(cellDef)
    CellDef *cellDef;	/* Cell def whose bounding box may have changed */
{
    extern bool DBBoundPlaneVert();

    dbReComputeBboxFunc(cellDef, DBBoundPlaneVert, DBReComputeBboxVert);
}

void
dbReComputeBboxFunc(cellDef, boundProc, recurseProc)
    CellDef *cellDef;	/* Cell def whose bounding box may have changed */
    bool (*boundProc)();
    void (*recurseProc)();
{
    bool degenerate;
    Rect rect, area, extended, *box;
    Rect redisplayArea;
    CellUse *use;
    CellDef *parent;
    Label *label;
    bool foundAny;
    int pNum;

    /* Cells which declare their bounding box to be fixed	*/
    /* must return immediately.					*/

    if (cellDef->cd_flags & CDFIXEDBBOX) return;

    /*
     * Include area of subcells separately
     */
    if ((foundAny = DBBoundCellPlane(cellDef, TRUE, &rect)) > 0)
	area = rect;

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (pNum != PL_DRC_CHECK)
	    if ((*boundProc)(cellDef->cd_planes[pNum], &rect))
	    {
		if (foundAny)
		    (void) GeoInclude(&rect, &area);
		else
		    area = rect;
		foundAny = TRUE;
	    }

    /*
     * Include the area of labels, too.
     */
    for (label = cellDef->cd_labels; label != NULL;  label = label->lab_next)
    {
	if (foundAny)
	{
	    if (label->lab_rect.r_xbot < area.r_xbot)
		area.r_xbot = label->lab_rect.r_xbot;
	    if (label->lab_rect.r_ybot < area.r_ybot)
		area.r_ybot = label->lab_rect.r_ybot;
	    if (label->lab_rect.r_xtop > area.r_xtop)
		area.r_xtop = label->lab_rect.r_xtop;
	    if (label->lab_rect.r_ytop > area.r_ytop)
		area.r_ytop = label->lab_rect.r_ytop;
	}
	else
	{
	    area = label->lab_rect;
	    foundAny = TRUE;
	}
    }

    extended = area;
    if (foundAny)
    {
	for (label = cellDef->cd_labels; label != NULL;  label = label->lab_next)
	    if (label->lab_font >= 0)
		GeoInclude(&label->lab_bbox, &extended);
    }

    /*
     * If there is nothing at all, produce a 1x1 box with its
     * lower left corner at the origin.
     */
    if (!foundAny)
    {
	degenerate = TRUE;
	area.r_xbot = area.r_ybot = 0;
	area.r_xtop = area.r_ytop = 1;
    }
    else degenerate = FALSE;

    /*
     * Canonicalize degeneracy in all other directions by
     * expanding the box by one unit in the degenerate
     * direction.
     */
    if (area.r_xbot == area.r_xtop)
	area.r_xtop = area.r_xbot + 1;

    if (area.r_ybot == area.r_ytop)
	area.r_ytop = area.r_ybot + 1;

    if (degenerate) extended = area;

    /* Did the bounding box change?  If not then there's no need to
     * recompute the parents.  If the cell has no material, then
     * we must always propagate upwards.  This is because DBClearCellDef
     * sets the bounding box to the degenerate size WITHOUT propagating
     * (it isn't safe for DBClearCellDef to propagate).  At this point
     * the propagation must be done.
     */
    box = &cellDef->cd_extended;
    if ((area.r_xbot == box->r_xbot && area.r_ybot == box->r_ybot
	    && area.r_xtop == box->r_xtop && area.r_ytop == box->r_ytop)
	    && !degenerate)
	return;

    /*
     * Alas, the cell's bounding box has actually changed.  Thus
     * we must recompute the areas of all the parents of this
     * cell recursively.  We must rip up the child cell and place
     * it down again to insure that the subcell tile planes in the
     * parents are all correct.
     *
     * The undo package is disabled around this since it will already
     * insure that DBReComputeBbox will be called at the appropriate
     * times.
     */

    UndoDisable();
    for (use = cellDef->cd_parents; use != NULL; use = use->cu_nextuse)
	if (use->cu_parent != (CellDef *) NULL)
	{
	    /* DBDeleteCell trashes the parent pointer, but we restore
	     * it so we'll know where to put the cell back later.
	     */

	    parent = use->cu_parent;
	    DBDeleteCell(use);
	    use->cu_parent = parent;
	}

    cellDef->cd_bbox = area;
    cellDef->cd_extended = extended;

    /*
     * Relink each of the uses into the subcell tile planes
     * of their respective parents.  Also, redisplay the use
     * in each parent, in each window where the cell isn't
     * expanded (i.e. the bounding box is no longer correct).
     */

    for (use = cellDef->cd_parents; use != NULL; use = use->cu_nextuse)
    {
	redisplayArea = use->cu_extended;
	DBComputeUseBbox(use);
	if ((parent = use->cu_parent) != (CellDef *) NULL)
	{
	    parent->cd_flags |= CDBOXESCHANGED;
	    DBPlaceCell(use, parent);
	    (*recurseProc)(parent);
	    (void) GeoInclude(&use->cu_extended, &redisplayArea);
	    DBWAreaChanged(parent, &redisplayArea, (int) ~use->cu_expandMask,
		&DBAllButSpaceBits);
	}
    }
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBComputeArrayArea --
 *
 * Given an area in native coordinates of a celldef, computes the
 * corresponding area in a parent's coordinates, for a particular
 * celluse and a particular element of an array.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Sets *prect to the given area in the given array instance,
 *	subject to the arraying and transformation inforamtion in
 *	the given cellUse.
 *
 * ----------------------------------------------------------------------------
 */

void
DBComputeArrayArea(area, cellUse, x, y, prect)
    Rect *area;		/* Area to be transformed. */
    CellUse *cellUse;	/* Cell use whose bounding box is to be computed */
    int x, y;		/* Indexes of array element whose box is being found */
    Rect *prect;	/* Pointer to rectangle to be set to bounding
			 * box of the given array element, in coordinates
			 * of the def of cellUse.
			 */
{
    int xdelta, ydelta;

    if (cellUse->cu_xlo > cellUse->cu_xhi)	x = cellUse->cu_xlo - x;
    else					x = x - cellUse->cu_xlo;

    if (cellUse->cu_ylo > cellUse->cu_yhi)	y = cellUse->cu_ylo - y;
    else					y = y - cellUse->cu_ylo;

    xdelta = cellUse->cu_xsep * x;
    ydelta = cellUse->cu_ysep * y;

    prect->r_xbot = area->r_xbot + xdelta;
    prect->r_xtop = area->r_xtop + xdelta;
    prect->r_ybot = area->r_ybot + ydelta;
    prect->r_ytop = area->r_ytop + ydelta;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBGetArrayTransform --
 *
 * 	This procedure computes the transform from a particular element
 *	of an array to the coordinates of the array as a whole.
 *
 * Results:
 *	The return result is a pointer to a transform describing how
 *	coordinates of use->cu_def must be transformed in order to
 *	appear in the (x,y) element location.  In other words, if the
 *	transform for the whole array (use->cu_transform) were
 *	GeoIdentityTransform, this is the transform from use->cu_def
 *	to the parent use for the (x,y) element.  By the way, the
 *	return result is a locally-allocated transform that goes away
 *	the next time this procedure is called, so use it carefully.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

Transform *
DBGetArrayTransform(use, x, y)
    CellUse *use;
    int x, y;			/* Array indices of the desired element.
				 * These must fall within the range of
				 * use's array indices.
				 */
{
    static Transform result;
    int xsep, ysep, xbase, ybase;

    if (use->cu_xlo > use->cu_xhi) xsep = -use->cu_xsep;
    else xsep = use->cu_xsep;
    if (use->cu_ylo > use->cu_yhi) ysep = -use->cu_ysep;
    else ysep = use->cu_ysep;
    xbase = xsep * (x - use->cu_xlo);
    ybase = ysep * (y - use->cu_ylo);
    GeoTransTranslate(xbase, ybase, &GeoIdentityTransform, &result);
    return &result;
}
