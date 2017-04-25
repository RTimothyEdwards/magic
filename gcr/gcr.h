/*
 * gcr.h --
 *
 * Routines to implement a modified version of Rivest's Greedy Router.
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/gcr/gcr.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _GCR_H
#define _GCR_H

#include "utils/magic.h"

/* GCRPin:  One of these for each pin location along each edge of a channel.
 *	All of the pins for a given net are stored on a doubly-linked list
 *	for the net, sorted from left to right and bottom to top.  Stored
 *	with the pin are its track and column coordinates (not the actual
 *	locations of terminals).
 *
 *	A pin also stores crossing information for use by the global router,
 *	namely gcr_cost (the best cost known so far to reach this pin),
 *	gcr_side (GEO_NORTH, GEO_SOUTH, GEO_EAST, or GEO_WEST, giving the
 *	side of the channel that the pin lies on), and gcr_linked, pointing
 *	to the pin (in the neighboring channel) that shares the same crossing
 *	point as this one.
 *
 *	x= 0  	    means the pin is at the left edge of the channel.
 *	x= length+1 means the pin is at the right edge of the channel.
 *	y= 0 	    means the pin is at the bottom edge of the channel.
 *	y= width+1  means the pin is at the top edge of the channel.
 *
 *	A net id is in two parts, a net id (gcr_pId) and a segment
 *	id (gcr_pSeg).  During global routing the net id is an integer
 *	net id, not a pointer to a struct (it doesn't turn into a
 *	pointer until just before channel routing).  The segment id
 *	distinguishes parts of a net which are not connected within a
 *	channel because they are connected elsewhere (this prevents
 *	the router from generating loops in the routing).
 */
typedef struct pin
{
    int			 gcr_x, gcr_y;	/* Column and track coordinates */

    /* Information on nearest obstacle */
    int			 gcr_pFlags;	/* 0=clear,1=Obstacle,2=Hazard */
    short		 gcr_pSize;	/* Size of nearest obstacle */
    short		 gcr_pDist;	/* Distance to nearest obstacle */

    /*
     * Net id and segment id assigned to this pin.
     * Special semantics:
     *	gcr_pId == GCR_BLOCKEDNETID means this pin is blocked.
     *	gcr_pSeg == GCR_STEMSEGID means this pin is a stem tip;
     *		gcr_pId gives the net to which the stem tip belongs.
     */
    int                  gcr_pSeg;	/* Id naming part of a net id */
    struct gcrnet	*gcr_pId;	/* Net structure for pin's net */

    /*
     * These fields link available pins along the side of a channel
     * during global routing, and link pins in a net during channel
     * routing.
     */
    struct pin		*gcr_pNext;	/* Next pin to the right or up */
    struct pin		*gcr_pPrev;	/* Next pin left or down */

    /*
     * Stuff for the global router.
     * This is NOT valid in transformed channels, so should
     * not be used by the channel router.
     */
    int			 gcr_cost;	/* Used during global routing */
    struct chan		*gcr_ch;	/* Channel to which this pin belongs */
    int			 gcr_side;	/* Side of channel this pin lies on */
    struct pin		*gcr_linked;	/* Pin (in channel abutting this one)
					 * sharing same crossing point.
					 */
    Point		 gcr_point;	/* Point along channel boundary
					 * in edit cell coords.
					 */
} GCRPin;

#define	GCR_BLOCKEDNETID	((GCRNet *) -1)
#define	GCR_STEMSEGID		(-1)

/* GCRNet:  One structure for each net to be routed.  Nets have pointers,
 * 	to their leftmost and rightmost pins.  The dist field stores the
 *	distance from an unsplit rising or falling active net's current
 *	track to its next connection.  Nets are linked so they may be
 *	freed when the routing is complete.
 *
 *	Each net_id/seg_id pair generates a unique gcrnet struct.
 */
typedef struct gcrnet
{
    int			 gcr_Id;	/* Integer identifying the net */
    int			 gcr_dist;	/* Distance to target track +/- */
    int			 gcr_sortKey;	/* Use to prioritize nets */
    int	  	    	 gcr_track;	/* Track index, used for working
					 * storage in colInit.
					 */
    GCRPin		*gcr_lPin;	/* Leftmost pin of net */
    GCRPin		*gcr_rPin;	/* Rightmost pin of net */
    struct gcrnet	*gcr_next;	/* Next on a linked list of nets */
} GCRNet;


/* GRColEl:  Used in array form, this struct stores horizontal and vertical
 *	wiring for the current column as it is routed.  Locations 1..width are
 *	valid tracks, while 0 and width+1 are wiring to channel top and bottom.
 *
 *	gcr_hi and gcr_lo are used to maintain ordered doubly linked lists of
 *	all tracks occupied by the same net.
 *
 *	gcr_hOk and gcr_lOk are tricky.  They are needed because we
 *	artificially split nets near the end of a channel if there
 *	are multiple end connections.  The net must stay split, but
 *	what's tricky is that several groups of tracks may exist that
 *	are connected within the groups, but the groups haven't been
 *	connected together yet.  These flags keep track of this information.
 *	For example, if gcr_hOk is TRUE but gcr_lOk is false, it
 *	means we don't need to collapse this track with the next one
 *	up, but we do need to join this track with the next one down.
 *	These flags are only used near the ends of channels, and are
 *	normally FALSE.
 */
typedef struct gcrColEl
{
    GCRNet *	gcr_h;		/* Net for horizontal wiring		*/
    GCRNet *	gcr_v;		/* Net for vertical wiring		*/
    int 	gcr_hi;		/* Next higher track for net gcr_h	*/
    int   	gcr_lo;		/* Next lower track for net gcr_h	*/
    bool	gcr_hOk;	/* TRUE if don't need to collapse up	*/
    bool	gcr_lOk;	/* TRUE if don't need to collapse dn	*/
    int		gcr_flags;	/* Flags from the "result" array	*/
    GCRNet *	gcr_wanted;	/* Net that wants this track for end pin*/
} GCRColEl;


/* GCRChannel:
 *
 * Represents all information concerning a channel to be routed.
 * In order to handle boundary conditions more easily, the grid
 * arrays, such as gcr_lCol and gcr_result hold an extra grid
 * point's worth on each side:  the grids run from 0 through
 * length+1 in x, and from 0 through width+1 in y.  The actual
 * channel is from 1..length and 1..width
 */

#define	CHAN_NORMAL	0
#define	CHAN_HRIVER	1
#define	CHAN_VRIVER	2

typedef struct chan
{
/* Description of the channel	 */
    int		 gcr_type;	/* CHAN_NORMAL (0) if a normal channel,
				 * CHAN_HRIVER if only horizontal river routes
				 * are allowed, CHAN_VRIVER if only vertical
				 * river routes are allowed.
				 */
    int		 gcr_length;	/* Number of columns in the channel */
    int		 gcr_width;	/* Number of tracks in the channel */
    Point	 gcr_origin;	/* Grid 0 point */
    Rect	 gcr_area;	/* Area of channel in edit cell coords */
    Transform	 gcr_transform;	/* Transform used when cell has been flipped
				 * or rotated.  Apply this transfrom to grid
				 * coords, scale by grid size, then displace
				 * by gcr_origin to get edit cell coords.
				 */

/* For use by the global router */
    short	*gcr_dRowsByCol;/* Horizontal density during global routing */
    short	*gcr_dColsByRow;/* Vertical density during global routing */
    short	 gcr_dMaxByCol;	/* Max horizontal density */
    short	 gcr_dMaxByRow;	/* Max vertical density */
#define	IDENSITY
#ifdef	IDENSITY
    short	*gcr_iRowsByCol;/* FOR DEBUGGING */
    short	*gcr_iColsByRow;/* FOR DEBUGGING */
#endif	/* IDENSITY */
    struct chan	*gcr_next;	/* For linked lists of channels */

/* Description of the connections */
    GCRPin	*gcr_tPins;	/* Pins at the top of the channel */
    GCRPin	*gcr_bPins;	/* Pins at the bottom of the channel */
    GCRPin	*gcr_lPins;	/* Pins at the left edge of the channel */
    GCRPin	*gcr_rPins;	/* Pins at the right edge of the channel */
    GCRNet	*gcr_nets;	/* Data for each net */

/* Working storage */
    GCRColEl	*gcr_lCol;	/* Column for vertical wiring */
    int		*gcr_density;	/* Density for each column */
    short      **gcr_result;	/* Array of columns, storing the result */

/* For hanging additional information in channels */
    ClientData	 gcr_client;	/* For hire */
    int		 gcr_orient;	/* Channel orientation */
} GCRChannel;

/* Macros that look like procedures */

/************************************************************************
 * Is1stPin(pin)
 *	GCRPin *	pin;
 *
 * Return TRUE if the indexed pin is the first pin on its list.
 */
#define Is1stPin(pin) ((pin)==(pin)->gcr_pId->gcr_lPin)

/************************************************************************
 * IsLstPin(pin)
 *	GCRPin *	pin;
 *
 * Return TRUE if the indexed pin is the last pin on its list.
 */
#define IsLstPin(pin) ((pin)==(pin)->gcr_pId->gcr_rPin)

/************************************************************************
 * GCRNearEnd(channel, column)
 *	GCRChannel * channel;
 *	int          column;
 *
 * Return TRUE if the column is within GCREndDist of the end of the channel.
 */
#define GCRNearEnd(ch,col) (((ch)->gcr_length+1-(col)) <= GCREndDist)

/************************************************************************
 * GCRPin1st(net)
 *	GCRNet * net;
 *
 * Return a pointer to the net's first pin.
 */
#define GCRPin1st(n) ((n)->gcr_lPin)

/************************************************************************
 * GCRPinNext(pin)
 *	GCRPin * pin;
 *
 * Return a pointer to a net's next pin.
 */
#define GCRPinNext(p) ((p)->gcr_pNext)

#define BLOCK(i)	(((i)&GCRBLKM) && ((i)&GCRBLKP))
#define CLEAR(i)	(!((i)&(GCRBLKM|GCRBLKP)))
#define HAS_M(i)	(((i)&GCRBLKM) && (!((i)&GCRBLKP)))
#define HAS_P(i)	(((i)&GCRBLKP) && (!((i)&GCRBLKM)))
#define IS_EMPTY(c,x,y)	((x)<0 ? TRUE : ((x)>(c)->gcr_length ? TRUE : \
			    CLEAR((c)->gcr_result[x][y])))


/*
 * Constants for the result array bits.  Metal and poly here refer to
 * the preferred materials for the long and short dimension of the
 * channel.
 */

    /*
     * The input routine sets these to show where the obstacles are.
     * These entries need to be present in these positions.
     */
#define	GCRBLKM	0x0001	/* 1 if the location is blocked	with metal */
#define GCRBLKP	0x0002 	/* 1 if the location is blocked with poly */

    /* The router sets these bits to show the wiring it produces */
#define GCRU	0x0004	/* 1 if connect from track upwards */
#define GCRR	0x0008	/* 1 if connect to the right */
#define GCRX	0x0010	/* 1 if metal/poly contact */

    /*
     * These bits are used to indicate whether obstacles block tracks
     * or columns for the density computation; they are reset to zero
     * after density initialization so they don't interfere with the
     * wiring bits above (which occupy the same bit positions and mean
     * something entirely different).
     */
#define	GCRBLKT	0x0004	/* Blocks a track */
#define	GCRBLKC	0x0008	/* Blocks a column */

    /* The obstacle identifier sets these to say how to avoid obstacles */
#define GCRVL	0x0020	/* 1 if the track should be vacated here */
#define GCRV2	0x0040	/* 1 if vacate here due to 2-layer obstacle */
#define GCRTC	0x0080	/* 1 if track contact needed */
#define GCRCC	0x0100	/* 1 if column contact needed */
#define GCRTE	0x0200	/* 1 if track can't be use in next column to rt */
#define GCRCE	0x0400	/* 1 if column ends beyond this point */

    /* The metal maximizer sets these bits */
#define GCRVM   0x0800	/* 1 if vertical poly changed to metal */
#define GCRXX   0x1000	/* 1 if via not deleted */               

    /* The hazard generation code uses these bits */
#define GCRVR	0x2000	/* 1 if hazard to nets entering from the right */
#define GCRVU	0x4000	/* 1 if hazard to nets entering from the top */
#define GCRVD	0x8000	/* 1 if hazard to nets entering from the bottom */
#define EMPTY	-1

/* Flag bits used to indicate obstacles or hazards over pins */
#define GCRCLR	  0	/* No hazard or obstacle over crossing */
#define GCRHAZRD  1	/* Hazard over crossing */
#define GCROBST   2	/* Obstacle over crossing */
#define GCRBLK    4	/* Blocked area over crossing */
#define GCRTCC    8	/* Track or column contact near crossing */

/* Clip a value (e.g, column or track number) to lie within a range */
#define	INRANGE(x, min, max) \
	((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

/* These are parameters the user sets to control the router. */
extern int	GCRMinJog;
extern int	GCREndDist;
extern int	GCRSteadyNet;
extern float	GCRObstDist;
extern int	GCRMinChannelSize;
extern bool	GcrShowMap;
extern bool	GcrShowEnd;
extern bool     GcrShowResult;
extern bool     GcrNoCheck;
extern bool     GcrDebug;


/* Procedures exported by the greedy router to the rest of the world: */

extern GCRChannel *GCRNewChannel();
extern void GCRFreeChannel();
extern GCRChannel *GCRRouteFromFile();
extern int GCRroute();
extern void GCRFlipLeftRight();
extern void GCRFlipXY();
extern void GCRNoFlip();
extern void GCRShow();

#endif /* _GCR_H */
