/*
 * maxrect.h --
 *
 * Definitions needed for finding maximum-area rectangles
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/utils/maxrect.h,v 1.1 2008/12/04 16:21:44 tim Exp $
 */

#ifndef _MAXRECT_H
#define	_MAXRECT_H

#include "database/database.h"

/* Data structure to keep a record of multiple rectangular regions,
 * used by the widespacing rule
 */

typedef struct {
    Rect *rlist;
    Rect *swap;
    int  entries;
    int  maxdist;
    int  listdepth;
    ClientData match;
} MaxRectsData;

extern MaxRectsData *genCanonicalMaxwidth();

/*
 * Exported procedures
 */

extern Rect *FindMaxRectangle();
extern Rect *FindMaxRectangle2();
extern int FindMaxRects();

#endif /* _MAXRECT_H */
