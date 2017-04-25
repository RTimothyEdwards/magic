/*
 * malloc.h --
 *
 *	See malloc.c for a description of magic's allocation functions.
 *	Magic's built-in malloc() function has been removed.
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/utils/malloc.h,v 1.2 2009/09/10 20:32:55 tim Exp $"
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
 */

#ifndef _MALLOC_H
#define _MALLOC_H

extern void *mallocMagic(unsigned int);
extern void *callocMagic(unsigned int);
extern void freeMagic(void *);

#endif
