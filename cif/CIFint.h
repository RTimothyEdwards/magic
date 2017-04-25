/*
 * CIFint.h --
 *
 * Defines things shared internally by the cif module of Magic,
 * but not generally needed outside the cif module.
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/cif/CIFint.h,v 1.3 2008/12/04 17:10:29 tim Exp $"
 */

#ifndef _CIFINT_H
#define _CIFINT_H

#include "database/database.h"

/* The main data structure used in the cif module is a description
 * of how to generate CIF layers from the Magic tiles.  There may
 * be several different styles for generating CIF from the same Magic
 * information, e.g. for fabricating at different geometries.  Each
 * of these CIF styles involves three kinds of data.  A "CIFStyle"
 * record gives overall information such as the number of layers.
 * One "CIFLayer" gives overall information for each layer, and
 * then a list of one or more "CIFOp" records describes a sequence
 * of geometrical operations to perform to generate the layer.  This
 * data structure is built up by reading the technology file.
 */

/* A CIFOp starts from a partially-completed CIF layer, does something
 * to it, which may possibly involve some existing layers or temporary
 * layers, and creates the next stage of the partially-completed
 * CIF layer.  Example operations are to AND with some existing paint,
 * or to grow by a certain amount.
 */

typedef struct bloat_data
{
    int bl_plane;		/* Plane on which a bloat or squares
				 * operation is valid.
				 */
    int bl_distance[TT_MAXTYPES];
} BloatData;

typedef struct squares_data
{
    int sq_border;
    int sq_size;
    int sq_sep;
    int sq_gridx;		/* Only used for "squares-grid" */
    int sq_gridy;		/* Only used for "squares-grid" */
} SquaresData;

typedef struct slots_data
{
    int sl_sborder;		/* short tile side */
    int sl_ssize;
    int sl_ssep;
    int sl_lborder;		/* long tile side */
    int sl_lsize;
    int sl_lsep;
    int sl_offset;
} SlotsData;

typedef struct cifop
{
    TileTypeBitMask co_paintMask;/* Zero or more paint layers to consider. */
    TileTypeBitMask co_cifMask;	 /* Zero or more other CIF layers. */
    int co_opcode;		/* Which geometric operation to use.  See
				 * below for the legal ones.
				 */
    int co_distance;		/* Grow or shrink distance (if needed). */
    ClientData co_client;	/* Pointer to a BloatData, SquaresData, or
				 * SlotsData structure, or NULL.
				 */
    struct cifop *co_next;	/* Next in list of operations to perform. */
} CIFOp;

/* The opcodes defined so far are:
 *
 * CIFOP_AND -		AND current results with the layers indicated by
 *			the masks.
 * CIFOP_ANDNOT -	Wherever there is material indicated by the masks,
 *			erase those areas from the current results.
 * CIFOP_OR -		OR current results with the layers indicated by
 *			the masks.
 * CIFOP_GROW -		Grow the current results uniformly by co_distance.
 * CIFOP_GROW_G -	Grow the current results to snap to the indicated grid.
 * CIFOP_SHRINK -	Shrink the current results uniformly by co_distance.
 * CIFOP_BLOAT -	Find layers in paintMask, then bloat selectively
 *			according to bl_distance, and OR the results into
 *			the current plane
 * CIFOP_SQUARES -	Generates a pattern of squares (used for making
 *			contact vias.  Each square is co_distance large,
 *			the squares are separated from each other by
 *			co_distance, and they are inside the edge of
 *			the material by at least co_distance.
 * CIFOP_SLOTS -	Generate a pattern of rectangles (used for making
 *			slots and slot vias).  Similar to squares except
 *			for different dimensions in short and long tile
 *			dimensions. "0" for the long size indicates that
 *			the slot should extend the length of the tile
 *			minus the long-side border length.
 * CIFOP_BLOATMAX -	Like CIFOP_BLOAT, except whole side of tile gets
 *			bloated by same amount, which is max bloat from
 *			anywhere along side.  Bloats can be negative.
 * CIFOP_BLOATMIN -	Same as CIFOP_BLOAT, except use min bloat from
 *			anywhere along side.
 * CIFOP_BLOATALL -	Added 3/21/05---bloat to encompass all connected
 *			material of the indicated type(s).
 * CIFOP_BBOX -		Added 4/2/05---create a single rectangle encompassing
 *			the cell bounding box.  This involves no magic type
 *			layers but may itself be acted upon with grow/shrink
 *			rules.
 * CIFOP_NET -		Added 11/3/08---pull an entire electrical net into
 *			the CIF layer, selectively picking layers.
 * CIFOP_MAXRECT -	Reduce all areas to the largest internal fitting
 *			rectangle.
 * CIFOP_COPYUP -	Added 5/5/16---make and keep a copy the resulting layer,
 *			which will be painted into parent cells instead of the
 *			current cell.  This replaces the "fault" method.
 */

#define CIFOP_AND	1
#define CIFOP_OR	2
#define CIFOP_GROW	3
#define CIFOP_GROW_G	4
#define CIFOP_SHRINK	5
#define CIFOP_BLOAT	6
#define CIFOP_SQUARES	7
#define CIFOP_SLOTS	8
#define CIFOP_BLOATMAX	9
#define CIFOP_BLOATMIN	10
#define CIFOP_BLOATALL	11
#define CIFOP_ANDNOT	12
#define CIFOP_SQUARES_G	13
#define CIFOP_BBOX	14
#define CIFOP_NET	15
#define CIFOP_MAXRECT	16
#define CIFOP_COPYUP	17

/* Added by Tim 10/21/2004 */
/* The following structure is used to pass information on how to draw
 * contact subcell arrays for a specific magic contact tile type.  For
 * the GDS write routine, the GDS file (FILE *) is passed as the client
 * data.
 */

typedef struct cifsquaresinfo
{
    SquaresData	*csi_squares;	/* Information on how to generate squares */
    TileType	 csi_type;	/* Magic contact tile type */
    ClientData	 csi_client;	/* Used to pass output file info.	*/
} CIFSquaresInfo;

/* The following data structure contains all the information about
 * a particular CIF layer.
 */

typedef struct
{
    char *cl_name;		/* Name of layer. */
    CIFOp *cl_ops;		/* List of operations.  If NULL, layer is
				 * determined entirely by cl_initial.
				 */
    int cl_growDist;		/* Largest distance material may move in
				 * this layer from its original Magic
				 * position, due to grows.  Expressed
				 * in CIF units.  If this layer uses temp
				 * layers, this distance must include grows
				 * from the temp layers.
				 */
    int cl_shrinkDist;		/* Same as above, except for shrinks. */
    int cl_flags;		/* Bunches of flags:  see below. */
    int cl_calmanum;		/* Number (0-63) of this layer for output as
				 * Calma (GDS-II stream format), or -1 if
				 * this layer should not be output.
				 */
    int cl_calmatype;		/* Data type (0-63) for Calma output, or -1
				 * if this layer should not be output.
				 */
    int min_width;		/* the minimum width rule in centi-microns
				 * for the layer. This is used by Grow Sliver
				 * to generate drc correct parent slivers	
				 */
#ifdef THREE_D
    int	   cl_renderStyle;	/* Style to render CIF layer with */
    float  cl_height;		/* (rendered) height of CIF layer above substrate */
    float  cl_thick;		/* (rendered) thickness of CIF layer */
#endif

} CIFLayer;

/* The CIFLayer flags are:
 *
 * CIF_TEMP:	Means that this is a temporary layer used to build
 *		up CIF information.  It isn't output in the CIF file.
 * CIF_BBOX_TOP:  Indicates that the bounding box rectangle should
 *		only be generated if the cell is a top-level cell.
 */

#define CIF_TEMP 	1
#define CIF_BBOX_TOP	2

/* The following data structure describes a complete set of CIF
 * layers.  The number of CIF layers (MAXCIFLAYERS) must not be
 * greater than the number of tile types (TT_MAXTYPES)!!
 */

#define MAXCIFLAYERS (TT_MAXTYPES - 1)

typedef struct cifkeep
{
    struct cifkeep	*cs_next;
    char		*cs_name;
} CIFKeep;

typedef struct cifstyle
{
    char cs_status;		/* Status:  Loaded, not loaded, or pending. */
    char *cs_name;		/* Name used for this kind of CIF. */
    int cs_nLayers;		/* Number of layers. */
    int cs_radius;		/* Radius of interaction for hierarchical
				 * processing (expressed in Magic units).
				 */
    int cs_stepSize;		/* If non-zero, user-specified step size
				 * for hierarchical processing (in Magic
				 * units).
				 */
    int cs_gridLimit;		/* The limit of grid scaling.  This limits
				 * the use of "scalegrid" to prevent Magic
				 * from generating geometry smaller than the
				 * process minimum grid.
				 */
    int cs_scaleFactor;		/* Number of CIF units per Magic unit.
 				 * CIF units are usually centimicrons, but
				 * see cs_expander below.
				 */
    int cs_reducer;		/* Reduction factor (used only to reduce
				 * number of zeroes in CIF files and make
				 * file more readable).  Default of 1.
				 * Unused for GDS input/output.
				 */
    int cs_expander;		/* cs_scaleFactor / cs_expander = scale in 
				 * centimicrons.  Default of 1.  Value 10
				 * means cs_scaleFactor is measured in
				 * nanometers (millimicrons)
                                 */

    TileTypeBitMask cs_yankLayers;
				/* For hierarchical processing, only these
				 * Magic types need to be yanked.
				 */
    TileTypeBitMask cs_hierLayers;
				/* For hierarchical processing, only these
				 * CIF layers need to be generated.
				 */
    int cs_labelLayer[TT_MAXTYPES];
				/* Each entry corresponds to one Magic layer,
				 * and gives index of CIF real layer to use
				 * for labels attached to this Magic layer.
				 * -1 means no known CIF layer for this Magic
				 * layer.
				 */
    CIFLayer *cs_layers[MAXCIFLAYERS];
				/* Describes how to generate each layer.*/
    int cs_flags;		/* bitmask of boolean-valued output options */

} CIFStyle;

/* values for cs_flags */
#define CWF_PERMISSIVE_LABELS	0x01
#define CWF_GROW_SLIVERS	0x02
#define CWF_ANGSTROMS 		0x04
#define CWF_GROW_EUCLIDEAN	0x08
#define CWF_SEE_VENDOR		0x10	/* Override vendor GDS flag in cells */
#define CWF_NO_ERRORS		0x20	/* Do not generate error msgs and fdbk */

/* procedures */

extern bool CIFNameToMask();
extern void CIFGenSubcells();
extern void CIFGenArrays();
extern void CIFGen();
extern void CIFClearPlanes();
extern Plane *CIFGenLayer();
extern void CIFInitCells();
extern int cifHierCopyFunc();
extern void CIFLoadStyle();

/* Shared variables and structures: */

extern Plane *CIFPlanes[];		/* Normal place to store CIF. */
extern CIFKeep *CIFStyleList;		/* List of all CIF styles. */
extern CIFStyle *CIFCurStyle;		/* Current style being used. */
extern CellUse *CIFComponentUse;	/* Flatten stuff in here if needed. */
extern CellDef *CIFComponentDef;	/* Corresponds to CIFComponentUse. */
extern CellUse *CIFDummyUse;		/* Used to dummy up a CellUse for a
					 * def.
					 */

/* Valid values of CIFWarningLevel (see cif.h) */

typedef enum {CIF_WARN_DEFAULT, CIF_WARN_NONE, CIF_WARN_ALIGN,
	CIF_WARN_LIMIT, CIF_WARN_REDIRECT, CIF_WARN_END} CIFWarningTypes;

/* Statistics counters: */

extern int CIFTileOps;
extern int CIFHierTileOps;
extern int CIFRects;
extern int CIFHierRects;

/* Tables used for painting and erasing CIF. */

extern PaintResultType CIFPaintTable[], CIFEraseTable[];

/* Procedures and variables for reporting errors. */

extern int CIFErrorLayer;
extern CellDef *CIFErrorDef;
extern void CIFError();

/* The following determines the tile type used to hold the CIF
 * information on its paint plane.
 */

#define CIF_SOLIDTYPE 1
extern TileTypeBitMask CIFSolidBits;

#endif /* _CIFINT_H */
