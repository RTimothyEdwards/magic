/*
 * mzInit.c --
 *
 * Initialization code for maze router module.
 * Called after technology file readin.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1988, 1990 Michael H. Arnold and the Regents of the *
 *     * University of California.                                         *
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/mzrouter/mzInit.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "select/select.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "debug/debug.h"
#include "utils/undo.h"
#include "textio/txcommands.h"
#include "utils/malloc.h"
#include "utils/list.h"
#include "utils/heap.h"
#include "mzrouter/mzrouter.h"
#include "mzrouter/mzInternal.h"


/*---- Static Data (local to this file) ----*/

/* Dummy cells used to display global hint planes */
CellDef *mzHHintDef = (CellDef *) NULL;
CellUse *mzHHintUse = (CellUse *) NULL;
CellDef *mzVHintDef = (CellDef *) NULL;
CellUse *mzVHintUse = (CellUse *) NULL;

/* Dummy cells used to display global fence planes */
CellDef *mzHFenceDef = (CellDef *) NULL;
CellUse *mzHFenceUse = (CellUse *) NULL;

/* Dummy cells used to display global rotate planes */
CellDef *mzHRotateDef = (CellDef *) NULL;
CellUse *mzHRotateUse = (CellUse *) NULL;
CellDef *mzVRotateDef = (CellDef *) NULL;
CellUse *mzVRotateUse = (CellUse *) NULL;

/* Dummy cells used to display global bounds planes */
CellDef *mzHBoundsDef = (CellDef *) NULL;
CellUse *mzHBoundsUse = (CellUse *) NULL;
CellDef *mzVBoundsDef = (CellDef *) NULL;
CellUse *mzVBoundsUse = (CellUse *) NULL;

/* Dummy cell used to display blockage plane */
CellDef *mzBlockDef = (CellDef *) NULL;
CellUse *mzBlockUse = (CellUse *) NULL;

/* Dummy cell used to display estimate plane */
CellDef *mzEstimateDef = (CellDef *) NULL;
CellUse *mzEstimateUse = (CellUse *) NULL;

/* Forward declarations */
extern void mzBuildPlanes();


/*
 * ----------------------------------------------------------------------------
 *
 * MZInit --
 *
 * This procedure is called when Magic starts up, after 
 * technology initialization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Register ourselves with debug module
 *	Setup datastructures.
 *	
 * ----------------------------------------------------------------------------
 */

void
MZInit()
{
    int n;

    /* Debug structure */
    static struct
    {
	char	*di_name;
	int	*di_id;
    } dflags[] = {
	"steppath",	&mzDebStep,
	"maze",		&mzDebMaze,
	0
    };

    /* Register with debug module */
    mzDebugID = DebugAddClient("mzrouter", sizeof dflags/sizeof dflags[0]);
    for (n = 0; dflags[n].di_name; n++)
	*(dflags[n].di_id) = DebugAddFlag(mzDebugID, dflags[n].di_name);

    /* Finalize parameters */
    MZAfterTech();

    /* Setup internal tile planes and associated paint tables, cells for
     * display during debugging, etc.
     */
    mzBuildPlanes();

    /* Initialize destination alignment structures */
    mzNLInit(&mzXAlignNL, INITIAL_ALIGN_SIZE);
    mzNLInit(&mzYAlignNL, INITIAL_ALIGN_SIZE);

    /* Setup result cell */
    DBNewYank("__mz_result", &mzResultUse, &mzResultDef);
}

/*
 * ----------------------------------------------------------------------------
 *
 * MZAttachHintPlanes --
 *
 * Reattaches the global pointers to the various hint planes.  Should be
 * called anytime the plane structures are altered (on initialization, and
 * in response to DBScaleEverything())
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Global pointers modified.
 * ----------------------------------------------------------------------------
 */

void
MZAttachHintPlanes()
{
    mzHHintPlane = mzHHintDef->cd_planes[PL_M_HINT];
    mzVHintPlane = mzVHintDef->cd_planes[PL_M_HINT];

    mzHFencePlane = mzHFenceDef->cd_planes[PL_F_HINT];
    mzEstimatePlane = mzEstimateDef->cd_planes[PL_M_HINT];

    mzHRotatePlane = mzHRotateDef->cd_planes[PL_R_HINT];
    mzVRotatePlane = mzVRotateDef->cd_planes[PL_R_HINT];

    mzHBoundsPlane = mzHBoundsDef->cd_planes[PL_M_HINT];
    mzVBoundsPlane = mzVBoundsDef->cd_planes[PL_M_HINT];
}


/*
 * ----------------------------------------------------------------------------
 *
 * mzBuildPlanes --
 *
 * Setup internal type masks, paint tables, planes, and cells for debugging.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Internal cells allocated.
 *	
 * ----------------------------------------------------------------------------
 */

void
mzBuildPlanes()
{
    /* --------- Setup mask of all hint types --------------- */

    /* mzHintTypesMask */
    TTMaskZero(&mzHintTypesMask);
    TTMaskSetType(&mzHintTypesMask,TT_MAGNET);
    TTMaskSetType(&mzHintTypesMask,TT_FENCE);
    TTMaskSetType(&mzHintTypesMask,TT_ROTATE);

    /* --------- Blockage planes ----------------------------------------- */
    /* (Blockage planes indicate where the router is allowed to route on
     *  each route-type.  0-width routes are made and then flushed out
     *  to design rule correct paths after routing completes).  There are
     *  two blockage planes for each routing layer or and routing 
     *  contact - one organized into maximal vertical strips, and one into
     *  horizontal strips).  
     */

    /* Setup paint table for blockage planes */
    {
	int r,s;

        /* Indices are "paint", "have".  The entry value designates "result" */

        /* Blockage painting is governed by strict priority order:
	 * you always get the higher numbered type,
	 * EXCEPT that painting space always gives space. 
	 */
        for (r = 0; r < TT_MAXROUTETYPES; r++)
	{
	    for (s = 0; s < TT_MAXROUTETYPES; s++)
	    {
		if(r == TT_SPACE)
		{
		    mzBlockPaintTbl[r][s] = TT_SPACE;
		}
		else
		{
		    mzBlockPaintTbl[r][s] = MAX(r,s);
		}
	    }
	}
    }

    /* Create dummy cell for displaying blockage planes 
     * (see *mzroute showblock command in mzTest.c)
     */
    DBNewYank("__BLOCK", &mzBlockUse,&mzBlockDef);
    DBFreePaintPlane(mzBlockDef->cd_planes[PL_M_HINT]);
    TiFreePlane(mzBlockDef->cd_planes[PL_M_HINT]);
    mzBlockDef->cd_planes[PL_M_HINT] = NULL;

    /* -- Setup mask of valid tile types on which to start a route path -- */
    /* mzStartTypesMask */
    TTMaskZero(&mzStartTypesMask);
    TTMaskSetType(&mzStartTypesMask,TT_SAMENODE);
    TTMaskSetType(&mzStartTypesMask,TT_ABOVE_UD_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_BELOW_UD_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_ABOVE_LR_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_BELOW_LR_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_LEFT_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_RIGHT_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_TOP_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_BOTTOM_WALK);
    TTMaskSetType(&mzStartTypesMask,TT_DEST_AREA);

    /*------------- Bounds Planes --------------------------------------- */
    /* (Blockage planes are generated incrementally.  The two global bounds
     *  planes record the areas for which blockage planes have already been
     *  generated.) */

    /* Setup paint table for bounds planes */
    {
	int r,s;

        /* Indices are "paint", "have".  The entry value designates "result" */

        /* (Want TT_INBLOCK to persist when TT_GENBLOCK painted on top, 
	 *  so that 
	 * after painting TT_GENBLOCK over region to be expanded TT_GENBLOCK
	 * tiles give subregions that haven't already been expanded.
	 */

	/* Default is to get what you paint */
	for (r = 0; r < TT_MAXROUTETYPES; r++)
	    for (s = 0; s < TT_MAXROUTETYPES; s++)
		mzBoundsPaintTbl[r][s] = r;

	/* Nothing changes TT_INBOUNDS except TT_SPACE */
	for (r = 0; r < TT_MAXROUTETYPES; r++)
	    if (r != TT_SPACE)
	        mzBoundsPaintTbl[r][TT_INBOUNDS] = TT_INBOUNDS;
    }

    /* Create global bounds planes - 
     * and attach to dummy cells for display during debugging */
    
    DBNewYank("__HBOUNDS", &mzHBoundsUse,&mzHBoundsDef);
    DBNewYank("__VBOUNDS", &mzVBoundsUse,&mzVBoundsDef);

    /*------------- Dest Area Internal Cell ------------------------------ */
    /* (Destination area'a are painted here to make sure there is always
     *  something for the router to connect to.  This cell is used along
     *  with mzRouteUse to generate blockage planes.)
     */

    DBNewYank("__DESTAREAS", &mzDestAreasUse,&mzDestAreasDef);

    /* ------------ Estimate Plane ------------------------------------- */
    /* (The global estimation plane provides info. for estimating cost to
     *  completion for partial paths, that factor in large blocks such as
     *  fences and subcells.)
     */

    /* Setup paint table for estimate plane */
    {
	int r,s;

        /* Indices are "paint", "have".  The entry value designates "result" */

        /* Estimate painting is governed by priority order:
	 * you always get the higher numbered type,
	 * EXCEPT that painting space always gives space 
	 */
        for (r = 0; r < TT_MAXROUTETYPES; r++)
	{
	    for (s = 0; s < TT_MAXROUTETYPES; s++)
	    {
		if(r == TT_SPACE)
		{
		    mzEstimatePaintTbl[r][s] = TT_SPACE;
		}
		else
		{
		    mzEstimatePaintTbl[r][s] = MAX(r,s);
		}
	    }
	}
    }

    /* Create global estimate plane 
     * and attach to dummy cells for display during debugging */

    DBNewYank("__ESTIMATE", &mzEstimateUse,&mzEstimateDef);

    /* ----------- Hint Planes ---------------------------------------- */
    /* (Global hint planes store all hints visible to router - unencumbered
     *  by cell structure and fence and rotate regions.)
     */

    /* Create global hint planes -
     * and attach to dummy cells for display  during debugging */

    DBNewYank("__HHINT", &mzHHintUse,&mzHHintDef);
    DBNewYank("__VHINT", &mzVHintUse,&mzVHintDef);

    /* --------------- Fence Plane ---------------------------------- 
     * (Global fence plane gives location of all fences visible to router -
     *  unencumbered by cell structure and hint and rotate regions.)*/

    /* Create global fence plane
     * and attach to dummy cells for display during debugging */

    DBNewYank("__HFENCE", &mzHFenceUse,&mzHFenceDef);

    /* --------------- Rotate Planes ------------------------------- 
     * (Global rotate plane gives location of all rotate regions visible 
     *  to router -
     *  unencumbered by cell structure and hint and rotate regions.) */

    /* Create global rotate planes 
     * and attach to dummy cells for display during debugging */

    DBNewYank("__HROTATE", &mzHRotateUse,&mzHRotateDef);
    DBNewYank("__VROTATE", &mzVRotateUse,&mzVRotateDef);

    MZAttachHintPlanes();

    return;
}
