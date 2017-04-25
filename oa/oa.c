/*--------------------------------------------------------------*/
/* oa.c --							*/
/* OpenAccess database support for magic			*/
/* 								*/
/* Written by R. Timothy Edwards 4/22/04			*/
/* Open Circuit Design, Inc. for				*/
/* MultiGiG, Inc.						*/
/*--------------------------------------------------------------*/

#ifdef MAGIC_WRAPPER
#ifdef OPENACCESS

#include "tcltk/tclmagic.h"
#include "utils/geometry.h"
#include "utils/magic.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "oa/oa.h"

/*
 *-----------------------------------------------------------------------------
 *
 * OAInit --
 *
 * Initialize variables required by the OpenAccess module.
 *
 *-----------------------------------------------------------------------------
 */

void
OAInit()
{
    /* This generates the TCL commands for magicoa */
    Magicoa_Init(magicinterp);
}

/*
 *-----------------------------------------------------------------------------
 *
 * OACellSearch --
 *
 * The OpenAccess equivalent of DBTreeSrCells.  This function is called by
 * DBTreeSrCells when the cell def has the flag CDOPENACCESS set.
 *
 * The procedure should be of the following form:
 *      int
 *      func(scx, cdarg)
 *          SearchContext *scx;
 *          ClientData cdarg;
 *      {
 *      }
 *
 * In the above, the transform scx->scx_trans is from coordinates of
 * the def of scx->scx_use to the "root".  The array indices
 * scx->scx_x and scx->scx_y identify this element if it is a
 * component of an array.  Func normally returns 0.  If func returns
 * 1, then the search is aborted.  If func returns 2, then all
 * remaining elements of the current array are skipped, but the
 * search is not aborted.
 *
 * Each element of an array is returned separately.
 *
 * Results:
 *      0 is returned if the search terminated normally.  1 is
 *      returned if it was aborted.
 *
 * Side effects:
 *      Whatever side effects are brought about by applying the
 *      procedure supplied.
 *
 * Warnings:
 *	The OpenAccess routines should only be used when (*func)()
 *	is read-only;  that is, the function should not attempt to
 *	alter the cell structure.
 *-----------------------------------------------------------------------------
 */

int
OACellSearch(scx, xMask, func, cdarg)
    SearchContext *scx; /* Pointer to search context specifying a cell use to
                         * search, an area in the coordinates of the cell's
                         * def, and a transform back to "root" coordinates.
                         */
    int xMask;          /* All subcells are visited recursively until we
                         * encounter uses whose flags, when anded with
                         * xMask, are not equal to xMask.  Func is called
                         * for these cells.  A zero mask means all cells in
                         * the root use are considered not to be expanded,
                         * and hence are passed to func.
                         */
    int (*func)();      /* Function to apply to each qualifying cell */
    ClientData cdarg;   /* Client data for above function */
{
    TreeContext context;
    TreeFilter filter;
    char *instname;

    filter.tf_xmask = xMask;
    filter.tf_func = func;
    filter.tf_arg = cdarg;

    context.tc_scx = scx;
    context.tc_filter = &filter;

    /* Get the name of the instance, which should have been set by	*/
    /* rexlite when the OpenAccess database was opened under magic.	*/
   
    instname = scx->scx_use->cu_id;

    /* Query the OpenAccess database.  For each cell overlapping the
     * area scx->scx_area, call the function (*func)() with client
     * data cdarg.  Currently, xMask is not used (assumed to be 0).
     */

    /* The rexlite function is tentatively called REXSearchInstances():	*/
    /* REXSearchInstances(name, func, arg) {				*/
    /*    char *name;							*/
    /*    (int)(*func)();						*/
    /*	  ClientData arg;						*/
    /*									*/
    /* This function should return 0 normally, 1 in case of error.	*/
    /* For each cell instance which is in the hierarchy of "instname",	*/
    /* REXSearchInstances() should call "func" with arguments:		*/
    /*									*/
    /* (int)(*func)(char *iname, char *dname, int llx, int lly,		*/
    /*		int urx, int urx, ClientData arg);			*/
    /*									*/
    /* where "iname" is the name of the instance, "dname" is the name	*/
    /* of the master cell, and "llx", "lly", "urx", and "ury" are the	*/
    /* bounding box coordinates.  "arg" is passed on from above.	*/
    /* If func() returns 0, then the database search should continue.	*/
    /* If func() returns 1, then the database search should stop and	*/
    /* REXSearchInstances() should return 1.				*/

    /* return REXSearchInstances(instname, oaTreeCellSrFunc, (ClientData)&context); */
    return 0;	 /* placeholder */
}

/*
 * Callback function called from REXSearchInstances() for each instance
 * encountered in the cell search.
 *
 * For the time being, we pass back a minimal amount of information:  The
 * instance name and the bounding box.  The bounding box will be compared
 * against the search area to check for overlap, and the function buried
 * in the TreeContext structure (passed as a generic client data pointer)
 * will be called.
 */

int
oaTreeCellSrFunc(instname, defname, llx, lly, urx, ury, cdarg)
    char *instname;
    char *defname;
    int llx, lly, urx, ury;
    ClientData cdarg;
{
    TreeContext *context = (TreeContext *)cdarg;
    TreeFilter *filter = context->tc_filter;
    SearchContext newscx, *scx = context->tc_scx;
    int fres;
    Rect r, *area = &scx->scx_area;
    CellUse newuse;

    /* Check for area overlap */
    r.r_xbot = llx;
    r.r_xtop = urx;
    r.r_ybot = lly;
    r.r_ytop = ury;

    /* Transform---for now, assuming that llx, etc., are relative to	*/
    /* the top-level coordinate system.					*/

    /* Check for area overlap */
    if (!GEO_OVERLAP(area, &r))
	return 0;		/* Don't call function but keep the search going */

    newuse.cu_id = instname;
    newuse.cu_bbox = r;
    /* Need to fill in more of the CellUse structure! */

    newscx.scx_use = &newuse;
    newscx.scx_area = scx->scx_area;
    /* Need to fill in more of the SearchContext structure! */

    /* Call the function */
    fres = (*(filter->tf_func))(newscx, (ClientData)filter->tf_arg);
    return fres;	/* keep the search going */
}

#endif	/* OPENACCESS */
#endif  /* MAGIC_WRAPPER */
