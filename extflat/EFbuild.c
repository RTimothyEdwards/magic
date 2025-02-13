/*
 * EFbuild.c -
 *
 * Procedures for building up the hierarchical representation
 * of a circuit.  These are all called from efReadDef() in EFread.c.
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
static char rcsid[] __attribute__ ((unused)) = "$Header$";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>		/* for atof() */
#include <string.h>
#include <strings.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "tiles/tile.h"
#include "database/database.h"	/* for TileType definition */
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"	/* for device class list */
#include "extract/extractInt.h"	/* for extGetDevType()	*/

/* C99 compat */
#include "textio/textio.h"

/*
 * To avoid allocating ridiculously large amounts of memory to hold
 * transistor types and the names of node types, we maintain the following
 * string tables.  Each string (transistor type or Magic layername) appears
 * exactly once in its respective table; each Dev structure's dev_type field
 * is an index into EFDevTypes[], and each node layer name an index into
 * EFLayerNames[].
 */

/* The following are ridiculously high */
#define	MAXTYPES	100

/* Table of transistor types */
char *EFDevTypes[TT_MAXTYPES];
int   EFDevNumTypes;

/* Table of Magic layers */
char *EFLayerNames[MAXTYPES] = { "space" };
int   EFLayerNumNames;

/* Forward declarations */
Connection *efAllocConn();
EFNode *efBuildDevNode();
void efNodeAddName();
EFNode *efNodeMerge();

bool efConnBuildName();
bool efConnInitSubs();

extern float locScale;


/*
 * ----------------------------------------------------------------------------
 *
 * efBuildNode --
 *
 * Process a "node" line from a .ext file.
 * Creates a new node with an initial name of 'nodeName'
 * and capacitance to substrate 'nodeCap'.  If there is
 * already a node by the name of 'nodeName', adds 'nodeCap'
 * to its existing capacitance.
 *
 * In addition, the arguments 'av' and 'ac' are an (argv, argc)
 * vector of pairs of perimeters and areas for each of the
 * resist classes; these are either stored in the newly created
 * node, or added to the values already stored in an existing one.
 *
 * Results:
 *	Return a pointer to the new node.
 *
 * Side effects:
 *	Updates the HashTable and node list of 'def'.
 *
 * EFNode tables:
 *	Each hash table of nodes is organized in the following way.
 *	This organization is true both for the node tables for each
 *	Def, and for the global table of flattened nodes maintained
 *	in EFflatten.c (although the flattened nodes use the HierName
 *	struct for representing hierarchical names efficiently).
 *
 *	Each HashEntry points to a EFNodeName struct.  The EFNodeName
 *	is a link back to the hash key (a HierName), as well as
 *	a link to the actual EFNode for that name.  The EFNode points
 *	to the first EFNodeName in the NULL-terminated list of all
 *	EFNodeNames pointing to that EFNode; the intent is that this
 *	first EFNodeName is the "official" or highest-precedence
 *	name for the node.
 *
 *	The nodes themselves are linked into a circular, doubly
 *	linked list, for ease in merging two nodes into a single
 *	one as a result of a "connect" statement.
 *
 *	HashEntries	EFNodeNames	       EFNodes
 *
 *			    +---------------+
 *			    |		    |
 *			    V		    | to    from
 *	+-------+	+-------+	    | prev  prev
 *	|	| ---->	|	|-+ 	    |	^   |
 *	+-------+	+-------+ | 	    |	|   |
 *			    |	  | 	    |	|   V
 *			    V	  | 	+---------------+
 *	+-------+	+-------+ +--->	|		|
 *	|	| ---->	|	| ---->	|		|
 *	+-------+	+-------+ +--->	|		|
 *			    |	  |	+---------------+
 *			    V	  |		^   |
 *	+-------+	+-------+ |		|   |
 *	|	| ---->	|	|-+		|   V
 *	+-------+	+-------+	      from  to
 *			    |		      next  next
 *			    V
 *			   NIL
 *
 * ----------------------------------------------------------------------------
 */

void
efBuildNode(def, isSubsnode, isDevSubsnode, isExtNode, nodeName, nodeCap,
		x, y, layerName, av, ac)
    Def *def;		/* Def to which this connection is to be added */
    bool isSubsnode;	/* TRUE if the node is the global substrate */
    bool isDevSubsnode;	/* TRUE if the node is a device body connection */
    bool isExtNode;	/* TRUE if this was a "node" or "substrate" in .ext */
    char *nodeName;	/* One of the names for this node */
    double nodeCap;	/* Capacitance of this node to ground */
    int x; int y;	/* Location of a point inside this node */
    char *layerName;	/* Name of tile type */
    char **av;		/* Pairs of area, perimeter strings */
    int ac;		/* Number of strings in av */
{
    EFNodeName *newname;
    EFNode *newnode;
    HashEntry *he;
    unsigned size;
    int n;
    LinkedRect *lr;
    Rect rnew;
    int tnew = 0;

    he = HashFind(&def->def_nodes, nodeName);
    newname = (EFNodeName *)HashGetValue(he);

    if (newname && (def->def_kills != NULL))
    {
	HashEntry *hek;
	EFNodeName *nn, *knn, *nodeAlias, *lastAlias;

	/* Watch for nodes that are aliases of the node that was most
	 * recently killed.  This can occur in .res.ext files where an
	 * equivalent node name (alias) is used as one of the node
	 * points.  It must be regenerated as its own node and removed
	 * from the name list of the killed node.
	 */
	hek = HashLookOnly(&def->def_nodes, EFHNToStr(def->def_kills->kill_name));
	if (hek != NULL)
        {
            knn = (EFNodeName *) HashGetValue(hek);
	    if ((knn != NULL) && (knn->efnn_node == newname->efnn_node))
	    {
		/* Remove alias from killed node's name list */
		lastAlias = NULL;
		for (nodeAlias = knn->efnn_node->efnode_name; nodeAlias != NULL;
			nodeAlias = nodeAlias->efnn_next)
		{
		    if (!strcmp(EFHNToStr(nodeAlias->efnn_hier), nodeName))
		    {
			if (lastAlias == NULL)
			    knn->efnn_node->efnode_name = nodeAlias->efnn_next;
			else
			    lastAlias->efnn_next = nodeAlias->efnn_next;
			EFHNFree(nodeAlias->efnn_hier, (HierName *)NULL, HN_ALLOC);
			freeMagic(nodeAlias);
			break;
		    }
		    lastAlias = nodeAlias;
		}
		
		/* Force name to be made into a new node */
		newname = (EFNodeName *)NULL;
	    }
	}
    }

    if (newname)
    {
	if (efWarn)
	    efReadError("Warning: duplicate node name %s\n", nodeName);

	/* newnode should exist if newname does.  Having a NULL node	*/
	/* may be caused by detached labels "connected" to space.	*/

	if ((newnode = newname->efnn_node) != NULL)
	{
	    /* Just add to C, perim, area of existing node */
	    newnode->efnode_cap += (EFCapValue) nodeCap;
	    for (n = 0; n < efNumResistClasses && ac > 1; n++, ac -= 2)
	    {
		newnode->efnode_pa[n].pa_area += atoi(*av++);
		newnode->efnode_pa[n].pa_perim += atoi(*av++);
	    }

	    /* If this node is identified as a device substrate or  */
	    /* the global substrate, ensure that the corresponding  */
	    /* flag is set.					    */

	    if (isDevSubsnode == TRUE)
		newnode->efnode_flags |= EF_SUBS_NODE;

	    if (isSubsnode == TRUE)
	    {
		newnode->efnode_flags |= EF_GLOB_SUBS_NODE;
		EFCompat = FALSE;
	    }

	    /* The node is a duplicate port name at a different location. */
	    /* If EFSaveLocs is TRUE, then save the layer and position in */
	    /* newnode's efnode_disjoint list.				  */

	    if ((EFSaveLocs == TRUE) && (isExtNode == TRUE))
	    {
		rnew.r_xbot = (int)(0.5 + (float)x * locScale);
		rnew.r_ybot = (int)(0.5 + (float)y * locScale);
		rnew.r_xtop = rnew.r_xbot + 1;
		rnew.r_ytop = rnew.r_ybot + 1;

		if (layerName)
		    tnew = efBuildAddStr(EFLayerNames, &EFLayerNumNames,
					MAXTYPES, layerName);
		else
		    tnew = 0;
		lr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
		lr->r_r = rnew;
		lr->r_type = tnew;
		lr->r_next = newnode->efnode_disjoint;
		newnode->efnode_disjoint = lr; 
	    }
	    return;
	}
    }

    if (!newname)
    {
	/* Allocate a new node with 'nodeName' as its single name */
	newname = (EFNodeName *) mallocMagic((unsigned)(sizeof (EFNodeName)));
	newname->efnn_hier = EFStrToHN((HierName *) NULL, nodeName);
	newname->efnn_port = -1;	/* No port assignment */
	newname->efnn_refc = 0;		/* Only reference is self */
	newname->efnn_next = NULL;
	HashSetValue(he, (char *) newname);
    }

    /* New node itself */
    size = sizeof (EFNode) + (efNumResistClasses - 1) * sizeof (EFPerimArea);
    newnode = (EFNode *) mallocMagic((unsigned)(size));
    newnode->efnode_cap = nodeCap;
    newnode->efnode_flags = 0;
    newnode->efnode_attrs = (EFAttr *) NULL;
    newnode->efnode_loc.r_xbot = (int)(0.5 + (float)x * locScale);
    newnode->efnode_loc.r_ybot = (int)(0.5 + (float)y * locScale);
    newnode->efnode_loc.r_xtop = newnode->efnode_loc.r_xbot + 1;
    newnode->efnode_loc.r_ytop = newnode->efnode_loc.r_ybot + 1;
    newnode->efnode_client = (ClientData) NULL;
    newnode->efnode_num = 1;
    if (layerName) newnode->efnode_type =
	    efBuildAddStr(EFLayerNames, &EFLayerNumNames, MAXTYPES, layerName);
    else newnode->efnode_type = 0;

    if (isSubsnode == TRUE) newnode->efnode_flags |= EF_GLOB_SUBS_NODE;
    if (isDevSubsnode == TRUE) newnode->efnode_flags |= EF_SUBS_NODE;

    for (n = 0; n < efNumResistClasses && ac > 1; n++, ac -= 2)
    {
	newnode->efnode_pa[n].pa_area = atoi(*av++);
	newnode->efnode_pa[n].pa_perim = atoi(*av++);
    }
    for ( ; n < efNumResistClasses; n++)
	newnode->efnode_pa[n].pa_area = newnode->efnode_pa[n].pa_perim = 0;

    /* Update back pointers */
    newnode->efnode_name = newname;
    newname->efnn_node = newnode;

    /* Link the node into the list for this def */
    newnode->efnode_next = def->def_firstn.efnode_next;
    newnode->efnode_prev = (EFNodeHdr *) &def->def_firstn;
    def->def_firstn.efnode_next->efnhdr_prev = (EFNodeHdr *) newnode;
    def->def_firstn.efnode_next = (EFNodeHdr *) newnode;

    /* If isSubsnode was TRUE, then turn off backwards compatibility mode */
    if (isSubsnode == TRUE) EFCompat = FALSE;

    /* Save location of top-level geometry if EFSaveLocs is TRUE */
    if ((EFSaveLocs == TRUE) && (isExtNode == TRUE))
    {
	lr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	lr->r_r = newnode->efnode_loc;
	lr->r_type = newnode->efnode_type;
	lr->r_next = (LinkedRect *)NULL;
	newnode->efnode_disjoint = lr;
    }
    else
	newnode->efnode_disjoint = (LinkedRect *)NULL;
}

/*
 * Process a "subcap" line by adding the specified adjustment
 * value to the indicated node's substrate capacitance.
 */

void
efAdjustSubCap(def, nodeName, nodeCapAdjust)
    Def *def;			/* Def to which this connection is to be added */
    char *nodeName;		/* One of the names for this node */
    double nodeCapAdjust;	/* Substrate capacitance adjustment */
{
    EFNodeName *nodename;
    EFNode *node;
    HashEntry *he;

    he = HashLookOnly(&def->def_nodes, nodeName);
    if (he && (nodename = (EFNodeName *) HashGetValue(he)))
    {
	node = nodename->efnn_node;
	node->efnode_cap += (EFCapValue) nodeCapAdjust;
	return;
    }
    else
    {
	if (efWarn)
	    efReadError("Error: subcap has unknown node %s\n", nodeName);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildAttr --
 *
 * Prepend another node attribute to the list for node 'nodeName'.
 * The attribute is located at the coordinates given by 'r' and
 * is on the layer 'layerName'.  The text of the attribute is 'text'.
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
efBuildAttr(def, nodeName, r, layerName, text)
    Def *def;
    char *nodeName;
    Rect *r;
    char *layerName;
    char *text;
{
    HashEntry *he;
    EFNodeName *nn;
    EFAttr *ap;
    int size;

    he = HashLookOnly(&def->def_nodes, nodeName);
    if (he == NULL || HashGetValue(he) == NULL)
    {
	efReadError("Attribute for nonexistent node %s ignored\n", nodeName);
	return;
    }
    nn = (EFNodeName *) HashGetValue(he);

    size = ATTRSIZE(strlen(text));
    ap = (EFAttr *) mallocMagic((unsigned)(size));
    (void) strcpy(ap->efa_text, text);
    ap->efa_type =
	efBuildAddStr(EFLayerNames, &EFLayerNumNames, MAXTYPES, layerName);
    ap->efa_loc = *r;
    ap->efa_next = nn->efnn_node->efnode_attrs;
    nn->efnn_node->efnode_attrs = ap;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildDist --
 *
 * Process a "dist" line from a .ext file.
 * Both of the names driver and receiver are pathnames with slashes.
 * Add a new Distance record to the hash table for Def, or update
 * an existing Distance record.
 *
 * This strategy allows the .ext file to contain several distance
 * lines for the same pair of points; we do the compression here
 * rather than requiring it be done during extraction.  It's necessary
 * to do compression at some point before flattening; see the description
 * in efFlatDists().
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
efBuildDist(def, driver, receiver, min, max)
    Def *def;		/* Def for which we're adding a new Distance */
    char *driver;	/* Source terminal */
    char *receiver;	/* Destination terminal */
    int min, max;	/* Minimum and maximum acyclic distance from source
			 * to destination.
			 */
{
    Distance *dist, distKey;
    HierName *hn1, *hn2;
    HashEntry *he;

    hn1 = EFStrToHN((HierName *) NULL, driver);
    hn2 = EFStrToHN((HierName *) NULL, receiver);
    distKey.dist_min = min;
    distKey.dist_max = max;
    if (EFHNBest(hn1, hn2))
    {
	distKey.dist_1 = hn1;
	distKey.dist_2 = hn2;
    }
    else
    {
	distKey.dist_1 = hn2;
	distKey.dist_2 = hn1;
    }
#ifdef	notdef
    TxError("ADD %s ", EFHNToStr(distKey.dist_1));
    TxError("%s ", EFHNToStr(distKey.dist_2));
    TxError("%d %d\n", min, max);
#endif	/* notdef */

    he = HashFind(&def->def_dists, (char *) &distKey);
    if ((dist = (Distance *) HashGetValue(he)))
    {
	/*
	 * There was already an entry in the table; update it
	 * to reflect new minimum and maximum distances.  We
	 * can free the keys since they were already in the
	 * table.
	 */
	dist->dist_min = MIN(dist->dist_min, min);
	dist->dist_max = MAX(dist->dist_max, max);
	EFHNFree(hn1, (HierName *) NULL, HN_ALLOC);
	EFHNFree(hn2, (HierName *) NULL, HN_ALLOC);
    }
    else
    {
	/*
	 * When the key was installed in the hash table, it was
	 * a copy of the Distance 'distKey'.  Leave this as the
	 * value of the HashEntry.
	 */
	HashSetValue(he, (ClientData) he->h_key.h_ptr);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildKill --
 *
 * Process a "killnode" line from a .ext file.
 * Prepends a Kill to the list for def.
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
efBuildKill(def, name)
    Def *def;		/* Def for which we're adding a new Kill */
    char *name;		/* Name of node to die */
{
    Kill *kill;

    kill = (Kill *) mallocMagic((unsigned)(sizeof (Kill)));
    kill->kill_name = EFStrToHN((HierName *) NULL, name);
    kill->kill_next = def->def_kills;
    def->def_kills = kill;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildEquiv --
 *
 * Process an "equiv" line from a .ext file.
 * One of the names 'nodeName1' or 'nodeName2' should be a name for
 * an existing node in the def 'def'.  We simply prepend this name to
 * the list of names for that node.
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
efBuildEquiv(def, nodeName1, nodeName2, resist, isspice)
    Def *def;		/* Def for which we're adding a new node name */
    char *nodeName1;	/* One of node names to be made equivalent */
    char *nodeName2;	/* Other name to be made equivalent.  One of nodeName1
			 * or nodeName2 must already be known.
			 */
    bool resist;	/* True if "extresist on" option was selected */
    bool isspice;	/* Passed from EFReadFile(), is only TRUE when
			 * running ext2spice.  Indicates that nodes are
			 * case-insensitive.
			 */
{
    EFNodeName *nn1, *nn2;
    HashEntry *he1, *he2;

    /* Look up both names in the hash table for this def */
    he1 = HashFind(&def->def_nodes, nodeName1);
    he2 = HashFind(&def->def_nodes, nodeName2);

    nn1 = (EFNodeName *) HashGetValue(he1);
    nn2 = (EFNodeName *) HashGetValue(he2);

    if (nn1 == nn2) return;	/* These nodes already merged */

    if (nn2 == (EFNodeName *) NULL)
    {
	bool isNew = TRUE;

	/* Create nodeName1 if it doesn't exist */
	if (nn1 == (EFNodeName *) NULL)
	{
	    if (efWarn)
		efReadError("Creating new node %s\n", nodeName1);
	    efBuildNode(def, FALSE, FALSE, FALSE,
		    nodeName1, (double)0, 0, 0,
		    (char *) NULL, (char **) NULL, 0);
	    nn1 = (EFNodeName *) HashGetValue(he1);
	    isNew = FALSE;
	}

	/* Make nodeName2 be another alias for node1 */
	efNodeAddName(nn1->efnn_node, he2,
			EFStrToHN((HierName *) NULL, nodeName2), isNew);
	return;
    }
    else if (nn2->efnn_node == (EFNode *)NULL)
	return;		/* Repeated "equiv" statement */

    /* If both names exist and are for different ports, then keep   */
    /* them separate and add a zero ohm resistor or a zero volt	    */
    /* source between them, based on the method set by "ext2spice   */
    /* shorts".							    */

    if (nn1 && nn2 && (nn1->efnn_port >= 0) && (nn2->efnn_port >= 0) &&
	    (nn1->efnn_port != nn2->efnn_port))
    {
	bool equalByCase = FALSE;
	if (isspice)
	{
	    /* If ports have the same name under the assumption of
	     * case-insensitivity, then just quietly merge them.
	     */
	    if (!strcasecmp(nodeName1, nodeName2)) equalByCase = TRUE;
	}
	if (!equalByCase)
	{
	    if ((EFOutputFlags & EF_SHORT_MASK) != EF_SHORT_NONE)
	    {
		int i;
		int sdev;
		char *argv[10], zeroarg[] = "0";

		if ((EFOutputFlags & EF_SHORT_MASK) == EF_SHORT_R)
		    sdev = DEV_RES;
		else
		    sdev = DEV_VOLT;

		for (i = 0; i < 10; i++) argv[i] = zeroarg;
		argv[0] = StrDup((char **)NULL, "0.0");
		argv[1] = StrDup((char **)NULL, "dummy");
		argv[4] = StrDup((char **)NULL, nodeName1);
		argv[7] = StrDup((char **)NULL, nodeName2);
		efBuildDevice(def, sdev, "None", &GeoNullRect, 10, argv);
		freeMagic(argv[0]);
		freeMagic(argv[1]);
		freeMagic(argv[4]);
		freeMagic(argv[7]);
		return;
	    }
	    else if (!resist)
		TxError("Warning:  Ports \"%s\" and \"%s\" are electrically shorted.\n",
				nodeName1, nodeName2);
	    else
		/* Do not merge the nodes when folding in extresist parasitics */
		return;
	}
    }

    /* If both names exist and are for different nodes, merge them */
    if (nn1)
    {
	EFNode *lostnode;

	if (nn1->efnn_node == (EFNode *)NULL)
	    return;		/* Repeated "equiv" statement */
	if (nn1->efnn_node != nn2->efnn_node)
	{
	    struct efnode *node1 = nn1->efnn_node;
	    struct efnode *node2 = nn2->efnn_node;
    	    HashSearch hs;
	    HashEntry *he;

	    if (efWarn)
		efReadError("Merged nodes %s and %s\n", nodeName1, nodeName2);
	    lostnode = efNodeMerge(&nn1->efnn_node, &nn2->efnn_node);
	    if (nn1->efnn_port > 0) nn2->efnn_port = nn1->efnn_port;
	    else if (nn2->efnn_port > 0) nn1->efnn_port = nn2->efnn_port;

	    /* Check if there are any device terminals pointing to the
	     * node that was just removed.
	     */
	    HashStartSearch(&hs);
	    while ((he = HashNext(&def->def_devs, &hs)))
	    {
	    	Dev *dev;
		int n;

		dev = (Dev *)HashGetValue(he);
		for (n = 0; n < dev->dev_nterm; n++)
		    if (dev->dev_terms[n].dterm_node == lostnode)
			dev->dev_terms[n].dterm_node =
				(nn1->efnn_node == NULL) ?
				nn2->efnn_node : nn1->efnn_node;
	    }

	    /* If a node has been merged away, make sure that its name	*/
	    /* and all aliases point to the merged name's hash.		*/

	    if (nn1->efnn_node == NULL)
	    {
		nn2->efnn_refc += nn1->efnn_refc + 1;
    		HashStartSearch(&hs);
		while ((he1 = HashNext(&def->def_nodes, &hs)))
		    if ((EFNodeName *)HashGetValue(he1) == nn1)
			HashSetValue(he1, (char *)nn2);
	    }
	    else if (nn2->efnn_node == NULL)
	    {
		nn1->efnn_refc += nn2->efnn_refc + 1;
    		HashStartSearch(&hs);
		while ((he2 = HashNext(&def->def_nodes, &hs)))
		    if ((EFNodeName *)HashGetValue(he2) == nn2)
			HashSetValue(he2, (char *)nn1);
	    }
	}
	return;
    }

    /* Make nodeName1 be another alias for node2 */
    efNodeAddName(nn2->efnn_node, he1,
			EFStrToHN((HierName *) NULL, nodeName1), FALSE);
}


/*
 * ----------------------------------------------------------------------------
 *
 *
 * ----------------------------------------------------------------------------
 */

DevParam *
efGetDeviceParams(name)
    char *name;
{
    HashEntry *he;
    DevParam *plist = NULL;

    he = HashLookOnly(&efDevParamTable, (char *)name);
    if (he != NULL)
	plist = (DevParam *)HashGetValue(he);
    return plist;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildDeviceParams --
 *
 * Fill in a device parameter hash table entry from a "parameters" line in
 * the .ext file.
 *
 * ----------------------------------------------------------------------------
 */

void
efBuildDeviceParams(name, argc, argv)
    char *name;
    int argc;
    char *argv[];
{
    HashEntry *he;
    DevParam *plist = NULL, *newparm;
    char *pptr;
    int n;

    he = HashFind(&efDevParamTable, name);
    plist = (DevParam *)HashGetValue(he);
    if (plist != NULL) return;			/* Already got one! */

    /* Parse arguments for each parameter */
    for (n = 0; n < argc; n++)
    {
	char *mult, *offset;

	pptr = strchr(argv[n], '=');
	if (pptr == NULL)
	{
	    efReadError("Bad parameter assignment \"%s\" for device \"%s\"\n",
			argv[n], name);
	    continue;
	}
	newparm = (DevParam *)mallocMagic(sizeof(DevParam));
	newparm->parm_type[0] = *argv[n];
	if ((pptr - argv[n]) == 1)
	    newparm->parm_type[1] = '\0';
	else
	    newparm->parm_type[1] = *(argv[n] + 1);

	if ((mult = strchr(pptr + 1, '*')) != NULL)
	{
	    *mult = '\0';
	    newparm->parm_scale = atof(mult + 1);
	}
	else
	{
	    newparm->parm_scale = 1.0;

	    /* NOTE:  If extending feature to allow for both scale
	     * and offset, be sure to distinguish between +/- as an
	     * offset and +/- as a sign.
	     */
	    if ((offset = strchr(pptr + 1, '+')) != NULL)
	    {
		*offset = '\0';
		newparm->parm_offset = atoi(offset + 1);
	    }
	    else if ((offset = strchr(pptr + 1, '-')) != NULL)
	    {
		*offset = '\0';
		newparm->parm_offset = -atoi(offset + 1);
	    }
	    else
		newparm->parm_offset = 0;
	}

	// For parameters defined for cell defs, copy the whole
	// expression verbatim into parm_name.  parm_type is
	// reassigned to be a numerical order.

	if (name[0] == ':')
	{
	    newparm->parm_name = StrDup((char **)NULL, argv[n]);
	    newparm->parm_type[0] = '0' + n / 10;
	    newparm->parm_type[1] = '0' + n % 10;
	}
	else
	    newparm->parm_name = StrDup((char **)NULL, pptr + 1);
	newparm->parm_next = plist;
	plist = newparm;
    }
    HashSetValue(he, (char *)plist);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildDevice --
 *
 * Process a device line from a .ext file.
 * The number of terminals in the dev is argc/3 (which must be integral).
 * Each block of 3 strings in argv describes a single terminal; see the
 * comments below for their interpretation.
 *
 * Results:
 *	Returns 0 on success, 1 on failure to parse any terminal's values
 *
 * Side effects:
 *	Prepends this dev to the list for the def 'def'.
 *
 * ----------------------------------------------------------------------------
 */

int
efBuildDevice(def, class, type, r, argc, argv)
    Def *def;		/* Def to which this connection is to be added */
    char class;		/* Class (dev, bjt, etc.) of this device */
    char *type;		/* Type (name) of this device */
    Rect *r;		/* Coordinates of 1x1 rectangle entirely inside device */
    int argc;		/* Size of argv */
    char *argv[];	/* Tokens for the rest of the dev line.
			 * Starts with the last two position values, used to
			 * hash the device record.  The next arguments depend
			 * on the type of device.  The rest are taken in groups
			 * of 3, one for each terminal.  Each group of 3 consists
			 * of the node name to which the terminal connects, the
			 * length of the terminal, and an attribute list (or the
			 * token 0).
			 */
{
    int n, nterminals, pn;
    HashEntry *he;
    DevTerm *term;
    Dev *newdev, devtmp;
    DevParam *newparm, *devp, *sparm;
    TileType ttype;
    int dev_type;
    char ptype, *pptr, **av;
    char devhash[64];
    int argstart = 1;	/* start of terminal list in argv[] */
    bool hasModel = strcmp(type, "None") ? TRUE : FALSE;

    int area, perim;	/* Total area, perimeter of primary type (i.e., channel) */

    newdev = (Dev *)NULL;
    devtmp.dev_subsnode = NULL;
    devtmp.dev_cap = 0.0;
    devtmp.dev_res = 0.0;
    devtmp.dev_area = 0;
    devtmp.dev_perim = 0;
    devtmp.dev_length = 0;
    devtmp.dev_width = 0;
    devtmp.dev_params = NULL;

    switch (class)
    {
	case DEV_FET:
	case DEV_MOSFET:
	case DEV_ASYMMETRIC:
	case DEV_BJT:
	    argstart = 3;
	    break;
	case DEV_DIODE:
	case DEV_NDIODE:
	case DEV_PDIODE:
	    argstart = 0;
	    break;
	case DEV_RES:
	case DEV_CAP:
	case DEV_CAPREV:
	    if (hasModel)
		argstart = 2;
	    break;
	case DEV_SUBCKT:
	case DEV_MSUBCKT:
	case DEV_RSUBCKT:
	case DEV_CSUBCKT:
	    argstart = 0;
    }

    devp = efGetDeviceParams(type);

    /* Parse initial arguments for parameters */
    while ((pptr = strchr(argv[argstart], '=')) != NULL)
    {
	// Check if this parameter is in the table.
	// If so, handle appropriately.  Otherwise, the
	// parameter gets saved verbatim locally.  The
	// "parameters" line comes before any "device" line
	// in the .ext file, so the table should be complete.

	*pptr = '\0';
	for (sparm = devp; sparm; sparm = sparm->parm_next)
	    if (!strcasecmp(sparm->parm_type, argv[argstart]))
		break;
	*pptr = '=';
	if (sparm == NULL)
	{
	    /* Copy the parameter into dev_params */
	    /* (parm_type and parm_scale records are not used) */
	    newparm = (DevParam *)mallocMagic(sizeof(DevParam));
	    newparm->parm_name = StrDup((char **)NULL, argv[argstart]);
	    newparm->parm_next = devtmp.dev_params;
	    devtmp.dev_params = newparm;
	    argstart++;
	    continue;
	}

	pptr++;
	switch(*argv[argstart])
	{
	    case 'a':
		if ((pptr - argv[argstart]) == 2)
		    devtmp.dev_area = atoi(pptr);
		else
		{
		    pn = *(argv[argstart] + 1) - '0';
		    if (pn == 0)
			devtmp.dev_area = (int)(0.5 + (float)atoi(pptr)
				* locScale * locScale);
		    /* Otherwise, punt */
		}
		break;
	    case 'p':
		if ((pptr - argv[argstart]) == 2)
		    devtmp.dev_perim = atoi(pptr);
		else
		{
		    pn = *(argv[argstart] + 1) - '0';
		    if (pn == 0)
			devtmp.dev_perim = (int)(0.5 + (float)atoi(pptr) * locScale);
		    /* Otherwise, use verbatim */
		}
		break;
	    case 'l':
		devtmp.dev_length = (int)(0.5 + (float)atoi(pptr) * locScale);
		break;
	    case 'w':
		devtmp.dev_width = (int)(0.5 + (float)atoi(pptr) * locScale);
		break;
	    case 'c':
		devtmp.dev_cap = (float)atof(pptr);
		break;
	    case 'r':
		devtmp.dev_res = (float)atof(pptr);
		break;
	}
	argstart++;
    }

    /* Check for optional substrate node */
    switch (class)
    {
	case DEV_RES:
	case DEV_CAP:
	case DEV_CAPREV:
	case DEV_RSUBCKT:
	case DEV_CSUBCKT:
	case DEV_MSUBCKT:
	case DEV_SUBCKT:
	case DEV_DIODE:
	case DEV_NDIODE:
	case DEV_PDIODE:
	    n = argc - argstart;
	    if ((n % 3) == 1)
	    {
		if (strncmp(argv[argstart], "None", 4) != 0)
		    devtmp.dev_subsnode = efBuildDevNode(def, argv[argstart], TRUE);

		argstart++;
	    }
	    break;
    }

    /* Between argstart and argc, we should only have terminal triples */
    if (((argc - argstart) % 3) != 0)
	return 1;

    nterminals = (argc - argstart) / 3;

    dev_type = efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, type);

    /* Determine if this device has been seen before */
    /* NOTE:  This is done by tile type, not name, because the extresist
     * device extraction is less sophisticated than the standard extraction
     * and does not differentiate between different device names belonging
     * to the same tile type.  The extGetDevType() function is not efficient,
     * and all of this needs to be done better.
     */
    
    ttype = extGetDevType(type);
    if (ttype < 0)
    {
	/* For zero-ohm resistors used to separate ports on the same	*/
	/* net, generate a unique devhash.				*/
	ttype = DBNumTypes;
	while (1)
	{
	    sprintf(devhash, "%dx%d_%d", r->r_xbot, r->r_ybot, ttype);
	    he = HashLookOnly(&def->def_devs, devhash);
	    if (he == NULL) break;
	    ttype++;
	}
    }

    sprintf(devhash, "%dx%d_%d", r->r_xbot, r->r_ybot, ttype);
    he = HashFind(&def->def_devs, devhash);
    newdev = (Dev *)HashGetValue(he);

    if (newdev)
    {
	/* Duplicate device.  Duplicates will only appear in res.ext files
	 * where a device has nodes changed.  Merge all properties of the
	 * original device with nodes from the new device.  Keep the
	 * original device and discard the new one.
	 *
	 * Check that the device is actually the same device type and number
	 * of terminals.  If not, throw an error and abandon the new device.
	 *
	 * NOTE:  Quick check is made on dev_type, but for the reason stated
	 * above for the calculation of ttype, only the tile types need to
	 * match, so make an additional (expensive) check on tile type.
	 */

        if ((newdev->dev_class != class) || ((newdev->dev_type != dev_type)
		 && (ttype != extGetDevType(EFDevTypes[newdev->dev_type]))))
	{
	    TxError("Device %s %s at (%d, %d) overlaps incompatible device %s %s!\n",
		    extDevTable[(unsigned char)class], type, r->r_xbot, r->r_ybot,
		    extDevTable[newdev->dev_class], EFDevTypes[newdev->dev_type]);
	    return 0;
	}
        else if (newdev->dev_nterm != nterminals)
	{
	    TxError("Device %s %s at (%d, %d) overlaps device with incompatible"
		    " number of terminals (%d vs. %d)!\n",
		    extDevTable[(unsigned char)class], type, r->r_xbot, r->r_ybot, nterminals,
		    newdev->dev_nterm);
	    return 0;
	}
    }
    else
    {
	newdev = (Dev *) mallocMagic((unsigned) DevSize(nterminals));

	/* Add this dev to the hash table for def */
	HashSetValue(he, (ClientData)newdev);

        newdev->dev_cap = devtmp.dev_cap;
        newdev->dev_res = devtmp.dev_res;
        newdev->dev_area = devtmp.dev_area;
        newdev->dev_perim = devtmp.dev_perim;
        newdev->dev_length = devtmp.dev_length;
        newdev->dev_width = devtmp.dev_width;
        newdev->dev_params = devtmp.dev_params;

        newdev->dev_nterm = nterminals;
        newdev->dev_rect = *r;
        newdev->dev_type = dev_type;
        newdev->dev_class = class;

        switch (class)
        {
	    case DEV_FET:		/* old-style "fet" record */
		newdev->dev_area = atoi(argv[0]);
		newdev->dev_perim = atoi(argv[1]);
		break;
	    case DEV_MOSFET:	/* new-style "device mosfet" record */
	    case DEV_ASYMMETRIC:
	    case DEV_BJT:
		newdev->dev_length = atoi(argv[0]);
		newdev->dev_width = atoi(argv[1]);
		break;
	    case DEV_RES:
		if (hasModel && StrIsInt(argv[0]) && StrIsInt(argv[1]))
		{
		    newdev->dev_length = atoi(argv[0]);
		    newdev->dev_width = atoi(argv[1]);
		}
		else if (StrIsNumeric(argv[0]))
		{
		    newdev->dev_res = (float)atof(argv[0]);
		}
		else
		{
		    if (hasModel)
		    {
			efReadError("Error: expected L and W, got %s %s\n", argv[0],
				argv[1]);
			newdev->dev_length = 0;
			newdev->dev_width = 0;
		    }
		    else
		    {
			efReadError("Error: expected resistance value, got %s\n",
				    argv[0]);
			newdev->dev_res = 0.0;
		    }
		}
		break;
	    case DEV_CAP:
	    case DEV_CAPREV:
		if (hasModel && StrIsInt(argv[0]) && StrIsInt(argv[1]))
		{
		    newdev->dev_length = atoi(argv[0]);
		    newdev->dev_width = atoi(argv[1]);
		}
		else if (StrIsNumeric(argv[0]))
		{
		    newdev->dev_cap = (float)atof(argv[0]);
		}
		else
		{
		    if (hasModel)
		    {
			efReadError("Error: expected L and W, got %s %s\n", argv[0],
				argv[1]);
			newdev->dev_length = 0;
			newdev->dev_width = 0;
		    }
		    else
		    {
			efReadError("Error: expected capacitance value, got %s\n",
					    argv[0]);
			newdev->dev_cap = 0.0;
		    }
		}
		break;
	}
    }

    newdev->dev_subsnode = devtmp.dev_subsnode;
    switch (class)
    {
	case DEV_FET:		/* old-style "fet" record */
	    newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);
	    break;
	case DEV_MOSFET:	/* new-style "device mosfet" record */
	case DEV_ASYMMETRIC:
	case DEV_BJT:
	    /* "None" in the place of the substrate name means substrate is ignored */
	    if ((argstart == 3) && (strncmp(argv[2], "None", 4) != 0))
		newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);
	    break;
	case DEV_RES:
	    if ((argstart == 3) && (strncmp(argv[2], "None", 4) != 0))
		newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);

	    break;
	case DEV_CAP:
	case DEV_CAPREV:
	    if ((argstart == 3) && (strncmp(argv[2], "None", 4) != 0))
		newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);

	    break;
    }

#define	TERM_NAME	0
#define	TERM_PERIM	1
#define	TERM_ATTRS	2

    for (av = &argv[argstart], n = 0; n < nterminals; n++, av += 3)
    {
	term = &newdev->dev_terms[n];
	term->dterm_node = efBuildDevNode(def, av[TERM_NAME], FALSE);
	term->dterm_length = atoi(av[TERM_PERIM]);

	/* If the attr list is '0', this signifies no attributes */
	if (av[TERM_ATTRS][0] == '0' && av[TERM_ATTRS][1] == '\0')
	    term->dterm_attrs = (char *) NULL;
	else
	    term->dterm_attrs = StrDup((char **) NULL, av[TERM_ATTRS]);
    }

#undef	TERM_NAME
#undef	TERM_PERIM
#undef	TERM_ATTRS

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildPortNode --
 *
 *	Look for the node named 'name' in the local table for 'def', or in
 *	the global node name table.  If it doesn't already exist, create a
 *	EFNode for it, and set capacitance and area/perimeter to zero.
 *	Set the efnode_flags value to EF_PORT, with the port number encoded
 *	in the efNodeName structure.
 *
 * ----------------------------------------------------------------------------
 */

void
efBuildPortNode(def, name, idx, x, y, layername, toplevel)
    Def *def;		/* Def to which this connection is to be added */
    char *name;		/* One of the names for this node */
    int idx;		/* Port number (order) */
    int x; int y;	/* Location of a point inside this node */
    char *layername;	/* Name of tile type */
    bool toplevel;	/* 1 if the cell def is the top level cell */
{
    HashEntry *he;
    EFNodeName *nn;

    he = HashFind(&def->def_nodes, name);
    nn = (EFNodeName *) HashGetValue(he);
    if (nn == (EFNodeName *) NULL)
    {
	/* Create node if it doesn't already exist */
	efBuildNode(def, FALSE, FALSE, FALSE, name, (double)0, x, y,
			layername, (char **) NULL, 0);

	nn = (EFNodeName *) HashGetValue(he);
    }
    if (nn != (EFNodeName *) NULL)
    {
	nn->efnn_node->efnode_flags |= EF_PORT;
	if (toplevel)
	    nn->efnn_node->efnode_flags |= EF_TOP_PORT;
	nn->efnn_port = idx;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * EFGetPortMax --
 *
 * Find the highest port number in the cell def and return the value.
 *
 * Results:
 *	Value of highest port number in the cell def's node list
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
EFGetPortMax(def)
   Def *def;
{
    EFNode *snode;
    EFNodeName *nodeName;
    int portmax, portorder;

    portmax = -1;

    for (snode = (EFNode *) def->def_firstn.efnode_next;
                snode != &def->def_firstn;
                snode = (EFNode *) snode->efnode_next)
    {
        if (snode->efnode_flags & EF_PORT)
	{
	    for (nodeName = snode->efnode_name; nodeName != NULL; nodeName =
                        nodeName->efnn_next)
	    {
		portorder = nodeName->efnn_port;
		if (portorder > portmax) portmax = portorder;
	    }
	}
    }
    return portmax;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildDevNode --
 *
 * Look for the node named 'name' in the local table for 'def', or
 * in the global node name table.  If it doesn't already exist,
 * create a EFNode for it.  If 'isSubsNode' is TRUE, this is node
 * is a substrate node and may not exist yet; otherwise, the node
 * must already exist.
 *
 * Results:
 *	Returns a pointer to the EFNode for 'name'.
 *
 * Side effects:
 *	May create a new node, as per above.
 *
 * ----------------------------------------------------------------------------
 */

EFNode *
efBuildDevNode(def, name, isSubsNode)
    Def *def;
    char *name;
    bool isSubsNode;
{
    HashEntry *he;
    EFNodeName *nn;
    bool isNewNode = FALSE;

    he = HashFind(&def->def_nodes, name);
    nn = (EFNodeName *) HashGetValue(he);
    if (nn == (EFNodeName *) NULL)
    {
	/* Create node if it doesn't already exist */
	if (efWarn && !isSubsNode)
	    efReadError("Node %s doesn't exist so creating it\n", name);
	efBuildNode(def, FALSE, isSubsNode, FALSE, name, (double)0, 0, 0,
		(char *) NULL, (char **) NULL, 0);

	nn = (EFNodeName *) HashGetValue(he);
	ASSERT(nn, "nn");
	isNewNode = TRUE;
    }
    if (isSubsNode || (nn->efnn_node->efnode_flags & EF_GLOB_SUBS_NODE))
    {
	if (!EFHNIsGlob(nn->efnn_hier))
	{
	    /* This node is declared to be an implicit port */
	    nn->efnn_node->efnode_flags |= EF_SUBS_PORT;
	    if (isNewNode == TRUE)
		nn->efnn_port = -1;
	    def->def_flags |= DEF_SUBSNODES;
	}
	nn->efnn_node->efnode_flags |= EF_SUBS_NODE;
	if (isNewNode)
	    nn->efnn_node->efnode_flags |= EF_DEVTERM;
    }
    return nn->efnn_node;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildAddStr --
 *
 * Return the index of 'str' in 'table'.
 * Add the string 'str' to the table 'table' if it's not already there.
 *
 * Results:
 *	See above.
 *
 * Side effects:
 *	Increments *pMax if we add an entry to the table.
 *
 * ----------------------------------------------------------------------------
 */

int
efBuildAddStr(table, pMax, size, str)
    char *table[];	/* Table to search */
    int *pMax;		/* Increment this if we add an entry */
    int size;		/* Maximum size of table */
    char *str;		/* String to add */
{
    int n, max;

    max = *pMax;
    for (n = 0; n < max; n++)
	if (strcmp(table[n], str) == 0)
	    return n;

    if (max >= size)
    {
	printf("Too many entries in table (max is %d) to add %s\n", size, str);
	printf("Recompile libextflat.a with a bigger table size\n");
	exit (1);
    }

    table[n++] = StrDup((char **) NULL, str);
    *pMax = n;

    return max;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildUse --
 *
 * Process a "use" line from a .ext file.
 * Creates a new use by the name 'subUseId' of the def named 'subDefName'.
 * If 'subDefName' doesn't exist, it is created, but left marked as
 * unavailable so that readfile() will read it in after it is done
 * with this file.  If 'subUseId' ends in an array subscript, e.g,
 *	useid[xlo:xhi:xsep][ylo:yhi:ysep]
 * its ArrayInfo is filled in from this information; otherwise, its
 * ArrayInfo is marked as not being needed (xlo == xhi, ylo == yhi).
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
efBuildUse(def, subDefName, subUseId, ta, tb, tc, td, te, tf)
    Def *def;		/* Def to which this connection is to be added */
    char *subDefName;	/* Def of which this a use */
    char *subUseId;	/* Use identifier for the def 'subDefName' */
    int ta, tb, tc,
	td, te, tf;	/* Elements of a transform from coordinates of
			 * subDefName up to def.
			 */
{
    Use *newuse;
    Def *newdef;
    char *cp;
    HashEntry *he;

    newdef = efDefLook(subDefName);
    if (newdef == NULL)
	newdef = efDefNew(subDefName);

    newuse = (Use *) mallocMagic((unsigned)(sizeof (Use)));
    newuse->use_def = newdef;
    newuse->use_trans.t_a = ta;
    newuse->use_trans.t_b = tb;
    newuse->use_trans.t_c = tc;
    newuse->use_trans.t_d = td;
    newuse->use_trans.t_e = te;
    newuse->use_trans.t_f = tf;

    /* Set the use identifier and array information */
    if ((cp = strchr(subUseId, '[')) == NULL)
    {
	newuse->use_id = StrDup((char **) NULL, subUseId);
	newuse->use_xlo = newuse->use_xhi = 0;
	newuse->use_ylo = newuse->use_yhi = 0;
	newuse->use_xsep = newuse->use_ysep = 0;
    }
    else
    {
	/* Note: Preserve any use of brackets as-is other than the  */
	/* standard magic array notation below.  This allows, for   */
	/* example, verilog instance arrays read from DEF files	to  */
	/* be passed through correctly.				    */

	if ((sscanf(cp, "[%d:%d:%d][%d:%d:%d]",
		    &newuse->use_xlo, &newuse->use_xhi, &newuse->use_xsep,
		    &newuse->use_ylo, &newuse->use_yhi, &newuse->use_ysep)) == 6)
	{
	    *cp = '\0';
	    newuse->use_id = StrDup((char **) NULL, subUseId);
	    *cp = '[';
	}
	else
	{
	    newuse->use_id = StrDup((char **) NULL, subUseId);
	    newuse->use_xlo = newuse->use_xhi = 0;
	    newuse->use_ylo = newuse->use_yhi = 0;
	    newuse->use_xsep = newuse->use_ysep = 0;
	}
    }

    he = HashFind(&def->def_uses, newuse->use_id);
    if (HashGetValue(he))
        TxError("Warning: use %s appears more than once in def!\n", newuse->use_id);
    HashSetValue(he, (ClientData)newuse);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildConnect --
 *
 * Process a "connect" line from a .ext file.
 * Creates a connection record for the names 'nodeName1' and
 * 'nodeName2'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a new connection record, and prepends it to the
 *	list for def.
 *
 * ----------------------------------------------------------------------------
 */

void
efBuildConnect(def, nodeName1, nodeName2, deltaC, av, ac)
    Def *def;		/* Def to which this connection is to be added */
    char *nodeName1;	/* Name of first node in connection */
    char *nodeName2;	/* Name of other node in connection */
    double deltaC;	/* Adjustment in capacitance */
    char **av;		/* Strings for area, perimeter adjustment */
    int ac;		/* Number of strings in av */
{
    int n;
    Connection *conn;

    unsigned size = sizeof (Connection)
		    + (efNumResistClasses - 1) * sizeof (EFPerimArea);

    if ((EFOutputFlags & EF_SHORT_MASK) != EF_SHORT_NONE)
    {
	/* Handle the case where two ports on different nets get merged.
	 * If "extract short resistor" or "extract short voltage" has
	 * been specified, then his is similar to parsing an "equiv" statement,
	 * resulting in a shorting resistor or voltage source being placed
	 * between the two node names, and abandoning the merge.
	 */

	HashEntry *he1, *he2;

	he1 = HashLookOnly(&def->def_nodes, nodeName1);
	he2 = HashLookOnly(&def->def_nodes, nodeName2);

	if (he1 && he2)
	{
	    EFNodeName *nn1, *nn2;

	    nn1 = (EFNodeName *) HashGetValue(he1);
	    nn2 = (EFNodeName *) HashGetValue(he2);

	    if (nn1 && nn2 && (nn1->efnn_port >= 0) && (nn2->efnn_port >= 0) &&
			(nn1->efnn_port != nn2->efnn_port))
	    {
		int i;
		int sdev;
		char *argv[10], zeroarg[] = "0";

		if ((EFOutputFlags & EF_SHORT_MASK) == EF_SHORT_R)
		    sdev = DEV_RES;
		else
		    sdev = DEV_VOLT;

		for (i = 0; i < 10; i++) argv[i] = zeroarg;
		argv[0] = StrDup((char **)NULL, "0.0");
		argv[1] = StrDup((char **)NULL, "dummy");
		argv[4] = StrDup((char **)NULL, nodeName1);
		argv[7] = StrDup((char **)NULL, nodeName2);
		efBuildDevice(def, sdev, "None", &GeoNullRect, 10, argv);
		freeMagic(argv[0]);
		freeMagic(argv[1]);
		freeMagic(argv[4]);
		freeMagic(argv[7]);

		return;
	    }
	}
    }

    conn = (Connection *) mallocMagic((unsigned)(size));

    if (efConnInitSubs(conn, nodeName1, nodeName2))
    {
	conn->conn_cap = (EFCapValue) deltaC;
	conn->conn_next = def->def_conns;
	for (n = 0; n < efNumResistClasses && ac > 1; n++, ac -= 2)
	{
	    conn->conn_pa[n].pa_area = (int)(0.5 + (float)atoi(*av++)
		    * locScale * locScale);
	    conn->conn_pa[n].pa_perim = (int)(0.5 + (float)atoi(*av++) * locScale);
	}
	for ( ; n < efNumResistClasses; n++)
	    conn->conn_pa[n].pa_area = conn->conn_pa[n].pa_perim = 0;
	def->def_conns = conn;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildResistor --
 *
 * Process a "resistor" line from a .ext file.
 * Creates a resistor record for the names 'nodeName1' and
 * 'nodeName2'.  Both 'nodeName1' and 'nodeName2' must be non-NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a new connection record, and prepends it to the
 *	def_resistors list for def.
 *
 * ----------------------------------------------------------------------------
 */

void
efBuildResistor(def, nodeName1, nodeName2, resistance)
    Def *def;		/* Def to which this connection is to be added */
    char *nodeName1;	/* Name of first node in resistor */
    char *nodeName2;	/* Name of second node in resistor */
    float resistance;	/* Resistor value */
{
    Connection *conn;

    conn = (Connection *) mallocMagic((unsigned)(sizeof (Connection)));
    if (efConnInitSubs(conn, nodeName1, nodeName2))
    {
	conn->conn_res = resistance;
	conn->conn_next = def->def_resistors;
	def->def_resistors = conn;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efBuildCap --
 *
 * Process a "cap" line from a .ext file.
 * Creates a capacitor record for the names 'nodeName1' and
 * 'nodeName2'.  Both 'nodeName1' and 'nodeName2' must be non-NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a new connection record, and prepends it to the
 *	def_caps list for def.
 *
 * ----------------------------------------------------------------------------
 */

void
efBuildCap(def, nodeName1, nodeName2, cap)
    Def *def;		/* Def to which this connection is to be added */
    char *nodeName1;	/* Name of first node in capacitor */
    char *nodeName2;	/* Name of second node in capacitor */
    double cap;		/* Capacitor value */
{
    Connection *conn;

    conn = (Connection *) mallocMagic((unsigned)(sizeof (Connection)));
    if (efConnInitSubs(conn, nodeName1, nodeName2))
    {
	conn->conn_cap = (EFCapValue) cap;
	conn->conn_next = def->def_caps;
	def->def_caps = conn;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efConnInitSubs --
 *
 * Fill in and check the subscript information for the newly allocated
 * Connection 'conn'.
 *
 * Results:
 *	Returns TRUE if successful, FALSE on error.
 *
 * Side effects:
 *	Fills in the two ConnNames conn->conn_1 and conn->conn_2.
 *	Frees 'conn' in the event of an error.
 *
 * ----------------------------------------------------------------------------
 */

bool
efConnInitSubs(conn, nodeName1, nodeName2)
    Connection *conn;
    char *nodeName1, *nodeName2;
{
    ConnName *c1, *c2;
    int n;

    c1 = &conn->conn_1;
    c2 = &conn->conn_2;
    if (!efConnBuildName(c1, nodeName1) || !efConnBuildName(c2, nodeName2))
	goto bad;

    if (c1->cn_nsubs != c2->cn_nsubs)
    {
	efReadError("Number of subscripts doesn't match\n");
	goto bad;
    }

    for (n = 0; n < c1->cn_nsubs; n++)
    {
	if (c1->cn_subs[n].r_hi - c1->cn_subs[n].r_lo
		!= c2->cn_subs[n].r_hi - c2->cn_subs[n].r_lo)
	{
	    efReadError("Subscript %d range mismatch\n", n);
	    goto bad;
	}
    }
    return TRUE;

bad:
    if (c1->cn_name) freeMagic((char *) c1->cn_name);
    if (c2->cn_name) freeMagic((char *) c2->cn_name);
    freeMagic((char *) conn);
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efConnBuildName --
 *
 * Fill in the fields of 'cnp' from the string 'name'.
 * If 'name' contains no trailing subscript ranges (which are
 * of the form [lo1:hi1] or [lo1:hi1,lo2:hi2], or [lo1:hi1][lo2:hi2] for
 * compatibility with older versions of Magic), we set cnp->cn_nsubs
 * to zero and cnp->cn_name to a copy of 'name'.  Otherwise, we decode
 * the subscripts and fill in cnp->cn_subs and cnp->cn_nsubs appropriately.
 *
 * Results:
 *	Returns TRUE if successful, FALSE on error.
 *
 * Side effects:
 *	Fills in the fields of the ConnName 'cnp'.
 *
 * ----------------------------------------------------------------------------
 */

bool
efConnBuildName(cnp, name)
    ConnName *cnp;
    char *name;
{
    char *srcp, *dstp, *cp, *dp;
    int nsubs;
    Range *rp;
    char newname[1024];
    char c;

    cnp->cn_nsubs = 0;
    if (name == NULL)
    {
	cnp->cn_name = NULL;
	return TRUE;
    }

    cp = name;
    /* Make sure it's an array subscript range before treating it specially */
again:
    if ((cp = strchr(cp, '[')) == NULL)
    {
	cnp->cn_name = StrDup((char **) NULL, name);
	return TRUE;
    }
    for (dp = cp + 1; *dp && *dp != ':'; dp++)
    {
	if (*dp == ']')
	{
	    cp = dp+1;
	    goto again;
	}
    }

    /* Copy the initial part of the name */
    for (srcp = name, dstp = newname; srcp < cp; *dstp++ = *srcp++)
	/* Nothing */;

    /* Replace each subscript range with %d */
    for (nsubs = 0, rp = cnp->cn_subs; (c = *cp) == '[' || c == ','; nsubs++)
    {
	if (nsubs >= MAXSUBS)
	{
	    efReadError("Too many array subscripts (maximum=2)\n");
	    return FALSE;
	}
	if (sscanf(++cp, "%d:%d", &rp[nsubs].r_lo, &rp[nsubs].r_hi) != 2)
	{
	    efReadError("Subscript syntax error\n");
	    return FALSE;
	}
	if (rp[nsubs].r_lo > rp[nsubs].r_hi)
	{
	    efReadError("Backwards subscript range [%d:%d]\n",
			rp[nsubs].r_lo, rp[nsubs].r_hi);
	    return FALSE;
	}

	while (*cp && *cp != ']' && *cp != ',')
	    cp++;
	if (*cp == ']') cp++;
    }

    /* Generate format for sprintf */
    *dstp++ = '[';
    *dstp++ = '%';
    *dstp++ = 'd';
    if (nsubs == 2)
    {
	*dstp++ = ',';
	*dstp++ = '%';
	*dstp++ = 'd';
    }
    *dstp++ = ']';

    /* Copy remainder of path */
    while ((*dstp++ = *cp++))
	/* Nothing */;

    cnp->cn_name = StrDup((char **) NULL, newname);
    cnp->cn_nsubs = nsubs;
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * efNodeAddName --
 *
 * Add a name to the list for 'node'.
 * We already have a HashEntry for the new name.
 * The new name is added to the front of the list
 * for 'node' only if it is higher in precedence
 * than the name already at the front of the list.
 * Sets the value of 'he' to be the newly allocated
 * EFNodeName.
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
efNodeAddName(node, he, hn, isNew)
    EFNode *node;
    HashEntry *he;
    HierName *hn;
    bool isNew;		// If TRUE, added name is never the preferred name.
{
    EFNodeName *newnn;
    EFNodeName *oldnn;
    bool topport;	// New node to add is a top-level port

    newnn = (EFNodeName *) mallocMagic((unsigned)(sizeof (EFNodeName)));
    newnn->efnn_node = node;
    newnn->efnn_hier = hn;
    newnn->efnn_port = -1;
    newnn->efnn_refc = 0;

    HashSetValue(he, (char *)newnn);

    /* If the node is a port of the top level cell, denoted by flag	*/
    /* EF_TOP_PORT, then the name given to the port always stays at the	*/
    /* head of the list.						*/

    topport = (node->efnode_flags & EF_TOP_PORT) ? TRUE : FALSE;

    /* Link in the new name */
    oldnn = node->efnode_name;
    if ((oldnn == NULL) || (EFHNBest(newnn->efnn_hier, oldnn->efnn_hier)
		&& !topport && !isNew))
    {
	/* New head of list */
	newnn->efnn_next = oldnn;
	node->efnode_name = newnn;
    }
    else
    {
	/* Link it in behind the head of the list */
	newnn->efnn_next = oldnn->efnn_next;
	oldnn->efnn_next = newnn;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efNodeMerge --
 *
 * Combine two nodes.  The resistances and capacitances are summed.
 * The attribute lists are appended.  The location chosen is the
 * lower-leftmost, with lowest being considered before leftmost.
 * The canonical name of the new node is taken to be the highest
 * precedence among the names for all nodes.
 *
 * One of the nodes will no longer be referenced, so we arbitrarily
 * make this node2 and free its memory.
 *
 * Results:
 *	Return the pointer to the node that is removed and will be
 *	deallocated.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

EFNode *
efNodeMerge(node1ptr, node2ptr)
    EFNode **node1ptr, **node2ptr;	/* Pointers to hierarchical nodes */
{
    EFNodeName *nn, *nnlast;
    EFAttr *ap;
    int n;
    EFNode *keeping, *removing;

    /* Sanity check: ignore if same node */
    if (*node1ptr == *node2ptr)
	return NULL;

    /* Keep the node with the greater number of entries, and merge  */
    /* the node with fewer entries into it.			    */

    if ((*node1ptr)->efnode_num >= (*node2ptr)->efnode_num)
    {
	keeping = *node1ptr;
	removing = *node2ptr;
    }
    else
    {
	keeping = *node2ptr;
	removing = *node1ptr;
    }

    if (efWatchNodes)
    {
	if (HashLookOnly(&efWatchTable, (char *) keeping->efnode_name->efnn_hier)
	    || (removing->efnode_name
		&& HashLookOnly(&efWatchTable,
				(char *) removing->efnode_name->efnn_hier)))
	{
	    printf("\ncombine: %s\n",
		EFHNToStr(keeping->efnode_name->efnn_hier));
	    printf("  with   %s\n\n",
		removing->efnode_name
		    ? EFHNToStr(removing->efnode_name->efnn_hier)
		    : "(unnamed)");
	}
    }

    /* Sum capacitances, perimeters, areas */
    keeping->efnode_cap += removing->efnode_cap;
    for (n = 0; n < efNumResistClasses; n++)
    {
	keeping->efnode_pa[n].pa_area += removing->efnode_pa[n].pa_area;
	keeping->efnode_pa[n].pa_perim += removing->efnode_pa[n].pa_perim;
    }

    /* Make all EFNodeNames point to "keeping" */
    if (removing->efnode_name)
    {
	bool topportk, topportr;

	for (nn = removing->efnode_name; nn; nn = nn->efnn_next)
	{
	    nnlast = nn;
	    nn->efnn_node = keeping;
	}

	topportk = (keeping->efnode_flags & EF_TOP_PORT) ?  TRUE : FALSE;
	topportr = (removing->efnode_flags & EF_TOP_PORT) ?  TRUE : FALSE;

	/* Concatenate list of EFNodeNames, taking into account precedence */
	if ((!keeping->efnode_name) || (!topportk && (topportr
		    || EFHNBest(removing->efnode_name->efnn_hier,
		     keeping->efnode_name->efnn_hier))))
	{
	    /*
	     * New official name is that of "removing".
	     * The new list of names is:
	     *	removing-names, keeping-names
	     */
	    nnlast->efnn_next = keeping->efnode_name;
	    keeping->efnode_name = removing->efnode_name;

	    /*
	     * Choose the new location only if "removing"'s location is a valid one,
	     * i.e, "removing" wasn't created before it was mentioned.  This is mainly
	     * to deal with new fets, resistors, and capacitors created by resistance
	     * extraction, which appear with their full hierarchical names in the
	     * .ext file for the root cell.
	     *
	     * This code has been moved up from below so that the original
	     * location and type will be prefered when the original name
	     * is preferred.
	     *
	     * I am purposefully subverting the original specification that
	     * the node refer to the bottom corner of the network.  Does
	     * this have any effect on exttosim or exttospice?
	     *
	     *					Tim, 6/14/04
	     */
	    if (removing->efnode_type > 0)
	    {
		keeping->efnode_loc = removing->efnode_loc;
		keeping->efnode_type = removing->efnode_type;
	    }
	}
	else
	{
	    /*
	     * Keep old official name.
	     * The new list of names is:
	     *	keeping-names[0], removing-names, keeping-names[1-]
	     */
	    nnlast->efnn_next = keeping->efnode_name->efnn_next;
	    keeping->efnode_name->efnn_next = removing->efnode_name;
	}
    }

    /* Merge list counts */
    keeping->efnode_num += removing->efnode_num;

    /* Merge attribute lists */
    if ((ap = removing->efnode_attrs))
    {
	while (ap->efa_next)
	    ap = ap->efa_next;
	ap->efa_next = keeping->efnode_attrs;
	keeping->efnode_attrs = ap;
	removing->efnode_attrs = (EFAttr *) NULL;	/* Sanity */
    }

    /* Unlink "removing" from list for def */
    removing->efnode_prev->efnhdr_next = removing->efnode_next;
    removing->efnode_next->efnhdr_prev = removing->efnode_prev;

    /*
     * Only if both nodes were EF_DEVTERM do we keep EF_DEVTERM set
     * in the resultant node.
     */
    if ((removing->efnode_flags & EF_DEVTERM) == 0)
	keeping->efnode_flags &= ~EF_DEVTERM;

    /*
     * If "removing" has the EF_PORT flag set, then copy the port
     * record in the flags to node1.
     */
    if (removing->efnode_flags & EF_PORT)
	keeping->efnode_flags |= EF_PORT;
    if (removing->efnode_flags & EF_TOP_PORT)
	keeping->efnode_flags |= EF_TOP_PORT;
    if (removing->efnode_flags & EF_SUBS_PORT)
	keeping->efnode_flags |= EF_SUBS_PORT;

    /*
     * If "removing" has the EF_SUBS_NODE flag set, then copy the port
     * record in the flags to "keeping".
     */
    if (removing->efnode_flags & EF_SUBS_NODE)
	keeping->efnode_flags |= EF_SUBS_NODE;

    /* If EFSaveLocs is set, then merge any disjoint segments from
     * removing to keeping.
     */
    if (EFSaveLocs == TRUE)
    {
	LinkedRect *lr;

	if (keeping->efnode_disjoint == NULL)
	    keeping->efnode_disjoint = removing->efnode_disjoint;
	else
	{
	    for (lr = keeping->efnode_disjoint; lr->r_next; lr = lr->r_next);
	    lr->r_next = removing->efnode_disjoint;
	}
    }

    removing->efnode_flags = 0;

    /* Get rid of "removing" */
    freeMagic((char *) removing);

    /* Make sure that the active node is always node1 */
    *node1ptr = keeping;
    *node2ptr = (EFNode *)NULL;	    /* Sanity check */

    return removing;
}


/*
 * ----------------------------------------------------------------------------
 *
 * efFreeUseTable --
 *
 * Free the cell IDs allocated for each entry in the use hash table, and
 * the memory allocated by the cell use, leaving the hash entry null.
 *
 * ----------------------------------------------------------------------------
 */

void
efFreeUseTable(table)
    HashTable *table;
{
    HashSearch hs;
    HashEntry *he;
    Use *use;
    HierName *hn;
    EFNodeName *nn;

    HashStartSearch(&hs);
    while ((he = HashNext(table, &hs)))
	if ((use = (Use *) HashGetValue(he)))
	{
	    if (use->use_id != NULL) freeMagic((char *)use->use_id);
	    freeMagic(use);
	}
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFreeDevTable --
 *
 * Free the device records allocated for each entry in the device hash table,
 * the memory allocated by the device, leaving the hash entry null.
 *
 * ----------------------------------------------------------------------------
 */

void
efFreeDevTable(table)
    HashTable *table;
{
    Dev *dev;
    HashSearch hs;
    HashEntry *he;
    int n;

    HashStartSearch(&hs);
    while ((he = HashNext(table, &hs)))
    {
	dev = (Dev *)HashGetValue(he);
	for (n = 0; n < (int)dev->dev_nterm; n++)
	    if (dev->dev_terms[n].dterm_attrs)
		freeMagic((char *) dev->dev_terms[n].dterm_attrs);
	freeMagic((char *) dev);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFreeNodeTable --
 *
 * Free the EFNodeNames (and the HierNames they point to) pointed to by
 * the entries in the HashTable 'table'.  Each EFNodeName is assumed to
 * be pointed to by exactly one HashEntry, but each HierName can be
 * pointed to by many entries (some of which may be in other HashTables).
 * As a result, the HierNames aren't freed here; instead, an entry is
 * added to efFreeHashTable for each HierName encountered.  Everything
 * is then freed at the end by EFDone().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *	Adds an entry to hnTable for each HierName.
 *
 * ----------------------------------------------------------------------------
 */

void
efFreeNodeTable(table)
    HashTable *table;
{
    HashSearch hs;
    HashEntry *he;
    HierName *hn;
    EFNodeName *nn;

    HashStartSearch(&hs);
    while ((he = HashNext(table, &hs)))
	if ((nn = (EFNodeName *) HashGetValue(he)))
	{
	    for (hn = nn->efnn_hier; hn; hn = hn->hn_parent)
		(void) HashFind(&efFreeHashTable, (char *) hn);

	    /* Node equivalences made by "equiv" statements are	handled	*/
	    /* by reference count.  Don't free the node structure until	*/
	    /* all references have been seen.				*/

	    if (nn->efnn_refc > 0)
		nn->efnn_refc--;
	    else
		freeMagic((char *) nn);
	}
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFreeNodeList --
 *
 * Free the circular list of nodes of which 'head' is the head.
 * Don't free 'head' itself, since it's statically allocated.
 *
 * If the client (e.g., ext2spice, ext2sim, ...) allocates memory for a
 * node, then that client needs to provide a function "func" of the form:
 *
 *              int func(client) 
 *		    ClientData client;
 *		{
 *		}
 *
 * The return value is unused but should be zero for consistency.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.  Calls func() to free additional memory allocated
 *	in the node structure by a client.
 *
 * ----------------------------------------------------------------------------
 */

void
efFreeNodeList(head, func)
    EFNode *head;
    int (*func)();
{
    EFNode *node;
    EFAttr *ap;
    LinkedRect *lr;

    free_magic1_t mm1 = freeMagic1_init();
    for (node = (EFNode *) head->efnode_next;
	    node != head;
	    node = (EFNode *) node->efnode_next)
    {
	{
	    free_magic1_t mm1_ = freeMagic1_init();
	    for (ap = node->efnode_attrs; ap; ap = ap->efa_next)
		freeMagic1(&mm1_, (char *) ap);
	    freeMagic1_end(&mm1_);
	}
	if (node->efnode_client != (ClientData)NULL)
	{
	    if (func != NULL)
		(*func)(node->efnode_client);
	    freeMagic((char *)node->efnode_client);
	}
	{
	    free_magic1_t mm1_ = freeMagic1_init();
	    for (lr = node->efnode_disjoint; lr; lr = lr->r_next)
		freeMagic1(&mm1_, (char *)lr);
	    freeMagic1_end(&mm1_);
	}

	freeMagic1(&mm1, (char *) node);
    }
    freeMagic1_end(&mm1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * efFreeConn --
 *
 * Free the Connection *conn.
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
efFreeConn(conn)
    Connection *conn;
{
    if (conn->conn_name1) freeMagic(conn->conn_name1);
    if (conn->conn_name2) freeMagic(conn->conn_name2);
    freeMagic((char *) conn);
}
