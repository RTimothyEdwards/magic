/*
 * extcheck.c --
 *
 * Program to check .ext files for consistency without producing
 * any output.  Checks for disconnected global nodes as well as
 * for version consistency.  Counts the number of interesting
 * things in the circuit (devices, capacitors, resistors, nodes).
 *
 * Flattens the tree rooted at file.ext, reading in additional .ext
 * files as specified by "use" lines in file.ext.
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/paths.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/pathvisit.h"
#include "extflat/extflat.h"
#include "utils/runstats.h"
#include "extflat/EFint.h" /* HierContext */

int ecNumDevs;
int ecNumCaps;
int ecNumResists;
int ecNumThreshCaps;
int ecNumThreshResists;
int ecNumNodes;
int ecNumGlobalNodes;
int ecNumNodeCaps;
int ecNumNodeResists;

/* Forward declarations */
int nodeVisit(EFNode *node, int res, double cap, ClientData cdata); /* @typedef cb_extflat_visitnodes_t (UNUSED) */
int devVisit(Dev *dev, HierContext *hc, float scale, Transform *trans, ClientData cdata); /* @typedef cb_extflat_visitdevs_t (UNUSED) */
int capVisit(HierName *hn1, HierName *hn2, double cap, ClientData cdata); /* @typedef cb_extflat_visitcaps_t (UNUSED) */
int resistVisit(const HierName *hn1, const HierName *hn2, float res, ClientData cdata); /* @typedef cb_extflat_visitresists_t (UNUSED) */

/*
 * ----------------------------------------------------------------------------
 *
 * main --
 *
 * Top level of extcheck.
 *
 * ----------------------------------------------------------------------------
 */

int
main(int argc, char *argv[])
{
    char *inName;

    /* Process command line arguments */
    EFInit();
    inName = EFArgs(argc, argv, NULL, (int (*)()) NULL, (ClientData) NULL);
    if (inName == NULL)
	exit (1);

    /* Read the hierarchical description of the input circuit */
    EFReadFile(inName, FALSE, FALSE, FALSE, FALSE);
    if (EFArgTech) EFTech = StrDup((char **) NULL, EFArgTech);
    if (EFScale == 0.0) EFScale = 1.0;

    /* Convert the hierarchical description to a flat one */
    EFFlatBuild(inName, EF_FLATNODES|EF_FLATCAPS|EF_FLATRESISTS);

    EFVisitDevs(devVisit, (ClientData) NULL);
    if (IS_FINITE_F(EFCapThreshold))
	EFVisitCaps(capVisit, (ClientData) NULL);
    if (EFResistThreshold != INFINITE_THRESHOLD)
	EFVisitResists(resistVisit, PTR2CD(NULL));
    EFVisitNodes(nodeVisit, (ClientData) NULL);

#ifdef	free_all_mem
    EFFlatDone(NULL);
    EFDone(NULL);
#endif	/* free_all_mem */

    printf("Memory used: %s\n", RunStats(RS_MEM, NULL, NULL));
    printf("%d devices\n", ecNumDevs);
    printf("%d nodes (%d global, %d local)\n",
	    ecNumNodes, ecNumGlobalNodes, ecNumNodes - ecNumGlobalNodes);
    printf("%d nodes above capacitance threshold\n", ecNumNodeCaps);
    printf("%d nodes above resistance threshold\n", ecNumNodeResists);
    printf("%d internodal capacitors (%d above threshold)\n",
	    ecNumCaps, ecNumThreshCaps);
    printf("%d explicit resistors (%d above threshold)\n",
	    ecNumResists, ecNumThreshResists);
    exit (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * nodeVisit --
 * devVisit --
 * capVisit --
 * resistVisit --
 *
 * Called once for each of the appropriate type of object.
 * Each updates various counts.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

/* @typedef cb_extflat_visitnodes_t (UNUSED) */
int
nodeVisit(
    EFNode *node,
    int res,
    double cap,
    ClientData cdata) /* unused */
{
    ARG_UNUSED(cdata);
    cap = (cap + 500) / 1000;
    res = (res + 500) / 1000;

    ecNumNodes++;
    if (EFHNIsGlob(node->efnode_name->efnn_hier))
	ecNumGlobalNodes++;
    if (res > EFResistThreshold) ecNumNodeResists++;
    if (cap > (double) EFCapThreshold) ecNumNodeCaps++;
    return 0;
}

/*ARGSUSED*/
/* @typedef cb_extflat_visitdevs_t (UNUSED) */
int
devVisit(
    Dev *dev,
    HierContext *hc,
    float scale,
    Transform *trans,
    ClientData cdata) /* UNUSED */
{
    ARG_UNUSED(dev);
    ARG_UNUSED(hc);
    ARG_UNUSED(scale);
    ARG_UNUSED(trans);
    ARG_UNUSED(cdata);
    ecNumDevs++;
    return 0;
}

/*ARGSUSED*/
/* @typedef cb_extflat_visitcaps_t (UNUSED) */
int
capVisit(
    HierName *hn1,
    HierName *hn2,	/* UNUSED */
    double cap,
    ClientData cdata)	/* UNUSED */
{
    ARG_UNUSED(hn1);
    ARG_UNUSED(hn2);
    ARG_UNUSED(cdata);
    ecNumCaps++;
    if ((cap / 1000.) > (double) EFCapThreshold) ecNumThreshCaps++;
    return 0;
}

/*ARGSUSED*/
/* @typedef cb_extflat_visitresists_t (UNUSED) */
int
resistVisit(
    const HierName *hn1,
    const HierName *hn2,/* UNUSED */
    float res,
    ClientData cdata)   /* UNUSED */
{
    ARG_UNUSED(hn1);
    ARG_UNUSED(hn2);
    ARG_UNUSED(cdata);
    ecNumResists++;
    if ((res / 1000.) > EFResistThreshold) ecNumThreshResists++;
    return 0;
}
