/*
 *  utils/magic_alloca.h
 *
 *  Copyright (C) 1985, 1990 Regents of the University of California.
 *
 *  Permission to use, copy, modify, and distribute this
 *  software and its documentation for any purpose and without
 *  fee is hereby granted, provided that the above copyright
 *  notice appear in all copies.  The University of California
 *  makes no representations about the suitability of this
 *  software for any purpose.  It is provided "as is" without
 *  express or implied warranty.  Export of this software outside
 *  of the United States of America may require an export license.
 *
 *  SPDX-License-Identifier: HPND-UC-export-US
 *
 *
 *  This file contains code snippets that are part of the GNU autoconf 2.69
 *  project documentation.  This project is licensed under GPLv3 with a
 *  Section 7 clause outlined in autoconf-2.69/COPYING.EXCEPTION.
 *
 *  The documentation for the autoconf project is licensed under GNU Free
 *  Documentation License Version 1.3 or any later version published by the
 *  Free Software Foundation.  The snippets in here are taken entirely from the
 *  documentation so fall under GFDL-1.3-or-later terms.
 *  The documentation published by the FSF can be seen at
 *   https://www.gnu.org/savannah-checkouts/gnu/autoconf/manual/autoconf-2.69/autoconf.html
 *  This section seeks to provide attribution by providing references to the source
 *  a link to the full license text and the required boilerplate text directly below:
 *
 *  Copyright (C) 1992-1996, 1998-2012 Free Software Foundation, Inc.
 *
 *    Permission is granted to copy, distribute and/or modify this
 *    document under the terms of the GNU Free Documentation License,
 *    Version 1.3 or any later version published by the Free Software
 *    Foundation; with no Invariant Sections, no Front-Cover texts, and
 *    no Back-Cover Texts.  A copy of the license is included in the
 *    section entitled "GNU Free Documentation License."
 *
 *  A full copy of the GNU Free Documentation License version 1.3 is available at:
 *    https://www.gnu.org/licenses/fdl-1.3.html
 *  SPDX-License-Identifier: GFDL-1.3-or-later
 *
 *
 *  All other lines in this file are licensed under the HPND-UC-export-US license
 *  at the top of this file inline with the main project licensing.
 *
 */

#ifndef _MAGIC__ALLOCA_H
#define _MAGIC__ALLOCA_H

/* taken from autoconf documentation:
 *    https://www.gnu.org/software/autoconf/manual/autoconf-2.69/html_node/Particular-Functions.html
 *
 * the reason this header exists is to help hide this ugliness and prevent copy-and-paste error etc..
 *
 * #include "utils/magic_alloca.h"
 */
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif !defined alloca
# ifdef __GNUC__
#  define alloca __builtin_alloca
# elif defined _AIX
#  define alloca __alloca
# elif defined _MSC_VER
#  include <malloc.h>
#  define alloca _alloca
# elif !defined HAVE_ALLOCA
#  ifdef  __cplusplus
extern "C"
#  endif
void *alloca (size_t);
# endif
#endif

#endif /* _MAGIC__ALLOCA_H */
