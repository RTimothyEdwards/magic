/* windMove.c -
 *
 *	This file contains the functions which move windows around on
 *	the screen.  It does not contain the functions that change the
 *	contents of the windows.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windMove.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"

/* The following own variable is used to pass information between
 * WindReframe and windFindUnobscured.
 */

static MagWindow *sharedW;

/*
 * By default, new windows have scroll bars, border, a title caption,
 * and allow standard window commands.
 */
int WindDefaultFlags = WIND_SCROLLBARS | WIND_CAPTION |
			WIND_BORDER | WIND_COMMANDS |
			WIND_SCROLLABLE;

/*
 * A mask of the current window IDs, as well as a limit on the number of
 * windows we can create.
 */

int windWindowMask = 0;  /* One bit per window ID */
int windMaxWindows = 32; /* May be decreased via the WIND_MAX_WINDOWS() macro */
int windCurNumWindows = 0;


/*
 * ----------------------------------------------------------------------------
 * windUnlink --
 *
 *	Unlink a window from the doubly linked list of windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is unlinked.
 * ----------------------------------------------------------------------------
 */

void
windUnlink(w)
    MagWindow *w;
{
    ASSERT(w != (MagWindow *) NULL, "windUnlink");
    ASSERT(windTopWindow != (MagWindow *) NULL, "windUnlink");
    ASSERT(windBottomWindow != (MagWindow *) NULL, "windUnlink");
    ASSERT(windTopWindow->w_prevWindow == (MagWindow *) NULL, "windUnlink");
    ASSERT(windBottomWindow->w_nextWindow == (MagWindow *) NULL, "windUnlink");

    if ( (windTopWindow == w) || (windBottomWindow == w) )
    {
	if (windTopWindow == w)
	{
	    windTopWindow = w->w_nextWindow;
	    if (windTopWindow != (MagWindow *) NULL)
		windTopWindow->w_prevWindow = (MagWindow *) NULL;
	}
	if (windBottomWindow == w)
	{
	    windBottomWindow = w->w_prevWindow;
	    if (windBottomWindow != (MagWindow *) NULL)
		windBottomWindow->w_nextWindow = (MagWindow *) NULL;
	}
    }
    else
    {
       w->w_nextWindow->w_prevWindow = w->w_prevWindow;
       w->w_prevWindow->w_nextWindow = w->w_nextWindow;
    }

    w->w_nextWindow = (MagWindow *) NULL;
    w->w_prevWindow = (MagWindow *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * windFree --
 *
 * 	This local procedure does the dirty work of freeing up
 *	memory in a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All storage associated with w is returned to the free pool.
 *
 * ----------------------------------------------------------------------------
 */

void
windFree(w)
    MagWindow *w;
{
    windWindowMask &= ~(1 << w->w_wid);
    windCurNumWindows--;
    if (w->w_caption != (char *) NULL) freeMagic(w->w_caption);
    if (w->w_iconname != (char *) NULL) freeMagic(w->w_iconname);
    if (GrFreeBackingStorePtr != NULL) (*GrFreeBackingStorePtr)(w);
    if (w->w_redrawAreas != (ClientData) NULL) {
	DBFreePaintPlane( (Plane *) w->w_redrawAreas);
	TiFreePlane( (Plane *) w->w_redrawAreas);
    }
    freeMagic( (char *) w);
}
 
/*
 * ----------------------------------------------------------------------------
 * WindSetWindowAreas --
 *
 *	Given the location of the window on the screen, compute w->w_allArea
 *	and w->w_screenArea in the window's own coordinate system.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies w->w_allArea and w->w_screenArea.
 * ----------------------------------------------------------------------------
 */

void
WindSetWindowAreas(w)
    MagWindow *w;
{
    switch ( WindPackageType )
    {
	case WIND_X_WINDOWS:
	    /* Windows have origin at lower-left corner */
	    w->w_allArea.r_xbot = w->w_allArea.r_ybot = 0;
	    w->w_allArea.r_xtop = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
	    w->w_allArea.r_ytop = w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
	    break;
	
	default:
	    /* Windows are all in the same coordinate system */
	    w->w_allArea = w->w_frameArea;
    }
    WindOutToIn(w, &w->w_allArea, &w->w_screenArea);
}

 
/*
 * ----------------------------------------------------------------------------
 * windSetWindowPosition --
 *
 *	(deprecated function)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
windSetWindowPosition(w)
    MagWindow *w;
{
}

/*
 * ----------------------------------------------------------------------------
 * WindDelete --
 *
 *	Delete a window.
 *
 * Results:
 *	TRUE if the window was deleted, FALSE otherwise.
 *
 * Side effects:
 *	The window disappears from the sreen.  The window's client is notified 
 *	that this is about to happen, and it may refuse to let it happen.
 * ----------------------------------------------------------------------------
 */

bool
WindDelete(w)
    MagWindow *w;
{
    clientRec *cr;

    ASSERT(w != (MagWindow *) NULL, "WindDelete");

    cr = (clientRec *) w->w_client;
    if ( (cr->w_delete == NULL) || (*(cr->w_delete))(w) )
    {
	WindAreaChanged(w, &(w->w_allArea) );
	if (GrDeleteWindowPtr != NULL) (*GrDeleteWindowPtr)(w);
	windUnlink(w);
	windReClip();
	windFree(w);
	return TRUE;
    }
    else
	return FALSE;
}


/*
 * ----------------------------------------------------------------------------
 * WindCreate --
 *
 *	Create a new window for the specified client.
 *
 * Results:
 *	A pointer to the new window, or NULL if one couldn't be created.
 *
 * Side effects:
 *	An empty window is created, and it is displayed.
 *	The new window is place on top of all the other
 *	windows.
 * ----------------------------------------------------------------------------
 */

MagWindow *
WindCreate(client, frameArea, isHint, argc, argv)
    WindClient client;		/* The client that will control this window */
    Rect *frameArea;		/* The area that the window is to occupy */
    bool isHint;		/* TRUE if the above rectangle is only a 
				 * hint and it is OK for a window package to
				 * override it to maintain a consistent
				 * user interface.
				 */
    int argc;			/* Passed to the client */
    char *argv[];
{
    MagWindow *w;
    clientRec *cr;
    bool OK;
    int id;

    if (windCurNumWindows + 1 > windMaxWindows) {
	TxError("Can't have more than %d windows.\n", windMaxWindows);
	return NULL;
    }
    windCurNumWindows++;

    cr = (clientRec *) client;

    /* initialize the window */
    w = (MagWindow *) mallocMagic( sizeof(MagWindow) );
    w->w_client = client;
    w->w_flags = WindDefaultFlags;
    w->w_clipAgainst = (LinkedRect *) NULL;
    w->w_caption = (char *) NULL;
    w->w_stippleOrigin.p_x = 0;
    w->w_stippleOrigin.p_y = 0;
    w->w_bbox = NULL;
    w->w_grdata = (ClientData) NULL;
    w->w_backingStore = (ClientData)NULL;
    w->w_redrawAreas = (ClientData) NULL;
    w->w_iconname = NULL;
    for (id = 0; ((1 << id) & windWindowMask) != 0; id++) /* advance id */ ;
    windWindowMask |= (1 << id);
    w->w_wid = id;

    /* locate window on the screen */
    if (frameArea == (Rect *) NULL)
    {
	switch ( WindPackageType )
	{
	    case WIND_X_WINDOWS:
		/*
		 * Create default size window in upper left corner
		 * of screen.
		 */
		w->w_frameArea.r_xbot = GrScreenRect.r_xbot;
		w->w_frameArea.r_ytop = GrScreenRect.r_ytop;
		w->w_frameArea.r_xtop =
		    (GrScreenRect.r_xtop - GrScreenRect.r_xbot) / 2;
		w->w_frameArea.r_ybot =
		    (GrScreenRect.r_ytop - GrScreenRect.r_ybot) / 2;
		break;
	    
	    default:
		w->w_frameArea = GrScreenRect;
	}
    }
    else
	w->w_frameArea = *frameArea;

    WindSetWindowAreas(w);

    /* now link the window in on top */
    w->w_nextWindow = windTopWindow;
    w->w_prevWindow = (MagWindow *) NULL;
    if (windTopWindow == (MagWindow *) NULL)
	windBottomWindow = w;
    else
	windTopWindow->w_prevWindow = w;
    windTopWindow = w;

    /* notify the client */
    OK = ((cr->w_create == NULL) || (*(cr->w_create))(w, argc, argv));

#ifdef THREE_D
    if (strcmp(cr->w_clientName, "wind3d"))
#endif

    if (OK && (GrCreateWindowPtr != NULL))
	OK = (*GrCreateWindowPtr)(w, (argc > 1) ? argv[1] : NULL);

    if (OK)
    {
	WindSetWindowAreas(w);
	windSetWindowPosition(w);
	WindAreaChanged(w, &(w->w_allArea));
    }
    else
    {
	/* the client refused the new window */
	windUnlink(w);
	windFree(w);
	w = (MagWindow *) NULL;
    }
    windReClip();
    if ((GrCreateBackingStorePtr != NULL) && (w != NULL) &&
		(!(w->w_flags & WIND_OBSCURED)))
	(*GrCreateBackingStorePtr)(w);

    return w;
}

/*
 * ----------------------------------------------------------------------------
 *
 * WindOutToIn and WindInToOut --
 *
 * 	The two procedures on this page translate from window inside
 *	area (the area used to display the surface) to window
 *	outside area (the total area of the window including caption),
 *	and vice versa.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Each procedure modifies its third parameter.  WindOutToIn
 *	fills in the third parameter with the inside area of the
 *	window whose outside area is out, and WindInToOut does the
 *	opposite.
 *
 * ----------------------------------------------------------------------------
 */

void
WindOutToIn(w, out, in)
    MagWindow *w;			/* Window under consideration */
    Rect *out;			/* Pointer to rectangle of outside area of
				 * a window.
				 */
    Rect *in;			/* Pointer to rectangle to be filled in with
				 * inside area corresponding to out.
				 */
{
    *in = *out;
    in->r_xbot += LEFT_BORDER(w);
    in->r_xtop -= RIGHT_BORDER(w);
    in->r_ybot += BOT_BORDER(w);
    in->r_ytop -= TOP_BORDER(w);
}

void WindInToOut(w, in, out)
    MagWindow *w;			/* Window under consideration */
    Rect *in;			/* Pointer to rectangle of outside area of
				 * a window.
				 */
    Rect *out;			/* Pointer to rectangle to be filled in with
				 * inside area corresponding to out.
				 */
{
    *out = *in;
    out->r_xbot -= LEFT_BORDER(w);
    out->r_xtop += RIGHT_BORDER(w);
    out->r_ybot -= BOT_BORDER(w);
    out->r_ytop += TOP_BORDER(w);
}


/*
 * ----------------------------------------------------------------------------
 * WindUnder --
 *
 *	Move a window underneath the rest.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is moved so that it is underneath the rest.  This will
 *	cause portions of uncovered windows to be redisplayed.
 * ----------------------------------------------------------------------------
 */

void
WindUnder(w)
    MagWindow *w;		/* the window to be moved */
{
    Rect area;
    MagWindow *w2;

    switch ( WindPackageType )
    {
	case WIND_X_WINDOWS:
	    if ( GrUnderWindowPtr )
		(*GrUnderWindowPtr)(w);
	    break;
	default:
	    /* Mark for redisplay all the areas that this window
	     * currently obscures.
	     */
	    
	    for (w2 = w->w_nextWindow; w2 != NULL; w2 = w2->w_nextWindow)
	    {
		area = w2->w_allArea;
		GeoClip(&area, &w->w_allArea);
		if ((area.r_xbot <= area.r_xtop) && (area.r_ybot <= area.r_ytop))
		    WindAreaChanged(w, &area);
	    }

	    /* take the window out of the linked list */
	    windUnlink(w);

	    /* now link it back in at the bottom */
	    w->w_prevWindow = windBottomWindow;
	    if (windBottomWindow != (MagWindow *) NULL)
		windBottomWindow->w_nextWindow = w;
	    else
		windTopWindow = w;
	    windBottomWindow = w;

	    windReClip();
    }
}


/*
 * ----------------------------------------------------------------------------
 * WindOver --
 *
 *	Move a window so that it is over (on top of) the other windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is moved to the top.  This may obscure some other windows.
 *	The window that is moved will be redisplayed.
 * ----------------------------------------------------------------------------
 */

void
WindOver(w)
    MagWindow *w;		/* the window to be moved */
{
    LinkedRect *r;
    Rect area;

    switch ( WindPackageType )
    {
	case WIND_X_WINDOWS:
	    if ( GrOverWindowPtr )
		(*GrOverWindowPtr)(w);
	    break;
	
	default:
	    /* Mark for redisplay all of the areas of the screen that
	     * currently obscure this window.
	     */
	    
	    for (r = w->w_clipAgainst; r != NULL; r = r->r_next)
	    {
		area = r->r_r;
		GeoClip(&area, &w->w_frameArea);
		if ((area.r_xbot <= area.r_xtop) && (area.r_ybot <= area.r_ytop))
		    WindAreaChanged((MagWindow *) NULL, &area);
	    }

	    /* take the window out of the linked list */
	    windUnlink(w);

	    /* now link it back in at the top */
	    w->w_nextWindow = windTopWindow;
	    if (windTopWindow != (MagWindow *) NULL)
		windTopWindow->w_prevWindow = w;
	    else
		windBottomWindow = w;
	    windTopWindow = w;

	    windReClip();
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * windFindUnobscured --
 *
 * 	Locates one portion of a rectangle that is unobscured, if
 *	any part of the rectangle is unobscured.  Used only by
 *	WindReframe.
 *
 * Results:
 *	Always returns TRUE.
 *
 * Side effects:
 *	The caller must place in the shared variable sharedW the
 *	name of a window, or NULL.  That window, and all windows
 *	above it, are checked to see if any obscure area.  If
 *	there is any unobscured part of area, it is placed in
 *	okArea.  If several distinct parts of area are unobscured,
 *	one, but only one, of them will be placed in okArea.
 *
 * ----------------------------------------------------------------------------
 */

int
windFindUnobscured(area, okArea)
    Rect *area;				/* Area that may be obscured. */
    Rect *okArea;			/* Modified to contain one of the
					 * unobscured areas.
					 */
{
    MagWindow *w;
    w = sharedW;
    if (w == NULL)
    {
	*okArea = *area;
	return FALSE;
    }
    sharedW = w->w_prevWindow;
    (void) GeoDisjoint(area, &w->w_frameArea, 
	windFindUnobscured, (ClientData) okArea);
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 * WindReframe --
 *
 *	Change the size or location of a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is moved, and areas of the screen are marked for
 *	redisplay.  This routine tries to be tricky in order to avoid
 *	massive redisplay for small changes.
 * ----------------------------------------------------------------------------
 */

void
WindReframe(w, r, inside, move)
    MagWindow *w;		/* the window to be reframed */
    Rect *r;		/* the new location in screen coordinates */
    bool inside;	/* TRUE if the rectangle is the screen location of
			 * the inside of the window, FALSE if the above
			 * rectangle includes border areas.
			 */
    bool move;		/* Move the coordinate system of the window the same
			 * amount as the lower left corner of the window?
			 */
{
    Rect newFrameArea;			/* New w_frameArea. */
    Rect dontRedisplay;			/* Used to record an area that does
					 * not have to be redisplayed.
					 */
    int xmove, ymove;			/* Distance window is moving. */
    extern int windReframeFunc();	/* Forward declaration. */
    clientRec *cr;

    cr = (clientRec *) w->w_client;

    /* Ensure that the new window size is not inside out and has some size to 
     * it.  Compute the new w_frameArea (in newFrameArea).
     */

    GeoCanonicalRect(r, &newFrameArea);
    if (inside) WindInToOut(w, &newFrameArea, &newFrameArea);

    if ((w->w_flags & WIND_ISICONIC) == 0) {
	/* Not iconic -- enforce a minimum size */
	newFrameArea.r_xtop = MAX(newFrameArea.r_xtop, 
	    newFrameArea.r_xbot + WIND_MIN_WIDTH);
	newFrameArea.r_ytop = MAX(newFrameArea.r_ytop, 
	newFrameArea.r_ybot + WIND_MIN_HEIGHT);
    }

    /* Give the client a chance to modify the change. */
    if (cr->w_reposition != NULL)
	(*(cr->w_reposition))(w, &newFrameArea, FALSE);


    /* If the window coordinates are moving, update the transform
     * so that the lower-left corner of the window remains at the
     * same location in surface coordinates.
     */

    if (move)
    {
	xmove = newFrameArea.r_xbot - w->w_frameArea.r_xbot;
	w->w_origin.p_x += xmove << SUBPIXELBITS;
	ymove = newFrameArea.r_ybot - w->w_frameArea.r_ybot;
	w->w_origin.p_y += ymove << SUBPIXELBITS;
	w->w_stippleOrigin.p_x += xmove;
	w->w_stippleOrigin.p_y += ymove;
    }

    switch ( WindPackageType )
    {
	case WIND_X_WINDOWS:
	    break;
	
	default:
	    /* Now comes the tricky part:  figuring out what to redisplay.
	     * The simple way out is to force redisplay at both the old and
	     * new window positions.  Naturally this code is going to be more
	     * ambitious.  There are two steps.  First, figure out what piece
	     * of the old window must be redisplayed, then move the window,
	     * then figure out what pieces of the new window must be redisplayed.
	     * If the window coordinates aren't moving, then any screen area
	     * common to the old and new positions needn't be redisplayed,
	     * since its contents won't change.
	     */

	     if (!move)
	     {
		/* Compute the intersection of the old and new areas in 
		 * dontRedisplay.  Mark old areas outside of this common area as
		 * needing to be redisplayed.
		 */
		WindOutToIn(w, &newFrameArea, &dontRedisplay);
		GeoClip(&dontRedisplay, &w->w_screenArea);
		(void) GeoDisjoint(&w->w_frameArea, &dontRedisplay, windReframeFunc,
		    (ClientData) w);
	    }
	    else
	    {
		/* Record the entire old area as needing to be redisplayed. */

		WindAreaChanged(w, &w->w_allArea);
		dontRedisplay = w->w_allArea;
	    }
    }
	
    /* At this point, we've recorded any old area that needs to be
     * redisplayed.
     */

    w->w_frameArea = newFrameArea;
    WindSetWindowAreas(w);
    windSetWindowPosition(w);
    windFixSurfaceArea(w);
    windReClip();

    switch (WindPackageType)
    {
	case WIND_X_WINDOWS:
	    /* Regenerate backing store, if enabled */
	    if ((GrCreateBackingStorePtr != NULL) &&
			(!(w->w_flags & WIND_OBSCURED)))
		(*GrCreateBackingStorePtr)(w);
	    break;
	
	default:
	    /* Now that the window has been moved, record any of the new area that
	     * has to be redisplayed. 
	     */

	    (void) GeoDisjoint(&w->w_allArea, &dontRedisplay, windReframeFunc,
		(ClientData) w);
    }

    /* Give the client a chance to do things like windMove().
     */
    if (cr->w_reposition != NULL)
	(*(cr->w_reposition))(w, &newFrameArea, TRUE);
}

int
windReframeFunc(area, w)
    Rect *area;			/* Area to redisplay. */
    MagWindow *w;			/* Window in which to redisplay. */

{
    WindAreaChanged(w, area);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * WindFullScreen --
 *
 * 	This procedure blows a window up so it's on top of all the others
 *	and is full-screen.  Or, if the window was already full-screen,
 *	it is put back where it came from before it was made full-screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window's size and location are changed.
 *
 * ----------------------------------------------------------------------------
 */

void
WindFullScreen(w)
    MagWindow *w;			/* Window to be blown up or shrunk back. */
{
    int i;
    MagWindow *w2;
    Rect newFrameArea;
    clientRec *cr;
    int newDepth;

    cr = (clientRec *) w->w_client;

    /* Compute default new location. */

    if (w->w_flags & WIND_FULLSCREEN)
	newFrameArea = w->w_oldArea;
    else
	newFrameArea = GrScreenRect;

    /* Give the client a chance to modify newFrameArea. */

    if (cr->w_reposition != NULL)
	(*(cr->w_reposition))(w, &newFrameArea, FALSE);

    /* 
     * Now, actually modify the window and its position. 
     */

    /* Compute new stuff. */

    if (w->w_flags & WIND_FULLSCREEN)
    {
	newDepth = w->w_oldDepth;
	w->w_flags &= ~WIND_FULLSCREEN;
    }
    else
    {
	newDepth = 0;
	w->w_flags |= WIND_FULLSCREEN;

	/* Record old depth and area. */

	w->w_oldArea = w->w_frameArea;
	w->w_oldDepth = 0;
	for (w2 = windTopWindow; w2 != w; w2 = w2->w_nextWindow)
	{
	    ASSERT(w2 != (MagWindow *) NULL, "WindFullScreen");
	    w->w_oldDepth += 1;
	}
    }


    /* Change the view and screen location. */
    w->w_frameArea = newFrameArea;
    WindSetWindowAreas(w);
    windSetWindowPosition(w);
    WindMove(w, &w->w_surfaceArea);

    /* Move the window to the proper depth.  */
    if (windTopWindow != (MagWindow *) NULL)
    {
	if (newDepth == 0)
	{
	    switch ( WindPackageType )
	    {
		case WIND_X_WINDOWS:
		    break;

		default:
		    WindOver(w);
	    }
	}
	else
	{
	    windUnlink(w);
	    w2 = windTopWindow;
	    for (i=1; i<newDepth; i++)
		if (w2->w_nextWindow != NULL) w2 = w2->w_nextWindow;
	    w->w_nextWindow = w2->w_nextWindow;
	    w->w_prevWindow = w2;
	    w2->w_nextWindow = w;
	    if (w->w_nextWindow == NULL) 
		windBottomWindow = w;
	    else 
		w->w_nextWindow->w_prevWindow = w;
	    windReClip();
	}
    }

    /* Notify the client. */
    if (cr->w_reposition != NULL)
	(*(cr->w_reposition))(w, &newFrameArea, TRUE);

    /* Record new display areas. */
    switch (WindPackageType)
    {
	case WIND_X_WINDOWS:
	    if (GrConfigureWindowPtr != NULL)
		(*GrConfigureWindowPtr)(w);
	    if (GrCreateBackingStorePtr != NULL &&
			(!(w->w_flags & WIND_OBSCURED)))
		(*GrCreateBackingStorePtr)(w);
	    break;
	default:
	    WindAreaChanged((MagWindow *) NULL, (Rect *) NULL);
    }
}
