/*
 * plowDebugInt.h --
 *
 * Definitions of debugging flags for plowing.
 * This is a separate include file so that new debugging flags
 * can be added to it without forcing recompilation of the
 * entire plow module.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/plow/plowDebugInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

extern int plowDebAdd;
extern int plowDebMove;
extern int plowDebNext;
extern int plowDebTime;
extern int plowDebWidth;
extern int plowDebJogs;
extern int plowDebYankAll;

extern ClientData plowDebugID;

extern Point plowWhenTopPoint, plowWhenBotPoint;
extern bool plowWhenTop, plowWhenBot;
