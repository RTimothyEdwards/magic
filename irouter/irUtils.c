/*
 * irUtils.c ---
 *
 * Misc. utility routines for irouter.
 *
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1987, 1990 Michael H. Arnold, Walter S. Scott, and  *
 *     * the Regents of the University of California.                      *
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/irouter/irUtils.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/list.h"
#include "mzrouter/mzrouter.h"
#include "irouter/irouter.h"
#include "irouter/irInternal.h"


/*
 * ----------------------------------------------------------------------------
 *
 * irFindRouteType --
 *
 * Search irRouteLayers and irRouteContacts lists for routeType of given
 * tile type.
 *
 * Results:
 *	pointer to routetype struc if found, else Null.
 *
 * ----------------------------------------------------------------------------
 */

RouteType *
irFindRouteType(type)
    TileType type;
{
    RouteType *rT;

    /* Search list of routetypes for one with appropriate type */
    for (rT = irRouteTypes; 
	rT && rT->rt_tileType!=type; 
	rT=rT->rt_next)
	;

    /* return result */
    return(rT);
}


/*
 * ----------------------------------------------------------------------------
 *
 * irFindRouteLayer --
 *
 * search irRouteLayers struc for given tiletype.
 *
 * Results:
 *	pointer to routelayer struc if found, else Null.
 *
 * ----------------------------------------------------------------------------
 */

RouteLayer *
irFindRouteLayer(type)
    TileType type;
{
    RouteLayer *rL;

    /* Search list of routelayers for one with appropriate type */
    for (rL = irRouteLayers; 
	rL && rL->rl_routeType.rt_tileType!=type; 
	rL=rL->rl_next)
	;

    /* return result */
    return(rL);
}


/*
 * ----------------------------------------------------------------------------
 *
 * irFindRouteContact --
 *
 * Search irRoutecontacts for given tiletype.
 *
 * Results:
 *	pointer to routecontact struc if found, else Null.
 *
 * ----------------------------------------------------------------------------
 */

RouteContact *
irFindRouteContact(type)
    TileType type;
{
    RouteContact *rC;

    /* Search list of routecontacts for one with appropriate type */
    for (rC = irRouteContacts; 
	rC && rC->rc_routeType.rt_tileType!=type; 
	rC=rC->rc_next)
	;

    /* return result */
    return(rC);
}


/*
 * ----------------------------------------------------------------------------
 *
 * irRepeatChar --
 *
 * Build temporary string consisting of n repetitions of a character.
 *
 * Results:
 *	Pointer to temporary string.
 *
 * Side effects:
 *	RepeatString set to desired string.  
 *
 * Note:
 *	A call to this procedure destroys old strings previously built
 *      by the procedure.
 *
 * ----------------------------------------------------------------------------
 */

char RepeatString[100];

char *
irRepeatChar(n,c)
    int n;
    char c;
{
    int i;
    for(i=0; i<n; i++)
	RepeatString[i]=c;

    RepeatString[n]='\0';

    return(&(RepeatString[0]));
}

