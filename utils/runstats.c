/*
 * runstats.c -
 *
 * This file provides a single procedure that returns a string
 * containing the amount of user and system time used so far,
 * as well as the size of the data area.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/runstats.c,v 1.2 2009/05/13 15:03:18 tim Exp $";
#endif  /* not lint */

#include <sys/types.h>
#include <sys/times.h>
#include <sys/time.h>
#include <stdio.h>

#include "utils/magic.h"
#include "utils/runstats.h"

/* Library imports: */

#ifndef __APPLE__
extern char *sbrk();
extern int end;
#else
#ifndef CYGWIN
extern void *sbrk();
int end;
#endif
#endif

/*
 * ----------------------------------------------------------------------------
 *
 * RunStats --
 *
 * This procedure collects information about the process.
 * Depending on the flags provided, the following information is
 * returned:
 *
 *	RS_TCUM	 -- cumulative user and system time
 *	RS_TINCR -- difference between current cumulative user and system
 *		    time and that when RunStats was last called with RS_TINCR
 *		    as a flag.
 *	RS_MEM	 -- number of bytes in the heap area.
 *
 * Results:
 *	The return value is a string of the form "[ ... <stuff> ...]",
 *	where <stuff> contains the information specified by the flags.
 *	Times are of the form "mins:secsu mins:secss", where the first
 *	time is the amount of user-space CPU time this process has
 *	used, and the second time is the amount of system time used.
 *	Memory is specified by a string of the form "Nk", where N
 *	is the number of kilobytes of heap area used so far.
 *
 * Side Effects:
 *	If RS_TINCR is specified, the parameters lastt and deltat
 *	are set (if they are both non-NULL).  Both point to tms structs;
 *	the one pointed to by deltat is set to the difference between
 *	the current user/system time and the time given in the tms struct
 *	pointed to by lastt; the one pointed to by lastt is then set to
 *	the current user/system time.
 *
 * ----------------------------------------------------------------------------
 */

char *
RunStats(flags, lastt, deltat)
    int flags;
    struct tms *lastt, *deltat;
{
    struct tms buffer;
    static char string[100];
    int umins, usecs, smins, ssecs, udsecs, sdsecs;
    pointertype size;
    char *sp = string;

    *sp = '\0';
    times(&buffer);
    if (flags & RS_TCUM)
    {
	umins = buffer.tms_utime;
	umins = (umins+30)/60;
	usecs = umins % 60;
	umins = umins/60;
	smins = buffer.tms_stime;
	smins = (smins+30)/60;
	ssecs = smins % 60;
	smins = smins/60;
	sprintf(sp, "%d:%02du %d:%02ds", umins, usecs, smins, ssecs);
	while (*sp) sp++;
    }

    if (flags & RS_TINCR)
    {
	umins = buffer.tms_utime - lastt->tms_utime;
	udsecs = umins % 6;
	umins = (umins+30)/60;
	usecs = umins % 60;
	umins = umins/60;
	smins = buffer.tms_stime - lastt->tms_stime;
	sdsecs = smins % 6;
	smins = (smins+30)/60;
	ssecs = smins % 60;
	smins = smins/60;

	if (deltat != (struct tms *) NULL)
	{
	    deltat->tms_utime = buffer.tms_utime - lastt->tms_utime;
	    deltat->tms_stime = buffer.tms_stime - lastt->tms_stime;
	    lastt->tms_utime = buffer.tms_utime;
	    lastt->tms_stime = buffer.tms_stime;
	}

	if (sp != string)
	    *sp++ = ' ';
	sprintf(sp, "%d:%02d.%du %d:%02d.%ds", umins, usecs, udsecs,
				smins, ssecs, sdsecs);
	while (*sp) sp++;
    }

#ifndef CYGWIN
    // Ignoring this under cygwin instead of trying to find a workaround
    if (flags & RS_MEM)
    {
	size = (((pointertype)sbrk(0) - (pointertype) &end) + 512)/1024;
	if (sp != string)
	    *sp++ = ' ';
	sprintf(sp, "%dk", (int)size);
    }
#endif

    return (string);
}

/*
 * ----------------------------------------------------------------------------
 *
 * RunStatsRealTime --
 *
 * Reports the real time, both since the first invocation and incremental
 * since the last invocation.
 *
 * Results:
 *	A statically allocated string of the form:
 *		x:xx.x x:xx.x
 *	where the first number is the amount of elapsed real time since the
 *	first call to this routine, and the second is the amount of elapsed
 *	real time since the lastest call to this routine.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
RunStatsRealTime()
{
    struct timeval curtime;
    static struct timeval firsttime, lasttime;
    static int havetime = 0;
    long totm, tots, tott, incm, incs, inct;
    struct timezone dummyz;
    static char buf[50];

    gettimeofday(&curtime, &dummyz);
    if (!havetime)
    {
	havetime = 1;
	firsttime = curtime;
	lasttime = curtime;
    }

    /*
     * Compute time differences in minutes, seconds, and tenths.
     */

    totm = (curtime.tv_sec - firsttime.tv_sec) / 60;
    tots = (curtime.tv_sec - firsttime.tv_sec) % 60;
    tott = curtime.tv_usec - firsttime.tv_usec;

    while (tott < 0) { tots--; tott += 1000000; }
    while (tots < 0) { tots += 60; totm--; }
    tott = (tott + 50000) / 100000;
    while (tott >= 10) { tott -= 10; tots++; }
    while (tots >= 60) { tots -= 60;  totm++; }

    incm = (curtime.tv_sec - lasttime.tv_sec) / 60;
    incs = (curtime.tv_sec - lasttime.tv_sec) % 60;
    inct = curtime.tv_usec - lasttime.tv_usec;

    while (inct < 0) { incs--; inct += 1000000; }
    while (incs < 0) { incs += 60; incm--; }
    inct = (inct + 50000) / 100000;
    while (inct >= 10) { inct -= 10; incs++; }
    while (incs >= 60) { incs -= 60;  incm++; }

    sprintf(buf, "%ld:%02ld.%ld %ld:%02ld.%ld",
	totm, tots, tott, incm, incs, inct);

    lasttime = curtime;

    return buf;
}

