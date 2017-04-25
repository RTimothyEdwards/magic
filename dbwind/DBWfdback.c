/* DBWfeedback.c -
 *
 *	This file provides a standard set of procedures for Magic
 *	commands to use to provide feedback to users.  Feedback
 *	consists of areas of the screen that are highlighted, along
 *	with text describing why those particular areas are important.
 *	Feedback is used for things like displaying CIF, and for errors
 *	in CIF-generation and routing.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWfdback.c,v 1.2 2010/06/24 12:37:16 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "utils/malloc.h"
#include "utils/signals.h"

/* Use a reference-counted character structure for feedback info */

typedef struct rcstring
{
    int refcount;
    char *string;
} RCString;
    

/* Each feedback area is stored in a record that looks like this: */

typedef struct feedback
{
    Rect fb_area;		/* The area to be highlighted, in coords of
				 * fb_rootDef, but prior to scaling by
				 * fb_scale.
				 */
    Rect fb_rootArea;		/* The area of the feedback, in Magic coords.
				 * of fb_rootDef, scaled up to the next
				 * integral Magic unit.
				 */
    RCString *fb_text;		/* Text explanation for the feedback. */
    CellDef *fb_rootDef;	/* Root definition of windows in which to
				 * display this feedback.
				 */
    int fb_scale;		/* Scale factor to use in redisplaying
				 * (fb_scale units in fb_area correspond
				 * to one unit in fb_rootDef).
				 */
    int fb_style;		/* Display style to use for this feedback. */
} Feedback;

/* The following stuff describes all the feedback information we know
 * about.  The feedback is stored in a big array that grows whenever
 * necessary.
 */

static Feedback *dbwfbArray = NULL;	/* Array holding all feedback info. */
global int DBWFeedbackCount = 0;	/* Number of active entries in
					 * dbwfbArray.
					 */
static int dbwfbSize = 0;		/* Size of dbwfbArray, in entries. */
static int dbwfbNextToShow = 0;		/* Index of first feedback area that
					 * hasn't been displayed yet.  Used by
					 * DBWFBShow.
					 */
static CellDef *dbwfbRootDef;		/* To pass root cell definition from
					 * dbwfbGetTransform back up to
					 * DBWFeedbackAdd.
					 */


/*
 * ----------------------------------------------------------------------------
 *
 * DBWFeedbackRedraw --
 *
 * 	This procedure is called by the highlight manager to redisplay
 *	feedback highlights.  The window is locked before entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any feedback information that overlaps a non-space tile in
 *	plane is redrawn.
 *
 * Tricky stuff:
 *	Redisplay is numerically difficult, particularly when feedbacks
 *	have a large internal scale factor:  the tendency is to get
 *	integer overflow and get everything goofed up.  Be careful
 *	when making changes to the code below.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWFeedbackRedraw(window, plane)
    MagWindow *window;		/* Window in which to redraw. */
    Plane *plane;		/* Non-space tiles on this plane mark what
				 * needs to be redrawn.
				 */
{
    dlong x, y;
    int i, halfScale, curStyle, newStyle, curScale;
    CellDef *windowRoot;
    Rect worldArea, screenArea, tmp;
    Feedback *fb;
    extern int dbwFeedbackAlways1();	/* Forward reference. */

    if (DBWFeedbackCount == 0) return;

    windowRoot = ((CellUse *) (window->w_surfaceID))->cu_def;
    curStyle = -1;
    curScale = -1;
    for (i = 0, fb = dbwfbArray; i < DBWFeedbackCount; i++, fb++)
    {
	/* We expect that most of the feedbacks will have the same
	 * style and scale, so compute information with the current
	 * values, and recompute only when the values change.  
	 */
	if (fb->fb_scale != curScale)
	{
	    curScale = fb->fb_scale;
	    halfScale = curScale / 2;
	    worldArea.r_xbot = window->w_surfaceArea.r_xbot * curScale;
	    worldArea.r_xtop = window->w_surfaceArea.r_xtop * curScale;
	    worldArea.r_ybot = window->w_surfaceArea.r_ybot * curScale;
	    worldArea.r_ytop = window->w_surfaceArea.r_ytop * curScale;
	}

	/*
	 * Check to make sure this feedback area is relevant.
	 * Clip it to TiPlaneRect as a sanity check against
	 * callers who provide a feedback area that extends
	 * out somewhere in never-never land.
	 */
	if (fb->fb_rootDef != windowRoot) continue;
	tmp = fb->fb_rootArea;
	GeoClip(&tmp, &TiPlaneRect);
	if (!DBSrPaintArea((Tile *) NULL, plane, &tmp,
		&DBAllButSpaceBits, dbwFeedbackAlways1, (ClientData) NULL))
	    continue;

	/* Transform the feedback area to screen coords.  This is
	 * very similar to the code in WindSurfaceToScreen, except
	 * that there's additional scaling involved.
	 */

	tmp = fb->fb_area;

	/* Don't clip non-Manhattan tiles, or else the diagonals are	*/
	/* clipped to an incorrect position.				*/

	if (!(fb->fb_style & TT_DIAGONAL))
	GeoClip(&tmp, &worldArea);

	/*
	 * Ignore areas which have been clipped out of existence
	 */
	if (tmp.r_xtop < tmp.r_xbot || tmp.r_ytop < tmp.r_ybot)
	    continue;

	x = ((dlong)(tmp.r_xbot - worldArea.r_xbot) * window->w_scale) + halfScale;
	screenArea.r_xbot = (int)((x/curScale + window->w_origin.p_x) >> SUBPIXELBITS);
	x = ((dlong)(tmp.r_xtop - worldArea.r_xbot) * window->w_scale) + halfScale;
	screenArea.r_xtop = (int)((x/curScale + window->w_origin.p_x) >> SUBPIXELBITS);
	y = ((dlong)(tmp.r_ybot - worldArea.r_ybot) * window->w_scale) + halfScale;
	screenArea.r_ybot = (int)((y/curScale + window->w_origin.p_y) >> SUBPIXELBITS);
	y = ((dlong)(tmp.r_ytop - worldArea.r_ybot) * window->w_scale) + halfScale;
	screenArea.r_ytop = (int)((y/curScale + window->w_origin.p_y) >> SUBPIXELBITS);

	/*
	 * Sanity check on integer bounds
	 */
	if (screenArea.r_xtop < screenArea.r_xbot ||
	    screenArea.r_ytop < screenArea.r_ybot)
	{
	    TxError("Internal error: Feedback area exceeds integer bounds "
		"on screen rectangle!\n");
	    continue;
	}

	newStyle = fb->fb_style & (TT_LEFTMASK | TT_RIGHTMASK);

	/* Another little trick:  when the feedback area is very small ("very 
	 * small" is a hand-tuned constant), change all stippled styles to
	 * solid.
	 * (The usefulness of this trick is questionable, as it generally
	 * creates very bothersome output for the "cif see" function and
	 * other places where the feedback is generated from tiles.)
	 */
/*
	if (((GEO_WIDTH(&screenArea) < 18) || (GEO_HEIGHT(&screenArea) < 18))
			&& ((newStyle == STYLE_MEDIUMHIGHLIGHTS)
			|| (newStyle == STYLE_PALEHIGHLIGHTS)))
	    newStyle = STYLE_SOLIDHIGHLIGHTS;
*/
	if (newStyle != curStyle)
	{
	    curStyle = newStyle;
	    GrSetStuff(curStyle);
	}

	if (fb->fb_style & TT_DIAGONAL)
	    GrDiagonal(&screenArea, fb->fb_style);

	/* Special case (i.e., "hack")---to get a diagonal line	*/
	/* instead of a triangle, set TT_SIDE but not		*/
	/* TT_DIAGONAL.	 TT_DIRECTION still indicates / or \	*/

	else if (fb->fb_style & TT_SIDE)
	{
	    if (fb->fb_style & TT_DIRECTION)
	        GrClipLine(screenArea.r_xbot, screenArea.r_ytop,
			screenArea.r_xtop, screenArea.r_ybot);
	    else
	        GrClipLine(screenArea.r_xbot, screenArea.r_ybot,
			screenArea.r_xtop, screenArea.r_ytop);
	}
	else
	    GrFastBox(&screenArea);
    }
}

int
dbwFeedbackAlways1()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWFeedbackClear --
 *
 * 	This procedure clears all existing feedback information from
 *	the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any existing feedback information is cleared from the screen
 *	and from our database.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWFeedbackClear(text)
    char *text;
{
    int i, oldCount;
    Feedback *fb, *fl, *fe;
    Rect area;
    CellDef *currentRoot;
    RCString *quickptr = NULL;

    /* Clear out the feedback array and recycle string storage.  Whenever
     * the root cell changes, make a call to erase from the screen.
     */

    currentRoot = (CellDef *) NULL;
    oldCount = DBWFeedbackCount;
    DBWFeedbackCount = 0;
    for (fb = dbwfbArray; fb < dbwfbArray + oldCount; fb++)
    {
	if ((text == NULL) ||
		((quickptr != NULL) && (fb->fb_text == quickptr)) ||
		(strstr(fb->fb_text->string, text) != NULL))
	{
	    // Track the last refcount string that matched the
	    // text so we can run through lists of matching text
	    // without having to call strstr()

	    if (text != NULL) quickptr = fb->fb_text;

	    if (currentRoot != fb->fb_rootDef)
	    {
		if (currentRoot != (CellDef *) NULL)
		    DBWHLRedraw(currentRoot, &area, TRUE);
		area = GeoNullRect;
	    }
	    (void) GeoInclude(&fb->fb_rootArea, &area);
	    currentRoot = fb->fb_rootDef;

	    // Decrement refcount and free if zero.
	    fb->fb_text->refcount--;
	    if (fb->fb_text->refcount == 0)
	    {
		freeMagic(fb->fb_text->string);
		freeMagic(fb->fb_text);
	    }
	    fb->fb_text = NULL;
	}
    }
    if (currentRoot != NULL)
	DBWHLRedraw(currentRoot, &area, TRUE);
    dbwfbNextToShow = 0;

    if (text != NULL)
    {
	/* Close up feedback list around removed text entries */

	fl = dbwfbArray;
	fe = fl + oldCount;
	for (fb = dbwfbArray; fb < fe; fb++)
	{
	    while ((fb->fb_text == NULL) && (fb < fe)) fb++;
	    if (fb < fe) *fl++ = *fb;
	}
	DBWFeedbackCount = (int)(fl - dbwfbArray);

	for (fb = fl; fb < dbwfbArray + oldCount; fb++)
	    fb->fb_text = NULL;
    }

    /* Free up feedback memory whenever we have removed all feedback entries */
    if (DBWFeedbackCount == 0)
    {
	if (dbwfbArray != NULL) {
	    freeMagic((char *) dbwfbArray);
	    dbwfbArray = NULL;
	}
	dbwfbSize = 0;
	return;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWFeedbackAdd --
 *
 * 	Adds a new piece of feedback information to the list we have
 *	already.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	CellDef's ancestors are searched until its first root definition
 *	is found, and the coordinates of area are transformed into the
 *	root.  Then the feedback area is added to our current list, using
 *	the style and scalefactor given.  This stuff will be displayed on
 *	the screen at the end of the current command.
 * ----------------------------------------------------------------------------
 */

void
DBWFeedbackAdd(area, text, cellDef, scaleFactor, style)
    Rect *area;			/* The area to be highlighted. */
    char *text;			/* Text associated with the area. */
    CellDef *cellDef;		/* The cellDef in whose coordinates area
				 * is given.
				 */
    int scaleFactor;		/* The coordinates provided for feedback
				 * areas are divided by this to produce
				 * coordinates in Magic database units.
				 * This will probably be 1 most of the time.
				 * By making it bigger, say 10, and scaling
				 * other coordinates appropriately, it's
				 * possible to draw narrow lines on the
				 * screen, or to handle CIF, which isn't in
				 * exactly the same coordinates as other Magic
				 * stuff.
				 */
    int style;			/* A display style to use for the feedback.
				 * Use one of:
				 * STYLE_OUTLINEHIGHLIGHTS:	solid outlines
				 * STYLE_DOTTEDHIGHLIGHTS:	dotted outlines
				 * STYLE_SOLIDHIGHLIGHTS:	solid fill
				 * STYLE_MEDIUMHIGHLIGHTS:	medium stipple
				 * STYLE_PALEHIGHLIGHTS:	pald stipple
				 * At very coarse viewing scales, the last
				 * two styles are hard to see, so they are
				 * turned into STYLE_SOLIDHIGHLIGHTS.
				 */
{
    Rect tmp, tmp2, tmp3;
    Transform transform;
    int i;
    Feedback *fb, *fblast;
    extern int dbwfbGetTransform();	/* Forward declaration. */

    /* Find a transform from this cell to the root, and use it to
     * transform the area.  If the root isn't an ancestor, just
     * return.
     */
    
    if (!DBSrRoots(cellDef, &GeoIdentityTransform,
	dbwfbGetTransform, (ClientData) &transform)) return;

    /* SigInterruptPending screws up DBSrRoots */
    if (SigInterruptPending)
	return;

    /* Don't get fooled like I did.  The translations for
     * this transform are in Magic coordinates, not feedback
     * coordinates.  Scale them into feedback coordinates.
     */
    
    transform.t_c *= scaleFactor;
    transform.t_f *= scaleFactor;
    GeoTransRect(&transform, area, &tmp2);
    area = &tmp2;

    /* Make sure there's enough space in the current array.  If
     * not, make a new array, copy the old to the new, then delete
     * the old array.  Use memcpy() to make sure this happens very fast.
     */
    
    if (DBWFeedbackCount == dbwfbSize)
    {
	Feedback *new;

	if (dbwfbSize == 0) dbwfbSize = 32;
	else dbwfbSize <<= 1;

	new = (Feedback *) mallocMagic(dbwfbSize * sizeof(Feedback));
	memcpy((char *)new, (char *)dbwfbArray, DBWFeedbackCount * sizeof(Feedback));

	for (i = DBWFeedbackCount; i < dbwfbSize; i++) new[i].fb_text = NULL;

	if (dbwfbArray != NULL) freeMagic((char *) dbwfbArray);
	dbwfbArray = new;
    }
    fb = &dbwfbArray[DBWFeedbackCount];
    fb->fb_area = *area;
    fblast = (DBWFeedbackCount == 0) ? NULL : &dbwfbArray[DBWFeedbackCount - 1];

    if (fblast && (!strcmp(fblast->fb_text->string, text)))
    {
	fb->fb_text = fblast->fb_text;
	fb->fb_text->refcount++;
    }
    else
    {
	fb->fb_text = (RCString *)mallocMagic(sizeof(RCString));
	fb->fb_text->refcount = 1;
	fb->fb_text->string = StrDup(NULL, text);
    }

    fb->fb_rootDef = dbwfbRootDef;
    fb->fb_scale = scaleFactor;
    fb->fb_style = style;
    DBWFeedbackCount++;

    /* Round the area up into Magic coords, and save it too. */
    if (area->r_xtop > 0)
	tmp.r_xtop = (area->r_xtop + scaleFactor - 1)/scaleFactor;
    else tmp.r_xtop = area->r_xtop/scaleFactor;
    if (area->r_ytop > 0)
	tmp.r_ytop = (area->r_ytop + scaleFactor - 1)/scaleFactor;
    else tmp.r_ytop = area->r_ytop/scaleFactor;
    if (area->r_xbot > 0) tmp.r_xbot = area->r_xbot/scaleFactor;
    else tmp.r_xbot = (area->r_xbot - scaleFactor + 1)/scaleFactor;
    if (area->r_ybot > 0) tmp.r_ybot = area->r_ybot/scaleFactor;
    else tmp.r_ybot = (area->r_ybot - scaleFactor + 1)/scaleFactor;

    /* Clip to ensure well within TiPlaneRect */
    tmp3.r_xbot = TiPlaneRect.r_xbot + 10;
    tmp3.r_ybot = TiPlaneRect.r_ybot + 10;
    tmp3.r_xtop = TiPlaneRect.r_xtop - 10;
    tmp3.r_ytop = TiPlaneRect.r_ytop - 10;
    GeoClip(&tmp, &tmp3);

    fb->fb_rootArea = tmp;
}

/* This utility procedure is invoked by DBSrRoots.  Save the root definition
 * in dbwfbRootDef, save the transform in the argument, and abort the search.
 * Make sure that the root we pick is actually displayed in a window
 * someplace (there could be root cells that are no longer displayed
 * anywhere).
 */

int
dbwfbGetTransform(use, transform, cdarg)
    CellUse *use;			/* A root use that is an ancestor
					 * of cellDef in DBWFeedbackAdd.
					 */
    Transform *transform;		/* Transform up from cellDef to use. */
    Transform *cdarg;			/* Place to store transform from
					 * cellDef to its root def.
					 */
{
    extern int dbwfbWindFunc();
    if (use->cu_def->cd_flags & CDINTERNAL) return 0;
    if (!WindSearch((ClientData) DBWclientID, (ClientData) use,
	    (Rect *) NULL, dbwfbWindFunc, (ClientData) NULL)) return 0;
    if (SigInterruptPending)
	return 0;
    dbwfbRootDef = use->cu_def;
    *cdarg = *transform;
    return 1;
}

/* This procedure is called if a window is found for the cell in
 * dbwfbGetTransform above.  It returns 1 to abort the search and
 * notify dbwfbGetTransform that there was a window for that root
 * cell.
 */

int
dbwfbWindFunc()
{
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * Client initialization
 *
 * ----------------------------------------------------------------------------
 */

void
dbwFeedbackInit()
{
    DBWHLAddClient(DBWFeedbackRedraw);
}


/*
 * ----------------------------------------------------------------------------
 *
 * DBWFeedbackShow --
 *
 * 	Causes new feedback information actually to be displayed on
 *	the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All new feedback information that has been created since the
 *	last call to this procedure is added to the display.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWFeedbackShow()
{
    Rect area;
    CellDef *currentRoot;
    Feedback *fb;
    int i;

    /* Scan through all of the feedback areas starting with dbwfbNextToShow.
     * Save up the total bounding box until the root definition changes,
     * then redisplay what's been saved up so far.
     */

    currentRoot = NULL;
    for (i = dbwfbNextToShow, fb = &(dbwfbArray[dbwfbNextToShow]);
	i < DBWFeedbackCount; i++, fb++)
    {
	if (currentRoot != fb->fb_rootDef)
	{
	    if (currentRoot != NULL)
		DBWHLRedraw(currentRoot, &area, FALSE);
	    area = GeoNullRect;
	}
	(void) GeoInclude(&fb->fb_rootArea, &area);
	currentRoot = fb->fb_rootDef;
    }
    if (currentRoot != NULL)
	DBWHLRedraw(currentRoot, &area, FALSE);
    dbwfbNextToShow = DBWFeedbackCount;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWFeedbackNth --
 *
 * 	Provides the area and text associated with a particular
 *	feedback area.
 *
 * Results:
 *	Returns a pointer to the text associated with the Nth feedback
 *	entry.
 *
 * Side effects:
 *	The parameter "area" is filled with the area of the nth
 *	feedback, and the text of that feedback is returned. *pRootDef
 *	is filled in with rootDef for window of feedback area.  *pStyle
 *	is filled in with the display style for the feedback area.  If
 *	the particular area doesn't exist (nth >= DBWFeedbackCount),
 *	area and *pRootDef  and *pStyle are untouched and NULL is
 *	returned.  NULL	may also be returned if there simply wasn't
 *	any text associated with the selected feedback.
 *
 * ----------------------------------------------------------------------------
 */

char *
DBWFeedbackNth(nth, area, pRootDef, pStyle)
    int nth;			/* Selects which feedback area to return
				 * stuff from.  (0 <= nth < DBWFeedbackCount)
				 */
    Rect *area;			/* To be filled in with area of feedback, in
				 * rounded-outward Magic coordinates.
				 */
    CellDef **pRootDef;		/* *pRootDef gets filled in with root def for
				 * this feedback area.  If pRootDef is NULL,
				 * nothing is touched.
				 */
    int *pStyle;		/* *pStyle gets filled in with the display
				 * style for this feedback area.  If NULL,
				 * nothing is touched.
				 */
{
    if (nth >= DBWFeedbackCount) return NULL;
    *area = dbwfbArray[nth].fb_rootArea;
    if (pRootDef != NULL) *pRootDef = dbwfbArray[nth].fb_rootDef;
    if (pStyle != NULL) *pStyle = dbwfbArray[nth].fb_style;
    return dbwfbArray[nth].fb_text->string;
}
