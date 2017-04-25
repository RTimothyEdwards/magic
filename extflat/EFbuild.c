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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFbuild.c,v 1.6 2010/06/24 12:37:17 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>		/* for atof() */
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"	/* for device class list */

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
char *EFDevTypes[MAXDEVTYPES];
int   EFDevNumTypes;

/* Table of Magic layers */
char *EFLayerNames[MAXTYPES] = { "space" };
int   EFLayerNumNames;

/* Forward declarations */
Connection *efAllocConn();
EFNode *efBuildDevNode();
void efNodeAddName();
void efNodeMerge();

bool efConnBuildName();
bool efConnInitSubs();


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
 *	None.
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
efBuildNode(def, isSubsnode, nodeName, nodeCap, x, y, layerName, av, ac)
    Def *def;		/* Def to which this connection is to be added */
    bool isSubsnode;	/* TRUE if the node is the substrate */
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

    he = HashFind(&def->def_nodes, nodeName);
    if (newname = (EFNodeName *) HashGetValue(he))
    {
	if (efWarn)
	    efReadError("Warning: duplicate node name %s\n", nodeName);

	/* Just add to C, perim, area of existing node */
	newnode = newname->efnn_node;
	newnode->efnode_cap += (EFCapValue) nodeCap;
	for (n = 0; n < efNumResistClasses && ac > 1; n++, ac -= 2)
	{
	    newnode->efnode_pa[n].pa_area += atoi(*av++);
	    newnode->efnode_pa[n].pa_perim += atoi(*av++);
	}
	return;
    }

    /* Allocate a new node with 'nodeName' as its single name */
    newname = (EFNodeName *) mallocMagic((unsigned)(sizeof (EFNodeName)));
    newname->efnn_hier = EFStrToHN((HierName *) NULL, nodeName);
    newname->efnn_port = -1;	/* No port assignment */
    newname->efnn_next = NULL;
    HashSetValue(he, (char *) newname);

    /* New node itself */
    size = sizeof (EFNode) + (efNumResistClasses - 1) * sizeof (PerimArea);
    newnode = (EFNode *) mallocMagic((unsigned)(size));
    newnode->efnode_flags = (isSubsnode == TRUE) ? EF_SUBS_NODE : 0;
    newnode->efnode_cap = nodeCap;
    newnode->efnode_attrs = (EFAttr *) NULL;
    newnode->efnode_loc.r_xbot = x;
    newnode->efnode_loc.r_ybot = y;
    newnode->efnode_loc.r_xtop = x + 1;
    newnode->efnode_loc.r_ytop = y + 1;
    newnode->efnode_client = (ClientData) NULL;
    if (layerName) newnode->efnode_type =
	    efBuildAddStr(EFLayerNames, &EFLayerNumNames, MAXTYPES, layerName);
    else newnode->efnode_type = 0;

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

    he = HashFind(&def->def_nodes, nodeName);
    if (nodename = (EFNodeName *) HashGetValue(he))
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
    if (dist = (Distance *) HashGetValue(he))
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
efBuildEquiv(def, nodeName1, nodeName2)
    Def *def;		/* Def for which we're adding a new node name */
    char *nodeName1;	/* One of node names to be made equivalent */
    char *nodeName2;	/* Other name to be made equivalent.  One of nodeName1
			 * or nodeName2 must already be known.
			 */
{
    EFNodeName *nn1, *nn2;
    HashEntry *he1, *he2;

    /* Look up both names in the hash table for this def */
    he1 = HashFind(&def->def_nodes, nodeName1);
    he2 = HashFind(&def->def_nodes, nodeName2);

    nn1 = (EFNodeName *) HashGetValue(he1);
    nn2 = (EFNodeName *) HashGetValue(he2);

    if (nn2 == (EFNodeName *) NULL)
    {
	/* Create nodeName1 if it doesn't exist */
	if (nn1 == (EFNodeName *) NULL)
	{
	    if (efWarn)
		efReadError("Creating new node %s\n", nodeName1);
	    efBuildNode(def, FALSE,
		    nodeName1, (double)0, 0, 0,
		    (char *) NULL, (char **) NULL, 0);
	    nn1 = (EFNodeName *) HashGetValue(he1);
	}

	/* Make nodeName2 be another alias for node1 */
	efNodeAddName(nn1->efnn_node, he2,
			EFStrToHN((HierName *) NULL, nodeName2));
	return;
    }

    /* If both names exist and are for different nodes, merge them */
    if (nn1)
    {
	if (nn1->efnn_node != nn2->efnn_node)
	{
	    if (efWarn)
		efReadError("Merged nodes %s and %s\n", nodeName1, nodeName2);
	    efNodeMerge(nn1->efnn_node, nn2->efnn_node);
	}
	return;
    }

    /* Make nodeName1 be another alias for node2 */
    efNodeAddName(nn2->efnn_node, he1,
			EFStrToHN((HierName *) NULL, nodeName1));
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
	char *mult;

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
	    newparm->parm_scale = 1.0;

	// For parameters defined for cell defs, copy the whole
	// expression verbatim into parm_name.  parm_type is
	// reassigned to be a numerical order.

	if (name[0] == ':')
	{
	    newparm->parm_name = StrDup((char **)NULL, argv[n]);
	    newparm->parm_type[1] = '0' + n / 10;
	    newparm->parm_type[0] = '0' + n % 10;
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
			 * The first depend on the type of device.  The rest
			 * are taken in groups of 3, one for each terminal.
			 * Each group of 3 consists of the node name to which
			 * the terminal connects, the length of the terminal,
			 * and an attribute list (or the token 0).
			 */
{
    int n, nterminals, pn;
    DevTerm *term;
    Dev *newdev, devtmp;
    DevParam *newparm, *devp, *sparm;
    char ptype, *pptr, **av;
    int argstart = 1;	/* start of terminal list in argv[] */
    bool hasModel = strcmp(type, "None") ? TRUE : FALSE;

    int area, perim;	/* Total area, perimeter of primary type (i.e., channel) */

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
			devtmp.dev_area = atoi(pptr);
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
			devtmp.dev_perim = atoi(pptr);
		    /* Otherwise, use verbatim */
		}
		break;
	    case 'l':
		devtmp.dev_length = atoi(pptr);
		break;
	    case 'w':
		devtmp.dev_width = atoi(pptr);
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

    newdev = (Dev *) mallocMagic((unsigned) DevSize(nterminals));
    newdev->dev_subsnode = devtmp.dev_subsnode;
    newdev->dev_cap = devtmp.dev_cap;
    newdev->dev_res = devtmp.dev_res;
    newdev->dev_area = devtmp.dev_area;
    newdev->dev_perim = devtmp.dev_perim;
    newdev->dev_length = devtmp.dev_length;
    newdev->dev_width = devtmp.dev_width;
    newdev->dev_params = devtmp.dev_params;

    newdev->dev_nterm = nterminals;
    newdev->dev_rect = *r;
    newdev->dev_type = efBuildAddStr(EFDevTypes, &EFDevNumTypes, MAXDEVTYPES, type);
    newdev->dev_class = class;
 
    switch (class)
    {
	case DEV_FET:		/* old-style "fet" record */
	    newdev->dev_area = atoi(argv[0]);
	    newdev->dev_perim = atoi(argv[1]);
	    newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);
	    break;
	case DEV_MOSFET:	/* new-style "device mosfet" record */
	case DEV_ASYMMETRIC:
	case DEV_BJT:
	    newdev->dev_length = atoi(argv[0]);
	    newdev->dev_width = atoi(argv[1]);

	    /* "None" in the place of the substrate name means substrate is ignored */
	    if ((argstart == 3) && (strncmp(argv[2], "None", 4) != 0))
		newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);
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
		    efReadError("Error: expected resistance value, got %s\n", argv[0]);
		    newdev->dev_res = 0.0;
		}
	    }
	    if ((argstart == 3) && (strncmp(argv[2], "None", 4) != 0))
		newdev->dev_subsnode = efBuildDevNode(def, argv[2], TRUE);

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
		    efReadError("Error: expected capacitance value, got %s\n", argv[0]);
		    newdev->dev_cap = 0.0;
		}
	    }
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
	term->dterm_area = 0;
	term->dterm_perim = 0;

	/* If the attr list is '0', this signifies no attributes */
	if (av[TERM_ATTRS][0] == '0' && av[TERM_ATTRS][1] == '\0')
	    term->dterm_attrs = (char *) NULL;
	else
	    term->dterm_attrs = StrDup((char **) NULL, av[TERM_ATTRS]);
    }

#undef	TERM_NAME
#undef	TERM_PERIM
#undef	TERM_ATTRS

    /* Add this dev to the list for def */
    newdev->dev_next = def->def_devs;
    def->def_devs = newdev;

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
efBuildPortNode(def, name, idx, x, y, layername)
    Def *def;		/* Def to which this connection is to be added */
    char *name;		/* One of the names for this node */
    int idx;		/* Port number (order) */
    int x; int y;	/* Location of a point inside this node */
    char *layername;	/* Name of tile type */
{
    HashEntry *he;
    EFNodeName *nn;

    he = HashFind(&def->def_nodes, name);
    nn = (EFNodeName *) HashGetValue(he);
    if (nn == (EFNodeName *) NULL)
    {
	/* Create node if it doesn't already exist */
	efBuildNode(def, FALSE, name, (double)0, x, y,
			layername, (char **) NULL, 0);

	nn = (EFNodeName *) HashGetValue(he);
    }
    if (nn != (EFNodeName *) NULL)
    {
	nn->efnn_node->efnode_flags |= EF_PORT;
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
 *	Larger value including the implicit ports is placed in the
 *	location of the pointer imp_max.
 *
 * ----------------------------------------------------------------------------
 */

int
EFGetPortMax(def, imp_max)
   Def *def;
   int *imp_max;
{
    EFNode *snode;
    EFNodeName *nodeName;
    int portmax, portorder;

    portmax = -1;
    if (imp_max) *imp_max = -1;

    for (snode = (EFNode *) def->def_firstn.efnode_next;
                snode != &def->def_firstn;
                snode = (EFNode *) snode->efnode_next)
    {
        if (imp_max && (snode->efnode_flags & EF_SUBS_PORT))
	{
	    nodeName = snode->efnode_name;
	    portorder = nodeName->efnn_port;
	    if (portorder > (*imp_max)) (*imp_max) = portorder;
	}
        else if (snode->efnode_flags & EF_PORT)
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

    he = HashFind(&def->def_nodes, name);
    nn = (EFNodeName *) HashGetValue(he);
    if (nn == (EFNodeName *) NULL)
    {
	/* Create node if it doesn't already exist */
	if (efWarn && !isSubsNode)
	    efReadError("Node %s doesn't exist so creating it\n", name);
	efBuildNode(def, isSubsNode, name, (double)0, 0, 0,
		(char *) NULL, (char **) NULL, 0);

	nn = (EFNodeName *) HashGetValue(he);
	if (isSubsNode)
	{
	    if (!EFHNIsGlob(nn->efnn_hier))
	    {
		/* This node is declared to be an implicit port */
		nn->efnn_node->efnode_flags |= EF_SUBS_PORT;
		nn->efnn_port = -1;
		def->def_flags |= DEF_SUBSNODES;
	    }
	    nn->efnn_node->efnode_flags |= (EF_DEVTERM | EF_SUBS_NODE);
	}
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
    newuse->use_next = def->def_uses;
    def->def_uses = newuse;

    /* Set the use identifier and array information */
    if ((cp = strchr(subUseId, '[')) == NULL)
    {
	newuse->use_id = StrDup((char **) NULL, subUseId);
	newuse->use_xlo = newuse->use_xhi = 0;
	newuse->use_ylo = newuse->use_yhi = 0;
	newuse->use_xsep = newuse->use_ysep = 0;
	return;
    }

    *cp = '\0';
    newuse->use_id = StrDup((char **) NULL, subUseId);
    *cp = '[';
    (void) sscanf(cp, "[%d:%d:%d][%d:%d:%d]",
		    &newuse->use_xlo, &newuse->use_xhi, &newuse->use_xsep,
		    &newuse->use_ylo, &newuse->use_yhi, &newuse->use_ysep);
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
		    + (efNumResistClasses - 1) * sizeof (PerimArea);

    conn = (Connection *) mallocMagic((unsigned)(size));

    if (efConnInitSubs(conn, nodeName1, nodeName2))
    {
	conn->conn_cap = (EFCapValue) deltaC;
	conn->conn_next = def->def_conns;
	for (n = 0; n < efNumResistClasses && ac > 1; n++, ac -= 2)
	{
	    conn->conn_pa[n].pa_area = atoi(*av++);
	    conn->conn_pa[n].pa_perim = atoi(*av++);
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
    while (*dstp++ = *cp++)
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
efNodeAddName(node, he, hn)
    EFNode *node;
    HashEntry *he;
    HierName *hn;
{
    EFNodeName *newnn;
    EFNodeName *oldnn;

    newnn = (EFNodeName *) mallocMagic((unsigned)(sizeof (EFNodeName)));
    newnn->efnn_node = node;
    newnn->efnn_hier = hn;
    newnn->efnn_port = -1;
    HashSetValue(he, (char *) newnn);

    /* Link in the new name */
    oldnn = node->efnode_name;
    if (oldnn == NULL || EFHNBest(newnn->efnn_hier, oldnn->efnn_hier))
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
 *	Return 0 if node1 has precedence, 1 if node2 has precedence
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

void
efNodeMerge(node1, node2)
    EFNode *node1, *node2;	/* Hierarchical nodes */
{
    EFNodeName *nn, *nnlast;
    EFAttr *ap;
    int n;

    /* Sanity check: ignore if same node */
    if (node1 == node2)
	return;

    if (efWatchNodes)
    {
	if (HashLookOnly(&efWatchTable, (char *) node1->efnode_name->efnn_hier)
	    || (node2->efnode_name
		&& HashLookOnly(&efWatchTable,
				(char *) node2->efnode_name->efnn_hier)))
	{
	    printf("\ncombine: %s\n",
		EFHNToStr(node1->efnode_name->efnn_hier));
	    printf("  with   %s\n\n", 
		node2->efnode_name
		    ? EFHNToStr(node2->efnode_name->efnn_hier)
		    : "(unnamed)");
	}
    }

    /* Sum capacitances, perimeters, areas */
    node1->efnode_cap += node2->efnode_cap;
    for (n = 0; n < efNumResistClasses; n++)
    {
	node1->efnode_pa[n].pa_area += node2->efnode_pa[n].pa_area;
	node1->efnode_pa[n].pa_perim += node2->efnode_pa[n].pa_perim;
    }

    /* Make all EFNodeNames point to node1 */
    if (node2->efnode_name)
    {
	for (nn = node2->efnode_name; nn; nn = nn->efnn_next)
	{
	    nnlast = nn;
	    nn->efnn_node = node1;
	}

	/* Concatenate list of EFNodeNames, taking into account precedence */
	if (EFHNBest(node2->efnode_name->efnn_hier,
		     node1->efnode_name->efnn_hier))
	{
	    /*
	     * New official name is that of node2.
	     * The new list of names is:
	     *	node2-names, node1-names
	     */
	    nnlast->efnn_next = node1->efnode_name;
	    node1->efnode_name = node2->efnode_name;

	    /*
	     * Choose the new location only if node2's location is a valid one,
	     * i.e, node2 wasn't created before it was mentioned.  This is mainly
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
	    if (node2->efnode_type > 0)
	    {
		node1->efnode_loc = node2->efnode_loc;
		node1->efnode_type = node2->efnode_type;

		if (node2->efnode_loc.r_ybot < node1->efnode_loc.r_ybot
		    || (node2->efnode_loc.r_ybot == node1->efnode_loc.r_ybot
		    && node2->efnode_loc.r_xbot < node1->efnode_loc.r_xbot))
		{
//		    node1->efnode_loc = node2->efnode_loc;
//		    node1->efnode_type = node2->efnode_type;
		}
	    }
	}
	else
	{
	    /*
	     * Keep old official name.
	     * The new list of names is:
	     *	node1-names[0], node2-names, node1-names[1-]
	     */
	    nnlast->efnn_next = node1->efnode_name->efnn_next;
	    node1->efnode_name->efnn_next = node2->efnode_name;
	}
    }

    /* Merge attribute lists */
    if (ap = node2->efnode_attrs)
    {
	while (ap->efa_next)
	    ap = ap->efa_next;
	ap->efa_next = node1->efnode_attrs;
	node1->efnode_attrs = ap;
	node2->efnode_attrs = (EFAttr *) NULL;	/* Sanity */
    }

    /* Unlink node2 from list for def */
    node2->efnode_prev->efnhdr_next = node2->efnode_next;
    node2->efnode_next->efnhdr_prev = node2->efnode_prev;

    /*
     * Only if both nodes were EF_DEVTERM do we keep EF_DEVTERM set
     * in the resultant node.
     */
    if ((node2->efnode_flags & EF_DEVTERM) == 0)
	node1->efnode_flags &= ~EF_DEVTERM;

    /*
     * If node2 has the EF_PORT flag set, then copy the port
     * record in the flags to node1.
     */
    if (node2->efnode_flags & EF_PORT)
	node1->efnode_flags |= EF_PORT;

    /*
     * If node2 has the EF_SUBS_NODE flag set, then copy the port
     * record in the flags to node1.
     */
    if (node2->efnode_flags & EF_SUBS_NODE)
	node1->efnode_flags |= EF_SUBS_NODE;

    /* Get rid of node2 */
    freeMagic((char *) node2);
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
    while (he = HashNext(table, &hs))
	if (nn = (EFNodeName *) HashGetValue(he))
	{
	    for (hn = nn->efnn_hier; hn; hn = hn->hn_parent)
		(void) HashFind(&efFreeHashTable, (char *) hn);
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * ----------------------------------------------------------------------------
 */

void
efFreeNodeList(head)
    EFNode *head;
{
    EFNode *node;
    EFAttr *ap;

    for (node = (EFNode *) head->efnode_next;
	    node != head;
	    node = (EFNode *) node->efnode_next)
    {
	for (ap = node->efnode_attrs; ap; ap = ap->efa_next)
	    freeMagic((char *) ap);
	freeMagic((char *) node);
    }
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
