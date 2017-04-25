/*
 * EFflat.c -
 *
 * Procedures to flatten the hierarchical description built
 * by efReadDef().
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFflat.c,v 1.5 2010/12/16 18:59:03 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"

/* Initial size of the hash table of all flattened node names */
#define	INITFLATSIZE	1024

/* Hash table containing all flattened capacitors */
HashTable efCapHashTable;

/* Hash table containing all flattened distances */
HashTable efDistHashTable;

/* Head of circular list of all flattened nodes */
EFNode efNodeList;

/* Root of the tree being flattened */
Def *efFlatRootDef;
Use efFlatRootUse;
HierContext efFlatContext;

/* Forward declarations */
int efFlatSingleCap();
void efFlatGlob();
int efFlatGlobHash(HierName *);
bool efFlatGlobCmp(HierName *, HierName *);
char *efFlatGlobCopy(HierName *);
void efFlatGlobError(EFNodeName *, EFNodeName *);
int efAddNodes(HierContext *, bool);
int efAddOneConn(HierContext *, char *, char *, Connection *);


/*
 * ----------------------------------------------------------------------------
 *
 * EFFlatBuild --
 *
 * First pass of flattening a circuit.
 * Builds up the flattened tables of nodes, capacitors, etc, depending
 * on the bits contained in flags: EF_FLATNODES causes the node table
 * to be built, EF_FLATCAPS the internodal capacitor table (implies
 * EF_FLATNODES), and EF_FLATDISTS the distance table.
 *
 * Callers who want various pieces of information should call
 * the relevant EFVisit procedures (e.g., EFVisitDevs(), EFVisitCaps(),
 * EFVisitNodes(), etc).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates lots of memory.
 *	Be certain to call EFFlatDone() when this memory is
 *	no longer needed.
 *
 * ----------------------------------------------------------------------------
 */

void
EFFlatBuild(name, flags)
    char *name;		/* Name of root def being flattened */
    int flags;		/* Say what to flatten; see above */
{
    efFlatRootDef = efDefLook(name);
    if (efHNStats) efHNPrintSizes("before building flattened table");

    /* Keyed by a full HierName */
    HashInitClient(&efNodeHashTable, INITFLATSIZE, HT_CLIENTKEYS,
	efHNCompare, (char *(*)()) NULL, efHNHash, (int (*)()) NULL);

    /* Keyed by a pair of HierNames */
    HashInitClient(&efDistHashTable, INITFLATSIZE, HT_CLIENTKEYS,
	efHNDistCompare, efHNDistCopy, efHNDistHash, efHNDistKill);

    /* Keyed by pairs of EFNode pointers (i.e., EFCoupleKeys) */
    HashInit(&efCapHashTable, INITFLATSIZE, HashSize(sizeof (EFCoupleKey)));

    /* Keyed by a string and a HierName */
    HashInitClient(&efHNUseHashTable, INITFLATSIZE, HT_CLIENTKEYS,
	efHNUseCompare, (char *(*)()) NULL, efHNUseHash, (int (*)()) NULL);

    /* Circular list of all nodes contains no elements initially */
    efNodeList.efnode_next = (EFNodeHdr *) &efNodeList;
    efNodeList.efnode_prev = (EFNodeHdr *) &efNodeList;

    efFlatContext.hc_hierName = (HierName *) NULL;
    efFlatContext.hc_use = &efFlatRootUse;
    efFlatContext.hc_trans = GeoIdentityTransform;
    efFlatContext.hc_x = efFlatContext.hc_y = 0;
    efFlatRootUse.use_def = efFlatRootDef;

    if (flags & EF_FLATNODES)
    {
	if (flags & EF_NOFLATSUBCKT)
	    efFlatNodesStdCell(&efFlatContext);
	else
	    efFlatNodes(&efFlatContext);
	efFlatKills(&efFlatContext);
	if (!(flags & EF_NONAMEMERGE))
	    efFlatGlob();
    }

    /* Must happen after kill processing */
    if (flags & EF_FLATCAPS)
	efFlatCaps(&efFlatContext);

    /* Distances are independent of kill processing */
    if (flags & EF_FLATDISTS)
	efFlatDists(&efFlatContext);

    if (efHNStats) efHNPrintSizes("after building flattened table");

 return;
}

/*----------------------------------------------------------------------*/
/* EFFlatBuildOneLevel --						*/
/*									*/
/* EFFlatBuild for a single hierarchical level.  Note, however that	*/
/* where subcircuits have no extracted components, the hierarchy of	*/
/* the subcircuit will be traversed and the subcircuit merged into	*/
/* the root being flattened.						*/
/*									*/
/* This routine used for hierarchical extraction.			*/
/*----------------------------------------------------------------------*/

HierContext *
EFFlatBuildOneLevel(def, flags)
    Def *def;		/* root def being flattened */
    int flags;
{
    int usecount, savecount;
    Use *use;
    int efFlatNodesDeviceless();	/* Forward declaration */
    int efFlatCapsDeviceless();		/* Forward declaration */

    efFlatRootDef = def;

    /* Keyed by a full HierName */
    HashInitClient(&efNodeHashTable, INITFLATSIZE, HT_CLIENTKEYS,
	efHNCompare, (char *(*)()) NULL, efHNHash, (int (*)()) NULL);

    /* Keyed by a pair of HierNames */
    HashInitClient(&efDistHashTable, INITFLATSIZE, HT_CLIENTKEYS,
	efHNDistCompare, efHNDistCopy, efHNDistHash, efHNDistKill);

    /* Keyed by pairs of EFNode pointers (i.e., EFCoupleKeys) */
    HashInit(&efCapHashTable, INITFLATSIZE, HashSize(sizeof (EFCoupleKey)));

    /* Keyed by a string and a HierName */
    HashInitClient(&efHNUseHashTable, INITFLATSIZE, HT_CLIENTKEYS,
	efHNUseCompare, (char *(*)()) NULL, efHNUseHash, (int (*)()) NULL);

    /* Circular list of all nodes contains no elements initially */
    efNodeList.efnode_next = (EFNodeHdr *) &efNodeList;
    efNodeList.efnode_prev = (EFNodeHdr *) &efNodeList;

    efFlatContext.hc_hierName = (HierName *) NULL;
    efFlatContext.hc_use = &efFlatRootUse;
    efFlatContext.hc_trans = GeoIdentityTransform;
    efFlatContext.hc_x = efFlatContext.hc_y = 0;
    efFlatRootUse.use_def = efFlatRootDef;

    usecount = 0;

    /* Record all nodes of the next level in the hierarchy */
    efHierSrUses(&efFlatContext, efAddNodes, (ClientData)TRUE);

    /* Expand all subcells that contain connectivity information but	*/
    /* no active devices (including those in subcells).			*/

    for (use = efFlatRootUse.use_def->def_uses; use; use = use->use_next)
	usecount++;

    /* Recursively flatten uses that have no active devices */
    if (usecount > 0)
	efHierSrUses(&efFlatContext, efFlatNodesDeviceless, (ClientData)&usecount);

    if ((usecount == 0) && (efFlatRootUse.use_def->def_devs == NULL))
	efFlatRootUse.use_def->def_flags |= DEF_NODEVICES;

    /* Record all local nodes */
    efAddNodes(&efFlatContext, FALSE);
    efAddConns(&efFlatContext);

    efFlatKills(&efFlatContext);
    if (!(flags & EF_NONAMEMERGE))
	efFlatGlob();
    if (flags & EF_FLATCAPS)
	efFlatCapsDeviceless(&efFlatContext);
    if (flags & EF_FLATDISTS)
	efFlatDists(&efFlatContext);

    return &efFlatContext;
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFFlatDone --
 *
 * Cleanup by removing all memory used by the flattened circuit
 * representation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees lots of memory.
 *
 * ----------------------------------------------------------------------------
 */

void
EFFlatDone()
{
#ifdef	MALLOCTRACE
    /* Hash table statistics */
    TxPrintf("\n\nStatistics for node hash table:\n");
    HashStats(&efNodeHashTable);
#endif	/* MALLOCTRACE */

    /* Free temporary storage */
    efFreeNodeTable(&efNodeHashTable);
    efFreeNodeList(&efNodeList);
    HashFreeKill(&efCapHashTable);
    HashKill(&efNodeHashTable);
    HashKill(&efHNUseHashTable);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * efFlatNodes --
 *
 * Recursive procedure to flatten the nodes in hc->hc_use->use_def,
 * using a depth-first post-order traversal of the hierarchy.
 *
 * Algorithm:
 *	We first recursivly call efFlatNodes for all of our children uses.
 *	This adds their node names to the global node table.  Next we add
 *	our own nodes to the table.  Some nodes will have to be merged
 *	by connections made in this def, or at least will require adjustments
 *	to their resistance or capacitance.  We walk down the connection
 *	list hc->hc_use->use_def->def_conns to do this merging.  Whenever
 *	two nodes merge, the EFNodeName list for the resulting node is
 *	rearranged to begin with the highest precedence name from the lists
 *	for the two nodes being combined.  See efNodeMerge for a discussion
 *	of precedence.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Adds node names to the table of flattened node names efNodeHashTable.
 *	May merge nodes from the list efNodeList as per the connection
 *	list hc->hc_use->use_def->def_conns.
 *
 * ----------------------------------------------------------------------------
 */

int
efFlatNodes(hc)
    HierContext *hc;
{
    (void) efHierSrUses(hc, efFlatNodes);

    /* Add all our own nodes to the table */
    efAddNodes(hc, FALSE);

    /* Process our own connections and adjustments */
    (void) efAddConns(hc);

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFlatNodesStdCell --
 *
 * Recursive procedure to flatten the nodes in hc->hc_use->use_def,
 * using a depth-first post-order traversal of the hierarchy.  We stop
 * whenever we reach a subcircuit definition, only enumerating its ports.
 *
 * Algorithm:
 *	We first recursivly call efFlatNodes for all of our children uses.
 *	This adds their node names to the global node table.  Next we add
 *	our own nodes to the table.  Some nodes will have to be merged
 *	by connections made in this def, or at least will require adjustments
 *	to their resistance or capacitance.  We walk down the connection
 *	list hc->hc_use->use_def->def_conns to do this merging.  Whenever
 *	two nodes merge, the EFNodeName list for the resulting node is
 *	rearranged to begin with the highest precedence name from the lists
 *	for the two nodes being combined.  See efNodeMerge for a discussion
 *	of precedence.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	Adds node names to the table of flattened node names efNodeHashTable.
 *	May merge nodes from the list efNodeList as per the connection
 *	list hc->hc_use->use_def->def_conns.
 *
 * ----------------------------------------------------------------------------
 */

int
efFlatNodesStdCell(hc)
    HierContext *hc;
{
    if (!(hc->hc_use->use_def->def_flags & DEF_SUBCIRCUIT))
    {
  	/* Recursively flatten each use, except in defined subcircuits */
	(void) efHierSrUses(hc, efFlatNodesStdCell, (ClientData) NULL);
    }

    /* Add all our own nodes to the table */
    efAddNodes(hc, TRUE);

    /* Process our own connections and adjustments */
    if (!(hc->hc_use->use_def->def_flags & DEF_SUBCIRCUIT))
	(void) efAddConns(hc);

    return (0);
}

int
efFlatNodesDeviceless(hc, cdata)
    HierContext *hc;
    ClientData cdata;
{
    int *usecount = (int *)cdata;
    int newcount = 0;
    Use *use;

    for (use = hc->hc_use->use_def->def_uses; use; use = use->use_next)
	newcount++;

    /* Recursively flatten uses that have no active devices */
    if (newcount > 0)
	efHierSrUses(hc, efFlatNodesDeviceless, (ClientData)&newcount);

    if ((hc->hc_use->use_def->def_devs == NULL) && (newcount == 0))
    {
	/* Add all our own nodes to the table */
	efAddNodes(hc, TRUE);

	/* Process our own connections and adjustments */
	efAddConns(hc);

	/* Mark this definition as having no devices, so it will not be visited */
	hc->hc_use->use_def->def_flags |= DEF_NODEVICES;

	(*usecount)--;
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efAddNodes --
 *
 * Add all the nodes defined by the def 'hc->hc_use->use_def' to the
 * global symbol table.  Each global name is prefixed by the hierarchical
 * name component hc->hc_hierName.  If "stdcell" is TRUE, we ONLY add nodes
 * that are defined ports.  Otherwise, we add all nodes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds node names to the table of flattened node names efNodeHashTable.
 *
 * ----------------------------------------------------------------------------
 */

int
efAddNodes(hc, stdcell)
    HierContext *hc;
    bool stdcell;
{
    Def *def = hc->hc_use->use_def;
    EFNodeName *nn, *newname, *oldname;
    EFNode *node, *newnode;
    EFAttr *ap, *newap;
    HierName *hierName;
    float scale;
    int size, asize;
    HashEntry *he;
    bool is_subcircuit = (def->def_flags & DEF_SUBCIRCUIT) ? TRUE : FALSE;

    scale = def->def_scale;
    size = sizeof (EFNode) + (efNumResistClasses-1) * sizeof (PerimArea);

    for (node = (EFNode *) def->def_firstn.efnode_next;
	    node != &def->def_firstn;
	    node = (EFNode *) node->efnode_next)
    {
	/* In subcircuits, only enumerate the ports */
 	if (stdcell && is_subcircuit && !(node->efnode_flags & EF_PORT))
	    continue;

	newnode = (EFNode *) mallocMagic((unsigned)(size));
	newnode->efnode_attrs = (EFAttr *) NULL;
	for (ap = node->efnode_attrs; ap; ap = ap->efa_next)
	{
	    asize = ATTRSIZE(strlen(ap->efa_text));
	    newap = (EFAttr *) mallocMagic((unsigned)(asize));
	    (void) strcpy(newap->efa_text, ap->efa_text);
	    GeoTransRect(&hc->hc_trans, &ap->efa_loc, &newap->efa_loc);
	    newap->efa_loc.r_xbot = (int)((float)(newap->efa_loc.r_xbot) * scale);
	    newap->efa_loc.r_xtop = (int)((float)(newap->efa_loc.r_xtop) * scale);
	    newap->efa_loc.r_ybot = (int)((float)(newap->efa_loc.r_ybot) * scale);
	    newap->efa_loc.r_ytop = (int)((float)(newap->efa_loc.r_ytop) * scale);

	    newap->efa_type = ap->efa_type;
	    newap->efa_next = newnode->efnode_attrs;
	    newnode->efnode_attrs = newap;
	}

	// If called with "hierarchy on", all local node caps and adjustments
	// have been output and should be ignored.
	
	newnode->efnode_cap = (!stdcell) ? node->efnode_cap : (EFCapValue)0.0;
	newnode->efnode_client = (ClientData) NULL;
	newnode->efnode_flags = node->efnode_flags;
	newnode->efnode_type = node->efnode_type;
	if (!stdcell)
	    bcopy((char *) node->efnode_pa, (char *) newnode->efnode_pa,
			efNumResistClasses * sizeof (PerimArea));
	else
	    bzero((char *) newnode->efnode_pa,
			efNumResistClasses * sizeof (PerimArea));
	GeoTransRect(&hc->hc_trans, &node->efnode_loc, &newnode->efnode_loc);

	/* Scale the result by "scale" --- hopefully we end up with an integer	*/
	/* We don't scale the transform because the scale may be non-integer	*/
	/* and the Transform type has integers only.				*/
	newnode->efnode_loc.r_xbot = (int)((float)(newnode->efnode_loc.r_xbot) * scale);
	newnode->efnode_loc.r_xtop = (int)((float)(newnode->efnode_loc.r_xtop) * scale);
	newnode->efnode_loc.r_ybot = (int)((float)(newnode->efnode_loc.r_ybot) * scale);
	newnode->efnode_loc.r_ytop = (int)((float)(newnode->efnode_loc.r_ytop) * scale);

	/* Prepend to global node list */
	newnode->efnode_next = efNodeList.efnode_next;
	newnode->efnode_prev = (EFNodeHdr *) &efNodeList;
	efNodeList.efnode_next->efnhdr_prev = (EFNodeHdr *) newnode;
	efNodeList.efnode_next = (EFNodeHdr *) newnode;

	/* Add each name for this node to the hash table */
	newnode->efnode_name = (EFNodeName *) NULL;

	for (nn = node->efnode_name; nn; nn = nn->efnn_next)
	{
	    /*
	     * Construct the full hierarchical name of this node.
	     * The path down to this point is given by hc->hc_hierName,
	     * to which nn->efnn_hier is "appended".  Exception: nodes
	     * marked with EF_DEVTERM (fet substrate nodes used before
	     * declared, so intended to refer to default global names)
	     * are added as global nodes.
	     */
	    if (node->efnode_flags & EF_DEVTERM) hierName = nn->efnn_hier;
	    else hierName = EFHNConcat(hc->hc_hierName, nn->efnn_hier);
	    he = HashFind(&efNodeHashTable, (char *) hierName);

	    /*
	     * The name should only have been in the hash table already
	     * if the node was marked with EF_DEVTERM as described above.
	     */
	    if (oldname = (EFNodeName *) HashGetValue(he))
	    {
		if (hierName != nn->efnn_hier)
		    EFHNFree(hierName, hc->hc_hierName, HN_CONCAT);
		if (oldname->efnn_node != newnode)
		    efNodeMerge(oldname->efnn_node, newnode);
		newnode = oldname->efnn_node;
		continue;
	    }

	    /*
	     * We only guarantee that the first name for the node remains
	     * first (since the first name is the "canonical" name for the
	     * node).  The order of the remaining names will be reversed.
	     */
	    newname = (EFNodeName *) mallocMagic((unsigned)(sizeof (EFNodeName)));
	    HashSetValue(he, (char *) newname);
	    newname->efnn_node = newnode;
	    newname->efnn_hier = hierName;
	    if (newnode->efnode_name)
	    {
		newname->efnn_next = newnode->efnode_name->efnn_next;
		newnode->efnode_name->efnn_next = newname;
	    }
	    else
	    {
		newname->efnn_next = (EFNodeName *) NULL;
		newnode->efnode_name = newname;
	    }
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efAddConns --
 *
 * Make all the connections for a given def.  This may cause previously
 * distinct nodes in the flat table to merge.
 *
 * Results:
 *      Returns 0
 *
 * Side effects:
 *	May merge nodes from the list efNodeList as per the connection
 *	list hc->hc_use->use_def->def_conns.
 *
 * ----------------------------------------------------------------------------
 */

int
efAddConns(hc)
    HierContext *hc;
{
    Connection *conn;

    if (efWatchNodes)
	TxPrintf("Processing %s (%s)\n",
		EFHNToStr(hc->hc_hierName),
		hc->hc_use->use_def->def_name);

    for (conn = hc->hc_use->use_def->def_conns; conn; conn = conn->conn_next)
    {
	/* Special case for speed when no array info is present */
	if (conn->conn_1.cn_nsubs == 0)
	    efAddOneConn(hc, conn->conn_name1, conn->conn_name2, conn);
	else
	    efHierSrArray(hc, conn, efAddOneConn, (ClientData) NULL);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efAddOneConn --
 *
 * Do the work of adding a single connection.  The names of the nodes
 * to be connected are 'name1' and 'name2' (note that these are regular
 * strings, not HierNames).  The resistance of the merged node is to be
 * adjusted by 'deltaR' and its capacitance by 'deltaC'.  If 'name2' is
 * NULL, we just adjust the R and C of the node 'name1'.
 *
 * Results:
 *      Returns 0
 *
 * Side effects:
 *	May merge nodes from the list efNodeList.
 *
 * ----------------------------------------------------------------------------
 */

int
efAddOneConn(hc, name1, name2, conn)
    HierContext *hc;
    char *name1, *name2;	/* These are strings, not HierNames */
    Connection *conn;
{
    HashEntry *he1, *he2;
    EFNode *node, *newnode;
    int n;

    he1 = EFHNLook(hc->hc_hierName, name1, "connect(1)");
    if (he1 == NULL)
	return 0;

    /* Adjust the resistance and capacitance of its corresponding node */
    node = ((EFNodeName *) HashGetValue(he1))->efnn_node;
    node->efnode_cap += conn->conn_cap;
    for (n = 0; n < efNumResistClasses; n++)
    {
	node->efnode_pa[n].pa_area += conn->conn_pa[n].pa_area;
	node->efnode_pa[n].pa_perim += conn->conn_pa[n].pa_perim;
    }

    /* Merge this node with conn_name2 if one was specified */
    if (name2)
    {
	he2 = EFHNLook(hc->hc_hierName, name2, "connect(2)");
	if (he2 == NULL)
	    return 0;
	newnode = ((EFNodeName *) HashGetValue(he2))->efnn_node;
	if (node != newnode)
	    efNodeMerge(node, newnode);
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFlatGlob --
 *
 * This procedure checks to ensure that all occurrences of the same global
 * name are connected.  It also adds the reduced form of the global name
 * (i.e., the global name with no pathname prefix) to the hash table
 * efNodeHashTable, making this the preferred name of the global node.
 *
 * Algorithm:
 *	Scan through the node table looking for globals.  Add each
 *	global to a global name table keyed by just the first component
 *	of the HierName.  The value of the entry in this table is set
 *	initially to the EFNodeName for the first occurrence of the
 *	global node.  If another occurrence of the global name is
 *	found whose EFNode differs from this one's, it's an error.
 *	However, we still merge all the pieces of a global node
 *	into a single one at the end.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
efFlatGlob()
{
    EFNodeName *nameFlat, *nameGlob;
    EFNode *nodeFlat, *nodeGlob;
    HashEntry *heFlat, *heGlob;
    HierName *hnFlat, *hnGlob;
    HashTable globalTable;
    HashSearch hs;

    HashInitClient(&globalTable, INITFLATSIZE, HT_CLIENTKEYS,
	efFlatGlobCmp, efFlatGlobCopy, efFlatGlobHash, (int (*)()) NULL);

    /*
     * The following loop examines each global name (the last component of
     * each flat HierName that ends in the global symbol '!'), using the
     * hash table globalTable to keep track of how many times each global
     * name has been seen.  Each global name should be seen exactly once.
     * The only exceptions are fet substrate nodes (nodes marked with the
     * flag EF_DEVTERM), which automatically merge with other global nodes
     * with the same name, since they're only implicitly connected anyway.
     */
    for (nodeFlat = (EFNode *) efNodeList.efnode_next;
	    nodeFlat != &efNodeList;
	    nodeFlat = (EFNode *) nodeFlat->efnode_next)
    {
	/*
	 * Ignore nodes whose names aren't global.  NOTE: we rely on
	 * the fact that EFHNBest() prefers global names to all others,
	 * so if the first name in a node's list isn't global, none of
	 * the rest are either.
	 */
	nameFlat = nodeFlat->efnode_name;
	hnFlat = nameFlat->efnn_hier;
	if (!EFHNIsGlob(hnFlat))
	    continue;

	/*
	 * Look for an entry corresponding to the global part of hnFlat
	 * (only the leaf component) in the global name table.  If one
	 * isn't found, an entry gets created.
	 */
	heGlob = HashFind(&globalTable, (char *) hnFlat);
	nameGlob = (EFNodeName *) HashGetValue(heGlob);
	if (nameGlob == NULL)
	{
	    /*
	     * Create a new EFNodeName that points to nodeFlat, but
	     * don't link it in to nodeFlat->efnode_name yet.
	     */
	    nameGlob = (EFNodeName *) mallocMagic((unsigned)(sizeof (EFNodeName)));
	    HashSetValue(heGlob, (ClientData) nameGlob);
	    nameGlob->efnn_node = nodeFlat;
	    nameGlob->efnn_hier = (HierName *) heGlob->h_key.h_ptr;
	}
	else if (nameGlob->efnn_node != nodeFlat)
	{
	    /*
	     * If either node is a fet substrate node (marked with EF_DEVTERM)
	     * it's OK to merge them; otherwise, it's an error, but we still
	     * merge the nodes.  When merging, we blow away nodeGlob and
	     * absorb it into nodeFlat for simplicity in control of the main
	     * loop.  Note that since nameGlob isn't on the efnode_name list
	     * for nodeGlob, we have to update its node backpointer explicitly.
	     */
	    nodeGlob = nameGlob->efnn_node;
	    if ((nodeGlob->efnode_flags & EF_DEVTERM) == 0
		    && (nodeFlat->efnode_flags & EF_DEVTERM) == 0)
	    {
		efFlatGlobError(nameGlob, nameFlat);
	    }
	    efNodeMerge(nodeFlat, nodeGlob);
	    nameGlob->efnn_node = nodeFlat;
	}
    }

    /*
     * Now make another pass through the global name table,
     * prepending the global name (the HierName consisting of
     * the trailing component only that was allocated when the
     * name was added to globalTable above) to its node, and
     * also adding it to the global hash table efNodeHashTable.
     */
    HashStartSearch(&hs);
    while (heGlob = HashNext(&globalTable, &hs))
    {
	/*
	 * Add the name to the flat node name hash table, and
	 * prepend the EFNodeName to the node's list, but only
	 * if the node didn't already exist in efNodeHashTable.
	 * Otherwise, free nameGlob.
	 */
	nameGlob = (EFNodeName *) HashGetValue(heGlob);
	hnGlob = nameGlob->efnn_hier;
	heFlat = HashFind(&efNodeHashTable, (char *) hnGlob);
	if (HashGetValue(heFlat) == NULL)
	{
	    nodeFlat = nameGlob->efnn_node;
	    HashSetValue(heFlat, (ClientData) nameGlob);
	    nameGlob->efnn_next = nodeFlat->efnode_name;
	    nodeFlat->efnode_name = nameGlob;
	}
	else
	{
	    freeMagic((char *) nameGlob);
	    EFHNFree(hnGlob, (HierName *) NULL, HN_GLOBAL);
	}
    }

    HashKill(&globalTable);
    return;
}

void
efFlatGlobError(nameGlob, nameFlat)
    EFNodeName *nameGlob, *nameFlat;
{
    EFNode *nodeGlob = nameGlob->efnn_node, *nodeFlat = nameFlat->efnn_node;
    EFNodeName *nn;
    int count;

    TxPrintf("*** Global name %s not fully connected:\n",
			nameGlob->efnn_hier->hn_name);
    TxPrintf("One portion contains the names:\n");
    for (count = 0, nn = nodeGlob->efnode_name;
	    count < 10 && nn;
	    count++, nn = nn->efnn_next)
    {
	TxPrintf("    %s\n", EFHNToStr(nn->efnn_hier));
    }
    if (nn) TxPrintf("    .... (no more names will be printed)\n");
    TxPrintf("The other portion contains the names:\n");
    for (count = 0, nn = nodeFlat->efnode_name;
	    count < 10 && nn;
	    count++, nn = nn->efnn_next)
    {
	TxPrintf("    %s\n", EFHNToStr(nn->efnn_hier));
    }
    if (nn) TxPrintf("    .... (no more names will be printed)\n");
    TxPrintf("I'm merging the two pieces into a single node, but you\n");
    TxPrintf("should be sure eventually to connect them in the layout.\n\n");
    return;
}

bool
efFlatGlobCmp(hierName1, hierName2)
    HierName *hierName1, *hierName2;
{
    if (hierName1 == hierName2)
	return FALSE;

    return ((bool)(hierName1 == NULL || hierName2 == NULL
		   || hierName1->hn_hash != hierName2->hn_hash
		   || strcmp(hierName1->hn_name, hierName2->hn_name) != 0
		  ));
}

char *
efFlatGlobCopy(hierName)
    HierName *hierName;
{
    HierName *hNew;
    int size;

    size = HIERNAMESIZE(strlen(hierName->hn_name));
    hNew = (HierName *) mallocMagic((unsigned)(size));
    (void) strcpy(hNew->hn_name, hierName->hn_name);
    hNew->hn_parent = (HierName *) NULL;
    hNew->hn_hash = hierName->hn_hash;
    if (efHNStats)
	efHNRecord(size, HN_GLOBAL);

    return (char *) hNew;
}

int
efFlatGlobHash(hierName)
    HierName *hierName;
{
    return hierName->hn_hash;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFlatKills --
 *
 * Recursively mark all killed nodes, using a depth-first post-order
 * traversal of the hierarchy.  The algorithm is the same as for
 * efFlatNodes above.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	May mark node entries in the global name table as killed
 *	by setting EF_KILLED in the efnode_flags field.
 *
 * ----------------------------------------------------------------------------
 */

int
efFlatKills(hc)
    HierContext *hc;
{
    Def *def = hc->hc_use->use_def;
    HashEntry *he;
    EFNodeName *nn;
    Kill *k;

    /* Recursively visit each use */
    (void) efHierSrUses(hc, efFlatKills, (ClientData) NULL);

    /* Process all of our kill information */
    for (k = def->def_kills; k; k = k->kill_next)
    {
	if (he = EFHNConcatLook(hc->hc_hierName, k->kill_name, "kill"))
	{
	    nn = (EFNodeName *) HashGetValue(he);
	    nn->efnn_node->efnode_flags |= EF_KILLED;
	}
    }

    return (0);
}


/*----
 * WIP
 *----
 */

int
efFlatCapsDeviceless(hc)
    HierContext *hc;
{
    Connection *conn;
    int newcount = 0;
    Use *use;

    for (use = hc->hc_use->use_def->def_uses; use; use = use->use_next)
	newcount++;

    /* Recursively flatten uses that have no active devices */
    if (newcount > 0)
	efHierSrUses(hc, efFlatCapsDeviceless, (ClientData)NULL);

    if (!(hc->hc_use->use_def->def_flags & DEF_NODEVICES))
	if (hc->hc_use->use_def->def_flags & DEF_PROCESSED)
	    return 0;

    /* Output our own capacitors */
    for (conn = hc->hc_use->use_def->def_caps; conn; conn = conn->conn_next)
    {
	/* Special case for speed if no arraying info */
	if (conn->conn_1.cn_nsubs == 0)
	    efFlatSingleCap(hc, conn->conn_name1, conn->conn_name2, conn);
	else
	    efHierSrArray(hc, conn, efFlatSingleCap, (ClientData) NULL);
    }
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFlatCaps --
 *
 * Recursive procedure to flatten all capacitors in the circuit.
 * Produces a single, global hash table (efCapHashTable) indexed
 * by pairs of EFNode pointers, where the value of each entry is the
 * capacitance between the two nodes.
 *
 * Algorithm:
 *	Before this procedure is called, efFlatNodes() should have been
 *	called to create a global table of all node names.  We do a recursive
 *	traversal of the design rooted at 'hc->hc_use->use_def', and construct
 *	full hierarchical names from the terminals of each capacitor
 *	encountered.
 *
 *	These full names are used to find via a lookup in efNodeHashTable the
 *	canonical name of the node for which this full name is an alias.  The
 *	canonical name is output as the node to which this terminal connects.
 *
 *	Capacitance where one of the nodes is substrate is treated specially;
 *	instead of adding an entry to the global hash table, we update
 *	the substrate capacitance of the other node appropriately.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
efFlatCaps(hc)
    HierContext *hc;
{
    Connection *conn;

    /* Recursively flatten capacitors */
    (void) efHierSrUses(hc, efFlatCaps, (ClientData) 0);

    /* Output our own capacitors */
    for (conn = hc->hc_use->use_def->def_caps; conn; conn = conn->conn_next)
    {
	/* Special case for speed if no arraying info */
	if (conn->conn_1.cn_nsubs == 0)
	    efFlatSingleCap(hc, conn->conn_name1, conn->conn_name2, conn);
	else
	    efHierSrArray(hc, conn, efFlatSingleCap, (ClientData) NULL);
    }

    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFlatSingleCap --
 *
 * Add a capacitor with value 'conn->conn_cap' between the nodes
 * 'name1' and 'name2' (text names, not hierarchical names).  Don't
 * add the capacitor if either terminal is a killed node.
 *
 * Results:
 *      Returns 0
 *
 * Side effects:
 *	Adds an entry to efCapHashTable indexed by the nodes of 'name1'
 *	and 'name2' respectively.  If the two nodes are the same, though,
 *	nothing happens.  If either node is ground (GND!), the capacitance
 *	is added to the substrate capacitance of the other node instead of
 *	creating a hash table entry.
 *
 * ----------------------------------------------------------------------------
 */

int
efFlatSingleCap(hc, name1, name2, conn)
    HierContext *hc;		/* Contains hierarchical pathname to cell */
    char *name1, *name2;	/* Names of nodes connecting to capacitor */
    Connection *conn;		/* Contains capacitance to add */
{
    EFNode *n1, *n2;
    HashEntry *he;
    EFCoupleKey ck;

    if ((he = EFHNLook(hc->hc_hierName, name1, "cap(1)")) == NULL)
	return 0;
    n1 = ((EFNodeName *) HashGetValue(he))->efnn_node;
    if (n1->efnode_flags & EF_KILLED)
	return 0;

    if ((he = EFHNLook(hc->hc_hierName, name2, "cap(2)")) == NULL)
	return 0;
    n2 = ((EFNodeName *) HashGetValue(he))->efnn_node;
    if (n2->efnode_flags & EF_KILLED)
	return 0;

    /* Do nothing if the nodes aren't different */
    if (n1 == n2)
	return 0;

    if (n1->efnode_flags & EF_SUBS_NODE)
	n2->efnode_cap += conn->conn_cap;	/* node 2 to substrate */
    else if (n2->efnode_flags & EF_SUBS_NODE)
	n1->efnode_cap += conn->conn_cap;	/* node 1 to substrate */
    else
    {
	/* node1 to node2 */
	if (n1 < n2) ck.ck_1 = n1, ck.ck_2 = n2;
	else ck.ck_1 = n2, ck.ck_2 = n1;
	he = HashFind(&efCapHashTable, (char *) &ck);
	CapHashSetValue(he, (double) (conn->conn_cap + CapHashGetValue(he)));
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFlatDists --
 *
 * Recursive procedure to flatten all distance information in the circuit.
 * Produces a single, global hash table (efDistHashTable) indexed
 * by Distance structures, where the value of each entry is the same
 * as the key and gives the min and maximum distances between the two
 * points.
 *
 * Results:
 *	Returns 0 to keep efHierSrUses going.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

int
efFlatDists(hc)
    HierContext *hc;
{
    Distance *dist, *distFlat, distKey;
    HashEntry *he, *heFlat;
    HashSearch hs;

    /* Recursively flatten distances */
    (void) efHierSrUses(hc, efFlatDists, (ClientData) 0);

    /* Process our own distances */
    HashStartSearch(&hs);
    while (he = HashNext(&hc->hc_use->use_def->def_dists, &hs))
    {
	dist = (Distance *) HashGetValue(he);
	efHNBuildDistKey(hc->hc_hierName, dist, &distKey);
	heFlat = HashFind(&efDistHashTable, (char *) &distKey);
	if (distFlat = (Distance *) HashGetValue(heFlat))
	{
	    /*
	     * This code differs from that in efBuildDist(), in that
	     * we replace the min/max information in distFlat from
	     * that in dist, rather than computing a new min/max.
	     * The reason is that the information in dist (in the
	     * parent) is assumed to override that already computed
	     * in the child.
	     */
	    distFlat->dist_min = dist->dist_min;
	    distFlat->dist_max = dist->dist_max;
	    EFHNFree(distKey.dist_1, hc->hc_hierName, HN_CONCAT);
	    EFHNFree(distKey.dist_2, hc->hc_hierName, HN_CONCAT);
	}
	else
	{
	    /*
	     * If there was no entry in the table already with this
	     * key, make the HashEntry point to its key (which is
	     * the newly malloc'd Distance structure).
	     */
	    HashSetValue(heFlat, (ClientData) he->h_key.h_ptr);
	}
    }

    return 0;
}

/*
 * CapHashGetValue()
 *      do a HashGetValue, and if the pointer is null, return (EFCapValue)0.0
 */

EFCapValue CapHashGetValue(he)
HashEntry *he;
{
	EFCapValue *capp = (EFCapValue *)HashGetValue(he);
	if(capp == NULL)
		return (EFCapValue)0;
	else
		return *capp;
}

/*
 * CapHashSetValue()
 *       if the pointer is null, allocate a EFCapValue and point to it.
 *        Then copy in the new value.
 * 
 * need to pass doubles regardless of what CapValue is because of
 * argument promotion in ANSI C
 *
 */
void
CapHashSetValue(he, c)
HashEntry *he; 
double c;
{
	EFCapValue *capp = (EFCapValue *)HashGetValue(he);
	if(capp == NULL) {
		capp = (EFCapValue *) mallocMagic((unsigned)(sizeof(EFCapValue)));
		HashSetValue(he, capp);
	}
	*capp = (EFCapValue) c;
	return;
}
