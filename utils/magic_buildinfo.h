/*
 * magic_buildinfo.h --
 *
 * Public declarations for the build-information globals: magic's version,
 * revision, git commit, and compile date/time.
 *
 * These are the ONLY symbols initialized from the -DMAGIC_VERSION /
 * -DMAGIC_REVISION / -DMAGIC_COMMIT / -DMAGIC_BUILDDATE command-line defines.
 * They are defined in exactly one translation unit, utils/buildinfo.c, which is
 * the only unit compiled with those defines.  Every other unit reads the values
 * through these externs, so the volatile build-date/commit values stay off every
 * other compile command line (which keeps ccache/sccache keys stable and the
 * baked-in date/commit consistent across the whole binary).
 */

#ifndef _MAGIC_BUILDINFO_H
#define _MAGIC_BUILDINFO_H

extern char *MagicVersion;	/* e.g. "8.3"  (major.minor) */
extern char *MagicRevision;	/* e.g. "670"  (patch/revision) */
extern char *MagicCommit;	/* git commit SHA, or "" if unavailable */
extern char *MagicCompileTime;	/* build date/time, or "" if disabled */

#endif /* _MAGIC_BUILDINFO_H */
