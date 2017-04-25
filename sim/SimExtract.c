/*
 * SimExtract.c
 *
 *	This file provides routines to extract a single node name from the
 *	Magic circuit.  Some of this code is based on the Magic extract
 *	module.
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
 * of California.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "debug/debug.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "utils/signals.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/styles.h"
#include "utils/stack.h"
#include "sim/sim.h"


/* When performing node name extraction, we mark all tiles of a node
 * as we walk through the database.  Since nodes can span multiple cells,
 * we need to keep a list off all cell definitions which we marked during
 * the search so we can erase these marks when we are finished.
 */
typedef struct def_list_elt
{
    CellDef *dl_def;
    struct def_list_elt *dl_next;
} DefListElt;

/* When the node name is extracted, all the labels associated with the
 * name are contained in a NodeRegion data structure.  We save all the
 * node regions created during the name search, freeing them when we are
 * finished.
 */


static DefListElt *DefList = (DefListElt *) NULL;
			/* list of cell defs used in the node name search */

static NodeRegion *NodeRegList = (NodeRegion *) NULL;
			/* list of the nodes found in the current selection */

extern Stack *extNodeStack;
			/* stack used to process node tiles */

static ExtStyle *simExtStyle = NULL;

    /*
     * Mask with a bit set for every type of transistor.
     */
TileTypeBitMask	SimTransMask;
    /*
     * Mask indicating which tile types may form a transistor
     * terminal (source/drain).
     */
TileTypeBitMask	SimSDMask;
    /*
     * A mask indicating which tile types may be the active part
     * of a transistor.  if type 't' is a possible transistor terminal,
     * then SimFetMask[t] indicates which tile types to search for the
     * 'gate' part.
     */
TileTypeBitMask	SimFetMask[TT_MAXTYPES];
    /*
     * Mask of planes for which SimFetMask is non-zero.
     */
PlaneMask	SimFetPlanes;


/*
 *----------------------------------------------------------------
 * SimAddDefList --
 *
 * 	This procedure adds a cell definition to the DefList.  A cell
 * 	definition is added only if is not already in the list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cell may be added to the list.
 *
 *----------------------------------------------------------------
 */
 
void
SimAddDefList(newdef)
    CellDef *newdef;
{
    DefListElt *d;

    /* check to see if the cell def is already in our list */

    for (d = DefList; d != (DefListElt *) NULL; d = d->dl_next) {
	if (d->dl_def == newdef) {	
	    return;
	}
    }

    /* add the cell def to the list */

    if (DefList == (DefListElt *) NULL) {
	DefList = (DefListElt *) mallocMagic((unsigned) (sizeof(DefListElt)));
	DefList->dl_def = newdef;
	DefList->dl_next = (DefListElt *) NULL;
	return;
    }
    else {
	d = (DefListElt *) mallocMagic((unsigned) (sizeof(DefListElt)));
	d->dl_next = DefList;
	d->dl_def = newdef;
	DefList= d;
    }
}

/*
 *----------------------------------------------------------------
 * SimInitDefList
 *
 * 	This procedure initializes the cell definition list.  Any cell
 *	definitions in the list have their tiles erased of the marks
 *	we left during the node name search, and the space in the list
 *	is freed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cell definitions' tiles' marks we left are erased and the
 *	list is set to NULL.
 *
 *----------------------------------------------------------------
 */

void
SimInitDefList()
{
    DefListElt *p, *q;
     
    p = q = DefList;
    while (p != (DefListElt *) NULL) {
	q = p;
	p = p->dl_next;
	ExtResetTiles(q->dl_def, extUnInit);
	freeMagic(q);
    }
    DefList = (DefListElt *) NULL;
}


/*
 *----------------------------------------------------------------
 * SimAddNodeList
 *
 *      This procedure prepends a node region to the node region list.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The region next pointer is updated.
 *
 *----------------------------------------------------------------
 */

void
SimAddNodeList(newnode)
    NodeRegion *newnode;
{
    if( NodeRegList != (NodeRegion *) NULL )
	newnode->nreg_next = NodeRegList;
    NodeRegList = newnode;
}



/*
 *----------------------------------------------------------------
 * SimFreeNodeRegs
 *
 * 	This procedure frees the label regions stored in the list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The list is set to NULL.
 *
 *----------------------------------------------------------------
 */

void
SimFreeNodeRegs()
{
    NodeRegion *p, *q;
     
    if( NodeRegList != (NodeRegion *) NULL )		/* sanity */
	ExtFreeLabRegions((LabRegion *) NodeRegList );

     NodeRegList = (NodeRegion *) NULL;
}


/*
 *----------------------------------------------------------------
 * SimInitConnTables --
 *
 * Initialize the connectivity tables for finding transistors connected
 * to the region being searched.
 *----------------------------------------------------------------
 */
int SimInitConnTables()
{
    int  i, t, sd, p;

    SimTransMask = ExtCurStyle->exts_transMask;

    TTMaskZero( &SimSDMask );
    for( t = TT_TECHDEPBASE; t < DBNumTypes; t++ )
    {
	for (i = 0; !TTMaskHasType(&ExtCurStyle->exts_transSDTypes[t][i],
			TT_SPACE); i++)
	{
	     TTMaskSetMask( &SimSDMask, &ExtCurStyle->exts_transSDTypes[t][i] );
	     TTMaskZero( &SimFetMask[t] );
	}
    }

    SimFetPlanes = 0;
    for (t = TT_TECHDEPBASE; t < DBNumTypes; t++)
    {
	if (TTMaskHasType(&SimTransMask, t))
	{
	    for (sd = TT_TECHDEPBASE; sd < DBNumTypes; sd++)
	    {
		for (i = 0; !TTMaskHasType(&ExtCurStyle->exts_transSDTypes[t][i],
				TT_SPACE); i++)
		{
		    if (TTMaskHasType(&ExtCurStyle->exts_transSDTypes[t][i], sd))
		    {
			TTMaskSetType(&SimFetMask[sd], t);
			SimFetPlanes |= PlaneNumToMaskBit(DBPlane(t));
		    }
		}
	    }
	}
    }
    simExtStyle = ExtCurStyle;
    return 0;
}

#define	IsTransGate( T )	( TTMaskHasType( &SimTransMask, T ) )
#define	IsTransTerm( T )	( TTMaskHasType( &SimSDMask, T ) )


typedef struct
{
    NodeRegion	*region;	/* Node to which this terminal is connected */
    int		pnum;		/* Lowest numbered plane in this region */
    Point	pos;		/* Lower-leftmost point for this node  */
} TransTerm;


typedef struct
{
    LabRegion	*t_dummy;	/* UNUSED */
    int		t_pnum;		/* Lowest numbered plane in this region */
    int		t_type;		/* Type of tile that contains lreg_ll */
    Point	t_ll;		/* Lower-leftmost point of 'gate' region */
    int		t_nterm;	/* number of terminals (at most 2) */
    bool	t_do_terms;	/* Set if we should collect terminals */
    TransTerm	t_term[10];	/* transistor 'source/drain' terminal(s) */
} SimTrans;


static	Tile		*gateTile;	/* Set to point to a transistor tile
					 * whose gate is connected to the
					 * node being searched
					 */
static	Tile		*sdTile;	/* Set to point to a transistor tile
					 * whose source/drain is connected
					 * to the node being searched
					 */
static	SimTrans	transistor;	/* Transistor being extracted */


typedef enum { ND_REGION, ND_NAME } RegOrName;


typedef struct			/* return value from SimFindOneNode */
{
    RegOrName	nd_what;	/* ND_REGION, => region, ND_NAME => nd_name */
    NodeRegion	*nd_region;	/* The node region extracted */
    char	*nd_name;	/* The 'final' node name */
} NodeSpec;


/*
 *----------------------------------------------------------------
 * SimTxtorLabel --
 *
 * Return a string that identifies a node as a function of a transistor
 * position.
 *
 *----------------------------------------------------------------
 */
char *SimTxtorLabel( nterm, tm, trans )
    int		nterm;
    Transform	*tm;
    SimTrans	*trans;
{
    static char	name[30];
    Rect	r1, r2;

    r1.r_ll = trans->t_ll;
    r1.r_xtop = r1.r_xbot + 1;
    r1.r_ytop = r1.r_ybot + 1;
    GeoTransRect( tm, &r1, &r2 );
    if( nterm > 1 )
	nterm = 1;
    sprintf( name, "@=%c%d,%d", "gsd"[nterm+1], r2.r_xbot, r2.r_ybot );

    return( name );
}

int SimSDTransFunc( tile, ptile )
    Tile  *tile;
    Tile  **ptile;
{
    *ptile = tile;
    return( 1 );
}


int SimTransTerms( bp, trans )
    Boundary	*bp;
    SimTrans	*trans;
{
    TransTerm	*term;
    Tile	*tile = bp->b_outside;
    TileType	type;
    NodeRegion	*reg = (NodeRegion *) tile->ti_client;
    int		pNum;
    int		i;

    if (IsSplit(tile)) {
	switch(bp->b_direction) {
	    case BD_LEFT:
		type = TiGetRightType(tile);
		break;
	    case BD_RIGHT:
		type = TiGetLeftType(tile);
		break;
	    case BD_TOP:
		type = TiGetBottomType(tile);
		break;
	    case BD_BOTTOM:
		type = TiGetTopType(tile);
		break;
	}
    }
    else
	type = TiGetTypeExact(tile);

    pNum = DBPlane(type);

    for( i = 0; i < trans->t_nterm; i++ )
    {
	term = &trans->t_term[i];
	if( term->region == reg )
	{
	    if( pNum < term->pnum )
	    {
		term->pnum = pNum;
		term->pos = tile->ti_ll;
	    }
	    else if( pNum == term->pnum )
	    {
		if( LEFT(tile) < term->pos.p_x )
		    term->pos = tile->ti_ll;
		else if( LEFT(tile) == term->pos.p_x && 
		  BOTTOM(tile) < term->pos.p_y )
		    term->pos.p_y = BOTTOM(tile);
	    }
	    return( 0 );
	}
    }

    term = &trans->t_term[ trans->t_nterm++ ];
    term->region = reg;
    term->pnum = pNum;
    term->pos = tile->ti_ll;
    return( 0 );
}


int SimTermNum( trans, reg )
    SimTrans	*trans;
    NodeRegion	*reg;
{
    int		i, changed;
    TransTerm	*p1, *p2, tmp;

    do
    {
	changed = 0;
	for( i = 0; i < trans->t_nterm-1; i++ )
	{
	    p1 = &(trans->t_term[i]);
	    p2 = &(trans->t_term[i+1]);
	    if( p2->pnum > p1->pnum )
		continue;
	    else if( p2->pnum == p1->pnum )
	    {
		if( p2->pos.p_x > p1->pos.p_x )
		    continue;
		else if( p2->pos.p_x == p1->pos.p_x &&
		  p2->pos.p_y > p1->pos.p_y )
		    continue;
	    }
	    changed = 1;
	    tmp = *p1;
	    *p1 = *p2;
	    *p2 = tmp;
	}
     }
     while( changed );

    for( i = 0; i < trans->t_nterm; i++ )
    {
	if( trans->t_term[i].region == reg )
	    return( i );
    }
	/* should never get here */
    return( -1 );
}


int
SimTransistorTile(tile, pNum, arg)
    Tile	*tile;
    int		pNum;
    FindRegion	*arg;
{
    int i;
    TileType t;

    extSetNodeNum((LabRegion *)&transistor, pNum, tile);
    if (transistor.t_do_terms)
    {
	t = TiGetType(tile);
	for (i = 0; !TTMaskHasType(&ExtCurStyle->exts_transSDTypes[t][i],
			TT_SPACE); i++)
	    extEnumTilePerim(tile, ExtCurStyle->exts_transSDTypes[t][i],
			SimTransTerms, (ClientData) &transistor );
    }

    return (0);
}


int SimFindTxtor( tile, pNum, arg )
  Tile		*tile;
  int		pNum;
  FindRegion	*arg;
{
    TileType	type;

    extSetNodeNum( (LabRegion *) arg->fra_region, pNum, tile );

    if( ! SimUseCoords )	/* keep searching, forget transistors */
	return( 0 );

    type = TiGetType( tile );

    if( IsTransGate( type ) )
    {
	gateTile = tile;	/* found a transistor gate, stop searching */
	return( 1 );
    }
    else if( IsTransTerm( type ) && sdTile == (Tile *) NULL )
    {
	Rect  area;

	TITORECT( tile, &area );
	GEO_EXPAND( &area, 1, &area );
	for( pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++ )
	    if( PlaneMaskHasPlane( SimFetPlanes, pNum ) )
	    {
		if( DBSrPaintArea((Tile *) NULL,
		  arg->fra_def->cd_planes[pNum], &area, &SimFetMask[type],
		  SimSDTransFunc, (ClientData) &sdTile ) )
		    break;
	    }
    }
    return( 0 );
}


/*
 *----------------------------------------------------------------
 * SimFindOneNode
 *
 *	This procedure returns the node region for a tile which lies
 *	in the node.
 *
 * Results:
 *	A pointer to the node's node region data structure is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------
 */

NodeSpec *
SimFindOneNode( sx, tile )
    SearchContext	*sx;
    Tile		*tile;
{
    CellDef		*def = sx->scx_use->cu_def;
    NodeRegion		*reg;
    FindRegion		arg;
    TileType		type, loctype;
    static NodeSpec	ret;

	/* Allocate a new node */
    reg = (NodeRegion *) mallocMagic((unsigned) (sizeof(NodeRegion) ));
    reg->nreg_labels = (LabelList *) NULL;
    reg->nreg_cap = 0;
    reg->nreg_resist = 0;
    reg->nreg_pnum = DBNumPlanes;
    reg->nreg_next = (NodeRegion *) NULL;

    gateTile = sdTile = (Tile *) NULL;

	/* Find all connected paint in this cell */
    arg.fra_connectsTo = ExtCurStyle->exts_nodeConn;
    arg.fra_def = def;

    if (IsSplit(tile))
	type = SplitSide(tile) ? TiGetRightType(tile) : TiGetLeftType(tile);
    else
	type = TiGetTypeExact(tile);

    arg.fra_pNum = DBPlane(type);
    arg.fra_uninit = (ClientData) extUnInit;
    arg.fra_region = (Region *) reg;
    arg.fra_each = SimFindTxtor;
    (void) ExtFindNeighbors( tile, arg.fra_pNum, &arg );

    if( gateTile != (Tile *) NULL )
    {
	    /* Determine the transistor position (leftmost-lowest tile) */
	transistor.t_pnum = DBNumPlanes;
	transistor.t_do_terms = FALSE;

	gateTile->ti_client = (ClientData) extUnInit;
	arg.fra_connectsTo = &SimTransMask;

	if (IsSplit(tile))
	    loctype = SplitSide(gateTile) ? TiGetRightType(gateTile)
			: TiGetLeftType(gateTile);
	else
	    loctype = TiGetTypeExact(gateTile);

	arg.fra_pNum = DBPlane(loctype);
	arg.fra_uninit = (ClientData) extUnInit;
	arg.fra_region = (Region *) reg;
	arg.fra_each = SimTransistorTile;
	(void) ExtFindNeighbors( gateTile, arg.fra_pNum, &arg );

	    /* Unmark current region since not all paint was traced */
	arg.fra_connectsTo = ExtCurStyle->exts_nodeConn;
	arg.fra_pNum = DBPlane(type);
	arg.fra_uninit = (ClientData) reg;
	arg.fra_region = (Region *) extUnInit;
	arg.fra_each = (int (*)()) NULL;
	(void) ExtFindNeighbors( tile, arg.fra_pNum, &arg );

	freeMagic( reg );

	ret.nd_name = SimTxtorLabel( -1, &sx->scx_trans, &transistor );
	ret.nd_what = ND_NAME;
    }
    else if( sdTile != (Tile *) NULL )
    {
	int  tNum;

	SimAddNodeList( reg );
	SimAddDefList( def );

	transistor.t_pnum = DBNumPlanes;
	transistor.t_nterm = 0;
	transistor.t_do_terms = TRUE;

	/* collect the transistor position, and its terminals */
	arg.fra_connectsTo = &SimTransMask;

	if (IsSplit(tile))
	    loctype = SplitSide(sdTile) ? TiGetRightType(sdTile)
			: TiGetLeftType(sdTile);
	else
	    loctype = TiGetTypeExact(sdTile);

	arg.fra_pNum = DBPlane(loctype);
	arg.fra_uninit = (ClientData) sdTile->ti_client;
	arg.fra_region = (Region *) &ret;
	arg.fra_each = SimTransistorTile;
	(void) ExtFindNeighbors( sdTile, arg.fra_pNum, &arg );

	/* Unmark the transitor, since its not part of this region */
	arg.fra_region = (Region *) arg.fra_uninit;
	arg.fra_uninit = (ClientData) &ret;
	arg.fra_each = (int (*)()) NULL;
	(void) ExtFindNeighbors( sdTile, arg.fra_pNum, &arg );

	if( (tNum = SimTermNum( &transistor, reg )) < 0 )
	{
	    TxPrintf( "\tSimFindOneNode: bad transistor terminal number\n" );
	    goto use_name;
	}

	ret.nd_name = SimTxtorLabel( tNum, &sx->scx_trans, &transistor );
	ret.nd_what = ND_NAME;
    }
    else		/* no transistors found, get the regions labels */
    {
	SimAddNodeList( reg );
	SimAddDefList( def );

      use_name:
	ExtLabelOneRegion( def, ExtCurStyle->exts_nodeConn, reg );
	ret.nd_region = reg;
	ret.nd_what = ND_REGION;
    }
    return( &ret );
}


/*
 *----------------------------------------------------------------
 * SimGetNodeName
 *
 * 	This procedure uses the Magic circuit extraction code to generate
 *	the node name.  The node is extracted by searching the database
 *	for all tiles electrically connected to the node.  Each tile is
 *	marked with a pointer to the node region it belongs to.  It is
 *	the node region data structure which contains the labels for a
 *	node.  The "preferred" label for the node is taken, and this
 *	is combined with the path name to produce the complete node name.
 *
 * Results:
 *	A pointer to the node name is returned.  This is a pointer to
 *	statically allocated memory, so subsequent calls to this procedure
 *	will change the node name the returned pointer references.
 *
 * Side effects:
 *	Any node region created is added to the node region list.
 *----------------------------------------------------------------
 */

char *
SimGetNodeName(sx, tp, path)
    SearchContext	*sx;		/* current search context */
    Tile		*tp;		/* tile in this cell which is part
					 * of the node
					 */
    char		*path;		/* path name of hierarchy of search */
{
    CellDef	*def = sx->scx_use->cu_def;
    NodeRegion 	*nodeList;
    LabelList 	*ll;
    static char nodename[256];
    char	buff[256];
    char 	*text;
    char 	*nname;

    SimSawAbortString = FALSE;

    if( SimUseCoords && simExtStyle != ExtCurStyle )
	SimInitConnTables();

    /* check to see if this tile has been extracted before */

    if (tp->ti_client == extUnInit)
    {
	NodeSpec  *ns;

	ns = SimFindOneNode(sx, tp);
	if( ns->nd_what == ND_NAME )
	{
	    SimSawAbortString = TRUE;
	    return( ns->nd_name );
	}
	nodeList = ns->nd_region;
    }
    else
    {
	nodeList = (NodeRegion *)(tp->ti_client);
    }

    /* generate the node name from the label region and the path name */

    text = extNodeName((LabRegion *)nodeList);
    strcpy(buff, text);
    strcpy(nodename, path);
    strcat(nodename, text);

    /* check to see if we should abort the search on the node name */

    if (!SimInitGetnode) {
	if (HashLookOnly(&SimGetnodeTbl, buff) != (HashEntry *) NULL) {
	    SimSawAbortString = TRUE;
	    if (HashLookOnly(&SimAbortSeenTbl, buff) == (HashEntry *) NULL) {
		HashFind(&SimAbortSeenTbl, buff);
		TxPrintf("Node name search aborted on \"%s\"\n", buff);
	    }
	}
    }

    /* Check whether or not to print out node name aliases.  Each alias 
     * found is hashed in a table in order to suppress printing of
     * duplicate aliases.
     */

    if (SimGetnodeAlias && SimIsGetnode) {
	if (HashLookOnly(&SimGNAliasTbl, nodename) == (HashEntry *) NULL) {
	    HashFind(&SimGNAliasTbl, nodename);
#ifdef MAGIC_WRAPPER
	    Tcl_AppendElement(magicinterp, nodename);
#else
	    TxPrintf("alias: %s\n", nodename);
#endif
	}
    }

    /* search the list of all labels for this node, creating the full
     * node name and returning the "best" node name found.
     */
    for (ll = nodeList->nreg_labels; ll; ll = ll->ll_next) {
	if (ll->ll_label->lab_text == text) {
	    for (ll = ll->ll_next; ll; ll = ll->ll_next) {
		nname = ll->ll_label->lab_text;
		if (extLabType(nname, LABTYPE_NAME)) {
		    strcpy(nodename, path);
		    strcat(nodename, nname);
		    if (efPreferredName(nname, buff)) {
			strcpy(buff, nname);
		    }
		    if (SimGetnodeAlias && SimIsGetnode) {
			if (HashLookOnly(&SimGNAliasTbl, nodename) 
			    == (HashEntry *) NULL) {
			    HashFind(&SimGNAliasTbl, nodename);
#ifdef MAGIC_WRAPPER
			    Tcl_AppendElement(magicinterp, nodename);
#else
			    TxPrintf("alias: %s\n", nodename);
#endif
			}
		    }
		}
	    }
	    break;
	}
    }
    strcpy(nodename, path);
    strcat(nodename, buff);
    return(nodename);
}

/*
 *----------------------------------------------------------------
 * SimGetNodeCleanup
 *
 *	This procedure is called to clean up the data structures and the
 *	tile database after a node name is extracted.
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	The node region list and cell def lists are re-initialized.
 *----------------------------------------------------------------
 */

void
SimGetNodeCleanUp()
{
    SimFreeNodeRegs();
    SimInitDefList();
}
