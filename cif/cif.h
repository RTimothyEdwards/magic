/*
 * cif.h --
 *
 * This procedure defines things that are exported by the
 * cif module to the rest of the world.
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/cif/cif.h,v 1.4 2010/06/24 12:37:15 tim Exp $
 */

#ifndef _MAGIC__CIF__CIF_H
#define _MAGIC__CIF__CIF_H

#include "database/database.h"

#define CIFMAXRESCALE 100	/* This value avoids integer overflow
				 * in most cases.  However, that
				 * depends on the size of the layout.
				 */

/* Passed to CIFPaintCurrent() for print statement formatting */
#define FILE_CIF    0
#define FILE_CALMA  1

/* Exported global variables (commands/CmdCD.c) */

extern int  CIFWarningLevel;
extern int  CIFRescaleLimit;
extern bool CIFRescaleAllow;
extern bool CIFNoDRCCheck;
extern bool CIFDoAreaLabels;
extern bool CIFDoCellIdLabels;
extern char *CIFPathPrefix;
extern char *CIFErrorFilename;
extern bool CIFArrayWriteDisable;
extern bool CIFHierWriteDisable;
extern bool CIFSubcellPolygons;
extern bool CIFUnfracture;

/* Procedures that parse the cif sections of a technology file. */

extern void CIFTechStyleInit(void);
extern void CIFTechInit(void);
extern bool CIFTechLine(char *sectionName, int argc, char *argv[]);
extern void CIFTechFinal(void);
extern void CIFTechOutputScale(int n, int d);
extern int CIFTechInputScale(int n, int d, bool opt);
extern bool CIFTechLimitScale(int ns, int ds);
extern void CIFReadTechStyleInit(void);
extern void CIFReadTechInit(void);
extern bool CIFReadTechLine(char *sectionName, int argc, char *argv[]);
extern void CIFReadTechFinal(void);
extern void CIFParseReadLayers(char *string, TileTypeBitMask *mask, bool newok);

/* Externally-visible procedures: */

extern float CIFGetOutputScale(int convert);
extern float CIFGetScale(int convert);
extern float CIFGetInputScale(int convert);

extern int CIFPaintCurrent(int filetype);
extern void CIFSeeLayer(CellDef *rootDef, Rect *area, char *layer);
extern void CIFPaintLayer(CellDef *rootDef, Rect *area, char *cifLayer, int magicLayer, CellDef *paintDef);
extern void CIFSeeHierLayer(CellDef *rootDef, Rect *area, char *layer, int arrays, int subcells);
extern void CIFPrintStats(void);

extern bool CIFWrite(CellDef *rootDef, FILE *f);
extern void CIFReadFile(FILE *file);

extern void CIFSetStyle(char *name);
extern void CIFSetReadStyle(char *name);

extern void CIFPrintStyle(bool dolist, bool doforall, bool docurrent);
extern void CIFPrintReadStyle(bool dolist, bool doforall, bool docurrent);

extern int CIFOutputScaleFactor(void);

extern void PaintWireList(Point *pointlist, int number, int width, int endcap, Plane *plane,
                          PaintResultType *ptable, PaintUndoInfo *ui);
extern LinkedRect *PaintPolygon(Point *pointlist, int number, Plane *plane, PaintResultType *ptable,
                                PaintUndoInfo *ui, int keep);

/* C99 compat */
extern int CIFGetContactSize(TileType type, int *edge, int *spacing, int *border);

#endif /* _MAGIC__CIF__CIF_H */
