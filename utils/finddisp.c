/*
 * finddisplay.c -
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
 * This file contains a single routine that will find the correct
 * graphics display to use from the current terminal and return
 * parameters for the display, such as its type and the location
 * of the tablet port.  It is used as part of the Caesar design
 * system.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/finddisp.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/utils.h"

/* Import from library routines: */

extern char *ttyname();


/*
 * ----------------------------------------------------------------------------
 *	FindDisplay --
 *
 * 	This procedure locates information about the color display
 *	to use from a given terminal (determined by tty or the name
 *	of the standard input file) by looking in the file passed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The parameters pDisplayPort, pTabletPort, pDisType, and
 *	pMonType all point to strings that are modified.  *pDisplayPort
 *	is filled with the name of the port that connects to the graphics
 *	display.  *pTabletPort is filled in with the name of the port to be
 *	used to receive characters from the tablet.  *pDisType is filled
 *	in with the name of the type of display ("OGL", etc.), and
 *	*pMonType is filled in with the name of the monitor type attached
 *	to the display (used to select color maps).
 *
 *	If the file is not found or if there is not a suitable line then
 *	nothing is modified.
 *
 * File format:
 *	Each line in the displays file contains up to five strings
 *	which are, in order, the name of a text terminal, the name
 *	of the corresponding graphics display port, the monitor type
 *	attached to that display, the name of the display type, and
 *	the name of the tablet port to use for that display.  The
 *	tablet port can be omitted, in which case the display port
 *	is used.
 * ----------------------------------------------------------------------------
 */

void
FindDisplay(tty, file, path, pDisplayPort, pTabletPort, pDisType, pMonType)
    char *tty;			/* Terminal for which display info is desired.
				 * If NULL, used device on stdin.
				 */
    char *file;			/* Name of file in which to find info. */
    char *path;			/* Path to use for lookup. */
    char **pDisplayPort;	/* Will be filled in with the name of the
				 * port for the graphics display.
				 */
    char **pTabletPort;		/* Will be filled in with the name of the 
				 * tablet. */
    char **pDisType;		/* Will be filled in with the display type. */
    char **pMonType;		/* Will be filled in with the monitor type. */

{
    int i;
    char line[100], name1[100];
    static char name2[100], mon[100], dType[100], tabletPort[100];
    FILE *f;

    if (tty == NULL) tty = ttyname(fileno(stdin));
    if (tty == NULL) return;
    f = PaOpen(file, "r", (char *) NULL, path, (char *) NULL, (char **) NULL);
    if (f == NULL) return;
    while (1)
    {
	if (fgets(line, 99, f) == NULL)
	{
	    (void) fclose(f);
	    return;
	}
	i = sscanf(line, "%99s %99s %99s %99s %99s", name1, name2,
	    mon, dType, tabletPort);
	if (i < 4)
	{
	    (void) fclose(f);
	    return;
	}
	if (strcmp(name1, tty) == 0)
	{
	    (void) fclose(f);
	    *pDisplayPort = name2;
	    *pMonType = mon;
	    *pDisType = dType;
	    if (i >= 5) *pTabletPort = tabletPort;
	    else *pTabletPort = name2;
	    return;
	}
    }
}
