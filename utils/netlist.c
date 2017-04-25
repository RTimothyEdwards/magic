/*
 * netlist.c
 *
 * This code is responsible for building up the internal representation
 * of a netlist, finding all locations for each terminal, and sorting
 * the nets of a netlist into order of increasing area on a Heap.
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/netlist.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* lint */

#include <stdio.h>
#include <string.h>
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/heap.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "utils/netlist.h"
#include "utils/signals.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "utils/styles.h"

/* Compute the "cost" of a net from the bounding rect for all its terminals */
#define	NETSIZE(r)  ((int)((r)->r_xtop - (r)->r_xbot + (r)->r_ytop - (r)->r_ybot))

/* Forward declarations */
int nlTermFunc(), nlLabelFunc();

/*
 * ----------------------------------------------------------------------------
 *
 * NLBuild --
 *
 * Build an internal NLNetList structure from the information
 * in the current netlist.  We call the netmenu module to enumerate
 * the names of all the terminals in each net, and then search
 * the layout rooted at 'rootUse' for all occurrences of each
 * terminal label.
 *
 * Results:
 *	Returns the number of nets in the netlist.
 *
 * Side effects:
 *	Fills in the NLNetList pointed to by netList.
 *	The HashTable netList->nnl_names is initialized;
 *	it is indexed by the name of a terminal and each entry
 *	so found points to the NLTerm struct with that name.
 *
 * ----------------------------------------------------------------------------
 */

int
NLBuild(rootUse, netList)
    CellUse *rootUse;	/* Cell searched for terminals */
    NLNetList *netList;	/* Netlist to build */
{
    NLTerm *term;
    NLNet *net;

    netList->nnl_nets = (NLNet *) NULL;
    HashInit(&netList->nnl_names, 128, 0);

    /* Build list of all nets and terminals, but don't assign locations */
    (void) NMEnumNets(nlTermFunc, (ClientData) netList);

    /* Count the number of nets */
    netList->nnl_numNets = 0;
    for (net = netList->nnl_nets; net; net = net->nnet_next)
	netList->nnl_numNets++;
    if (SigInterruptPending) goto done;

    /* Now find all the occurrences of each terminal */
    for (net = netList->nnl_nets; net; net = net->nnet_next)
	for (term = net->nnet_terms; term; term = term->nterm_next)
	    (void) DBSrLabelLoc(rootUse, term->nterm_name, nlLabelFunc,
		    (ClientData) term);

    /*
     * Sanity checking and error reporting.
     * Complain if no occurrences were found for a terminal, or
     * if a net had only a single terminal in it.
     */
    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	char mesg[256];
	int nterms = 0;
	Rect r;

	for (term = net->nnet_terms; term; term = term->nterm_next)
	{
	    /* Complain if terminal couldn't be found */
	    if (term->nterm_locs == NULL)
		TxError("Terminal %s couldn't be found\n", term->nterm_name);
	    nterms++;
	}

	/* Complain if only one terminal in net */
	if (nterms == 1)
	{
	    (void) sprintf(mesg, "Net %s has only one terminal",
			net->nnet_terms->nterm_name);
	    if ( net->nnet_terms->nterm_locs )
	    {
		GEO_EXPAND(&net->nnet_terms->nterm_locs->nloc_rect, 1, &r);
		DBWFeedbackAdd(&r, mesg, rootUse->cu_def, 1,
			STYLE_PALEHIGHLIGHTS);
	    }
	}
    }

done:
    return (netList->nnl_numNets);
}

/*
 * ----------------------------------------------------------------------------
 *
 * nlTermFunc --
 *
 * Called for each terminal in each net.  If the first terminal
 * in the net, allocate a new NLNet and prepend to netList->nnl_nets.
 * In any case, allocate a new NLTerm and prepend to the net on the
 * head of netList->nnl_nets (the new net allocated above if the
 * first terminal in the net).
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
nlTermFunc(name, firstInNet, netList)
    char *name;
    bool firstInNet;
    NLNetList *netList;
{
    NLNet *net;
    NLTerm *term;
    HashEntry *he;

    if (firstInNet)
    {
	net = (NLNet *) mallocMagic((unsigned) (sizeof (NLNet)));
	bzero( (char *) net, sizeof(*net));
	net->nnet_terms = (NLTerm *) NULL;
	net->nnet_next = netList->nnl_nets;
	net->nnet_area = GeoNullRect;
	net->nnet_cdata = (ClientData) NULL;
	netList->nnl_nets = net;
    }
    else net = netList->nnl_nets;

    /* Find hash entry for this terminal */
    he = HashFind(&netList->nnl_names, name);
    if (HashGetValue(he))
	TxError("Warning: terminal %s appears in more than one net\n", name);

    term = (NLTerm *) mallocMagic((unsigned) (sizeof (NLTerm)));
    term->nterm_locs = (NLTermLoc *) NULL;
    term->nterm_net = net;
    term->nterm_name = he->h_key.h_name;
    term->nterm_next = net->nnet_terms;
    term->nterm_flags = 0;
    net->nnet_terms = term;

    HashSetValue(he, (ClientData) term);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * nlLabelFunc --
 *
 * Called for each occurrence of each label named in a netlist.
 * Allocates a new NLTermLoc for this label and prepends it to
 * the list for 'term'.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
nlLabelFunc(area, name, label, term)
    Rect *area;		/* Root coords of label */
    char *name;		/* Same as term->nterm_name (UNUSED) */
    Label *label;	/* Label within scx->scx_use->cu_def */
    NLTerm *term;	/* Prepend new NLTermLoc to this terminal */
{
    NLTermLoc *loc;

    loc = (NLTermLoc *) mallocMagic((unsigned) (sizeof (NLTermLoc)));
    loc->nloc_term = term;
    loc->nloc_rect = *area;
    loc->nloc_label = label;

	/* Uninitialized */
    loc->nloc_dir = -1;
    loc->nloc_pin = (struct pin *) NULL;
    loc->nloc_chan = (struct chan *) NULL;
    loc->nloc_stem = TiPlaneRect.r_ll;

    loc->nloc_region = (struct region *) NULL;
    loc->nloc_czone  = (struct czone *) NULL;
    loc->nloc_stemcost = 0;

    /* Link into term's list */
    loc->nloc_next = term->nterm_locs;
    term->nterm_locs = loc;

    (void) GeoInclude(&loc->nloc_rect, &term->nterm_net->nnet_area);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NLFree --
 *
 * Free the storage in the NLNetList that was allocated
 * by glBuildNetList() above.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * ----------------------------------------------------------------------------
 */

void
NLFree(netList)
    NLNetList *netList;
{
    NLTermLoc *loc;
    NLTerm *term;
    NLNet *net;

    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	for (term = net->nnet_terms; term; term = term->nterm_next)
	{
	    for (loc = term->nterm_locs; loc; loc = loc->nloc_next)
		freeMagic((char *) loc);
	    freeMagic((char *) term);
	}
	freeMagic((char *) net);
    }

    HashKill(&netList->nnl_names);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NLSort --
 *
 * Build a heap of nets, ordered with smallest area first.
 * Nets with only one terminal are not added to the heap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes and builds the heap netHeap, which is
 *	sorted in order of increasing area.
 *
 * ----------------------------------------------------------------------------
 */

void
NLSort(netList, netHeap)
    NLNetList *netList;
    Heap *netHeap;
{
    NLTermLoc *loc;
    NLTerm *term;
    NLNet *net;
    int nterms;
    Rect r;

    HeapInit(netHeap, 128, FALSE, FALSE);
    for (net = netList->nnl_nets; net; net = net->nnet_next)
    {
	/* Skip nets with only one terminal */
	if (net->nnet_terms == NULL || net->nnet_terms->nterm_next == NULL)
	    continue;

	/* Find bounding box around all terminals in this net */
	nterms = 0;
	for (term = net->nnet_terms; term; term = term->nterm_next)
	    if (loc = term->nterm_locs)
	    {
		for ( ; loc; loc = loc->nloc_next)
		{
		    if (nterms++ == 0) r = loc->nloc_rect;
		    else (void) GeoInclude(&loc->nloc_rect, &r);
		}
	    }

	/* Add it to the heap if non-degenerate */
	if (nterms >= 2)
	{
	    HeapAddInt(netHeap, NETSIZE(&r), (char *)net);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * NLNetName --
 *
 * Given a pointer to a net, return the name of some terminal associated
 * with the net.
 *
 * Kludge: it's possible, when debugging the channel router, that the
 * nets it passes aren't pointers to real NLNets at all, but simply
 * small integers.  We're clever enough to try to recognize this by
 * comparing net with _etext; if it's earlier, we assume it's a small
 * integer and not a NLNet pointer.
 *
 * Results:
 *	Pointer to the name of the net.
 *
 * Side effects:
 *	If the net appears to have no terminal names, return a
 *	pointer to a statically allocated string that contains
 *	the hex address of the net structure.
 *
 * ----------------------------------------------------------------------------
 */

char *
NLNetName(net)
    NLNet *net;
{
    static char tempId[100];
#if defined(linux) || defined(CYGWIN)
    extern int etext asm("etext");
#elif defined(__APPLE__)
 int etext;
#else
    extern int etext;
#endif
    NLTerm *term;

    if (net == (NLNet *) NULL)
	return "(NULL)";

    /* Handle case of small integers, for debugging the channel router */
    if (net <= (NLNet *)(&etext))
    {
	(void) sprintf(tempId, "#%"DLONG_PREFIX"d", (dlong) net);
	return tempId;
    }

    term = net->nnet_terms;
    if (term == NULL || term->nterm_name == NULL)
    {
	(void) sprintf(tempId, "[%p]", net);
	return tempId;
    }

    return term->nterm_name;
}

