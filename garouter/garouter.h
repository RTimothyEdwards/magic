/*
 * garouter.h --
 *
 * Header file for the gate-array router.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/garouter/garouter.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _GAROUTER_H
#define _GAROUTER_H

#include "grouter/grouter.h"

/*
 * The following structure describes "simple stems" -- routes
 * from a terminal to a grid point that are of a very simple
 * style, involving a straight-across wire and at most one
 * contact at either end.
 */

typedef struct
{
    TileType	 sw_type;	/* Type of wire */
    Rect	 sw_long;	/* Wire all the way to dest */
    Rect	 sw_short;	/* Wire only to pin contact */
    Rect	 sw_pinStub;    /* Wire from pin contact to pin */
    bool	 sw_longOK;	/* Space exists for long wire */
    bool 	 sw_shortOK;	/* Space exists for short wire (implied
				 * if sw_longOK is TRUE).
				 */
} SimpleWire;

typedef struct
{
    /* Terminal */
    TileType		 ss_termType;
    Rect		 ss_termArea;

    TileTypeBitMask	 ss_termMask;	/* If ss_termType is a contact type,
					 * this mask contains both metal and
					 * poly; otherwise, it contains just
					 * ss_termType.
					 */

    /* Pin */
    int		 ss_dir;
    Point	 ss_pinPoint;

    /* Information describing the wires to paint */
    SimpleWire	 ss_metalWire;	/* Metal wire */
    SimpleWire	 ss_polyWire;	/* Poly wire */

    /* Contacts */
    Rect	 ss_cTerm;	/* Contact over terminal */
    bool	 ss_cTermOK;	/* Contact over terminal is OK */
    Rect	 ss_cPin;	/* Contact near pin */
    bool	 ss_cPinOK;	/* Contact near pin is OK */
} SimpleStem;

/* List of all active channels */
extern GCRChannel *gaChannelList;

/* Def used to hold channel plane */
extern CellDef *gaChannelDef;

/* Used during stem generation */
extern int gaMinAbove;
extern int gaMaxAbove, gaMaxBelow;

    /* Debugging information */
extern bool gaInitialized;	/* TRUE if registered with debug module */
extern ClientData gaDebugID;	/* Our identity with the debugging module */
#include "gaDebug.h"		/* Can add flags without total recompile */

/* Internal procedures */
extern GCRChannel *gaStemContainingChannel();
extern GCRPin *gaStemCheckPin();
extern int gaAlwaysOne();
extern bool gaMazeRoute();

/* Exported procedures */
extern int GARoute();
extern void GAChannelInitOnce();
extern void GAClearChannels();
extern bool GADefineChannel();
extern bool GAMazeInitParms();

/* Exported variables */
extern bool GAStemWarn;

#endif /* _GAROUTER_H */
