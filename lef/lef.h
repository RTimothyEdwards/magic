/*
 * lef.h --
 *
 * Contains definitions for things that are exported by the
 * lef module.
 *
 */

#ifndef _MAGIC__LEF__LEF_H
#define _MAGIC__LEF__LEF_H

#include "utils/magic.h"

/* Procedures for reading the technology file: */

extern void LefTechInit(void);
extern bool LefTechLine(const char *sectionName, int argc, char *argv[]);
extern void LefTechScale(int scalen, int scaled);
extern void LefTechSetDefaults(void);

/* Initialization: */

extern void LefInit(void);

#endif /* _MAGIC__LEF__LEF_H */
