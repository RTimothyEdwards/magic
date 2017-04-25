/*
 * DRCPrint.c --
 *
 * Edge-based design rule checker
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/drc/DRCprint.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* not lint */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"

extern char *maskToPrint();
extern char *DBTypeShortName();

/*
 * ----------------------------------------------------------------------------
 *
 * drcGetName --
 *
 * 	This is a utility procedure that returns a convenient name for
 *	a mask layer.
 *
 * Results:
 *	The result is the first 8 characters of the long name for
 *	the layer.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
drcGetName(layer, string)
    int layer;
    char *string;		/* Used to hold name.  Must have length >= 8 */
{
    (void) strncpy(string, DBTypeShortName(layer), 8);
    string[8] = '\0';
    if (layer == TT_SPACE) return "space";
    return string;
}


/*
 * ----------------------------------------------------------------------------
 * DRCPrintRulesTable --
 *
 *	Write compiled DRC rules table and adjacency matrix to the given file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
DRCPrintRulesTable (fp)
    FILE *fp;
{
    int		  i, j, k;
    DRCCookie	* dp;
    char buf1[20], buf2[20];
    int gotAny;
					/* print the rules table */
    for (i = 0; i < DBNumTypes; i++)
    {
	gotAny = FALSE;
	for (j = 0; j < DBNumTypes; j++)
	{
	    if (DRCCurStyle->DRCRulesTbl [i][j] != (DRCCookie *) NULL)
	    {
		k = 1;
	        for (dp = DRCCurStyle->DRCRulesTbl [i][j]; dp != (DRCCookie *) NULL;
			      dp = dp->drcc_next)
		{
		    gotAny = TRUE;
		    if (k == 1)
		    {
			fprintf(fp,"%-8s %-8s  ",drcGetName(i, buf1),
			    drcGetName(j, buf2));
			k++;
		    }
		    else fprintf(fp,"                   ");

		    fprintf(fp,"%d x %d   %s (%s)\n",
			dp->drcc_dist, dp->drcc_cdist,
			maskToPrint(&dp->drcc_mask),
			DBPlaneLongName(dp->drcc_plane));
		    fprintf(fp,"                           %s",
			maskToPrint(&dp->drcc_corner));
		    if (dp->drcc_flags > 0)
			fprintf(fp, "\n                          ");
		    if (dp->drcc_flags & DRC_REVERSE)
			fprintf(fp," reverse");
		    if (dp->drcc_flags & DRC_BOTHCORNERS)
			fprintf(fp," both-corners");
		    if (dp->drcc_flags & DRC_TRIGGER)
			fprintf(fp," trigger");
		    if (dp->drcc_flags & DRC_AREA)
			fprintf(fp," area");
		    if (dp->drcc_flags & DRC_MAXWIDTH)
			fprintf(fp," maxwidth");
		    if (dp->drcc_flags & DRC_BENDS)
			fprintf(fp," bends");
		    if (dp->drcc_flags & DRC_RECTSIZE)
			fprintf(fp," rect-size");
		    if (dp->drcc_flags & DRC_ANGLES)
			fprintf(fp," angles");
		    fprintf(fp,"\n");
	        }
	    }
	}
	if (gotAny) fprintf(fp,"\n");
    }

    /* Print out overlaps that are illegal between subcells. */

    for (i = 0; i < DBNumTypes; i++)
    {
	for (j = 0; j < DBNumTypes; j++)
	{
	    if ((i == TT_ERROR_S) || (j == TT_ERROR_S)) continue;
	    if (DRCCurStyle->DRCPaintTable[0][i][j] == TT_ERROR_S)
		fprintf(fp, "Tile type %s can't overlap type %s.\n",
		    drcGetName(i, buf1), drcGetName(j, buf2));
	}
    }

    /* Print out tile types that must have exact overlaps. */

    if (!TTMaskIsZero(&DRCCurStyle->DRCExactOverlapTypes))
    {
	fprintf(fp, "Types that must overlap exactly: %s\n",
	    maskToPrint(&DRCCurStyle->DRCExactOverlapTypes));
    }
}

char *
maskToPrint (mask)
    TileTypeBitMask *mask;
{
    int	i;
    int gotSome = FALSE;
    static char printchain[400];
    char buffer[20];

    if (TTMaskIsZero(mask))
	return "<none>";

    printchain[0] = '\0';

    for (i = 0; i < DBNumTypes; i++)
	if (TTMaskHasType(mask, i))
	{
	    if (gotSome) strcat(printchain, ",");
	    else gotSome = TRUE;
	    strcat(printchain, drcGetName(i, buffer));
	}

    return (printchain);
}
