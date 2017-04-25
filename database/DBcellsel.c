/*
 * DBcellsel.c --
 *
 * Cell selection.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcellsel.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/utils.h"

struct selectArg
{
    int			 csa_xmask;	/* Only the contents of uses whose
					 * expand masks match csa_xmask are
					 * visible.
					 */
    CellUse		*csa_lastuse;	/* Last cell selected */
    Point		 csa_lastp;	/* X, Y indices of csa_lastuse */
    bool		 csa_sawlast;	/* TRUE if csa_lastuse has been visited
					 * in this pass.
					 */
    CellUse		*csa_retuse;	/* Cell we will return as selected */
    CellUse		*csa_bestuse;	/* Best candidate seen in this pass */
    Point		*csa_bestp;	/* X, Y indices of csa_bestuse */
    Transform		*csa_besttrans;	/* Points to a Transform that will be
					 * set to the transform from the parent
					 * of csa_bestuse to the root use.
					 */
    TerminalPath	*csa_bestpath;	/* Pathname of best candidate found */
    TerminalPath	 csa_curpath;	/* Current candidate's pathname */
};

/* Forward declarations */
int dbSelectCellSr();

/*
 * ----------------------------------------------------------------------------
 *
 * DBSelectCell --
 *
 * Select the next cell containing a given point.
 *
 * Results:
 *	Returns a pointer to the next CellUse containing the given
 *	point, or NULL if we have visited all CellUses containing it.
 *	The ordering of CellUses visited is smallest in area to largest.
 *	Both expanded cells, and unexpanded cells all of whose parents
 *	are expanded, are returned.
 *
 * Side effects:
 *	Sets *transform to be the transform from coordinates of
 *	the CellUse's definition to those of the root cell use.
 *	Sets *selp to be the x and y array indices of the selected cell.
 *	Returns path in the argument "tpath".
 *
 * ----------------------------------------------------------------------------
 */

CellUse *
DBSelectCell(rootUse, lastUse, lastP, rootRect, xMask, transform, selp, tpath)
    CellUse *rootUse;		/* Root cell use for the search (usually
				 * that of the window containing the point
				 * tool).  The pathname we construct into
				 * tpath will be relative to this use.
				 */
    CellUse *lastUse;		/* Pointer to last CellUse returned by
				 * DBSelectCell().  This is only used when
				 * stepping through multiple overlapping
				 * cells.
				 */
    Point *lastP;		/* X, Y array indices of last cell selected */
    Rect *rootRect;		/* Box around point tool in coordinates of
				 * the parent def of rootUse.
				 */
    int xMask;			/* Expand bit mask for determining whether
				 * a cell is expanded or not.
				 */
    Transform *transform;	/* Transform set by DBSelectCell, from
				 * coordinates of the returned CellUse's
				 * definition to those of the rootUse's
				 * def.
				 */
    Point *selp;		/* X, Y array indices of the selected cell,
				 * also set by DBSelectCell.
				 */
    TerminalPath *tpath;	/* Set to contain the use id of selected cell */
{
    int xlo, xhi, ylo, yhi, xbase, ybase, xsep, ysep;
    char currentId[BUFSIZ];
    SearchContext scontext;
    struct selectArg arg;

    arg.csa_curpath.tp_first = arg.csa_curpath.tp_next = currentId;
    arg.csa_curpath.tp_last = &currentId[sizeof currentId - 2];
    currentId[0] = '\0';

    arg.csa_xmask = xMask;
    arg.csa_lastp = *lastP;
    arg.csa_sawlast = FALSE;
    arg.csa_retuse = (CellUse *) NULL;
    arg.csa_lastuse = (CellUse *) NULL;

    /*
     * Sanity check in case lastUse has somehow been freed
     * prior to our being called.
     */
    if (lastUse && lastUse->cu_def)
	arg.csa_lastuse = lastUse;

    arg.csa_besttrans = transform;
    arg.csa_bestp = selp;
    arg.csa_bestuse = (CellUse *) NULL;
    arg.csa_bestpath = tpath;

    DBArrayOverlap(rootUse, rootRect, &xlo, &xhi, &ylo, &yhi);
    scontext.scx_use = rootUse;
    scontext.scx_area = *rootRect;
    xsep = (rootUse->cu_xlo>rootUse->cu_xhi)?-rootUse->cu_xsep:rootUse->cu_xsep;
    ysep = (rootUse->cu_ylo>rootUse->cu_yhi)?-rootUse->cu_ysep:rootUse->cu_ysep;
    for (scontext.scx_y = ylo; scontext.scx_y <= yhi; scontext.scx_y++)
    {
	for (scontext.scx_x = xlo; scontext.scx_x <= xhi; scontext.scx_x++)
	{
	    xbase = xsep * (scontext.scx_x - rootUse->cu_xlo);
	    ybase = ysep * (scontext.scx_y - rootUse->cu_ylo);
	    GeoTransTranslate(xbase, ybase, &GeoIdentityTransform,
		&scontext.scx_trans);
	    (void) dbSelectCellSr(&scontext, &arg);
	    if (arg.csa_retuse != (CellUse *) NULL)
		break;
	}
    }

    return (arg.csa_bestuse);
}

/*
 * Sets arg->csa_retuse if we've found the next use.  This enables us
 * to short-circuit the remainder of the tree search (it has now been
 * changed to abort the search).
 *
 * Sets arg->csa_bestuse to point to the best candidate yet found
 * for being the next CellUse.  Sets arg->csa_sawlast to TRUE once
 * arg->csa_lastuse has been seen.
 */

int
dbSelectCellSr(scx, arg)
    SearchContext *scx;	/* Context describing the cell use, the
			 * x and y array element under consider-
			 * ation, the area surrounding the
			 * selection point in coordinates of
			 * the def of the cell use, and a
			 * transform back to the "root".
			 */
    struct selectArg *arg;	/* Client data */
{
    /* BY NP */
    dlong childArea, bestArea, lastArea;
    TerminalPath *cpath = &arg->csa_curpath;
    char *savenext;
    Rect *pbx;
    int n;

    /*
     * If we have already found the (use, element) to be returned,
     * prune the search short.
     */
    if (arg->csa_retuse != (CellUse *) NULL)
	return 1;

    /*
     * If this was the (use, element) last returned by DBSelectCell,
     * we can prune the search not to look at any of its subcells.
     * Since the area of a parent must be at least as great as the
     * area of any of its subcells, these subcells must have already
     * been returned by a previous call to DBSelectCell.
     */
    if (scx->scx_use == arg->csa_lastuse
	    && scx->scx_x == arg->csa_lastp.p_x
	    && scx->scx_y == arg->csa_lastp.p_y)
    {
	arg->csa_sawlast = TRUE;
	return 0;
    }

    pbx = &scx->scx_use->cu_def->cd_bbox;
    if (!GEO_OVERLAP((pbx), (&scx->scx_area)))
	return 0;

    /* compute childArea (using long long to avoid overflow). */
    {
        int xDiff, yDiff;

	xDiff = pbx->r_xtop - pbx->r_xbot;
	yDiff = pbx->r_ytop - pbx->r_ybot;
  	/* BY NP */
	childArea = (dlong)xDiff * (dlong)yDiff;
    }

    /* Append the use identifier of this instance to the current path */
    savenext = arg->csa_curpath.tp_next;
    if (cpath->tp_next != cpath->tp_first)
	*cpath->tp_next++ = '/';
    n = cpath->tp_last - cpath->tp_next;
    cpath->tp_next = DBPrintUseId(scx, cpath->tp_next, n, FALSE);

    /*
     * Visit all the children of the def for this use first.
     * If, during the visiting of any of these, we get a non-NULL
     * arg->csa_retuse value, we can return immediately.
     */
    if (DBDescendSubcell(scx->scx_use, arg->csa_xmask))
    {
	(void) DBCellSrArea(scx, dbSelectCellSr, (ClientData) arg);
	if (arg->csa_retuse != (CellUse *) NULL)
	{
	    cpath->tp_next = savenext;
	    *savenext = '\0';
	    return 1;
	}
    }

    if (arg->csa_lastuse != (CellUse *) NULL)
    {
	pbx = &arg->csa_lastuse->cu_def->cd_bbox;

	/* compute lastArea (using long long to avoid overflow). */
	{
	    int xDiff, yDiff;
	  
	    xDiff = pbx->r_xtop - pbx->r_xbot;
	    yDiff = pbx->r_ytop - pbx->r_ybot;
	    /* BY NP */
	    lastArea = (dlong)xDiff * (dlong)yDiff;
	}
    }
    else
	/* BY NP */
	lastArea = 0;

    if (arg->csa_sawlast)
    {
	/*
	 * We have found a cell with the same area as the last cell
	 * selected, later in the search tree than the last one.
	 * We cut the search short and return this immediately.
	 */
   	/* BY NP */
        if (childArea == lastArea)
	{
	    arg->csa_bestp->p_x = scx->scx_x;
	    arg->csa_bestp->p_y = scx->scx_y;
	    arg->csa_retuse = arg->csa_bestuse = scx->scx_use;
	    *arg->csa_besttrans = scx->scx_trans;

	    /* Copy current path into best path */
	    n = arg->csa_bestpath->tp_last - arg->csa_bestpath->tp_next;
	    strncpy(arg->csa_bestpath->tp_next, cpath->tp_first, n);
	    arg->csa_bestpath->tp_next[n] = '\0';
	
	    /* Pop last component of current path */
	    cpath->tp_next = savenext;
	    *savenext = '\0';

	    /* Abort the search */
	    return 1;
	}
    }

    /*
     * We only update our best guess if this cell is larger than
     * the last one selected.  This has the effect of ignoring all
     * cells already returned by DBSelectCell in previous calls.
     *
     * This one might be okay.  It is larger than the previous one, but
     * there may be a smaller one to do first.
     */

    if (childArea > lastArea)
    {
	if (arg->csa_bestuse != (CellUse *) NULL)
	{
	    pbx = &arg->csa_bestuse->cu_def->cd_bbox;
	    /* compute bestArea (using long long to avoid overflow). */
	    {
	        int xDiff, yDiff;
	  
		xDiff = pbx->r_xtop - pbx->r_xbot;
		yDiff = pbx->r_ytop - pbx->r_ybot;
		bestArea = (dlong)xDiff * (dlong)yDiff;
	    }
	    /* BY NP */
	    if (childArea >= bestArea)
	    {
		/* Too big: pop last component of current path */
		cpath->tp_next = savenext;
		*savenext = '\0';

		/* Keep going */
		return 0;
	    }
	}
	arg->csa_bestp->p_x = scx->scx_x;
	arg->csa_bestp->p_y = scx->scx_y;
	arg->csa_bestuse = scx->scx_use;
	*arg->csa_besttrans = scx->scx_trans;

	/* Copy current path into best path */
	n = arg->csa_bestpath->tp_last - arg->csa_bestpath->tp_next;
	strncpy(arg->csa_bestpath->tp_next, cpath->tp_first, n);
	arg->csa_bestpath->tp_next[n] = '\0';
    }

    /* Pop last component of current path */
    cpath->tp_next = savenext;
    *savenext = '\0';

    return 0;
}
