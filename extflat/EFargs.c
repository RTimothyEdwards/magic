/*
 * EFargs.c -
 *
 * General command-line argument processing and overall initialization
 * for the .ext file flattener.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/extflat/EFargs.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/paths.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "utils/pathvisit.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"

#define        atoCap(s)       ((EFCapValue)atof(s))

/* --------------------- Visible outside extflat ---------------------- */

    /* Command-line flags */
EFCapValue EFCapThreshold = 2;  /* -c/-C: (fF) smallest interesting C */
int EFResistThreshold = 10;	/* -r/-R: (Ohms) smallest interesting R */
int EFTrimFlags = 0;		/* -t: output of nodename trailing #!'s */
char *EFSearchPath = NULL;	/* -p: Search path for .ext files */
char *EFArgTech = NULL;		/* -T: Tech specified on command line */

    /* Misc globals */
float EFScale = 0.0;		/* Uninitialized scale factor */
char *EFVersion = MAGIC_VERSION;/* Version number of .ext format we read */
char *EFLibPath = NULL;		/* Library search path for .ext files */
char *EFTech = NULL;
char *EFStyle = NULL;		/* Start with no extraction style */
bool  EFCompat = TRUE;		/* Start with backwards compatibility enabled */

#ifdef MAGIC_WRAPPER
extern char     *Path;		/* magic's search path---note this should  */
				/* be done with #include "utils/main.h" but */
				/* this is easier.			   */
#endif


/* -------------------- Visible only inside extflat ------------------- */

    /* Command-line flags */
bool efWarn = FALSE;		/* -v: Warn about duplicate node names */
bool efHNStats = FALSE;		/* -z: TRUE if we gather mem usage stats */
bool efWatchNodes = FALSE;	/* -n: TRUE if watching nodes in table below */
HashTable efWatchTable;		/* -n: Names to watch, keyed by HierName */

    /* Misc globals */
int efResists[128];		/* Sheet resistivity for each resist class */
int efNumResistClasses = 0;	/* Number of resist classes */
bool efResistChanged = FALSE;	/* TRUE if .ext resist classes mismatch */
bool efScaleChanged = FALSE;	/* TRUE if .ext scales mismatch */

    /* Forward declarations */
#ifndef MAGIC_WRAPPER
extern int  efLoadPathFunc();
extern void efLoadSearchPath();
#endif

/*
 * ----------------------------------------------------------------------------
 *
 * EFArgs --
 *
 * Process command-line arguments that are relevant to the extractor
 * flattener.  Arguments that are specific to the calling function
 * are processed by the procedure (*argsProc)(), which should
 * have the following form:
 *
 *	(*argsProc)(pargc, pargv, cdata)
 *	    int *pargc;
 *	    char ***pargv;
 *	    ClientData cdata;
 *	{
 *	}
 *
 * If we don't recognize an argument, we call (*argsProc)() with
 * *pargc and *pargv pointing to the position in the argument
 * vector that we didn't recognize.  If (*argsProc)() also doesn't
 * recognize the argument, it exits; otherwise, it updates *pargc
 * and *pargv to point past the argument it gobbled off and returns.
 * If argsProc is NULL, then any arguments we don't recognize are
 * considered errors.
 *
 * Arguments processed are:
 *
 *	-T techname	Specify the name of the technology, leaving
 *			EFArgTech pointing to the technology name.
 *	-p path		Use the colon-separated search path 'path'
 *			for finding .ext files.  Overrides any paths
 *			found in .magicrc files.
 *	-s sym=value	Set the name 'sym' in the symbol hash table to
 *			have value 'value', where 'value' is an integer.
 *			Certain attributes interpreted during circuit
 *			flattening may have symbolic values; the -s flag
 *			provides a means of associating a numeric value
 *			with a symbol.
 *	-S symfile	Read the file 'symfile', which should consist of
 *			lines of the form sym=value, processing each line
 *			as though it were an argument to -s.
 *
 * The following flags are for debugging purposes only:
 *	-n nodename	For debugging: print all merges involving
 *			the node named 'nodename'.
 *	-N nodefile	For debugging: print all merges involving
 *			any of the nodes whose names appear in the
 *			file 'nodefile' (one node name per line).
 *	-v		Warn about unusual occurrences while flattening
 *			the circuit; mainly for debugging.
 *	-z		Print memory utilized for names.
 *
 * Results:
 *	Returns a pointer to a string containing the base name
 *	of the input .ext file.
 *
 * Side effects:
 *	Can set global variables based on the values of command-line
 *	arguments.
 *	err_result, if non-null, is set to TRUE if an error occurred.
 *	err_result is only used by the Tcl version of Magic.
 *
 * ----------------------------------------------------------------------------
 */

char *
EFArgs(argc, argv, err_result, argsProc, cdata)
    int argc;		/* Number of command-line args */
    char *argv[];	/* Vector of command-line args */
    bool *err_result;	/* Set to TRUE if error occurs */
    bool (*argsProc)();	/* Called for args we don't recognize */
    ClientData cdata;	/* Passed to (*argsProc)() */
{
    static char libpath[FNSIZE];
    char *realIn, line[1024], *inname = NULL, *name, *cp;
    HierName *hierName;
    FILE *f;

    if (err_result != NULL) *err_result = FALSE;

    /* Hash table of nodes we're going to watch if -N given */
    HashInitClient(&efWatchTable, 32, HT_CLIENTKEYS,
	efHNCompare, (char *(*)()) NULL,
	efHNHash, (int (*)()) NULL);

    /* Process command line options */
    for (argc--, argv++; argc-- > 0; argv++)
    {
	if (argv[0][0] != '-')
	{
	    if (inname)
	    {
		TxError("Warning: multiple input files specified; ");
		TxError("ignoring \"%s\"\n", inname);
	    }
	    inname = argv[0];
	    continue;
	}

	switch (argv[0][1])
	{
	    /*** NORMAL OPTIONS ***/
	    case 'c':
		if ((cp = ArgStr(&argc, &argv, "cap threshold")) == NULL)
		    goto usage;
		EFCapThreshold = atoCap(cp);	/* Femtofarads */
		break;
	    case 'p':
		EFSearchPath = ArgStr(&argc, &argv, "search path");
		if (EFSearchPath == NULL)
		    goto usage;
		break;
	    case 'r':
		if ((cp = ArgStr(&argc, &argv, "resist threshold")) == NULL)
		    goto usage;
		EFResistThreshold = atoi(cp);	/* Ohms */
		break;
	    case 's':
		if ((cp = ArgStr(&argc, &argv, "symbolic name")) == NULL)
		    goto usage;
		efSymAdd(cp);
		break;
	    case 't':
		if ((cp = ArgStr(&argc, &argv, "trim characters")) == NULL)
		    goto usage;
		if (strchr(cp, '!')) EFTrimFlags |= EF_TRIMGLOB;
		if (strchr(cp, '#')) EFTrimFlags |= EF_TRIMLOCAL;
		if (strchr(cp, ',')) EFTrimFlags |= EF_CONVERTCOMMAS;
		if (strchr(cp, '=')) EFTrimFlags |= EF_CONVERTEQUAL;
		break;
	    case 'C':
		EFCapThreshold = (EFCapValue)INFINITE_THRESHOLD_F;
		break;
	    case 'R':
		EFResistThreshold = INFINITE_THRESHOLD;
		break;
	    case 'S':
		if ((cp = ArgStr(&argc, &argv, "symbol file")) == NULL)
		    goto usage;
		efSymAddFile(cp);
		break;

#ifndef MAGIC_WRAPPER
	    case 'T':
		if ((EFArgTech = ArgStr(&argc, &argv, "tech name")) == NULL)
		    goto usage;
		break;
#endif

	    /*** OPTIONS FOR DEBUGGING ***/
	    case 'n':
		if ((name = ArgStr(&argc, &argv, "nodename")) == NULL)
		    goto usage;
		printf("Watching node '%s'\n", name);
		hierName = EFStrToHN((HierName *) NULL, name);
		(void) HashFind(&efWatchTable, (char *) hierName);
		efWatchNodes = TRUE;
		break;
	    case 'N':
		if ((name = ArgStr(&argc, &argv, "filename")) == NULL)
		    goto usage;

		/* Add everything in the file to the hash table */
		f = fopen(name, "r");
		if (f == NULL)
		{
		    perror(name);
		    break;
		}
		while (fgets(line, sizeof line, f))
		{
		    cp = strchr(line, '\n');
		    if (cp) *cp = '\0';
		    printf("Watching node '%s'\n", line);
		    hierName = EFStrToHN((HierName *) NULL, line);
		    (void) HashFind(&efWatchTable, (char *) hierName);
		}
		(void) fclose(f);
		efWatchNodes = TRUE;
		break;
	    case 'v':
		efWarn = TRUE;
		break;
	    case 'z':
		efHNStats = TRUE;
		break;

	    /*** Try a caller-supplied argument processing function ***/
	    default:
		if (argsProc == NULL)
		    goto usage;
		if ((*argsProc)(&argc, &argv, cdata))
		{
		    TxError("\n");
		    goto usage;
		}
		break;
	}
    }

    /* Find the search path if one was not specified */
    if (EFSearchPath == NULL)
#ifdef MAGIC_WRAPPER
	/* Set the search path to be the same as magic's search path */
	EFSearchPath = StrDup(NULL, Path);
#else
	efLoadSearchPath(&EFSearchPath);
#endif

    EFLibPath = libpath;
    *EFLibPath = 0; /* start with no path */
    if (EFArgTech) (void) sprintf(EFLibPath, EXT_PATH, EFArgTech);

    if (inname == NULL)
#ifdef MAGIC_WRAPPER
	return NULL;
#else
	goto usage;
#endif

    /* Eliminate trailing .ext from input name */
    if ((cp = strrchr(inname, '.')) && strcmp(cp, ".ext") == 0)
    {
	realIn = (char *) mallocMagic((unsigned)(cp - inname + 1));
	(void) strncpy(realIn, inname, cp - inname);
	realIn[cp - inname] = '\0';
	inname = realIn;
    }

    return inname;

usage:
    TxError("Standard arguments: [-R] [-C] [-r rthresh] [-c cthresh] [-v]\n"
		"[-p searchpath] [-s sym=value] [-S symfile] [-t trimchars]\n"

#ifdef MAGIC_WRAPPER
		"[rootfile]\n");
    if (err_result != NULL) *err_result = TRUE;
    return NULL;
#else
		"[-T techname] rootfile\n");
    exit (1);
    /*NOTREACHED*/
#endif
}

#ifndef MAGIC_WRAPPER

/*
 * ----------------------------------------------------------------------------
 *
 * efLoadSearchPath --
 *
 * Load the search path string pointed to by 'path'
 * with whatever is specified in the .magicrc files
 * in $CAD_ROOT/magic/sys, ~, and ., searched in that
 * order with the last path taking precedence. See paths.h.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Leaves *path pointing to the correct search path,
 *	which may either be static or allocated via StrDup().
 *
 * ----------------------------------------------------------------------------
 */

void
efLoadSearchPath(path)
    char **path;
{
    PaVisit *pv;

    *path = NULL;
    pv = PaVisitInit();
    PaVisitAddClient(pv, "path", efLoadPathFunc, (ClientData) path);
    PaVisitFiles(DOT_MAGIC_PATH, ".magicrc", pv);
    PaVisitFree(pv);
    if (*path == NULL)
	*path = ".";
}

int
efLoadPathFunc(line, ppath)
    char *line;
    char **ppath;
{
    char *cp, *dp, c;
    char path[BUFSIZ];

    /* Skip leading blanks */
    for (cp = &line[4]; *cp && isspace(*cp); cp++)
	/* Nothing */;

    /* Copy the path into 'path' */
    for (dp = path; (c = *cp++) && !isspace(c) && c != '\n'; )
    {
	if (c == '"')
	{
	    while ((c = *cp++) && c != '"')
		*dp++ = c;
	    if (c == '\0')
		break;
	}
	else *dp++ = c;
    }
    *dp = '\0';
    (void) StrDup(ppath, path);
    return 0;			/* continue search */
}

#endif
