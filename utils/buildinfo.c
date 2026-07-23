/*
 * buildinfo.c --
 *
 * The single translation unit that captures the build-information defines.
 *
 * This is the ONLY file compiled with -DMAGIC_VERSION / -DMAGIC_REVISION /
 * -DMAGIC_COMMIT / -DMAGIC_BUILDDATE (see the buildinfo.o recipe in
 * utils/Makefile.in).  Isolating them here keeps those defines -- especially the
 * ever-changing MAGIC_BUILDDATE -- off every other compile command line, so
 * ccache/sccache keys stay stable and the version/date/commit baked into the
 * binary are consistent rather than smeared across whenever each object last
 * recompiled.
 *
 * Consumers include "utils/magic_buildinfo.h" and read these externs.
 */

#include "utils/magic_buildinfo.h"

char *MagicVersion = MAGIC_VERSION;
char *MagicRevision = MAGIC_REVISION;
char *MagicCommit = MAGIC_COMMIT;

/*
 * MAGIC_BUILDDATE may be disabled at configure time (--disable-magic-builddate)
 * for reproducible builds; when it is, MagicCompileTime reports the empty string.
 */
#ifdef MAGIC_NO_BUILDDATE
char *MagicCompileTime = "";
#else
char *MagicCompileTime = MAGIC_BUILDDATE;
#endif
