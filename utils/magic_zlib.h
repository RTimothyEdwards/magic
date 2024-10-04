/* zlib.h --
 *
 * Routines relating to the external dependency zlib
 *
 *     *********************************************************************
 *     * Copyright (C) 2022, 2024 Regents of the University of California. *
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

#ifndef _MAGIC__UTILS__MAGIC_ZLIB_H
#define _MAGIC__UTILS__MAGIC_ZLIB_H

#ifdef HAVE_ZLIB

#include <zlib.h>

extern gzFile PaZOpen(const char *file, const char *mode, const char *ext, const char *path, const char *library,
                      char **pRealName);
extern gzFile PaLockZOpen(const char *file, const char *mode, const char *ext, const char *path, const char *library,
                          char **pRealName, bool *is_locked, int *fdp);
extern char *PaCheckCompressed(const char *filename);

#ifdef FILE_LOCKS
extern gzFile flock_zopen();
#endif

#endif

#endif /* _MAGIC__UTILS__MAGIC_ZLIB_H */
