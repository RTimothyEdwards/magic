/*
 * ExtHier.c --
 *
 * Circuit extraction.
 * Lower-level procedures common both to ordinary subtree extraction,
 * and to array extraction.
 * The procedures in this file are not re-entrant.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extract/ExtHier.c,v 1.3 2010/06/24 12:37:17 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "utils/styles.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"

/* Local data */

    /* Passed to search functions by extHierConnections */
ExtTree *extHierCumFlat;	/* Cum buffer */
ExtTree *extHierOneFlat;	/* Subtree being compared with extHierCumFlat */

    /* List of free cells around for use in yanking subtrees */
ExtTree *extHierFreeOneList = (ExtTree *) NULL;

    /* Appended to the name of each new CellDef created by extHierNewOne() */
int extHierOneNameSuffix = 0;

/* Forward declarations */
int extHierConnectFunc1();
int extHierConnectFunc2();
int extHierConnectFunc3();
Node *extHierNewNode();


/*----------------------------------------------*/
/* extHierSubstrate				*/
/*						*/
/* Find the substrate node of a child cell and	*/
/* make a connection between parent and child	*/
/* substrates.  If either of the substrate 	*/
/* nodes is already in the hash table, then the	*/
/* table will be updated as necessary.		*/
/*----------------------------------------------*/

void
extHierSubstrate(ha, use, x, y)
    HierExtractArg *ha; 	// Contains parent def and hash table
    CellUse *use;		// Child use
    int x, y;			// Array subscripts, or -1 if not an array
{
    NodeRegion *nodeList;
    HashTable *table = &ha->ha_connHash;
    HashEntry *he;
    NodeName *nn;
    Node *node1, *node2;
    char *name1, *name2, *childname;
    CellDef *def;

    NodeRegion *extFindNodes();

    /* Backwards compatibility with tech files that don't */
    /* define a substrate plane or substrate connections. */
    if (glob_subsnode == NULL) return;

    def = (CellDef *)ha->ha_parentUse->cu_def;

    /* Register the name of the parent's substrate */
    /* The parent def's substrate node is in glob_subsnode */

    name1 = extNodeName(glob_subsnode);
    he = HashFind(table, name1);
    nn = (NodeName *) HashGetValue(he);
    node1 = nn ? nn->nn_node : extHierNewNode(he);

    /* Find the child's substrate node */
    nodeList = extFindNodes(use->cu_def, (Rect *) NULL, TRUE);

    /* Make sure substrate labels are represented */
    ExtLabelRegions(use->cu_def, ExtCurStyle->exts_nodeConn, &nodeList,
			&TiPlaneRect);

    ExtResetTiles(use->cu_def, extUnInit);

    name2 = extNodeName(temp_subsnode);

    if (x >= 0 && y >= 0)
    {
	/* Process array information */
	childname = mallocMagic(strlen(name2) + strlen(use->cu_id) + 14);
	sprintf(childname, "%s[%d,%d]/%s", use->cu_id, y, x, name2);
    }
    else if (x >= 0 || y >= 0)
    {
	childname = mallocMagic(strlen(name2) + strlen(use->cu_id) + 9);
	sprintf(childname, "%s[%d]/%s", use->cu_id, ((x >= 0) ? x : y),
			name2);
    }
    else
    {
	childname = mallocMagic(strlen(name2) + strlen(use->cu_id) + 2);
	sprintf(childname, "%s/%s", use->cu_id, name2);
    }
    he = HashFind(table, childname);
    nn = (NodeName *) HashGetValue(he);
    node2 = nn ? nn->nn_node : extHierNewNode(he);

    freeMagic(childname);

    if (node1 != node2)
    {
       /*
        * Both sets of names will now point to node1.
        */
	for (nn = node2->node_names; nn->nn_next; nn = nn->nn_next)
		nn->nn_node = node1;
	nn->nn_node = node1;
	nn->nn_next = node1->node_names;
	node1->node_names = node2->node_names;
	freeMagic((char *) node2);
    }
    freeMagic(nodeList);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHierConnections --
 *
 * Process connections between the two ExtTrees 'oneFlat' and 'cumFlat'.
 * This consists of detecting overlaps or abutments between connecting
 * tiles (maybe on different planes), and recording the connection in the hash
 * table ha->ha_connHash.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds connections to ha->ha_connHash.
 *	Doesn't change resistance or capacitance of the connected
 *	nodes; that is the job of extHierAdjustments().
 *
 * ----------------------------------------------------------------------------
 */

void
extHierConnections(ha, cumFlat, oneFlat)
    HierExtractArg *ha;
    ExtTree *cumFlat, *oneFlat;
{
    int pNum;
    CellDef *sourceDef = oneFlat->et_use->cu_def;
    Label *lab;

    extHierCumFlat = cumFlat;
    extHierOneFlat = oneFlat;
    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
    {
	ha->hierPNum = pNum;
	(void) DBSrPaintArea((Tile *) NULL,
		sourceDef->cd_planes[pNum], &ha->ha_subArea,
		&DBAllButSpaceBits, extHierConnectFunc1, (ClientData) ha);
    }

    /* Look for sticky labels in the child cell that are not	*/
    /* connected to any geometry.				*/

    for (lab = sourceDef->cd_labels;  lab;  lab = lab->lab_next)
    {
	CellDef *cumDef = cumFlat->et_use->cu_def;
	Rect r = lab->lab_rect;
	TileTypeBitMask *connected = &DBConnectTbl[lab->lab_type];
	int i = DBPlane(lab->lab_type);

	ha->hierOneTile = (Tile *)lab;	/* Blatant hack recasting */
	ha->hierType = lab->lab_type;
	ha->hierPNumBelow = i;

	GEOCLIP(&r, &ha->ha_subArea);
	if (lab->lab_flags & LABEL_STICKY)
	    DBSrPaintArea((Tile *) NULL,
			cumFlat->et_use->cu_def->cd_planes[i], &r,
			connected, extHierConnectFunc3, (ClientData) ha);
    }
}

/*
 * extHierConnectFunc1 --
 *
 * Called for each tile 'oneTile' in the ExtTree 'oneFlat' above
 * that lies in the area ha->ha_subArea.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	None here, but see extHierConnectFunc2().
 */

int
extHierConnectFunc1(oneTile, ha)
    Tile *oneTile;	/* Comes from 'oneFlat' in extHierConnections */
    HierExtractArg *ha;	/* Extraction context */
{
    CellDef *cumDef = extHierCumFlat->et_use->cu_def;
    Rect r;
    TileTypeBitMask mask, *connected;
    TileType rtype;
    Label *lab, *newlab;
    int i;
    unsigned n;

    /*
     * Find all tiles that connect to 'srcTile', but in the
     * yank buffer cumDef.  Adjust connectivity for each tile found.
     * Widen the rectangle to detect connectivity by abutment.
     */
    ha->hierOneTile = oneTile;
    ha->hierType = TiGetTypeExact(oneTile);

    if (IsSplit(oneTile))
    {
	rtype = ha->hierType;
	ha->hierType = (rtype & TT_SIDE) ? SplitRightType(oneTile) :
		SplitLeftType(oneTile);
    }  

    connected = &(ExtCurStyle->exts_nodeConn[ha->hierType]);
    TITORECT(oneTile, &r);
    GEOCLIP(&r, &ha->ha_subArea);
    r.r_xbot--, r.r_ybot--, r.r_xtop++, r.r_ytop++;
    for (i = PL_TECHDEPBASE; i < DBNumPlanes; i++)
    {
	ha->hierPNumBelow = i;
	TTMaskAndMask3(&mask, connected, &DBPlaneTypes[i]);
	if (!TTMaskIsZero(&mask))
	{
	    if (IsSplit(oneTile))
		DBSrPaintNMArea((Tile *) NULL, cumDef->cd_planes[i],
			rtype, &r,
			((i == ha->hierPNum) ? &ExtCurStyle->exts_activeTypes
			: connected), extHierConnectFunc2, (ClientData) ha);
	    else
		DBSrPaintArea((Tile *) NULL, cumDef->cd_planes[i], &r,
			((i == ha->hierPNum) ? &ExtCurStyle->exts_activeTypes
			: connected), extHierConnectFunc2, (ClientData) ha);
	}
    }

    /* Where labels have been saved from the parent cell, look for any	*/
    /* that are inside the cell boundary and would connect to the tile.	*/
    /* This allows the extractor to catch "sticky" labels that are not	*/
    /* attached to a physical layer in the parent cell.			*/

    // NOTE by Tim, 9/10/2014:  This generates phantom nodes when the
    // labels are created by the "hard" node search;  I think this code
    // should be restricted to sticky labels only.  But not certain.
    // Definitely this causes problems in arrays, because the array node
    // name may refer to a range of array elements, and the generated
    // node only describes a single point.

    for (lab = cumDef->cd_labels;  lab;  lab = lab->lab_next)
	if (GEO_TOUCH(&r, &lab->lab_rect) && (lab->lab_flags & LABEL_STICKY))
	    if (TTMaskHasType(connected, lab->lab_type))
	    {
		HashTable *table = &ha->ha_connHash;
		HashEntry *he;
		NodeName *nn;
		Node *node1, *node2;
		char *name;

		/* Register the name, like is done in extHierConnectFunc2 */
		he = HashFind(table, lab->lab_text);
		nn = (NodeName *) HashGetValue(he);
		node1 = nn ? nn->nn_node : extHierNewNode(he);

		name = (*ha->ha_nodename)(ha->hierOneTile, ha->hierPNum,
			extHierOneFlat, ha, TRUE);
		he = HashFind(table, name);
		nn = (NodeName *) HashGetValue(he);
		node2 = nn ? nn->nn_node : extHierNewNode(he);

		if (node1 != node2)
		{
		    /*
		     * Both sets of names will now point to node1.
		     * We don't need to update node_cap since it
		     * hasn't been computed yet.
		     */
		    for (nn = node2->node_names; nn->nn_next; nn = nn->nn_next)
			nn->nn_node = node1;
		    nn->nn_node = node1;
		    nn->nn_next = node1->node_names;
		    node1->node_names = node2->node_names;
		    freeMagic((char *) node2);
		}

#if 0
		/* Copy this label to the parent def with a	*/
		/* special flag, so we can output it as a node	*/
	 	/* and then delete it.  Don't duplicate labels	*/
		/* that are already in the parent.		*/

		for (newlab = ha->ha_parentUse->cu_def->cd_labels;
				newlab; newlab = newlab->lab_next)
		    if (!strcmp(newlab->lab_text, lab->lab_text))
			break;

		if (newlab == NULL)
		{
		    n = sizeof(Label) + strlen(lab->lab_text)
				- sizeof lab->lab_text + 1;
		    newlab = (Label *)mallocMagic((unsigned)n);
		    bcopy((char *)lab, (char *)newlab, (int)n);
		
		    newlab->lab_next = ha->ha_parentUse->cu_def->cd_labels;
		    ha->ha_parentUse->cu_def->cd_labels = newlab;
		}
#endif
	    }

    return (0);
}

/*
 * extHierConnectFunc2 --
 *
 * Called once for each tile 'cum' in extHierCumFlat->et_use->cu_def
 * on the same plane as ha->hierOneTile that also overlaps or abuts
 * the intersection of ha->hierOneTile with ha->ha_subArea, and for tiles
 * in other planes that may connect.
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Makes a connection between the nodes of the two tiles
 *	if the types of ha->hierOneTile and 'cum' connect.
 *	Otherwise, if the tiles actually overlap (as opposed
 *	to merely abut), mark it with feedback as an error.
 */

int
extHierConnectFunc2(cum, ha)
    Tile *cum;		/* Comes from extHierCumFlat->et_use->cu_def */
    HierExtractArg *ha;	/* Extraction context */
{
    HashTable *table = &ha->ha_connHash;
    Node *node1, *node2;
    TileType ttype;
    HashEntry *he;
    NodeName *nn;
    char *name;
    Rect r;

    /* Compute the overlap area */
    r.r_xbot = MAX(LEFT(ha->hierOneTile), LEFT(cum));
    r.r_xtop = MIN(RIGHT(ha->hierOneTile), RIGHT(cum));
    r.r_ybot = MAX(BOTTOM(ha->hierOneTile), BOTTOM(cum));
    r.r_ytop = MIN(TOP(ha->hierOneTile), TOP(cum));

    /* If the tiles don't even touch, they don't connect */
    if (r.r_xtop < r.r_xbot || r.r_ytop < r.r_ybot
		|| (r.r_xtop == r.r_xbot && r.r_ytop == r.r_ybot))
	return (0);

    /*
     * Only make a connection if the types of 'ha->hierOneTile' and 'cum'
     * connect.  If they overlap and don't connect, it is an error.
     * If they do connect, mark their nodes as connected.
     */

    ttype = TiGetTypeExact(cum);

    if (IsSplit(cum))
	ttype = (ttype & TT_SIDE) ? SplitRightType(cum) : SplitLeftType(cum);

    if (extConnectsTo(ha->hierType, ttype, ExtCurStyle->exts_nodeConn))
    {
	name = (*ha->ha_nodename)(cum, ha->hierPNumBelow, extHierCumFlat, ha, TRUE);
	he = HashFind(table, name);
	nn = (NodeName *) HashGetValue(he);
	node1 = nn ? nn->nn_node : extHierNewNode(he);

	name = (*ha->ha_nodename)(ha->hierOneTile, ha->hierPNum, extHierOneFlat,
		ha, TRUE);
	he = HashFind(table, name);
	nn = (NodeName *) HashGetValue(he);
	node2 = nn ? nn->nn_node : extHierNewNode(he);

	if (node1 != node2)
	{
	    /*
	     * Both sets of names will now point to node1.
	     * We don't need to update node_cap since it
	     * hasn't been computed yet.
	     */
	    for (nn = node2->node_names; nn->nn_next; nn = nn->nn_next)
		nn->nn_node = node1;
	    nn->nn_node = node1;
	    nn->nn_next = node1->node_names;
	    node1->node_names = node2->node_names;
	    freeMagic((char *) node2);
	}
    }
    else if (r.r_xtop > r.r_xbot && r.r_ytop > r.r_ybot)
    {
	extNumFatal++;
	if (!DebugIsSet(extDebugID, extDebNoFeedback))
	    DBWFeedbackAdd(&r, "Illegal overlap (types do not connect)",
		ha->ha_parentUse->cu_def, 1, STYLE_MEDIUMHIGHLIGHTS);
    }

    return (0);
}

/*
 * extHierConnectFunc3 --
 *
 * Called once for each tile 'cum' in extHierCumFlat->et_use->cu_def
 * Similar to extHierConnectFunc2, but is called for a label in the
 * parent cell that does not necessarily have associated geometry.
 * Value passed in ha_oneTile is the label (recast for convenience;
 * need to use a union type in HierExtractArg).
 */

int
extHierConnectFunc3(cum, ha)
    Tile *cum;		/* Comes from extHierCumFlat->et_use->cu_def */
    HierExtractArg *ha;	/* Extraction context */
{
    HashTable *table = &ha->ha_connHash;
    Node *node1, *node2;
    TileType ttype;
    HashEntry *he;
    NodeName *nn;
    char *name;
    Rect r;
    Label *lab = (Label *)(ha->hierOneTile);	/* Lazy recasting */	

    /* Compute the overlap area */
    r.r_xbot = MAX(lab->lab_rect.r_xbot, LEFT(cum));
    r.r_xtop = MIN(lab->lab_rect.r_xtop, RIGHT(cum));
    r.r_ybot = MAX(lab->lab_rect.r_ybot, BOTTOM(cum));
    r.r_ytop = MIN(lab->lab_rect.r_ytop, TOP(cum));

    /* If the tiles don't even touch, they don't connect */
    if (r.r_xtop < r.r_xbot || r.r_ytop < r.r_ybot)
	return (0);

    /*
     * Only make a connection if the types of 'ha->hierOneTile' and 'cum'
     * connect.  If they overlap and don't connect, it is an error.
     * If they do connect, mark their nodes as connected.
     */

    ttype = TiGetTypeExact(cum);

    if (IsSplit(cum))
	ttype = (ttype & TT_SIDE) ? SplitRightType(cum) : SplitLeftType(cum);

    if (extConnectsTo(ha->hierType, ttype, ExtCurStyle->exts_nodeConn))
    {
	name = (*ha->ha_nodename)(cum, ha->hierPNumBelow, extHierCumFlat, ha, TRUE);
	he = HashFind(table, name);
	nn = (NodeName *) HashGetValue(he);
	node1 = nn ? nn->nn_node : extHierNewNode(he);

	name = lab->lab_text;
	he = HashFind(table, name);
	nn = (NodeName *) HashGetValue(he);
	node2 = nn ? nn->nn_node : extHierNewNode(he);

	if (node1 != node2)
	{
	    /*
	     * Both sets of names will now point to node1.
	     * We don't need to update node_cap since it
	     * hasn't been computed yet.
	     */
	    for (nn = node2->node_names; nn->nn_next; nn = nn->nn_next)
		nn->nn_node = node1;
	    nn->nn_node = node1;
	    nn->nn_next = node1->node_names;
	    node1->node_names = node2->node_names;
	    freeMagic((char *) node2);
	}
    }
    else if (r.r_xtop > r.r_xbot && r.r_ytop > r.r_ybot)
    {
	extNumFatal++;
	if (!DebugIsSet(extDebugID, extDebNoFeedback))
	    DBWFeedbackAdd(&r, "Illegal overlap (types do not connect)",
		ha->ha_parentUse->cu_def, 1, STYLE_MEDIUMHIGHLIGHTS);
    }

    return (0);
}
/*
 * ----------------------------------------------------------------------------
 *
 * extHierAdjustments --
 *
 * Process adjustments to substrate capacitance, coupling capacitance,
 * node perimeter, and node area between the subtree 'oneFlat' and the
 * cumulative yank buffer 'cumFlat'.  The subtree 'lookFlat' is used
 * for looking up node names when handling capacitance/perimeter/area
 * adjustment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates capacitance in the table cumFlat->et_coupleHash.
 *	Updates capacitance, perimeter, and area recorded in the
 *	nodes of 'cumFlat'.
 *
 * Algorithm:
 *	For each capacitor recorded in oneFlat->et_coupleHash, find
 *	the corresponding nodes in 'cumFlat' and subtract the
 *	capacitance from the entry indexed by these nodes in the
 *	table cumFlat->et_coupleHash.
 *
 *	For each node in oneFlat->et_nodes, find the corresponding
 *	node in 'lookFlat'.  Look for the Node with this name in
 *	the table ha->ha_connHash, and subtract the oneFlat node's
 *	capacitance, perimeter, and area from it.  If no Node is
 *	found in this table, don't do anything since the oneFlat
 *	node must not participate in any connections.
 *
 *	The node in 'cumFlat' corresponding to one in 'oneFlat'
 *	is the one containing some point in 'oneFlat', since 'oneFlat'
 *	is a strict subset of 'cumFlat'.
 *
 * ----------------------------------------------------------------------------
 */

void
extHierAdjustments(ha, cumFlat, oneFlat, lookFlat)
    HierExtractArg *ha;
    ExtTree *cumFlat, *oneFlat, *lookFlat;
{
    HashEntry *he, *heCum;
    int n;
    CoupleKey *ckpOne, ckCum;
    NodeRegion *np;
    HashSearch hs;
    NodeName *nn;
    Tile *tp;
    char *name;

    /* Update all coupling capacitors */
    if (ExtOptions & EXT_DOCOUPLING)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&oneFlat->et_coupleHash, &hs))
	{
	    ckpOne = ((CoupleKey *) he->h_key.h_words);

	    /* Find nodes in cumFlat->et_coupleHash */
	    NODETONODE(ckpOne->ck_1, cumFlat, ckCum.ck_1);
	    NODETONODE(ckpOne->ck_2, cumFlat, ckCum.ck_2);
	    if (ckCum.ck_1 == NULL || ckCum.ck_2 == NULL) continue;

	    /* Skip if the same; reverse to make smaller node pointer first */
	    if (ckCum.ck_1 == ckCum.ck_2) continue;
	    if (ckCum.ck_2 < ckCum.ck_1)
		np = ckCum.ck_1, ckCum.ck_1 = ckCum.ck_2, ckCum.ck_2 = np;

	    /* Update the capacitor record in cumFlat->et_coupleHash */
	    heCum = HashFind(&cumFlat->et_coupleHash, (char *) &ckCum);
	    extSetCapValue(heCum, extGetCapValue(heCum) - extGetCapValue(he));
	}
    }

    /*
     * Update all node values.
     * Find the corresponding tile in the ExtTree lookFlat, then look
     * for its name.  If this name appear in the connection hash table,
     * update the capacitance, perimeter, and area stored there; otherwise
     * ignore it.
     *
     * The FALSE argument to (*ha->ha_nodename)() means that we don't bother
     * looking for node names the hard way; if we didn't already have a valid
     * node name then it couldn't appear in the table ha->ha_connHash in the
     * first place.
     */
    for (np = oneFlat->et_nodes; np; np = np->nreg_next)
    {
	/* Ignore orphaned nodes (non-Manhattan shards outside the clip box) */
	if (np->nreg_pnum == DBNumPlanes) continue;

	tp = extNodeToTile(np, lookFlat);

	/* Ignore regions that do not participate in extraction */
	if (!extHasRegion(tp, extUnInit)) continue;

	/* Ignore substrate nodes (failsafe:  should not happen) */
	if (TiGetTypeExact(tp) == TT_SPACE) continue;

	if (tp	&& (name = (*ha->ha_nodename)(tp, np->nreg_pnum, lookFlat, ha, FALSE))
		&& (he = HashLookOnly(&ha->ha_connHash, name))
		&& (nn = (NodeName *) HashGetValue(he)))
	{
	    /* Adjust the capacitance and resistance */
	    nn->nn_node->node_cap -= np->nreg_cap;
	    for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
	    {
		nn->nn_node->node_pa[n].pa_perim -= np->nreg_pa[n].pa_perim;
		nn->nn_node->node_pa[n].pa_area -= np->nreg_pa[n].pa_area;
	    }
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extOutputConns --
 *
 * Dump the contents of the hash table 'table' of connectivity and
 * node R, C adjustments to the output file outf.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Outputs a number of "merge" records to the file 'outf'.
 *
 * ----------------------------------------------------------------------------
 */

void
extOutputConns(table, outf)
    HashTable *table;
    FILE *outf;
{
    CapValue c;  /* cap value */
    NodeName *nn, *nnext;
    Node *node;
    int n;
    NodeName *nfirst;
    HashSearch hs;
    HashEntry *he;

    HashStartSearch(&hs);
    while (he = HashNext(table, &hs))
    {
	nfirst = (NodeName *) HashGetValue(he);

	/*
	 * If nfirst->nn_node == NULL, the name for this hash entry
	 * had been output previously as a member of the merge list
	 * for a node appearing earlier in the table.  If so, we need
	 * only free the NodeName without any further processing.
	 */
	if (node = nfirst->nn_node)
	{
	    /*
	     * If there are N names for this node, output N-1 merge lines.
	     * Only the first merge line will contain the C, perimeter,
	     * and area updates.
	     */
	    /* Note 3/1/2017:  Cap value no longer used */
	    c = (node->node_cap) / ExtCurStyle->exts_capScale;
	    nn = node->node_names;
	    if (nnext = nn->nn_next)
	    {
		/* First merge */
		fprintf(outf, "merge \"%s\" \"%s\" %lg",
				nn->nn_name, nnext->nn_name, c);
		for (n = 0; n < ExtCurStyle->exts_numResistClasses; n++)
		    fprintf(outf, " %d %d",
				node->node_pa[n].pa_area,
				node->node_pa[n].pa_perim);
		fprintf(outf, "\n");

		nn->nn_node = (Node *) NULL;		/* Processed */

		/* Subsequent merges */
		for (nn = nnext; nnext = nn->nn_next; nn = nnext)
		{
		    fprintf(outf, "merge \"%s\" \"%s\"\n",
				    nn->nn_name, nnext->nn_name);
		    nn->nn_node = (Node *) NULL;	/* Processed */
		}
	    }
	    nn->nn_node = (Node *) NULL;
	    freeMagic((char *) node);
	}
	freeMagic((char *) nfirst);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHierNewNode --
 *
 * Create a new NodeName and Node to go with the HashEntry supplied.
 * The NodeName will point to the new Node, which will point back to the
 * NodeName.
 *
 * Results:
 *	Returns a pointer to the newly created Node.
 *
 * Side effects:
 *	Allocates memory.
 *	Sets (via HashSetValue) the value of HashEntry 'he' to the
 *	newly created NodeName.
 *
 * ----------------------------------------------------------------------------
 */

Node *
extHierNewNode(he)
    HashEntry *he;
{
    int n, nclasses;
    NodeName *nn;
    Node *node;

    nclasses = ExtCurStyle->exts_numResistClasses;
    n = (nclasses - 1) * sizeof (PerimArea) + sizeof (Node);
    nn = (NodeName *) mallocMagic((unsigned) (sizeof (NodeName)));
    node = (Node *) mallocMagic((unsigned) n);

    nn->nn_node = node;
    nn->nn_next = (NodeName *) NULL;
    nn->nn_name = he->h_key.h_name;
    node->node_names = nn;
    node->node_cap = (CapValue) 0;
    for (n = 0; n < nclasses; n++)
	node->node_pa[n].pa_perim = node->node_pa[n].pa_area = 0;
    HashSetValue(he, (char *) nn);

    return (node);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHierLabFirst --
 * extHierLabEach --
 *
 * Filter functions passed to ExtFindRegions when tracing out labelled
 * regions as part of a hierarchical circuit extraction.
 *
 * Results:
 *	extHierLabFirst returns a pointer to a new LabRegion.
 *	extHierLabEach returns 0 always.
 *
 * Side effects:
 *	Memory is allocated by extHierLabFirst(); it conses the newly
 *	allocated region onto the front of the existing region list.
 *	The node-naming info (reg_ll, reg_pnum) is updated by
 *	extHierLabEach().
 *
 * ----------------------------------------------------------------------------
 */

    /*ARGSUSED*/
Region *
extHierLabFirst(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    LabRegion *new;

    new = (LabRegion *) mallocMagic((unsigned) (sizeof (LabRegion)));
    new->lreg_next = (LabRegion *) NULL;
    new->lreg_labels = (LabelList *) NULL;
    new->lreg_pnum = DBNumPlanes;

    /* Prepend it to the region list */
    new->lreg_next = (LabRegion *) arg->fra_region;
    arg->fra_region = (Region *) new;

    return ((Region *) new);
}

    /*ARGSUSED*/
int
extHierLabEach(tile, pNum, arg)
    Tile *tile;
    int pNum;
    FindRegion *arg;
{
    LabRegion *reg;

    reg = (LabRegion *) arg->fra_region;
    extSetNodeNum(reg, pNum, tile);
    return (0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHierNewOne --
 *
 * Allocate a new ExtTree for use in hierarchical extraction.
 * This ExtTree will be used to hold an entire flattened subtree.
 * We try to return one from our free list if one exists; if none
 * are left, we create a new CellDef and CellUse and allocate a
 * new ExtTree.  The new CellDef has a name of the form __EXTTREEn__,
 * where 'n' is a small integer.
 *
 * The HashTable et_coupleHash will be initialized but empty.
 * The node list et_nodes, the next pointer et_next, and the CellDef
 * pointer et_lookNames will all be set to NULL.
 *
 * Results:
 *	Returns a pointer to a new ExtTree.
 *
 * Side effects:
 *	See above.
 *
 * ----------------------------------------------------------------------------
 */

ExtTree *
extHierNewOne()
{
    char defname[128];
    CellDef *dummy;
    ExtTree *et;

    if (extHierFreeOneList)
    {
	et = extHierFreeOneList;
	extHierFreeOneList = et->et_next;
    }
    else
    {
	et = (ExtTree *) mallocMagic((unsigned)(sizeof (ExtTree)));
	(void) sprintf(defname, "__EXTTREE%d__", extHierOneNameSuffix++);
	DBNewYank(defname, &et->et_use, &dummy);
    }

    et->et_next = (ExtTree *) NULL;
    et->et_lookNames = (CellDef *) NULL;
    et->et_nodes = (NodeRegion *) NULL;
    if (ExtOptions & EXT_DOCOUPLING)
	HashInit(&et->et_coupleHash, 32, HashSize(sizeof (CoupleKey)));
    return (et);
}

/*
 * ----------------------------------------------------------------------------
 *
 * extHierFreeOne --
 *
 * Return an ExtTree allocated via extHierNewOne() above to the
 * free list.  Frees the HashTable et->et_coupleHash, any NodeRegions
 * on the list et->et_nodes, any labels on the label list and any
 * paint in the cell et->et_use->cu_def.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See above.
 *	The caller should NOT use et->et_next after this procedure
 *	has returned.
 *
 * ----------------------------------------------------------------------------
 */

void
extHierFreeOne(et)
    ExtTree *et;
{
    if (ExtOptions & EXT_DOCOUPLING)
	extCapHashKill(&et->et_coupleHash);
    if (et->et_nodes) ExtFreeLabRegions((LabRegion *) et->et_nodes);
    extHierFreeLabels(et->et_use->cu_def);
    DBCellClearDef(et->et_use->cu_def);

    et->et_next = extHierFreeOneList;
    extHierFreeOneList = et;
}
