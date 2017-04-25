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
    unsigned char     drcc_flags;	/* Miscellaneous flags, see below. */
    int		      drcc_edgeplane;	/* Plane of edge */
    int		      drcc_plane;	/* Index of plane on which to check
					 * legal types. */
    char              *drcc_why;	/* Explanation of error found */
    struct drccookie  *drcc_next;
} DRCCookie;

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

#define		DRC_FORWARD		0x00
#define		DRC_REVERSE		0x01
#define		DRC_BOTHCORNERS		0x02
#define		DRC_TRIGGER		0x04
#define		DRC_BENDS		0x08
#define		DRC_OUTSIDE		0x08	// Note: Shared with DRC_BENDS
#define		DRC_AREA		0x10
#define		DRC_MAXWIDTH		0x20
#define		DRC_RECTSIZE		0x40
#define		DRC_ANGLES		0x80
#define 	DRC_NONSTANDARD		(DRC_AREA|DRC_MAXWIDTH|DRC_RECTSIZE|DRC_ANGLES)

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
    CellDef		*dcl_def;
    int			dcl_count;
    struct drccountlist	*dcl_next;
} DRCCountList;

/* Structure used to keep information about the current DRC style */

typedef struct drckeep
{
    struct drckeep	*ds_next;
    char		*ds_name;
} DRCKeep;

/*
 * DRC "why" strings are potentially referred to hundreds of times by
 * DRC cookies in the rule table.  Rather than creating hundreds of
 * copies of each string, we create just one copy and let all the cookies
 * point to that one copy.
 *
 * Since we can't free these shared "why" strings when we delete a cookie,
 * we keep a list of these strings and free them all when convenient.
 */
    
typedef struct drcwhylist
{
    char                * dwl_string;
    struct drcwhylist   * dwl_next;
} drcWhyList;      
 
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
    drcWhyList		*DRCWhyList;
    PaintResultType	DRCPaintTable[NP][NT][NT];
} DRCStyle;

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

/*
 * Exported procedures
 */

extern int DRCGetDefaultLayerWidth();
extern int DRCGetDefaultLayerSpacing();
extern int DRCGetDefaultLayerSurround();

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
