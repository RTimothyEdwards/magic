/*
 * magic.h --
 *
 * Global definitions for all MAGIC modules
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
 * rcsid="$Header"
 */

#ifndef _MAGIC_H
#define	_MAGIC_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>

/* ------------------- Universal pointer typecast --------------------- */

/* Set default value for backwards compatibility with non-autoconf make */
#ifndef SIZEOF_VOID_P
#define SIZEOF_VOID_P SIZEOF_UNSIGNED_INT
#endif

#if SIZEOF_VOID_P == SIZEOF_UNSIGNED_INT
typedef unsigned int pointertype;
typedef signed int spointertype;
#elif SIZEOF_VOID_P == SIZEOF_UNSIGNED_LONG
typedef unsigned long pointertype;
typedef signed long spointertype;
#else
ERROR: Cannot compile without knowing the size of a pointer.  See utils/magic.h
#endif

typedef int64_t dlong;
#define DLONG_MAX INT64_MAX
#define DLONG_MIN INT64_MIN
#define DLONG_PREFIX "ll"

/* --------------------- Universal pointer type ----------------------- */

#ifndef _CLIENTDATA
typedef pointertype ClientData;
#endif

/* --------------------------- Booleans ------------------------------- */

typedef unsigned char bool;

#ifndef TRUE
#define	TRUE	((bool)1)
#endif
#ifndef FALSE
#define	FALSE	((bool)0)
#endif

/* ----------------------- Simple functions --------------------------- */

#ifndef	MAX
#define MAX(a,b)	(((a) < (b)) ? (b) : (a))
#endif

#ifndef	MIN
#define MIN(a,b)	(((a) > (b)) ? (b) : (a))
#endif

#define	ABS(x)		(((x) >= 0)  ? (x) : -(x))
#define	ABSDIFF(x,y)	(((x) < (y)) ? (y) - (x) : (x) - (y))
#define ODD(i)		(i&1)
#define EVEN(i)		(!(i&1))

/*----------------------------------------------------------------------*/
/* Round to nearest integer---use c99 functions if available from the	*/
/* math library (checked by "configure"), otherwise use the macro	*/
/* definitions below.							*/
/*----------------------------------------------------------------------*/

#ifndef HAVE_ROUND
#define round(a) (((a) < 0) ? (int)((a) - 0.5) : (int)((a) + 0.5))
#endif

#ifndef HAVE_ROUNDF
#define roundf(x) ((float)((int)((float)(x) + ((x < 0) ? -0.5 : 0.5))))
#endif

/* -------------------------- Search paths ---------------------------- */

extern char *CellLibPath;	/* Path for finding cells. */
extern char *SysLibPath;	/* Path for finding system
				 * files like color maps, styles, etc.
				 */

/* --------------------- Debugging and assertions --------------------- */

/* To enable assertions, undefine NDEBUG in file defs.mak */

#include <assert.h>
#define	ASSERT(p, where) assert(p)	/* "where" is ignored */

/* ------------------------ Malloc/free ------------------------------- */

/*
 * Magic has its own versions of malloc() and free(), called mallocMagic()
 * and freeMagic().  Magic procedures should ONLY use these procedures.
 * Just for the sake of robustness, though, we define malloc and free
 * here to error strings.
 */
#define	malloc	You_should_use_the_Magic_procedure_mallocMagic_instead
#define	free	You_should_use_the_Magic_procedure_freeMagic_instead
#define calloc	You_should_use_the_Magic_procedure_callocMagic_instead

/* ---------- Flag for global variables (for readability) ------------- */

#define	global	/* Nothing */

/* ------------ Globally-used strings. -------------------------------- */

extern char *MagicVersion;
extern char *MagicRevision;
extern char *MagicCompileTime;
extern char AbortMessage[];

/* ---------------- Start of Machine Configuration Section ----------------- */

    /* ------- Configuration:  Handle Missing Routines/Definitions ------- */

/* System V is missing some BSDisms. */
#ifdef SYSV
# ifndef bcopy
#  define bcopy(a, b, c)	memcpy(b, a, c)
# endif
# ifndef bzero
#  define bzero(a, b)		memset(a, 0, b)
# endif
# ifndef bcmp
#  define bcmp(a, b, c)		memcmp(b, a, c)
# endif
#endif

/* Some machines need vfprintf().  (A temporary MIPS bug?) (see txOutput.c) */
#if 	(defined(MIPSEB) && defined(SYSTYPE_BSD43)) || ibm032
# define	NEED_VFPRINTF
#endif

/* Some machines expect signal handlers to return an "int".  But most machines
 * expect them to return a "void".  If your machine expects an "int", put in
 * an "ifdef" below.
 */

#if 	(defined(MIPSEB) && defined(SYSTYPE_BSD43)) || ibm032
# define	SIG_RETURNS_INT
#endif

/*
 * Linux
 */
#ifdef	linux
#define       sigvec          sigaction
#define       sv_handler      sa_handler
#endif

/*
 * Irix 
 */
#ifdef sgi
#define vfork fork
#endif


/*
 * Select system call
 *
 * 	Note:  Errors here may be caused by not including <sys/types.h> 
 *	before "magic.h" (deprecated; more modern usage is <sys/select.h>
 */
#include <sys/select.h>
#ifndef FD_SET
#define fd_set int
#define FD_SET(n, p)    ((*(p)) |= (1 << (n)))
#define FD_CLR(n, p)    ((*(p)) &= ~(1 << (n)))
#define FD_ISSET(n, p)  ((*(p)) & (1 << (n)))
#define FD_ZERO(p)      (*(p) = 0)
#endif

/*
 * Handling of VA_COPY.  These variables are set by the configuration
 * script.  Some systems define va_copy, some define __va_copy, and
 * some don't define it at all.  It is assumed that systems which do
 * not define it at all allow arguments to be copied with "=".
 */

#ifndef HAVE_VA_COPY
  #ifdef HAVE___VA_COPY
    #define va_copy(a, b) __va_copy(a, b)
  #else
    #define va_copy(a, b) a = b
  #endif
#endif

/* ------------------ End of Machine Configuration Section ----------------- */

#endif /* _MAGIC_H */
