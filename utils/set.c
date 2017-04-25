/* set.c -
 *
 *	Generic routines for setting (from a string) and printing
 *      parameter values.  Error messages are printed for invalid
 *      input strings.
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
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/set.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/list.h"


/*
 * ----------------------------------------------------------------------------
 *
 * SetNoisy<type> --
 *
 * Set parameter and print current value.
 * 
 * Results:
 *	None.  SetNoisyBool() now returns -2 on error, -1 ambiguous
 *	(but what's ambiguous about true/false??), 0 on success.  All
 *	of these routines should do the same!
 *
 * Side effects:
 *	If valueS is a nonnull string, interpret as <type> and set parm
 *      accordingly.
 *
 *      If valueS is null, the parameter value is left unaltered.
 *
 *      If file is nonnull parameter value is written to file.
 *
 *      If file is null, parameter value is written to magic text window via
 *      TxPrintf
 *	
 * ----------------------------------------------------------------------------
 */

/* SetNoisyInt -- */
void
SetNoisyInt(parm,valueS,file)
    int *parm;
    char *valueS;
    FILE *file;
{

    /* If value not null, set parm */
    if (valueS)
    {
	if(!StrIsInt(valueS))
	{
	    TxError("Noninteger value for integer parameter (\"%.20s\") ignored.\n",
		valueS);
	}
	else
	{
	    *parm = atoi(valueS);
	}
    }

    /* Print parm value */
    if(file)
	fprintf(file,"%8d ", *parm);
    else
	TxPrintf("%8d ", *parm);

    return;
}

/* SetNoisyBool --  */

int
SetNoisyBool(parm,valueS,file)
    bool *parm;
    char *valueS;
    FILE *file;
{
    int n, which, result;

    /* Bool string Table */
    static struct
    {
	char	*bS_name;	/* name */
	bool    bS_value;	/* procedure processing this parameter */
    } boolStrings[] = {
	"yes",		TRUE,
	"no",		FALSE,
	"true",		TRUE,
	"false",	FALSE,
	"1",		TRUE,
	"0",		FALSE,
	"on",		TRUE,
	"off",		FALSE,
	0
    };
    
    /* If value not null, set parm */
    if (valueS)
    {
	/* Lookup value string in boolString table */
	which = LookupStruct(
	    valueS, 
	    (LookupTable *) boolStrings, 
	    sizeof boolStrings[0]);

        /* Process result of lookup */
	if (which >= 0)
	{
	    /* string found - set parm */
	    *parm = boolStrings[which].bS_value;
	    result = 0;
	}
	else if (which == -1)
	{
	    /* ambiguous boolean value - complain */
	    TxError("Ambiguous boolean value: \"%s\"\n", 
		valueS);
	    result = -1;
	}
	else 
	{
	    TxError("Unrecognized boolean value: \"%s\"\n", valueS);
	    TxError("Valid values are:  ");
	    for (n = 0; boolStrings[n].bS_name; n++)
		TxError(" %s", boolStrings[n].bS_name);
	    TxError("\n");
	    result = -2;
	}
    }
    
    /* Print parm value */
    if(file)
	fprintf(file,"%8.8s ", *parm ? "YES" : "NO");
    else
	TxPrintf("%8.8s ", *parm ? "YES" : "NO");

    return result;
}

/* SetNoisyDI -- */
/* double size non-negative integer */
void
SetNoisyDI(parm,valueS,file)
    dlong *parm;		/* BY NP */
    char *valueS;
    FILE *file;
{
    /* If value not null, set parm */
    if (valueS)
    {
	if(!StrIsInt(valueS))
	{
	    TxError("Noninteger value for integer parameter (\"%.20s\") ignored.\n",
		valueS);
	}
	else
	{
	    /* BY NP */
	    *parm = (dlong)atoi(valueS);
	}
    }
    
    /* Print parm value */
    {
	if(file)
	{
	    fprintf(file,"%.0f ", (double) (*parm));	/* BY NP */
	}
	else
	{
	    TxPrintf("%.0f ", (double) (*parm));	/* BY NP */
	}
    }
    return;
}
