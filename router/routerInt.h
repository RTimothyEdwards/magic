/*
 * routerInt.h -
 *
 *	Internal definitions for Router module.
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
 * rcsid="$Header: /usr/cvsroot/magic-8.0/router/routerInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $" 
 */

/*
 *	Declarations local to the router module,
 *	but global to all source files within the router module.
 */

#ifndef _ROUTERINT_H
#define _ROUTERINT_H

extern int	rtrTarget;			/* Via minimization, target type	*/
extern int	rtrReplace;			/* Via minimization, replacement type	*/
extern int	rtrDelta;			/* Change in layer width		*/

#endif /* _ROUTERINT_H */
