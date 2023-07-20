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
extern unsigned char CalmaSubcellPolygons;
extern bool CalmaSubcellPaths;
extern bool CalmaDoLabels;
extern bool CalmaDoLibrary;
extern bool CalmaDoLower;
extern bool CalmaAddendum;
extern bool CalmaNoDuplicates;
extern time_t *CalmaDateStamp;
extern bool CalmaUnique;
extern TileTypeBitMask *CalmaMaskHints;
extern bool CalmaMergeTiles;
extern bool CalmaFlattenArrays;
extern bool CalmaNoDRCCheck;
extern bool CalmaFlattenUses;
extern int  CalmaFlattenLimit;
extern float CalmaMagScale;
extern char **CalmaFlattenUsesByName;
extern bool CalmaReadOnly;
extern bool CalmaContactArrays;
#ifdef HAVE_ZLIB
extern int  CalmaCompression;
#endif
extern bool CalmaPostOrder;
extern bool CalmaAllowUndefined;
extern bool CalmaAllowAbstract;

/* Definitions used by the return value for CalmaSubcellPolygons */
/* 	CALMA_POLYGON_NONE:  Process polygons immediately	 */
/* 	CALMA_POLYGON_TEMP:  Create temporary polygon subcells	 */
/* 	CALMA_POLYGON_KEEP:  Keep polygons in subcells		 */

#define CALMA_POLYGON_NONE	0
#define CALMA_POLYGON_TEMP	1
#define CALMA_POLYGON_KEEP	2

/* Externally-visible procedures: */
extern bool CalmaWrite();
extern void CalmaReadFile();
extern void CalmaTechInit();
extern bool CalmaGenerateArray();
extern void CalmaReadError(char *format, ...);

/* C99 compat */
extern int  calmaAddSegment();
extern void calmaDelContacts();
extern void calmaElementBoundary();
extern void calmaElementBox();
extern void calmaElementPath();
extern void calmaElementText();
extern bool calmaIsUseNameDefault();
extern bool calmaParseStructure();
extern int  calmaProcessDef();
extern int  calmaProcessDefZ();
extern bool calmaReadI2Record();
extern bool calmaReadI4Record();
extern void calmaReadPoint();
extern bool calmaReadR8();
extern bool calmaReadStampRecord();
extern bool calmaReadStringRecord();
extern bool calmaReadStringRecord();
extern bool calmaReadTransform();
extern bool calmaSkipBytes();
extern bool calmaSkipExact();
extern bool calmaSkipTo();
extern void calmaUnexpected();
extern void calmaMergeSegments();
extern void calmaRemoveDegenerate();
extern void calmaRemoveColinear();

#ifdef HAVE_ZLIB
extern bool CalmaWriteZ();
extern bool CalmaGenerateArrayZ();
#endif

#endif /* _CALMA_H */
