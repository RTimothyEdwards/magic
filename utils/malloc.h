/*
 * malloc.h --
 *
 *	See malloc.c for a description of magic's allocation functions.
 *	Magic's built-in malloc() function has been removed.
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/utils/malloc.h,v 1.2 2009/09/10 20:32:55 tim Exp $"
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

#ifndef _MAGIC__UTILS__MALLOC_H
#define _MAGIC__UTILS__MALLOC_H

#include <stdlib.h>


/* build time configuration check */
#if (!defined(SUPPORT_DIRECT_MALLOC) && defined(SUPPORT_REMOVE_MALLOC_LEGACY))
 #error "ERROR: Unspported build configuration SUPPORT_REMOVE_MALLOC_LEGACY is defined but SUPPORT_DIRECT_MALLOC is undefined"
#endif


#ifdef SUPPORT_DIRECT_MALLOC

#define mallocMagic malloc
#define callocMagic calloc
#define freeMagic free

#else

extern void *mallocMagicLegacy(size_t);
#define mallocMagic(size) mallocMagicLegacy(size)

/* renamed like this, so there is no performance loss if the byte count
 *  can be computed at compile time.
 */
extern void *callocMagicLegacy(size_t);
#define callocMagic(nmemb, size) callocMagicLegacy((nmemb) * (size))

extern void freeMagicLegacy(void *);
#define freeMagic(ptr) freeMagicLegacy(ptr)

#endif /* SUPPORT_DIRECT_MALLOC */


typedef void* free_magic1_t;

/* TODO this should be moved to autoconf/build/toolchain detection, this does exist
 * in autoconf and in another changeset, so I come back and fixup/remove this later.
 */
#if (defined(__STDC__) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
 /* C99 or later */
 #ifndef __inline__
   /* you'd have thought on linux this was enabled already, TODO check bplane module inlines */
   #define __inline__ inline
 #endif
#endif

#if (!defined(_MAGIC__UTILS__MALLOC_H__NOINLINE) && defined(__inline__))

/* TODO this (__extern_inline__) should be moved to autoconf/build/toolchain detection */
#define __extern_inline__ inline

/*
 *  NOTICE: inline form, keep in sync with malloc.c copied
 */
__extern_inline__ free_magic1_t freeMagic1_init() {
    return NULL;
}
__extern_inline__ void freeMagic1(free_magic1_t* m1, void* ptr) {
    if(*m1) /* this if() is here to help inliner remove the call to free() when it can */
    {
#if (defined(SUPPORT_DIRECT_MALLOC) || defined(SUPPORT_REMOVE_MALLOC_LEGACY))
	free(*m1); /* no need for NULL check with free() */
#else
	freeMagicLegacy(*m1);
#endif
    }
    *m1 = ptr;
}
__extern_inline__ void freeMagic1_end(free_magic1_t* m1) {
    if(*m1) /* this if() is here to help inliner remove the call to free() when it can */
    {
#if (defined(SUPPORT_DIRECT_MALLOC) || defined(SUPPORT_REMOVE_MALLOC_LEGACY))
	free(*m1); /* no need for NULL check with free() */
#else
	freeMagicLegacy(*m1);
#endif
    }
}

#else

#define freeMagic1_init() freeMagic1_init_func()
#define freeMagic1(m1, ptr) freeMagic1_func((m1), (ptr))
#define freeMagic1_end(m1) freeMagic1_end_func((m1))

#endif /* !_MAGIC__UTILS__MALLOC_H__NOINLINE && __inline__ */

/* we'll emit a function call interface just in case a platform won't inline and we can redirect */
extern free_magic1_t freeMagic1_init_func(void);
extern void freeMagic1_func(free_magic1_t* m1, void* ptr);
extern void freeMagic1_end_func(free_magic1_t* m1);

#endif /* _MAGIC__UTILS__MALLOC_H */
