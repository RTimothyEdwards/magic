/*
 * databaseInt.h --
 *
 * Definitions internal to the database module.
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
 * Needs to include: magic.h, tile.h, database.h
 *
 * rcsid $Header: /usr/cvsroot/magic-8.0/database/databaseInt.h,v 1.3 2010/06/24 12:37:15 tim Exp $
 */

#ifndef _DATABASEINT_H
#define _DATABASEINT_H

#include "database/database.h"

/* ----------- Argument to area search when writing out cell ---------- */

struct writeArg
{
    char       *wa_name;	/* Filename of output file */
    FILE       *wa_file;	/* File to which to output */
    TileType	wa_type;	/* Type of tile being searched for */
    bool	wa_found;	/* Have any tiles been found yet? */
    int		wa_reducer;	/* Scale factor for all geometry */
    int		wa_plane;	/* Current plane being searched */
};

/* --------------------- Undo info for painting ----------------------- */

/* The following is the structure of the undo info saved for each tile */
typedef struct
{
    Rect	 pue_rect;	/* Rectangle painted/erased */
    TileType     pue_oldtype;   /* Material erased */
    TileType     pue_newtype;   /* Material painted */
    char	 pue_plane;	/* Plane index affected */
} paintUE;

/* The following is the structure of the undo info saved for a split tile */
typedef struct
{
    Point	sue_point;	/* lower-left position of split/joined tile */
    int		sue_splitx;	/* x position of split/join */
    char	sue_plane;	/* Plane of material that was split/joined */
} splitUE;

/* -------------- Codes for undo of cell use operations --------------- */

#define UNDO_CELL_CLRID		0	/* Clear use id */
#define UNDO_CELL_SETID		1	/* Set use id */
#define UNDO_CELL_PLACE		2	/* Create and place cell use */
#define UNDO_CELL_DELETE	3	/* Delete and destroy cell use */
#define UNDO_CELL_LOCKDOWN	4	/* Lock down a cell use	*/

/* --------------- Default types and planes, and name lists ----------- */

/*
 * Type or plane names.
 * These are invisible outside of the technology module
 * except via DBTechNameType() and DBTechNamePlane().
 * The first name in any list is by convention pointed
 * to by DBTypeLongNameTbl[] or DBPlaneLongNameTbl[]
 * respectively.
 */
typedef struct namelist
{
    struct namelist	*sn_next;	/* Next name in table */
    struct namelist	*sn_prev;	/* Previous name in table */
    char		*sn_name;	/* Text of name */
    ClientData		 sn_value;	/* Value (TileType or plane number) */
    bool		 sn_primary;	/* If TRUE, this is the primary name */
} NameList;

typedef struct
{
    int		 dp_plane;	/* Internal index for this plane */
    char	*dp_names;	/* List of comma-separated names */
} DefaultPlane;

typedef struct
{
    TileType	 dt_type;	/* This type's number */
    int		 dt_plane;	/* Plane on which this type resides */
    char	*dt_names;	/* List of comma-separated names.  The first
				 * is the "long" name of the type.
				 */
    bool	dt_print;	/* TRUE if layer is to be printed by
				 * DBTechPrintTypes.  These are layers
				 * that user would normally paint.
				 */
} DefaultType;

extern NameList dbTypeNameLists;		/* Type abbreviations */
extern NameList dbPlaneNameLists;		/* Plane abbreviations */
extern DefaultPlane dbTechDefaultPlanes[];	/* Builtin planes */
extern DefaultType dbTechDefaultTypes[];	/* Builtin types */

/* ------------------- Layer composition tables ----------------------- */

/*
 * Contacts in Magic are funny beasts.  Instead of representing the via
 * holes separately from the two layers they connect, Magic bundles them
 * into a single "type" that implies the presence of adjacent vias.
 * In the discussion that follows, "contact" means the aggregate described
 * by a line in the "contacts" section of the technology file, while "type"
 * has its usual meaning, namely a TileType.  "Painting a contact" means
 * painting its primary type, namely the one that appeared in the "types"
 * section of the technology file (as opposed to any automatically generated
 * tile types).
 *
 * Magic represents contacts between tile planes by storing the tile type
 * on each plane, each of which is referred to as an "image" of the
 * particular type of contact on its respective plane.  The primary contact
 * type indicates the planes it connects to, and also the "residues" on
 * each connected plane (residues are the types that would be present if
 * there were no contact).
 *
 * When the technology file is read, the "types" section defines the tile
 * type names that users will paint and erase.  Contacts can be painted
 * and erased by a single name, but actually consist of several tiles;
 * the "contacts" section provides the information needed to automatically
 * generate the tiles needed to represent a contact with the specified
 * connectivity.
 *
 * Magic's scheme has a couple of important properties.  Each contact
 * defined in the technology file determines a set of planes that will
 * be painted with the type when that contact is painted over empty
 * space.
 *
 * The LayerInfo structure is used primarily to store information about 
 * the various types of contacts, although it contains degenerate information
 * about non-contact types as well.
 */

typedef struct
{
    TileType		 l_type;	/* Back-index into dbLayerInfo[] */
    bool		 l_isContact;	/* TRUE if this layer has images */

    /* Residues of this contact on its contacted planes. */ 
    TileTypeBitMask	 l_residues;

    /* Mask of connected planes, including this one */
    PlaneMask		 l_pmask;
} LayerInfo;

extern LayerInfo dbLayerInfo[];
extern LayerInfo *dbContactInfo[];
extern int dbNumContacts;
extern int dbNumImages;

/* Macros for above table */
#define	LayerPlaneMask(t)	dbLayerInfo[t].l_pmask
#define	IsContact(t)		dbLayerInfo[t].l_isContact

/* --------------------- Composition rule tables ---------------------- */

/* Saved contact compose/erase rules */
typedef struct
{
    TileType	 rp_a, rp_b;	/* Two types in pair */
} TypePair;

typedef struct
{
    int		 r_ruleType;	/* Kind of rule (RULE_* below) */
    TileType	 r_result;	/* Result type */
    int		 r_npairs;	/* Number of type pairs in rule */
    TypePair	 r_pairs[NT];	/* Pairs of types in rule */
} Rule;

/*
 * Types of rules in the compose section of a technology file
 * (represented in the Rule structure above).
 */
#define	RULE_DECOMPOSE	0
#define	RULE_COMPOSE	1
#define	RULE_PAINT	2
#define	RULE_ERASE	3

extern int dbNumSavedRules;
extern Rule dbSavedRules[];

/* -------------------- Internal procedure headers -------------------- */

extern void DBUndoPutLabel();
extern void DBUndoEraseLabel();
extern void DBUndoCellUse();
extern void DBStampMismatch();
extern void DBTechAddNameToType();

extern void dbComputeBbox();
extern void dbFreeCellPlane();
extern void dbFreePaintPlane();
extern bool dbTechAddPaint();
extern bool dbTechAddErase();
ClientData dbTechNameLookup();
ClientData dbTechNameLookupExact();

/* --------------- Internal database technology variables ------------- */

/*
 * Macros to set the paint result tables.
 * The argument order is different from the index order in
 * the tables, for historical reasons.
 *
 * Usage:
 *	dbSetPaintEntry(oldType, paintType, planeNum, resultType)
 *	dbSetEraseEntry(oldType, paintType, planeNum, resultType)
 *	dbSetWriteEntry(oldType, paintType, resultType)
 */
#define	dbSetPaintEntry(h,t,p,r) 	(DBPaintResultTbl[p][t][h] = r)
#define	dbSetEraseEntry(h,t,p,r)	(DBEraseResultTbl[p][t][h] = r)
#define	dbSetWriteEntry(h,t,r)		(DBWriteResultTbl[t][h] = r)

extern TileTypeBitMask dbNotDefaultEraseTbl[];
extern TileTypeBitMask dbNotDefaultPaintTbl[];

#define	IsDefaultErase(h, e)	(!TTMaskHasType(&dbNotDefaultEraseTbl[h], e))
#define	IsDefaultPaint(h, p)	(!TTMaskHasType(&dbNotDefaultPaintTbl[h], p))

/*
 * Macros to determine whether painting or erasing type s affects
 * type t on its home plane.  The check for t != TT_SPACE is because
 * TT_SPACE has no specific home plane and is handled specially.
 */
#define	PAINTAFFECTS(t, s) \
	((t) != TT_SPACE && DBStdPaintEntry((t), (s), DBPlane(t)) != (t))
#define	ERASEAFFECTS(t, s) \
	((t) != TT_SPACE && DBStdEraseEntry((t), (s), DBPlane(t)) != (t))

#endif /* _DATABASEINT_H */
