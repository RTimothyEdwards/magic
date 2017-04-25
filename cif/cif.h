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

#ifndef _CIF_H
#define _CIF_H

#include "database/database.h"

#define CIFMAXRESCALE 100	/* This value avoids integer overflow
				 * in most cases.  However, that
				 * depends on the size of the layout.
				 */

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

extern void CIFTechStyleInit();
extern void CIFTechInit();
extern bool CIFTechLine();
extern void CIFTechFinal();
extern void CIFTechOutputScale();
extern void CIFTechInputScale();
extern bool CIFTechLimitScale();
extern void CIFReadTechStyleInit();
extern void CIFReadTechInit();
extern bool CIFReadTechLine();
extern void CIFReadTechFinal();

/* Externally-visible procedures: */

extern float CIFGetOutputScale();
extern float CIFGetInputScale();
extern int CIFGetDefaultContactSize();

extern int CIFPaintCurrent();
extern void CIFSeeLayer();
extern void CIFPaintLayer();
extern void CIFSeeHierLayer();
extern void CIFPrintStats();

extern bool CIFWrite();
extern void CIFReadFile();

extern void CIFSetStyle();
extern void CIFSetReadStyle();

extern void CIFPrintStyle();
extern void CIFPrintReadStyle();

extern int CIFOutputScaleFactor();

extern void PaintWireList();
extern LinkedRect *PaintPolygon();

#endif /* _CIF_H */
