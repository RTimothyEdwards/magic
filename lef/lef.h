/*
 * lef.h --
 *
 * Contains definitions for things that are exported by the
 * lef module.
 *
 */

#ifndef _LEF_H
#define _LEF_H

#include "utils/magic.h"

/* Procedures for reading the technology file: */

extern void LefTechInit(void);
extern bool LefTechLine(char *sectionName, int argc, char *argv[]);
extern void LefTechScale(int scalen, int scaled);
extern void LefTechSetDefaults(void);

/* Initialization: */

extern void LefInit(void);

#endif /* _LEF_H */
