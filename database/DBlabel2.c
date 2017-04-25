/*
 * DBlabel2.c --
 *
 * Label searching primitives.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBlabel2.c,v 1.3 2010/09/12 20:32:31 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "database/databaseInt.h"
#include "utils/malloc.h"

/*
 * The following structure is used to pass data between DBNearestLabel
 * and its search function.
 */
struct nldata
{
    int nld_distance;		/* Square of distance to nearest
				 * label seen so far.
				 */
    Point *nld_point;		/* Reference:  find nearest label to this */
    Rect *nld_labelArea;	/* Fill in with area of nearest label */
    char *nld_name;		/* Fill in with name of nearest label */
    bool nld_gotLabel;		/* TRUE means some label was found */
};

/*
 * The following structure is used to pass the arguments of
 * DBSearchLabel down to the filter function.
 */

#define	MAXLABPATHSIZE	256

typedef struct {
    char *labSrPattern;		/* Pattern for matching. */
    int (*labSrFunc)();		/* Function to apply to each label found */
    ClientData labSrArg;	/* Client data of caller */
} labSrStruct;

/* Forward declarations */

extern void DBTreeFindUse();

bool dbParseArray();

/*
 * ----------------------------------------------------------------------------
 *
 * DBSearchLabel --
 *
 * Search for all occurrences of a point label matching the pattern in the
 * region rect in the indicated cell and all of its children.  On each label
 * matching the pattern found in the area, the supplied procedure is invoked.
 *
 * The supplied procedure should be of the form
 *	int
 *	func(scx, label, tpath, cdarg)
 *	    SearchContext *scx;
 *	    Label *label;
 *	    TerminalPath *tpath;
 *	    ClientData cdarg;
 *	{
 *	}
 *
 * In the above, scx is a search context specifying the cell use whose
 * def was found to contain the label, and label is a pointer to the
 * Label structure itself.  The transform specified in scx is from
 * coordinates of the def of the cell containing the label to "root"
 * coordinates.  Func should normally return 0.  If it returns 1 then
 * the search is aborted.
 *
 * Results:
 *	If the search terminates normally, 0 is returned.  1 is
 *	returned if the search was aborted.
 *
 * Side effects:
 *	Applies the supplied procedure to each tile containing a label
 *	matching the pattern.
 *
 * WARNING: because of the way regex(3) works, it is possible to be
 *	    searching for at most one pattern at a time.
 *
 * ----------------------------------------------------------------------------
 */

int
DBSearchLabel(scx, mask, xMask, pattern, func, cdarg)
    SearchContext *scx;		/* Search context: specifies CellUse,
				 * transform to "root" coordinates, and
				 * an area to search.
				 */
    TileTypeBitMask *mask;	/* Only search for labels on these layers */
    int xMask;			/* Expansion state mask for searching.  Cell
				 * uses are only considered to be expanded
				 * when their expand masks have all the bits
				 * of xMask set.
				 */
    char *pattern;		/* Pattern for which to search */
    int (*func)();		/* Function to apply to each match */
    ClientData cdarg;		/* Argument to pass to function */
{
    TerminalPath tpath;
    int dbSrLabelFunc();
    labSrStruct labsr;
    char labSrStr[MAXLABPATHSIZE];	/* String buffer in which the full pathname
				 	* of each label is assembled for handing
				 	* to the filter function.
				 	*/

    labsr.labSrPattern = pattern;
    labsr.labSrFunc = func;
    labsr.labSrArg = cdarg;
    tpath.tp_first = tpath.tp_next = labSrStr;
    tpath.tp_last = &(labSrStr[sizeof(labSrStr) - 2]);

    if (DBTreeSrLabels(scx, mask, xMask, &tpath, TF_LABEL_ATTACH,
		dbSrLabelFunc, (ClientData) &labsr))
	return 1;
    else return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbSrLabelFunc --
 *
 * Filter procedure applied to all labels by DBTreeSrLabels().  For
 * each label that matches the pattern set by DBSearchLabel(), the
 * filter function (*cdarg->lsa_func)() is applied.
 *
 * Results:
 *	Always return 0 to keep the search going.
 *
 * Side effects:
 *	Applies the supplied procedure to each label matching the pattern.
 *
 * ----------------------------------------------------------------------------
 */

int
dbSrLabelFunc(scx, label, tpath, labsr)
    SearchContext *scx;		/* Contains pointer to use in which label
				 * occurred, and transform back to root
				 * coordinates.
				 */
    Label *label;		/* Label itself */
    TerminalPath *tpath;	/* Full pathname of the terminal */
    labSrStruct *labsr;		/* Information passed to this routine */
{
    if (Match(labsr->labSrPattern, label->lab_text))
	if ((*labsr->labSrFunc)(scx, label, tpath, labsr->labSrArg))
	    return 1;
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBSrLabelLoc --
 *
 * This procedure finds the locations of all labels with a given
 * hierarchical name.  For each label found, a client-supplied
 * search function is called.  The search function has the form:
 *
 *	int
 *	func(rect, name, label, cdarg)
 *	    Rect *rect;
 *	    char *name;
 *	    Label *label;
 *	    ClientData cdarg;
 *
 * Rect is the location of the label, in the coordinates of rootUse->cu_def,
 * name is the label's hierarchical name (just the parameter passed to us),
 * label is a pointer to the label, and cdarg is the client data passed in
 * to us by the client.  Note that there can be more than one label with the
 * same name.  Func should normally return 0.  If it returns 1, then the
 * search is aborted.
 *
 * Results:
 *	The return value is 0, unless func returned a non-zero value,
 *	in which case the return value is 1.
 *
 * Side effects:
 *	Whatever the search function does.
 *
 * ----------------------------------------------------------------------------
 */

int
DBSrLabelLoc(rootUse, name, func, cdarg)
    CellUse *rootUse;	/* Cell in which to search. */
    char *name;		/* A hierarchical label name consisting of zero or more
			 * use-ids followed by a label name (fields separated
			 * with slashes).
			 */
    int (*func)();	/* Applied to each instance of the label name */
    ClientData cdarg;	/* Data to pass through to (*func)() */
{
    CellDef *def;
    SearchContext scx;
    char *cp;
    Label *lab;
    char csave;
    Rect r;

    if (cp = strrchr(name, '/'))
    {
	csave = *cp;
	*cp = '\0';
	DBTreeFindUse(name, rootUse, &scx);
	*cp = csave;
	if (scx.scx_use == NULL)
	    return 0;
	cp++;
    }
    else
    {
	scx.scx_use = rootUse;
	scx.scx_trans = GeoIdentityTransform;
	cp = name;
    }

    def = scx.scx_use->cu_def;
    for (lab = def->cd_labels; lab; lab = lab->lab_next)
	if (lab->lab_text[0] == *cp && strcmp(lab->lab_text, cp) == 0)
	{
	    GeoTransRect(&scx.scx_trans, &lab->lab_rect, &r);
	    if ((*func)(&r, name, lab, cdarg))
		return 1;
	}

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBTreeFindUse --
 *
 * This procedure finds the cell use with the given hierarchical name.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets scx->scx_use to the cell use found, with scx->scx_trans
 *	and scx->scx_x, scx->scx_y also valid.  If the cell was not
 *	found, leaves scx->scx_use set to NULL.
 *
 * ----------------------------------------------------------------------------
 */

void
DBTreeFindUse(name, use, scx)
    char *name;
    CellUse *use;
    SearchContext *scx;
{
    char *cp;
    HashEntry *he;
    CellDef *def;
    char csave;

    def = use->cu_def;
    scx->scx_use = (CellUse *) NULL;
    scx->scx_trans = GeoIdentityTransform;
    scx->scx_x = scx->scx_y = 0;
    while (*name)
    {
	/*
	 * Make sure that the cell whose children are being searched
	 * is read in from disk.
	 */
	if ((def->cd_flags & CDAVAILABLE) == 0)
	    (void) DBCellRead(def, (char *) NULL, TRUE, NULL);

	/*
	 * Pull off the next component of path up to but not including
	 * any array subscripts.
	 */
	for (cp = name; *cp && *cp != '[' && *cp != '/'; cp++)
	    /* Nothing */;
	csave = *cp;
	*cp = '\0';
	he = HashLookOnly(&def->cd_idHash, name);
	*cp = csave;
	if (he == NULL || HashGetValue(he) == NULL)
	    return;
	use = (CellUse *) HashGetValue(he);
	def = use->cu_def;

	/*
	 * Pull off array subscripts and build next stage in transform.
	 * Return NULL if the number of subscripts specified doesn't
	 * match the number that are implied by the array, if use is
	 * an array.
	 */
	if (!dbParseArray(cp, use, scx))
	{
	    /* Allow non-indexed match of array */
	    if (strcmp(name, use->cu_id)) return;
	    /* Check for both 1- and 2-dimensional arrays */
	    if (!dbParseArray("[0][0]", use, scx))
		if (!dbParseArray("[0]", use, scx))
		    return;
	    break;
	}
	while (*cp && *cp++ != '/')
	    /* Nothing */;
	name = cp;
    }

    /* Ensure that the leaf cell is read in */
    def = use->cu_def;
    if ((def->cd_flags & CDAVAILABLE) == 0)
	(void) DBCellRead(def, (char *) NULL, TRUE, NULL);

    scx->scx_use = use;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbParseArray --
 *
 * Pull off the array subscripts starting at 'cp' (there may be none),
 * checking to ensure that there are the correct number for 'use' and that
 * they are in range.  Store these in scx->scx_x and scx->scx_y, and use them
 * to update scx->scx_trans to be use->cu_trans (as adjusted for the indicated
 * array element) followed by the old value of scx->scx_trans.
 *
 * Results:
 *	Returns TRUE on success, FALSE on error.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

bool
dbParseArray(cp, use, scx)
    char *cp;
    CellUse *use;
    SearchContext *scx;
{
    int xdelta, ydelta, i1, i2, indexCount;
    Transform trans, trans2;

    /*
     * The transform stuff is a little tricky because if there
     * was only one index given we don't know whether it's the
     * x- or y-index.  Make sure the number of indices specified
     * matches the number of dimensions in an array, and that
     * the indices are in range.
     */
    indexCount = 0;
    if (*cp == '[')
    {
	if (sscanf(cp, "[%d][%d]", &i1, &i2) == 2)
	{
	    indexCount = 2;
	    while (*cp++ != ']') /* Nothing */;
	    while (*cp++ != ']') /* Nothing */;
	}
	else if (sscanf(cp, "[%d,%d]", &i1, &i2) == 2)
	{
	    indexCount = 2;
	    while (*cp++ != ']') /* Nothing */;
	}
	else if (sscanf(cp, "[%d]", &i1) == 1)
	{
	    indexCount = 1;
	    while (*cp++ != ']') /* Nothing */;
	}

	if (indexCount && *cp != '\0' && *cp != '/')
	    return FALSE;
    }

    switch (indexCount)
    {
	case 0:
	    if (use->cu_xlo != use->cu_xhi || use->cu_ylo != use->cu_yhi)
		return FALSE;
	    scx->scx_x = use->cu_xlo;
	    scx->scx_y = use->cu_ylo;
	    break;
	case 1:
	    if (use->cu_xlo == use->cu_xhi)
	    {
		scx->scx_x = use->cu_xlo;
		scx->scx_y = i1;
	    }
	    else if (use->cu_ylo == use->cu_yhi)
	    {
		scx->scx_x = i1;
		scx->scx_y = use->cu_ylo;
	    }
	    else return FALSE;
	    break;
	case 2:
	    if (use->cu_xlo == use->cu_xhi || use->cu_ylo == use->cu_yhi)
		return FALSE;
	    scx->scx_y = i1;
	    scx->scx_x = i2;
	    break;
    }

    if (use->cu_xhi > use->cu_xlo)
    {
	if (scx->scx_x < use->cu_xlo || scx->scx_x > use->cu_xhi)
	    return FALSE;
	xdelta = use->cu_xsep * (scx->scx_x - use->cu_xlo);
    }
    else
    {
	if (scx->scx_x > use->cu_xlo || scx->scx_x < use->cu_xhi)
	    return FALSE;
	xdelta = use->cu_xsep * (use->cu_xlo - scx->scx_x);
    }
    if (use->cu_yhi > use->cu_ylo)
    {
	if (scx->scx_y < use->cu_ylo || scx->scx_y > use->cu_yhi)
	    return FALSE;
	ydelta = use->cu_ysep * (scx->scx_y - use->cu_ylo);
    }
    else
    {
	if (scx->scx_y > use->cu_ylo || scx->scx_y < use->cu_yhi)
	    return FALSE;
	ydelta = use->cu_ysep * (use->cu_ylo - scx->scx_y);
    }

    GeoTransTranslate(xdelta, ydelta, &use->cu_transform, &trans);
    GeoTransTrans(&trans, &scx->scx_trans, &trans2);
    scx->scx_trans = trans2;
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * DBNearestLabel --
 *
 * 	This procedure finds the nearest label to a given point
 *	and returns its hierarchical name and location.
 *
 * Results:
 *	Area is searched in cellUse to find the nearest label
 *	to point.  TRUE is returned if any label was found.
 *	If there is no label in the given area, FALSE is
 *	returned.
 *
 * Side effects:
 *	The parameter labelArea is filled in with the location of
 *	the label, if one was found.  LabelName is filled in with
 *	the hierarchical name of the label.
 *
 * ----------------------------------------------------------------------------
 */

bool
DBNearestLabel(cellUse, area, point, xMask, labelArea, labelName, length)
    CellUse *cellUse;		/* Start search at this cell. */
    Rect *area;			/* Search this area of cellUse. */
    Point *point;		/* Find nearest label to this point. */
    int xMask;			/* Recursively search subcells as long
				 * as their expand masks, when anded with
				 * xMask, are equal to xMask.  0 means search
				 * all the way down through the hierarchy.
				 */
    Rect *labelArea;		/* To be filled in with area of closest
				 * label.  NULL means ignore.
				 */
    char *labelName;		/* Fill this in with name of label, unless
				 * NULL.
				 */
    int length;			/* This gives the maximum number of chars
				 * that may be used in labelName, including
				 * the NULL character to terminate.
				 */
{
    TerminalPath tPath, *tp;
    SearchContext scx;
    char *name;
    struct nldata funcData;
    extern int dbNearestLabelFunc();

    /* Allocate space to generate a label name, and set up information
     * for the DBTreeSrLabels call.
     */

    if (labelName == NULL) tp = NULL, name = NULL;
    else
    {
	name = (char *) mallocMagic((unsigned) (length));
	tPath.tp_first = name;
	tPath.tp_next = name;
	tPath.tp_last = name + length - 1;
	tp = &tPath;
    }
    scx.scx_use = cellUse;
    scx.scx_area = *area;
    scx.scx_trans = GeoIdentityTransform;

    funcData.nld_point = point;
    funcData.nld_labelArea = labelArea;
    funcData.nld_name = labelName;
    funcData.nld_gotLabel = FALSE;

    (void) DBTreeSrLabels(&scx, &DBAllTypeBits, xMask, tp, TF_LABEL_ATTACH,
		dbNearestLabelFunc, (ClientData) &funcData);

    if (name) freeMagic(name);
    
    if (!funcData.nld_gotLabel) return FALSE;
    else return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * dbNearestLabelFunc --
 *
 * 	This function is called by DBTreeSrLabels for each label
 *	found as part of DBNearestLabel.
 *
 * Results:
 *	Always returns 0 to continue the search.
 *
 * Side effects:
 *	If this is the closest label seen so far, update the information
 *	passed via funcData.
 *
 * ----------------------------------------------------------------------------
 */

int
dbNearestLabelFunc(scx, label, tpath, funcData)
    SearchContext *scx;		/* Describes state of search. */
    Label *label;		/* Label found. */
    TerminalPath *tpath;	/* Name of label. */
    struct nldata *funcData;	/* Parameters to DBNearestLabel (passed as
				 * ClientData).
				 */
{
    int x, y, distance, left, used;
    Rect area;
    char *src, *dst;

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &area);
    x = (area.r_xtop + area.r_xbot)/2 - funcData->nld_point->p_x;
    y = (area.r_ytop + area.r_ybot)/2 - funcData->nld_point->p_y;
    distance = x*x + y*y;
    if ((funcData->nld_gotLabel) && (distance > funcData->nld_distance))
	return 0;
    funcData->nld_distance = distance;
    funcData->nld_gotLabel = TRUE;

    if (funcData->nld_labelArea != NULL)
	*(funcData->nld_labelArea) = area;
    if (funcData->nld_name != NULL)
    {
	left = tpath->tp_last - tpath->tp_next;
	used = tpath->tp_next - tpath->tp_first;
	(void) strncpy(funcData->nld_name, tpath->tp_first, used);
	dst = funcData->nld_name + used;
	src = label->lab_text;
	for ( ; left > 0; left -= 1)
	{
	    if (*src == 0) break;
	    *dst++ = *src++;
	}
	*dst = 0;
    }
    
    return 0;
}
