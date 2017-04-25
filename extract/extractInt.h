/*
 * extractInt.h --
 *
 * Defines things shared internally by the extract module of Magic,
 * but not generally needed outside the extract module.
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/extract/extractInt.h,v 1.7 2010/08/10 00:18:46 tim Exp $"
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
 *
 *   This module has been modified at DEC/WRL and Stanford University.
 *   The above disclaimers apply.
 *
 */

#ifndef _EXTRACTINT_H
#define _EXTRACTINT_H

#include "database/database.h"

#undef	NT
#define	NT	TT_MAXTYPES
#undef	NP
#define	NP	PL_MAXTYPES

/* ------------------------ Capacitance Values ------------------------- */

typedef double CapValue; /* No longer allowed to define back to integer,
			  * as this touches too many parts of the code.
			  */

/* Procs to manipulate capacitance hash tables. */
extern CapValue extGetCapValue();
extern void extSetCapValue();
extern void extCapHashKill();

typedef int ResValue;	/* Warning:  in some places resistances are stored
			 * as ints.  This is here for documentation only.
			 */

/* ------------------------ Parameter lists --------------------------- */

/* These lists keep track of what parameter names subcircuit definitions
 * use for parameters that magic knows how to generate.  Valid pl_param
 * values are a (area), p (perimeter), w (width), l (length), s (substrate),
 * x (position), and y (position).  Values "a" and "p" may be followed by
 * an additional integer indicating the terminal from which the value is
 * used (e.g., source area, drain perimeter, etc.).  An integer "0"
 * indicates the device identifier region (e.g., gate) and is equivalent
 * to having no integer at all.  Integers "1" and up indicate terminals,
 * in order.
 */

typedef struct pl
{
    int		 pl_count;	/* Share this list. . . */
    char	 pl_param[2];	/* Default character for parameter */
    char	*pl_name;	/* Full name for parameter */
    double	 pl_scale;	/* Scaling of parameter, if specified */
    struct pl	*pl_next;	/* Next parameter in list */
} ParamList;

/* -------------------------- Label lists ----------------------------- */

/*
 * List of labels for a node.
 * We keep around pointers to the entire labels for
 * later figuring out which are attached to the gates,
 * sources, or drains of transistors.
 */
typedef struct ll
{
    Label	*ll_label;	/* Actual Label in the source CellDef */
    struct ll	*ll_next;	/* Next LabelList in this region */
    int		 ll_attr;	/* Which terminal of a transistor this is
				 * an attribute of.
				 */
} LabelList;

#define	LL_NOATTR	-1	/* Value for ll_attr above if the label is
				 * not a transistor attribute.
				 */
#define	LL_GATEATTR	-2	/* Value for ll_attr if the label is a gate
				 * attribute, rather than one of the diffusion
				 * terminals' attributes.
				 */
#define LL_SORTATTR	-3      /* value for ll_attr used in 
				 * ExtBasic.c/ExtSortTerminals() to swap 
				 * the attributes as well as the regions
				 * -- Stefanos 5/96
				 */
#define LL_PORTATTR	-4	/* value for ll_attr used to declare
				 * the label to be a subcircuit port
				 * -- Tim 5/02
				 */
/*
 * Types of labels.
 * These can be or'd into a mask and passed to extLabType().
 */
#define	LABTYPE_NAME		0x01	/* Normal node name */
#define	LABTYPE_NODEATTR	0x02	/* Node attribute */
#define	LABTYPE_GATEATTR	0x04	/* Transistor gate attribute */
#define	LABTYPE_TERMATTR	0x08	/* Transistor terminal (source/drain)
					 * attribute.
					 */
#define LABTYPE_PORTATTR	0x10	/* Subcircuit port */

/* ----------------------------- Regions ------------------------------ */

/*
 * The following are the structures built up by the various
 * clients of ExtFindRegions.  The general rule for these
 * structures is that their initial fields must be identical
 * to those in a Region, but subsequent fields are up to
 * the individual client.
 *
 * Regions marked as GENERIC are the types accepted by
 * procedures in ExtRegion.c.
 */

    /*
     * GENERIC Region struct.
     * All this provides is a pointer to the next Region.
     * This is the type passed to functions like ExtFreeRegions,
     * and is the type returned by ExtFindRegions.  Clients should
     * cast pointers of this type to their own, client type.
     */
typedef struct reg
{
    struct reg	*reg_next;	/* Next region in list */
} Region;

    /*
     * GENERIC region with labels.
     * Any other structure that wants to reference node names
     * must include the same fields as this one as its first part.
     */
typedef struct lreg
{
    struct lreg	*lreg_next;	/* Next region in list */
    int		 lreg_pnum;	/* Lowest numbered plane in this region */
    int		 lreg_type;	/* Type of tile that contains lreg_ll */
    Point	 lreg_ll;	/* Lower-leftmost point in this region on
				 * plane lreg_pnum.  We take the min first
				 * in X, then in Y.
				 */
    LabelList	*lreg_labels;	/* List of labels for this region.  These are
				 * any labels connected to the geometry making
				 * up this region.  If the list is empty, make
				 * up a name from lreg_pnum and lreg_ll.
				 */
} LabRegion;

    /*
     * Node region: labelled region with resistance and capacitance.
     * Used for each node in the flat extraction of a cell.
     */

typedef struct
{
    int		 pa_perim;
    int		 pa_area;
} PerimArea;

typedef struct nreg
{
    struct nreg	*nreg_next;	/* Next region in list */
    int		 nreg_pnum;	/* Lowest numbered plane in this region */
    int		 nreg_type;	/* Type of tile that contains nreg_ll */
    Point	 nreg_ll;	/* Lower-leftmost point in this region on
				 * plane nreg_pnum.  We take the min first
				 * in X, then in Y.
				 */
    LabelList	*nreg_labels;	/* See LabRegion for description */
    CapValue	 nreg_cap;	/* Capacitance to ground */
    ResValue	 nreg_resist;	/* Resistance estimate */
    PerimArea	 nreg_pa[1];	/* Dummy; each node actually has
				 * ExtCurStyle->exts_numResistClasses
				 * array elements allocated to it.
				 */
} NodeRegion;

    /*
     * Transistor region: labelled region with perimeter and area.
     * Used for each transistor in the flat extraction of a cell.
     */
typedef struct treg
{
    struct treg	*treg_next;	/* Next region in list */
    int		 treg_pnum;	/* UNUSED */
    int		 treg_type;	/* Type of tile that contains treg_ll */
    Point	 treg_ll;	/* UNUSED */
    LabelList	*treg_labels;	/* Attribute list */
    Tile	*treg_tile;	/* Some tile in the channel */
    int		 treg_area;	/* Area of channel */
} TransRegion;

typedef struct {	/* Maintain plane information when pushing	*/
    Rect area;		/* tiles on the node stack.  For use with	*/
    int  plane;		/* function extNbrPushFunc().			*/
} PlaneAndArea; 

/* Structure to be kept in a hash table of node regions for the current	*/
/* extract cell.  It tracks the original substrate cap calculated for	*/
/* each region used in the "node" line output, the final substrate cap	*/
/* calculated after taking all subcircuits into account, and a running	*/
/* total of all corrections to the node's substrate cap generated in	*/
/* "merge" lines by extSubtree() and extArray().  After both routines	*/
/* have run, any unaccounted capacitance is output to the .ext file as	*/
/* a "subcap" line.							*/

typedef struct {
    NodeRegion *subcap_reg;
    CapValue	subcap_orig;
    CapValue	subcap_final;
    CapValue	subcap_adjust;
} SubCapAdjust;

/*
 * The following constructs a node name from the plane number 'n'
 * and lower left Point l, and places it in the string 's' (which must
 * be large enough).
 */
#define	extMakeNodeNumPrint(buf, plane, coord) \
    (void) sprintf((buf), "%s_%s%d_%s%d#", DBPlaneShortName(plane), \
	((coord).p_x < 0) ? "n": "", abs((coord).p_x), \
	((coord).p_y < 0) ? "n": "", abs((coord).p_y)) 

/* Old way:  cryptic numbers, but a bit shorter
 *
 * #define extMakeNodeNumPrint(s, n, l) \
 * (void) sprintf((s), "%d_%d_%d#", (n), extCoord((l).p_x), extCoord((l).p_y))
 *
 * The following is used to map the full coordinate space into
 * the positive integers, for constructing internally generated
 * node names.
 *
 * #define	extCoord(x)	(((x) < 0) ? (1 - ((x) << 1)) : ((x) << 1))
 */

/*
 * Argument passed to filter functions for finding regions.
 */
typedef struct
{
    TileTypeBitMask	*fra_connectsTo; /* Array of TileTypeBitMasks.  The
					  * element fra_connectsTo[t] has a
					  * bit set for each type that
					  * connects to 't'.
					  */
    CellDef		*fra_def;	 /* Def being searched */
    int			 fra_pNum;	 /* Plane currently searching */
    ClientData		 fra_uninit;	 /* This value appears in the ti_client
					  * field of a tile if it's not yet
					  * been visited.
					  */
    Region	      *(*fra_first)();	 /* Function to init new region */
    int		       (*fra_each)();	 /* Function for each tile in region */
    Region		*fra_region;	 /* Ptr to Region struct for current
					  * region.  May be set by fra_first
					  * and used by fra_each.
					  */
} FindRegion;

#define	TILEAREA(tp)	((TOP(tp) - BOTTOM(tp)) * (RIGHT(tp) - LEFT(tp)))

/* -------------------- Perimeter of a region ------------------------- */

/*
 * Segment of the boundary of a region whose perimeter
 * is being traced by ExtTracePerimeter() and extEnumTilePerim().
 */
typedef struct
{
    Tile	*b_inside;	/* Pointer to tile just inside segment */
    Tile	*b_outside;	/* Pointer to tile just outside segment */
    Rect	 b_segment;	/* Actual coordinates of segment */
    unsigned char b_direction;	/* Direction following segment (see below) */
    int		 b_plane;	/* extract argument for extSideOverlap   */
} Boundary;

#define	BoundaryLength(bp) \
	((bp)->b_segment.r_xtop - (bp)->b_segment.r_xbot \
    +    (bp)->b_segment.r_ytop - (bp)->b_segment.r_ybot)

/* Directions in which we can be following the boundary of a perimeter	*/

#define	BD_LEFT		1	/* Inside is to right */
#define	BD_TOP		2	/* Inside is below */
#define	BD_RIGHT	4	/* Inside is to left */
#define	BD_BOTTOM	8	/* Inside is above */

/* -------- Yank buffers for hierarchical and array extraction -------- */

extern CellUse *extYuseCum;
extern CellDef *extYdefCum;

/* --------------- Argument passed to extHierYankFunc ----------------- */

typedef struct
{
    Rect	*hy_area;	/* Area (in parent coordinates) to be yanked */
    CellUse	*hy_target;	/* Yank into this use */
    bool	 hy_prefix;	/* If TRUE, prefix labels with use id */
} HierYank;

/* ----- Arguments to filter functions in hierarchical extraction ---- */

    /*
     * The following defines an extracted subtree.
     * The CellUse et_use will be either a cell we are extracting,
     * or a flattened subtree.  If et_lookNames is non-NULL, it
     * points to a CellDef that we should look in for node names.
     */
typedef struct extTree
{
    CellUse		*et_use;	/* Extracted cell, usually flattened */
    CellUse		*et_realuse;	/* If et_use is flattened, et_realuse
					 * points to the unflattened subtree's
					 * root use; otherwise it is NULL.
					 */
    CellDef		*et_lookNames;	/* See above */
    NodeRegion		*et_nodes;	/* List of nodes */
    HashTable		 et_coupleHash;	/* Table for coupling capacitance.
					 * key is type CoupleKey
					 * value is pointer to type CapValue
					 */
    struct extTree	*et_next;	/* Next one in list */
} ExtTree;

    /*
     * The following structure contains information passed down
     * through several levels of filter functions during hierarchical
     * extraction.
     *
     * The procedure ha_nodename is used to map from a tile into the
     * name of the node to which that tile belongs.  It should be of
     * the following format:
     *
     *	char *
     *	proc(tp, et, ha)
     *	    Tile *tp;
     *	    ExtTree *et;
     *	    HierExtractArg *ha;
     *	{
     *	}
     *
     * It should always return a non-NULL string; if the name of a
     * node can't be determined, the string can be "(none)".
     */
typedef struct
{
    FILE	*ha_outf;	 /* The .ext file being written */
    CellUse	*ha_parentUse;	 /* Use pointing to the def being extracted */
    char      *(*ha_nodename)(); /* Map (tp, et, ha) into nodename; see above */
    ExtTree	 ha_cumFlat;	 /* Cumulative yank buffer */
    NodeRegion  *ha_parentReg;	 /* Node region list from parent def */
    HashTable	 ha_connHash;	 /* Connections made during hier processing */

/* All areas are in parent coordinates */

    Rect	 ha_interArea;	/* Area of whole interaction being considered */
    Rect	 ha_clipArea;	/* Only consider capacitance, perimeter, and
				 * area that come from inside this area.  This
				 * rectangle is contained within ha_interArea.
				 */
    CellUse	*ha_subUse;	/* Root of the subtree being processed now */
    Rect	 ha_subArea;	/* Area of ha_subUse inside the interaction
				 * area, i.e, contained within ha_interArea.
				 */
    Tile	*hierOneTile;	/* Used in ExtHier.c, tile from extHierOneFlat */
    int		hierPNum;	/* Used in ExtHier.c, plane of tile above */
    TileType	hierType;	/* Used in ExtHier.c, type of tile above */
    int		hierPNumBelow;	/* Used in ExtHier.c, plane of tile below */
} HierExtractArg;

/*
 * Normally, nodes in overlapping subcells are expected to have labels
 * in the area of overlap.  When this is not the case, we have to use
 * a much more expensive algorithm for finding the labels attached to
 * the subcells' geometry in the overlap area.  The following structure
 * is used to hold information about the search in progress for such
 * labels.
 */
typedef struct
{
    HierExtractArg	*hw_ha;		/* Describes context of search */
    Label		*hw_label;	/* We update hw_label with a ptr to a
					 * newly allocated label if successful.
					 */
    Rect		 hw_area;	/* Area in parent coordinates of the
					 * area where we're searching.
					 */
    bool		 hw_autogen;	/* If TRUE, we trace out all geometry
					 * in the first node in the first cell
					 * found to overlap the search area,
					 * and use the internal name for that
					 * node.
					 */
    TerminalPath	 hw_tpath;	/* Hierarchical path down to label
					 * we are searching for, rooted at
					 * the parent being extracted.
					 */
    TileTypeBitMask	 hw_mask;	/* Mask of tile types that connect to
					 * the tile whose node is to be found,
					 * and which are on the same plane.
					 * Used when calling ExtFindRegions.
					 */
    bool		 hw_prefix;	/* If FALSE, we skip the initial
					 * use identifier when building
					 * hierarchical labels (as when
					 * extracting arrays; see hy_prefix
					 * in the HierYank struct).
					 */
    int		       (*hw_proc)();
} HardWay;

/* --------------------- Coupling capacitance ------------------------- */

/*
 * The following structure is the hash key used for computing
 * internodal coupling capacitance.  Each word is a pointer to
 * one of the nodes being coupled.  By convention, the first
 * word is the lesser of the two NodeRegion pointers.
 */
typedef struct
{
    NodeRegion	*ck_1, *ck_2;
} CoupleKey;

extern void extCoupleHashZero(); /* Clears out all pointers to data in table */

/* ------------------ Interface to debugging module ------------------- */

extern ClientData extDebugID;	/* Identifier returned by the debug module */

/* ----------------- Technology-specific information ------------------ */

/*
 * Structure used to define sidewall coupling capacitances.
 */
typedef struct edgecap
{
    struct edgecap	*ec_next;	/* Next edge capacitance rule in list */
    CapValue		 ec_cap;	/* Capacitance (attofarads) */
    TileTypeBitMask	 ec_near;	/* Types closest to causing edge, or in
					 * the case of sideOverlaps, the
					 * types we are overlapping.
					 */
    TileTypeBitMask	 ec_far;	/* Types farthest from causing edge, or
					 * in the case of sideOverlaps, the
					 * types that shield the edge from
					 * the overlaped tile.
					 */
    int			 ec_pmask;	/* specifies which planes are to be	*/
					/* used.				*/
} EdgeCap;


/* A type used to determine if current style needs planeorder or not */
typedef enum { noPlaneOrder, needPlaneOrder, seenPlaneOrder } planeOrderStatus ;

/*
 * Because a large TT_MAXTYPES value quickly generates huge extract section
 * structures, we want to keep around only the style names, and dynamically
 * load and destroy the extract section values as needed, when doing an
 * extraction command.
 */

typedef struct extkeep
{
    struct extkeep	*exts_next;
    char		*exts_name;
} ExtKeep;

/*
 * Parameters for the process being extracted.
 * We try to use use integers here, rather than floats, to be nice to
 * machines like Sun workstations that don't have hardware
 * floating point.
 * 
 * In the case of capacitances, though, we may have to use floats, depending
 * upon the type CapValue.  In some newer processes the capacitance per
 * lambda^2 is less than 1 attofarad.
 */

typedef struct extstyle
{
    char		exts_status;	/* Loaded, not loaded, or pending */
    char		*exts_name;	/* Name of this style */

    /*
     * Connectivity tables.
     * Each table is an array of TileTypeBitMasks indexed by TileType.
     * The i-th element of each array is a mask of those TileTypes
     * to which type 'i' connects.
     */

    /* Everything is connected to everything else in this table */
    TileTypeBitMask	 exts_allConn[NT];

    /*
     * Connectivity for determining electrical nodes.
     * This should be essentially the same as DBConnectTbl[].
     */
    TileTypeBitMask	 exts_nodeConn[NT];

    /*
     * Connectivity for determining resistive regions.
     * Two types should be marked as connected here if
     * they are both connected in exts_nodeConnect[], and
     * if they both have the same resistance per square.
     */
    TileTypeBitMask	 exts_resistConn[NT];

    /*
     * Connectivity for determining transistors.
     * Each transistor type should connect only to itself.
     * Nothing else should connect to anything else.
     */
    TileTypeBitMask	 exts_transConn[NT];

    /*
     * Set of types to be considered for extraction.  Types not in
     * this list cannot be nodes (e.g., implant layers)
     */
    TileTypeBitMask	 exts_activeTypes;

    /*
     * Sheet resistivity for each tile type, in milli-ohms per square.
     * For types that are transistors or capacitors, this corresponds
     * to the sheet resistivity of the gate.
     */

    /* Maps from a tile type to the index of its sheet resistance entry */
    int			 exts_typeToResistClass[NT];

    /* Gives a mask of neighbors of a type with different resistivity */
    TileTypeBitMask	 exts_typesResistChanged[NT];

    /*
     * Resistance information is also provided by the following tables:
     * exts_typesByResistClass[] is an array of masks of those types
     * having the same sheet resistivity, for each different value
     * of sheet resistivity; exts_resistByResistClass[] is a parallel array
     * giving the actual value of sheet resistivity.  Both are indexed
     * from 0 up to (but not including) exts_numResistClasses.
     */
    TileTypeBitMask	 exts_typesByResistClass[NT];
    ResValue		 exts_resistByResistClass[NT];
    int			 exts_numResistClasses;

    /* Resistance per type */
    ResValue		 exts_sheetResist[NT];

    /*
     * Resistances for via holes, given in milliohms.  Number of
     * cuts is determined by the "cifoutput" style "squares"
     * parameters.
     */
    ResValue		exts_viaResist[NT];

    /*
     * Amount to scale resistance of a material on a corner.
     * Defauts to 1.0.  Often set to 0.5.
     */
    float		exts_cornerChop[NT];

    /* Layer height and thickness used by the geometry extractor */
    float		exts_height[NT];
    float		exts_thick[NT];

    /*
     * Capacitance to substrate for each tile type, in units of
     * attofarads per square lambda.
     */

	/*
	 * Capacitance per unit area.  This is zero for explicit capacitor
	 * types, which handle gate-channel capacitance specially.  For
	 * transistor types, this is at best an approximation that is
	 * truly valid only when the transistor is switched off.
	 */
    CapValue		 exts_areaCap[NT];

	/*
	 * Capacitance per unit perimeter.  Sidewall capacitance depends both
	 * on the type inside the perimeter as well as the type outside it,
	 * so the table is doubly indexed by TileType.
	 *
	 * The mask exts_perimCapMask[t] contains bits for all those TileTypes
	 * 's' such that exts_perimCap[t][s] is nonzero.
	 */
    CapValue		 exts_perimCap[NT][NT];
    TileTypeBitMask	 exts_perimCapMask[NT];

    /*
     * Overlap coupling capacitance for each pair of tile types, in units
     * of attofarads per square lambda of overlap.
     * Internodal capacitance due to overlap only occurs between tile
     * types on different tile planes that are not shielded by intervening
     * tiles.
     */

	/*
	 * The mask exts_overlapPlanes is a mask of those planes that must
	 * be searched for tiles having overlap capacitance, and the mask
	 * exts_overlapTypes[p] is those types having overlap capacitance
	 * on each plane p.  The intent is that exts_overlapTypes[p] lists
	 * only those types t for which some entry of exts_overlapCap[t][s]
	 * is non-zero.
	 */
    PlaneMask		 exts_overlapPlanes;
    TileTypeBitMask	 exts_overlapTypes[NP];

	/*
	 * The mask exts_overlapOtherPlanes[t] is a mask of the planes that
	 * must be searched for tiles having overlap capacitance with tiles
	 * of type 't', and exts_overlapOtherTypes[t] is a mask of the types
	 * with which our overlap capacitance is non-zero.
	 */
    TileTypeBitMask	 exts_overlapOtherTypes[NT];
    PlaneMask		 exts_overlapOtherPlanes[NT];
    
	/*
	 * Both exts_overlapShieldTypes[][] and exts_overlapShieldPlanes[][]
	 * are indexed by the same pair of types used to index the table
	 * exts_overlapCap[][]; they identify the types and planes that
	 * shield capacitance between their index types.
	 */
    TileTypeBitMask	 exts_overlapShieldTypes[NT][NT];
    PlaneMask		 exts_overlapShieldPlanes[NT][NT];

	/*
	 * The table extOverlapCap[][] is indexed by two types to give the
	 * overlap coupling capacitance between them, per unit area.  Only
	 * one of extOverlapCap[i][j] and extOverlapCap[j][i] should be
	 * nonzero.  The capacitance to substrate of the tile of type 'i'
	 * is deducted when an overlap between i and j is detected, if
	 * extOverlapCap[i][j] is nonzero.  This is only done, however, if
	 * tile i is below tile j in exts_planeOrder;
	 */
    CapValue		 exts_overlapCap[NT][NT];

	/* Specifies an ordering of the planes, so we can determine which
	 * tile is above another one.  This is used only when determining
	 * if we should subtract capacitance to substrate for overlap and
	 * sideoverlap rules. If no planeorder is specified and the style
	 * does not contain a noplaneordering command a warning is issued
	 * and the default planeorder is used for the style.
	 */
    int			exts_planeOrder[NP];
	/* set/reset with planeorder commands to determine  whether 
	 * we will warn if no planeorder is specified. This is done
	 * because at Stanford we use a lot of diagnostic extraction
	 * styles (for floating wells etc.) and we don't want to specify
	 * the planeorder for each and every one of them.
	 */
    planeOrderStatus	exts_planeOrderStatus;


    /*
     * Sidewall coupling capacitance.  This capacitance is between edges
     * on the same plane, and is in units of attofarads.  It is multiplied
     * by the value interpolated from a fringing-field table indexed by the
     * common length of the pair of edges divided by their separation:
     *
     *		   |				|
     *		E1 +----------------------------+
     *				^
     *				+--- distance between edges
     *				v
     *			+-----------------------------------+ E2
     *			|				    |
     *
     *			<-----------------------> length in common
     */

	/*
	 * The entry exts_sideCoupleCap[i][j] is a list of the coupling
	 * capacitance info between edges with type 'i' on the inside
	 * and 'j' on the outside, and other kinds of edges.
	 */
    EdgeCap		*exts_sideCoupleCap[NT][NT];

	/*
	 * exts_sideCoupleOtherEdges[i][j] is a mask of those types on the
	 * far sides of edges to which an edge with 'i' on the inside and
	 * 'j' on the outside has coupling capacitance.
	 */
    TileTypeBitMask	 exts_sideCoupleOtherEdges[NT][NT];

	/*
	 * We search out a distance exts_sideCoupleHalo from each edge
	 * for other types with which we have coupling capacitance.
	 * This value determines how much extra gets yanked when
	 * computing hierarchical adjustments, so should be kept
	 * small to insure reasonable performance.
	 */
    int			 exts_sideCoupleHalo;

    /*
     * Sidewall-overlap coupling capacitance.
     * This is between an edge on one plane and a type on another plane
     * that overlaps the edge (from the outside of the edge), and is in
     * units of attofarads per lambda.
     *
     * When an edge with sidewall capacitance to substrate is found to
     * overlap a type to which it has sidewall overlap capacitance, the
     * original capacitance to substrate is replaced with the overlap
     * capacitance to the tile overlapped, if the edge is above the tile
     * being overlapped (according to ext_planeOrder).  If the tiles are 
     * the other way around, then this replacement is not done.
     */

	/*
	 * The entry exts_sideOverlapCap[i][j] is a list of the coupling
	 * capacitance info between edges with type 'i' on the inside
	 * and 'j' on the outside, and other kinds of tiles on other
	 * planes.  The ec_near mask in the EdgeCap record identifies the 
	 * types to which we have sidewall overlap capacitance, and the
	 * ec_far mask identifies the types that shield the tiles preventing
	 * a capacitance.
	 */
    EdgeCap		*exts_sideOverlapCap[NT][NT];

	/*
	 * extSideOverlapOtherTypes[i][j] is a mask of those types to which
	 * an edge with 'i' on the inside and 'j' on the outside has coupling
	 * capacitance.  extSideOverlapOtherPlanes[i][j] is a mask of those
	 * planes to which edge [i][j] has overlap coupling capacitance.
	 * exts_sideOverlapShieldPlanes[s][t] is a list of the planes that
	 * need to be examined for shielding material when we are considering
	 * a sidewall overlap capacitor between types s and t.  This may
	 * be the "or" of the planes needed by several sideoverlap rules, 
	 * since there can be several types of edges in which type s is 
	 * the "intype" member and the "outtype" member varies.  Note that
	 * sideOverlapShieldPlanes is indexed like overlapShieldPlanes, not
	 * like sideOverlapOtherPlanes.
	 */
    PlaneMask		 exts_sideOverlapOtherPlanes[NT][NT];
    TileTypeBitMask	 exts_sideOverlapOtherTypes[NT][NT];
    PlaneMask		 exts_sideOverlapShieldPlanes[NT][NT];

	/*
	 * Both exts_overlapShieldTypes[][] and exts_overlapShieldPlanes[][]
	 * are indexed by the same pair of types used to index the table
	 * exts_overlapCap[][]; they identify the types and planes that
	 * shield capacitance between their index types.
	 */


    /* Common to both sidewall coupling and sidewall overlap */

	/*
	 * exts_sideTypes[p] is a mask of those types 't' having sidewall
	 * coupling or sidewall overlap capacitance on plane p (i.e, for
	 * which a bin in exts_sideCoupleCap[t][] or exts_sideOverlapCap[t][]
	 * is non-empty), and exts_sidePlanes a mask of those planes containing
	 * tiles in exts_sideTypes[].
	 */
    PlaneMask		 exts_sidePlanes;
    TileTypeBitMask	 exts_sideTypes[NP];

	/*
	 * The mask exts_sideEdges[i] is just a mask of those types j for
	 * which either exts_sideCoupleCap[i][j] or exts_sideOverlapCap[i][j]
	 * is non-empty.
	 */
    TileTypeBitMask	 exts_sideEdges[NT];

    /* Transistors */

	/* Name of each transistor type as output in .ext file */
    char		*exts_transName[NT];

	/* List of parameter names for each subcircuit type */
    ParamList		*exts_deviceParams[NT];

	/* Device class for each layer type */
    char		exts_deviceClass[NT];

	/* Contains one for each type of fet, zero for all other types */
    TileTypeBitMask	 exts_transMask;

	/*
	 * Per-square resistances for each possible transistor type,
	 * in the various regions that such a type might operate.
	 * The only operating region currently used is "linear",
	 * which the resistance extractor uses in its thresholding
	 * operation.  NOTE: resistances in this table are in OHMS
	 * per square, not MILLIOHMS!
	 */
    HashTable		 exts_transResist[NT];
    ResValue		 exts_linearResist[NT];

	/*
	 * Mask of the types of tiles that connect to the channel terminals
	 * of a transistor type.  The intent is that these will be the
	 * diffusion terminals of a transistor, ie, its source and drain.
	 * UPDATED May, 2008:  Record is a list of type masks, allowing
	 * multiple terminal types in the case of, e.g., high-voltage
	 * or other asymmetric devices.  The last entry in the list should
	 * be equal to DBSpaceBits.
	 */
    TileTypeBitMask	 *exts_transSDTypes[NT];

	/*
	 * Maximum number of terminals (source/drains) per transistor type.
	 * This table exists to allow the possibility of transistors with
	 * more than two diffusion terminals at some point in the future.
	 */
    int			 exts_transSDCount[NT];

	/* Currently unused: gate-source capacitance per unit perimeter */
    CapValue		 exts_transSDCap[NT];

	/* Currently unused: gate-channel capacitance per unit area */
    CapValue		 exts_transGateCap[NT];

	/*
	 * Each type of transistor has a substrate node.  By default,
	 * it is the one given by exts_transSubstrateName[t].  However,
	 * if the mask exts_transSubstrateTypes[t] is non-zero, and if
	 * the transistor overlaps material of one of the types in the
	 * mask, then the transistor substrate node is the node of the
	 * material it overlaps.  If exts_transSub
	 */
    char		*exts_transSubstrateName[NT];
    TileTypeBitMask	 exts_transSubstrateTypes[NT];
#ifdef ARIEL
    TileTypeBitMask	 exts_subsTransistorTypes[NT];
#endif	/* ARIEL */

	/*
	 * There is a single name for global substrate, and a list of
	 * types that connect to the substrate.  Since for non-SOI
	 * processes, this generally is used to specify that space on
	 * the well plane is the substrate, the plane number for the
	 * well plane is given, too.
	 */
    char		*exts_globSubstrateName;
    TileTypeBitMask	 exts_globSubstrateTypes;
    int			 exts_globSubstratePlane;

    /* Scaling */
	/*
	 * Step size used when breaking up a large cell for interaction
	 * checks during hierarchical extraction.  We check exts_stepSize
	 * by exts_stepSize chunks for interactions one at a time.
	 */
    int			 exts_stepSize;

	/*
	 * Number of linear units per lambda.  All perimeter dimensions
	 * that we output to the .ext file should be multiplied by
	 * exts_unitsPerLambda; we produce a "scale" line in the .ext file
	 * indicating this.  All area dimensions should be multiplied
	 * by exts_unitsPerLambda**2.
	 * (changed to type float May 11, 2006 to accommodate, e.g., 90
	 * and 130 nm technologies)
	 */
    float		 exts_unitsPerLambda;

	/*
	 * Scaling for resistance and capacitance.
	 * All resistances in the .ext file should be multiplied by
	 * exts_resistScale to get milliohms, and all capacitances by
	 * exts_capScale to get attofarads.  These numbers appear in
	 * the "scale" line in the .ext file.
	 */
    int			 exts_capScale;
    int			 exts_resistScale;
} ExtStyle;

#define EXT_PLUG_GND	1
#define EXT_PLUG_VDD	2

extern ExtStyle *ExtCurStyle;

/* ------------------- Hierarchical node merging ---------------------- */

/*
 * Table used to hold all merged nodes during hierarchical extraction.
 * Used for duplicate suppression.
 */
extern HashTable extHierMergeTable;

/*
 * Each hash entry in the above table points to a NodeName struct.
 * Each NodeName points to the Node corresponding to that name.
 * Each Node points back to a list of NodeNames that point to that
 * Node, and which are linked together along their nn_next fields.
 */
typedef struct nn
{
    struct node	*nn_node;	/* Node for which this is a name */
    char	*nn_name;	/* Text of name */
    struct nn	*nn_next;	/* Other names of nn_node */
} NodeName;

typedef struct node
{
    NodeName	*node_names;	/* List of names for this node.  The first name
				 * in the list is the "official" node name.
				 */
    CapValue	 node_cap;	/* Capacitance to substrate */
    PerimArea	 node_pa[1];	/* Dummy; each node actually has
				 * ExtCurStyle->exts_numResistClasses
				 * array elements allocated to it.
				 */
} Node;

/* -------------------------------------------------------------------- */

/*
 * Value normally resident in the ti_client field of a tile,
 * indicating that the tile has not yet been visited in a
 * region search.
 */
extern ClientData extUnInit;

#define extGetRegion(tp)	( (tp)->ti_client )
#define extHasRegion(tp,und)	( (tp)->ti_client != (und) )


/* For non-recursive flooding algorithm */
#define	VISITPENDING	((ClientData) NULL)	/* Marks tiles on stack */

/* Note that this macro depends on MAXPLANES being small	*/
/* compared to the bit position of TT_SIDE.  Since tens of	*/
/* thousands of planes is inconceivable, this should not be a	*/
/* problem.  It is necessary to push the tile's TT_SIDE	bit	*/
/* because the search algorithm can overwrite it between the	*/
/* time the tile is pushed and the time that it is popped.	*/

#define	PUSHTILE(tp, pl) \
	(tp)->ti_client = VISITPENDING; \
	STACKPUSH((ClientData)(pointertype)(pl | \
		((TileType)(spointertype)(tp)->ti_body & TT_SIDE)), extNodeStack); \
	STACKPUSH((ClientData)(pointertype)tp, extNodeStack)

#define POPTILE(tp, pl) \
	tp = (Tile *) STACKPOP(extNodeStack); \
	pl = (spointertype) STACKPOP(extNodeStack); \
	if (pl & TT_SIDE) { \
	   TiSetBody((tp), TiGetTypeExact(tp) | TT_SIDE); \
	   pl &= (~TT_SIDE); \
	} \
	else \
	   TiSetBody((tp), TiGetTypeExact(tp) & (~TT_SIDE))

/* Variations of "pushtile" to force a specific value on TT_SIDE */

#define PUSHTILEBOTTOM(tp, pl) \
	(tp)->ti_client = VISITPENDING; \
	STACKPUSH((ClientData)(pointertype)(pl | \
		((SplitDirection(tp)) ? 0 : TT_SIDE)), extNodeStack) ;\
	STACKPUSH((ClientData)(pointertype)tp, extNodeStack)

#define PUSHTILETOP(tp, pl) \
	(tp)->ti_client = VISITPENDING; \
	STACKPUSH((ClientData)(pointertype)(pl | \
		((SplitDirection(tp)) ? TT_SIDE : 0)), extNodeStack) ;\
	STACKPUSH((ClientData)(pointertype)tp, extNodeStack)

#define PUSHTILELEFT(tp, pl) \
	(tp)->ti_client = VISITPENDING; \
	STACKPUSH((ClientData)(pointertype)(pl), extNodeStack); \
	STACKPUSH((ClientData)(pointertype)tp, extNodeStack)

#define PUSHTILERIGHT(tp, pl) \
	(tp)->ti_client = VISITPENDING; \
	STACKPUSH((ClientData)(pointertype)(pl | TT_SIDE), extNodeStack); \
	STACKPUSH((ClientData)(pointertype)tp, extNodeStack)

/* ------------------------- Region finding --------------------------- */

extern Region *ExtFindRegions();

/* Filter functions for ExtFindRegions() */
extern Region *extTransFirst();		extern int extTransEach();
extern Region *extResFirst();		extern int extResEach();
extern Region *extNodeFirst();		extern int extNodeEach();
extern Region *extHierLabFirst();	extern int extHierLabEach();

extern Tile *extNodeToTile();

/* -------- Search for matching node in another ExtTree ---------- */

/*
 * NODETONODE(nold, et, nnew)
 *	NodeRegion *nold;
 *	ExtTree *et;
 *	NodeRegion *nnew;
 *
 * Like extNodeToTile(), but leaves nnew pointing to the node associated
 * with the tile we find.
 */
#define	NODETONODE(nold, et, nnew) \
	if (1) { \
	    Tile *tp; \
 \
	    (nnew) = (NodeRegion *) NULL; \
	    tp = extNodeToTile((nold), (et)); \
	    if (tp && extHasRegion(tp, extUnInit)) \
		(nnew) = (NodeRegion *) extGetRegion(tp); \
	}

/* -------------------- Miscellaneous procedures ---------------------- */

extern char *extNodeName();
extern NodeRegion *extBasic();
extern NodeRegion *extFindNodes();
extern ExtTree *extHierNewOne();
extern int extNbrPushFunc();

/* --------------------- Miscellaneous globals ------------------------ */

extern int extNumFatal;		/* Number fatal errors encountered so far */
extern int extNumWarnings;	/* Number warning messages so far */
extern CellUse *extParentUse;	/* Dummy use for def being extracted */
extern ClientData extNbrUn;	/* Ditto */

extern NodeRegion *glob_subsnode;	/* Substrate node for cell def */
extern NodeRegion *temp_subsnode;	/* Substrate connection to subcell */

    /*
     * This is really a (Stack *), but we use the struct tag to avoid
     * having to include stack.h in every .c file.  Used in the non-recursive
     * flooding algorithm.
     */
extern struct stack *extNodeStack;

/* ------------------ Connectivity table management ------------------- */

/*
 * The following is true if tile types 'r' and 's' are connected
 * according to the connectivity table 'tbl'
 */
#define extConnectsTo(r, s, tbl)	( TTMaskHasType(&(tbl)[(r)], (s)) )

/* -------------------------------------------------------------------- */

#include "extDebugInt.h"

#endif /* _EXTRACTINT_H */
