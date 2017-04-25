/*
 * pathvisit.h --
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
 * This file defines the PaVisit structure used by pathvisit.c.
 */

/* rcsid "$Header: /usr/cvsroot/magic-8.0/utils/pathvisit.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $" */

#ifndef	_PATHVISIT_H
#define	_PATHVISIT_H

/* Each client will match words beginning with a particular keyword */
typedef struct pvc
{
    struct pvc	*pvc_next;	/* Next client in list */
    char	*pvc_keyword;	/* Initial keyword */
    int		(*pvc_proc)();	/* Procedure to call for lines matching
				 * pvc_keyword.
				 */
    ClientData	 pvc_cdata;	/* Client data passed to above procedure */
} PaVisitClient;

/* Each of these has a list of the above clients */
typedef struct
{
    PaVisitClient	*pv_first;	/* First client in list */
    PaVisitClient	*pv_last;	/* Last client in list */
} PaVisit;

PaVisit *PaVisitInit();

#endif	/* _PATHVISIT_H */
