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

#ifndef _MAGIC__UTILS__MAGIC_H
#define	_MAGIC__UTILS__MAGIC_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "utils/magic_assert.h"

/* ------------------- Universal pointer typecast --------------------- */

/* Set default value for backwards compatibility with non-autoconf make */
#ifndef SIZEOF_VOID_P
#define SIZEOF_VOID_P SIZEOF_UNSIGNED_INT
#endif

#if SIZEOF_VOID_P == SIZEOF_UNSIGNED_LONG
typedef unsigned long pointertype;
typedef signed long spointertype;
#elif SIZEOF_VOID_P == SIZEOF_UNSIGNED_INT
typedef unsigned int pointertype;
typedef signed int spointertype;
#else
ERROR: Cannot compile without knowing the size of a pointer.  See utils/magic.h
#endif

typedef int64_t dlong;
#define DLONG_MAX INT64_MAX
#define DLONG_MIN INT64_MIN
#if (defined(__x86_64__) && !defined(_WIN64))
/* gcc x86_64 defines int64_t as 'long int' on LP64 */
#define DLONG_PREFIX "l"
#else
/* for 32bit and 64bit LLP64 (_WIN64) systems */
#define DLONG_PREFIX "ll"
#endif

/* --------------------- Universal pointer type ----------------------- */

#ifndef _CLIENTDATA
// #ifdef MAGIC_WRAPPER
//#error "ClientData type is not defined, but we are building with TCL support, so we expect TCL to provide this type definition"
// #endif
 #ifndef NO_VOID
typedef void *ClientData;
 #else
typedef pointertype ClientData;
 #endif
#define _CLIENTDATA
#endif

/* this is not the (int) C type, but the conceptual difference between
 *  a pointer and an integer.  The integer width uses same size as pointer
 *  width, so integer width truncations need to be applied at usage site.
 */
#define CD2PTR(cd)   ((void*)(cd))
#define CD2INT(cd)   ((pointertype)(cd))
#define CD2FUN(cd)   ((const void*)(cd))

#define PTR2CD(data) ((ClientData)(data))
#define INT2CD(data) ((ClientData)(pointertype)(data))
#define FUN2CD(data) ((ClientData)(const void*)(data))

/* --------------------------- Booleans ------------------------------- */

#include "utils/magic_stdbool.h"

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

/* ------------------------ Malloc/free ------------------------------- */

/*
 * Magic has its own versions of malloc() and free(), called mallocMagic()
 * and freeMagic().  Magic procedures should ONLY use these procedures.
 * Just for the sake of robustness, though, we define malloc and free
 * here to error strings.
 */
#ifndef SUPPORT_DIRECT_MALLOC
#define	malloc	You_should_use_the_Magic_procedure_mallocMagic_instead
#define	free	You_should_use_the_Magic_procedure_freeMagic_instead
#define calloc	You_should_use_the_Magic_procedure_callocMagic_instead
#endif

/* ---------- Flag for global variables (for readability) ------------- */

#define	global	/* Nothing */

/* ------------ Globally-used strings. -------------------------------- */

extern char *MagicVersion;
extern char *MagicRevision;
extern char *MagicCompileTime;
extern char AbortMessage[];

/* ------------ zlib (compression) support -------------------------------- */

#ifdef HAVE_ZLIB
    #define magicFOPEN    gzopen
    #define FCLOSE   gzclose
    #define FGETC    gzgetc
    #define magicFREAD(a,b,c,d)    gzread(d,a,b*c)
    #define FEOF     gzeof
    #define FSEEK    gzseek
    #define FTELL    gztell
    #define REWIND   gzrewind
    #define FILETYPE gzFile
    #define OFFTYPE  z_off_t
#else
    #define magicFOPEN    fopen
    #define FCLOSE   fclose
    #define FGETC    getc
    #define magicFREAD    fread
    #define FEOF     feof
    #define FSEEK    fseek
    #define FTELL    ftello
    #define REWIND   rewind
    #define FILETYPE FILE *
    #define OFFTYPE  off_t
    #define PaZOpen  PaOpen
    #define PaLockZOpen  PaLockOpen
#endif

/* ------------ modern compiler support -------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
 #define ATTR_FORMAT_PRINTF_1 __attribute__((format (printf,1,2)))
 #define ATTR_FORMAT_PRINTF_2 __attribute__((format (printf,2,3)))
 #define ATTR_SENTINEL __attribute__ ((sentinel))
 #define ATTR_UNREACHABLE __builtin_unreachable()
 #define ATTR_NORETURN __attribute__((noreturn))
 #define __unused__(x) x __attribute__((unused))

 #define ANALYSER_CSTRING(n) __attribute__((null_terminated_string_arg(n)))
 #define ANALYSER_FD_ARG(fd) __attribute__((fd_arg(fd)))
 #define ANALYSER_MALLOC(dealloc, idx) __attribute__((malloc, malloc(dealloc, idx)))
 /* looking to squash excessive -Wpedantic warnings ? add into defs.mak: CPPFLAGS += -Wno-variadic-macros */
 #define ANALYSER_NONNULL(n...) __attribute__((nonnull(n)))
 #define ANALYSER_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
 #define ATTR_FORMAT_PRINTF_1 /* */
 #define ATTR_FORMAT_PRINTF_2 /* */
 #define ATTR_SENTINEL /* */
 #define ATTR_UNREACHABLE /* */
 #define ATTR_NORETURN /* */
 #define __unused__(x) x

 #define ANALYSER_CSTRING(n) /* */
 #define ANALYSER_FD_ARG(fd) /* */
 #define ANALYSER_MALLOC(dealloc, idx) /* */
 #define ANALYSER_NONNULL(n...) /* */
 #define ANALYSER_RETURNS_NONNULL /* */
#endif

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

#endif /* _MAGIC__UTILS__MAGIC_H */
