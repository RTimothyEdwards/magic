/*
 * extflat.h --
 *
 * Internal definitions for the procedures to flatten hierarchical
 * (.ext) circuit extraction files.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/extflat/extflat.h,v 1.2 2008/12/03 14:12:09 tim Exp $
 */

#ifndef _MAGIC__EXTFLAT__EXTFLAT_H
#define _MAGIC__EXTFLAT__EXTFLAT_H

#include "utils/magic.h"

#include "extflat/EFtypes.h" /* EFCapValue, HierName, EFPerimArea, EFNode */
#include "extflat/EFint.h" /* Def, HierContext, Connection, Distance, CallArg */


extern float EFScale;		/* Scale factor to multiply all coords by */
extern char *EFTech;		/* Technology of extracted circuit */
extern char *EFStyle;           /* Extraction style of extracted circuit */
extern char *EFSearchPath;	/* Path to search for .ext files */
extern char *EFLibPath;		/* Library search path */
extern char *EFVersion;		/* Version of extractor we work with */
extern char *EFArgTech;		/* Tech file given as command line argument */
extern bool  EFCompat;		/* Subtrate backwards-compatibility mode */

    /*
     * Thresholds used by various extflat clients to filter out
     * unwanted resistors and capacitors.  Resistance is in milliohms,
     * capacitance in attofarads.
     */
extern int EFResistThreshold;
extern EFCapValue EFCapThreshold;

    /* Table of transistor types */
extern char *EFDevTypes[];
extern int EFDevNumTypes;

    /* Table of Magic layers */
extern char *EFLayerNames[];
extern int EFLayerNumNames;

    /* Output control flags */
extern int EFOutputFlags;

    /* Behavior regarding disjoint node segments */
extern bool EFSaveLocs;

/* -------------------------- Exported procedures --------------------- */

extern char *EFArgs();

    /* HierName manipulation */
extern HashEntry *EFHNLook();
extern HashEntry *EFHNConcatLook();
extern HierName *EFHNConcat();
extern HierName *EFStrToHN();
extern char *EFHNToStr();
extern int EFGetPortMax();

/* C99 compat */
extern void EFHNFree();
extern bool EFHNIsGlob();
extern int  EFNodeResist();
extern void efAdjustSubCap();
extern int  efBuildAddStr();
extern void efBuildAttr();
extern int  efBuildDevice(Def *def, char class, char *type, const Rect *r, int argc, char *argv[]);
extern void efBuildDeviceParams();
extern void efBuildDist();
extern void efBuildEquiv();
extern void efBuildKill();
extern void efBuildPortNode();
extern void efBuildUse();
extern int  efFlatCaps();
extern int  efFlatDists();
extern int  efFlatKills();
extern int  efFlatNodes();
extern int  efFlatNodesStdCell();
extern void efFreeConn();
extern void efFreeDevTable();
extern void efFreeNodeList();
extern void efFreeNodeTable();
extern void efFreeUseTable();
extern void efHNBuildDistKey();
extern int  efHNLexOrder();
extern void efHNPrintSizes();
extern void efHNRecord();
extern int  efHierSrArray();
extern int  efHierSrUses();
extern int  efHierVisitDevs();
extern EFNode *efNodeMerge();
extern void efReadError(const char *fmt, ...) ATTR_FORMAT_PRINTF_1;
extern int  efReadLine();
extern bool efSymAdd();
extern bool efSymAddFile();
extern void efSymInit();
extern void EFDone();
extern void EFFlatBuild();
extern void EFFlatDone();
extern bool EFHNIsGND();
extern void EFInit();
extern bool EFReadFile();
typedef int (*cb_extflat_visitdevs_t)(Dev *dev, HierContext *hc, float scale, Transform *trans, ClientData cdata);
extern int EFVisitDevs(const cb_extflat_visitdevs_t devProc, ClientData cdata);
extern int efVisitDevs(HierContext *hc, CallArg *ca);
extern bool efSymLook();
extern int efVisitResists(HierContext *hc, CallArg *ca);
typedef int (*cb_extflat_visitresists_t)(const HierName *hn1, const HierName *hn2, float resistance, ClientData cdata);
extern int EFVisitResists(const cb_extflat_visitresists_t resProc, ClientData cdata);
extern int  EFVisitNodes();
extern int  EFVisitCaps();
extern void EFGetLengthAndWidth();
extern void EFHNOut();
extern int  EFHierSrDefs();
extern int  EFVisitSubcircuits();
extern int  EFHierVisitSubcircuits();
typedef int (*cb_extflat_hiervisitdevs_t)(HierContext *hc, Dev *dev, float scale, ClientData cdata);
extern int EFHierVisitDevs(HierContext *hc, const cb_extflat_hiervisitdevs_t devProc, ClientData cdata);
typedef int (*cb_extflat_hiervisitresists_t)(HierContext *hc, const HierName *hierName1, const HierName *hierName2, float resistance, ClientData cdata);
extern int EFHierVisitResists(HierContext *hc, const cb_extflat_hiervisitresists_t resProc, ClientData cdata);
extern int  EFHierVisitCaps();
extern int  EFHierVisitNodes();

#endif /* _MAGIC__EXTFLAT__EXTFLAT_H */
