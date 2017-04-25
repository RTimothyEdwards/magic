/* path.c
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
 * This file contains routines that implement a path mechanism, whereby
 * several places may be searched for files.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/path.c,v 1.3 2009/05/13 15:03:18 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/param.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"

/* Library routines: */

/* extern char *getenv(); */
/* extern char *strcmp(); */

/* A hash table is used to keep track of the logins and variable expansions we've
 * already looked up.
 */

static HashTable expansionTable;
static bool noTable = TRUE;

/* Limit on how long a single file name may be: */

#define MAXSIZE MAXPATHLEN

/*-------------------------------------------------------------------
 * PaAppend --
 *	Add a string to the designated path variable.
 *
 *	pathptr is a pointer to the path variable;  srcptr is the
 *	newstring is the new string to append to the path. 
 */
void
PaAppend(char **pathptr, char *newstring)
{
    int oldlength, addlength;
    char *new;

    oldlength = strlen(*pathptr);
    addlength = strlen(newstring);
    new = (char *)mallocMagic((unsigned) (oldlength + addlength + 2));
    (void) strcpy(new, *pathptr);
    new[oldlength] = ' ';
    (void) strcpy(new + oldlength + 1, newstring);
    freeMagic(*pathptr);
    *pathptr = new;
}


/*-------------------------------------------------------------------
 * PaExpand --
 *	This routine converts tilde notation and environment variable
 *	notation into standard directory names.
 *
 * Results:
 *	If the conversion was done successfully, then the return value
 *	is the number of bytes of space left in the destination area.
 *	If a user name couldn't be found in the password file, then
 *	-1 is returned.
 *
 * Side Effects:
 *	If the first character of the string indicated by psource is a
 *	tilde ("~") then the subsequent user name is converted to a login
 *	directory name and stored in the string indicated by dest.  Then
 *	remaining characters in the file name at psource are copied to
 *	pdest (the file name is terminated by white space, a null character,
 *	or a colon) and psource is updated.  Upon return, psource points
 *	to the terminating character in the source file name, and pdest
 *	points to the null character terminating the expanded name.
 *	If a tilde cannot be converted because the user name cannot
 *	be found, psource is still advanced past the current entry, but
 *	nothing	is stored at the destination.  At most size characters
 *	(including the terminating null character) will be stored at pdest.
 *	Note:  the name "~" with no user name expands to the home directory.
 *
 *	If the first character of the string indicated by psource is a
 *	dollar sign ("$") then the subsequent name is converted to the
 *	expansion of the indicated environment variable.  If the environment
 *	variable does not exist and the indicated name is "CAD_ROOT", then
 *	we substitute the value of CAD_DIR passed down from the Makefile.
 *	Otherwise, we return error status -1.
 *
 *-------------------------------------------------------------------
 */

int
PaExpand(psource, pdest, size)
    char **psource;		/* Pointer to a pointer to the source string */
    char **pdest;		/* Pointer to a ptr to dest string area. */
    int size;			/* Number of bytes available at pdest */

{
    char *ps, *pd;
    struct passwd *passwd, *getpwnam();
    char expandName[100], *string, *newEntry;
    HashEntry *h;
    int i, length;

    size -= 1;
    ps = *psource;
    if (*ps == '~')
    {
	/* Strip off the login name from the front of the file name. */

	pd = expandName;
	for (i=0; ; i++)
	{
	    *pd = *++ps;
	    if (isspace(*pd) || (*pd=='\0') || (*pd=='/') || (*pd==':'))
		break;
	    if (i < 99) pd++;
	}
	*pd = '\0';

	/* Lookup the login name in the hash table.  Create a hash
	 * table if we don't have one already.
	 */

	if (noTable)
	{
	    HashInit(&expansionTable, 16, 0);
	    noTable = FALSE;
	}
	h = HashFind(&expansionTable, expandName);
	string = HashGetValue(h);
	if (string != 0) goto gotname;

	/* We haven't seen this name before.  Look it up in the
	 * password file.  If the name is "~", then just use the
	 * home directory.
	 */

	if (strcmp(expandName, "") == 0) 
	    string = getenv("HOME");
	else
	{
	    string = NULL;
	    passwd = getpwnam(expandName);
	    if (passwd != NULL) string = passwd->pw_dir;
	}
	if (string != NULL)
	{
	    newEntry = (char *) mallocMagic((unsigned) (strlen(string) + 1));
	    (void) strcpy(newEntry, string);
	    HashSetValue(h, newEntry);
	}
	else
	{
	    /* No login entry.  Skip the rest of the file name. */

	    while ((*ps != '\0') && !isspace(*ps) && (*ps != ':')) ps++;
	    *psource = ps;
	    return -1;
	}

	gotname: length = strlen(string);
	if (length > size) length = size;
	(void) strncpy(*pdest, string, length+1);
	size -= length;
	pd = *pdest+length;
    }
    else if (*ps == '$')	/* (Possible) environment variable expansion */
    {
	char saveChar;

	pd = expandName;
	for (i=0; ; i++)
	{
	    *pd = *++ps;
	    if (isspace(*pd) || (*pd=='\0') || (*pd=='/') || (*pd==':'))
		break;
	    if (i < 99) pd++;
	}
	saveChar = *pd;
	*pd = '\0';

	/* Lookup the environment variable in the hash table.  Make a new
	 * table if we don't have one already.
	 */

	if (noTable)
	{
	    HashInit(&expansionTable, 16, 0);
	    noTable = FALSE;
	}
	h = HashFind(&expansionTable, expandName);
	string = HashGetValue(h);
	if (string != 0) goto gotvar;

	/* We haven't seen this name before.  Get it with "getenv".
	 */

#ifdef MAGIC_WRAPPER
	string = (char *)Tcl_GetVar(magicinterp, expandName, TCL_GLOBAL_ONLY);
#else
	string = getenv(expandName);
#endif

	if (string == NULL)
	{
	    /* Check for CAD_ROOT = CAD_DIR, the only internal variable */
	    /* recognized (this is passed down from the Makefile)	*/

	    if (!strcmp(expandName, "CAD_ROOT"))
		string = CAD_DIR;
	    else
	    {
		*pd = saveChar;
		ps = *psource;
		goto noexpand;
	    }
	}

	newEntry = (char *) mallocMagic((unsigned) (strlen(string) + 1));
	(void) strcpy(newEntry, string);
	HashSetValue(h, newEntry);

	gotvar: length = strlen(string);
	if (length > size) length = size;
	(void) strncpy(*pdest, string, length+1);
	size -= length;
	pd = *pdest+length;
    }
    else
    {
	/* No tilde or variable to expand.  As a minor convenience, check
	 * to see if the first two characters of the name are "./".  If so,
	 * then just skip over them.
	 */

noexpand:
	while (ps[0] == '.')
	{
	    if (ps[1] == '/') ps += 2;
	    else
	    {
		if ((ps[1] == 0) || (ps[1] == ':') || isspace(ps[1]))
		    ps += 1;
		break;
	    }
	}
        pd = *pdest;
    }

    /* Copy the rest of the directory name from the source to the dest. */

    while ((*ps != '\0') && !isspace(*ps) && (*ps != ':'))
	if (size > 0)
	{
	    *pd++ = *ps++;
	    size--;
	}
	else ps++;
    *pd = 0;
    *psource = ps;
    *pdest = pd;
    return size;
}


/* ----------------------------------------------------------------------------
 * nextName --
 *	This local procedure is used to step through a path, adding a
 *	directory name from the path to a file name.
 *
 * Results:
 *	The return value is a pointer to a path-extended name, or
 *	NULL if the end of the path has been reached.  If a tilde
 *	couldn't be expanded, then a zero-length string is returned.
 *
 * Side effects:
 *	The pointer at *ppath is updated to refer to the terminating
 *	character of the path entry used this time.
 * ----------------------------------------------------------------------------
 */

char *
nextName(ppath, file, dest, size)
    char **ppath;		/* Pointer to a pointer to the next
				 * entry in the path.
				 */
    char *file;			/* Pointer to a file name. */
    char *dest;			/* Place to build result name. */
    int size;			/* Size of result area. */

{
    char *p;

    /* Don't bother with NULL paths */
    if (*ppath == 0) return NULL;

    /* Skip leading blanks and colons.  Then make sure that there's
     * another entry in the path.
     */
    while (isspace(**ppath) || (**ppath == ':')) *ppath += 1;
    if (**ppath == 0) return NULL;

    /* Grab the next directory name and terminate it with a slash if
     * there isn't one there already.
     */

    p = dest;
    dest[size-1] = 0;
    size = PaExpand(ppath, &p, size);
    if (**ppath) *ppath += 1;		 /* Skip the terminating character. */
    if (size < 0)
    {
	dest[0] = 0;
	return dest;
    }
    if ((p != dest) && (*(p-1) != '/'))
    {
	*p++ = '/';
	size -= 1;
    }
    if (size < strlen(file)) strncpy(p, file, size);
    else strcpy(p, file);
    return dest;
}

/*-------------------------------------------------------------------
 * PaLockOpen --
 *	This routine does a file lookup using the current path and
 *	supplying a default extension.
 *
 * Results:
 *	A pointer to a FILE, or NULL if the file couldn't be found.
 *
 * Side Effects:
 *	If ext is specified, then it is tacked onto the end of
 *	the given file name.  If the first character of the
 *	file name is "~" or "/" or if nosearch is TRUE, then we try
 *	to look up the file with the original name, doing tilde
 *	expansion of course and returning that result.  If none of 
 *	these conditions is met, we go through the path	trying to
 *	look up the file once for each path entry by prepending the
 *	path entry to the original file name. This concatenated name
 *	is stored in a static string and made available to the caller
 *	through prealName if the open succeeds.  If the entire path is
 *	tried, and still nothing works, then we try each entry in the
 *	library path next.
 *	Note: the static string will be trashed on the next call to this
 *	routine.  Also, note that no individual file name is allowed to
 *	be more than MAXSIZE characters long.  Excess characters are lost.
 *
 * Path Format:
 *	A path is a string containing directory names separated by
 *	colons or white space.  Tilde notation may be used within paths.
 *-------------------------------------------------------------------
 */

FILE *
PaLockOpen(file, mode, ext, path, library, pRealName, is_locked)
    char *file;			/* Name of the file to be opened. */
    char *mode;			/* The file mode, as given to fopen. */
    char *ext;			/* The extension to be added to the file name,
				 * or NULL.  Note:  this string must include
				 * the dot (or whatever separator you use).
				 */
    char *path;			/* A search path:  a list of directory names
				 * separated by colons or blanks.  To use
				 * only the working directory, use "." for
				 * the path.
				 */
    char *library;		/* A 2nd path containing library names.  Can be
				 * NULL to indicate no library.
				 */
    char **pRealName;		/* Pointer to a location that will be filled
				 * in with the address of the real name of
				 * the file that was successfully opened.
				 * If NULL, then nothing is stored.
				 */
    bool *is_locked;		/* Pointer to a location to store the result
				 * of the attempt to grab an advisory lock
				 * on the file.  If NULL, then nothing is
				 * stored.
				 */
{
    char extendedName[MAXSIZE], *p1, *p2;
    static char realName[MAXSIZE];
    int length, extLength, i;
    FILE *f;

    if (file == NULL) return (FILE *) NULL;
    if (file[0] == '\0') return (FILE *) NULL;
    if (pRealName != NULL) (*pRealName) = realName;

    /* See if we must supply an extension. */

    length = strlen(file);
    if (length >= MAXSIZE) length = MAXSIZE - 1;
    if (ext != NULL)
    {
	(void) strncpy(extendedName, file, length + 1);
	i = MAXSIZE - 1 - length;
	extLength = strlen(ext);
	if (extLength > i) extLength = i;

	/* (Modified by Tim, 1/13/2015;  assume that "file" has	*/
	/* the extension already stripped, therefore always add	*/
	/* the extension if one is specified.  This allows the	*/
	/* code to distinguish between, say, "a.mag" and	*/
	/* "a.mag.mag".)					*/

	/* If the extension is already on the name, don't add it */
	// if ((length < extLength) || ((extLength > 0)
	//		&& (strcmp(ext, file + length - extLength))))

	    (void) strncpy(&(extendedName[length]), ext, extLength + 1);

	extendedName[MAXSIZE-1] = '\0';
	file = extendedName;
    }

    /* If the first character of the file name is a tilde or dollar sign,
     * do tilde or environment variable expansion but don't touch a search
     * path.
     */

    if (file[0] == '~' || file[0] == '$')
    {
	p1 = realName;
	p2 = file;
	if (PaExpand(&p2, &p1, MAXSIZE) < 0) return NULL;

#ifndef FILE_LOCKS
	return fopen(realName, mode);
#else
	return flock_open(realName, mode, is_locked);
#endif
    }

    /* If we were already given a full rooted file name,
     * or a relative pathname, just use it.
     */

    if (file[0] == '/'
	    || (file[0] == '.' && (strcmp(file, ".") == 0
				|| strncmp(file, "./", 2) == 0
				|| strcmp(file, "..") == 0
				|| strncmp(file, "../", 3) == 0)))
    {
	(void) strncpy(realName, file, MAXSIZE-1);
	realName[MAXSIZE-1] = '\0';
#ifndef FILE_LOCKS
	return fopen(realName, mode);
#else
	return flock_open(realName, mode, is_locked);
#endif
    }

    /* Now try going through the path, one entry at a time. */

    while (nextName(&path, file, realName, MAXSIZE) != NULL)
    {
	if (*realName == 0) continue;
#ifndef FILE_LOCKS
	f = fopen(realName, mode);
#else
	f = flock_open(realName, mode, is_locked);
#endif
	if (f != NULL) return f;

	// If any error other than "file not found" occurred,
	// then halt immediately.
	if (errno != ENOENT) return NULL;
    }

    /* We've tried the path and that didn't work.  Now go through
     * the library area, one entry at a time.
     */

    if (library == NULL) return NULL;
    while (nextName(&library, file, realName, MAXSIZE) != NULL)
    {
#ifndef FILE_LOCKS
	f = fopen(realName, mode);
#else
	f = flock_open(realName, mode, is_locked);
#endif
	if (f != NULL) return f;

	// If any error other than "file not found" occurred,
	// then halt immediately.
	if (errno != ENOENT) return NULL;
    }

    return NULL;
}

/*
 *-------------------------------------------------------------------
 * PaOpen --
 *	This is a wrapper for PaLockOpen() that is backwardly-
 *	compatible with the original (non-file-locking) PaOpen(),
 *	and is here because it's a pain to add an extra argument
 *	to every call to PaOpen() when only .mag files are locked.
 *
 * Results:
 *	See PaLockOpen()
 *
 * Side Effects:
 *	See PaLockOpen()
 *
 *-------------------------------------------------------------------
 */

FILE *
PaOpen(file, mode, ext, path, library, pRealName)
    char *file;			/* Name of the file to be opened. */
    char *mode;			/* The file mode, as given to fopen. */
    char *ext;			/* The extension to be added to the file name,
				 * or NULL.  Note:  this string must include
				 * the dot (or whatever separator you use).
				 */
    char *path;			/* A search path:  a list of directory names
				 * separated by colons or blanks.  To use
				 * only the working directory, use "." for
				 * the path.
				 */
    char *library;		/* A 2nd path containing library names.  Can be
				 * NULL to indicate no library.
				 */
    char **pRealName;		/* Pointer to a location that will be filled
				 * in with the address of the real name of
				 * the file that was successfully opened.
				 * If NULL, then nothing is stored.
				 */
{
    return PaLockOpen(file, mode, ext, path, library, pRealName, NULL);
}

/*
 * ----------------------------------------------------------------------------
 *	PaSubsWD --
 *
 *	Replaces all uses of the working directory in a path
 *	by some fixed directory.
 *
 * Results:
 *	The return result is a path that is just like the path
 *	argument except that every implicit or explicit use of
 *	the working directory is replaced by the newWD argument.
 *	The result is a static array, which will be trashed on
 *	the next call to this procedure.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

char *
PaSubsWD(path, newWD)
char *path;			/* Path in which to substitute. */
char *newWD;			/* New working directory to be used.  Must
				 * end in a slash.
				 */

{
#define NEWPATHSIZE 1000
    static char newPath[NEWPATHSIZE];
    char *pOld, *pNew, *pWD;
    int spaceLeft;

    pOld = path;
    pNew = newPath;
    spaceLeft = NEWPATHSIZE;

    while (TRUE)
    {
	/* Scan through the old path, copying separators to the new
	 * path until getting the first character of the directory.
	 */
	
	while (isspace(*pOld) || (*pOld == ':'))
	{
	    if (spaceLeft <= 0) goto subsDone;
	    spaceLeft -= 1;
	    *pNew++ = *pOld++;
	}

	/* If the first character of the directory isn't a "/", "$", or "~",
	 * then add in the new directory name in front of it in newPath.
	 */
	
	if ((*pOld != '/') && (*pOld != '~') && (*pOld != '$') && (*pOld != 0))
	{
	    pWD = newWD;
	    while (*pWD != 0)
	    {
		if (spaceLeft <= 0) goto subsDone;
		spaceLeft -= 1;
		*pNew++ = *pWD++;
	    }
	}

	/* Add in the rest of the path entry from the old path to the new. */

	while ((!isspace(*pOld)) && (*pOld != ':') && (*pOld != 0))
	{
	    if (spaceLeft <= 0) goto subsDone;
	    spaceLeft -= 1;
	    *pNew++ = *pOld++;
	}

	/* See if we're done. */

	if (*pOld == 0) break;
    }

    subsDone: if (spaceLeft > 0) *pNew = 0;
    else newPath[NEWPATHSIZE-1] = 0;
    return newPath;
}

/*
 * ----------------------------------------------------------------------------
 *	PaEnum --
 *
 *	Call a client procedure with each directory in a path
 *	prepended to a filename.  The client procedure is as
 *	follows:
 *
 *	int
 *	(*proc)(name, cdata)
 *	    char *name;		/# A directory in the path prepended to
 *				 # a file name.
 *				 #/
 *	    ClientData *cdata;	/# Provided by caller #/
 *	{
 *	}
 *
 *	The client procedure should return 0 normally, or 1 to abort
 *	the path enumeration.  If a directory in the search path
 *	refers to a non-existent user name (using the ~user syntax),
 *	we skip that component.
 *
 * Results:
 *	Returns 0 if all the clients returned 0, or 1 if
 *	some client returned 1.  When a client returns 1
 *	we abort the enumeration.
 *
 * Side effects:
 *	Calls the client procedure.
 *
 * ----------------------------------------------------------------------------
 */

int
PaEnum(path, file, proc, cdata)
    char *path;		/* Search path */
    char *file;		/* Each element of the search path is prepended to
			 * this file name and passed to the client.
			 */
    int (*proc)();	/* Client procedure */
    ClientData cdata;	/* Passed to (*proc)() */
{
    char component[MAXSIZE], *next;

    while (next = nextName(&path, file, component, sizeof component))
	if (next[0] && (*proc)(next, cdata))
	    return (1);

    return (0);
}
