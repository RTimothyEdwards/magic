/*
 * CalmaReadpaint.c --
 *
 * Input of Calma GDS-II stream format.
 * Processing of paint (paths, boxes, and boundaries) and text.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/calma/CalmaRdpt.c,v 1.7 2010/08/25 17:33:54 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>             /* for abs() */
#include <string.h>		/* for strlen() */
#include <sys/types.h>

#include <netinet/in.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"
#include "utils/tech.h"
#include "cif/cif.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "textio/textio.h"
#include "calma/calmaInt.h"
#include "calma/calma.h"

extern int calmaNonManhattan;

extern int CalmaPolygonCount;
extern HashTable calmaDefInitHash;

extern void calmaLayerError();
bool calmaReadPath();

/*
 * ----------------------------------------------------------------------------
 *
 * calmaInputRescale ---
 *
 *	This routine does the same thing as CIFInputRescale().  However,
 *	the "gds flatten" option allows us to retain GDS layout
 *	information in the cd_client record of a cell def.  If we
 *	change the GDS input scale factor, then all of these saved
 *	layouts need to be rescaled.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Reallocates memory for layout planes in each of the cells in
 *	the database that have the CDFLATGDS flag set.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaInputRescale(n, d)
    int n, d;
{
    HashEntry *h;
    HashSearch hs;
    CellDef *def;

    HashStartSearch(&hs);
    while (TRUE)
    {
	h = HashNext(&CifCellTable, &hs);
	if (h == NULL) break;

	def = (CellDef *) HashGetValue(h);
	if (def == NULL) continue;	 /* shouldn't happen */
	if (def->cd_flags & CDFLATGDS)
	{
	    /* Scale the GDS planes in this cell's cd_client record */
	    Plane **gdsplanes = (Plane **)def->cd_client;
	    CIFScalePlanes(n, d, gdsplanes);
	}
    }

    CIFInputRescale(n, d);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadPoint ---
 *
 * Read a point from the input.
 * We take care of scaling by calmaReadScale1/calmaReadScale2.
 * Also take care of noting when the scaling results in a sub-integer
 * value, and rescaling everything appropriately.  "iscale" is an
 * integer scaling value used to return values at, for instance, double
 * the scale, as for a path centerline.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The Point pointed to by parameter "p" is filled with the
 *	coordinates of the point.  If a fractional integer is
 *	encountered, then everything in the GDS planes is rescaled
 *	to match.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaReadPoint(p, iscale)
    Point *p;
    int iscale;
{
    int rescale;

    READI4((p)->p_x);
    p->p_x *= (calmaReadScale1 * iscale);
    if ((iscale != 0) && (p->p_x % calmaReadScale2 != 0))
    {
	rescale = calmaReadScale2 / FindGCF(calmaReadScale2, abs(p->p_x));
	if ((calmaReadScale1 * rescale) > CIFRescaleLimit)
	{
	    calmaReadError("Warning:  calma units at max scale; value rounded\n");
	    if (p->p_x < 0)
		p->p_x -= ((calmaReadScale2 - 1) >> 1);
	    else
		p->p_x += (calmaReadScale2 >> 1);
	}
	else
	{
	    calmaReadScale1 *= rescale;
	    calmaInputRescale(rescale, 1);
	    p->p_x *= rescale;
	}
    }
    p->p_x /= calmaReadScale2;

    READI4((p)->p_y);
    p->p_y *= (calmaReadScale1 * iscale);
    if ((iscale != 0) && (p->p_y % calmaReadScale2 != 0))
    {
	rescale = calmaReadScale2 / FindGCF(calmaReadScale2, abs(p->p_y));
	if ((calmaReadScale1 * rescale) > CIFRescaleLimit)
	{
	    calmaReadError("Warning:  calma units at max scale; value rounded\n");
	    if (p->p_y < 0)
		p->p_y -= ((calmaReadScale2 - 1) >> 1);
	    else
		p->p_y += (calmaReadScale2 >> 1);
	}
	else
	{
	    calmaReadScale1 *= rescale;
	    calmaInputRescale(rescale, 1);
	    p->p_x *= rescale;
	    p->p_y *= rescale;
	}
    }
    p->p_y /= calmaReadScale2;
}


/*
 * ----------------------------------------------------------------------------
 *
 * calmaElementBoundary --
 *
 * Read a polygon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints one or more rectangles into one of the CIF planes.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaElementBoundary()
{
    int dt, layer, ciftype;
    CIFPath *pathheadp;
    LinkedRect *rp;
    Plane *plane;
    CellUse *use;
    CellDef *savedef, *newdef = NULL;

    /* Skip CALMA_ELFLAGS, CALMA_PLEX */
    calmaSkipSet(calmaElementIgnore);

    /* Read layer and data type */
    if (!calmaReadI2Record(CALMA_LAYER, &layer)
	    || !calmaReadI2Record(CALMA_DATATYPE, &dt))
    {
	calmaReadError("Missing layer or datatype in boundary/box.\n");
	return;
    }

    /* Set current plane */
    ciftype = CIFCalmaLayerToCifLayer(layer, dt, cifCurReadStyle);
    if (ciftype < 0)
    {
	plane = NULL;
	calmaLayerError("Unknown layer/datatype in boundary", layer, dt);
    }
    else
	plane = cifCurReadPlanes[ciftype];

    /* Read the path itself, building up a path structure */
    if (!calmaReadPath(&pathheadp, (plane == NULL) ? 0 : 1)) 
    {
	if (plane != NULL)
	    calmaReadError("Error while reading path for boundary/box; ignored.\n");
	return;
    }

    /* Note that calmaReadPath() may reallocate planes of cifCurReadPlanes */
    /* so we need to set it again.					   */
    if (ciftype >= 0) plane = cifCurReadPlanes[ciftype];

    /* Convert the polygon to rectangles. */

    if (CalmaSubcellPolygons && (calmaNonManhattan > 0))
    {
	/* Place the polygon in its own subcell */
	char newname[] = "polygonXXXXX";
	HashEntry *he;

	savedef = cifReadCellDef;

	/* Make up name for cell */
	sprintf(newname + 7, "%05d", ++CalmaPolygonCount);

	he = HashFind(&calmaDefInitHash, newname);
	if (!HashGetValue(he))
	{
	    newdef = calmaFindCell(newname, NULL);
	    cifReadCellDef = newdef;
	    DBCellClearDef(cifReadCellDef);
	    DBCellSetAvail(cifReadCellDef);

	    /* cifEditCellPlanes is not used by the gds reader, so it's */
	    /* available to be used to store the polygon.		*/

	    cifCurReadPlanes = cifEditCellPlanes;
	    if (plane != NULL)
		plane = cifCurReadPlanes[ciftype];
	}
    }

    rp = CIFPolyToRects(pathheadp, plane, CIFPaintTable, (PaintUndoInfo *)NULL);
    CIFFreePath(pathheadp);

    /* Paint the rectangles (if any) */
    for (; rp != NULL ; rp = rp->r_next)
    {
	if (plane)
	    DBPaintPlane(plane, &rp->r_r, CIFPaintTable, (PaintUndoInfo *)NULL);
	freeMagic((char *) rp);
    }

    if (cifCurReadPlanes == cifEditCellPlanes)
    {
	CIFPaintCurrent();
	DBReComputeBbox(cifReadCellDef);
	DRCCheckThis(cifReadCellDef, TT_CHECKPAINT, &cifReadCellDef->cd_bbox);
	DBWAreaChanged(cifReadCellDef, &cifReadCellDef->cd_bbox,
		DBW_ALLWINDOWS, &DBAllButSpaceBits);
	DBCellSetModified(cifReadCellDef, TRUE);
	DBGenerateUniqueIds(cifReadCellDef, FALSE);  /* Is this necessary? */

	cifCurReadPlanes = cifSubcellPlanes;
	cifReadCellDef = savedef;

	use = DBCellNewUse(newdef, (char *)NULL);
	DBSetTrans(use, &GeoIdentityTransform);
	DBPlaceCell(use, cifReadCellDef);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaElementBox --
 *
 * Read a box.
 * This is an optimized version of calmaElementBoundary
 * that handles rectangular polygons.  These polygons each
 * have five vertex points, with the first and last point
 * being the same, and all sides parallel to one of the two
 * coordinate axes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints one rectangle into one of the CIF planes.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaElementBox()
{
    int nbytes, rtype, npoints, savescale;
    int dt, layer, ciftype;
    Plane *plane;
    Point p;
    Rect r;

    /* Skip CALMA_ELFLAGS, CALMA_PLEX */
    calmaSkipSet(calmaElementIgnore);

    /* Read layer and data type */
    if (!calmaReadI2Record(CALMA_LAYER, &layer)
	    || !calmaReadI2Record(CALMA_BOXTYPE, &dt))
    {
	calmaReadError("Missing layer or datatype in boundary/box.\n");
	return;
    }

    /* Set current plane */
    ciftype = CIFCalmaLayerToCifLayer(layer, dt, cifCurReadStyle);
    if (ciftype < 0)
    {
	calmaLayerError("Unknown layer/datatype in box", layer, dt);
	return;
    }
    else plane = cifCurReadPlanes[ciftype];

    /*
     * Read the path itself.
     * Since it is Manhattan, we can build our rectangle directly.
     */
    r.r_xbot = r.r_ybot = INFINITY;
    r.r_xtop = r.r_ytop = MINFINITY;

    /* Read the record header */
    READRH(nbytes, rtype);
    if (nbytes < 0)
    {
	calmaReadError("EOF when reading box.\n");
	return;
    }
    if (rtype != CALMA_XY)
    {
	calmaUnexpected(CALMA_XY, rtype);
	return;
    }

    /* Read this many points (pairs of four-byte integers) */
    npoints = (nbytes - CALMAHEADERLENGTH) / 8;
    if (npoints != 5)
    {
	calmaReadError("Box doesn't have 5 points.\n");
	(void) calmaSkipBytes(nbytes - CALMAHEADERLENGTH);
	return;
    }
    while (npoints-- > 0)
    {
	savescale = calmaReadScale1;
	calmaReadPoint(&p, 1);
	if (savescale != calmaReadScale1)
	{
	    int newscale = calmaReadScale1 / savescale;
	    r.r_xbot *= newscale;
	    r.r_xtop *= newscale;
	    r.r_ybot *= newscale;
	    r.r_ytop *= newscale;
	}
	if (p.p_x < r.r_xbot) r.r_xbot = p.p_x;
	if (p.p_y < r.r_ybot) r.r_ybot = p.p_y;
	if (p.p_x > r.r_xtop) r.r_xtop = p.p_x;
	if (p.p_y > r.r_ytop) r.r_ytop = p.p_y;
    }

    /* Paint the rectangle */
    DBPaintPlane(plane, &r, CIFPaintTable, (PaintUndoInfo *)NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaElementPath --
 *
 * Read a centerline wire.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May paint rectangles into CIF planes.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaElementPath()
{
    int nbytes, rtype, extend1, extend2;
    int layer, dt, width, pathtype, ciftype, savescale;
    int xmin, ymin, xmax, ymax, temp;
    CIFPath *pathheadp, *pathp, *previousp;
    Rect segment;
    Plane *plane;
    int first,last;
    CellUse *use;
    CellDef *savedef, *newdef = NULL;

    /* Skip CALMA_ELFLAGS, CALMA_PLEX */
    calmaSkipSet(calmaElementIgnore);

    /* Grab layer and datatype */
    if (!calmaReadI2Record(CALMA_LAYER, &layer)) return;
    if (!calmaReadI2Record(CALMA_DATATYPE, &dt)) return;

    /* Describes the shape of the ends of the path */
    pathtype = CALMAPATH_SQUAREFLUSH;
    PEEKRH(nbytes, rtype);
    if (nbytes > 0 && rtype == CALMA_PATHTYPE)
	if (!calmaReadI2Record(CALMA_PATHTYPE, &pathtype)) return;

    if (pathtype != CALMAPATH_SQUAREFLUSH && pathtype != CALMAPATH_SQUAREPLUS)
    {
	calmaReadError("Warning: pathtype %d unsupported (ignored).\n", pathtype);
	pathtype = CALMAPATH_SQUAREFLUSH;
    }

    /*
     * Width of this path.
     * Allow zero-width paths; we will ignore them later.
     */
    width = 0;
    PEEKRH(nbytes, rtype) 
    if (nbytes > 0 && rtype == CALMA_WIDTH)
    {
	if (!calmaReadI4Record(CALMA_WIDTH, &width)) 
	{
	    calmaReadError("Error in reading WIDTH in calmaElementPath()\n") ;
	    return;
	}
    }
    width *= calmaReadScale1;
    if (width % calmaReadScale2 != 0)
	calmaReadError("Wire width snapped to nearest integer boundary.\n");

    width /= calmaReadScale2;

    /* Handle BGNEXTN, ENDEXTN */
    extend1 = extend2 = 0;
    PEEKRH(nbytes, rtype);
    if (nbytes > 0 && rtype == CALMA_BGNEXTN)
    {
	if (!calmaReadI4Record(CALMA_BGNEXTN, &extend1))
	    calmaReadError("Error in reading BGNEXTN in path (ignored)\n") ;
	else
	{
	    extend1 *= calmaReadScale1;
	    if (extend1 % calmaReadScale2 != 0)
		calmaReadError("Wire extension snapped to nearest integer boundary.\n");
	    extend1 *= 2;
	    extend1 /= calmaReadScale2;
	}
    }

    PEEKRH(nbytes, rtype);
    if (nbytes > 0 && rtype == CALMA_ENDEXTN)
    {
	if (!calmaReadI4Record(CALMA_ENDEXTN, &extend2))
	    calmaReadError("Error in reading ENDEXTN in path (ignored)\n") ;
	else
	{
	    extend2 *= calmaReadScale1;
	    if (extend2 % calmaReadScale2 != 0)
		calmaReadError("Wire extension snapped to nearest integer boundary.\n");
	    extend2 *= 2;
	    extend2 /= calmaReadScale2;
	}
    }

    /* Read the points in the path */
    savescale = calmaReadScale1;
    if (!calmaReadPath(&pathheadp, 2)) 
    {
	calmaReadError("Improper path; ignored.\n");
	return;
    }
    if (savescale != calmaReadScale1)
    {
	width *= (calmaReadScale1 / savescale);
	extend1 *= (calmaReadScale1 / savescale);
	extend2 *= (calmaReadScale1 / savescale);
    }

    /* Create path end extensions */

    if (extend1 > 0)
    {
	if (pathheadp->cifp_x > pathheadp->cifp_next->cifp_x)
	    pathheadp->cifp_x += extend1;
	else if (pathheadp->cifp_x < pathheadp->cifp_next->cifp_x)
	    pathheadp->cifp_x -= extend1;

	if (pathheadp->cifp_y > pathheadp->cifp_next->cifp_y)
	    pathheadp->cifp_y += extend1;
	else if (pathheadp->cifp_y < pathheadp->cifp_next->cifp_y)
	    pathheadp->cifp_y -= extend1;
    }

    if (extend2 > 0)
    {
	pathp = pathheadp;
	while (pathp && pathp->cifp_next && pathp->cifp_next->cifp_next)
	    pathp = pathp->cifp_next;

	if (pathp && pathp->cifp_next)
	{
	    if (pathp->cifp_x > pathp->cifp_next->cifp_x)
		pathp->cifp_next->cifp_x -= extend2;
	    else if (pathp->cifp_x < pathp->cifp_next->cifp_x)
		pathp->cifp_next->cifp_x += extend2;

	    if (pathp->cifp_y > pathp->cifp_next->cifp_y)
		pathp->cifp_next->cifp_y -= extend2;
	    else if (pathp->cifp_y < pathp->cifp_next->cifp_y)
		pathp->cifp_next->cifp_y += extend2;
	}
    }

    /* Don't process zero-width paths any further */
    if (width <= 0)
    {
	CIFFreePath(pathheadp);
	return;
    }

    /* Make sure we know about this type */
    ciftype = CIFCalmaLayerToCifLayer(layer, dt, cifCurReadStyle);
    if (ciftype < 0)
    {
	calmaLayerError("Unknown layer/datatype in path", layer, dt);
	CIFFreePath(pathheadp);
    }
    else
    {
	plane = cifCurReadPlanes[ciftype];

	if (CalmaSubcellPolygons && (calmaNonManhattan > 0))
	{
	    /* Place the polygon in its own subcell */
	    char newname[] = "polygonXXXXX";
	    HashEntry *he;

	    savedef = cifReadCellDef;

	    /* Make up name for cell */
	    sprintf(newname + 7, "%05d", ++CalmaPolygonCount);

	    he = HashFind(&calmaDefInitHash, newname);
	    if (!HashGetValue(he))
	    {
		newdef = calmaFindCell(newname, NULL);
		cifReadCellDef = newdef;
		DBCellClearDef(cifReadCellDef);
		DBCellSetAvail(cifReadCellDef);

		/* cifEditCellPlanes is not used by the gds reader, so it's */
		/* available to be used to store the polygon.		*/

		cifCurReadPlanes = cifEditCellPlanes;
		if (plane != NULL)
		    plane = cifCurReadPlanes[ciftype];
	    }
	}

	CIFPaintWirePath(pathheadp, width,
		(pathtype == CALMAPATH_SQUAREFLUSH) ? FALSE : TRUE,
		plane, CIFPaintTable, (PaintUndoInfo *)NULL);

	if (cifCurReadPlanes == cifEditCellPlanes)
	{
	    CIFPaintCurrent();
	    DBReComputeBbox(cifReadCellDef);
	    DRCCheckThis(cifReadCellDef, TT_CHECKPAINT, &cifReadCellDef->cd_bbox);
	    DBWAreaChanged(cifReadCellDef, &cifReadCellDef->cd_bbox,
			DBW_ALLWINDOWS, &DBAllButSpaceBits);
	    DBCellSetModified(cifReadCellDef, TRUE);
	    DBGenerateUniqueIds(cifReadCellDef, FALSE);  /* Is this necessary? */

	    cifCurReadPlanes = cifSubcellPlanes;
	    cifReadCellDef = savedef;

	    use = DBCellNewUse(newdef, (char *)NULL);
	    DBSetTrans(use, &GeoIdentityTransform);
	    DBPlaceCell(use, cifReadCellDef);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaElementText --
 *
 * Read labels.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Add labels to our label list.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaElementText()
{
    static int ignore[] = { CALMA_PATHTYPE, CALMA_WIDTH, -1 };
    char *textbody = NULL;
    int nbytes, rtype;
    int layer, textt, cifnum;
    TileType type;
    Rect r;
    unsigned short textpres;
    double dval;
    int size, angle, font, pos;

    /* Skip CALMA_ELFLAGS, CALMA_PLEX */
    calmaSkipSet(calmaElementIgnore);

    /* Grab layer and texttype */
    if (!calmaReadI2Record(CALMA_LAYER, &layer)) return;
    if (!calmaReadI2Record(CALMA_TEXTTYPE, &textt)) return;
    cifnum = CIFCalmaLayerToCifLayer(layer, textt, cifCurReadStyle);
    if (cifnum < 0)
    {
	if(cifCurReadStyle->crs_flags & CRF_IGNORE_UNKNOWNLAYER_LABELS) 
	    type = -1;
	else {
	    calmaLayerError("Label on unknown layer/datatype", layer, textt);
	    type = TT_SPACE;
	}
    }
    else type = cifCurReadStyle->crs_labelLayer[cifnum];

    font = -1;
    angle = 0;

    /* Default size is 1um */
    size = (int)((800 * cifCurReadStyle->crs_multiplier)
				/ cifCurReadStyle->crs_scaleFactor);
    /* Default position is bottom-right (but what the spec calls "top-left"!) */
    pos = GEO_SOUTHEAST;

    /* Parse presentation and magnitude/angle part of transform */
    /* Skip pathtype and width */

    PEEKRH(nbytes, rtype);
    if (nbytes > 0 && rtype == CALMA_PRESENTATION)
    {
	calmaReadI2Record(CALMA_PRESENTATION, &textpres);
	font = (textpres >> 4) & 0x03;
	switch (textpres & 0x000f)
	{
	    case 0x000a:
		pos = GEO_NORTHWEST;
		break;
	    case 0x0006:
		pos = GEO_WEST;
		break;
	    case 0x0002:
		pos = GEO_SOUTHWEST;
		break;
	    case 0x0009:
		pos = GEO_NORTH;
		break;
	    case 0x0005:
		pos = GEO_CENTER;
		break;
	    case 0x0001:
		pos = GEO_SOUTH;
		break;
	    case 0x0008:
		pos = GEO_NORTHEAST;
		break;
	    case 0x0004:
		pos = GEO_EAST;
		break;
	    case 0x0000:
		pos = GEO_SOUTHEAST;
		break;
	}
    }
    else if (nbytes > 0 && rtype != CALMA_STRANS)
	calmaSkipSet(ignore);

    READRH(nbytes, rtype);
    if (nbytes > 0 && rtype == CALMA_STRANS)
    {
	/* We don't handle the strans record for text */
	calmaSkipBytes(nbytes - CALMAHEADERLENGTH);

	READRH(nbytes, rtype);
	if (nbytes > 0 && rtype == CALMA_MAG)
	{
	    calmaReadR8(&dval);
	    /* Assume that MAG is the label size in microns	*/
	    /* "size" is the label size in 8 * (database units) */
	    size = (int)((dval * 800 * cifCurReadStyle->crs_multiplier)
				/ cifCurReadStyle->crs_scaleFactor);
	}
	else
	    UNREADRH(nbytes, rtype);

	READRH(nbytes, rtype);
	if (nbytes > 0 && rtype == CALMA_ANGLE)
	{
	    calmaReadR8(&dval);
	    angle = (int)dval;
	}
	else
	    UNREADRH(nbytes, rtype);
    }
    else
	UNREADRH(nbytes, rtype);


    /* Coordinates of text */
    READRH(nbytes, rtype)
    if (nbytes < 0) return;
    if (rtype != CALMA_XY)
    {
	calmaUnexpected(CALMA_XY, rtype);
	return;
    }
    nbytes -= CALMAHEADERLENGTH;
    if (nbytes < 8)
    {
	calmaReadError("Not enough bytes in point record.\n");
    }
    else
    {
	calmaReadPoint(&r.r_ll, 1);
	nbytes -= 8;
    }
    if (!calmaSkipBytes(nbytes)) return;
    r.r_ll.p_x /= cifCurReadStyle->crs_scaleFactor;
    r.r_ll.p_y /= cifCurReadStyle->crs_scaleFactor;
    r.r_ur = r.r_ll;

    /* String itself */
    if (!calmaReadStringRecord(CALMA_STRING, &textbody)) return;

    /* Eliminate characters not in the official GDSII string format set. */

    /* NOTE:  Disabling this 10/3/2014.  I can think of no reason that
     * other ASCII characters may not be parsed out of a non-compliant
     * GDS file.  If we want to generate GDS-compliant output, there's
     * a flag for that in the "cifoutput" section of the techfile.
     */

#if 0 

    {
	static bool algmsg = FALSE;
	bool changed = FALSE;
	char *cp;
	char *savstring;
	for (cp = textbody; *cp; cp++)
	{
	    if (*cp <= ' ' | *cp > '~') 
	    {
		if (!changed)
		{
		    savstring = StrDup(NULL, textbody);
		    changed = TRUE;
		}
		if (*cp == '\r' && *(cp+1) == '\0')
		    *cp = '\0';
		else if (*cp == '\r') 
		    *cp = '_';
		else if (*cp == ' ')
		    *cp = '_';
		else
		    *cp = '?';
	    }
	}
	if (changed) {
	    calmaReadError("Warning:  improper characters fixed in label '%s'\n",
			savstring);
	    if (!algmsg) {
		algmsg = TRUE;
		calmaReadError("    (algorithm used:  trailing <CR> dropped, "
				"<CR> and ' ' changed to '_', \n"
				"    other non-printables changed to '?')\n");
	    }
	    calmaReadError("	modified label is '%s'\n", textbody);
	    freeMagic(savstring);
	}
    }

#endif /* 0 */

    /* Place the label */
    if (strlen(textbody) == 0)
    {
	calmaReadError("Warning:  Ignoring empty string label at (%d, %d)\n",
		r.r_ll.p_x * cifCurReadStyle->crs_scaleFactor,
		r.r_ll.p_y * cifCurReadStyle->crs_scaleFactor);
    }
    else if (type < 0)
    {
	calmaReadError("Warning:  label \"%s\" at (%d, %d) is on unhandled"
		" layer:purpose pair %d:%d and will be discarded.\n", textbody,
		r.r_ll.p_x * cifCurReadStyle->crs_scaleFactor,
		r.r_ll.p_y * cifCurReadStyle->crs_scaleFactor, layer, textt);
    }
    else
    {
	int flags, i;

        /* Find the style layer record corresponding to the label type */
        layer = -1;
        for (i = 0; i < cifCurReadStyle->crs_nLayers; i++)
            if (cifCurReadStyle->crs_layers[i]->crl_magicType == type) {
                layer = i;
                break;
            }

	if (layer >= 0 && (cifCurReadStyle->crs_layers[layer]->crl_flags
			& CIFR_TEXTLABELS))
	    flags = LABEL_STICKY;
	else if (cifCurReadStyle->crs_flags & CRF_NO_RECONNECT_LABELS)
	    flags = LABEL_STICKY;
	else
	    flags = 0;

	if (font < 0)
	    DBPutLabel(cifReadCellDef, &r, pos, textbody, type, flags);
	else
	    DBPutFontLabel(cifReadCellDef, &r, font, size, angle,
			&GeoOrigin, pos, textbody, type, flags);
    }

    /* done with textbody */
    if (textbody != NULL) freeMagic(textbody);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaReadPath --
 *
 * This procedure parses a Calma path, which is an XY record
 * containing one or more points.  "iscale" is an internal
 * scaling, usually 1, but is 2 for paths specified on a
 * centerline, to avoid roundoff errors.
 *
 * Results:
 *	TRUE is returned if the path was parsed successfully,
 *	FALSE otherwise.
 *
 * Side effects:
 *	Modifies the parameter pathheadpp to point to the path
 *	that is constructed.
 *
 * ----------------------------------------------------------------------------
 */

bool
calmaReadPath(pathheadpp, iscale)
    CIFPath **pathheadpp;
    int iscale;
{
    CIFPath path, *pathtailp, *newpathp;
    int nbytes, rtype, npoints, savescale;
    bool nonManhattan = FALSE;

    *pathheadpp = (CIFPath *) NULL;
    pathtailp = (CIFPath *) NULL;
    path.cifp_next = (CIFPath *) NULL;

    /* Read the record header */
    READRH(nbytes, rtype);
    if (nbytes < 0)
    {
	calmaReadError("EOF when reading path.\n");
	return (FALSE);
    }
    if (rtype != CALMA_XY)
    {
	calmaUnexpected(CALMA_XY, rtype);
	return (FALSE);
    }

    /* Read this many points (pairs of four-byte integers) */
    npoints = (nbytes - CALMAHEADERLENGTH) / 8;
    while (npoints--)
    {
	savescale = calmaReadScale1;
	calmaReadPoint(&path.cifp_point, iscale);
	if (savescale != calmaReadScale1)
	{
	    CIFPath *phead = *pathheadpp;
	    int newscale = calmaReadScale1 / savescale;
	    while (phead != NULL)
	    {
		phead->cifp_x *= newscale;
		phead->cifp_y *= newscale;
		phead = phead->cifp_next;
	    }
	}
	if (ABS(path.cifp_x) > 0x0fffffff || ABS(path.cifp_y) > 0x0fffffff) {
	    calmaReadError("Warning:  Very large point in path:  (%d, %d)\n",
		path.cifp_x, path.cifp_y);
	}
	if (feof(calmaInputFile))
	{
	    CIFFreePath(*pathheadpp);
	    return (FALSE);
	}

	if (iscale != 0)
	{
	    newpathp = (CIFPath *) mallocMagic((unsigned) (sizeof (CIFPath)));
	    *newpathp = path;

	    if (*pathheadpp)
	    {
		/*
		 * Check that this segment is Manhattan.  If not, remember the
		 * fact and later introduce extra stair-steps to make the path
		 * Manhattan.  We don't do the stair-step introduction here for
		 * two reasons: first, the same code is also used by the Calma
		 * module, and second, it is important to know which side of
		 * the polygon is the outside when generating the stair steps.
		 */
		if (pathtailp->cifp_x != newpathp->cifp_x
			&& pathtailp->cifp_y != (newpathp->cifp_y))
		{
		    if (!nonManhattan)
		    {
			calmaNonManhattan++;
			nonManhattan = TRUE;
		    }
		}
		pathtailp->cifp_next = newpathp;
	    }
	    else *pathheadpp = newpathp;
	    pathtailp = newpathp;
	}
    }
    return (*pathheadpp != NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * calmaLayerError --
 *
 * This procedure is called when (layer, dt) doesn't map to a valid
 * Calma layer.  The first time this procedure is called for a given
 * (layer, dt) pair, we print an error message; on subsequent times,
 * no error message is printed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An error message is printed if the first time for this (layer, dt).
 *	Adds an entry to the HashTable calmaLayerHash if one is not
 *	already present for this (layer, dt) pair.
 *
 * ----------------------------------------------------------------------------
 */

void
calmaLayerError(mesg, layer, dt)
    char *mesg;
    int layer;
    int dt;
{
    CalmaLayerType clt;
    HashEntry *he;

    clt.clt_layer = layer;
    clt.clt_type = dt;
    he = HashFind(&calmaLayerHash, (char *) &clt);
    if (HashGetValue(he) == NULL)
    {
	HashSetValue(he, (ClientData) 1);
	calmaReadError("%s, layer=%d type=%d\n", mesg, layer, dt);
    }
}
