/* DBWhighlights.c -
 *
 *	This file contains routines that allow the highlight plane
 *	to be used to display additional things besides the box.
 *	The routines coordinate all the clients that have highlights
 *	to display so that when one of them updates its highlights
 *	it doesn't trash the others' highlights.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWhlights.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "textio/textio.h"
#include "graphics/graphicsInt.h"

#define WINDOW_DEF(w)	(((CellUse *)(w->w_surfaceID))->cu_def)

/* The array below is used to hold the addresses of redisplay
 * procedures for each of the highlight clients.  Whenever
 * highlights must be redisplayed, each highlight client
 * is invoked for each database window.
 */

#define MAXCLIENTS 10
static int (*(dbwhlClients[MAXCLIENTS]))();

/*
 * ----------------------------------------------------------------------------
 *
 * DBWHLAddClient --
 *
 * 	This procedure is used to add another client to those
 *	that are displaying highlights.  The redisplay procedure
 *	passed in by the client will be invoked in the following
 *	way:
 *		int	
 *		redisplayProc(window, plane)
 *		    MagWindow *window;
 *		    Plane *plane;
 *		{
 *		}
 *	The procedure is invoked once for each window that contains
 *	database information (and potentially has highlights).  The
 *	window has been locked via GrLock() before the proc is called,
 *	and the clipping area has already been set up.  The procedure
 *	is given a pointer to the window, and a pointer to a plane.
 *	The plane contains non-space tiles over all areas where highlight
 *	information needs to be redrawn (all of these areas have had
 *	their highlight information erased).  The client should redraw
 *	any of its highlights that touch any non-space areas.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The client is added to our list of clients.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWHLAddClient(redisplayProc)
    int (*redisplayProc)();	/* Procedure to call during redisplays. */
{
    int i;
    for (i = 0; i < MAXCLIENTS; i++)
    {
	if (dbwhlClients[i] == NULL)
	{
	    dbwhlClients[i] = redisplayProc;
	    return;
	}
    }
    TxError("Magic error:  ran out of space in highlight client table.\n");
    TxError("Tell your system maintainer to enlarge the table.\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWHLRemoveClient --
 *
 * 	This just removes a client from the list of those that we
 *	know about.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given redisplay procedure will no longer be invoked
 *	during redisplays.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWHLRemoveClient(redisplayProc)
    int (*redisplayProc)();		/* A redisplay procedure.  This
					 * procedure must previously have been
					 * passed in to DBWHLAddClient.
					 */
{
    int i;
    for (i = 0; i < MAXCLIENTS; i += 1)
    {
	if (dbwhlClients[i] == redisplayProc)
	{
	    dbwhlClients[i] = NULL;
	    return;
	}
    }
    ASSERT(FALSE, "DBWHLRemoveClient");
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWHLRedraw --
 *
 * 	This procedure is invoked to redisplay highlights.  The
 *	clients that manage highlights are free to draw on the screen
 *	at will.  But if a client ever erases highlight information, it
 *	must call this procedure so that the other clients can redraw
 *	any of their highlights that might have been erased.  This
 *	procedure records what has changed.  The information isn't actually
 *	redrawn until the next time WindUpdate is called.  This is done
 *	to avoid repeated redraws when several pieces of highlights change
 *	in the same area at the same time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is recorded so that specified area will have its
 *	highlight information erased and redrawn the next time that
 *	WindUpdate is called.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWHLRedraw(rootDef, area, erase)
    CellDef *rootDef;		/* Highlight information will be redrawn in
				 * all windows for which this is the root
				 * cell definition.
				 */
    Rect *area;			/* The area over which to redraw.  Highlights
				 * will be redrawn in this area plus enough
				 * surrounding area to catch degenerate boxes
				 * (drawn as crosses) and labels that may
				 * stick out from their attachment points.
				 */
    bool erase;			/* TRUE means we should erase are before
				 * redrawing it.  FALSE means that either the
				 * client has erased the area, or there's no
				 * need to erase it because all that's
				 * happening is to add new information to the
				 * display.
				 */
{
    extern CellDef *dbwhlDef;
    extern bool dbwhlErase;
    extern int dbwhlRedrawFunc();
    Rect ourArea;

    dbwhlDef = rootDef;		/* Must pass to search function. */
    dbwhlErase = erase;

    /* If we're passed a NULL area, expand it by one unit so that
     * we're certain to have non-zero area.  Otherwise the various
     * search procedures have big troubles.
     */
    
    ourArea = *area;
    if (ourArea.r_xbot >= ourArea.r_xtop)
	ourArea.r_xtop = ourArea.r_xbot + 1;
    if (ourArea.r_ybot >= ourArea.r_ytop)
	ourArea.r_ytop = ourArea.r_ybot + 1;
    (void) WindSearch(DBWclientID, (ClientData) NULL, &ourArea,
	dbwhlRedrawFunc, (ClientData) &ourArea);
}

CellDef *dbwhlDef;
bool dbwhlErase;

/* This procedure records the area to be erased (if any) and the
 * area to be redisplayed (which is larger than the area to be
 * erased).
 */

int
dbwhlRedrawFunc(window, area)
    MagWindow *window;		/* Window to redraw. */
    Rect *area;			/* Passed as client data. */
{
    Rect erase, expand, redraw;
    DBWclientRec *crec = (DBWclientRec *) window->w_clientData;

    if (WINDOW_DEF(window) != dbwhlDef) return 0;

    /* The area that we erase must be large enough to include material
     * that sticks out from the area of the highlights.  There are two
     * ways that material sticks out:  (a) zero-size boxes are drawn as
     * crosses, and the crosses stick outside of the box's area;  (b)
     * labels are attached to points or rectangles, but the text usually
     * extends far beyond the attachment point.
     */

    WindSurfaceToScreen(window, area, &erase);
    expand = GrCrossRect;
    (void) GeoInclude(&crec->dbw_expandAmounts, &expand);

    if (dbwhlErase)
    {
	bool needErase = TRUE;

	erase.r_xbot += expand.r_xbot;
	erase.r_ybot += expand.r_ybot;
	erase.r_xtop += expand.r_xtop;
	erase.r_ytop += expand.r_ytop;

	/* On some displays (e.g. black-and-white ones), highlights use
	 * the same color planes as other information.  If this is the
	 * case, redisplay everything (this will redisplay highlights too,
	 * so there's nothing additional to do here).
	 *
	 * This is also the case if we use backing store but the backing
	 * store has been removed due to becoming invalid, such as when
	 * an attempt is made to redraw into an obscured or unmapped
	 * window.
	 */
    
	if (((GrGetBackingStorePtr == NULL) &&
		((GrStyleTable[STYLE_ERASEHIGHLIGHTS].mask &
		GrStyleTable[STYLE_ERASEALLBUTTOOLS].mask) != 0)) ||
		((GrGetBackingStorePtr != NULL) &&
		window->w_backingStore == (ClientData)NULL))
		
	{
	    DBWAreaChanged(dbwhlDef, area, crec->dbw_bitmask,
			(TileTypeBitMask *) NULL);
	    WindAnotherUpdatePlease = TRUE;
	    return 0;
	}

	DBPaintPlane(crec->dbw_hlErase, &erase,
		DBStdPaintTbl(TT_ERROR_P, PL_DRC_ERROR),
		(PaintUndoInfo *) NULL);
    }
    
    /* The area whose highlights must be redrawn is the area erased, but
     * it must be expanded again to include the fact that we may have
     * just erased a piece of a label that stuck out from some other point.
     * This area gets translated back into database coordinates and saved
     * in dbwhlRedrawPlane, but first it gets expanded by one more unit just to
     * eliminate edge effects:  all impacted highlights are now guaranteed
     * to OVERLAP an area in dbwhlRedrawPlane, not just touch.
     */
    
    erase.r_xbot -= expand.r_xtop;
    erase.r_ybot -= expand.r_ytop;
    erase.r_xtop -= expand.r_xbot;
    erase.r_ytop -= expand.r_ybot;
    (void) WindScreenToSurface(window, &erase, &redraw);
    GEO_EXPAND(&redraw, 1, &redraw);
    DBPaintPlane(crec->dbw_hlRedraw, &redraw,
	    DBStdPaintTbl(TT_ERROR_P, PL_DRC_ERROR),
	    (PaintUndoInfo *) NULL);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWHLRedrawPrepWindow --
 *
 * 	This procedure is similar to DBWHLRedraw.  However, it is
 *	intended to indicate areas to redraw highlights for a
 *	single window only.  This is required by the backing store
 *	mechanism when a window is scrolled, and the part of the
 *	window area that remains visible is refreshed from backing
 *	store.  This area does not need to have layout redrawn, but
 *	does need to have all hightlights redrawn, since the
 *	highlights aren't saved in backing store.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is recorded so that specified area will have its
 *	highlight information erased and redrawn the next time that
 *	WindUpdate is called.
 *
 * ----------------------------------------------------------------------------
 */

void
DBWHLRedrawPrepWindow(MagWindow *window, Rect *area)
{
    extern CellDef *dbwhlDef;
    extern bool dbwhlErase;
    extern int dbwhlRedrawFunc();

    dbwhlDef = WINDOW_DEF(window);
    dbwhlErase = FALSE;
    dbwhlRedrawFunc(window, area);
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBWHLRedrawWind --
 *
 * 	This procedure is called to redraw highlight information in a
 *	particular window.  It is normally called as part of WindUpdate
 *	by DBWHLUpdate.  The areas to be erased and redrawn must already
 *	be present in the clientData record.
 *
 * Results:
 *	Always returns 0 to keep searches from aborting.
 *
 * Side effects:
 *	The plane dbw_hlPlane indicates which highlight areas must be
 *	redrawn for this window.  Any highlights that touch any of these
 *	areas are redrawn.  The plane is then cleared.
 *
 * ----------------------------------------------------------------------------
 */

int
DBWHLRedrawWind(window)
    MagWindow *window;		/* Window in which to redraw highlights. */
{
    int i;
    DBWclientRec *crec;
    extern int dbwhlEraseFunc();	/* Forward reference. */
    
    GrLock(window, TRUE);
    crec = (DBWclientRec *) window->w_clientData;

    /* First erase, then redraw: */
    
    (void) DBSrPaintArea((Tile *) NULL, crec->dbw_hlErase, &TiPlaneRect,
	    &DBAllButSpaceBits, dbwhlEraseFunc, (ClientData)window);
    
    /* Now call each client to redraw its own stuff. */

    for (i = 0; i < MAXCLIENTS; i += 1)
    {
	if (dbwhlClients[i] == NULL) continue;
	(void) (*(dbwhlClients[i]))(window, crec->dbw_hlRedraw);
    }

    DBClearPaintPlane(crec->dbw_hlErase);
    DBClearPaintPlane(crec->dbw_hlRedraw);
    GrUnlock(window);
    return 0;
}

/* The procedure below erases highlight information for each tile that
 * it's called with.  Returns 0 to keep the search from aborting.
 */

int
dbwhlEraseFunc(tile, window)
    Tile *tile;			/* Tile describing area to be erased.	*/
    MagWindow *window;		/* Window that is being altered.	*/
{
    Rect area;
    bool needErase = TRUE;

    TiToRect(tile, &area);

    /* If the graphics package allows highlight areas to be	*/
    /* cached in backing store, then we do a quick check to	*/
    /* see if we can just copy the background back in and	*/
    /* avoid repainting.					*/

    if (GrGetBackingStorePtr != NULL)
	if ((*GrGetBackingStorePtr)(window, &area))
	    needErase = FALSE;

    if (needErase) GrClipBox(&area, STYLE_ERASEHIGHLIGHTS);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * DBWHLUpdate --
 *
 * 	This procedure is called once as part of each WindUpdate call.
 *	It checks for any windows that have highlight information that
 *	needs to be redrawn
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Highlights get redrawn on the screen.
 * ----------------------------------------------------------------------------
 */

void
DBWHLUpdate()
{
    extern int dbwhlUpdateWindowFunc();

    /* Scan through all of the layout windows and redraw their
     * highlight information, if necessary.
     */

    (void) WindSearch(DBWclientID, (ClientData) NULL, (Rect *) NULL,
	    DBWHLRedrawWind, (ClientData) NULL);
}
