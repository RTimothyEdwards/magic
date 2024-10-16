/*
 * extract.h --
 *
 * Defines the exported interface to the circuit extractor.
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/extract/extract.h,v 1.3 2009/01/30 03:51:02 tim Exp $"
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

#ifndef _MAGIC__EXTRACT__EXTRACT_H
#define _MAGIC__EXTRACT__EXTRACT_H

#include "utils/magic.h"
#include "database/database.h"  /* TileTypeBitMask */

/* Extractor warnings */
#define	EXTWARN_DUP	0x01	/* Warn if two nodes have the same name */
#define	EXTWARN_LABELS	0x02	/* Warn if connecting to unlabelled subcell
				 * node.
				 */
#define	EXTWARN_FETS	0x04	/* Warn about badly constructed fets */

#define	EXTWARN_ALL	(EXTWARN_DUP|EXTWARN_LABELS|EXTWARN_FETS)

extern int ExtDoWarn;		/* Bitmask of above */

/* Known devices (see ExtTech.c and ExtBasic.c)			*/
/* Make sure these match extDevTable in extract/ExtBasic.c and	*/
/* also extflat/EFread.c					*/

#define DEV_FET		0		/* FET w/area, perimeter declared */
#define DEV_MOSFET      1		/* FET w/length, width declared   */
#define DEV_ASYMMETRIC  2		/* Like MOSFET but D,S not swappable */
#define DEV_BJT         3		/* Bipolar Junction Transistor */
#define DEV_RES         4		/* Resistor */
#define DEV_CAP         5		/* Capacitor */
#define DEV_CAPREV      6		/* Capacitor, terminals reversed */
#define DEV_VOLT        7		/* Voltage source (used for shorts) */
#define DEV_DIODE	8		/* Diode */
#define DEV_PDIODE	9		/* pDiode, same as Diode	*/
#define DEV_NDIODE     10		/* nDiode, terminals reversed	*/
#define DEV_SUBCKT     11		/* general-purpose subcircuit	*/
#define DEV_RSUBCKT    12		/* Resistor-like subcircuit.	*/
#define DEV_MSUBCKT    13		/* MOSFET-like subcircuit.	*/
#define DEV_CSUBCKT    14		/* Capacitor-like subcircuit.	*/

/* Device names for .ext file output (new in version 7.2)	*/
/* (defined in extract/ExtBasic.c *and* extflat/EFread.c)	*/

extern const char * const extDevTable[];

/* Extractor options */

#define	EXT_DOADJUST		0x001	/* Extract hierarchical adjustments */
#define	EXT_DOCAPACITANCE	0x002	/* Extract capacitance */
#define	EXT_DOCOUPLING		0x004	/* Extract coupling capacitance */
#define	EXT_DORESISTANCE	0x008	/* Extract resistance */
#define	EXT_DOLENGTH		0x010	/* Extract pathlengths */
#define	EXT_DOFRINGEHALO	0x020	/* Distributed fringe capacitance */
#define	EXT_DOALL		0x03f	/* ALL OF THE ABOVE */
#define	EXT_DOLABELCHECK	0x040	/* Check for connections by label */
#define EXT_DOALIASES		0x080	/* Output all node aliases */

extern int ExtOptions;		/* Bitmask of above */
extern char *ExtLocalPath;	/* If non-NULL, location to write .ext files */ 

/* Options for "extract unique" */
#define EXT_UNIQ_ALL		0
#define EXT_UNIQ_TAGGED		1
#define EXT_UNIQ_NOPORTS	2
#define EXT_UNIQ_NOTOPPORTS	3

extern bool ExtTechLine();
extern void ExtTechInit();
extern void ExtTechFinal();
extern void ExtSetStyle();
extern void ExtPrintStyle();
extern void ExtSetPath();
extern void ExtPrintPath();
extern void ExtRevertSubstrate();
extern Plane *ExtCell();
extern void ExtractOneCell();

extern int ExtGetGateTypesMask();
extern int ExtGetDiffTypesMask();

#ifdef MAGIC_WRAPPER
extern bool ExtCompareStyle();
#endif

#ifdef THREE_D
extern void ExtGetZAxis();
#endif

extern void ExtDumpCaps();

extern int extEnumTilePerim(Tile *tpIn, const TileTypeBitMask *maskp, int pNum, int (*func)(), ClientData cdata);
extern Plane *extPrepSubstrate();

/* C99 compat */
extern void ExtAll();
extern void ExtIncremental();
extern void ExtLengthClear();
extern void ExtParents();
extern void ExtSetDriver();
extern void ExtSetReceiver();
extern void ExtShowParents();
extern void ExtTechScale();
extern void ExtUnique();
extern void ExtractTest();
extern int  ExtFindNeighbors();
extern void ExtFreeLabRegions();
extern void ExtResetTiles();
extern void extArray();
extern void extFindCoupling();
extern void extHierAdjustments();
extern void extHierConnections();
extern void extHierFreeLabels();
extern void extHierFreeOne();
extern void extHierFreeOne();
extern void extHierSubstrate();
extern int  extHierYankFunc();
extern bool extLabType();
extern void extLength();
extern void extLengthInit();
extern void extOutputConns();
extern void extOutputCoupling();
extern int  extPathTileDist();
extern void extRelocateSubstrateCoupling();
extern void extSetNodeNum();
extern void extShowTile();
extern void extSubtree();
extern int  extUniqueCell();
extern void ExtLabelOneRegion();
extern void ExtInit();
extern bool ExtGetDevInfo();

#endif /* _MAGIC__EXTRACT__EXTRACT_H */
