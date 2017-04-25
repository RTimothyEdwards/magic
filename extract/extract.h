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

#ifndef _EXTRACT_H
#define _EXTRACT_H

#include "utils/magic.h"

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
#define DEV_DIODE	7		/* Diode */
#define DEV_PDIODE	8		/* pDiode, same as Diode	*/
#define DEV_NDIODE	9		/* nDiode, terminals reversed	*/
#define DEV_SUBCKT     10		/* general-purpose subcircuit	*/
#define DEV_RSUBCKT    11		/* Resistor-like subcircuit.	*/
#define DEV_MSUBCKT    12		/* MOSFET-like subcircuit.	*/

/* Device names for .ext file output (new in version 7.2)	*/
/* (defined in extract/ExtBasic.c *and* extflat/EFread.c)	*/

extern char *extDevTable[];

/* Extractor options */
#define	EXT_DOADJUST		0x01	/* Extract hierarchical adjustments */
#define	EXT_DOCAPACITANCE	0x02	/* Extract capacitance */
#define	EXT_DOCOUPLING		0x04	/* Extract coupling capacitance */
#define	EXT_DORESISTANCE	0x08	/* Extract resistance */
#define	EXT_DOLENGTH		0x10	/* Extract pathlengths */
#define	EXT_DOALL		0x1f	/* ALL OF THE ABOVE */

extern int ExtOptions;		/* Bitmask of above */

extern bool ExtTechLine();
extern void ExtTechInit();
extern void ExtTechFinal();
extern void ExtSetStyle();
extern void ExtPrintStyle();
extern void ExtCell();

#ifdef MAGIC_WRAPPER
extern bool ExtGetDevInfo();
extern bool ExtCompareStyle();
#endif

#ifdef THREE_D
extern void ExtGetZAxis();
#endif

extern void ExtDumpCaps();

#endif /* _EXTRACT_H */
