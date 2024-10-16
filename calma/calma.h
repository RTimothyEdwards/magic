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

#ifndef _MAGIC__CALMA__CALMA_H
#define _MAGIC__CALMA__CALMA_H

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
extern bool CalmaWrite(CellDef *rootDef, FILE *f);
extern void CalmaReadFile(FILETYPE file, char *filename);
extern void CalmaTechInit(void);
extern bool CalmaGenerateArray(FILE *f, TileType type, int llx, int lly, int pitch, int cols, int rows);
extern void CalmaReadError(const char *format, ...) ATTR_FORMAT_PRINTF_1;

/* C99 compat */
extern void calmaDelContacts(void);
extern void calmaElementBoundary(void);
extern void calmaElementBox(void);
extern void calmaElementPath(void);
extern void calmaElementText(void);
extern bool calmaIsUseNameDefault(char *defName, char *useName);
extern bool calmaParseStructure(char *filename);
extern int calmaProcessDef(CellDef *def, FILE *outf, bool do_library);
#ifdef HAVE_ZLIB
extern int calmaProcessDefZ(CellDef *def, gzFile outf, bool do_library);
#endif
extern bool calmaReadI2Record(int type, int *pvalue);
extern bool calmaReadI4Record(int type, int *pvalue);
extern void calmaReadPoint(Point *p, int iscale);
extern bool calmaReadR8(double *pd);
extern bool calmaReadStampRecord(int type, int *stampptr);
extern bool calmaReadStringRecord(int type, char **str);
extern bool calmaReadStringRecord(int type, char **str);
extern bool calmaReadTransform(Transform *ptrans, char *name);
extern bool calmaSkipBytes(int nbytes);
extern bool calmaSkipExact(int type);
extern bool calmaSkipTo(int what);
extern void calmaUnexpected(int wanted, int got);

#ifdef HAVE_ZLIB
extern bool CalmaWriteZ(CellDef *rootDef, gzFile f);
extern bool CalmaGenerateArrayZ(gzFile f, TileType type, int llx, int lly, int pitch, int cols, int rows);
#endif

#endif /* _MAGIC__CALMA__CALMA_H */
