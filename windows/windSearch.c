
/* windSearch.c -
 *
 *	Functions to find & enumerate windows.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/windows/windSearch.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/glyphs.h"
#include "windows/windInt.h"


/*
 * ----------------------------------------------------------------------------
 * windSearchPoint --
 *
 *	Find the window that is displayed at a given screen point.
 *
 * Results:
 *	A pointer to the window that contains the point, or NULL if no
 *	window fits the bill.  This routine is dangerous, since in some
 *	window packages more that one window may contain that point.  In
 *	particular, under Sun Windows ALL windows contain the point (0, 0).
 *
 *	If 'inside' is non-NULL, it points to a boolean variable which will be 
 *	set to TRUE if the point is in the interior of the window, and FALSE 
 *	if it is in the border of the window.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

MagWindow *
windSearchPoint(p, inside)
    Point *p;		/* A point in screen coordinates */
    bool *inside;	/* A pointer to a boolean variable that is set to
			 * TRUE if the point is in the interior of the window,
			 * and FALSE if it is in the border.  If this pointer
			 * is NULL then 'inside' is not filled in.
			 */
{
    MagWindow *w;

    for(w = windTopWindow; w != (MagWindow *) NULL; w = w->w_nextWindow)
    {
	if (GEO_ENCLOSE(p, &(w->w_allArea) ))
	{
	    if (inside != (bool *) NULL) 
		*inside = GEO_ENCLOSE(p, &(w->w_screenArea) );
	    return w;
	}
    }

    return (MagWindow *) NULL;
}


/*
 * ----------------------------------------------------------------------------
 *
 * WindSearchWid --
 *
 *	Find a window given its window ID.
 *
 * Results:
 *	A pointer to the window, of NULL if not found..
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

MagWindow *
WindSearchWid(wid)
    int wid;
{
    MagWindow *w;
    for(w = windTopWindow; w != (MagWindow *) NULL; w = w->w_nextWindow) {
	if (w->w_wid == wid) return w;
    }
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * WindSearchData --
 *
 *	Find a window with a matching w_grdata record.
 *
 * Results:
 *	A pointer to the window, of NULL if not found..
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

MagWindow *
WindSearchData(grdata)
    ClientData grdata;
{
    MagWindow *w;
    for(w = windTopWindow; w != (MagWindow *) NULL; w = w->w_nextWindow) {
	if (w->w_grdata == grdata) return w;
    }
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 * WindSearch --
 *
 *	Search for all of the client's windows that contain a particular
 *	surface area, whether exposed or not.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the function 'func' for each window that matches. 'func' should
 *	be of the form
 *
 *	    int func(window, clientData)
 *		MagWindow *window;		
 *		ClientData clientData;
 *	    {
 *	    }
 *
 *	Window is the window that matched the search, and clientData is the
 *	clientData parameter supplied to this procedure.
 *	If the function returns a non-zero value the search is aborted, and
 *	that value is returned.  Otherwise the search continues and 0 is
 *	returned.
 * ----------------------------------------------------------------------------
 */

int
WindSearch(client, surfaceID, surfaceArea, func, clientData)
    WindClient client;		/* Search for the windows that belong to
				 * this client.  NULL means all clients.
				 */
    ClientData surfaceID;	/* The unique ID of the surface that we
				 * are looking for.  If NULL then look for
				 * any surface.
				 */
    Rect *surfaceArea;		/* The area that we are looking for in surface
				 * coordinates.  If NULL then match without
				 * regard to the area in the window.
				 */
    int (*func)();		/* The function to call with each window
				 * that matches.
				 */
    ClientData clientData;	/* The client data to be passed to the caller's
				 * function.
				 */
{
    MagWindow *w;
    int res = 0;

    for (w = windTopWindow; w != (MagWindow *) NULL; w = w->w_nextWindow)
    {
	if ( ((client == (WindClient) NULL) || (w->w_client == client)) &&
	     ((surfaceID == (ClientData) NULL) || 
	      (w->w_surfaceID == surfaceID)) )
	{
	    if (surfaceArea == (Rect *) NULL)
	    {
		res = (*func)(w, clientData);
		if (res != 0) return res;
		continue; /* continue on to next window */
	    }
	    else if (GEO_TOUCH(surfaceArea, &(w->w_surfaceArea) ))
	    {
		res = (*func)(w, clientData);
		if (res != 0) return res;
	    }
	}
    }
    return 0;
}
