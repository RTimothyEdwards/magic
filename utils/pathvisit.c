/*
 * pathvisit.c
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
 * This file contains several procedures to implement a means of scanning
 * all files in a search path, calling client procedures when lines match
 * a specified pattern.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/pathvisit.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/pathvisit.h"
#include "utils/utils.h"


/*
 * -------------------------------------------------------------------
 *
 * PaVisitInit --
 *
 * Return a newly allocated client structure that may be passed to
 * PaVisitAddClient and PaVisitFiles.
 *
 * Results:
 *	Returns a pointer to a newly allocated PaVisit struct.
 *
 * Side Effects:
 *	Allocates memory.
 *	PaVisitFree should be called when done.
 *
 * -------------------------------------------------------------------
 */

PaVisit *
PaVisitInit()
{
    PaVisit *pv;

    pv = (PaVisit *) mallocMagic((unsigned) (sizeof (PaVisit)));
    pv->pv_first = pv->pv_last = (PaVisitClient *) NULL;
    return (pv);
}

/*
 * -------------------------------------------------------------------
 *
 * PaVisitFree --
 *
 * Frees all memory associated with the argument PaVisit struct.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Deallocates memory.
 *
 * -------------------------------------------------------------------
 */

void
PaVisitFree(pv)
    PaVisit *pv;
{
    PaVisitClient *pvc;

    for (pvc = pv->pv_first; pvc; pvc = pvc->pvc_next)
    {
	if (pvc->pvc_keyword)
	    freeMagic(pvc->pvc_keyword);
	freeMagic((char *) pvc);
    }

    freeMagic((char *) pv);
}

/*
 * -------------------------------------------------------------------
 *
 * PaVisitAddClient --
 *
 * Add keywords and client procedures to the list that will be applied
 * by PaVisitFiles.  For each line in the files found by PaVisitFiles
 * that matches this keyword, we call the client procedure:
 *
 *	(*proc)(line, cdata)
 *	    char *line;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * This procedure should return 0 if PaVisitFiles should continue
 * processing, 1 if PaVisitFiles should stop processing completely,
 * and 2 if PaVisitFiles should stop processing just this file but
 * continue to the next file in the list.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Modifies the PaVisit struct pointed to by 'pv' by adding a
 *	new client to its list.
 *
 * -------------------------------------------------------------------
 */

void
PaVisitAddClient(pv, keyword, proc, cdata)
    PaVisit *pv;
    char *keyword;
    int  (*proc)();
    ClientData cdata;
{
    PaVisitClient *pvc;

    pvc = (PaVisitClient *) mallocMagic((unsigned) (sizeof (PaVisitClient)));
    pvc->pvc_keyword = StrDup((char **) NULL, keyword);
    pvc->pvc_proc = proc;
    pvc->pvc_cdata = cdata;
    pvc->pvc_next = (PaVisitClient *) NULL;
    if (pv->pv_last)
    {
	pv->pv_last->pvc_next = pvc;
	pv->pv_last = pvc;
    }
    else
    {
	pv->pv_first = pv->pv_last = pvc;
    }
}

/*
 * -------------------------------------------------------------------
 *
 * PaVisitFiles --
 *
 * PaVisitFiles will be used to visit all files in a search path.
 * For each such file, all lines matching one of the keywords supplied
 * to PaVisitAddClient will be passed along to a client procedure that
 * corresponds to that keyword.
 *
 * Lines in each of the input files that end in a backslash have the
 * following line appended to them and the backslash ignored.
 *
 * Results:
 *	Returns 0 if all files were visited.  Returns 1 if some
 *	client aborted the search by returning 1.
 *
 * Side Effects:
 *	Applies all the client procedures in the PaVisitClient
 *	list of pv to each line matching one of the client patterns.
 *
 * -------------------------------------------------------------------
 */

int
PaVisitFiles(path, file, pv)
    char *path;		/* Colon or space separated list of directories to
			 * search for the file 'file'.  If 'file' does not
			 * exist in a given directory, that directory is
			 * skipped.
			 */
    char *file;		/* If 'file' exists in a directory of 'path' we
			 * open it and match each line against the list
			 * of clients pointed to by 'pv'.
			 */
    PaVisit *pv;
{
    int paVisitFilesProc();

    /* Do no work if degenerate */
    if (pv->pv_first == (PaVisitClient *) NULL)
	return (0);

    return (PaEnum(path, file, paVisitFilesProc, (ClientData) pv));
}

/*
 *
 * paVisitFilesProc --
 *
 * Process each file in the path supplied to PaVisitFiles above.
 * If the file 'name' exists, we open it and process each line
 * as described in PaVisitFiles above.
 *
 * Results:
 *	Returns 0 if the file 'name' doesn't exist, or if it does
 *	exist and all the clients returned 0.  If a client returns
 *	2, we abort processing this file but still return 0.  If a
 * 	client returns 1, we abort processing and return 1 to abort
 *	the remainder of the path search.
 *
 * Side Effects:
 *	Applies all the client procedures in the PaVisitClient
 *	list of pv to each line matching one of the client patterns.
 *
 * -------------------------------------------------------------------
 */

int
paVisitFilesProc(name, pv)
    char *name;		/* Full filename */
    PaVisit *pv;	/* Points to list of clients */
{
    char *lp;
    char line[BUFSIZ+2];
    int code = 0;
    FILE *f;

    f = fopen(name, "r");
    if (f == NULL)
	return (0);

    lp = line;
    while (fgets(lp, BUFSIZ - (lp - line), f))
    {
	while (*lp && *lp != '\n')
	{
	    if (*lp++ == '\\' && *lp == '\n')
	    {
		*--lp = '\0';
		goto next;
	    }
	}
	*lp = '\0';
	if (code = paVisitProcess(lp = line, pv))
	    break;
next:	;
    }

    if (lp != line)
	code = paVisitProcess(line, pv);

    (void) fclose(f);
    if (code == 1)
	return (1);
    return (0);
}

/*
 * -------------------------------------------------------------------
 *
 * paVisitProcess --
 *
 * Process an extended line from an input file.  If the initial part
 * (up to the first white space) matches one of the keywords in the
 * client list pv->pv_first, we apply the associated procedure.
 *
 * Results:
 *	Returns the result of applying the first client procedure
 *	that returns a non-zero result, or zero if all clients
 *	return zero.  If there is no match, we return 0.
 *
 * Side Effects:
 *	Applies all the client procedures in the PaVisitClient
 *	whose keywords match 'line'.
 *
 * -------------------------------------------------------------------
 */

int
paVisitProcess(line, pv)
    char *line;
    PaVisit *pv;
{
    PaVisitClient *pvc;
    char *cp;
    int code = 0, len;

    for (cp = line; *cp && !isspace(*cp); cp++)
	/* Nothing */;
    len = cp - line;

    for (pvc = pv->pv_first; pvc; pvc = pvc->pvc_next)
	if (len > 0 && strncmp(line, pvc->pvc_keyword, len) == 0)
	    if (code = (*pvc->pvc_proc)(line, pvc->pvc_cdata))
		break;

    return (code);
}
