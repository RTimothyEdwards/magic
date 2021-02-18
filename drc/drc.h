/*
 * drc.h --
 *
 * Definitions for the DRC module.
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
 * Needs to include: magic.h database.h
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/drc/drc.h,v 1.6 2010/09/20 21:13:22 tim Exp $
 */

#ifndef _DRC_H
#define	_DRC_H

#include "database/database.h"

/* ----------------- component of DRC table ---------------------------- */

typedef struct drccookie
{
    int    	      drcc_dist;	/* Extent of rule from edge. */
    unsigned char     drcc_mod;		/* Fractional part of drcc_dist */
    int	      	      drcc_cdist;	/* Size of corner extension.* */
    unsigned char     drcc_cmod;	/* Fractional part of drcc_cdist */
    TileTypeBitMask   drcc_mask;	/* Legal types on RHS */
    TileTypeBitMask   drcc_corner;	/* Types that trigger corner check */
    unsigned short    drcc_flags;	/* Miscellaneous flags, see below. */
    int		      drcc_edgeplane;	/* Plane of edge */
    int		      drcc_plane;	/* Index of plane on which to check
					 * legal types. */
    int 	      drcc_tag;		/* Tag to explanation of error found */
    struct drccookie  *drcc_next;
} DRCCookie;

/* These DRC tags in DRCcookie are predefined. */
#define DRC_ARRAY_OVERLAP_TAG	1
#define DRC_OVERLAP_TAG		2
#define DRC_SUBCELL_OVERLAP_TAG	3
#define DRC_IN_SUBCELL_TAG	4
#define DRC_OFFGRID_TAG		5

/* *This is size "int" because it holds an area for DRC_AREA rules,	  */
/* and therefore may have twice the bit length of a normal rule distance. */

/* DRCCookie flags:
 * DRC_FORWARD:		Rule applies from left to right (or bottom to top).
 * DRC_REVERSE:		Rule applies from right to left (or top to bottom).
 * DRC_BOTHCORNERS:	Must make corner extensions in both directions.
 * DRC_OUTSIDE:		Rule applies only to the outside edge of the rule area.
 * DRC_TRIGGER:		Violation of rule triggers a secondary rule.
 *
 * All other flags denote special DRC rules that do not use the standard 4-way
 * edge processing.
 */

#define		DRC_FORWARD		0x000
#define		DRC_REVERSE		0x001
#define		DRC_BOTHCORNERS		0x002
#define		DRC_TRIGGER		0x004
#define		DRC_BENDS		0x008
#define		DRC_OUTSIDE		0x010
#define		DRC_AREA		0x020
#define		DRC_OFFGRID		0x040
#define		DRC_MAXWIDTH		0x080
#define		DRC_MAXWIDTH_BOTH       0x100
#define		DRC_RECTSIZE		0x200
#define		DRC_ANGLES		0x400
#define 	DRC_NONSTANDARD		(DRC_AREA|DRC_MAXWIDTH|DRC_RECTSIZE\
					 |DRC_ANGLES|DRC_OFFGRID)

/* More flags for indicating what the rule type represents */
#define		DRC_CIFRULE		0x400

#define	DRC_PENDING			0
#define DRC_UNPROCESSED 		CLIENTDEFAULT
#define DRC_PROCESSED 			1

/*
 * Background DRC (DRC Idle proc) for Tcl-based Magic
 */
#ifdef MAGIC_WRAPPER
enum { DRC_NOT_RUNNING, DRC_IN_PROGRESS, DRC_BREAK_PENDING };
extern unsigned char DRCStatus;
#endif

/*
 * States of the background checker.  This allows us to force an off state
 * during initialization while allowing the startup scripts to set the
 * state on or off.
 */
enum { DRC_SET_OFF, DRC_SET_ON, DRC_NOT_SET };

/* This is client data passed down through the various DRC checking
 * routines, and contains information about the area and rule being
 * checked.
 */
struct drcClientData
{
    CellDef	* dCD_celldef;		/* CellDef, plane and area to DRC. */
    int		  dCD_plane;
    Rect	* dCD_rect;
    Tile 	* dCD_initial;		/* Initial tile for search (left side
					 * for forward rules, right for reverse
					 * rules).
					 */
    Rect	* dCD_clip;		/* Clip error tiles against this. */
    int		* dCD_errors;		/* Count of errors found. */
    int		  dCD_radial;		/* Radial check for corner extensions */
    DRCCookie	* dCD_cptr;		/* Rule being checked. */
    Rect	* dCD_constraint;	/* Constraint area from rule. */
    Rect	* dCD_rlist;		/* Multiple constraints for triggered rules */
    int		  dCD_entries;		/* Number of constraints for triggered rules */
    void	(* dCD_function)(); 	/* Function to call for each
				    	 * error found. */
    ClientData	dCD_clientData;		/* Parameter for dCD_function */
};

/* Describes a cell whose contents require design-rule checking of
 * some sort.  These are linked together for processing by the
 * continuous checker.
 */
#define DRCYANK	"__DRCYANK__"	/* predefined DRC yank buffer */

typedef struct drcpendingcookie
{
    CellDef                 *dpc_def;
    struct drcpendingcookie *dpc_next;
} DRCPendingCookie;

/* Structure used to pass back lists of cell definitions and error tile counts */

typedef struct drccountlist
{
    CellDef             *dcl_def;
    int                 dcl_count;
    struct drccountlist *dcl_next;
} DRCCountList;

/* Structure used to keep information about the current DRC style */

typedef struct drckeep
{
    struct drckeep	*ds_next;
    char		*ds_name;
} DRCKeep;

/*
 * Structure defining a DRC style
 */

typedef struct drcstyle
{
    char		ds_status;	/* Status:  Loaded, not loaded, or pending */
    char		*ds_name;	/* Name of this DRC style */
    DRCCookie        	*DRCRulesTbl[TT_MAXTYPES][TT_MAXTYPES];
    TileTypeBitMask     DRCExactOverlapTypes;
    int			DRCScaleFactorN; /* Divide dist by this to get magic units */
    int			DRCScaleFactorD; /* Multiply dist by this to get magic units */
    int			DRCTechHalo;	/* largest action distance of design rules */
    int			DRCStepSize;	/* chunk size for decomposing large areas */
    unsigned short	DRCFlags;	/* Option flags */
    char		**DRCWhyList;	/* Indexed list of "why" text strings */
    int			DRCWhySize;	/* Length of DRCWhyList */
    PaintResultType	DRCPaintTable[NP][NT][NT];
} DRCStyle;

/* flag values used by DRCFlags */

/*  DRC_FLAGS_WIDEWIDTH_NONINCLUSIVE:  If set, indicates that the ruleset	*/
/*  defines "wide" as material of MORE THAN the given DRC rule width value, as	*/
/*  opposed to the default behavior, which is to define "wide" as material of	*/
/*  AT LEAST the given DRC rule width value.					*/

/* (Note that at least for now, there is no similar flag for "maxwidth" rules,	*/
/* which are always interpreted as inclusive, meaning that material of the	*/
/* exact width of the DRC rule width value is legal.)				*/

#define DRC_FLAGS_WIDEWIDTH_NONINCLUSIVE  0x01

/* Things shared between DRC functions, but not used by the
 * outside world:
 */

extern int  DRCstatEdges;	/* counters for statistics gathering */
extern int  DRCstatSlow;
extern int  DRCstatRules;
extern int  DRCstatTiles;
extern int  DRCstatInteractions;
extern int  DRCstatIntTiles;
extern int  DRCstatCifTiles;
extern int  DRCstatSquares;
extern int  DRCstatArrayTiles;

#ifdef	DRCRULESHISTO
#	define	DRC_MAXRULESHISTO 30	/* Max rules per edge for statistics */
extern int  DRCstatHRulesHisto[DRC_MAXRULESHISTO];
extern int  DRCstatVRulesHisto[DRC_MAXRULESHISTO];
#endif	/* DRCRULESHISTO */

extern int DRCTechHalo;		/* Current halo being used */
extern int DRCStepSize;		/* Current step size being used */
extern DRCPendingCookie * DRCPendingRoot;

extern unsigned char DRCBackGround;	/* global flag to enable/disable
				 * continuous DRC
			     	 */
extern bool DRCEuclidean;	/* global flag to enable/disable
				 * Euclidean distance measure
				 */
extern int  dbDRCDebug;
extern bool DRCForceReload;	/* TRUE if we have to reload DRC on a
				 * change of the CIF output style
				 */

extern DRCKeep  *DRCStyleList;	/* List of available DRC styles */
extern DRCStyle *DRCCurStyle;	/* Current DRC style in effect */
extern CellDef  *DRCdef;	/* Current cell being checked for DRC */
extern CellUse  *DRCuse, *DRCDummyUse;

/*
 * Internal procedures
 */

extern void drcPaintError();
extern void drcPrintError();
extern int drcIncludeArea();
extern int drcExactOverlapTile();
extern void drcInitRulesTbl();
extern void drcAssign();
extern void drcCifAssign();
extern int drcWhyCreate();

/*
 * Exported procedures
 */

extern int DRCGetDefaultLayerWidth();
extern int DRCGetDefaultLayerSpacing();
extern int DRCGetDefaultWideLayerSpacing();
extern int DRCGetDefaultLayerSurround();

extern int DRCInteractionCheck();
extern int drcArrayFunc();

extern void DRCTechInit();
extern bool DRCTechLine();
extern bool DRCTechAddRule();
extern void DRCTechStyleInit();
extern void DRCTechFinal();
extern void DRCTechRuleStats();
extern void DRCTechScale();
extern void DRCReloadCurStyle();

extern void DRCInit();
extern void DRCContinuous();
extern void DRCCheckThis();
extern void DRCRemovePending();
extern void DRCPrintRulesTable();
extern void DRCWhy();
extern void DRCPrintStats();
extern void DRCCheck();
extern DRCCountList *DRCCount();
extern int DRCFind();
extern void DRCCatchUp();
extern bool DRCFindInteractions();
extern int  DRCBasicCheck();
extern void DRCOffGridError();

extern void DRCPrintStyle();
extern void DRCSetStyle();
extern void DRCLoadStyle();

/* The following macro can be used by the outside world to see if
 * the background checker needs to be called.
 */

#ifdef MAGIC_WRAPPER
#define DRCHasWork ((DRCPendingRoot != NULL) && (DRCBackGround == DRC_SET_ON))
#else
#define DRCHasWork ((DRCPendingRoot != NULL) && (DRCBackGround != DRC_SET_OFF))
#endif

#endif /* _DRC_H */
