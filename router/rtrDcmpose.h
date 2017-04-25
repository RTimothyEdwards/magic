/*
 * rtrDecompose.h --
 *
 * Routines to do channel decomposition.
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
 * rcsid="$Header: /usr/cvsroot/magic-8.0/router/rtrDcmpose.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $" 
 */

#ifndef _RTRDECOMPOSE_H
#define _RTRDECOMPOSE_H

#define rtrNW 1	/* Corner codes :	*/
#define rtrSW 2  /*  NW=1 ______ NE=8	*/
#define rtrSE 4  /*       |    |		*/
#define rtrNE 8  /*  SW=2 |____| SE=4	*/

/* One horizontal space tile edge may touch an arbitrary number of other tiles:
 *	  _______________________________________________
 *                          |CELL|    |CELL|
 *        ________**********|____|====|____|****_________
 *          |CELL|          SPACE               |CELL|
 *	  __|____|**********______**************|____|___
 *                          |CELL|
 *        __________________|____|_______________________
 *
 * Flags indicate permanently marked edges.  There are 4 flags, one for each
 * overlapping corner edge marked above (*).  Test a horizontal edge by
 * looking at the tiles on both sides of the edge.  If one edge is completely
 * spanned by the other marked above (=), then use either of its tile's two
 * lower flag bits (they should be identical).
 *
 * Edge spans can only overlap (*), at the extreme leftmost and/or rightmost
 * horizontal tile-to-tile edges.  Four flag bits:  topLeft, topRight,
 * bottomLeft, and bottomRight, are used to test a particular edge.
 */

/* rtrMARKED(t,s) 	Tile * t;  int s;
 * Return 1 if the indicated horizontal boundary of a tile is marked.
 */
#define rtrMARKED(t,s) (((int) (t)->ti_client) & (s))

/* rtrMARK(t,s)		Tile * t;  int s;
 * Mark the indicated horizontal tile edge as a valid channel boundary.
 */
#define rtrMARK(t,s) \
    ((t)->ti_client = (ClientData) (((int) (t)->ti_client)&(s)))

/* rtrCLEAR(t,s)		Tile * t;  int s;
 * Clear the indicated horizontal tile edge as a valid channel boundary.
 */
#define rtrCLEAR(t,s) \
    ((t)->ti_client = (ClientData) (((int) (t)->ti_client)&(!s)))

/* Private Procedures */
int rtrSrPaint();
int rtrSrClear();
int rtrSrFunc();

/* Exported Procedures */
CellDef * RtrDecompose();

#endif /* _RTRDECOMPOSE_H */
