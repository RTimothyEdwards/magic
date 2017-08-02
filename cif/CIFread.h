/*
 * CIFread.h --
 *
 * This file contains definitions used by the CIF reader, but not
 * by the CIF writing code.  The definitions are only used internally
 * to this module.
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
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/cif/CIFread.h,v 1.3 2010/08/25 17:33:55 tim Exp $
 */

#ifndef _CIFREAD_H
#define _CIFREAD_H

#include "cif/CIFint.h"

/* The structures below are built up by CIFreadtech.c to describe
 * various styles for reading CIF.
 */

/* The following structure describes a sequence of geometric
 * operations used to produce information for a single Magic
 * layer.  There may be several of these structures for the
 * same Magic layer;  in that case, the results end up being
 * OR'ed together.
 */

typedef struct
{
    TileType crl_magicType;	/* Magic layer to paint results. */
    CIFOp *crl_ops;		/* List of operations to generate
				 * info for Magic layer.
				 */
    int crl_flags;		/* Miscellaneous flags (see below). */
} CIFReadLayer;

/* The CIFReadLayer flags are:
 *
 * CIFR_SIMPLE: Means this layer is a simple one, coming from only
 *		a single OR operation, so it can be handled specially.
 * CIFR_TEMPLAYER: Means this layer is a temporary CIF layer, and that
 *		the "crl_magicType" should be interpreted as a CIF layer.
 */

#define CIFR_SIMPLE 1
#define CIFR_TEMPLAYER 2

/* The following structure defines a complete CIF read-in style.
 * The constant MAXCIFRLAYERS must be less than TT_MAXTYPES, and
 * is used both as the largest number of distinct CIF layer names
 * in all read styles, and as the larges number of distinct "layer"
 * commands in any one read style.
 */

#define MAXCIFRLAYERS (TT_MAXTYPES - 1)

/*
 * To avoid the large memory demands of maintaining all CIF styles in
 * memory, we keep only the style names and re-read the technology file
 * as necessary
 */

typedef struct cifrkeep
{
    struct cifrkeep	*crs_next;
    char		*crs_name;
} CIFReadKeep;

typedef struct cifrstyle
{
    char  crs_status;		/* Status:  Loaded, not loaded, or pending. */
    char *crs_name;		/* Name for this style of CIF input. */
    TileTypeBitMask crs_cifLayers;
				/* Mask of CIF layers understood in
				 * this style.
				 */
    int crs_nLayers;		/* Number of CIFReadLayers involved. */
    int crs_scaleFactor;	/* Number of CIF units per Magic unit. */
    int crs_multiplier;		/* crs_scaleFactor / crs_multiplier =
				 * units in traditional centimicrons.
				 * So if crs_multiplier = 10, CIF units
				 * are in nanometers (millimicrons).
				 */
    int crs_gridLimit;		/* Input is considered off-grid if on
				 * a pitch less than crs_gridLimit CIF
				 * units, and input will be snapped to
				 * grid rather than scaling the grid
				 * to accomodate the data.  0 = no limit.
				 */

    TileType crs_labelLayer[MAXCIFRLAYERS];
				/* Gives the Magic layer to use for labels
				 * on each possible CIF layer.
				 */
    bool crs_labelSticky[MAXCIFRLAYERS];
				/* Marker if label layer makes sticky labels */
    CIFReadLayer *crs_layers[MAXCIFRLAYERS];
    HashTable cifCalmaToCif;    /* Table mapping from Calma layer numbers to
                                 * CIF layers
				 */
    int crs_flags;		/* Mask of boolean cif-reading options */
} CIFReadStyle;

/* option bitmasks used in crs_flags */
#define CRF_IGNORE_UNKNOWNLAYER_LABELS	1
#define CRF_NO_RECONNECT_LABELS		2

/* Methods to deal with fractional results of conversion from CIF to magic */
/* units (see routine CIFScaleCoord() for details).			   */

#define COORD_EXACT	0
#define COORD_HALF_U	1
#define COORD_HALF_L	2
#define COORD_ANY	3

/* For parsing CIF, we need to keep track of paths (wire locations
 * or polygon boundaries.  These are just linked lists of points.
 */

#define CIF_ZERO        0
#define CIF_LEFT        1
#define CIF_UP          2
#define CIF_RIGHT       3
#define CIF_DOWN        4
#define CIF_DIAG        5

/* Specific diagonal directions */
#define CIF_DIAG_UL     5
#define CIF_DIAG_UR     6
#define CIF_DIAG_DL     7
#define CIF_DIAG_DR     8

typedef struct cifpath
{
    Point cifp_point;		/* A point in the path. */
    struct cifpath *cifp_next;	/* The next point in the path, or NULL. */
} CIFPath;

#define cifp_x cifp_point.p_x
#define cifp_y cifp_point.p_y

/* Procedures */

extern bool CIFParseBox(), CIFParseWire(), CIFParsePoly();
extern bool CIFParseFlash(), CIFParseLayer(), CIFParseStart();
extern bool CIFParseFinish(), CIFParseDelete(), CIFParseUser();
extern bool CIFParseCall(), CIFParseTransform(), CIFParseInteger();
extern bool CIFParsePath(), CIFParsePoint(), CIFParseSInteger();
extern void CIFSkipToSemi(), CIFSkipSep(), CIFSkipBlanks();
extern void CIFFreePath(), CIFCleanPath();
extern void CIFReadCellInit(), CIFReadCellCleanup();
extern LinkedRect *CIFPolyToRects();
extern Transform *CIFDirectionToTrans();
extern int CIFReadNameToType();

/* Variable argument procedures require complete prototype */

extern void CIFReadError(char *format, ...);
extern void CIFReadWarning(char *format, ...);

/* Variables shared by the CIF-reading modules, see CIFreadutils.c
 * for more details:
 */

extern int cifReadScale1, cifReadScale2;
extern int cifNReadLayers;
extern Plane *cifReadPlane;
extern Plane **cifCurReadPlanes;
extern TileType cifCurLabelType;
extern CIFReadStyle *cifCurReadStyle;
extern bool cifSubcellBeingRead;
extern CellDef *cifReadCellDef;
extern FILE *cifInputFile;
extern bool cifParseLaAvail;
extern int cifParseLaChar;

/* Macros to read characters, with one-character look-ahead. */

#define PEEK()	( cifParseLaAvail \
		? cifParseLaChar \
		: (cifParseLaAvail = TRUE, \
			cifParseLaChar = getc(cifInputFile)))

#define TAKE()	( cifParseLaAvail \
		? (cifParseLaAvail = FALSE, cifParseLaChar) \
		: (cifParseLaChar = getc(cifInputFile)))

#endif /* _CIFREAD_H */
