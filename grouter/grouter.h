/*
 * grouter.h --
 *
 * Header file for the global router.
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
 * sccsid "@(#)grouter.h	4.5 MAGIC (Berkeley) 10/31/85"
 */

#ifndef _GROUTER_H
#define _GROUTER_H

#include "router/router.h"
#include "gcr/gcr.h"

/*
 * Density means the number of signals passing through a particular
 * point of a channel.  Since there are two directions in which
 * signals can run, there are two density maps: one for the number
 * of signals crossing horizontally across each vertical column
 * (d_RowsByCol), and the other for the number of signals crossing
 * vertically across each horizontal row (d_ColsByRow).  The maximum
 * density is just the maximum value for each of these two maps.
 * The indices of the two maps parallel those of the channel's
 * columns and rows, which include the pseudo-columns/rows at
 * the two ends of the channel as well as the ones in the interior.
 * However, the zero-th and last entries of the two maps are ignored.
 */
typedef struct
{
    short	*dm_value;	/* Contains dm_size elements, but element 0
				 * is unused.
				 */
    int		 dm_size;	/* Number of elements in dm_value array */
    int		 dm_max;	/* Maximum over dm_value[1 .. dm_size-1] */
    int		 dm_cap;	/* Capacity: if dm_value[i] exceeds dm_cap,
				 * then the channel is overcommitted at
				 * position (row or column) i.
				 */
} DensMap;

/*
 * Each channel's gcr_client field points to one of these structures
 * during global routing.  The information contained here includes
 * density maps and net penalties.  The net penalty gc_penalty is
 * set differently for each net.
 */
typedef struct
{
    DensMap		 gc_prevDens[2];/* Density prior to global routing */
    DensMap		 gc_postDens[2];/* Density including routed signals */
    struct czone	*gc_penList;	/* Penalties and areas applicable to
					 * this channel.  Updated for each
					 * net being routed.
					 */
} GlobChan;

/*
 * Each net's nnet_cdata field will point to one of the following structures,
 * which is used to remember several pieces of information.  The primary
 * one, which is the result of the penalty computation, is nc_pens, a
 * NULL-terminated list of NetPen structs that will be used during the
 * regular global routing phase.
 *
 * During the penalty computation, nc_paths (a NULL-terminated List of
 * the head GlPoint in each of several paths) is used to point to all
 * of the GlPoints that comprise the global routing of the net.
 */
typedef struct
{
    /* Holds onto previous nnet_cdata field; restored after global routing */
    ClientData		 nc_saveClient;

    /* The result of the penalty computation phase */
    struct czone	*nc_pens;	/* Penalty for each congested zone */

    /* Used only during the penalty computation phase */
    struct list		*nc_paths;	/* List of heads of GlPoint paths */
} NetClient;

/*
 * The following structure represents a congested region of a channel.
 * Such a region is one where the number of nets that want to pass
 * through it is greater than the number of available tracks or
 * columns.  There can be two kinds of region: row or column.
 * The cz_type field tells which kind of region it is, and cz_lo
 * and cz_hi are the pin coordinates (not the edit coordinates)
 * of the range of pins this congested zone applies to.
 *
 * A congested zone is as large a contiguous region of overcommitted
 * tracks or columns as possible.
 *
 * The cz_penalty field is used in several different ways.  During the
 * penalty computation phase of global routing, it is used to store the
 * incremental cost for a net to avoid this congested region.  In the
 * nc_pens list of a NetClient (after penalties have been computed),
 * it represents the penalty that should be applied to a net for
 * using this congested zone.
 */
typedef struct czone
{
    struct chan		*cz_chan;	/* Which channel is affected */
    int			 cz_type;	/* See below */
    int			 cz_lo, cz_hi;	/* Inclusive range of pin coordinates
					 * that are congested.
					 */
    int			 cz_penalty;	/* Penalty for this net using cz_chan */
    struct netset	*cz_nets;	/* Nets passing through here */
    struct czone	*cz_next;	/* Next penalty on list */
} CZone;

/* Types of congested zones */
#define	CZ_ROW	0	/* Cz_lo and cz_hi are row coordinates */
#define	CZ_COL	1	/* Cz_lo and cz_hi are column coordinates */

/*
 * Used in building up sets of nets affected by a given congested zone.
 * It is used to store the cost of that net avoiding that zone.
 */
typedef struct netset
{
    struct nlNet	*ns_net;	/* Net belonging to the set */
    int			 ns_cost;	/* Cost of avoiding our CZone */
    struct netset	*ns_next;	/* Next in NULL-terminated list */
} NetSet;

/*
 * In order to prevent the channel router from trying to connect two
 * independent segments of a net that both happen to pass through a
 * channel, if they are already connected elsewhere, the global router
 * assigns a "segment" identifier to each distinct two-point component
 * of the global route.  The following structure bundles together a
 * unique identifier for a net with this segment identifier, using
 * this information to set up the pins for channel routing.
 */
typedef struct
{
    struct nlNet	*netid_net;
    int			 netid_seg;
} NetId;

#define	SAMENET(p, nId, sId) \
    ( ((NLNet *) (p)->gcr_pId == (nId)) && (p)->gcr_pSeg == (sId))

/*
 * --------------------------------------------------------------------
 *
 * GlPoint --
 *
 * This is the key data structure used by the global router.
 * It is used to remember intermediate crossing points along a path
 * from a source terminal to a destination terminal.  Each GlPoint
 * points back (via gl_path) to the point from which it was reached.
 * This structure forms a forest of inverted trees, rooted at each
 * of the starting points for the route.  The leaves in these inverted
 * trees (those GlPoints that aren't pointed to by any others) represent
 * partial paths that may eventually be extended to the destination
 * point for a global route.  The cost stored in each GlPoint tells
 * how expensive it was to reach that GlPoint from the starting point
 * of the path used to reach it.
 *
 * The coordinates of the crossing point and the channel containing it
 * are stored in the GCRPin pointed to by gl_pin.
 *
 * --------------------------------------------------------------------
 */

typedef struct glpoint
{
   GCRPin		*gl_pin;	/* Crossing point */
   Tile			*gl_tile;	/* Tile for gl_pin in glChanPlane */
   struct glpoint	*gl_path;	/* Point we came from */
   int			 gl_cost;	/* Cost of path to get here */
} GlPoint;

/*
 * --------------------------------------------------------------------
 * To allow easy freeing of GlPoints, we allocate them from our own
 * special heap which can be freed in one fell swoop.
 * --------------------------------------------------------------------
 */
#define POINTSPERSEG	200	/* Number of GlPoints per segment */

typedef struct glPage
{
    struct glPage	*glp_next;
    int			 glp_free;
    GlPoint		 glp_array[POINTSPERSEG];
} GlPage;

/* First, last, and current GlPages on list for allocating GlPoints */
extern GlPage *glPathFirstPage;
extern GlPage *glPathLastPage;
extern GlPage *glPathCurPage;

/* -------------------------- Miscellaneous --------------------------- */

/* TRUE if pin is available for use as a crossing point */
#define	PINOK(pin)	((pin)->gcr_pId == NULL && (pin)->gcr_linked)

/* ------- Plane holding channel structure during global routing ------ */

/*
 * Types of tiles in glChanPlane are CHAN_NORMAL, CHAN_HRIVER, CHAN_VRIVER,
 * or CHAN_BLOCKED (defined here).
 */
#define	CHAN_BLOCKED	3	/* Unusable area */

/* Macro to test a tile for penetrability */
#define	NOTBLOCKED(tp)	(TiGetType(tp) != CHAN_BLOCKED)

/* Plane to hold channel information */
extern Plane *glChanPlane;

/* Dummy celldef whose PL_DRC_CHECK plane is identical to glChanPlane above */
extern CellDef *glChanDef;
extern CellUse *glChanUse;

/* ------------------------ Intramodule exports ----------------------- */

    /* Internal procedures */
GlPoint *glPathNew();
GlPoint *glPathCopyPerm();
GlPoint *glProcessLoc();
Tile *glChanPinToTile();
void glCrossMark();

    /* Penalties for crossings */
extern int glJogPenalty;
extern int glObsPenalty1;
extern int glObsPenalty2;
extern int glNbrPenalty1;
extern int glNbrPenalty2;
extern int glOrphanPenalty;
extern int glChanPenalty;
extern bool glPenaltiesScaled;

    /* Statistics */
extern int glCrossingsUsed;
extern int glCrossingsAdded;
extern int glCrossingsExpanded;
extern int glCrossingsSeen;
extern int glGoodRoutes;
extern int glBadRoutes;
extern int glNoRoutes;

    /* Used when finding a global signal path */
extern Heap glMazeHeap;		/* Search point heap */

    /* Debugging information */
extern bool glInitialized;	/* TRUE if registered with debug module */
extern ClientData glDebugID;	/* Our identity with the debugging module */
#include "grouteDebug.h"	/* Can add flags without total recompile */

#endif /* _GROUTER_H */
