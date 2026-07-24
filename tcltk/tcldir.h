/*
 * tcldir.h --
 *
 * Resolve magic's Tcl-runtime directory (where magic.tcl, tclmagic.so, and the
 * Tcl auto_path packages live) at *run time* instead of hardwiring the
 * compile-time install path.
 *
 * If the environment variable CAD_ROOT is set and non-empty, use
 * $CAD_ROOT/magic/tcl -- so magic runs from a relocated install, or straight out
 * of a build tree (via a staged CAD_ROOT), the same way the sys files already
 * follow CAD_ROOT and the tcltk/magic.sh wrapper already computes
 * TCL_MAG_DIR=${CAD_ROOT}/magic/tcl.  Otherwise fall back to the compile-time
 * TCL_DIR (the default install location).
 *
 * Must be included in a translation unit compiled with -DTCL_DIR.
 */

#ifndef _MAGIC_TCLDIR_H
#define _MAGIC_TCLDIR_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>

/* Platform path-length limit.  <limits.h> declares PATH_MAX when the system has
 * a fixed limit (4096 on Linux); provide a safe fallback when it does not. */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
 * Write magic's Tcl-runtime directory into buf (of the given size), with the
 * optional suffix appended as a path component -- the '/' directory separator is
 * supplied automatically, so pass "magic.tcl", not "/magic.tcl" (a NULL or empty
 * suffix appends nothing).  A caller that wants "<dir>/magic.tcl" thus gets it in
 * one call instead of a second snprintf.  Return the number of characters written
 * -- snprintf semantics, as a size_t -- or 0 on a (very unlikely) snprintf
 * encoding error.  The assert catches truncation: snprintf returns the length it
 * *would* have written, so a return of >= size means the path did not fit.
 * Callers should likewise assert the returned length < their buffer size.
 */
static inline size_t
MagicTclDir(char *buf, size_t size, const char *suffix)
{
    const char *cad = getenv("CAD_ROOT");
    const char *sep;
    int n;

    if (suffix == NULL) suffix = "";
    sep = (*suffix != '\0') ? "/" : "";		/* imply the '/' separator */

    if (cad != NULL && *cad != '\0')
	n = snprintf(buf, size, "%s/magic/tcl%s%s", cad, sep, suffix);
    else
	n = snprintf(buf, size, "%s%s%s", TCL_DIR, sep, suffix);

    if (n < 0) n = 0;			/* coerce encoding error to 0, assert below */
    assert((size_t)n < size);
    return (size_t)n;
}

#endif /* _MAGIC_TCLDIR_H */
