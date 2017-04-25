/*
 * tech.h --
 *
 * Interface to technology module.
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/utils/tech.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _TECH_H
#define _TECH_H

#include "utils/magic.h"

typedef int SectionID;		/* Mask set by TechAddClient */

/* Techfile section status:  needed for switching between extract and	*/
/* cif styles when only one style is kept in memory at any given time.	*/
/* TECH_SUSPENDED is added so that style variants can be merged together*/
/* in a style section, and will set TECH_SUSPEND when reading a portion	*/
/* of the style that does not pertain to the specific variant we want.	*/

#define TECH_NOT_LOADED	0
#define TECH_LOADED	1
#define TECH_PENDING   ((char)-1)
#define TECH_SUSPENDED ((char)-2)

/* The last format version which was included in the file name */
#define TECH_FORMAT_VERSION 27

/* The format version of the tech file currently active */
extern int TechFormatVersion;

/* ----------------- Exported variables  ---------------- */

extern char *TechFileName;	/* Full path and file name of technology file */
extern bool TechOverridesDefault; /* Set TRUE if technology was specified on
				   * the command line.
				   */

/* ----------------- Exported procedures ---------------- */

extern void TechError(char *, ...);
extern void TechAddClient();
extern void TechAddAlias();

#endif /* _TECH_H */
