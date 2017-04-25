/* gcrDebug.c -
 *
 * The greedy router, debug routines.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/gcr/gcrDebug.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>
#include <sys/times.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "textio/textio.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "gcr/gcr.h"
#include "utils/heap.h"
#include "router/router.h"
#include "utils/malloc.h"

int gcrViaCount;
bool GcrShowEnd = FALSE;
bool GcrShowMap = FALSE;
int gcrStandalone=FALSE; /*Flag to control standalone/integrated operation*/

/* Forward declarations */
void gcrDumpResult();
void gcrStats();
void gcrShowMap();
bool gcrMakeChannel();

void gcrPrintCol(GCRChannel *, int, int);



/*
 * ----------------------------------------------------------------------------
 *
 * GCRRouteFromFile --
 *
 * Reads a routing problem from the named file and performs the routing.
 *
 * Results:
 *	Returns a pointer to the routed channel.
 *
 * Side effects:
 *	Allocates memory.
 *
 * ----------------------------------------------------------------------------
 */

GCRChannel *
GCRRouteFromFile(fname)
    char *fname;
{
    static Point initOrigin = { 0, 0 };
    struct tms tbuf1, tbuf2;
    GCRChannel *ch;
    Transform trans;
    FILE *fp;
    Rect box;

    fp = fopen(fname, "r");
    if (fp == NULL)
    {
	perror(fname);
	return ((GCRChannel *) NULL);
    }

    ch = (GCRChannel *) mallocMagic((unsigned) (sizeof (GCRChannel)));
    ch->gcr_type = CHAN_NORMAL;
/*     ch->gcr_area = box; */
    ch->gcr_transform = GeoIdentityTransform;
    ch->gcr_lCol = (GCRColEl *) NULL;
    ch->gcr_nets = (GCRNet *) NULL;
    ch->gcr_result = (short **) NULL;
    ch->gcr_origin = initOrigin;

    if (!gcrMakeChannel(ch, fp))
    {
	TxError("Couldn't initialize channel routing problem\n");
	(void) fclose(fp);
	freeMagic((char *) ch);
	return ((GCRChannel *) NULL);
    }

    (void) fclose(fp);
    ch->gcr_lCol = (GCRColEl *) mallocMagic((unsigned) ((ch->gcr_width+2) * sizeof (GCRColEl)));
    times(&tbuf1);
    (void) GCRroute(ch);
    times(&tbuf2);
    TxPrintf("Time   :  %5.2fu  %5.2fs\n", (tbuf2.tms_utime -
	    tbuf1.tms_utime)/60.0, (tbuf2.tms_stime-tbuf1.tms_stime)*60);

    gcrDumpResult(ch, GcrShowEnd);
    gcrShowMap(ch);
    return (ch);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrMakeChannel --
 *
 * 	Read a channel in the new file format.
 *
 * Results:
 *	TRUE if successful, FALSE if not.
 *
 * Side effects:
 *	Sets values in *channel.
 *
 * ----------------------------------------------------------------------------
 */

bool
gcrMakeChannel(ch, fp)
    GCRChannel *ch;
    FILE *fp;
{
    GCRPin *gcrMakePinLR();
    unsigned lenWds, widWds;
    int i, j, c, pid;
    char s[25];

    c = getc(fp);
    if (c != '*')
    {
	TxError("Old-style channel format no longer supported.\n");
	return (FALSE);
    }

    if (fscanf(fp, "%d %d", &ch->gcr_width, &ch->gcr_length) != 2)
    {
	TxError("Format error in input file width or length.\n");
	return (FALSE);
    }

    lenWds = ch->gcr_length + 2;
    widWds = ch->gcr_width + 2;
    ch->gcr_density = (int *) mallocMagic((unsigned) (lenWds * sizeof (int)));
    ch->gcr_lPins = gcrMakePinLR(fp, 0, ch->gcr_width);
    ch->gcr_tPins = (GCRPin *) mallocMagic((unsigned) (lenWds * sizeof (GCRPin)));
    ch->gcr_bPins = (GCRPin *) mallocMagic((unsigned) (lenWds * sizeof (GCRPin)));
    ch->gcr_result = (short **) mallocMagic((unsigned) (lenWds * sizeof (short *)));

    /* Initialize end columns */
    ch->gcr_result[0] = (short *) mallocMagic((unsigned) (widWds * sizeof (short)));
    ch->gcr_result[ch->gcr_length+1] = (short *) mallocMagic((unsigned) (widWds * sizeof (short)));
    for (i = 0; i < widWds; i++)
    {
	ch->gcr_result[0][i] = 0;
	ch->gcr_result[ch->gcr_length + 1][i] = 0;
    }

    /* Initialize internal columns */
    for (i = 1; i <= ch->gcr_length; i++)
    {
	/* Allocate the column */
	ch->gcr_result[i] = (short *) mallocMagic((unsigned) (widWds * sizeof (short)));

	/* Initialize the bottom pin */
	if (fscanf(fp, "%d", &pid) != 1)
	{
	    TxError("Format error in pin-id in channel file\n");
	    return (FALSE);
	}
	ch->gcr_bPins[i].gcr_pId = (GCRNet *)(spointertype) pid;
	ch->gcr_bPins[i].gcr_x = i;
	ch->gcr_bPins[i].gcr_y = 0;

	ch->gcr_result[i][0] = 0;
	ch->gcr_result[i][ch->gcr_width+1] = 0;
	for (j = 1; j <= ch->gcr_width; j++)
	{
	    /*
	     * Read a column of obstacles.  m and M mean metal is blocked,
	     * p and P mean poly is blocked.  Upper case means vacate the
	     * column, lower case means vacate the track.
	     */
	    if (fscanf(fp, "%s", s) != 1)
	    {
		TxError("Format error in router input file\n");
		return (FALSE);
	    }
	    switch (s[0])
	    {
		case 'M': case 'm':
		    ch->gcr_result[i][j] = GCRBLKM;
		    break;
		case 'P': case 'p':
		    ch->gcr_result[i][j] = GCRBLKP;
		    break;
		case '.':
		    ch->gcr_result[i][j] = 0;
		    break;
		default:
		    ch->gcr_result[i][j] = GCRBLKP | GCRBLKM;
		    break;
	    }
	}

	/* Read bottom pin id */
	if (fscanf(fp, "%d", &pid) != 1)
	{
	    TxError("Format error in router input file\n");
	    return (FALSE);
	}
	ch->gcr_tPins[i].gcr_pId = (GCRNet *)(spointertype) pid;
	ch->gcr_tPins[i].gcr_x = i;
	ch->gcr_tPins[i].gcr_y = ch->gcr_width + 1;
    }

    ch->gcr_rPins = gcrMakePinLR(fp, ch->gcr_length + 1, ch->gcr_width);
    ch->gcr_area.r_xbot = 0;
    ch->gcr_area.r_ybot = 0;
    ch->gcr_area.r_xtop = (ch->gcr_length + 1) * RtrGridSpacing;
    ch->gcr_area.r_ytop = (ch->gcr_width  + 1) * RtrGridSpacing;

    return (TRUE);
}

GCRPin *
gcrMakePinLR(fp, x, size)
    FILE *fp;
    int x, size;
{
    GCRPin *result;
    int i;

    result = (GCRPin *) mallocMagic((unsigned) ((size+2) * sizeof (GCRPin)));
    result[0].gcr_x = result[0].gcr_y = 0;
    result[0].gcr_pId = (GCRNet *) NULL;
    result[size + 1].gcr_x = result[size + 1].gcr_y = 0;
    result[size + 1].gcr_pId = (GCRNet *) NULL;
    for (i = 1; i <= size; i++)
    {
	/* FIXME: Reading a pointer from file is almost guaranteed to break. */
	dlong pointer_bits;
	(void) fscanf(fp, "%"DLONG_PREFIX"d", &pointer_bits);
	result[i].gcr_pId = (struct gcrnet *) pointer_bits;
	result[i].gcr_x = x;
	result[i].gcr_y = i;
    }

    return (result);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrSaveChannel --
 *
 * Write a channel file for subsequent use in debugging.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a disk file named by the channel address,
 *	e.g, chan.1efb0
 *
 * ----------------------------------------------------------------------------
 */

void
gcrSaveChannel(ch)
    GCRChannel *ch;
{
    FILE *fp;
    char name[128];
    int flags, i, j;

    (void) sprintf(name, "chan.%p", ch);
    fp = fopen(name, "w");
    if (fp == NULL)
    {
	TxPrintf("Can't dump channel to file; "); TxFlush();
	perror(name);
	return;
    }

    /* Output the prologue */
    fprintf(fp, "* %d %d\n", ch->gcr_width, ch->gcr_length);

#define	NETID(pin)	((pin).gcr_pId ? (pin).gcr_pId->gcr_Id : 0)

    /* Output the left pin array */
    for (j = 1; j <= ch->gcr_width; j++)
	fprintf(fp, "%d ", NETID(ch->gcr_lPins[j]));
    fprintf(fp, "\n");

    /* Process main body of channel */
    for (i = 1; i <= ch->gcr_length; i++)
    {
	/* Bottom pin */
	fprintf(fp, "%d ", NETID(ch->gcr_bPins[i]));

	/*
	 * Interior points (for obstacle map).
	 * Codes are as follows:
	 *
	 *	m	metal blocked		vacate track
	 *	M	metal blocked		vacate column
	 *	p	poly blocked		vacate track
	 *	P	poly blocked		vacate column
	 */
	for (j = 1; j <= ch->gcr_width; j++)
	{
	    flags = ch->gcr_result[i][j];
	    switch (flags & (GCRBLKM|GCRBLKP))
	    {
		case 0:			fprintf(fp, ". "); break;
		case GCRBLKM:		fprintf(fp, "m "); break;
		case GCRBLKP:		fprintf(fp, "p "); break;
		case GCRBLKM|GCRBLKP:	fprintf(fp, "x "); break;
	    }
	}

	/* Top pin */
	fprintf(fp, "%d\n", NETID(ch->gcr_tPins[i]));
    }

    /* Output the right pin array */
    for (j = 1; j <= ch->gcr_width; j++)
	fprintf(fp, "%d ", NETID(ch->gcr_rPins[j]));
    fprintf(fp, "\n");
    (void) fclose(fp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrPrDensity --
 *
 * Create a file of the form "dens.xbot.ybot.xtop.ytop", where
 * xbot, ybot are the lower-left coordinates of ch->gcr_area
 * and xtop, ytop are the upper right coordinates.  This file
 * contains a comparison of the density computed by the global
 * router with that computed by the channel router; it is used
 * for debugging only.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a file named as described above.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrPrDensity(ch, chanDensity)
    GCRChannel *ch;
    int chanDensity;
{
    int i, diff;
    char name[256];
    FILE *fp;

    (void) sprintf(name, "dens.%d.%d.%d.%d",
		ch->gcr_area.r_xbot, ch->gcr_area.r_ybot,
		ch->gcr_area.r_xtop, ch->gcr_area.r_ytop);
    fp = fopen(name, "w");
    if (fp == NULL)
	fp = stdout;

    fprintf(fp, "Chan width: %d\n", ch->gcr_width);
    fprintf(fp, "Chan length: %d\n", ch->gcr_length);
    fprintf(fp, "Chan area: ll=(%d,%d) ur=(%d,%d)\n",
		ch->gcr_area.r_xbot, ch->gcr_area.r_ybot,
		ch->gcr_area.r_xtop, ch->gcr_area.r_ytop);
    fprintf(fp, "Max column density (global):  %d\n", ch->gcr_dMaxByCol);
    fprintf(fp, "Max column density (channel): %d\n", chanDensity);
    fprintf(fp, "Column density by column:\n");
    fprintf(fp, "%3s %5s", "COL", "GLOB");
#ifdef	IDENSITY
    fprintf(fp, " %5s %5s", "INIT", "DIFF");
#endif	/* IDENSITY */
    fprintf(fp, " %5s\n", "CHAN");
    for (i = 1; i <= ch->gcr_length; i++)
    {
	fprintf(fp, "%3d %5d", i, ch->gcr_dRowsByCol[i]);
	diff = ch->gcr_dRowsByCol[i];
#ifdef	IDENSITY
	diff -= ch->gcr_iRowsByCol[i];
	fprintf(fp, " %5d %5d", ch->gcr_iRowsByCol[i], diff);
#endif	/* IDENSITY */
	fprintf(fp, "%5d%s\n", ch->gcr_density[i],
		(diff != ch->gcr_density[i]) ? " *****" : "");
    }
    fprintf(fp, "------\n");
    fprintf(fp, "Row density by column (global only):\n");
    fprintf(fp, "%3s %5s", "ROW", "GLOB");
#ifdef	IDENSITY
    fprintf(fp, " %5s %5s", "INIT", "DIFF");
#endif	/* IDENSITY */
    fprintf(fp, "\n");
    for (i = 1; i <= ch->gcr_width; i++)
    {
	fprintf(fp, "%3d %5d", i, ch->gcr_dColsByRow[i]);
#ifdef	IDENSITY
	fprintf(fp, " %5d %5d", ch->gcr_iColsByRow[i],
			ch->gcr_dColsByRow[i] - ch->gcr_iColsByRow[i]);
#endif	/* IDENSITY */
	fprintf(fp, "\n");
    }

    (void) fflush(fp);
    if (fp != stdout)
	(void) fclose(fp);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrDumpPins --
 *
 * Prints a pin array.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrDumpPins(ch)
    GCRChannel *ch;
{
    int      i;
    GCRPin * pinArray;

    pinArray=ch->gcr_lPins;
    TxPrintf("LEFT PINS\n");
    for(i=0; i<=ch->gcr_width; i++)
    {
	TxPrintf("Location [%d]=%d:  x=%d, y=%d, pNext=%d, pPrev=%d, id=%d\n",
	i, &pinArray[i], pinArray[i].gcr_x, pinArray[i].gcr_y,
	pinArray[i].gcr_pNext, pinArray[i].gcr_pPrev, pinArray[i].gcr_pId);
    }
    pinArray=ch->gcr_rPins;
    TxPrintf("RIGHT PINS\n");
    for(i=0; i<=ch->gcr_width; i++)
    {
	TxPrintf("Location [%d]=%d:  x=%d, y=%d, pNext=%d, pPrev=%d, id=%d\n",
	i, &pinArray[i], pinArray[i].gcr_x, pinArray[i].gcr_y,
	pinArray[i].gcr_pNext, pinArray[i].gcr_pPrev, pinArray[i].gcr_pId);
    }
    pinArray=ch->gcr_bPins;
    TxPrintf("BOTTOM PINS\n");
    for(i=0; i<=ch->gcr_length; i++)
    {
	TxPrintf("Location [%d]=%d:  x=%d, y=%d, pNext=%d, pPrev=%d, id=%d\n",
	i, &pinArray[i], pinArray[i].gcr_x, pinArray[i].gcr_y,
	pinArray[i].gcr_pNext, pinArray[i].gcr_pPrev, pinArray[i].gcr_pId);
    }
    pinArray=ch->gcr_tPins;
    TxPrintf("TOP PINS\n");
    for(i=0; i<=ch->gcr_length; i++)
    {
	TxPrintf("Location [%d]=%d:  x=%d, y=%d, pNext=%d, pPrev=%d, id=%d\n",
	i, &pinArray[i], pinArray[i].gcr_x, pinArray[i].gcr_y,
	pinArray[i].gcr_pNext, pinArray[i].gcr_pPrev, pinArray[i].gcr_pId);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrDumpPinList --
 *
 * Prints a list of pins for a net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrDumpPinList(pin, dir)
    GCRPin *pin;
    bool dir;
{
    if (pin)
    {
	TxPrintf("Location (%d, %d)=%x:  pNext=%d, pPrev=%d, id=%d\n",
		pin->gcr_x, pin->gcr_y, pin,
		pin->gcr_pNext, pin->gcr_pPrev, pin->gcr_pId);
	if (dir) gcrDumpPinList(pin->gcr_pNext, dir);
	else gcrDumpPinList(pin->gcr_pPrev, dir);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrDumpCol --
 *
 * Print the left column contents.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrDumpCol(col, size)
    GCRColEl *col;
    int size;
{
    int i;

    if (!gcrStandalone)
	return;

    for (i = size; i >= 0; i--)
	TxPrintf("[%2d] hi=%6d(%c) lo=%6d(%c) h=%6d v=%6d w=%6d f=%4d\n", i,
	    col[i].gcr_hi, col[i].gcr_hOk ? 'T' : 'F',
	    col[i].gcr_lo, col[i].gcr_lOk ? 'T' : 'F',
	    col[i].gcr_h, col[i].gcr_v,
	    col[i].gcr_wanted, col[i].gcr_flags);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrDumpResult --
 *
 * Print the results of the routing, up to the current column
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrDumpResult(ch, showResult)
    GCRChannel *ch;
    bool showResult;
{
    int j;

    if(!showResult)
	return;

    gcrStats(ch);
    TxPrintf("         ");
    for (j = 1; j <= ch->gcr_width; j++)
	if (ch->gcr_lPins[j].gcr_pId)
	    TxPrintf("%2d", ch->gcr_lPins[j].gcr_pId->gcr_Id);
	else
	    TxPrintf("  ");
    TxPrintf("\n");

    for (j = 0; j <= ch->gcr_length; j++)
	gcrPrintCol(ch, j, showResult);

    TxPrintf("         ");
    for (j = 1; j <= ch->gcr_width; j++)
	if (ch->gcr_rPins[j].gcr_pId)
	    TxPrintf("%2d", ch->gcr_rPins[j].gcr_pId->gcr_Id);
	else
	    TxPrintf("  ");
    TxPrintf("\n");
}

void  gcrPrintCol(ch, i, showResult)
    GCRChannel *ch;
    int i, showResult;
{
    short **res = ch->gcr_result;
    int j;

    if (!showResult)
	return;

    if (i>0)
    {
	if(ch->gcr_bPins[i].gcr_pId)
	    TxPrintf("[%3d] %2d:", i, (int) ch->gcr_bPins[i].gcr_pId->gcr_Id);
	else TxPrintf("[%3d]   :", i);
	for (j = 0; j <= ch->gcr_width; j++)
	{
	    if (j != 0)
	    {
		if ((res[i][j] & GCRX) && (!(res[i][j] & (GCRBLKM|GCRBLKP))))
		{
		    TxPrintf("X");
		    gcrViaCount++;
		}
		else if ((res[i][j] & GCRR) || (i > 0 && (res[i-1][j] & GCRR)))
		{
		    if (res[i][j] & GCRBLKM) TxPrintf("|");
		    else if ((res[i][j] & GCRU)
			|| (j != 0 && (res[i][j-1] & GCRU)))
		    {
			if ((res[i][j]&GCRBLKM) && !(res[i][j]&GCRR))
			    TxPrintf("+");
			else if (res[i][j]&GCRBLKP)
			    TxPrintf("#");
			else
			    TxPrintf(")");
		    }
		    else TxPrintf("#");
		}
		else if ((res[i][j] & GCRU) || j != 0 && (res[i][j-1] & GCRU))
		{
		    if((res[i][j]&GCRCC) && (!(res[i][j]&(GCRBLKM|GCRBLKP))))
		    {
			gcrViaCount++;
			TxPrintf("X");
		    }
		    else
		    if(res[i][j] & GCRBLKP)
			TxPrintf("#");
		    else
		    if(res[i][j+1] & GCRBLKP)
			TxPrintf("#");
		    else
		    if(res[i][j] & GCRVM)
			TxPrintf("#");
		    else
			TxPrintf("-");
		}
		else
		if((res[i][j] & GCRBLKM) && (res[i][j] & GCRBLKP))
		    TxPrintf("~");
		else
		if(res[i][j] & GCRBLKM)
		    TxPrintf("'");
		else
		if(res[i][j] & GCRBLKP)
		    TxPrintf("`");
		else
	    TxPrintf(" ");
	    }

	    if(res[i][j] & GCRU)
		if(res[i][j] & GCRBLKP)
		    TxPrintf("#");
		else
		if(res[i][j+1] & GCRBLKP)
		    TxPrintf("#");
		else
		if(res[i][j] & GCRVM)
		    TxPrintf("#");
		else
		    TxPrintf("-");
	    else
	    if((res[i][j] & GCRBLKM) && (res[i][j] & GCRBLKP))
		TxPrintf("~");
	    else
	    if( ((res[i][j] & GCRBLKM) && (res[i][j+1] & GCRBLKP)) ||
	        ((res[i][j] & GCRBLKP) && (res[i][j+1] & GCRBLKM)) )
		TxPrintf("~");
	    else
	    if((res[i][j+1] & GCRBLKM) && (res[i][j+1] & GCRBLKP))
		TxPrintf("~");
	    else
	    if((res[i][j] & GCRBLKM) || (res[i][j+1] & GCRBLKM))
		TxPrintf("'");
	    else
	    if((res[i][j] & GCRBLKP) || (res[i][j+1] & GCRBLKP))
		TxPrintf("`");
	    else
		TxPrintf(" ");
	}
	if(ch->gcr_tPins[i].gcr_pId!=(GCRNet *) 0)
	    TxPrintf(":%2d {%2d}", (int) ch->gcr_tPins[i].gcr_pId->gcr_Id,
		    ch->gcr_density[i]);
	else
	    TxPrintf(":   {%2d}", ch->gcr_density[i]);
    }

    TxPrintf("\n        :");
    for(j=0; j<=ch->gcr_width; j++)
    {
	if(j!=0)
	{
	    if(res[i][j] & GCRR)
		if(res[i][j] & GCRBLKM)
		    TxPrintf("|");
		else
		if((i<=ch->gcr_length) && (res[i+1][j] & GCRBLKM))
		    TxPrintf("|");
		else
		    TxPrintf("#");
	    else
	    if(((res[i][j] & GCRBLKM)&&(res[i][j] & GCRBLKP)) ||
	       ((res[i+1][j] & GCRBLKM)&&(res[i+1][j] & GCRBLKP)))
		TxPrintf("~");
	    else
	    if((res[i][j] & GCRBLKM) || (res[i+1][j] & GCRBLKM))
		TxPrintf("'");
	    else
	    if((res[i][j] & GCRBLKP) || (res[i+1][j] & GCRBLKP))
		TxPrintf("`");
	    else
		TxPrintf(" ");
	}
	else
	if((j!=0) && (i!=0))
	{
	    if(res[i-1][j] & GCRR)
		if(res[i-1][j] & GCRBLKM)
		    TxPrintf("|");
		else
		if((i<ch->gcr_length) && (res[i][j] & GCRBLKM))
		    TxPrintf("|");
		else
		    TxPrintf("#");
	    else
	    if(((res[i-1][j] & GCRBLKM)&&(res[i-1][j] & GCRBLKP)) ||
	       ((res[i  ][j] & GCRBLKM)&&(res[i  ][j] & GCRBLKP)))
		TxPrintf("~");
	    else
	    if((res[i-1][j] & GCRBLKM) || (res[i][j] & GCRBLKM))
		TxPrintf("'");
	    else
	    if((res[i-1][j] & GCRBLKP) || (res[i][j] & GCRBLKP))
		TxPrintf("`");
	    else
		TxPrintf(" ");
	}

	if((((res[i][j] & GCRBLKM) && (res[i][j] & GCRBLKP)) ||
	    ((res[i][j+1] & GCRBLKM) && (res[i][j+1] & GCRBLKP))) ||
	   (((res[i+1][j] & GCRBLKM) && (res[i+1][j] & GCRBLKP)) ||
	    ((res[i+1][j+1] & GCRBLKM) && (res[i+1][j+1] & GCRBLKP))))
	    TxPrintf("~");
	else
	if(((res[i][j] & GCRBLKM) || (res[i][j+1] & GCRBLKM))||
	   ((res[i+1][j] & GCRBLKM) || (res[i+1][j+1] & GCRBLKM)))
	    TxPrintf("'");
	else
	if(((res[i][j] & GCRBLKP) || (res[i][j+1] & GCRBLKP))||
	   ((res[i+1][j] & GCRBLKP) || (res[i+1][j+1] & GCRBLKP)))
	    TxPrintf("`");
	else
	    TxPrintf(" ");
    }
    TxPrintf(":\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrStats --
 *
 * 	Print routing statistics.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrStats(ch)
    GCRChannel * ch;
{
    int wireLength=0, viaCount=0, row, col;
    short **res, mask, code, code2;
    int hWire=0, vWire=0;

    viaCount=0;
    res=ch->gcr_result;
    for(col=0; col<=ch->gcr_length; col++)
	for(row=0; row<=ch->gcr_width; row++)
	{
	    code=res[col][row];
	    if(code & GCRR)
	    {
		wireLength++;
		hWire++;
	    }
	    if(code & GCRU)
	    {
		wireLength++;
		vWire++;
	    }
	    if(code & GCRX)
	    {
	    /* There is a connection at the crossing.  Count a contact if metal
	     * and poly come together here.
	     */
		mask=0;
		code2=ch->gcr_result[col][row+1];
		if(code&GCRU)
		{
		    if(code&GCRVM) mask|=GCRBLKM;	/*What type is up*/
		              else mask|=GCRBLKP;
		}
		code2=ch->gcr_result[col+1][row];
		if(code&GCRR)
		{
		    if(code2&GCRBLKM) mask|=GCRBLKP;	/*What type is right*/
			         else mask|=GCRBLKM;
		}
		code2=ch->gcr_result[col][row-1];
		if(code2&GCRU)
		{
		    if(code2&GCRVM) mask|=GCRBLKM;	/*What type is down*/
		               else mask|=GCRBLKP;
		}
		code2=ch->gcr_result[col-1][row];
		if(code2&GCRR)
		{
		    if(code2&GCRBLKM) mask|=GCRBLKP;	/*What type is left*/
			         else mask|=GCRBLKM;
		}
		if((mask!=GCRBLKM)&&(mask!=GCRBLKP))
		    viaCount++;
	    }
	}
    TxPrintf("Length :  %d\n", wireLength);
    TxPrintf("Vias   :  %d\n", viaCount);
    TxPrintf("Hwire  :  %d\n", hWire);
    TxPrintf("Vwire  :  %d\n", vWire);
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrCheckCol --
 *
 * Check the accuracy of the column's hi and lo pointers.  Abort if there
 * is a mistake.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrCheckCol(ch, c, where)
    GCRChannel *ch;
    int c;
    char *where;
{
    int i, j;
    GCRColEl * col;

    if(GcrNoCheck)
	return;
    col=ch->gcr_lCol;
    for(i=0; i<=ch->gcr_width; i++)
    {
	if( (col[i].gcr_hOk || col[i].gcr_lOk) && (col[i].gcr_h==(GCRNet *) 0))
	{
	    if(gcrStandalone)
	    {
	    TxError("Botch at column %d, %s (bad hOk/lOk at %d)\n",
	        c, where, i);
	    gcrDumpCol(col, ch->gcr_width);
	    }
	    if(GcrDebug)
	       niceabort();
	}
	if(((col[i].gcr_hi==i)||(col[i].gcr_lo==i))&&(i!=0))
	{
	    if(gcrStandalone)
	    {
	    TxError("Botch at column %d, %s(pointer loop at %d)\n",
		c, where, i);
	    gcrDumpCol(col, ch->gcr_width);
	    }
	    if(GcrDebug)
	       niceabort();
	}
	if(col[i].gcr_h!=(GCRNet *) NULL)
	
	/* Look upward from the track for the next higher track assigned to
	 * the net, if any.  Just take the first one, breaking afterwards.
	 */
	    for(j=i+1; j<=ch->gcr_width; j++)
	    {
		if(col[j].gcr_h==col[i].gcr_h)
		{
		/* Check to see if the lower track at i points to the higher
		 * track at j, and vice versa.  If an error, abort.
		 */
		    if( ((col[j].gcr_lo!=i) && !col[j].gcr_lOk &&
			!col[i].gcr_hOk) ||
		        ((col[i].gcr_hi!=j) && !col[i].gcr_hOk &&
			!col[j].gcr_lOk) )
		    {
			if(gcrStandalone)
			{
			TxError("Botch at column %d, %s", c, where);
			TxError(" (link error from %d to %d)\n", i, j);
			gcrDumpCol(col, ch->gcr_width);
			}
			if(GcrDebug) niceabort();
		    }
		    else break;
		}
	    }
	if((col[i].gcr_hi>ch->gcr_width)||(col[i].gcr_hi< EMPTY)||
	   (col[i].gcr_lo>ch->gcr_width)||(col[i].gcr_lo< EMPTY))
	{
	    if(gcrStandalone)
	    {
	    TxError("Botch at column %d, %s (bounds)\n", c, where);
	    gcrDumpCol(col, ch->gcr_width);
	    }
	    if(GcrDebug) niceabort();
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * gcrShowMap --
 *
 * Print the bit map in the result array for the selected field.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
gcrShowMap(ch)
    GCRChannel * ch;
{
    int i, j, field;
    short ** res;
    char buff[512];

    if (!GcrShowMap)
	return;

    while (1)
    {
	TxPrintf("Field selector (0 terminates): ");
	if(!scanf("%d", &field))	/*typed something funny*/
	{
	    TxPrintf("Bad input.  Legal responses are\n");
	    TxPrintf("   GCRBLKM     1\n");
	    TxPrintf("   GCRBLKP     2\n");
	    TxPrintf("   GCRU        4\n");
	    TxPrintf("   GCRR        8\n");
	    TxPrintf("   GCRX        16\n");
	    TxPrintf("   GCRVL       32\n");
	    TxPrintf("   GCRV2       64\n");
	    TxPrintf("   GCRTC       128\n");
	    TxPrintf("   GCRCC       256\n");
	    TxPrintf("   GCRTE       512\n");
	    TxPrintf("   GCRCE       1024\n");
	    TxPrintf("   GCRVM       2048\n");
	    TxPrintf("   GCRXX       4096\n");
	    TxPrintf("   GCRVR       8192\n");
	    TxPrintf("   GCRVU      16384\n");
	    TxPrintf("   GCRVD      32768\n");
	    (void) fgets(buff, 512, stdin);
	}
	TxPrintf("\n%d\n", field);
	if(field==0)
	    return;

	TxPrintf("\n     ");
	for(j=0; j<=ch->gcr_width+1; j++)
	    TxPrintf("%2d", j);

	for(i=0; i<=ch->gcr_length+1; i++)
	{
	    res=ch->gcr_result;
	    TxPrintf("\n[%3d] ", i);
	    for(j=0; j<=ch->gcr_width+1; j++)
	    {
		if(res[i][j] & field)
		    TxPrintf("1 ");
		else
		    TxPrintf(". ");
	    }
	}
	TxPrintf("\n");
    }
}
