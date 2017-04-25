/*
 * DBcount.c --
 *
 * Functions to compute statistics on the paint of
 * a tree of cells.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/database/DBcount.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "database/databaseInt.h"


/*
 * ----------------------------------------------------------------------------
 *
 * DBTreeCountPaint --
 *
 * Allow the client to compute statistics on the paint in a subtree.
 * The client provides three functions: 'count', 'hiercount', and
 * 'cleanup', which should be of the following form:
 *
 *	int
 *	count(def, cdata)
 *	    CellDef *def;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The 'count' function is applied in a pre-order traversal of the
 * cell graph; if it returns 0 then the subcells of 'def' are visited;
 * if it returns 1 then the subcells are not visited.
 *
 *	int	
 *	hiercount(parent, uses, child, cdata)
 *	    CellDef *parent, *child;
 *	    int uses;		/# Scale factor: number of times child
 *				 # is used by parent
 *				 #/
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The 'hiercount' function is applied in a post-order traversal of
 * the cell graph, ie, it is applied only after all children of a
 * cell have been visited.
 *
 *	int
 *	cleanup(def, cdata)
 *	    CellDef *def;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * The 'cleanup' function is applied in a pre-order traversal of the
 * cell graph; if it returns 0 then the subcells of 'def' are visited;
 * if it returns 1 then the subcells are not visited.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Applies the client procedures as described above.
 *	The client is free to use each CellDef's cd_client
 *	field, but should reset this field to zero when the
 *	cleanup procedure is supplied.
 *
 * Algorithm:
 *	We first visit all CellDefs in the tree, applying the
 *	client's 'count' procedure to each CellDef.
 *
 *	Next, we make a second pass over the cells, applying
 *	the client's 'hiercount' procedure to each CellDef
 *	in post-order (ie, the 'hiercount' procedure is first
 *	applied recursively to all the subtrees of a given
 *	def before being applied to the def itself).
 *
 *	Finally, we make a pass over all CellDefs and apply
 *	the client's 'cleanup' procedure.
 *
 * ----------------------------------------------------------------------------
 */

struct countArg
{
    int (*ca_count)();
    int (*ca_hiercount)();
    ClientData ca_cdata;
};

void
DBTreeCountPaint(def, count, hiercount, cleanup, cdata)
    CellDef *def;
    int (*count)();
    int (*hiercount)();
    int (*cleanup)();
    ClientData cdata;
{
    struct countArg ca;
    int dbCountFunc(), dbCountHierFunc();

    ca.ca_cdata = cdata;

    /* Apply the count procedure to each cell */
    ca.ca_count = count;
    if ((*count)(def, cdata) == 0)
	(void) DBCellEnum(def, dbCountFunc, (ClientData) &ca);

    /* Now the hiercount */
    ca.ca_hiercount = hiercount;
    (void) DBCellEnum(def, dbCountHierFunc, (ClientData) &ca);

    /* Now the cleanup */
    ca.ca_count = cleanup;
    if ((*cleanup)(def, cdata) == 0)
	(void) DBCellEnum(def, dbCountFunc, (ClientData) &ca);
}

int
dbCountFunc(use, ca)
    CellUse *use;
    struct countArg *ca;
{
    if ((*ca->ca_count)(use->cu_def, ca->ca_cdata) == 0)
	(void) DBCellEnum(use->cu_def, dbCountFunc, (ClientData) ca);
    return (0);
}

int
dbCountHierFunc(use, ca)
    CellUse *use;
    struct countArg *ca;
{
    int nx, ny;

    (void) DBCellEnum(use->cu_def, dbCountHierFunc, (ClientData) ca);
    if (use->cu_xlo > use->cu_xhi)
	nx = use->cu_xlo - use->cu_xhi + 1;
    else
	nx = use->cu_xhi - use->cu_xlo + 1;

    if (use->cu_ylo > use->cu_yhi)
	ny = use->cu_ylo - use->cu_yhi + 1;
    else
	ny = use->cu_yhi - use->cu_ylo + 1;

    (*ca->ca_hiercount)(use->cu_parent, nx * ny, use->cu_def, ca->ca_cdata);
    return (0);
}
