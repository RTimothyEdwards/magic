/* utils.h --
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
 * This file just defines all the features available from the
 * Magic utility routines.
 */

/* rcsid "$Header: /usr/cvsroot/magic-8.0/utils/utils.h,v 1.2 2009/09/10 20:32:55 tim Exp $" */

#ifndef _UTILS_H
#define _UTILS_H 1

#include "utils/magic.h"

/*
 * Cast second argument to LookupStruct() to (LookupTable *) to
 * make lint very happy.
 */
typedef struct
{
    char *d_str;
} LookupTable;

/* The following stuff just defines the global routines provided
 * by files other than hash and stack and geometry.
 */

extern int Lookup();
extern int LookupAny(char, char **);
extern int LookupFull(char *, char **);
extern int LookupStruct();
extern int LookupStructFull();
extern int PaExpand(char **, char **, int);
extern char *nextName();
extern FILE *PaOpen(char *, char *, char *, char *, char *, char **);
extern FILE *PaLockOpen(char *, char *, char *, char *, char *, char **, bool *);
extern char *StrDup(char **, char *);
extern int Match();
extern char *ArgStr();
extern bool StrIsWhite(char *, bool);
extern bool StrIsInt(char *);
extern bool StrIsNumeric(char *);

extern int SetNoisyBool(bool *, char *, FILE *);

#ifdef FILE_LOCKS
extern FILE *flock_open();
#endif

/* The following macro takes an integer and returns another integer that
 * is the same as the first except that all the '1' bits are turned off,
 * except for the rightmost '1' bit.
 *
 * Examples:	01010100 --> 00000100
 *		1111 --> 0001
 *		0111011100 --> 0000000100
 */
#define	LAST_BIT_OF(x)	((x) & ~((x) - 1))

extern float MagAtof();

extern int Wait();
extern int WaitPid();


#define FORK_f(pid) do { pid = fork(); if (pid > 0) ForkChildAdd (pid); } while (0)
#define FORK_vf(pid) do { pid = vfork(); if (pid > 0) ForkChildAdd (pid); } while (0)

#if  defined(SYSV) || defined(CYGWIN) || defined(__APPLE__)

#define FORK(pid) FORK_f(pid)

#else

#define FORK(pid) FORK_vf(pid)

#endif

#endif /* _UTILS_H */
