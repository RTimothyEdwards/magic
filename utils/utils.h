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
#include "utils/tech.h"

/*
 * Cast second argument to LookupStruct() to (LookupTable *) to
 * make lint very happy.
 */
typedef struct
{
    const char *d_str;
} LookupTable;

/* The following stuff just defines the global routines provided
 * by files other than hash and stack and geometry.
 */

extern int Lookup(const char *str, const char * const *table);
extern int LookupAny(char, const char * const *);
extern int LookupFull(const char *, const char * const *);
extern int LookupStruct(const char *str, const LookupTable *table_start, int size);
extern int LookupStructFull(const char *str, const char * const *table, int size);
extern int PaExpand(const char **, char **, int);
extern char *nextName(const char **ppath, const char *file, char *dest, int size);
extern FILE *PaOpen(const char *file, const char *mode, const char *ext, const char *path, const char *library,
                    char **pRealName);
extern FILE *PaLockOpen(const char *file, const char *mode, const char *ext, const char *path, const char *library,
                        char **pRealName, bool *is_locked, int *fdp);
extern char *StrDup(char **, const char *);
extern bool Match(const char *pattern, const char *string);
extern char *ArgStr(int *pargc, char ***pargv, const char *argType);
extern bool StrIsWhite(const char *, bool);
extern bool StrIsInt(const char *);
extern bool StrIsNumeric(const char *);

/* C99 compat */
extern void PaAppend(char **pathptr, const char *newstring);
extern void ReduceFraction(int *, int *);
extern bool TechLoad(char *, SectionID);
extern void UndoFlush();
extern int  FindGCF();
extern int  GetRect();
extern void niceabort();
extern void ShowRect();
extern void FindDisplay();
extern void ForkChildAdd();
extern int  PaEnum(const char *path, const char *file, int (*proc)(), ClientData cdata);
extern int  paVisitProcess();
extern void SetNoisyInt(int *parm, const char *valueS, FILE *file);
extern void SetNoisyDI(dlong *parm, const char *valueS, FILE *file);
extern bool ParsSplit();

extern int SetNoisyBool(bool *parm, const char *valueS, FILE *file);

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

extern float MagAtof(const char *s);

extern int Wait(int *status);
extern int WaitPid(int pid, int *status);


#define FORK_f(pid) do { pid = fork(); if (pid > 0) ForkChildAdd (pid); } while (0)
#define FORK_vf(pid) do { pid = vfork(); if (pid > 0) ForkChildAdd (pid); } while (0)

#if  defined(SYSV) || defined(CYGWIN) || defined(__APPLE__)

#define FORK(pid) FORK_f(pid)

#else

#define FORK(pid) FORK_vf(pid)

#endif

#endif /* _UTILS_H */
