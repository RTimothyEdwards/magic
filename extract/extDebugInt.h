/*
 * extDebugInt.h --
 *
 * Definitions of debugging flags for extraction.
 * This is a separate include file so that new debugging flags
 * can be added to it without forcing recompilation of the
 * entire extract module.
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/extract/extDebugInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
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
 */

extern int extDebAreaEnum;
extern int extDebArray;
extern int extDebHardWay;
extern int extDebHierCap;
extern int extDebHierAreaCap;
extern int extDebLabel;
extern int extDebNeighbor;
extern int extDebNoArray;
extern int extDebNoFeedback;
extern int extDebNoHard;
extern int extDebNoSubcell;
extern int extDebLength;
extern int extDebPerim;
extern int extDebResist;
extern int extDebVisOnly;
extern int extDebYank;
