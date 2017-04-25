/* grouteName.c -
 *
 *	Maintain a translation from net number to net name.
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
static char sccsid[] = "@(#)grouteName.c	4.1 MAGIC (Berkeley) 7/4/85";
#endif  /* not lint */

#include <stdio.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "debug/debug.h"
#include "utils/hash.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "utils/heap.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/utils.h"

char **glNameTable;
int glNameTableSize = 0, glNameTableUsed = 0;

static bool glNameDidInit = FALSE;

/*
 * ----------------------------------------------------------------------------
 *
 * glNetNameInit --
 *
 * Allocate storage for a net id to net name translation table.
 * Free any previously allocated table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates and frees memory.  Sets static variables.
 *
 * ----------------------------------------------------------------------------
 */

void
glNetNameInit(netList, numNets)
    NLNetList *netList;
    int numNets;
{
    NLNet *net;
    int i;

    if (glNameDidInit)	/* Free the old table */
    {
	for (i = 0; i <= glNameTableSize; i++)
	    if (glNameTable[i])
		freeMagic(glNameTable[i]);
	freeMagic((char *) glNameTable);
    }

    if (netList == (NLNetList *) NULL)
    {
	glNameDidInit = FALSE;
	return;
    }

    glNameTable = (char **) mallocMagic((unsigned) (numNets+1) * sizeof (char *));
    glNameTableSize = numNets;
    glNameTableUsed = 0;
    glNameTable[0] = NULL;
    glNameDidInit = TRUE;

    /* Create the table mapping back from net ids to net names */
    for (i = 0, net = netList->nnl_nets; net; net = net->nnet_next, i++)
    {
	ASSERT(i <= numNets, "glNetNameInit");	/* Sanity check */
	glNameTable[(int) net->nnet_cdata] =
		StrDup((char **) NULL, net->nnet_terms->nterm_name);
	if ((int) net->nnet_cdata > glNameTableUsed)
	    glNameTableUsed = (int) net->nnet_cdata;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * GlNetIdName --
 *
 * Given a net id number, return the name of a terminal associated with
 * the net.
 *
 * Results:
 *	Pointer to the name of the net.
 *
 * Side effects:
 *	If glNameDidInit is FALSE, the string we return is
 *	generated in a static area and is overwritten by
 *	the next call to GlNetIdName().
 *
 * ----------------------------------------------------------------------------
 */

char *
GlNetIdName(id)
    int id;
{
    static char tempId[100];

    if (!glNameDidInit || glNameTableUsed < id)
    {
	(void) sprintf(tempId, "Net %d (with no name)", id);
	return (tempId);
    }

    return (glNameTable[id]);
}
