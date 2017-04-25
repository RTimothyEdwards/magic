/*
 * calma.h --
 *
 * This file defines things that are exported by the
 * calma module to the rest of the world.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/calma/calma.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _CALMA_H
#define _CALMA_H

#include "utils/magic.h"

/* Externally visible variables */
extern bool CalmaSubcellPolygons;
extern bool CalmaDoLabels;
extern bool CalmaDoLower;
extern bool CalmaMergeTiles;
extern bool CalmaFlattenArrays;
extern bool CalmaNoDRCCheck;
extern bool CalmaFlattenUses;
extern bool CalmaReadOnly;
extern bool CalmaContactArrays;
extern bool CalmaPostOrder;

/* Externally-visible procedures: */
extern bool CalmaWrite();
extern void CalmaReadFile();
extern void CalmaTechInit();
extern bool CalmaGenerateArray();

#endif /* _CALMA_H */
